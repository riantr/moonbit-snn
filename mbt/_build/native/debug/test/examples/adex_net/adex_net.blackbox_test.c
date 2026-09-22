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
struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt4AdEx;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25adex__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TWRPC15error5ErrorEs;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191;

struct _M0TUdiE;

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186;

struct _M0TP26RiantR8snn__mbt9AdExModel;

struct _M0BTPB6Logger;

struct _M0TPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25adex__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt18SpikingSynapseAdExE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt11MonitorAdEx;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt4AdExE;

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx;

struct _M0TUddE;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25adex__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TP26RiantR8snn__mbt13AdExPostSpike {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
};

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TP26RiantR8snn__mbt9AdExModel {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt4AdExE* $0;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt18SpikingSynapseAdExE* $1;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* $2;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25adex__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0TPB8MutLocalGfE {
  float $0;
  
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

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
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

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1198(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1191(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1186(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1163(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1156(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25adex__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
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

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0MP26RiantR8snn__mbt18SpikingSynapseAdEx6random(
  struct _M0TP26RiantR8snn__mbt4AdEx*,
  struct _M0TP26RiantR8snn__mbt4AdEx*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
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

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0MPC15array5Array2atGRP26RiantR8snn__mbt11MonitorAdExE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE*,
  int32_t
);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

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

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*
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

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t*,
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t*,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t*,
  int32_t,
  int32_t*,
  int32_t,
  int32_t
);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

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

double cos(double);

moonbit_string_t* moonbit_rt_get_cli_args();

double sin(double);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_41 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[115]; 
} const moonbit_string_literal_42 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 114, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 97, 100, 101, 120, 95, 110, 101, 
    116, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 
    105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 97, 100, 101, 120, 95, 110, 101, 
    116, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 
    69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
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

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1198$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1198
  };

uint32_t const moonbit_layout_table_data[88] =
  {
    sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186)
    / 4, 1,
    offsetof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186, $1)
    / 4
    * 2,
    sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191)
    / 4, 1,
    offsetof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx, $3) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx, $3) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2514
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1219,
  moonbit_string_t _M0L8filenameS1188,
  int32_t _M0L5indexS1190
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186* _closure_2547;
  struct _M0TWEu* _M0L13handle__startS1186;
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191* _closure_2548;
  struct _M0TWssbEu* _M0L14handle__resultS1191;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1198;
  void* _M0L11_2atry__errS1213;
  struct moonbit_result_0 _tmp_2550;
  int32_t _handle__error__result_2551;
  int32_t _M0L6_2atmpS2502;
  void* _M0L3errS1214;
  moonbit_string_t _M0L4nameS1216;
  struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1217;
  moonbit_string_t _M0L7_2anameS1218;
  int32_t _M0L6_2acntS2541;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1188);
  _closure_2547
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186*)moonbit_malloc(sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186));
  Moonbit_object_header(_closure_2547)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2547->code
  = &_M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1186;
  _closure_2547->$0 = _M0L5indexS1190;
  _closure_2547->$1 = _M0L8filenameS1188;
  _M0L13handle__startS1186 = (struct _M0TWEu*)_closure_2547;
  moonbit_incref_cycle_free(_M0L8filenameS1188);
  _closure_2548
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191*)moonbit_malloc(sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191));
  Moonbit_object_header(_closure_2548)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2548->code
  = &_M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1191;
  _closure_2548->$0 = _M0L5indexS1190;
  _closure_2548->$1 = _M0L8filenameS1188;
  _M0L14handle__resultS1191 = (struct _M0TWssbEu*)_closure_2548;
  _M0L17error__to__stringS1198
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1198$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2550
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1219, _M0L8filenameS1188, _M0L5indexS1190, _M0L13handle__startS1186, _M0L14handle__resultS1191, _M0L17error__to__stringS1198);
  if (_tmp_2550.tag) {
    int32_t const _M0L5_2aokS2511 = _tmp_2550.data.ok;
    _handle__error__result_2551 = _M0L5_2aokS2511;
  } else {
    void* const _M0L6_2aerrS2512 = _tmp_2550.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
    moonbit_decref_cycle_free(_M0L13handle__startS1186);
    _M0L11_2atry__errS1213 = _M0L6_2aerrS2512;
    goto join_1212;
  }
  if (_handle__error__result_2551) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
    moonbit_decref_cycle_free(_M0L13handle__startS1186);
    _M0L6_2atmpS2502 = 1;
  } else {
    struct moonbit_result_0 _tmp_2552;
    int32_t _handle__error__result_2553;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2552
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1219, _M0L8filenameS1188, _M0L5indexS1190, _M0L13handle__startS1186, _M0L14handle__resultS1191, _M0L17error__to__stringS1198);
    if (_tmp_2552.tag) {
      int32_t const _M0L5_2aokS2509 = _tmp_2552.data.ok;
      _handle__error__result_2553 = _M0L5_2aokS2509;
    } else {
      void* const _M0L6_2aerrS2510 = _tmp_2552.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
      moonbit_decref_cycle_free(_M0L13handle__startS1186);
      _M0L11_2atry__errS1213 = _M0L6_2aerrS2510;
      goto join_1212;
    }
    if (_handle__error__result_2553) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
      moonbit_decref_cycle_free(_M0L13handle__startS1186);
      _M0L6_2atmpS2502 = 1;
    } else {
      struct moonbit_result_0 _tmp_2554;
      int32_t _handle__error__result_2555;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2554
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1219, _M0L8filenameS1188, _M0L5indexS1190, _M0L13handle__startS1186, _M0L14handle__resultS1191, _M0L17error__to__stringS1198);
      if (_tmp_2554.tag) {
        int32_t const _M0L5_2aokS2507 = _tmp_2554.data.ok;
        _handle__error__result_2555 = _M0L5_2aokS2507;
      } else {
        void* const _M0L6_2aerrS2508 = _tmp_2554.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
        moonbit_decref_cycle_free(_M0L13handle__startS1186);
        _M0L11_2atry__errS1213 = _M0L6_2aerrS2508;
        goto join_1212;
      }
      if (_handle__error__result_2555) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
        moonbit_decref_cycle_free(_M0L13handle__startS1186);
        _M0L6_2atmpS2502 = 1;
      } else {
        struct moonbit_result_0 _tmp_2556;
        int32_t _handle__error__result_2557;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2556
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1219, _M0L8filenameS1188, _M0L5indexS1190, _M0L13handle__startS1186, _M0L14handle__resultS1191, _M0L17error__to__stringS1198);
        if (_tmp_2556.tag) {
          int32_t const _M0L5_2aokS2505 = _tmp_2556.data.ok;
          _handle__error__result_2557 = _M0L5_2aokS2505;
        } else {
          void* const _M0L6_2aerrS2506 = _tmp_2556.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
          moonbit_decref_cycle_free(_M0L13handle__startS1186);
          _M0L11_2atry__errS1213 = _M0L6_2aerrS2506;
          goto join_1212;
        }
        if (_handle__error__result_2557) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
          moonbit_decref_cycle_free(_M0L13handle__startS1186);
          _M0L6_2atmpS2502 = 1;
        } else {
          struct moonbit_result_0 _tmp_2558;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2558
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1219, _M0L8filenameS1188, _M0L5indexS1190, _M0L13handle__startS1186, _M0L14handle__resultS1191, _M0L17error__to__stringS1198);
          moonbit_decref_cycle_free(_M0L13handle__startS1186);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1198);
          if (_tmp_2558.tag) {
            int32_t const _M0L5_2aokS2503 = _tmp_2558.data.ok;
            _M0L6_2atmpS2502 = _M0L5_2aokS2503;
          } else {
            void* const _M0L6_2aerrS2504 = _tmp_2558.data.err;
            _M0L11_2atry__errS1213 = _M0L6_2aerrS2504;
            goto join_1212;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2502) {
    void* _M0L128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2513 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2513)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2513)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1213
    = _M0L128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2513;
    goto join_1212;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1191);
  }
  goto joinlet_2549;
  join_1212:;
  _M0L3errS1214 = _M0L11_2atry__errS1213;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1217
  = (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1214;
  _M0L7_2anameS1218 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1217->$0;
  _M0L6_2acntS2541
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1217));
  if (_M0L6_2acntS2541 > 1) {
    int32_t _M0L11_2anew__cntS2542 = _M0L6_2acntS2541 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1217), _M0L11_2anew__cntS2542);
    moonbit_incref_cycle_free(_M0L7_2anameS1218);
  } else if (_M0L6_2acntS2541 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1217);
  }
  _M0L4nameS1216 = _M0L7_2anameS1218;
  goto join_1215;
  goto joinlet_2559;
  join_1215:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1191(_M0L14handle__resultS1191, _M0L4nameS1216, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1191);
  moonbit_decref_cycle_free(_M0L4nameS1216);
  joinlet_2559:;
  joinlet_2549:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1198(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2501,
  void* _M0L3errS1199
) {
  void* _M0L1eS1201;
  moonbit_string_t _M0L1eS1203;
  moonbit_string_t _result_2562;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1199)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1204 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1199;
      moonbit_string_t _M0L4_2aeS1205 = _M0L10_2aFailureS1204->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1205);
      _M0L1eS1203 = _M0L4_2aeS1205;
      goto join_1202;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1206 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1199;
      moonbit_string_t _M0L4_2aeS1207 = _M0L15_2aInspectErrorS1206->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1207);
      _M0L1eS1203 = _M0L4_2aeS1207;
      goto join_1202;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1208 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1199;
      moonbit_string_t _M0L4_2aeS1209 = _M0L16_2aSnapshotErrorS1208->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1209);
      _M0L1eS1203 = _M0L4_2aeS1209;
      goto join_1202;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1210 =
        (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1199;
      moonbit_string_t _M0L4_2aeS1211 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1210->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1211);
      _M0L1eS1203 = _M0L4_2aeS1211;
      goto join_1202;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1199);
      _M0L1eS1201 = _M0L3errS1199;
      goto join_1200;
      break;
    }
  }
  join_1202:;
  return _M0L1eS1203;
  join_1200:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2562 = _M0FP15Error10to__string(_M0L1eS1201);
  moonbit_decref_cycle_free(_M0L1eS1201);
  return _result_2562;
}

int32_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1191(
  struct _M0TWssbEu* _M0L6_2aenvS2498,
  moonbit_string_t _M0L10__testnameS1192,
  moonbit_string_t _M0L7messageS1193,
  int32_t _M0L7skippedS1194
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191* _M0L14_2acasted__envS2499;
  moonbit_string_t _M0L8filenameS1188;
  int32_t _M0L5indexS1190;
  moonbit_string_t _M0L10file__nameS1195;
  moonbit_string_t _M0L7messageS1196;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1197;
  moonbit_string_t _M0L6_2atmpS2500;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2499
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1191*)_M0L6_2aenvS2498;
  _M0L8filenameS1188 = _M0L14_2acasted__envS2499->$1;
  _M0L5indexS1190 = _M0L14_2acasted__envS2499->$0;
  if (!_M0L7skippedS1194 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1195
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1188, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1196
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1193, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1197
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1197, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1197, _M0L10file__nameS1195);
  moonbit_decref_cycle_free(_M0L10file__nameS1195);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1197, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1197, _M0L5indexS1190);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1197, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1197, _M0L7messageS1196);
  moonbit_decref_cycle_free(_M0L7messageS1196);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1197, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2500
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1197);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1197);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2500);
  moonbit_decref_cycle_free(_M0L6_2atmpS2500);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1186(
  struct _M0TWEu* _M0L6_2aenvS2495
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186* _M0L14_2acasted__envS2496;
  moonbit_string_t _M0L8filenameS1188;
  int32_t _M0L5indexS1190;
  moonbit_string_t _M0L10file__nameS1187;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1189;
  moonbit_string_t _M0L6_2atmpS2497;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2496
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fadex__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1186*)_M0L6_2aenvS2495;
  _M0L8filenameS1188 = _M0L14_2acasted__envS2496->$1;
  _M0L5indexS1190 = _M0L14_2acasted__envS2496->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1187
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1188, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1189
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1189, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1189, _M0L10file__nameS1187);
  moonbit_decref_cycle_free(_M0L10file__nameS1187);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1189, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1189, _M0L5indexS1190);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1189, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2497
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1189);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1189);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2497);
  moonbit_decref_cycle_free(_M0L6_2atmpS2497);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1156;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1163;
  struct _M0TUsiE** _M0L6_2atmpS2494;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1170;
  moonbit_string_t* _M0L9cli__argsS1171;
  moonbit_string_t _M0L6_2atmpS2493;
  moonbit_string_t _M0L6_2atmpS2492;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1172;
  int32_t _M0L7_2abindS1173;
  moonbit_string_t* _M0L7_2abindS1174;
  int32_t _M0L6_2acntS2543;
  int32_t _M0L2__S1175;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1156 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1163 = 0;
  _M0L6_2atmpS2494 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1170
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1170)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1170->$0 = _M0L6_2atmpS2494;
  _M0L16file__and__indexS1170->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1171
  = _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1171)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2493 = (moonbit_string_t)_M0L9cli__argsS1171[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2493);
  moonbit_decref_cycle_free(_M0L9cli__argsS1171);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2492
  = _M0MP46RiantR8snn__mbt8examples25adex__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2493);
  moonbit_decref_cycle_free(_M0L6_2atmpS2493);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1172
  = _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1163(_M0L51moonbit__test__driver__internal__split__mbt__stringS1163, _M0L6_2atmpS2492, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2492);
  _M0L7_2abindS1173 = _M0L10test__argsS1172->$1;
  _M0L7_2abindS1174 = _M0L10test__argsS1172->$0;
  _M0L6_2acntS2543
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1172));
  if (_M0L6_2acntS2543 > 1) {
    int32_t _M0L11_2anew__cntS2544 = _M0L6_2acntS2543 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1172), _M0L11_2anew__cntS2544);
    moonbit_incref_cycle_free(_M0L7_2abindS1174);
  } else if (_M0L6_2acntS2543 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1172);
  }
  _M0L2__S1175 = 0;
  while (1) {
    if (_M0L2__S1175 < _M0L7_2abindS1173) {
      moonbit_string_t _M0L3argS1176 =
        (moonbit_string_t)_M0L7_2abindS1174[_M0L2__S1175];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1177;
      moonbit_string_t _M0L4fileS1178;
      moonbit_string_t _M0L5rangeS1179;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1180;
      moonbit_string_t _M0L6_2atmpS2490;
      int32_t _M0L5startS1181;
      moonbit_string_t _M0L6_2atmpS2489;
      int32_t _M0L3endS1182;
      int32_t _M0L1iS1183;
      int32_t _M0L6_2atmpS2491;
      moonbit_incref_cycle_free(_M0L3argS1176);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1177
      = _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1163(_M0L51moonbit__test__driver__internal__split__mbt__stringS1163, _M0L3argS1176, 58);
      moonbit_decref_cycle_free(_M0L3argS1176);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1178
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1177, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1179
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1177, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1177);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1180
      = _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1163(_M0L51moonbit__test__driver__internal__split__mbt__stringS1163, _M0L5rangeS1179, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1179);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2490
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1180, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1181
      = _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1156(_M0L45moonbit__test__driver__internal__parse__int__S1156, _M0L6_2atmpS2490);
      moonbit_decref_cycle_free(_M0L6_2atmpS2490);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2489
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1180, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1180);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1182
      = _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1156(_M0L45moonbit__test__driver__internal__parse__int__S1156, _M0L6_2atmpS2489);
      moonbit_decref_cycle_free(_M0L6_2atmpS2489);
      _M0L1iS1183 = _M0L5startS1181;
      while (1) {
        if (_M0L1iS1183 < _M0L3endS1182) {
          struct _M0TUsiE* _M0L8_2atupleS2487;
          int32_t _M0L6_2atmpS2488;
          moonbit_incref_cycle_free(_M0L4fileS1178);
          _M0L8_2atupleS2487
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2487)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2487->$0 = _M0L4fileS1178;
          _M0L8_2atupleS2487->$1 = _M0L1iS1183;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1170, _M0L8_2atupleS2487);
          _M0L6_2atmpS2488 = _M0L1iS1183 + 1;
          _M0L1iS1183 = _M0L6_2atmpS2488;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1178);
        }
        break;
      }
      _M0L6_2atmpS2491 = _M0L2__S1175 + 1;
      _M0L2__S1175 = _M0L6_2atmpS2491;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1174);
    }
    break;
  }
  return _M0L16file__and__indexS1170;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1163(
  int32_t _M0L6_2aenvS2468,
  moonbit_string_t _M0L1sS1164,
  int32_t _M0L3sepS1165
) {
  moonbit_string_t* _M0L6_2atmpS2486;
  struct _M0TPB5ArrayGsE* _M0L3resS1166;
  struct _M0TPB8MutLocalGiE* _M0L1iS1167;
  struct _M0TPB8MutLocalGiE* _M0L5startS1168;
  int32_t _M0L3valS2481;
  int32_t _M0L6_2atmpS2482;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2486 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1166
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1166)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1166->$0 = _M0L6_2atmpS2486;
  _M0L3resS1166->$1 = 0;
  _M0L1iS1167
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1167)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1167->$0 = 0;
  _M0L5startS1168
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1168)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1168->$0 = 0;
  while (1) {
    int32_t _M0L3valS2469 = _M0L1iS1167->$0;
    int32_t _M0L6_2atmpS2470 = Moonbit_array_length(_M0L1sS1164);
    if (_M0L3valS2469 < _M0L6_2atmpS2470) {
      int32_t _M0L3valS2473 = _M0L1iS1167->$0;
      int32_t _M0L6_2atmpS2472;
      int32_t _M0L6_2atmpS2471;
      int32_t _M0L3valS2480;
      int32_t _M0L6_2atmpS2479;
      if (
        _M0L3valS2473 < 0
        || _M0L3valS2473 >= Moonbit_array_length(_M0L1sS1164)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2472 = _M0L1sS1164[_M0L3valS2473];
      _M0L6_2atmpS2471 = _M0L6_2atmpS2472;
      if (_M0L6_2atmpS2471 == _M0L3sepS1165) {
        int32_t _M0L3valS2475 = _M0L5startS1168->$0;
        int32_t _M0L3valS2476 = _M0L1iS1167->$0;
        moonbit_string_t _M0L6_2atmpS2474;
        int32_t _M0L3valS2478;
        int32_t _M0L6_2atmpS2477;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2474
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1164, _M0L3valS2475, _M0L3valS2476);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1166, _M0L6_2atmpS2474);
        _M0L3valS2478 = _M0L1iS1167->$0;
        _M0L6_2atmpS2477 = _M0L3valS2478 + 1;
        _M0L5startS1168->$0 = _M0L6_2atmpS2477;
      }
      _M0L3valS2480 = _M0L1iS1167->$0;
      _M0L6_2atmpS2479 = _M0L3valS2480 + 1;
      _M0L1iS1167->$0 = _M0L6_2atmpS2479;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1167);
    }
    break;
  }
  _M0L3valS2481 = _M0L5startS1168->$0;
  _M0L6_2atmpS2482 = Moonbit_array_length(_M0L1sS1164);
  if (_M0L3valS2481 < _M0L6_2atmpS2482) {
    int32_t _M0L3valS2484 = _M0L5startS1168->$0;
    int32_t _M0L6_2atmpS2485;
    moonbit_string_t _M0L6_2atmpS2483;
    moonbit_decref_cycle_free(_M0L5startS1168);
    _M0L6_2atmpS2485 = Moonbit_array_length(_M0L1sS1164);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2483
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1164, _M0L3valS2484, _M0L6_2atmpS2485);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1166, _M0L6_2atmpS2483);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1168);
  }
  return _M0L3resS1166;
}

int32_t _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1156(
  int32_t _M0L6_2aenvS2461,
  moonbit_string_t _M0L1sS1157
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1158;
  int32_t _M0L3lenS1159;
  int32_t _M0L7_2abindS1160;
  int32_t _M0L1iS1161;
  int32_t _result_2567;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1158
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1158)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1158->$0 = 0;
  _M0L3lenS1159 = Moonbit_array_length(_M0L1sS1157);
  _M0L7_2abindS1160 = 0;
  _M0L1iS1161 = _M0L7_2abindS1160;
  while (1) {
    if (_M0L1iS1161 < _M0L3lenS1159) {
      int32_t _M0L3valS2466 = _M0L3resS1158->$0;
      int32_t _M0L6_2atmpS2463 = _M0L3valS2466 * 10;
      int32_t _M0L6_2atmpS2465;
      int32_t _M0L6_2atmpS2464;
      int32_t _M0L6_2atmpS2462;
      int32_t _M0L6_2atmpS2467;
      if (
        _M0L1iS1161 < 0 || _M0L1iS1161 >= Moonbit_array_length(_M0L1sS1157)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2465 = _M0L1sS1157[_M0L1iS1161];
      _M0L6_2atmpS2464 = _M0L6_2atmpS2465 - 48;
      _M0L6_2atmpS2462 = _M0L6_2atmpS2463 + _M0L6_2atmpS2464;
      _M0L3resS1158->$0 = _M0L6_2atmpS2462;
      _M0L6_2atmpS2467 = _M0L1iS1161 + 1;
      _M0L1iS1161 = _M0L6_2atmpS2467;
      continue;
    }
    break;
  }
  _result_2567 = _M0L3resS1158->$0;
  moonbit_decref_cycle_free(_M0L3resS1158);
  return _result_2567;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25adex__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1155
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1155);
  return _M0L4selfS1155;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1125,
  moonbit_string_t _M0L12_2adiscard__S1126,
  int32_t _M0L12_2adiscard__S1127,
  struct _M0TWEu* _M0L12_2adiscard__S1128,
  struct _M0TWssbEu* _M0L12_2adiscard__S1129,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1130
) {
  struct moonbit_result_0 _result_2568;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2568.tag = 1;
  _result_2568.data.ok = 0;
  return _result_2568;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1131,
  moonbit_string_t _M0L12_2adiscard__S1132,
  int32_t _M0L12_2adiscard__S1133,
  struct _M0TWEu* _M0L12_2adiscard__S1134,
  struct _M0TWssbEu* _M0L12_2adiscard__S1135,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1136
) {
  struct moonbit_result_0 _result_2569;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2569.tag = 1;
  _result_2569.data.ok = 0;
  return _result_2569;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1137,
  moonbit_string_t _M0L12_2adiscard__S1138,
  int32_t _M0L12_2adiscard__S1139,
  struct _M0TWEu* _M0L12_2adiscard__S1140,
  struct _M0TWssbEu* _M0L12_2adiscard__S1141,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1142
) {
  struct moonbit_result_0 _result_2570;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2570.tag = 1;
  _result_2570.data.ok = 0;
  return _result_2570;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1143,
  moonbit_string_t _M0L12_2adiscard__S1144,
  int32_t _M0L12_2adiscard__S1145,
  struct _M0TWEu* _M0L12_2adiscard__S1146,
  struct _M0TWssbEu* _M0L12_2adiscard__S1147,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1148
) {
  struct moonbit_result_0 _result_2571;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2571.tag = 1;
  _result_2571.data.ok = 0;
  return _result_2571;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1149,
  moonbit_string_t _M0L12_2adiscard__S1150,
  int32_t _M0L12_2adiscard__S1151,
  struct _M0TWEu* _M0L12_2adiscard__S1152,
  struct _M0TWssbEu* _M0L12_2adiscard__S1153,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1154
) {
  struct moonbit_result_0 _result_2572;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2572.tag = 1;
  _result_2572.data.ok = 0;
  return _result_2572;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1124
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx3new(
  int32_t _M0L1nS1103,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L5paramS1104,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1105
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2460;
  struct _M0TP26RiantR8snn__mbt4AdEx* _result_2573;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6_2atmpS2460 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _result_2573
  = _M0MP26RiantR8snn__mbt4AdEx16new__with__spike(_M0L1nS1103, _M0L5paramS1104, _M0L6_2atmpS2460, _M0L3rngS1105);
  moonbit_decref_cycle_free(_M0L6_2atmpS2460);
  return _result_2573;
}

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx16new__with__spike(
  int32_t _M0L1nS1081,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L5paramS1083,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS1102,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1086
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1080;
  float _M0L2vtS2458;
  float _M0L2vrS2459;
  float _M0L6spreadS1082;
  int32_t _M0L7_2abindS1084;
  int32_t _M0L1kS1085;
  struct _M0TPB5ArrayGfE* _M0L1wS1088;
  struct _M0TPB5ArrayGbE* _M0L4fireS1089;
  float _M0L2vtS2457;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS1090;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1091;
  struct _M0TPB5ArrayGfE* _M0L1iS1092;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1093;
  struct _M0TPB5ArrayGfE* _M0L2geS1094;
  struct _M0TPB5ArrayGfE* _M0L2giS1095;
  struct _M0TPB5ArrayGfE* _M0L2heS1096;
  struct _M0TPB5ArrayGfE* _M0L2hiS1097;
  struct _M0TPB5ArrayGfE* _M0L3gluS1098;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1099;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1100;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1101;
  struct _M0TP26RiantR8snn__mbt4AdEx* _block_2575;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1vS1080 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  _M0L2vtS2458 = _M0L5paramS1083->$2;
  _M0L2vrS2459 = _M0L5paramS1083->$3;
  _M0L6spreadS1082 = _M0L2vtS2458 - _M0L2vrS2459;
  _M0L7_2abindS1084 = 0;
  _M0L1kS1085 = _M0L7_2abindS1084;
  while (1) {
    if (_M0L1kS1085 < _M0L1nS1081) {
      float _M0L2vrS2453 = _M0L5paramS1083->$3;
      float _M0L6_2atmpS2455;
      float _M0L6_2atmpS2454;
      float _M0L6_2atmpS2452;
      int32_t _M0L6_2atmpS2456;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2455 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1086);
      _M0L6_2atmpS2454 = _M0L6_2atmpS2455 * _M0L6spreadS1082;
      _M0L6_2atmpS2452 = _M0L2vrS2453 + _M0L6_2atmpS2454;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1080, _M0L1kS1085, _M0L6_2atmpS2452);
      _M0L6_2atmpS2456 = _M0L1kS1085 + 1;
      _M0L1kS1085 = _M0L6_2atmpS2456;
      continue;
    }
    break;
  }
  #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1wS1088 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4fireS1089 = _M0MPC15array5Array4makeGbE(_M0L1nS1081, 0);
  _M0L2vtS2457 = _M0L5paramS1083->$2;
  #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L9thresholdS1090
  = _M0MPC15array5Array4makeGfE(_M0L1nS1081, _M0L2vtS2457);
  #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4tabsS1091 = _M0MPC15array5Array4makeGiE(_M0L1nS1081, 1);
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1iS1092 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L9syn__currS1093 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2geS1094 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2giS1095 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2heS1096 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2hiS1097 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L3gluS1098 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4gabaS1099 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x0p+0f);
  #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7gsyn__eS1100 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x1p+0f);
  #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7gsyn__iS1101 = _M0MPC15array5Array4makeGfE(_M0L1nS1081, 0x1p+0f);
  moonbit_incref_cycle_free(_M0L5paramS1083);
  moonbit_incref_cycle_free(_M0L5spikeS1102);
  _block_2575
  = (struct _M0TP26RiantR8snn__mbt4AdEx*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4AdEx));
  Moonbit_object_header(_block_2575)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2575->$0 = _M0L5paramS1083;
  _block_2575->$1 = _M0L5spikeS1102;
  _block_2575->$2 = _M0L1nS1081;
  _block_2575->$3 = _M0L1vS1080;
  _block_2575->$4 = _M0L1wS1088;
  _block_2575->$5 = _M0L4fireS1089;
  _block_2575->$6 = _M0L9thresholdS1090;
  _block_2575->$7 = _M0L4tabsS1091;
  _block_2575->$8 = _M0L1iS1092;
  _block_2575->$9 = _M0L9syn__currS1093;
  _block_2575->$10 = _M0L2geS1094;
  _block_2575->$11 = _M0L2giS1095;
  _block_2575->$12 = _M0L2heS1096;
  _block_2575->$13 = _M0L2hiS1097;
  _block_2575->$14 = _M0L3gluS1098;
  _block_2575->$15 = _M0L4gabaS1099;
  _block_2575->$16 = _M0L7gsyn__eS1100;
  _block_2575->$17 = _M0L7gsyn__iS1101;
  _block_2575->$18 = 0x0p+0f;
  _block_2575->$19 = -0x1.2cp+6f;
  _block_2575->$20 = 0x1p+0f;
  _block_2575->$21 = 0x1.8p+2f;
  _block_2575->$22 = 0x1p-1f;
  _block_2575->$23 = 0x1p+1f;
  return _block_2575;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS1076;
  float _M0L2glS1077;
  float _M0L2tmS1078;
  float _M0L1rS1079;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_2576;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS1076 = 0x1.19p+8f;
  _M0L2glS1077 = 0x1.4p+5f;
  _M0L2tmS1078 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS1079 = 0x1p+0f / 0x1.4p+5f;
  _block_2576
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_2576)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2576->$0 = _M0L1cS1076;
  _block_2576->$1 = _M0L2glS1077;
  _block_2576->$2 = -0x1.9p+5f;
  _block_2576->$3 = -0x1.1a66666666666p+6f;
  _block_2576->$4 = -0x1.1a66666666666p+6f;
  _block_2576->$5 = _M0L2tmS1078;
  _block_2576->$6 = _M0L1rS1079;
  _block_2576->$7 = 0x1p+1f;
  _block_2576->$8 = 0x1.2p+7f;
  _block_2576->$9 = 0x1p+2f;
  _block_2576->$10 = 0x1.42p+6f;
  return _block_2576;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_2577;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_2577
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_2577)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2577->$0 = 0x0p+0f;
  _block_2577->$1 = 0x1.4p+3f;
  _block_2577->$2 = 0x1.4p+3f;
  _block_2577->$3 = 0x1p+0f;
  _block_2577->$4 = 0x1p+0f;
  return _block_2577;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt14adex__sim__for(
  struct _M0TP26RiantR8snn__mbt9AdExModel* _M0L5modelS1067,
  float _M0L8durationS1065
) {
  float _M0L2dtS1062;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS1063;
  float _M0L6_2atmpS2451;
  int32_t _M0L5stepsS1064;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* _M0L7_2abindS1066;
  int32_t _M0L7_2abindS1068;
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0L7_2abindS1069;
  int32_t _M0L2__S1070;
  int32_t _M0L7_2abindS1073;
  int32_t _M0L2__S1074;
  #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L2dtS1062 = 0x1p-3f;
  #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L4timeS1063 = _M0MP26RiantR8snn__mbt4Time3new();
  #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt7set__dt(_M0L4timeS1063, _M0L2dtS1062);
  _M0L6_2atmpS2451 = _M0L8durationS1065 / _M0L2dtS1062;
  #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L5stepsS1064 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2451);
  _M0L7_2abindS1066 = _M0L5modelS1067->$2;
  _M0L7_2abindS1068 = _M0L7_2abindS1066->$1;
  _M0L7_2abindS1069 = _M0L7_2abindS1066->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1069);
  _M0L2__S1070 = 0;
  while (1) {
    if (_M0L2__S1070 < _M0L7_2abindS1068) {
      struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0L1mS1071 =
        (struct _M0TP26RiantR8snn__mbt11MonitorAdEx*)_M0L7_2abindS1069[
          _M0L2__S1070
        ];
      int32_t _M0L6_2atmpS2449;
      moonbit_incref_cycle_free(_M0L1mS1071);
      #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17record__one__adex(_M0L1mS1071, 0x0p+0f);
      moonbit_decref_cycle_free(_M0L1mS1071);
      _M0L6_2atmpS2449 = _M0L2__S1070 + 1;
      _M0L2__S1070 = _M0L6_2atmpS2449;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1069);
    }
    break;
  }
  _M0L7_2abindS1073 = 0;
  _M0L2__S1074 = _M0L7_2abindS1073;
  while (1) {
    if (_M0L2__S1074 < _M0L5stepsS1064) {
      int32_t _M0L6_2atmpS2450;
      #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17step__adex__model(_M0L5modelS1067, _M0L4timeS1063);
      _M0L6_2atmpS2450 = _M0L2__S1074 + 1;
      _M0L2__S1074 = _M0L6_2atmpS2450;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4timeS1063);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17step__adex__model(
  struct _M0TP26RiantR8snn__mbt9AdExModel* _M0L5modelS1042,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS1049
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt18SpikingSynapseAdExE* _M0L7_2abindS1041;
  int32_t _M0L7_2abindS1043;
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx** _M0L7_2abindS1044;
  int32_t _M0L2__S1045;
  float _M0L2dtS1048;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt4AdExE* _M0L7_2abindS1050;
  int32_t _M0L7_2abindS1051;
  struct _M0TP26RiantR8snn__mbt4AdEx** _M0L7_2abindS1052;
  int32_t _M0L2__S1053;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* _M0L7_2abindS1056;
  int32_t _M0L7_2abindS1057;
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0L7_2abindS1058;
  int32_t _M0L2__S1059;
  #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS1041 = _M0L5modelS1042->$1;
  _M0L7_2abindS1043 = _M0L7_2abindS1041->$1;
  _M0L7_2abindS1044 = _M0L7_2abindS1041->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1044);
  _M0L2__S1045 = 0;
  while (1) {
    if (_M0L2__S1045 < _M0L7_2abindS1043) {
      struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0L1cS1046 =
        (struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx*)_M0L7_2abindS1044[
          _M0L2__S1045
        ];
      int32_t _M0L6_2atmpS2444;
      moonbit_incref_cycle_free(_M0L1cS1046);
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt22forward__adex__synapse(_M0L1cS1046);
      moonbit_decref_cycle_free(_M0L1cS1046);
      _M0L6_2atmpS2444 = _M0L2__S1045 + 1;
      _M0L2__S1045 = _M0L6_2atmpS2444;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1044);
    }
    break;
  }
  _M0L2dtS1048 = _M0L4timeS1049->$2;
  _M0L7_2abindS1050 = _M0L5modelS1042->$0;
  _M0L7_2abindS1051 = _M0L7_2abindS1050->$1;
  _M0L7_2abindS1052 = _M0L7_2abindS1050->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1052);
  _M0L2__S1053 = 0;
  while (1) {
    if (_M0L2__S1053 < _M0L7_2abindS1051) {
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1054 =
        (struct _M0TP26RiantR8snn__mbt4AdEx*)_M0L7_2abindS1052[_M0L2__S1053];
      int32_t _M0L6_2atmpS2445;
      moonbit_incref_cycle_free(_M0L1pS1054);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1pS1054, _M0L2dtS1048);
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1pS1054);
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt10step__adex(_M0L1pS1054, _M0L2dtS1048);
      moonbit_decref_cycle_free(_M0L1pS1054);
      _M0L6_2atmpS2445 = _M0L2__S1053 + 1;
      _M0L2__S1053 = _M0L6_2atmpS2445;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1052);
    }
    break;
  }
  _M0L7_2abindS1056 = _M0L5modelS1042->$2;
  _M0L7_2abindS1057 = _M0L7_2abindS1056->$1;
  _M0L7_2abindS1058 = _M0L7_2abindS1056->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1058);
  _M0L2__S1059 = 0;
  while (1) {
    if (_M0L2__S1059 < _M0L7_2abindS1057) {
      struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0L1mS1060 =
        (struct _M0TP26RiantR8snn__mbt11MonitorAdEx*)_M0L7_2abindS1058[
          _M0L2__S1059
        ];
      struct _M0TPB5ArrayGfE* _M0L1tS2447 = _M0L4timeS1049->$0;
      float _M0L6_2atmpS2446;
      int32_t _M0L6_2atmpS2448;
      moonbit_incref_cycle_free(_M0L1mS1060);
      #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0L6_2atmpS2446 = _M0MPC15array5Array2atGfE(_M0L1tS2447, 0);
      #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17record__one__adex(_M0L1mS1060, _M0L6_2atmpS2446);
      moonbit_decref_cycle_free(_M0L1mS1060);
      _M0L6_2atmpS2448 = _M0L2__S1059 + 1;
      _M0L2__S1059 = _M0L6_2atmpS2448;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1058);
    }
    break;
  }
  #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS1049, _M0L2dtS1048);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10step__adex(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1020,
  float _M0L2dtS1035
) {
  int32_t _M0L1nS1019;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1021;
  float _M0L2tmS1022;
  float _M0L2vtS1023;
  float _M0L2vrS1024;
  float _M0L2elS1025;
  float _M0L1rS1026;
  float _M0L9dt__slopeS1027;
  float _M0L2twS1028;
  float _M0L1aS1029;
  float _M0L1bS1030;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2443;
  float _M0L2atS1031;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2442;
  float _M0L6tau__aS1032;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2441;
  float _M0L11tabs__constS1033;
  float _M0L6_2atmpS2440;
  int32_t _M0L11tabs__stepsS1034;
  int32_t _M0L7_2abindS1036;
  int32_t _M0L1iS1037;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1019 = _M0L1pS1020->$2;
  _M0L3p__S1021 = _M0L1pS1020->$0;
  _M0L2tmS1022 = _M0L3p__S1021->$5;
  _M0L2vtS1023 = _M0L3p__S1021->$2;
  _M0L2vrS1024 = _M0L3p__S1021->$3;
  _M0L2elS1025 = _M0L3p__S1021->$4;
  _M0L1rS1026 = _M0L3p__S1021->$6;
  _M0L9dt__slopeS1027 = _M0L3p__S1021->$7;
  _M0L2twS1028 = _M0L3p__S1021->$8;
  _M0L1aS1029 = _M0L3p__S1021->$9;
  _M0L1bS1030 = _M0L3p__S1021->$10;
  _M0L5spikeS2443 = _M0L1pS1020->$1;
  _M0L2atS1031 = _M0L5spikeS2443->$0;
  _M0L5spikeS2442 = _M0L1pS1020->$1;
  _M0L6tau__aS1032 = _M0L5spikeS2442->$1;
  _M0L5spikeS2441 = _M0L1pS1020->$1;
  _M0L11tabs__constS1033 = _M0L5spikeS2441->$3;
  _M0L6_2atmpS2440 = _M0L11tabs__constS1033 / _M0L2dtS1035;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1034 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2440);
  _M0L7_2abindS1036 = 0;
  _M0L1iS1037 = _M0L7_2abindS1036;
  while (1) {
    if (_M0L1iS1037 < _M0L1nS1019) {
      struct _M0TPB5ArrayGfE* _M0L1vS2353 = _M0L1pS1020->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS2355 = _M0L1pS1020->$5;
      float _M0L6_2atmpS2354;
      struct _M0TPB5ArrayGbE* _M0L4fireS2357;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2358;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2361;
      int32_t _M0L6_2atmpS2360;
      int32_t _M0L6_2atmpS2359;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2363;
      int32_t _M0L6_2atmpS2362;
      struct _M0TPB5ArrayGfE* _M0L1wS2364;
      struct _M0TPB5ArrayGfE* _M0L1wS2376;
      float _M0L6_2atmpS2366;
      struct _M0TPB5ArrayGfE* _M0L1vS2375;
      float _M0L6_2atmpS2374;
      float _M0L6_2atmpS2373;
      float _M0L6_2atmpS2370;
      struct _M0TPB5ArrayGfE* _M0L1wS2372;
      float _M0L6_2atmpS2371;
      float _M0L6_2atmpS2369;
      float _M0L6_2atmpS2368;
      float _M0L6_2atmpS2367;
      float _M0L6_2atmpS2365;
      float _M0L9exp__termS1040;
      struct _M0TPB5ArrayGfE* _M0L1vS2377;
      struct _M0TPB5ArrayGfE* _M0L1vS2399;
      float _M0L6_2atmpS2379;
      struct _M0TPB5ArrayGfE* _M0L1vS2398;
      float _M0L6_2atmpS2397;
      float _M0L6_2atmpS2396;
      float _M0L6_2atmpS2395;
      float _M0L6_2atmpS2391;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2394;
      float _M0L6_2atmpS2393;
      float _M0L6_2atmpS2392;
      float _M0L6_2atmpS2387;
      struct _M0TPB5ArrayGfE* _M0L1wS2390;
      float _M0L6_2atmpS2389;
      float _M0L6_2atmpS2388;
      float _M0L6_2atmpS2383;
      struct _M0TPB5ArrayGfE* _M0L1iS2386;
      float _M0L6_2atmpS2385;
      float _M0L6_2atmpS2384;
      float _M0L6_2atmpS2382;
      float _M0L6_2atmpS2381;
      float _M0L6_2atmpS2380;
      float _M0L6_2atmpS2378;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2400;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2408;
      float _M0L6_2atmpS2402;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2407;
      float _M0L6_2atmpS2406;
      float _M0L6_2atmpS2405;
      float _M0L6_2atmpS2404;
      float _M0L6_2atmpS2403;
      float _M0L6_2atmpS2401;
      struct _M0TPB5ArrayGbE* _M0L4fireS2409;
      struct _M0TPB5ArrayGfE* _M0L1vS2412;
      float _M0L6_2atmpS2411;
      int32_t _M0L6_2atmpS2410;
      struct _M0TPB5ArrayGfE* _M0L1vS2413;
      struct _M0TPB5ArrayGbE* _M0L4fireS2415;
      float _M0L6_2atmpS2414;
      struct _M0TPB5ArrayGfE* _M0L1wS2417;
      struct _M0TPB5ArrayGbE* _M0L4fireS2419;
      float _M0L6_2atmpS2418;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2423;
      struct _M0TPB5ArrayGbE* _M0L4fireS2425;
      float _M0L6_2atmpS2424;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2429;
      struct _M0TPB5ArrayGbE* _M0L4fireS2431;
      int32_t _M0L6_2atmpS2430;
      int32_t _M0L6_2atmpS2352;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2355, _M0L1iS1037)) {
        _M0L6_2atmpS2354 = _M0L2vrS1024;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2356 = _M0L1pS1020->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2354
        = _M0MPC15array5Array2atGfE(_M0L1vS2356, _M0L1iS1037);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2353, _M0L1iS1037, _M0L6_2atmpS2354);
      _M0L4fireS2357 = _M0L1pS1020->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2357, _M0L1iS1037, 0);
      _M0L4tabsS2358 = _M0L1pS1020->$7;
      _M0L4tabsS2361 = _M0L1pS1020->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2360
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2361, _M0L1iS1037);
      _M0L6_2atmpS2359 = _M0L6_2atmpS2360 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2358, _M0L1iS1037, _M0L6_2atmpS2359);
      _M0L4tabsS2363 = _M0L1pS1020->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2362
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2363, _M0L1iS1037);
      if (_M0L6_2atmpS2362 > 0) {
        goto join_1038;
      }
      _M0L1wS2364 = _M0L1pS1020->$4;
      _M0L1wS2376 = _M0L1pS1020->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2366 = _M0MPC15array5Array2atGfE(_M0L1wS2376, _M0L1iS1037);
      _M0L1vS2375 = _M0L1pS1020->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2374 = _M0MPC15array5Array2atGfE(_M0L1vS2375, _M0L1iS1037);
      _M0L6_2atmpS2373 = _M0L6_2atmpS2374 - _M0L2elS1025;
      _M0L6_2atmpS2370 = _M0L1aS1029 * _M0L6_2atmpS2373;
      _M0L1wS2372 = _M0L1pS1020->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2371 = _M0MPC15array5Array2atGfE(_M0L1wS2372, _M0L1iS1037);
      _M0L6_2atmpS2369 = _M0L6_2atmpS2370 - _M0L6_2atmpS2371;
      _M0L6_2atmpS2368 = _M0L2dtS1035 * _M0L6_2atmpS2369;
      _M0L6_2atmpS2367 = _M0L6_2atmpS2368 / _M0L2twS1028;
      _M0L6_2atmpS2365 = _M0L6_2atmpS2366 + _M0L6_2atmpS2367;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS2364, _M0L1iS1037, _M0L6_2atmpS2365);
      if (_M0L9dt__slopeS1027 < 0x0p+0f) {
        _M0L9exp__termS1040 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2439 = _M0L1pS1020->$3;
        float _M0L6_2atmpS2436;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2438;
        float _M0L6_2atmpS2437;
        float _M0L6_2atmpS2435;
        float _M0L6_2atmpS2434;
        float _M0L6_2atmpS2433;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2436
        = _M0MPC15array5Array2atGfE(_M0L1vS2439, _M0L1iS1037);
        _M0L9thresholdS2438 = _M0L1pS1020->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2437
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2438, _M0L1iS1037);
        _M0L6_2atmpS2435 = _M0L6_2atmpS2436 - _M0L6_2atmpS2437;
        _M0L6_2atmpS2434 = _M0L6_2atmpS2435 / _M0L9dt__slopeS1027;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2433 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2434);
        _M0L9exp__termS1040 = _M0L9dt__slopeS1027 * _M0L6_2atmpS2433;
      }
      _M0L1vS2377 = _M0L1pS1020->$3;
      _M0L1vS2399 = _M0L1pS1020->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2379 = _M0MPC15array5Array2atGfE(_M0L1vS2399, _M0L1iS1037);
      _M0L1vS2398 = _M0L1pS1020->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2397 = _M0MPC15array5Array2atGfE(_M0L1vS2398, _M0L1iS1037);
      _M0L6_2atmpS2396 = _M0L6_2atmpS2397 - _M0L2elS1025;
      _M0L6_2atmpS2395 = -_M0L6_2atmpS2396;
      _M0L6_2atmpS2391 = _M0L6_2atmpS2395 + _M0L9exp__termS1040;
      _M0L9syn__currS2394 = _M0L1pS1020->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2393
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2394, _M0L1iS1037);
      _M0L6_2atmpS2392 = _M0L1rS1026 * _M0L6_2atmpS2393;
      _M0L6_2atmpS2387 = _M0L6_2atmpS2391 - _M0L6_2atmpS2392;
      _M0L1wS2390 = _M0L1pS1020->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2389 = _M0MPC15array5Array2atGfE(_M0L1wS2390, _M0L1iS1037);
      _M0L6_2atmpS2388 = _M0L1rS1026 * _M0L6_2atmpS2389;
      _M0L6_2atmpS2383 = _M0L6_2atmpS2387 - _M0L6_2atmpS2388;
      _M0L1iS2386 = _M0L1pS1020->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2385 = _M0MPC15array5Array2atGfE(_M0L1iS2386, _M0L1iS1037);
      _M0L6_2atmpS2384 = _M0L1rS1026 * _M0L6_2atmpS2385;
      _M0L6_2atmpS2382 = _M0L6_2atmpS2383 + _M0L6_2atmpS2384;
      _M0L6_2atmpS2381 = _M0L2dtS1035 * _M0L6_2atmpS2382;
      _M0L6_2atmpS2380 = _M0L6_2atmpS2381 / _M0L2tmS1022;
      _M0L6_2atmpS2378 = _M0L6_2atmpS2379 + _M0L6_2atmpS2380;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2377, _M0L1iS1037, _M0L6_2atmpS2378);
      _M0L9thresholdS2400 = _M0L1pS1020->$6;
      _M0L9thresholdS2408 = _M0L1pS1020->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2402
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2408, _M0L1iS1037);
      _M0L9thresholdS2407 = _M0L1pS1020->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2406
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2407, _M0L1iS1037);
      _M0L6_2atmpS2405 = _M0L2vtS1023 - _M0L6_2atmpS2406;
      _M0L6_2atmpS2404 = _M0L2dtS1035 * _M0L6_2atmpS2405;
      _M0L6_2atmpS2403 = _M0L6_2atmpS2404 / _M0L6tau__aS1032;
      _M0L6_2atmpS2401 = _M0L6_2atmpS2402 + _M0L6_2atmpS2403;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2400, _M0L1iS1037, _M0L6_2atmpS2401);
      _M0L4fireS2409 = _M0L1pS1020->$5;
      _M0L1vS2412 = _M0L1pS1020->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2411 = _M0MPC15array5Array2atGfE(_M0L1vS2412, _M0L1iS1037);
      _M0L6_2atmpS2410 = _M0L6_2atmpS2411 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2409, _M0L1iS1037, _M0L6_2atmpS2410);
      _M0L1vS2413 = _M0L1pS1020->$3;
      _M0L4fireS2415 = _M0L1pS1020->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2415, _M0L1iS1037)) {
        _M0L6_2atmpS2414 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2416 = _M0L1pS1020->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2414
        = _M0MPC15array5Array2atGfE(_M0L1vS2416, _M0L1iS1037);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2413, _M0L1iS1037, _M0L6_2atmpS2414);
      _M0L1wS2417 = _M0L1pS1020->$4;
      _M0L4fireS2419 = _M0L1pS1020->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2419, _M0L1iS1037)) {
        struct _M0TPB5ArrayGfE* _M0L1wS2421 = _M0L1pS1020->$4;
        float _M0L6_2atmpS2420;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2420
        = _M0MPC15array5Array2atGfE(_M0L1wS2421, _M0L1iS1037);
        _M0L6_2atmpS2418 = _M0L6_2atmpS2420 + _M0L1bS1030;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS2422 = _M0L1pS1020->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2418
        = _M0MPC15array5Array2atGfE(_M0L1wS2422, _M0L1iS1037);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS2417, _M0L1iS1037, _M0L6_2atmpS2418);
      _M0L9thresholdS2423 = _M0L1pS1020->$6;
      _M0L4fireS2425 = _M0L1pS1020->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2425, _M0L1iS1037)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2427 = _M0L1pS1020->$6;
        float _M0L6_2atmpS2426;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2426
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2427, _M0L1iS1037);
        _M0L6_2atmpS2424 = _M0L6_2atmpS2426 + _M0L2atS1031;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2428 = _M0L1pS1020->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2424
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2428, _M0L1iS1037);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2423, _M0L1iS1037, _M0L6_2atmpS2424);
      _M0L4tabsS2429 = _M0L1pS1020->$7;
      _M0L4fireS2431 = _M0L1pS1020->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2431, _M0L1iS1037)) {
        _M0L6_2atmpS2430 = _M0L11tabs__stepsS1034;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2432 = _M0L1pS1020->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2430
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2432, _M0L1iS1037);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2429, _M0L1iS1037, _M0L6_2atmpS2430);
      goto join_1038;
      goto joinlet_2584;
      join_1038:;
      _M0L6_2atmpS2352 = _M0L1iS1037 + 1;
      _M0L1iS1037 = _M0L6_2atmpS2352;
      continue;
      joinlet_2584:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1015
) {
  int32_t _M0L1nS1014;
  int32_t _M0L7_2abindS1016;
  int32_t _M0L1iS1017;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1014 = _M0L1pS1015->$2;
  _M0L7_2abindS1016 = 0;
  _M0L1iS1017 = _M0L7_2abindS1016;
  while (1) {
    if (_M0L1iS1017 < _M0L1nS1014) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2329 = _M0L1pS1015->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS2350 = _M0L1pS1015->$10;
      float _M0L6_2atmpS2345;
      struct _M0TPB5ArrayGfE* _M0L1vS2349;
      float _M0L6_2atmpS2347;
      float _M0L4e__eS2348;
      float _M0L6_2atmpS2346;
      float _M0L6_2atmpS2342;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2344;
      float _M0L6_2atmpS2343;
      float _M0L6_2atmpS2331;
      struct _M0TPB5ArrayGfE* _M0L2giS2341;
      float _M0L6_2atmpS2336;
      struct _M0TPB5ArrayGfE* _M0L1vS2340;
      float _M0L6_2atmpS2338;
      float _M0L4e__iS2339;
      float _M0L6_2atmpS2337;
      float _M0L6_2atmpS2333;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2335;
      float _M0L6_2atmpS2334;
      float _M0L6_2atmpS2332;
      float _M0L6_2atmpS2330;
      int32_t _M0L6_2atmpS2351;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2345 = _M0MPC15array5Array2atGfE(_M0L2geS2350, _M0L1iS1017);
      _M0L1vS2349 = _M0L1pS1015->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2347 = _M0MPC15array5Array2atGfE(_M0L1vS2349, _M0L1iS1017);
      _M0L4e__eS2348 = _M0L1pS1015->$18;
      _M0L6_2atmpS2346 = _M0L6_2atmpS2347 - _M0L4e__eS2348;
      _M0L6_2atmpS2342 = _M0L6_2atmpS2345 * _M0L6_2atmpS2346;
      _M0L7gsyn__eS2344 = _M0L1pS1015->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2343
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2344, _M0L1iS1017);
      _M0L6_2atmpS2331 = _M0L6_2atmpS2342 * _M0L6_2atmpS2343;
      _M0L2giS2341 = _M0L1pS1015->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2336 = _M0MPC15array5Array2atGfE(_M0L2giS2341, _M0L1iS1017);
      _M0L1vS2340 = _M0L1pS1015->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2338 = _M0MPC15array5Array2atGfE(_M0L1vS2340, _M0L1iS1017);
      _M0L4e__iS2339 = _M0L1pS1015->$19;
      _M0L6_2atmpS2337 = _M0L6_2atmpS2338 - _M0L4e__iS2339;
      _M0L6_2atmpS2333 = _M0L6_2atmpS2336 * _M0L6_2atmpS2337;
      _M0L7gsyn__iS2335 = _M0L1pS1015->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2334
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2335, _M0L1iS1017);
      _M0L6_2atmpS2332 = _M0L6_2atmpS2333 * _M0L6_2atmpS2334;
      _M0L6_2atmpS2330 = _M0L6_2atmpS2331 + _M0L6_2atmpS2332;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2329, _M0L1iS1017, _M0L6_2atmpS2330);
      _M0L6_2atmpS2351 = _M0L1iS1017 + 1;
      _M0L1iS1017 = _M0L6_2atmpS2351;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1006,
  float _M0L2dtS1009
) {
  int32_t _M0L1nS1005;
  int32_t _M0L7_2abindS1007;
  int32_t _M0L1iS1008;
  int32_t _M0L7_2abindS1011;
  int32_t _M0L1iS1012;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1005 = _M0L1pS1006->$2;
  _M0L7_2abindS1007 = 0;
  _M0L1iS1008 = _M0L7_2abindS1007;
  while (1) {
    if (_M0L1iS1008 < _M0L1nS1005) {
      struct _M0TPB5ArrayGfE* _M0L2heS2267 = _M0L1pS1006->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS2272 = _M0L1pS1006->$12;
      float _M0L6_2atmpS2269;
      struct _M0TPB5ArrayGfE* _M0L3gluS2271;
      float _M0L6_2atmpS2270;
      float _M0L6_2atmpS2268;
      struct _M0TPB5ArrayGfE* _M0L2hiS2273;
      struct _M0TPB5ArrayGfE* _M0L2hiS2278;
      float _M0L6_2atmpS2275;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2277;
      float _M0L6_2atmpS2276;
      float _M0L6_2atmpS2274;
      struct _M0TPB5ArrayGfE* _M0L2geS2279;
      struct _M0TPB5ArrayGfE* _M0L2geS2291;
      float _M0L6_2atmpS2281;
      struct _M0TPB5ArrayGfE* _M0L2geS2290;
      float _M0L6_2atmpS2289;
      float _M0L6_2atmpS2287;
      float _M0L3tdeS2288;
      float _M0L6_2atmpS2284;
      struct _M0TPB5ArrayGfE* _M0L2heS2286;
      float _M0L6_2atmpS2285;
      float _M0L6_2atmpS2283;
      float _M0L6_2atmpS2282;
      float _M0L6_2atmpS2280;
      struct _M0TPB5ArrayGfE* _M0L2heS2292;
      struct _M0TPB5ArrayGfE* _M0L2heS2301;
      float _M0L6_2atmpS2294;
      struct _M0TPB5ArrayGfE* _M0L2heS2300;
      float _M0L6_2atmpS2299;
      float _M0L6_2atmpS2297;
      float _M0L3treS2298;
      float _M0L6_2atmpS2296;
      float _M0L6_2atmpS2295;
      float _M0L6_2atmpS2293;
      struct _M0TPB5ArrayGfE* _M0L2giS2302;
      struct _M0TPB5ArrayGfE* _M0L2giS2314;
      float _M0L6_2atmpS2304;
      struct _M0TPB5ArrayGfE* _M0L2giS2313;
      float _M0L6_2atmpS2312;
      float _M0L6_2atmpS2310;
      float _M0L3tdiS2311;
      float _M0L6_2atmpS2307;
      struct _M0TPB5ArrayGfE* _M0L2hiS2309;
      float _M0L6_2atmpS2308;
      float _M0L6_2atmpS2306;
      float _M0L6_2atmpS2305;
      float _M0L6_2atmpS2303;
      struct _M0TPB5ArrayGfE* _M0L2hiS2315;
      struct _M0TPB5ArrayGfE* _M0L2hiS2324;
      float _M0L6_2atmpS2317;
      struct _M0TPB5ArrayGfE* _M0L2hiS2323;
      float _M0L6_2atmpS2322;
      float _M0L6_2atmpS2320;
      float _M0L3triS2321;
      float _M0L6_2atmpS2319;
      float _M0L6_2atmpS2318;
      float _M0L6_2atmpS2316;
      int32_t _M0L6_2atmpS2325;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2269 = _M0MPC15array5Array2atGfE(_M0L2heS2272, _M0L1iS1008);
      _M0L3gluS2271 = _M0L1pS1006->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2270
      = _M0MPC15array5Array2atGfE(_M0L3gluS2271, _M0L1iS1008);
      _M0L6_2atmpS2268 = _M0L6_2atmpS2269 + _M0L6_2atmpS2270;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2267, _M0L1iS1008, _M0L6_2atmpS2268);
      _M0L2hiS2273 = _M0L1pS1006->$13;
      _M0L2hiS2278 = _M0L1pS1006->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2275 = _M0MPC15array5Array2atGfE(_M0L2hiS2278, _M0L1iS1008);
      _M0L4gabaS2277 = _M0L1pS1006->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2276
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2277, _M0L1iS1008);
      _M0L6_2atmpS2274 = _M0L6_2atmpS2275 + _M0L6_2atmpS2276;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2273, _M0L1iS1008, _M0L6_2atmpS2274);
      _M0L2geS2279 = _M0L1pS1006->$10;
      _M0L2geS2291 = _M0L1pS1006->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2281 = _M0MPC15array5Array2atGfE(_M0L2geS2291, _M0L1iS1008);
      _M0L2geS2290 = _M0L1pS1006->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2289 = _M0MPC15array5Array2atGfE(_M0L2geS2290, _M0L1iS1008);
      _M0L6_2atmpS2287 = -_M0L6_2atmpS2289;
      _M0L3tdeS2288 = _M0L1pS1006->$21;
      _M0L6_2atmpS2284 = _M0L6_2atmpS2287 / _M0L3tdeS2288;
      _M0L2heS2286 = _M0L1pS1006->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2285 = _M0MPC15array5Array2atGfE(_M0L2heS2286, _M0L1iS1008);
      _M0L6_2atmpS2283 = _M0L6_2atmpS2284 + _M0L6_2atmpS2285;
      _M0L6_2atmpS2282 = _M0L2dtS1009 * _M0L6_2atmpS2283;
      _M0L6_2atmpS2280 = _M0L6_2atmpS2281 + _M0L6_2atmpS2282;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2279, _M0L1iS1008, _M0L6_2atmpS2280);
      _M0L2heS2292 = _M0L1pS1006->$12;
      _M0L2heS2301 = _M0L1pS1006->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2294 = _M0MPC15array5Array2atGfE(_M0L2heS2301, _M0L1iS1008);
      _M0L2heS2300 = _M0L1pS1006->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2299 = _M0MPC15array5Array2atGfE(_M0L2heS2300, _M0L1iS1008);
      _M0L6_2atmpS2297 = -_M0L6_2atmpS2299;
      _M0L3treS2298 = _M0L1pS1006->$20;
      _M0L6_2atmpS2296 = _M0L6_2atmpS2297 / _M0L3treS2298;
      _M0L6_2atmpS2295 = _M0L2dtS1009 * _M0L6_2atmpS2296;
      _M0L6_2atmpS2293 = _M0L6_2atmpS2294 + _M0L6_2atmpS2295;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2292, _M0L1iS1008, _M0L6_2atmpS2293);
      _M0L2giS2302 = _M0L1pS1006->$11;
      _M0L2giS2314 = _M0L1pS1006->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2304 = _M0MPC15array5Array2atGfE(_M0L2giS2314, _M0L1iS1008);
      _M0L2giS2313 = _M0L1pS1006->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2312 = _M0MPC15array5Array2atGfE(_M0L2giS2313, _M0L1iS1008);
      _M0L6_2atmpS2310 = -_M0L6_2atmpS2312;
      _M0L3tdiS2311 = _M0L1pS1006->$23;
      _M0L6_2atmpS2307 = _M0L6_2atmpS2310 / _M0L3tdiS2311;
      _M0L2hiS2309 = _M0L1pS1006->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2308 = _M0MPC15array5Array2atGfE(_M0L2hiS2309, _M0L1iS1008);
      _M0L6_2atmpS2306 = _M0L6_2atmpS2307 + _M0L6_2atmpS2308;
      _M0L6_2atmpS2305 = _M0L2dtS1009 * _M0L6_2atmpS2306;
      _M0L6_2atmpS2303 = _M0L6_2atmpS2304 + _M0L6_2atmpS2305;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2302, _M0L1iS1008, _M0L6_2atmpS2303);
      _M0L2hiS2315 = _M0L1pS1006->$13;
      _M0L2hiS2324 = _M0L1pS1006->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2317 = _M0MPC15array5Array2atGfE(_M0L2hiS2324, _M0L1iS1008);
      _M0L2hiS2323 = _M0L1pS1006->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2322 = _M0MPC15array5Array2atGfE(_M0L2hiS2323, _M0L1iS1008);
      _M0L6_2atmpS2320 = -_M0L6_2atmpS2322;
      _M0L3triS2321 = _M0L1pS1006->$22;
      _M0L6_2atmpS2319 = _M0L6_2atmpS2320 / _M0L3triS2321;
      _M0L6_2atmpS2318 = _M0L2dtS1009 * _M0L6_2atmpS2319;
      _M0L6_2atmpS2316 = _M0L6_2atmpS2317 + _M0L6_2atmpS2318;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2315, _M0L1iS1008, _M0L6_2atmpS2316);
      _M0L6_2atmpS2325 = _M0L1iS1008 + 1;
      _M0L1iS1008 = _M0L6_2atmpS2325;
      continue;
    }
    break;
  }
  _M0L7_2abindS1011 = 0;
  _M0L1iS1012 = _M0L7_2abindS1011;
  while (1) {
    if (_M0L1iS1012 < _M0L1nS1005) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2326 = _M0L1pS1006->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2327;
      int32_t _M0L6_2atmpS2328;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2326, _M0L1iS1012, 0x0p+0f);
      _M0L4gabaS2327 = _M0L1pS1006->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2327, _M0L1iS1012, 0x0p+0f);
      _M0L6_2atmpS2328 = _M0L1iS1012 + 1;
      _M0L1iS1012 = _M0L6_2atmpS2328;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22forward__adex__synapse(
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0L1cS1004
) {
  moonbit_string_t _M0L3symS2264;
  int32_t _if__result_2588;
  struct _M0TPB5ArrayGfE* _M0L6targetS1003;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2260;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3preS2262;
  struct _M0TPB5ArrayGbE* _M0L4fireS2261;
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L3symS2264 = _M0L1cS1004->$2;
  #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS2264 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2264)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2264, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2264) * 2)
  ) {
    _if__result_2588 = 1;
  } else {
    moonbit_string_t _M0L3symS2263 = _M0L1cS1004->$2;
    #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _if__result_2588
    = _M0L3symS2263 == (moonbit_string_t)moonbit_string_literal_10.data
      || Moonbit_array_length(_M0L3symS2263)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
         && 0
            == memcmp(_M0L3symS2263, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS2263) * 2);
  }
  if (_if__result_2588) {
    struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4postS2265 = _M0L1cS1004->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2515 = _M0L4postS2265->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS2515);
    _M0L6targetS1003 = _M0L8_2afieldS2515;
  } else {
    struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4postS2266 = _M0L1cS1004->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2516 = _M0L4postS2266->$15;
    moonbit_incref_cycle_free(_M0L8_2afieldS2516);
    _M0L6targetS1003 = _M0L8_2afieldS2516;
  }
  _M0L6matrixS2260 = _M0L1cS1004->$3;
  _M0L3preS2262 = _M0L1cS1004->$0;
  _M0L4fireS2261 = _M0L3preS2262->$5;
  #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2260, _M0L4fireS2261, _M0L6targetS1003);
  moonbit_decref_cycle_free(_M0L6targetS1003);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0MP26RiantR8snn__mbt18SpikingSynapseAdEx6random(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3preS996,
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4postS997,
  moonbit_string_t _M0L3symS1002,
  float _M0L2muS998,
  float _M0L5sigmaS999,
  float _M0L1pS1000,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1001
) {
  int32_t _M0L1nS2258;
  int32_t _M0L1nS2259;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS995;
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _block_2589;
  #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L1nS2258 = _M0L3preS996->$2;
  _M0L1nS2259 = _M0L4postS997->$2;
  #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6matrixS995
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2258, _M0L1nS2259, _M0L2muS998, _M0L5sigmaS999, _M0L1pS1000, _M0L3rngS1001);
  moonbit_incref_cycle_free(_M0L3preS996);
  moonbit_incref_cycle_free(_M0L4postS997);
  moonbit_incref_cycle_free(_M0L3symS1002);
  _block_2589
  = (struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx));
  Moonbit_object_header(_block_2589)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 37, 0);
  _block_2589->$0 = _M0L3preS996;
  _block_2589->$1 = _M0L4postS997;
  _block_2589->$2 = _M0L3symS1002;
  _block_2589->$3 = _M0L6matrixS995;
  return _block_2589;
}

int32_t _M0FP26RiantR8snn__mbt17record__one__adex(
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0L1mS993,
  float _M0L1tS994
) {
  moonbit_string_t _M0L3symS2246;
  float _M0L1vS992;
  struct _M0TPB5ArrayGfE* _M0L4dataS2244;
  struct _M0TPB5ArrayGfE* _M0L5timesS2245;
  #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L3symS2246 = _M0L1mS993->$1;
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS2246 == (moonbit_string_t)moonbit_string_literal_11.data
    || Moonbit_array_length(_M0L3symS2246)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
       && 0
          == memcmp(_M0L3symS2246, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS2246) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3popS2249 = _M0L1mS993->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS2247 = _M0L3popS2249->$3;
    int32_t _M0L6neuronS2248 = _M0L1mS993->$4;
    #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS992 = _M0MPC15array5Array2atGfE(_M0L1vS2247, _M0L6neuronS2248);
  } else {
    moonbit_string_t _M0L3symS2250 = _M0L1mS993->$1;
    #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS2250 == (moonbit_string_t)moonbit_string_literal_12.data
      || Moonbit_array_length(_M0L3symS2250)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_12.data)
         && 0
            == memcmp(_M0L3symS2250, (moonbit_string_t)moonbit_string_literal_12.data, Moonbit_array_length(_M0L3symS2250) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3popS2253 = _M0L1mS993->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2251 = _M0L3popS2253->$5;
      int32_t _M0L6neuronS2252 = _M0L1mS993->$4;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2251, _M0L6neuronS2252)) {
        _M0L1vS992 = 0x1p+0f;
      } else {
        _M0L1vS992 = 0x0p+0f;
      }
    } else {
      moonbit_string_t _M0L3symS2254 = _M0L1mS993->$1;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (
        _M0L3symS2254 == (moonbit_string_t)moonbit_string_literal_13.data
        || Moonbit_array_length(_M0L3symS2254)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_13.data)
           && 0
              == memcmp(_M0L3symS2254, (moonbit_string_t)moonbit_string_literal_13.data, Moonbit_array_length(_M0L3symS2254) * 2)
      ) {
        struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3popS2257 = _M0L1mS993->$0;
        struct _M0TPB5ArrayGfE* _M0L1wS2255 = _M0L3popS2257->$4;
        int32_t _M0L6neuronS2256 = _M0L1mS993->$4;
        #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
        _M0L1vS992 = _M0MPC15array5Array2atGfE(_M0L1wS2255, _M0L6neuronS2256);
      } else {
        _M0L1vS992 = 0x0p+0f;
      }
    }
  }
  _M0L4dataS2244 = _M0L1mS993->$2;
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS2244, _M0L1vS992);
  _M0L5timesS2245 = _M0L1mS993->$3;
  #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS2245, _M0L1tS994);
  return 0;
}

struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0MP26RiantR8snn__mbt11MonitorAdEx6new__v(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3popS990,
  int32_t _M0L6neuronS991
) {
  float* _M0L6_2atmpS2243;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2240;
  float* _M0L6_2atmpS2242;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2241;
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _block_2590;
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS2243 = moonbit_empty_float_array;
  _M0L6_2atmpS2240
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2240)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 43, 0);
  _M0L6_2atmpS2240->$0 = _M0L6_2atmpS2243;
  _M0L6_2atmpS2240->$1 = 0;
  _M0L6_2atmpS2242 = moonbit_empty_float_array;
  _M0L6_2atmpS2241
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2241)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 43, 0);
  _M0L6_2atmpS2241->$0 = _M0L6_2atmpS2242;
  _M0L6_2atmpS2241->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS990);
  _block_2590
  = (struct _M0TP26RiantR8snn__mbt11MonitorAdEx*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx));
  Moonbit_object_header(_block_2590)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 46, 0);
  _block_2590->$0 = _M0L3popS990;
  _block_2590->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_2590->$2 = _M0L6_2atmpS2240;
  _block_2590->$3 = _M0L6_2atmpS2241;
  _block_2590->$4 = _M0L6neuronS991;
  return _block_2590;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS978,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS981,
  struct _M0TPB5ArrayGfE* _M0L7post__gS987
) {
  int32_t _M0L4rowsS977;
  int32_t _M0L7_2abindS979;
  int32_t _M0L1iS980;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS977 = _M0L1mS978->$0;
  _M0L7_2abindS979 = 0;
  _M0L1iS980 = _M0L7_2abindS979;
  while (1) {
    if (_M0L1iS980 < _M0L4rowsS977) {
      int32_t _M0L6_2atmpS2239;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS981, _M0L1iS980)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2238 = _M0L1mS978->$2;
        int32_t _M0L5startS982;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2236;
        int32_t _M0L6_2atmpS2237;
        int32_t _M0L3endS983;
        int32_t _M0L1kS984;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS982
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2238, _M0L1iS980);
        _M0L6rowptrS2236 = _M0L1mS978->$2;
        _M0L6_2atmpS2237 = _M0L1iS980 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS983
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2236, _M0L6_2atmpS2237);
        _M0L1kS984 = _M0L5startS982;
        while (1) {
          if (_M0L1kS984 < _M0L3endS983) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS2234 = _M0L1mS978->$3;
            int32_t _M0L9post__idxS985;
            struct _M0TPB5ArrayGfE* _M0L4valsS2233;
            float _M0L1wS986;
            float _M0L6_2atmpS2232;
            float _M0L6_2atmpS2231;
            int32_t _M0L6_2atmpS2235;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS985
            = _M0MPC15array5Array2atGiE(_M0L6colptrS2234, _M0L1kS984);
            _M0L4valsS2233 = _M0L1mS978->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS986
            = _M0MPC15array5Array2atGfE(_M0L4valsS2233, _M0L1kS984);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS2232
            = _M0MPC15array5Array2atGfE(_M0L7post__gS987, _M0L9post__idxS985);
            _M0L6_2atmpS2231 = _M0L6_2atmpS2232 + _M0L1wS986;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS987, _M0L9post__idxS985, _M0L6_2atmpS2231);
            _M0L6_2atmpS2235 = _M0L1kS984 + 1;
            _M0L1kS984 = _M0L6_2atmpS2235;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2239 = _M0L1iS980 + 1;
      _M0L1iS980 = _M0L6_2atmpS2239;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS971,
  int32_t _M0L4colsS972,
  float _M0L2muS973,
  float _M0L5sigmaS974,
  float _M0L1pS975,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS976
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS971, _M0L4colsS972, _M0L2muS973, _M0L5sigmaS974, _M0L1pS975, 0, _M0L3rngS976);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS885,
  int32_t _M0L4colsS889,
  float _M0L2muS895,
  float _M0L5sigmaS896,
  float _M0L1pS908,
  int32_t _M0L4ruleS902,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS898
) {
  float* _M0L6_2atmpS2230;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2229;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS884;
  int32_t _M0L7_2abindS886;
  int32_t _M0L1iS887;
  int32_t _M0L6_2atmpS2228;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS961;
  int32_t* _M0L6_2atmpS2227;
  struct _M0TPB5ArrayGiE* _M0L6colptrS962;
  float* _M0L6_2atmpS2226;
  struct _M0TPB5ArrayGfE* _M0L4valsS963;
  int32_t _M0L7_2abindS964;
  int32_t _M0L1iS965;
  int32_t _M0L6_2atmpS2225;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2612;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2230 = moonbit_empty_float_array;
  _M0L6_2atmpS2229
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2229)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 43, 0);
  _M0L6_2atmpS2229->$0 = _M0L6_2atmpS2230;
  _M0L6_2atmpS2229->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS884
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS885, _M0L6_2atmpS2229);
  _M0L7_2abindS886 = 0;
  _M0L1iS887 = _M0L7_2abindS886;
  while (1) {
    if (_M0L1iS887 < _M0L4rowsS885) {
      struct _M0TPB5ArrayGfE* _M0L3rowS888;
      int32_t _M0L7_2abindS890;
      int32_t _M0L1jS891;
      int32_t _M0L6_2atmpS2181;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS888 = _M0MPC15array5Array4makeGfE(_M0L4colsS889, 0x0p+0f);
      _M0L7_2abindS890 = 0;
      _M0L1jS891 = _M0L7_2abindS890;
      while (1) {
        if (_M0L1jS891 < _M0L4colsS889) {
          double _M0L2z1S893;
          struct _M0TUddE* _M0L7_2abindS897;
          double _M0L5_2az1S899;
          float _M0L6_2atmpS2179;
          float _M0L6_2atmpS2178;
          float _M0L1wS894;
          int32_t _M0L6_2atmpS2180;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS897
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS898);
          _M0L5_2az1S899 = _M0L7_2abindS897->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS897);
          _M0L2z1S893 = _M0L5_2az1S899;
          goto join_892;
          goto joinlet_2595;
          join_892:;
          _M0L6_2atmpS2179 = (float)_M0L2z1S893;
          _M0L6_2atmpS2178 = _M0L5sigmaS896 * _M0L6_2atmpS2179;
          _M0L1wS894 = _M0L2muS895 + _M0L6_2atmpS2178;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS888, _M0L1jS891, _M0L1wS894);
          joinlet_2595:;
          _M0L6_2atmpS2180 = _M0L1jS891 + 1;
          _M0L1jS891 = _M0L6_2atmpS2180;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS884, _M0L1iS887, _M0L3rowS888);
      _M0L6_2atmpS2181 = _M0L1iS887 + 1;
      _M0L1iS887 = _M0L6_2atmpS2181;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS902) {
    case 0: {
      int32_t _M0L7_2abindS903 = 0;
      int32_t _M0L1iS904 = _M0L7_2abindS903;
      while (1) {
        if (_M0L1iS904 < _M0L4rowsS885) {
          int32_t _M0L7_2abindS905 = 0;
          int32_t _M0L1jS906 = _M0L7_2abindS905;
          int32_t _M0L6_2atmpS2184;
          while (1) {
            if (_M0L1jS906 < _M0L4colsS889) {
              float _M0L1uS907;
              int32_t _M0L6_2atmpS2183;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS907 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS898);
              if (_M0L1uS907 >= _M0L1pS908) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2182;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2182
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS884, _M0L1iS904);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2182, _M0L1jS906, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2182);
              }
              _M0L6_2atmpS2183 = _M0L1jS906 + 1;
              _M0L1jS906 = _M0L6_2atmpS2183;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2184 = _M0L1iS904 + 1;
          _M0L1iS904 = _M0L6_2atmpS2184;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2202 = (float)_M0L4rowsS885;
      float _M0L6_2atmpS2201 = _M0L6_2atmpS2202 * _M0L1pS908;
      int32_t _M0L7n__keepS911;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS911 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2201);
      if (_M0L7n__keepS911 > 0 && _M0L7n__keepS911 <= _M0L4rowsS885) {
        int32_t _M0L7_2abindS912 = 0;
        int32_t _M0L1jS913 = _M0L7_2abindS912;
        while (1) {
          if (_M0L1jS913 < _M0L4colsS889) {
            int32_t* _M0L6_2atmpS2196 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS914 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS915;
            int32_t _M0L1kS916;
            int32_t _M0L7n__dropS918;
            int32_t _M0L7_2abindS919;
            int32_t _M0L1kS920;
            int32_t _M0L7_2abindS926;
            int32_t _M0L1kS927;
            int32_t _M0L6_2atmpS2197;
            Moonbit_object_header(_M0L8pre__idxS914)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 52, 0);
            _M0L8pre__idxS914->$0 = _M0L6_2atmpS2196;
            _M0L8pre__idxS914->$1 = 0;
            _M0L7_2abindS915 = 0;
            _M0L1kS916 = _M0L7_2abindS915;
            while (1) {
              if (_M0L1kS916 < _M0L4rowsS885) {
                int32_t _M0L6_2atmpS2185;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS914, _M0L1kS916);
                _M0L6_2atmpS2185 = _M0L1kS916 + 1;
                _M0L1kS916 = _M0L6_2atmpS2185;
                continue;
              }
              break;
            }
            _M0L7n__dropS918 = _M0L4rowsS885 - _M0L7n__keepS911;
            _M0L7_2abindS919 = 0;
            _M0L1kS920 = _M0L7_2abindS919;
            while (1) {
              if (_M0L1kS920 < _M0L7n__dropS918) {
                float _M0L1uS921;
                float _M0L6_2atmpS2189;
                float _M0L6_2atmpS2191;
                float _M0L6_2atmpS2190;
                float _M0L6_2atmpS2188;
                int32_t _M0L6_2atmpS2187;
                int32_t _M0L6r__idxS922;
                int32_t _M0L10r__clampedS923;
                int32_t _M0L3tmpS924;
                int32_t _M0L6_2atmpS2186;
                int32_t _M0L6_2atmpS2192;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS921 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS898);
                _M0L6_2atmpS2189 = (float)_M0L4rowsS885;
                _M0L6_2atmpS2191 = (float)_M0L1kS920;
                _M0L6_2atmpS2190 = _M0L6_2atmpS2191 * _M0L1uS921;
                _M0L6_2atmpS2188 = _M0L6_2atmpS2189 - _M0L6_2atmpS2190;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2187
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2188);
                _M0L6r__idxS922 = _M0L1kS920 + _M0L6_2atmpS2187;
                if (_M0L6r__idxS922 >= _M0L4rowsS885) {
                  _M0L10r__clampedS923 = _M0L4rowsS885 - 1;
                } else {
                  _M0L10r__clampedS923 = _M0L6r__idxS922;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS924
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS914, _M0L1kS920);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2186
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS914, _M0L10r__clampedS923);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS914, _M0L1kS920, _M0L6_2atmpS2186);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS914, _M0L10r__clampedS923, _M0L3tmpS924);
                _M0L6_2atmpS2192 = _M0L1kS920 + 1;
                _M0L1kS920 = _M0L6_2atmpS2192;
                continue;
              }
              break;
            }
            _M0L7_2abindS926 = 0;
            _M0L1kS927 = _M0L7_2abindS926;
            while (1) {
              if (_M0L1kS927 < _M0L7n__dropS918) {
                int32_t _M0L6_2atmpS2194;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2193;
                int32_t _M0L6_2atmpS2195;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2194
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS914, _M0L1kS927);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2193
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS884, _M0L6_2atmpS2194);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2193, _M0L1jS913, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2193);
                _M0L6_2atmpS2195 = _M0L1kS927 + 1;
                _M0L1kS927 = _M0L6_2atmpS2195;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS914);
              }
              break;
            }
            _M0L6_2atmpS2197 = _M0L1jS913 + 1;
            _M0L1jS913 = _M0L6_2atmpS2197;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS911 == 0) {
        int32_t _M0L7_2abindS930 = 0;
        int32_t _M0L1iS931 = _M0L7_2abindS930;
        while (1) {
          if (_M0L1iS931 < _M0L4rowsS885) {
            int32_t _M0L7_2abindS932 = 0;
            int32_t _M0L1jS933 = _M0L7_2abindS932;
            int32_t _M0L6_2atmpS2200;
            while (1) {
              if (_M0L1jS933 < _M0L4colsS889) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2198;
                int32_t _M0L6_2atmpS2199;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2198
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS884, _M0L1iS931);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2198, _M0L1jS933, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2198);
                _M0L6_2atmpS2199 = _M0L1jS933 + 1;
                _M0L1jS933 = _M0L6_2atmpS2199;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2200 = _M0L1iS931 + 1;
            _M0L1iS931 = _M0L6_2atmpS2200;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2220 = (float)_M0L4colsS889;
      float _M0L6_2atmpS2219 = _M0L6_2atmpS2220 * _M0L1pS908;
      int32_t _M0L7n__keepS936;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS936 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2219);
      if (_M0L7n__keepS936 > 0 && _M0L7n__keepS936 <= _M0L4colsS889) {
        int32_t _M0L7_2abindS937 = 0;
        int32_t _M0L1iS938 = _M0L7_2abindS937;
        while (1) {
          if (_M0L1iS938 < _M0L4rowsS885) {
            int32_t* _M0L6_2atmpS2214 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS939 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS940;
            int32_t _M0L1kS941;
            int32_t _M0L7n__dropS943;
            int32_t _M0L7_2abindS944;
            int32_t _M0L1kS945;
            int32_t _M0L7_2abindS951;
            int32_t _M0L1kS952;
            int32_t _M0L6_2atmpS2215;
            Moonbit_object_header(_M0L9post__idxS939)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 52, 0);
            _M0L9post__idxS939->$0 = _M0L6_2atmpS2214;
            _M0L9post__idxS939->$1 = 0;
            _M0L7_2abindS940 = 0;
            _M0L1kS941 = _M0L7_2abindS940;
            while (1) {
              if (_M0L1kS941 < _M0L4colsS889) {
                int32_t _M0L6_2atmpS2203;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS939, _M0L1kS941);
                _M0L6_2atmpS2203 = _M0L1kS941 + 1;
                _M0L1kS941 = _M0L6_2atmpS2203;
                continue;
              }
              break;
            }
            _M0L7n__dropS943 = _M0L4colsS889 - _M0L7n__keepS936;
            _M0L7_2abindS944 = 0;
            _M0L1kS945 = _M0L7_2abindS944;
            while (1) {
              if (_M0L1kS945 < _M0L7n__dropS943) {
                float _M0L1uS946;
                float _M0L6_2atmpS2207;
                float _M0L6_2atmpS2209;
                float _M0L6_2atmpS2208;
                float _M0L6_2atmpS2206;
                int32_t _M0L6_2atmpS2205;
                int32_t _M0L6r__idxS947;
                int32_t _M0L10r__clampedS948;
                int32_t _M0L3tmpS949;
                int32_t _M0L6_2atmpS2204;
                int32_t _M0L6_2atmpS2210;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS946 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS898);
                _M0L6_2atmpS2207 = (float)_M0L4colsS889;
                _M0L6_2atmpS2209 = (float)_M0L1kS945;
                _M0L6_2atmpS2208 = _M0L6_2atmpS2209 * _M0L1uS946;
                _M0L6_2atmpS2206 = _M0L6_2atmpS2207 - _M0L6_2atmpS2208;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2205
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2206);
                _M0L6r__idxS947 = _M0L1kS945 + _M0L6_2atmpS2205;
                if (_M0L6r__idxS947 >= _M0L4colsS889) {
                  _M0L10r__clampedS948 = _M0L4colsS889 - 1;
                } else {
                  _M0L10r__clampedS948 = _M0L6r__idxS947;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS949
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS939, _M0L1kS945);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2204
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS939, _M0L10r__clampedS948);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS939, _M0L1kS945, _M0L6_2atmpS2204);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS939, _M0L10r__clampedS948, _M0L3tmpS949);
                _M0L6_2atmpS2210 = _M0L1kS945 + 1;
                _M0L1kS945 = _M0L6_2atmpS2210;
                continue;
              }
              break;
            }
            _M0L7_2abindS951 = 0;
            _M0L1kS952 = _M0L7_2abindS951;
            while (1) {
              if (_M0L1kS952 < _M0L7n__dropS943) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2211;
                int32_t _M0L6_2atmpS2212;
                int32_t _M0L6_2atmpS2213;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2211
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS884, _M0L1iS938);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2212
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS939, _M0L1kS952);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2211, _M0L6_2atmpS2212, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2211);
                _M0L6_2atmpS2213 = _M0L1kS952 + 1;
                _M0L1kS952 = _M0L6_2atmpS2213;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS939);
              }
              break;
            }
            _M0L6_2atmpS2215 = _M0L1iS938 + 1;
            _M0L1iS938 = _M0L6_2atmpS2215;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS936 == 0) {
        int32_t _M0L7_2abindS955 = 0;
        int32_t _M0L1iS956 = _M0L7_2abindS955;
        while (1) {
          if (_M0L1iS956 < _M0L4rowsS885) {
            int32_t _M0L7_2abindS957 = 0;
            int32_t _M0L1jS958 = _M0L7_2abindS957;
            int32_t _M0L6_2atmpS2218;
            while (1) {
              if (_M0L1jS958 < _M0L4colsS889) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2216;
                int32_t _M0L6_2atmpS2217;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2216
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS884, _M0L1iS956);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2216, _M0L1jS958, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2216);
                _M0L6_2atmpS2217 = _M0L1jS958 + 1;
                _M0L1jS958 = _M0L6_2atmpS2217;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2218 = _M0L1iS956 + 1;
            _M0L1iS956 = _M0L6_2atmpS2218;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2228 = _M0L4rowsS885 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS961 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2228, 0);
  _M0L6_2atmpS2227 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS962
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS962)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 52, 0);
  _M0L6colptrS962->$0 = _M0L6_2atmpS2227;
  _M0L6colptrS962->$1 = 0;
  _M0L6_2atmpS2226 = moonbit_empty_float_array;
  _M0L4valsS963
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS963)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 43, 0);
  _M0L4valsS963->$0 = _M0L6_2atmpS2226;
  _M0L4valsS963->$1 = 0;
  _M0L7_2abindS964 = 0;
  _M0L1iS965 = _M0L7_2abindS964;
  while (1) {
    if (_M0L1iS965 < _M0L4rowsS885) {
      int32_t _M0L6_2atmpS2221;
      int32_t _M0L7_2abindS966;
      int32_t _M0L1jS967;
      int32_t _M0L6_2atmpS2224;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2221 = _M0MPC15array5Array6lengthGfE(_M0L4valsS963);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS961, _M0L1iS965, _M0L6_2atmpS2221);
      _M0L7_2abindS966 = 0;
      _M0L1jS967 = _M0L7_2abindS966;
      while (1) {
        if (_M0L1jS967 < _M0L4colsS889) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2222;
          float _M0L1vS968;
          int32_t _M0L6_2atmpS2223;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2222
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS884, _M0L1iS965);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS968
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2222, _M0L1jS967);
          moonbit_decref_cycle_free(_M0L6_2atmpS2222);
          if (_M0L1vS968 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS962, _M0L1jS967);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS963, _M0L1vS968);
          }
          _M0L6_2atmpS2223 = _M0L1jS967 + 1;
          _M0L1jS967 = _M0L6_2atmpS2223;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2224 = _M0L1iS965 + 1;
      _M0L1iS965 = _M0L6_2atmpS2224;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS884);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2225 = _M0MPC15array5Array6lengthGfE(_M0L4valsS963);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS961, _M0L4rowsS885, _M0L6_2atmpS2225);
  _block_2612
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2612)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 55, 0);
  _block_2612->$0 = _M0L4rowsS885;
  _block_2612->$1 = _M0L4colsS889;
  _block_2612->$2 = _M0L6rowptrS961;
  _block_2612->$3 = _M0L6colptrS962;
  _block_2612->$4 = _M0L4valsS963;
  return _block_2612;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS883
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS2177;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS2177 = _M0L1mS883->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS2177);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS881
) {
  struct _M0TUmmmmE* _M0L1sS880;
  uint64_t _M0L6_2atmpS2176;
  struct _M0TUmmmmE* _M0L1tS882;
  uint64_t _M0L6_2atmpS2172;
  uint64_t _M0L6_2atmpS2173;
  uint64_t _M0L6_2atmpS2174;
  uint64_t _M0L6_2atmpS2175;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2613;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS880 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS881);
  _M0L6_2atmpS2176 = _M0L1sS880->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS882 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2176);
  _M0L6_2atmpS2172 = _M0L1sS880->$0;
  _M0L6_2atmpS2173 = _M0L1sS880->$1;
  _M0L6_2atmpS2174 = _M0L1sS880->$2;
  moonbit_decref_cycle_free(_M0L1sS880);
  _M0L6_2atmpS2175 = _M0L1tS882->$0;
  moonbit_decref_cycle_free(_M0L1tS882);
  _block_2613
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2613)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2613->$0 = _M0L6_2atmpS2172;
  _block_2613->$1 = _M0L6_2atmpS2173;
  _block_2613->$2 = _M0L6_2atmpS2174;
  _block_2613->$3 = _M0L6_2atmpS2175;
  return _block_2613;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS872) {
  uint64_t _M0L2s1S871;
  uint64_t _M0L2z1S873;
  uint64_t _M0L2s2S874;
  uint64_t _M0L2z2S875;
  uint64_t _M0L2s3S876;
  uint64_t _M0L2z3S877;
  uint64_t _M0L2s4S878;
  uint64_t _M0L2z4S879;
  struct _M0TUmmmmE* _block_2614;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S871 = _M0L4seedS872 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S873 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S871);
  _M0L2s2S874 = _M0L2s1S871 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S875 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S874);
  _M0L2s3S876 = _M0L2s2S874 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S877 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S876);
  _M0L2s4S878 = _M0L2s3S876 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S879 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S878);
  _block_2614 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2614)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2614->$0 = _M0L2z1S873;
  _block_2614->$1 = _M0L2z2S875;
  _block_2614->$2 = _M0L2z3S877;
  _block_2614->$3 = _M0L2z4S879;
  return _block_2614;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS869) {
  uint64_t _M0L6_2atmpS2171;
  uint64_t _M0L6_2atmpS2170;
  uint64_t _M0L1zS868;
  uint64_t _M0L6_2atmpS2169;
  uint64_t _M0L6_2atmpS2168;
  uint64_t _M0L1zS870;
  uint64_t _M0L6_2atmpS2167;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2171 = _M0L1zS869 >> 30;
  _M0L6_2atmpS2170 = _M0L1zS869 ^ _M0L6_2atmpS2171;
  _M0L1zS868 = _M0L6_2atmpS2170 * 13787848793156543929ull;
  _M0L6_2atmpS2169 = _M0L1zS868 >> 27;
  _M0L6_2atmpS2168 = _M0L1zS868 ^ _M0L6_2atmpS2169;
  _M0L1zS870 = _M0L6_2atmpS2168 * 10723151780598845931ull;
  _M0L6_2atmpS2167 = _M0L1zS870 >> 31;
  return _M0L1zS870 ^ _M0L6_2atmpS2167;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS863
) {
  double _M0L2u1S862;
  double _M0L8u1__safeS864;
  double _M0L2u2S865;
  double _M0L6_2atmpS2166;
  double _M0L6_2atmpS2165;
  double _M0L1rS866;
  double _M0L5thetaS867;
  double _M0L6_2atmpS2164;
  double _M0L6_2atmpS2161;
  double _M0L6_2atmpS2163;
  double _M0L6_2atmpS2162;
  struct _M0TUddE* _block_2615;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S862 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS863);
  if (_M0L2u1S862 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS864 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS864 = _M0L2u1S862;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S865 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS863);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2166 = _M0FPC14math2ln(_M0L8u1__safeS864);
  _M0L6_2atmpS2165 = -0x1p+1 * _M0L6_2atmpS2166;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS866 = sqrt(_M0L6_2atmpS2165);
  _M0L5thetaS867 = 0x1.921fb54442d18p+2 * _M0L2u2S865;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2164 = _M0FPC14math3cos(_M0L5thetaS867);
  _M0L6_2atmpS2161 = _M0L1rS866 * _M0L6_2atmpS2164;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2163 = _M0FPC14math3sin(_M0L5thetaS867);
  _M0L6_2atmpS2162 = _M0L1rS866 * _M0L6_2atmpS2163;
  _block_2615 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2615)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2615->$0 = _M0L6_2atmpS2161;
  _block_2615->$1 = _M0L6_2atmpS2162;
  return _block_2615;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS860
) {
  uint64_t _M0L1uS859;
  uint64_t _M0L4bitsS861;
  double _M0L6_2atmpS2160;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS859 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS860);
  _M0L4bitsS861 = _M0L1uS859 >> 11;
  _M0L6_2atmpS2160 = (double)_M0L4bitsS861;
  return _M0L6_2atmpS2160 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS857,
  float _M0L2dtS858
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2152;
  struct _M0TPB5ArrayGfE* _M0L1tS2155;
  float _M0L6_2atmpS2154;
  float _M0L6_2atmpS2153;
  struct _M0TPB5ArrayGiE* _M0L2ttS2156;
  struct _M0TPB5ArrayGiE* _M0L2ttS2159;
  int32_t _M0L6_2atmpS2158;
  int32_t _M0L6_2atmpS2157;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2152 = _M0L1tS857->$0;
  _M0L1tS2155 = _M0L1tS857->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2154 = _M0MPC15array5Array2atGfE(_M0L1tS2155, 0);
  _M0L6_2atmpS2153 = _M0L6_2atmpS2154 + _M0L2dtS858;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS2152, 0, _M0L6_2atmpS2153);
  _M0L2ttS2156 = _M0L1tS857->$1;
  _M0L2ttS2159 = _M0L1tS857->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2158 = _M0MPC15array5Array2atGiE(_M0L2ttS2159, 0);
  _M0L6_2atmpS2157 = _M0L6_2atmpS2158 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS2156, 0, _M0L6_2atmpS2157);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS855,
  float _M0L1vS856
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS855->$2 = _M0L1vS856;
  return 0;
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS2151;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2148;
  int32_t* _M0L6_2atmpS2150;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2149;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2616;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2151 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2151[0] = 0x0p+0f;
  _M0L6_2atmpS2148
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2148)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 43, 0);
  _M0L6_2atmpS2148->$0 = _M0L6_2atmpS2151;
  _M0L6_2atmpS2148->$1 = 1;
  _M0L6_2atmpS2150 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS2150[0] = 0;
  _M0L6_2atmpS2149
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2149)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 52, 0);
  _M0L6_2atmpS2149->$0 = _M0L6_2atmpS2150;
  _M0L6_2atmpS2149->$1 = 1;
  _block_2616
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2616)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_2616->$0 = _M0L6_2atmpS2148;
  _block_2616->$1 = _M0L6_2atmpS2149;
  _block_2616->$2 = 0x1p-3f;
  return _block_2616;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS853
) {
  uint32_t _M0L1uS852;
  uint32_t _M0L4bitsS854;
  double _M0L6_2atmpS2147;
  double _M0L6_2atmpS2146;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS852 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS853);
  _M0L4bitsS854 = _M0L1uS852 >> 8;
  _M0L6_2atmpS2147 = (double)_M0L4bitsS854;
  _M0L6_2atmpS2146 = _M0L6_2atmpS2147 * 0x1p-24;
  return (float)_M0L6_2atmpS2146;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS851
) {
  uint64_t _M0L1uS850;
  uint64_t _M0L6_2atmpS2145;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS850 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS851);
  _M0L6_2atmpS2145 = _M0L1uS850 >> 32;
  return (uint32_t)_M0L6_2atmpS2145;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS843
) {
  uint64_t _M0L2s0S842;
  uint64_t _M0L2s1S844;
  uint64_t _M0L2s2S845;
  uint64_t _M0L2s3S846;
  uint64_t _M0L3tmpS847;
  uint64_t _M0L6_2atmpS2144;
  uint64_t _M0L3resS848;
  uint64_t _M0L1tS849;
  uint64_t _M0L6_2atmpS2134;
  uint64_t _M0L6_2atmpS2135;
  uint64_t _M0L2s2S2137;
  uint64_t _M0L6_2atmpS2136;
  uint64_t _M0L2s3S2139;
  uint64_t _M0L6_2atmpS2138;
  uint64_t _M0L2s2S2141;
  uint64_t _M0L6_2atmpS2140;
  uint64_t _M0L2s3S2143;
  uint64_t _M0L6_2atmpS2142;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S842 = _M0L1rS843->$0;
  _M0L2s1S844 = _M0L1rS843->$1;
  _M0L2s2S845 = _M0L1rS843->$2;
  _M0L2s3S846 = _M0L1rS843->$3;
  _M0L3tmpS847 = _M0L2s0S842 + _M0L2s3S846;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2144 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS847, 23);
  _M0L3resS848 = _M0L6_2atmpS2144 + _M0L2s0S842;
  _M0L1tS849 = _M0L2s1S844 << 17;
  _M0L6_2atmpS2134 = _M0L2s2S845 ^ _M0L2s0S842;
  _M0L1rS843->$2 = _M0L6_2atmpS2134;
  _M0L6_2atmpS2135 = _M0L2s3S846 ^ _M0L2s1S844;
  _M0L1rS843->$3 = _M0L6_2atmpS2135;
  _M0L2s2S2137 = _M0L1rS843->$2;
  _M0L6_2atmpS2136 = _M0L2s1S844 ^ _M0L2s2S2137;
  _M0L1rS843->$1 = _M0L6_2atmpS2136;
  _M0L2s3S2139 = _M0L1rS843->$3;
  _M0L6_2atmpS2138 = _M0L2s0S842 ^ _M0L2s3S2139;
  _M0L1rS843->$0 = _M0L6_2atmpS2138;
  _M0L2s2S2141 = _M0L1rS843->$2;
  _M0L6_2atmpS2140 = _M0L2s2S2141 ^ _M0L1tS849;
  _M0L1rS843->$2 = _M0L6_2atmpS2140;
  _M0L2s3S2143 = _M0L1rS843->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2142 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2143, 45);
  _M0L1rS843->$3 = _M0L6_2atmpS2142;
  return _M0L3resS848;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS840, int32_t _M0L1kS841) {
  uint64_t _M0L6_2atmpS2131;
  int32_t _M0L6_2atmpS2133;
  uint64_t _M0L6_2atmpS2132;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2131 = _M0L1xS840 << (_M0L1kS841 & 63);
  _M0L6_2atmpS2133 = 64 - _M0L1kS841;
  _M0L6_2atmpS2132 = _M0L1xS840 >> (_M0L6_2atmpS2133 & 63);
  return _M0L6_2atmpS2131 | _M0L6_2atmpS2132;
}

double _M0FPC14math2ln(double _M0L1xS826) {
  struct _M0TUdiE* _M0L7_2abindS827;
  double _M0L5_2af1S828;
  int32_t _M0L5_2akiS829;
  double _M0L1fS831;
  double _M0L1kS832;
  double _M0L6_2atmpS2124;
  double _M0L1sS833;
  double _M0L2s2S834;
  double _M0L2s4S835;
  double _M0L6_2atmpS2123;
  double _M0L6_2atmpS2122;
  double _M0L6_2atmpS2121;
  double _M0L6_2atmpS2120;
  double _M0L6_2atmpS2119;
  double _M0L6_2atmpS2118;
  double _M0L2t1S836;
  double _M0L6_2atmpS2117;
  double _M0L6_2atmpS2116;
  double _M0L6_2atmpS2115;
  double _M0L6_2atmpS2114;
  double _M0L2t2S837;
  double _M0L1rS838;
  double _M0L6_2atmpS2113;
  double _M0L4hfsqS839;
  double _M0L6_2atmpS2106;
  double _M0L6_2atmpS2112;
  double _M0L6_2atmpS2110;
  double _M0L6_2atmpS2111;
  double _M0L6_2atmpS2109;
  double _M0L6_2atmpS2108;
  double _M0L6_2atmpS2107;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS826 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS826)
      || _M0MPC16double6Double7is__inf(_M0L1xS826)
    ) {
      return _M0L1xS826;
    } else if (_M0L1xS826 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS827 = _M0FPC14math5frexp(_M0L1xS826);
  _M0L5_2af1S828 = _M0L7_2abindS827->$0;
  _M0L5_2akiS829 = _M0L7_2abindS827->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS827);
  if (_M0L5_2af1S828 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2128 = _M0L5_2af1S828 * 0x1p+1;
    double _M0L6_2atmpS2125 = _M0L6_2atmpS2128 - 0x1p+0;
    int32_t _M0L6_2atmpS2127 = _M0L5_2akiS829 - 1;
    double _M0L6_2atmpS2126 = (double)_M0L6_2atmpS2127;
    _M0L1fS831 = _M0L6_2atmpS2125;
    _M0L1kS832 = _M0L6_2atmpS2126;
    goto join_830;
  } else {
    double _M0L6_2atmpS2129 = _M0L5_2af1S828 - 0x1p+0;
    double _M0L6_2atmpS2130 = (double)_M0L5_2akiS829;
    _M0L1fS831 = _M0L6_2atmpS2129;
    _M0L1kS832 = _M0L6_2atmpS2130;
    goto join_830;
  }
  join_830:;
  _M0L6_2atmpS2124 = 0x1p+1 + _M0L1fS831;
  _M0L1sS833 = _M0L1fS831 / _M0L6_2atmpS2124;
  _M0L2s2S834 = _M0L1sS833 * _M0L1sS833;
  _M0L2s4S835 = _M0L2s2S834 * _M0L2s2S834;
  _M0L6_2atmpS2123 = _M0L2s4S835 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2122 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2123;
  _M0L6_2atmpS2121 = _M0L2s4S835 * _M0L6_2atmpS2122;
  _M0L6_2atmpS2120 = 0x1.2492494229359p-2 + _M0L6_2atmpS2121;
  _M0L6_2atmpS2119 = _M0L2s4S835 * _M0L6_2atmpS2120;
  _M0L6_2atmpS2118 = 0x1.5555555555593p-1 + _M0L6_2atmpS2119;
  _M0L2t1S836 = _M0L2s2S834 * _M0L6_2atmpS2118;
  _M0L6_2atmpS2117 = _M0L2s4S835 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2116 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2117;
  _M0L6_2atmpS2115 = _M0L2s4S835 * _M0L6_2atmpS2116;
  _M0L6_2atmpS2114 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2115;
  _M0L2t2S837 = _M0L2s4S835 * _M0L6_2atmpS2114;
  _M0L1rS838 = _M0L2t1S836 + _M0L2t2S837;
  _M0L6_2atmpS2113 = 0x1p-1 * _M0L1fS831;
  _M0L4hfsqS839 = _M0L6_2atmpS2113 * _M0L1fS831;
  _M0L6_2atmpS2106 = _M0L1kS832 * 0x1.62e42feep-1;
  _M0L6_2atmpS2112 = _M0L4hfsqS839 + _M0L1rS838;
  _M0L6_2atmpS2110 = _M0L1sS833 * _M0L6_2atmpS2112;
  _M0L6_2atmpS2111 = _M0L1kS832 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2109 = _M0L6_2atmpS2110 + _M0L6_2atmpS2111;
  _M0L6_2atmpS2108 = _M0L4hfsqS839 - _M0L6_2atmpS2109;
  _M0L6_2atmpS2107 = _M0L6_2atmpS2108 - _M0L1fS831;
  return _M0L6_2atmpS2106 - _M0L6_2atmpS2107;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS819) {
  struct _M0TUdiE* _M0L7_2abindS820;
  double _M0L10_2anorm__fS821;
  int32_t _M0L6_2aexpS822;
  uint64_t _M0L1uS823;
  uint64_t _M0L6_2atmpS2105;
  uint64_t _M0L6_2atmpS2104;
  int32_t _M0L6_2atmpS2103;
  int32_t _M0L6_2atmpS2102;
  int32_t _M0L3expS824;
  uint64_t _M0L6_2atmpS2101;
  uint64_t _M0L6_2atmpS2100;
  uint64_t _M0L6_2atmpS2099;
  double _M0L4fracS825;
  struct _M0TUdiE* _block_2619;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS819 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS819)
    || _M0MPC16double6Double7is__nan(_M0L1fS819)
  ) {
    struct _M0TUdiE* _block_2618 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2618)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2618->$0 = _M0L1fS819;
    _block_2618->$1 = 0;
    return _block_2618;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS820 = _M0FPC14math9normalize(_M0L1fS819);
  _M0L10_2anorm__fS821 = _M0L7_2abindS820->$0;
  _M0L6_2aexpS822 = _M0L7_2abindS820->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS820);
  _M0L1uS823 = *(int64_t*)&_M0L10_2anorm__fS821;
  _M0L6_2atmpS2105 = _M0L1uS823 >> 52;
  _M0L6_2atmpS2104 = _M0L6_2atmpS2105 & 2047ull;
  _M0L6_2atmpS2103 = (int32_t)_M0L6_2atmpS2104;
  _M0L6_2atmpS2102 = _M0L6_2aexpS822 + _M0L6_2atmpS2103;
  _M0L3expS824 = _M0L6_2atmpS2102 - 1022;
  _M0L6_2atmpS2101 = ~9218868437227405312ull;
  _M0L6_2atmpS2100 = _M0L1uS823 & _M0L6_2atmpS2101;
  _M0L6_2atmpS2099 = _M0L6_2atmpS2100 | 4602678819172646912ull;
  _M0L4fracS825 = *(double*)&_M0L6_2atmpS2099;
  _block_2619 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2619)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2619->$0 = _M0L4fracS825;
  _block_2619->$1 = _M0L3expS824;
  return _block_2619;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS818) {
  double _M0L6_2atmpS2096;
  struct _M0TUdiE* _block_2621;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2096 = fabs(_M0L1fS818);
  if (_M0L6_2atmpS2096 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2098 = (double)4503599627370496ll;
    double _M0L6_2atmpS2097 = _M0L1fS818 * _M0L6_2atmpS2098;
    struct _M0TUdiE* _block_2620 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2620)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2620->$0 = _M0L6_2atmpS2097;
    _block_2620->$1 = -52;
    return _block_2620;
  }
  _block_2621 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2621)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2621->$0 = _M0L1fS818;
  _block_2621->$1 = 0;
  return _block_2621;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS817) {
  double _M0L6_2atmpS2095;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2095 = (double)_M0L4selfS817;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2095);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS816) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS816 != _M0L4selfS816) {
    return 0;
  } else if (_M0L4selfS816 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS816 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS816;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS797,
  float _M0L4elemS799
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS796;
  int32_t _M0L1iS798;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS796 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS797);
  _M0L1iS798 = 0;
  while (1) {
    if (_M0L1iS798 < _M0L3lenS797) {
      float* _M0L3bufS2087 = _M0L3arrS796->$0;
      int32_t _M0L6_2atmpS2088;
      _M0L3bufS2087[_M0L1iS798] = _M0L4elemS799;
      _M0L6_2atmpS2088 = _M0L1iS798 + 1;
      _M0L1iS798 = _M0L6_2atmpS2088;
      continue;
    }
    break;
  }
  return _M0L3arrS796;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS802,
  int32_t _M0L4elemS804
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS801;
  int32_t _M0L1iS803;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS801 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS802);
  _M0L1iS803 = 0;
  while (1) {
    if (_M0L1iS803 < _M0L3lenS802) {
      uint8_t* _M0L3bufS2089 = _M0L3arrS801->$0;
      int32_t _M0L6_2atmpS2090;
      _M0L3bufS2089[_M0L1iS803] = _M0L4elemS804;
      _M0L6_2atmpS2090 = _M0L1iS803 + 1;
      _M0L1iS803 = _M0L6_2atmpS2090;
      continue;
    }
    break;
  }
  return _M0L3arrS801;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS807,
  int32_t _M0L4elemS809
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS806;
  int32_t _M0L1iS808;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS806 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS807);
  _M0L1iS808 = 0;
  while (1) {
    if (_M0L1iS808 < _M0L3lenS807) {
      int32_t* _M0L3bufS2091 = _M0L3arrS806->$0;
      int32_t _M0L6_2atmpS2092;
      _M0L3bufS2091[_M0L1iS808] = _M0L4elemS809;
      _M0L6_2atmpS2092 = _M0L1iS808 + 1;
      _M0L1iS808 = _M0L6_2atmpS2092;
      continue;
    }
    break;
  }
  return _M0L3arrS806;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS812,
  struct _M0TPB5ArrayGfE* _M0L4elemS814
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS811;
  int32_t _M0L1iS813;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS811
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS812);
  _M0L1iS813 = 0;
  while (1) {
    if (_M0L1iS813 < _M0L3lenS812) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2093 = _M0L3arrS811->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2517 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2093[_M0L1iS813];
      int32_t _M0L6_2atmpS2094;
      moonbit_incref_cycle_free(_M0L4elemS814);
      if (_M0L6_2aoldS2517) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2517);
      }
      _M0L3bufS2093[_M0L1iS813] = _M0L4elemS814;
      _M0L6_2atmpS2094 = _M0L1iS813 + 1;
      _M0L1iS813 = _M0L6_2atmpS2094;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS814);
    }
    break;
  }
  return _M0L3arrS811;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS781,
  int32_t _M0L5indexS782,
  float _M0L5valueS783
) {
  int32_t _M0L3lenS780;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS780 = _M0L4selfS781->$1;
  if (_M0L5indexS782 >= 0 && _M0L5indexS782 < _M0L3lenS780) {
    float* _M0L6_2atmpS2083;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2083 = _M0MPC15array5Array6bufferGfE(_M0L4selfS781);
    _M0L6_2atmpS2083[_M0L5indexS782] = _M0L5valueS783;
    moonbit_decref_cycle_free(_M0L6_2atmpS2083);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS785,
  int32_t _M0L5indexS786,
  struct _M0TPB5ArrayGfE* _M0L5valueS787
) {
  int32_t _M0L3lenS784;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS784 = _M0L4selfS785->$1;
  if (_M0L5indexS786 >= 0 && _M0L5indexS786 < _M0L3lenS784) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2084;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2518;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2084
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS785);
    _M0L6_2aoldS2518
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2084[_M0L5indexS786];
    if (_M0L6_2aoldS2518) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2518);
    }
    _M0L6_2atmpS2084[_M0L5indexS786] = _M0L5valueS787;
    moonbit_decref_cycle_free(_M0L6_2atmpS2084);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS787);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS789,
  int32_t _M0L5indexS790,
  int32_t _M0L5valueS791
) {
  int32_t _M0L3lenS788;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS788 = _M0L4selfS789->$1;
  if (_M0L5indexS790 >= 0 && _M0L5indexS790 < _M0L3lenS788) {
    int32_t* _M0L6_2atmpS2085;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2085 = _M0MPC15array5Array6bufferGiE(_M0L4selfS789);
    _M0L6_2atmpS2085[_M0L5indexS790] = _M0L5valueS791;
    moonbit_decref_cycle_free(_M0L6_2atmpS2085);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS793,
  int32_t _M0L5indexS794,
  int32_t _M0L5valueS795
) {
  int32_t _M0L3lenS792;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS792 = _M0L4selfS793->$1;
  if (_M0L5indexS794 >= 0 && _M0L5indexS794 < _M0L3lenS792) {
    uint8_t* _M0L6_2atmpS2086;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2086 = _M0MPC15array5Array6bufferGbE(_M0L4selfS793);
    _M0L6_2atmpS2086[_M0L5indexS794] = _M0L5valueS795;
    moonbit_decref_cycle_free(_M0L6_2atmpS2086);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS763,
  int32_t _M0L5indexS764
) {
  int32_t _M0L3lenS762;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS762 = _M0L4selfS763->$1;
  if (_M0L5indexS764 >= 0 && _M0L5indexS764 < _M0L3lenS762) {
    float* _M0L6_2atmpS2077;
    float _result_2626;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2077 = _M0MPC15array5Array6bufferGfE(_M0L4selfS763);
    _result_2626 = (float)_M0L6_2atmpS2077[_M0L5indexS764];
    moonbit_decref_cycle_free(_M0L6_2atmpS2077);
    return _result_2626;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS766,
  int32_t _M0L5indexS767
) {
  int32_t _M0L3lenS765;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS765 = _M0L4selfS766->$1;
  if (_M0L5indexS767 >= 0 && _M0L5indexS767 < _M0L3lenS765) {
    uint8_t* _M0L6_2atmpS2078;
    int32_t _result_2627;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2078 = _M0MPC15array5Array6bufferGbE(_M0L4selfS766);
    _result_2627 = (int32_t)_M0L6_2atmpS2078[_M0L5indexS767];
    moonbit_decref_cycle_free(_M0L6_2atmpS2078);
    return _result_2627;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0MPC15array5Array2atGRP26RiantR8snn__mbt11MonitorAdExE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* _M0L4selfS769,
  int32_t _M0L5indexS770
) {
  int32_t _M0L3lenS768;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS768 = _M0L4selfS769->$1;
  if (_M0L5indexS770 >= 0 && _M0L5indexS770 < _M0L3lenS768) {
    struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0L6_2atmpS2079;
    struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0L6_2atmpS2519;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2079
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt11MonitorAdExE(_M0L4selfS769);
    _M0L6_2atmpS2519
    = (struct _M0TP26RiantR8snn__mbt11MonitorAdEx*)_M0L6_2atmpS2079[
        _M0L5indexS770
      ];
    if (_M0L6_2atmpS2519) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2519);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2079);
    return _M0L6_2atmpS2519;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS772,
  int32_t _M0L5indexS773
) {
  int32_t _M0L3lenS771;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS771 = _M0L4selfS772->$1;
  if (_M0L5indexS773 >= 0 && _M0L5indexS773 < _M0L3lenS771) {
    moonbit_string_t* _M0L6_2atmpS2080;
    moonbit_string_t _M0L6_2atmpS2520;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2080 = _M0MPC15array5Array6bufferGsE(_M0L4selfS772);
    _M0L6_2atmpS2520 = (moonbit_string_t)_M0L6_2atmpS2080[_M0L5indexS773];
    moonbit_incref_cycle_free(_M0L6_2atmpS2520);
    moonbit_decref_cycle_free(_M0L6_2atmpS2080);
    return _M0L6_2atmpS2520;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS775,
  int32_t _M0L5indexS776
) {
  int32_t _M0L3lenS774;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS774 = _M0L4selfS775->$1;
  if (_M0L5indexS776 >= 0 && _M0L5indexS776 < _M0L3lenS774) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2081;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2521;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2081
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS775);
    _M0L6_2atmpS2521
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2081[_M0L5indexS776];
    if (_M0L6_2atmpS2521) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2521);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2081);
    return _M0L6_2atmpS2521;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS778,
  int32_t _M0L5indexS779
) {
  int32_t _M0L3lenS777;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS777 = _M0L4selfS778->$1;
  if (_M0L5indexS779 >= 0 && _M0L5indexS779 < _M0L3lenS777) {
    int32_t* _M0L6_2atmpS2082;
    int32_t _result_2628;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2082 = _M0MPC15array5Array6bufferGiE(_M0L4selfS778);
    _result_2628 = (int32_t)_M0L6_2atmpS2082[_M0L5indexS779];
    moonbit_decref_cycle_free(_M0L6_2atmpS2082);
    return _result_2628;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS761) {
  moonbit_string_t _M0L6_2atmpS2076;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2076 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS761);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2076);
  moonbit_decref_cycle_free(_M0L6_2atmpS2076);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS760) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS760);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS759) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS759 > _M0FPB18double__max__value
         || _M0L4selfS759 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS758) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS758 != _M0L4selfS758;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS743) {
  uint64_t _M0L4bitsS746;
  uint64_t _M0L6_2atmpS2075;
  uint64_t _M0L6_2atmpS2074;
  int32_t _M0L8ieeeSignS747;
  uint64_t _M0L12ieeeMantissaS748;
  uint64_t _M0L6_2atmpS2073;
  uint64_t _M0L6_2atmpS2072;
  int32_t _M0L12ieeeExponentS749;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS750;
  struct _M0TPB17FloatingDecimal64* _M0L1vS751;
  moonbit_string_t _result_2630;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS743 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L3valS743 >= -0x1p+53 && _M0L3valS743 <= 0x1p+53) {
    if (_M0L3valS743 >= -0x1p+31 && _M0L3valS743 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS744;
      double _M0L6_2atmpS2061;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double7to__int(_M0L3valS743);
      _M0L6_2atmpS2061 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2061 == _M0L3valS743) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS744, 10);
      }
    } else {
      int64_t _M0L1iS745;
      double _M0L6_2atmpS2062;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS745 = _M0MPC16double6Double9to__int64(_M0L3valS743);
      _M0L6_2atmpS2062 = (double)_M0L1iS745;
      if (_M0L6_2atmpS2062 == _M0L3valS743) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS745, 10);
      }
    }
  }
  _M0L4bitsS746 = *(int64_t*)&_M0L3valS743;
  _M0L6_2atmpS2075 = _M0L4bitsS746 >> 63;
  _M0L6_2atmpS2074 = _M0L6_2atmpS2075 & 1ull;
  _M0L8ieeeSignS747 = _M0L6_2atmpS2074 != 0ull;
  _M0L12ieeeMantissaS748 = _M0L4bitsS746 & 4503599627370495ull;
  _M0L6_2atmpS2073 = _M0L4bitsS746 >> 52;
  _M0L6_2atmpS2072 = _M0L6_2atmpS2073 & 2047ull;
  _M0L12ieeeExponentS749 = (int32_t)_M0L6_2atmpS2072;
  if (
    _M0L12ieeeExponentS749 == 2047
    || _M0L12ieeeExponentS749 == 0 && _M0L12ieeeMantissaS748 == 0ull
  ) {
    int32_t _M0L6_2atmpS2063 = _M0L12ieeeExponentS749 != 0;
    int32_t _M0L6_2atmpS2064 = _M0L12ieeeMantissaS748 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS747, _M0L6_2atmpS2063, _M0L6_2atmpS2064);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS750
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS748, _M0L12ieeeExponentS749);
  if (_M0L7_2abindS750 == 0) {
    uint32_t _M0L6_2atmpS2065;
    if (_M0L7_2abindS750) {
      moonbit_decref_cycle_free(_M0L7_2abindS750);
    }
    _M0L6_2atmpS2065 = *(uint32_t*)&_M0L12ieeeExponentS749;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS751 = _M0FPB3d2d(_M0L12ieeeMantissaS748, _M0L6_2atmpS2065);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS752 = _M0L7_2abindS750;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS753 = _M0L7_2aSomeS752;
    struct _M0TPB17FloatingDecimal64* _M0L1xS754 = _M0L4_2afS753;
    while (1) {
      uint64_t _M0L8mantissaS2071 = _M0L1xS754->$0;
      uint64_t _M0L1qS755 = _M0L8mantissaS2071 / 10ull;
      uint64_t _M0L8mantissaS2069 = _M0L1xS754->$0;
      uint64_t _M0L6_2atmpS2070 = 10ull * _M0L1qS755;
      uint64_t _M0L1rS756 = _M0L8mantissaS2069 - _M0L6_2atmpS2070;
      int32_t _M0L8exponentS2068;
      int32_t _M0L6_2atmpS2067;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2066;
      if (_M0L1rS756 != 0ull) {
        _M0L1vS751 = _M0L1xS754;
        break;
      }
      _M0L8exponentS2068 = _M0L1xS754->$1;
      moonbit_decref_cycle_free(_M0L1xS754);
      _M0L6_2atmpS2067 = _M0L8exponentS2068 + 1;
      _M0L6_2atmpS2066
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2066)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2066->$0 = _M0L1qS755;
      _M0L6_2atmpS2066->$1 = _M0L6_2atmpS2067;
      _M0L1xS754 = _M0L6_2atmpS2066;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2630 = _M0FPB9to__chars(_M0L1vS751, _M0L8ieeeSignS747);
  moonbit_decref_cycle_free(_M0L1vS751);
  return _result_2630;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS738,
  int32_t _M0L12ieeeExponentS740
) {
  uint64_t _M0L2m2S737;
  int32_t _M0L6_2atmpS2060;
  int32_t _M0L2e2S739;
  int32_t _M0L6_2atmpS2059;
  uint64_t _M0L6_2atmpS2058;
  uint64_t _M0L4maskS741;
  uint64_t _M0L8fractionS742;
  int32_t _M0L6_2atmpS2057;
  uint64_t _M0L6_2atmpS2056;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2055;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S737 = 4503599627370496ull | _M0L12ieeeMantissaS738;
  _M0L6_2atmpS2060 = _M0L12ieeeExponentS740 - 1023;
  _M0L2e2S739 = _M0L6_2atmpS2060 - 52;
  if (_M0L2e2S739 > 0) {
    return 0;
  }
  if (_M0L2e2S739 < -52) {
    return 0;
  }
  _M0L6_2atmpS2059 = -_M0L2e2S739;
  _M0L6_2atmpS2058 = 1ull << (_M0L6_2atmpS2059 & 63);
  _M0L4maskS741 = _M0L6_2atmpS2058 - 1ull;
  _M0L8fractionS742 = _M0L2m2S737 & _M0L4maskS741;
  if (_M0L8fractionS742 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2057 = -_M0L2e2S739;
  _M0L6_2atmpS2056 = _M0L2m2S737 >> (_M0L6_2atmpS2057 & 63);
  _M0L6_2atmpS2055
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2055)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2055->$0 = _M0L6_2atmpS2056;
  _M0L6_2atmpS2055->$1 = 0;
  return _M0L6_2atmpS2055;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS705,
  int32_t _M0L4signS703
) {
  moonbit_bytes_t _M0L6resultS701;
  int32_t _M0Lm5indexS702;
  uint64_t _M0L6outputS704;
  int32_t _M0L7olengthS706;
  int32_t _M0L8exponentS2054;
  int32_t _M0L6_2atmpS2053;
  int32_t _M0Lm3expS707;
  int32_t _M0L6_2atmpS2052;
  int32_t _M0L6_2atmpS2050;
  int32_t _M0L18scientificNotationS708;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS701 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS702 = 0;
  if (_M0L4signS703) {
    int32_t _M0L6_2atmpS1924 = _M0Lm5indexS702;
    int32_t _M0L6_2atmpS1925;
    if (
      _M0L6_2atmpS1924 < 0
      || _M0L6_2atmpS1924 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1924] = 45;
    _M0L6_2atmpS1925 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS1925 + 1;
  }
  _M0L6outputS704 = _M0L1vS705->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS706 = _M0FPB17decimal__length17(_M0L6outputS704);
  _M0L8exponentS2054 = _M0L1vS705->$1;
  _M0L6_2atmpS2053 = _M0L8exponentS2054 + _M0L7olengthS706;
  _M0Lm3expS707 = _M0L6_2atmpS2053 - 1;
  _M0L6_2atmpS2052 = _M0Lm3expS707;
  if (_M0L6_2atmpS2052 >= -6) {
    int32_t _M0L6_2atmpS2051 = _M0Lm3expS707;
    _M0L6_2atmpS2050 = _M0L6_2atmpS2051 < 21;
  } else {
    _M0L6_2atmpS2050 = 0;
  }
  _M0L18scientificNotationS708 = !_M0L6_2atmpS2050;
  if (_M0L18scientificNotationS708) {
    int32_t _M0L7_2abindS709 = _M0L7olengthS706 - 1;
    uint64_t _M0L6outputS710;
    int32_t _M0L1iS711 = 0;
    uint64_t _M0L6outputS712 = _M0L6outputS704;
    int32_t _M0L6_2atmpS1926;
    int32_t _M0L6_2atmpS1930;
    int32_t _M0L6_2atmpS1929;
    int32_t _M0L6_2atmpS1928;
    int32_t _M0L6_2atmpS1927;
    int32_t _M0L6_2atmpS1934;
    int32_t _M0L6_2atmpS1935;
    int32_t _M0L6_2atmpS1936;
    int32_t _M0L6_2atmpS1937;
    int32_t _M0L6_2atmpS1938;
    int32_t _M0L6_2atmpS1944;
    int32_t _M0L6_2atmpS1977;
    moonbit_string_t _result_2632;
    while (1) {
      if (_M0L1iS711 < _M0L7_2abindS709) {
        uint64_t _M0L1cS713 = _M0L6outputS712 % 10ull;
        int32_t _M0L6_2atmpS1983 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS1982 = _M0L6_2atmpS1983 + _M0L7olengthS706;
        int32_t _M0L6_2atmpS1978 = _M0L6_2atmpS1982 - _M0L1iS711;
        int32_t _M0L6_2atmpS1981 = (int32_t)_M0L1cS713;
        int32_t _M0L6_2atmpS1980 = 48 + _M0L6_2atmpS1981;
        int32_t _M0L6_2atmpS1979 = _M0L6_2atmpS1980 & 0xff;
        int32_t _M0L6_2atmpS1984;
        uint64_t _M0L6_2atmpS1985;
        if (
          _M0L6_2atmpS1978 < 0
          || _M0L6_2atmpS1978 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS1978] = _M0L6_2atmpS1979;
        _M0L6_2atmpS1984 = _M0L1iS711 + 1;
        _M0L6_2atmpS1985 = _M0L6outputS712 / 10ull;
        _M0L1iS711 = _M0L6_2atmpS1984;
        _M0L6outputS712 = _M0L6_2atmpS1985;
        continue;
      } else {
        _M0L6outputS710 = _M0L6outputS712;
      }
      break;
    }
    _M0L6_2atmpS1926 = _M0Lm5indexS702;
    _M0L6_2atmpS1930 = (int32_t)_M0L6outputS710;
    _M0L6_2atmpS1929 = _M0L6_2atmpS1930 % 10;
    _M0L6_2atmpS1928 = 48 + _M0L6_2atmpS1929;
    _M0L6_2atmpS1927 = _M0L6_2atmpS1928 & 0xff;
    if (
      _M0L6_2atmpS1926 < 0
      || _M0L6_2atmpS1926 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1926] = _M0L6_2atmpS1927;
    if (_M0L7olengthS706 > 1) {
      int32_t _M0L6_2atmpS1932 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1931 = _M0L6_2atmpS1932 + 1;
      if (
        _M0L6_2atmpS1931 < 0
        || _M0L6_2atmpS1931 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1931] = 46;
    } else {
      int32_t _M0L6_2atmpS1933 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1933 - 1;
    }
    _M0L6_2atmpS1934 = _M0Lm5indexS702;
    _M0L6_2atmpS1935 = _M0L7olengthS706 + 1;
    _M0Lm5indexS702 = _M0L6_2atmpS1934 + _M0L6_2atmpS1935;
    _M0L6_2atmpS1936 = _M0Lm5indexS702;
    if (
      _M0L6_2atmpS1936 < 0
      || _M0L6_2atmpS1936 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1936] = 101;
    _M0L6_2atmpS1937 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS1937 + 1;
    _M0L6_2atmpS1938 = _M0Lm3expS707;
    if (_M0L6_2atmpS1938 < 0) {
      int32_t _M0L6_2atmpS1939 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1940;
      int32_t _M0L6_2atmpS1941;
      if (
        _M0L6_2atmpS1939 < 0
        || _M0L6_2atmpS1939 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1939] = 45;
      _M0L6_2atmpS1940 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1940 + 1;
      _M0L6_2atmpS1941 = _M0Lm3expS707;
      _M0Lm3expS707 = -_M0L6_2atmpS1941;
    } else {
      int32_t _M0L6_2atmpS1942 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1943;
      if (
        _M0L6_2atmpS1942 < 0
        || _M0L6_2atmpS1942 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1942] = 43;
      _M0L6_2atmpS1943 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1943 + 1;
    }
    _M0L6_2atmpS1944 = _M0Lm3expS707;
    if (_M0L6_2atmpS1944 >= 100) {
      int32_t _M0L6_2atmpS1960 = _M0Lm3expS707;
      int32_t _M0L1aS715 = _M0L6_2atmpS1960 / 100;
      int32_t _M0L6_2atmpS1959 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS1958 = _M0L6_2atmpS1959 / 10;
      int32_t _M0L1bS716 = _M0L6_2atmpS1958 % 10;
      int32_t _M0L6_2atmpS1957 = _M0Lm3expS707;
      int32_t _M0L1cS717 = _M0L6_2atmpS1957 % 10;
      int32_t _M0L6_2atmpS1945 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1947 = 48 + _M0L1aS715;
      int32_t _M0L6_2atmpS1946 = _M0L6_2atmpS1947 & 0xff;
      int32_t _M0L6_2atmpS1951;
      int32_t _M0L6_2atmpS1948;
      int32_t _M0L6_2atmpS1950;
      int32_t _M0L6_2atmpS1949;
      int32_t _M0L6_2atmpS1955;
      int32_t _M0L6_2atmpS1952;
      int32_t _M0L6_2atmpS1954;
      int32_t _M0L6_2atmpS1953;
      int32_t _M0L6_2atmpS1956;
      if (
        _M0L6_2atmpS1945 < 0
        || _M0L6_2atmpS1945 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1945] = _M0L6_2atmpS1946;
      _M0L6_2atmpS1951 = _M0Lm5indexS702;
      _M0L6_2atmpS1948 = _M0L6_2atmpS1951 + 1;
      _M0L6_2atmpS1950 = 48 + _M0L1bS716;
      _M0L6_2atmpS1949 = _M0L6_2atmpS1950 & 0xff;
      if (
        _M0L6_2atmpS1948 < 0
        || _M0L6_2atmpS1948 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1948] = _M0L6_2atmpS1949;
      _M0L6_2atmpS1955 = _M0Lm5indexS702;
      _M0L6_2atmpS1952 = _M0L6_2atmpS1955 + 2;
      _M0L6_2atmpS1954 = 48 + _M0L1cS717;
      _M0L6_2atmpS1953 = _M0L6_2atmpS1954 & 0xff;
      if (
        _M0L6_2atmpS1952 < 0
        || _M0L6_2atmpS1952 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1952] = _M0L6_2atmpS1953;
      _M0L6_2atmpS1956 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1956 + 3;
    } else {
      int32_t _M0L6_2atmpS1961 = _M0Lm3expS707;
      if (_M0L6_2atmpS1961 >= 10) {
        int32_t _M0L6_2atmpS1971 = _M0Lm3expS707;
        int32_t _M0L1aS718 = _M0L6_2atmpS1971 / 10;
        int32_t _M0L6_2atmpS1970 = _M0Lm3expS707;
        int32_t _M0L1bS719 = _M0L6_2atmpS1970 % 10;
        int32_t _M0L6_2atmpS1962 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS1964 = 48 + _M0L1aS718;
        int32_t _M0L6_2atmpS1963 = _M0L6_2atmpS1964 & 0xff;
        int32_t _M0L6_2atmpS1968;
        int32_t _M0L6_2atmpS1965;
        int32_t _M0L6_2atmpS1967;
        int32_t _M0L6_2atmpS1966;
        int32_t _M0L6_2atmpS1969;
        if (
          _M0L6_2atmpS1962 < 0
          || _M0L6_2atmpS1962 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS1962] = _M0L6_2atmpS1963;
        _M0L6_2atmpS1968 = _M0Lm5indexS702;
        _M0L6_2atmpS1965 = _M0L6_2atmpS1968 + 1;
        _M0L6_2atmpS1967 = 48 + _M0L1bS719;
        _M0L6_2atmpS1966 = _M0L6_2atmpS1967 & 0xff;
        if (
          _M0L6_2atmpS1965 < 0
          || _M0L6_2atmpS1965 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS1965] = _M0L6_2atmpS1966;
        _M0L6_2atmpS1969 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS1969 + 2;
      } else {
        int32_t _M0L6_2atmpS1972 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS1975 = _M0Lm3expS707;
        int32_t _M0L6_2atmpS1974 = 48 + _M0L6_2atmpS1975;
        int32_t _M0L6_2atmpS1973 = _M0L6_2atmpS1974 & 0xff;
        int32_t _M0L6_2atmpS1976;
        if (
          _M0L6_2atmpS1972 < 0
          || _M0L6_2atmpS1972 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS1972] = _M0L6_2atmpS1973;
        _M0L6_2atmpS1976 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS1976 + 1;
      }
    }
    _M0L6_2atmpS1977 = _M0Lm5indexS702;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2632
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS1977);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_2632;
  } else {
    int32_t _M0L6_2atmpS1986 = _M0Lm3expS707;
    int32_t _M0L6_2atmpS2049;
    moonbit_string_t _result_2638;
    if (_M0L6_2atmpS1986 < 0) {
      int32_t _M0L6_2atmpS1987 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1989;
      int32_t _M0L6_2atmpS1988;
      int32_t _M0L6_2atmpS1990;
      int32_t _M0L1iS720;
      int32_t _M0L6_2atmpS2005;
      int32_t _M0L6_2atmpS2007;
      int32_t _M0L6_2atmpS2006;
      int32_t _M0L7currentS722;
      int32_t _M0L1iS723;
      uint64_t _M0L6outputS724;
      if (
        _M0L6_2atmpS1987 < 0
        || _M0L6_2atmpS1987 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1987] = 48;
      _M0L6_2atmpS1989 = _M0Lm5indexS702;
      _M0L6_2atmpS1988 = _M0L6_2atmpS1989 + 1;
      if (
        _M0L6_2atmpS1988 < 0
        || _M0L6_2atmpS1988 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1988] = 46;
      _M0L6_2atmpS1990 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1990 + 2;
      _M0L1iS720 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1991 = _M0Lm3expS707;
        if (_M0L1iS720 > _M0L6_2atmpS1991) {
          int32_t _M0L6_2atmpS1994 = _M0Lm5indexS702;
          int32_t _M0L6_2atmpS1993 = _M0L6_2atmpS1994 - _M0L1iS720;
          int32_t _M0L6_2atmpS1992 = _M0L6_2atmpS1993 - 1;
          int32_t _M0L6_2atmpS1995;
          if (
            _M0L6_2atmpS1992 < 0
            || _M0L6_2atmpS1992 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS1992] = 48;
          _M0L6_2atmpS1995 = _M0L1iS720 - 1;
          _M0L1iS720 = _M0L6_2atmpS1995;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2005 = _M0Lm5indexS702;
      _M0L6_2atmpS2007 = _M0Lm3expS707;
      _M0L6_2atmpS2006 = -1 - _M0L6_2atmpS2007;
      _M0L7currentS722 = _M0L6_2atmpS2005 + _M0L6_2atmpS2006;
      _M0L1iS723 = 0;
      _M0L6outputS724 = _M0L6outputS704;
      while (1) {
        if (_M0L1iS723 < _M0L7olengthS706) {
          int32_t _M0L6_2atmpS2002 = _M0L7currentS722 + _M0L7olengthS706;
          int32_t _M0L6_2atmpS2001 = _M0L6_2atmpS2002 - _M0L1iS723;
          int32_t _M0L6_2atmpS1996 = _M0L6_2atmpS2001 - 1;
          uint64_t _M0L6_2atmpS2000 = _M0L6outputS724 % 10ull;
          int32_t _M0L6_2atmpS1999 = (int32_t)_M0L6_2atmpS2000;
          int32_t _M0L6_2atmpS1998 = 48 + _M0L6_2atmpS1999;
          int32_t _M0L6_2atmpS1997 = _M0L6_2atmpS1998 & 0xff;
          int32_t _M0L6_2atmpS2003;
          uint64_t _M0L6_2atmpS2004;
          if (
            _M0L6_2atmpS1996 < 0
            || _M0L6_2atmpS1996 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS1996] = _M0L6_2atmpS1997;
          _M0L6_2atmpS2003 = _M0L1iS723 + 1;
          _M0L6_2atmpS2004 = _M0L6outputS724 / 10ull;
          _M0L1iS723 = _M0L6_2atmpS2003;
          _M0L6outputS724 = _M0L6_2atmpS2004;
          continue;
        }
        break;
      }
      _M0Lm5indexS702 = _M0L7currentS722 + _M0L7olengthS706;
    } else {
      int32_t _M0L6_2atmpS2009 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS2008 = _M0L6_2atmpS2009 + 1;
      if (_M0L6_2atmpS2008 >= _M0L7olengthS706) {
        int32_t _M0L1iS726 = 0;
        uint64_t _M0L6outputS727 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2020;
        int32_t _M0L6_2atmpS2025;
        int32_t _M0L7_2abindS729;
        int32_t _M0L1iS730;
        int32_t _M0L6_2atmpS2026;
        int32_t _M0L6_2atmpS2029;
        int32_t _M0L6_2atmpS2028;
        int32_t _M0L6_2atmpS2027;
        while (1) {
          if (_M0L1iS726 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2017 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2016 = _M0L6_2atmpS2017 + _M0L7olengthS706;
            int32_t _M0L6_2atmpS2015 = _M0L6_2atmpS2016 - _M0L1iS726;
            int32_t _M0L6_2atmpS2010 = _M0L6_2atmpS2015 - 1;
            uint64_t _M0L6_2atmpS2014 = _M0L6outputS727 % 10ull;
            int32_t _M0L6_2atmpS2013 = (int32_t)_M0L6_2atmpS2014;
            int32_t _M0L6_2atmpS2012 = 48 + _M0L6_2atmpS2013;
            int32_t _M0L6_2atmpS2011 = _M0L6_2atmpS2012 & 0xff;
            int32_t _M0L6_2atmpS2018;
            uint64_t _M0L6_2atmpS2019;
            if (
              _M0L6_2atmpS2010 < 0
              || _M0L6_2atmpS2010 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2010] = _M0L6_2atmpS2011;
            _M0L6_2atmpS2018 = _M0L1iS726 + 1;
            _M0L6_2atmpS2019 = _M0L6outputS727 / 10ull;
            _M0L1iS726 = _M0L6_2atmpS2018;
            _M0L6outputS727 = _M0L6_2atmpS2019;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2020 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2020 + _M0L7olengthS706;
        _M0L6_2atmpS2025 = _M0Lm3expS707;
        _M0L7_2abindS729 = _M0L6_2atmpS2025 + 1;
        _M0L1iS730 = _M0L7olengthS706;
        while (1) {
          if (_M0L1iS730 < _M0L7_2abindS729) {
            int32_t _M0L6_2atmpS2023 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2022 = _M0L6_2atmpS2023 + _M0L1iS730;
            int32_t _M0L6_2atmpS2021 = _M0L6_2atmpS2022 - _M0L7olengthS706;
            int32_t _M0L6_2atmpS2024;
            if (
              _M0L6_2atmpS2021 < 0
              || _M0L6_2atmpS2021 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2021] = 48;
            _M0L6_2atmpS2024 = _M0L1iS730 + 1;
            _M0L1iS730 = _M0L6_2atmpS2024;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2026 = _M0Lm5indexS702;
        _M0L6_2atmpS2029 = _M0Lm3expS707;
        _M0L6_2atmpS2028 = _M0L6_2atmpS2029 + 1;
        _M0L6_2atmpS2027 = _M0L6_2atmpS2028 - _M0L7olengthS706;
        _M0Lm5indexS702 = _M0L6_2atmpS2026 + _M0L6_2atmpS2027;
      } else {
        int32_t _M0L6_2atmpS2046 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2045 = _M0L6_2atmpS2046 + 1;
        int32_t _M0L1iS732 = 0;
        int32_t _M0L7currentS733 = _M0L6_2atmpS2045;
        uint64_t _M0L6outputS734 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2047;
        int32_t _M0L6_2atmpS2048;
        while (1) {
          if (_M0L1iS732 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2041 = _M0L7olengthS706 - _M0L1iS732;
            int32_t _M0L6_2atmpS2039 = _M0L6_2atmpS2041 - 1;
            int32_t _M0L6_2atmpS2040 = _M0Lm3expS707;
            int32_t _M0L7currentS735;
            int32_t _M0L6_2atmpS2036;
            int32_t _M0L6_2atmpS2035;
            int32_t _M0L6_2atmpS2030;
            uint64_t _M0L6_2atmpS2034;
            int32_t _M0L6_2atmpS2033;
            int32_t _M0L6_2atmpS2032;
            int32_t _M0L6_2atmpS2031;
            int32_t _M0L6_2atmpS2037;
            uint64_t _M0L6_2atmpS2038;
            if (_M0L6_2atmpS2039 == _M0L6_2atmpS2040) {
              int32_t _M0L6_2atmpS2044 = _M0L7currentS733 + _M0L7olengthS706;
              int32_t _M0L6_2atmpS2043 = _M0L6_2atmpS2044 - _M0L1iS732;
              int32_t _M0L6_2atmpS2042 = _M0L6_2atmpS2043 - 1;
              if (
                _M0L6_2atmpS2042 < 0
                || _M0L6_2atmpS2042 >= Moonbit_array_length(_M0L6resultS701)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS701[_M0L6_2atmpS2042] = 46;
              _M0L7currentS735 = _M0L7currentS733 - 1;
            } else {
              _M0L7currentS735 = _M0L7currentS733;
            }
            _M0L6_2atmpS2036 = _M0L7currentS735 + _M0L7olengthS706;
            _M0L6_2atmpS2035 = _M0L6_2atmpS2036 - _M0L1iS732;
            _M0L6_2atmpS2030 = _M0L6_2atmpS2035 - 1;
            _M0L6_2atmpS2034 = _M0L6outputS734 % 10ull;
            _M0L6_2atmpS2033 = (int32_t)_M0L6_2atmpS2034;
            _M0L6_2atmpS2032 = 48 + _M0L6_2atmpS2033;
            _M0L6_2atmpS2031 = _M0L6_2atmpS2032 & 0xff;
            if (
              _M0L6_2atmpS2030 < 0
              || _M0L6_2atmpS2030 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2030] = _M0L6_2atmpS2031;
            _M0L6_2atmpS2037 = _M0L1iS732 + 1;
            _M0L6_2atmpS2038 = _M0L6outputS734 / 10ull;
            _M0L1iS732 = _M0L6_2atmpS2037;
            _M0L7currentS733 = _M0L7currentS735;
            _M0L6outputS734 = _M0L6_2atmpS2038;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2047 = _M0Lm5indexS702;
        _M0L6_2atmpS2048 = _M0L7olengthS706 + 1;
        _M0Lm5indexS702 = _M0L6_2atmpS2047 + _M0L6_2atmpS2048;
      }
    }
    _M0L6_2atmpS2049 = _M0Lm5indexS702;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2638
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS2049);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_2638;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS647,
  uint32_t _M0L12ieeeExponentS646
) {
  int32_t _M0Lm2e2S644;
  uint64_t _M0Lm2m2S645;
  uint64_t _M0L6_2atmpS1923;
  uint64_t _M0L6_2atmpS1922;
  int32_t _M0L4evenS648;
  uint64_t _M0L6_2atmpS1921;
  uint64_t _M0L2mvS649;
  int32_t _M0L7mmShiftS650;
  uint64_t _M0Lm2vrS651;
  uint64_t _M0Lm2vpS652;
  uint64_t _M0Lm2vmS653;
  int32_t _M0Lm3e10S654;
  int32_t _M0Lm17vmIsTrailingZerosS655;
  int32_t _M0Lm17vrIsTrailingZerosS656;
  int32_t _M0L6_2atmpS1823;
  int32_t _M0Lm7removedS675;
  int32_t _M0Lm16lastRemovedDigitS676;
  uint64_t _M0Lm6outputS677;
  int32_t _M0L6_2atmpS1919;
  int32_t _M0L6_2atmpS1920;
  int32_t _M0L3expS700;
  uint64_t _M0L6_2atmpS1918;
  struct _M0TPB17FloatingDecimal64* _block_2644;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S644 = 0;
  _M0Lm2m2S645 = 0ull;
  if (_M0L12ieeeExponentS646 == 0u) {
    _M0Lm2e2S644 = -1076;
    _M0Lm2m2S645 = _M0L12ieeeMantissaS647;
  } else {
    int32_t _M0L6_2atmpS1822 = *(int32_t*)&_M0L12ieeeExponentS646;
    int32_t _M0L6_2atmpS1821 = _M0L6_2atmpS1822 - 1023;
    int32_t _M0L6_2atmpS1820 = _M0L6_2atmpS1821 - 52;
    _M0Lm2e2S644 = _M0L6_2atmpS1820 - 2;
    _M0Lm2m2S645 = 4503599627370496ull | _M0L12ieeeMantissaS647;
  }
  _M0L6_2atmpS1923 = _M0Lm2m2S645;
  _M0L6_2atmpS1922 = _M0L6_2atmpS1923 & 1ull;
  _M0L4evenS648 = _M0L6_2atmpS1922 == 0ull;
  _M0L6_2atmpS1921 = _M0Lm2m2S645;
  _M0L2mvS649 = 4ull * _M0L6_2atmpS1921;
  _M0L7mmShiftS650
  = _M0L12ieeeMantissaS647 != 0ull || _M0L12ieeeExponentS646 <= 1u;
  _M0Lm2vrS651 = 0ull;
  _M0Lm2vpS652 = 0ull;
  _M0Lm2vmS653 = 0ull;
  _M0Lm3e10S654 = 0;
  _M0Lm17vmIsTrailingZerosS655 = 0;
  _M0Lm17vrIsTrailingZerosS656 = 0;
  _M0L6_2atmpS1823 = _M0Lm2e2S644;
  if (_M0L6_2atmpS1823 >= 0) {
    int32_t _M0L6_2atmpS1845 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1841;
    int32_t _M0L6_2atmpS1844;
    int32_t _M0L6_2atmpS1843;
    int32_t _M0L6_2atmpS1842;
    int32_t _M0L1qS657;
    int32_t _M0L6_2atmpS1840;
    int32_t _M0L6_2atmpS1839;
    int32_t _M0L1kS658;
    int32_t _M0L6_2atmpS1838;
    int32_t _M0L6_2atmpS1837;
    int32_t _M0L6_2atmpS1836;
    int32_t _M0L1iS659;
    struct _M0TPB8Pow5Pair _M0L4pow5S660;
    uint64_t _M0L6_2atmpS1835;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS661;
    uint64_t _M0L8_2avrOutS662;
    uint64_t _M0L8_2avpOutS663;
    uint64_t _M0L8_2avmOutS664;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1841 = _M0FPB9log10Pow2(_M0L6_2atmpS1845);
    _M0L6_2atmpS1844 = _M0Lm2e2S644;
    _M0L6_2atmpS1843 = _M0L6_2atmpS1844 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1842 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1843);
    _M0L1qS657 = _M0L6_2atmpS1841 - _M0L6_2atmpS1842;
    _M0Lm3e10S654 = _M0L1qS657;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1840 = _M0FPB8pow5bits(_M0L1qS657);
    _M0L6_2atmpS1839 = 125 + _M0L6_2atmpS1840;
    _M0L1kS658 = _M0L6_2atmpS1839 - 1;
    _M0L6_2atmpS1838 = _M0Lm2e2S644;
    _M0L6_2atmpS1837 = -_M0L6_2atmpS1838;
    _M0L6_2atmpS1836 = _M0L6_2atmpS1837 + _M0L1qS657;
    _M0L1iS659 = _M0L6_2atmpS1836 + _M0L1kS658;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S660 = _M0FPB22double__computeInvPow5(_M0L1qS657);
    _M0L6_2atmpS1835 = _M0Lm2m2S645;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS661
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1835, _M0L4pow5S660, _M0L1iS659, _M0L7mmShiftS650);
    _M0L8_2avrOutS662 = _M0L7_2abindS661.$0;
    _M0L8_2avpOutS663 = _M0L7_2abindS661.$1;
    _M0L8_2avmOutS664 = _M0L7_2abindS661.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS662;
    _M0Lm2vpS652 = _M0L8_2avpOutS663;
    _M0Lm2vmS653 = _M0L8_2avmOutS664;
    if (_M0L1qS657 <= 21) {
      int32_t _M0L6_2atmpS1831 = (int32_t)_M0L2mvS649;
      uint64_t _M0L6_2atmpS1834 = _M0L2mvS649 / 5ull;
      int32_t _M0L6_2atmpS1833 = (int32_t)_M0L6_2atmpS1834;
      int32_t _M0L6_2atmpS1832 = 5 * _M0L6_2atmpS1833;
      int32_t _M0L6mvMod5S665 = _M0L6_2atmpS1831 - _M0L6_2atmpS1832;
      if (_M0L6mvMod5S665 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS656
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS649, _M0L1qS657);
      } else if (_M0L4evenS648) {
        uint64_t _M0L6_2atmpS1825 = _M0L2mvS649 - 1ull;
        uint64_t _M0L6_2atmpS1826;
        uint64_t _M0L6_2atmpS1824;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1826 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS650);
        _M0L6_2atmpS1824 = _M0L6_2atmpS1825 - _M0L6_2atmpS1826;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1824, _M0L1qS657);
      } else {
        uint64_t _M0L6_2atmpS1827 = _M0Lm2vpS652;
        uint64_t _M0L6_2atmpS1830 = _M0L2mvS649 + 2ull;
        int32_t _M0L6_2atmpS1829;
        uint64_t _M0L6_2atmpS1828;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1829
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1830, _M0L1qS657);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1828 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1829);
        _M0Lm2vpS652 = _M0L6_2atmpS1827 - _M0L6_2atmpS1828;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1859 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1858 = -_M0L6_2atmpS1859;
    int32_t _M0L6_2atmpS1853;
    int32_t _M0L6_2atmpS1857;
    int32_t _M0L6_2atmpS1856;
    int32_t _M0L6_2atmpS1855;
    int32_t _M0L6_2atmpS1854;
    int32_t _M0L1qS666;
    int32_t _M0L6_2atmpS1846;
    int32_t _M0L6_2atmpS1852;
    int32_t _M0L6_2atmpS1851;
    int32_t _M0L1iS667;
    int32_t _M0L6_2atmpS1850;
    int32_t _M0L1kS668;
    int32_t _M0L1jS669;
    struct _M0TPB8Pow5Pair _M0L4pow5S670;
    uint64_t _M0L6_2atmpS1849;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS671;
    uint64_t _M0L8_2avrOutS672;
    uint64_t _M0L8_2avpOutS673;
    uint64_t _M0L8_2avmOutS674;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1853 = _M0FPB9log10Pow5(_M0L6_2atmpS1858);
    _M0L6_2atmpS1857 = _M0Lm2e2S644;
    _M0L6_2atmpS1856 = -_M0L6_2atmpS1857;
    _M0L6_2atmpS1855 = _M0L6_2atmpS1856 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1854 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1855);
    _M0L1qS666 = _M0L6_2atmpS1853 - _M0L6_2atmpS1854;
    _M0L6_2atmpS1846 = _M0Lm2e2S644;
    _M0Lm3e10S654 = _M0L1qS666 + _M0L6_2atmpS1846;
    _M0L6_2atmpS1852 = _M0Lm2e2S644;
    _M0L6_2atmpS1851 = -_M0L6_2atmpS1852;
    _M0L1iS667 = _M0L6_2atmpS1851 - _M0L1qS666;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1850 = _M0FPB8pow5bits(_M0L1iS667);
    _M0L1kS668 = _M0L6_2atmpS1850 - 125;
    _M0L1jS669 = _M0L1qS666 - _M0L1kS668;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S670 = _M0FPB19double__computePow5(_M0L1iS667);
    _M0L6_2atmpS1849 = _M0Lm2m2S645;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS671
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1849, _M0L4pow5S670, _M0L1jS669, _M0L7mmShiftS650);
    _M0L8_2avrOutS672 = _M0L7_2abindS671.$0;
    _M0L8_2avpOutS673 = _M0L7_2abindS671.$1;
    _M0L8_2avmOutS674 = _M0L7_2abindS671.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS672;
    _M0Lm2vpS652 = _M0L8_2avpOutS673;
    _M0Lm2vmS653 = _M0L8_2avmOutS674;
    if (_M0L1qS666 <= 1) {
      _M0Lm17vrIsTrailingZerosS656 = 1;
      if (_M0L4evenS648) {
        int32_t _M0L6_2atmpS1847;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1847 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS650);
        _M0Lm17vmIsTrailingZerosS655 = _M0L6_2atmpS1847 == 1;
      } else {
        uint64_t _M0L6_2atmpS1848 = _M0Lm2vpS652;
        _M0Lm2vpS652 = _M0L6_2atmpS1848 - 1ull;
      }
    } else if (_M0L1qS666 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS656
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS649, _M0L1qS666);
    }
  }
  _M0Lm7removedS675 = 0;
  _M0Lm16lastRemovedDigitS676 = 0;
  _M0Lm6outputS677 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS655 || _M0Lm17vrIsTrailingZerosS656) {
    int32_t _if__result_2641;
    uint64_t _M0L6_2atmpS1889;
    uint64_t _M0L6_2atmpS1895;
    uint64_t _M0L6_2atmpS1896;
    int32_t _if__result_2642;
    int32_t _M0L6_2atmpS1892;
    int64_t _M0L6_2atmpS1891;
    uint64_t _M0L6_2atmpS1890;
    while (1) {
      uint64_t _M0L6_2atmpS1872 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S678 = _M0L6_2atmpS1872 / 10ull;
      uint64_t _M0L6_2atmpS1871 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S679 = _M0L6_2atmpS1871 / 10ull;
      uint64_t _M0L6_2atmpS1870;
      int32_t _M0L6_2atmpS1867;
      int32_t _M0L6_2atmpS1869;
      int32_t _M0L6_2atmpS1868;
      int32_t _M0L7vmMod10S681;
      uint64_t _M0L6_2atmpS1866;
      uint64_t _M0L7vrDiv10S682;
      uint64_t _M0L6_2atmpS1865;
      int32_t _M0L6_2atmpS1862;
      int32_t _M0L6_2atmpS1864;
      int32_t _M0L6_2atmpS1863;
      int32_t _M0L7vrMod10S683;
      int32_t _M0L6_2atmpS1861;
      if (_M0L7vpDiv10S678 <= _M0L7vmDiv10S679) {
        break;
      }
      _M0L6_2atmpS1870 = _M0Lm2vmS653;
      _M0L6_2atmpS1867 = (int32_t)_M0L6_2atmpS1870;
      _M0L6_2atmpS1869 = (int32_t)_M0L7vmDiv10S679;
      _M0L6_2atmpS1868 = 10 * _M0L6_2atmpS1869;
      _M0L7vmMod10S681 = _M0L6_2atmpS1867 - _M0L6_2atmpS1868;
      _M0L6_2atmpS1866 = _M0Lm2vrS651;
      _M0L7vrDiv10S682 = _M0L6_2atmpS1866 / 10ull;
      _M0L6_2atmpS1865 = _M0Lm2vrS651;
      _M0L6_2atmpS1862 = (int32_t)_M0L6_2atmpS1865;
      _M0L6_2atmpS1864 = (int32_t)_M0L7vrDiv10S682;
      _M0L6_2atmpS1863 = 10 * _M0L6_2atmpS1864;
      _M0L7vrMod10S683 = _M0L6_2atmpS1862 - _M0L6_2atmpS1863;
      _M0Lm17vmIsTrailingZerosS655
      = _M0Lm17vmIsTrailingZerosS655 && _M0L7vmMod10S681 == 0;
      if (_M0Lm17vrIsTrailingZerosS656) {
        int32_t _M0L6_2atmpS1860 = _M0Lm16lastRemovedDigitS676;
        _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS1860 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS656 = 0;
      }
      _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S683;
      _M0Lm2vrS651 = _M0L7vrDiv10S682;
      _M0Lm2vpS652 = _M0L7vpDiv10S678;
      _M0Lm2vmS653 = _M0L7vmDiv10S679;
      _M0L6_2atmpS1861 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1861 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS655) {
      while (1) {
        uint64_t _M0L6_2atmpS1885 = _M0Lm2vmS653;
        uint64_t _M0L7vmDiv10S684 = _M0L6_2atmpS1885 / 10ull;
        uint64_t _M0L6_2atmpS1884 = _M0Lm2vmS653;
        int32_t _M0L6_2atmpS1881 = (int32_t)_M0L6_2atmpS1884;
        int32_t _M0L6_2atmpS1883 = (int32_t)_M0L7vmDiv10S684;
        int32_t _M0L6_2atmpS1882 = 10 * _M0L6_2atmpS1883;
        int32_t _M0L7vmMod10S685 = _M0L6_2atmpS1881 - _M0L6_2atmpS1882;
        uint64_t _M0L6_2atmpS1880;
        uint64_t _M0L7vpDiv10S687;
        uint64_t _M0L6_2atmpS1879;
        uint64_t _M0L7vrDiv10S688;
        uint64_t _M0L6_2atmpS1878;
        int32_t _M0L6_2atmpS1875;
        int32_t _M0L6_2atmpS1877;
        int32_t _M0L6_2atmpS1876;
        int32_t _M0L7vrMod10S689;
        int32_t _M0L6_2atmpS1874;
        if (_M0L7vmMod10S685 != 0) {
          break;
        }
        _M0L6_2atmpS1880 = _M0Lm2vpS652;
        _M0L7vpDiv10S687 = _M0L6_2atmpS1880 / 10ull;
        _M0L6_2atmpS1879 = _M0Lm2vrS651;
        _M0L7vrDiv10S688 = _M0L6_2atmpS1879 / 10ull;
        _M0L6_2atmpS1878 = _M0Lm2vrS651;
        _M0L6_2atmpS1875 = (int32_t)_M0L6_2atmpS1878;
        _M0L6_2atmpS1877 = (int32_t)_M0L7vrDiv10S688;
        _M0L6_2atmpS1876 = 10 * _M0L6_2atmpS1877;
        _M0L7vrMod10S689 = _M0L6_2atmpS1875 - _M0L6_2atmpS1876;
        if (_M0Lm17vrIsTrailingZerosS656) {
          int32_t _M0L6_2atmpS1873 = _M0Lm16lastRemovedDigitS676;
          _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS1873 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS656 = 0;
        }
        _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S689;
        _M0Lm2vrS651 = _M0L7vrDiv10S688;
        _M0Lm2vpS652 = _M0L7vpDiv10S687;
        _M0Lm2vmS653 = _M0L7vmDiv10S684;
        _M0L6_2atmpS1874 = _M0Lm7removedS675;
        _M0Lm7removedS675 = _M0L6_2atmpS1874 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS656) {
      int32_t _M0L6_2atmpS1888 = _M0Lm16lastRemovedDigitS676;
      if (_M0L6_2atmpS1888 == 5) {
        uint64_t _M0L6_2atmpS1887 = _M0Lm2vrS651;
        uint64_t _M0L6_2atmpS1886 = _M0L6_2atmpS1887 % 2ull;
        _if__result_2641 = _M0L6_2atmpS1886 == 0ull;
      } else {
        _if__result_2641 = 0;
      }
    } else {
      _if__result_2641 = 0;
    }
    if (_if__result_2641) {
      _M0Lm16lastRemovedDigitS676 = 4;
    }
    _M0L6_2atmpS1889 = _M0Lm2vrS651;
    _M0L6_2atmpS1895 = _M0Lm2vrS651;
    _M0L6_2atmpS1896 = _M0Lm2vmS653;
    if (_M0L6_2atmpS1895 == _M0L6_2atmpS1896) {
      if (!_M0L4evenS648) {
        _if__result_2642 = 1;
      } else {
        int32_t _M0L6_2atmpS1894 = _M0Lm17vmIsTrailingZerosS655;
        _if__result_2642 = !_M0L6_2atmpS1894;
      }
    } else {
      _if__result_2642 = 0;
    }
    if (_if__result_2642) {
      _M0L6_2atmpS1892 = 1;
    } else {
      int32_t _M0L6_2atmpS1893 = _M0Lm16lastRemovedDigitS676;
      _M0L6_2atmpS1892 = _M0L6_2atmpS1893 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1891 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1892);
    _M0L6_2atmpS1890 = *(uint64_t*)&_M0L6_2atmpS1891;
    _M0Lm6outputS677 = _M0L6_2atmpS1889 + _M0L6_2atmpS1890;
  } else {
    int32_t _M0Lm7roundUpS690 = 0;
    uint64_t _M0L6_2atmpS1917 = _M0Lm2vpS652;
    uint64_t _M0L8vpDiv100S691 = _M0L6_2atmpS1917 / 100ull;
    uint64_t _M0L6_2atmpS1916 = _M0Lm2vmS653;
    uint64_t _M0L8vmDiv100S692 = _M0L6_2atmpS1916 / 100ull;
    uint64_t _M0L6_2atmpS1911;
    uint64_t _M0L6_2atmpS1914;
    uint64_t _M0L6_2atmpS1915;
    int32_t _M0L6_2atmpS1913;
    uint64_t _M0L6_2atmpS1912;
    if (_M0L8vpDiv100S691 > _M0L8vmDiv100S692) {
      uint64_t _M0L6_2atmpS1902 = _M0Lm2vrS651;
      uint64_t _M0L8vrDiv100S693 = _M0L6_2atmpS1902 / 100ull;
      uint64_t _M0L6_2atmpS1901 = _M0Lm2vrS651;
      int32_t _M0L6_2atmpS1898 = (int32_t)_M0L6_2atmpS1901;
      int32_t _M0L6_2atmpS1900 = (int32_t)_M0L8vrDiv100S693;
      int32_t _M0L6_2atmpS1899 = 100 * _M0L6_2atmpS1900;
      int32_t _M0L8vrMod100S694 = _M0L6_2atmpS1898 - _M0L6_2atmpS1899;
      int32_t _M0L6_2atmpS1897;
      _M0Lm7roundUpS690 = _M0L8vrMod100S694 >= 50;
      _M0Lm2vrS651 = _M0L8vrDiv100S693;
      _M0Lm2vpS652 = _M0L8vpDiv100S691;
      _M0Lm2vmS653 = _M0L8vmDiv100S692;
      _M0L6_2atmpS1897 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1897 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1910 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S695 = _M0L6_2atmpS1910 / 10ull;
      uint64_t _M0L6_2atmpS1909 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S696 = _M0L6_2atmpS1909 / 10ull;
      uint64_t _M0L6_2atmpS1908;
      uint64_t _M0L7vrDiv10S698;
      uint64_t _M0L6_2atmpS1907;
      int32_t _M0L6_2atmpS1904;
      int32_t _M0L6_2atmpS1906;
      int32_t _M0L6_2atmpS1905;
      int32_t _M0L7vrMod10S699;
      int32_t _M0L6_2atmpS1903;
      if (_M0L7vpDiv10S695 <= _M0L7vmDiv10S696) {
        break;
      }
      _M0L6_2atmpS1908 = _M0Lm2vrS651;
      _M0L7vrDiv10S698 = _M0L6_2atmpS1908 / 10ull;
      _M0L6_2atmpS1907 = _M0Lm2vrS651;
      _M0L6_2atmpS1904 = (int32_t)_M0L6_2atmpS1907;
      _M0L6_2atmpS1906 = (int32_t)_M0L7vrDiv10S698;
      _M0L6_2atmpS1905 = 10 * _M0L6_2atmpS1906;
      _M0L7vrMod10S699 = _M0L6_2atmpS1904 - _M0L6_2atmpS1905;
      _M0Lm7roundUpS690 = _M0L7vrMod10S699 >= 5;
      _M0Lm2vrS651 = _M0L7vrDiv10S698;
      _M0Lm2vpS652 = _M0L7vpDiv10S695;
      _M0Lm2vmS653 = _M0L7vmDiv10S696;
      _M0L6_2atmpS1903 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1903 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1911 = _M0Lm2vrS651;
    _M0L6_2atmpS1914 = _M0Lm2vrS651;
    _M0L6_2atmpS1915 = _M0Lm2vmS653;
    _M0L6_2atmpS1913
    = _M0L6_2atmpS1914 == _M0L6_2atmpS1915 || _M0Lm7roundUpS690;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1912 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1913);
    _M0Lm6outputS677 = _M0L6_2atmpS1911 + _M0L6_2atmpS1912;
  }
  _M0L6_2atmpS1919 = _M0Lm3e10S654;
  _M0L6_2atmpS1920 = _M0Lm7removedS675;
  _M0L3expS700 = _M0L6_2atmpS1919 + _M0L6_2atmpS1920;
  _M0L6_2atmpS1918 = _M0Lm6outputS677;
  _block_2644
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2644)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2644->$0 = _M0L6_2atmpS1918;
  _block_2644->$1 = _M0L3expS700;
  return _block_2644;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS643) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS643) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS642) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS642) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS641) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS641) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS640) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS640 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS640 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS640 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS640 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS640 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS640 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS640 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS640 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS640 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS640 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS640 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS640 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS640 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS640 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS640 >= 100ull) {
    return 3;
  }
  if (_M0L1vS640 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS623) {
  int32_t _M0L6_2atmpS1819;
  int32_t _M0L6_2atmpS1818;
  int32_t _M0L4baseS622;
  int32_t _M0L5base2S624;
  int32_t _M0L6offsetS625;
  int32_t _M0L6_2atmpS1817;
  uint64_t _M0L4mul0S626;
  int32_t _M0L6_2atmpS1816;
  int32_t _M0L6_2atmpS1815;
  uint64_t _M0L4mul1S627;
  uint64_t _M0L1mS628;
  struct _M0TPB7Umul128 _M0L7_2abindS629;
  uint64_t _M0L7_2alow1S630;
  uint64_t _M0L8_2ahigh1S631;
  struct _M0TPB7Umul128 _M0L7_2abindS632;
  uint64_t _M0L7_2alow0S633;
  uint64_t _M0L8_2ahigh0S634;
  uint64_t _M0L3sumS635;
  uint64_t _M0Lm5high1S636;
  int32_t _M0L6_2atmpS1813;
  int32_t _M0L6_2atmpS1814;
  int32_t _M0L5deltaS637;
  uint64_t _M0L6_2atmpS1812;
  uint64_t _M0L6_2atmpS1804;
  int32_t _M0L6_2atmpS1811;
  uint32_t _M0L6_2atmpS1808;
  int32_t _M0L6_2atmpS1810;
  int32_t _M0L6_2atmpS1809;
  uint32_t _M0L6_2atmpS1807;
  uint32_t _M0L6_2atmpS1806;
  uint64_t _M0L6_2atmpS1805;
  uint64_t _M0L1aS638;
  uint64_t _M0L6_2atmpS1803;
  uint64_t _M0L1bS639;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1819 = _M0L1iS623 + 26;
  _M0L6_2atmpS1818 = _M0L6_2atmpS1819 - 1;
  _M0L4baseS622 = _M0L6_2atmpS1818 / 26;
  _M0L5base2S624 = _M0L4baseS622 * 26;
  _M0L6offsetS625 = _M0L5base2S624 - _M0L1iS623;
  _M0L6_2atmpS1817 = _M0L4baseS622 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1817);
  _M0L6_2atmpS1816 = _M0L4baseS622 * 2;
  _M0L6_2atmpS1815 = _M0L6_2atmpS1816 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1815);
  if (_M0L6offsetS625 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S626, .$1 = _M0L4mul1S627};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS628
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS625);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS629 = _M0FPB7umul128(_M0L1mS628, _M0L4mul1S627);
  _M0L7_2alow1S630 = _M0L7_2abindS629.$0;
  _M0L8_2ahigh1S631 = _M0L7_2abindS629.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS632 = _M0FPB7umul128(_M0L1mS628, _M0L4mul0S626);
  _M0L7_2alow0S633 = _M0L7_2abindS632.$0;
  _M0L8_2ahigh0S634 = _M0L7_2abindS632.$1;
  _M0L3sumS635 = _M0L8_2ahigh0S634 + _M0L7_2alow1S630;
  _M0Lm5high1S636 = _M0L8_2ahigh1S631;
  if (_M0L3sumS635 < _M0L8_2ahigh0S634) {
    uint64_t _M0L6_2atmpS1802 = _M0Lm5high1S636;
    _M0Lm5high1S636 = _M0L6_2atmpS1802 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1813 = _M0FPB8pow5bits(_M0L5base2S624);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1814 = _M0FPB8pow5bits(_M0L1iS623);
  _M0L5deltaS637 = _M0L6_2atmpS1813 - _M0L6_2atmpS1814;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1812
  = _M0FPB13shiftright128(_M0L7_2alow0S633, _M0L3sumS635, _M0L5deltaS637);
  _M0L6_2atmpS1804 = _M0L6_2atmpS1812 + 1ull;
  _M0L6_2atmpS1811 = _M0L1iS623 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1808
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1811);
  _M0L6_2atmpS1810 = _M0L1iS623 % 16;
  _M0L6_2atmpS1809 = _M0L6_2atmpS1810 << 1;
  _M0L6_2atmpS1807 = _M0L6_2atmpS1808 >> (_M0L6_2atmpS1809 & 31);
  _M0L6_2atmpS1806 = _M0L6_2atmpS1807 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1805 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1806);
  _M0L1aS638 = _M0L6_2atmpS1804 + _M0L6_2atmpS1805;
  _M0L6_2atmpS1803 = _M0Lm5high1S636;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS639
  = _M0FPB13shiftright128(_M0L3sumS635, _M0L6_2atmpS1803, _M0L5deltaS637);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS638, .$1 = _M0L1bS639};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS605) {
  int32_t _M0L4baseS604;
  int32_t _M0L5base2S606;
  int32_t _M0L6offsetS607;
  int32_t _M0L6_2atmpS1801;
  uint64_t _M0L4mul0S608;
  int32_t _M0L6_2atmpS1800;
  int32_t _M0L6_2atmpS1799;
  uint64_t _M0L4mul1S609;
  uint64_t _M0L1mS610;
  struct _M0TPB7Umul128 _M0L7_2abindS611;
  uint64_t _M0L7_2alow1S612;
  uint64_t _M0L8_2ahigh1S613;
  struct _M0TPB7Umul128 _M0L7_2abindS614;
  uint64_t _M0L7_2alow0S615;
  uint64_t _M0L8_2ahigh0S616;
  uint64_t _M0L3sumS617;
  uint64_t _M0Lm5high1S618;
  int32_t _M0L6_2atmpS1797;
  int32_t _M0L6_2atmpS1798;
  int32_t _M0L5deltaS619;
  uint64_t _M0L6_2atmpS1789;
  int32_t _M0L6_2atmpS1796;
  uint32_t _M0L6_2atmpS1793;
  int32_t _M0L6_2atmpS1795;
  int32_t _M0L6_2atmpS1794;
  uint32_t _M0L6_2atmpS1792;
  uint32_t _M0L6_2atmpS1791;
  uint64_t _M0L6_2atmpS1790;
  uint64_t _M0L1aS620;
  uint64_t _M0L6_2atmpS1788;
  uint64_t _M0L1bS621;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS604 = _M0L1iS605 / 26;
  _M0L5base2S606 = _M0L4baseS604 * 26;
  _M0L6offsetS607 = _M0L1iS605 - _M0L5base2S606;
  _M0L6_2atmpS1801 = _M0L4baseS604 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1801);
  _M0L6_2atmpS1800 = _M0L4baseS604 * 2;
  _M0L6_2atmpS1799 = _M0L6_2atmpS1800 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1799);
  if (_M0L6offsetS607 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S608, .$1 = _M0L4mul1S609};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS610
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS607);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS611 = _M0FPB7umul128(_M0L1mS610, _M0L4mul1S609);
  _M0L7_2alow1S612 = _M0L7_2abindS611.$0;
  _M0L8_2ahigh1S613 = _M0L7_2abindS611.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS614 = _M0FPB7umul128(_M0L1mS610, _M0L4mul0S608);
  _M0L7_2alow0S615 = _M0L7_2abindS614.$0;
  _M0L8_2ahigh0S616 = _M0L7_2abindS614.$1;
  _M0L3sumS617 = _M0L8_2ahigh0S616 + _M0L7_2alow1S612;
  _M0Lm5high1S618 = _M0L8_2ahigh1S613;
  if (_M0L3sumS617 < _M0L8_2ahigh0S616) {
    uint64_t _M0L6_2atmpS1787 = _M0Lm5high1S618;
    _M0Lm5high1S618 = _M0L6_2atmpS1787 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1797 = _M0FPB8pow5bits(_M0L1iS605);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1798 = _M0FPB8pow5bits(_M0L5base2S606);
  _M0L5deltaS619 = _M0L6_2atmpS1797 - _M0L6_2atmpS1798;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1789
  = _M0FPB13shiftright128(_M0L7_2alow0S615, _M0L3sumS617, _M0L5deltaS619);
  _M0L6_2atmpS1796 = _M0L1iS605 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1793
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1796);
  _M0L6_2atmpS1795 = _M0L1iS605 % 16;
  _M0L6_2atmpS1794 = _M0L6_2atmpS1795 << 1;
  _M0L6_2atmpS1792 = _M0L6_2atmpS1793 >> (_M0L6_2atmpS1794 & 31);
  _M0L6_2atmpS1791 = _M0L6_2atmpS1792 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1790 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1791);
  _M0L1aS620 = _M0L6_2atmpS1789 + _M0L6_2atmpS1790;
  _M0L6_2atmpS1788 = _M0Lm5high1S618;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS621
  = _M0FPB13shiftright128(_M0L3sumS617, _M0L6_2atmpS1788, _M0L5deltaS619);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS620, .$1 = _M0L1bS621};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS578,
  struct _M0TPB8Pow5Pair _M0L3mulS575,
  int32_t _M0L1jS591,
  int32_t _M0L7mmShiftS593
) {
  uint64_t _M0L7_2amul0S574;
  uint64_t _M0L7_2amul1S576;
  uint64_t _M0L1mS577;
  struct _M0TPB7Umul128 _M0L7_2abindS579;
  uint64_t _M0L5_2aloS580;
  uint64_t _M0L6_2atmpS581;
  struct _M0TPB7Umul128 _M0L7_2abindS582;
  uint64_t _M0L6_2alo2S583;
  uint64_t _M0L6_2ahi2S584;
  uint64_t _M0L3midS585;
  uint64_t _M0L6_2atmpS1786;
  uint64_t _M0L2hiS586;
  uint64_t _M0L3lo2S587;
  uint64_t _M0L6_2atmpS1784;
  uint64_t _M0L6_2atmpS1785;
  uint64_t _M0L4mid2S588;
  uint64_t _M0L6_2atmpS1783;
  uint64_t _M0L3hi2S589;
  int32_t _M0L6_2atmpS1782;
  int32_t _M0L6_2atmpS1781;
  uint64_t _M0L2vpS590;
  uint64_t _M0Lm2vmS592;
  int32_t _M0L6_2atmpS1780;
  int32_t _M0L6_2atmpS1779;
  uint64_t _M0L2vrS603;
  uint64_t _M0L6_2atmpS1778;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S574 = _M0L3mulS575.$0;
  _M0L7_2amul1S576 = _M0L3mulS575.$1;
  _M0L1mS577 = _M0L1mS578 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS579 = _M0FPB7umul128(_M0L1mS577, _M0L7_2amul0S574);
  _M0L5_2aloS580 = _M0L7_2abindS579.$0;
  _M0L6_2atmpS581 = _M0L7_2abindS579.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS582 = _M0FPB7umul128(_M0L1mS577, _M0L7_2amul1S576);
  _M0L6_2alo2S583 = _M0L7_2abindS582.$0;
  _M0L6_2ahi2S584 = _M0L7_2abindS582.$1;
  _M0L3midS585 = _M0L6_2atmpS581 + _M0L6_2alo2S583;
  if (_M0L3midS585 < _M0L6_2atmpS581) {
    _M0L6_2atmpS1786 = 1ull;
  } else {
    _M0L6_2atmpS1786 = 0ull;
  }
  _M0L2hiS586 = _M0L6_2ahi2S584 + _M0L6_2atmpS1786;
  _M0L3lo2S587 = _M0L5_2aloS580 + _M0L7_2amul0S574;
  _M0L6_2atmpS1784 = _M0L3midS585 + _M0L7_2amul1S576;
  if (_M0L3lo2S587 < _M0L5_2aloS580) {
    _M0L6_2atmpS1785 = 1ull;
  } else {
    _M0L6_2atmpS1785 = 0ull;
  }
  _M0L4mid2S588 = _M0L6_2atmpS1784 + _M0L6_2atmpS1785;
  if (_M0L4mid2S588 < _M0L3midS585) {
    _M0L6_2atmpS1783 = 1ull;
  } else {
    _M0L6_2atmpS1783 = 0ull;
  }
  _M0L3hi2S589 = _M0L2hiS586 + _M0L6_2atmpS1783;
  _M0L6_2atmpS1782 = _M0L1jS591 - 64;
  _M0L6_2atmpS1781 = _M0L6_2atmpS1782 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS590
  = _M0FPB13shiftright128(_M0L4mid2S588, _M0L3hi2S589, _M0L6_2atmpS1781);
  _M0Lm2vmS592 = 0ull;
  if (_M0L7mmShiftS593) {
    uint64_t _M0L3lo3S594 = _M0L5_2aloS580 - _M0L7_2amul0S574;
    uint64_t _M0L6_2atmpS1768 = _M0L3midS585 - _M0L7_2amul1S576;
    uint64_t _M0L6_2atmpS1769;
    uint64_t _M0L4mid3S595;
    uint64_t _M0L6_2atmpS1767;
    uint64_t _M0L3hi3S596;
    int32_t _M0L6_2atmpS1766;
    int32_t _M0L6_2atmpS1765;
    if (_M0L5_2aloS580 < _M0L3lo3S594) {
      _M0L6_2atmpS1769 = 1ull;
    } else {
      _M0L6_2atmpS1769 = 0ull;
    }
    _M0L4mid3S595 = _M0L6_2atmpS1768 - _M0L6_2atmpS1769;
    if (_M0L3midS585 < _M0L4mid3S595) {
      _M0L6_2atmpS1767 = 1ull;
    } else {
      _M0L6_2atmpS1767 = 0ull;
    }
    _M0L3hi3S596 = _M0L2hiS586 - _M0L6_2atmpS1767;
    _M0L6_2atmpS1766 = _M0L1jS591 - 64;
    _M0L6_2atmpS1765 = _M0L6_2atmpS1766 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid3S595, _M0L3hi3S596, _M0L6_2atmpS1765);
  } else {
    uint64_t _M0L3lo3S597 = _M0L5_2aloS580 + _M0L5_2aloS580;
    uint64_t _M0L6_2atmpS1776 = _M0L3midS585 + _M0L3midS585;
    uint64_t _M0L6_2atmpS1777;
    uint64_t _M0L4mid3S598;
    uint64_t _M0L6_2atmpS1774;
    uint64_t _M0L6_2atmpS1775;
    uint64_t _M0L3hi3S599;
    uint64_t _M0L3lo4S600;
    uint64_t _M0L6_2atmpS1772;
    uint64_t _M0L6_2atmpS1773;
    uint64_t _M0L4mid4S601;
    uint64_t _M0L6_2atmpS1771;
    uint64_t _M0L3hi4S602;
    int32_t _M0L6_2atmpS1770;
    if (_M0L3lo3S597 < _M0L5_2aloS580) {
      _M0L6_2atmpS1777 = 1ull;
    } else {
      _M0L6_2atmpS1777 = 0ull;
    }
    _M0L4mid3S598 = _M0L6_2atmpS1776 + _M0L6_2atmpS1777;
    _M0L6_2atmpS1774 = _M0L2hiS586 + _M0L2hiS586;
    if (_M0L4mid3S598 < _M0L3midS585) {
      _M0L6_2atmpS1775 = 1ull;
    } else {
      _M0L6_2atmpS1775 = 0ull;
    }
    _M0L3hi3S599 = _M0L6_2atmpS1774 + _M0L6_2atmpS1775;
    _M0L3lo4S600 = _M0L3lo3S597 - _M0L7_2amul0S574;
    _M0L6_2atmpS1772 = _M0L4mid3S598 - _M0L7_2amul1S576;
    if (_M0L3lo3S597 < _M0L3lo4S600) {
      _M0L6_2atmpS1773 = 1ull;
    } else {
      _M0L6_2atmpS1773 = 0ull;
    }
    _M0L4mid4S601 = _M0L6_2atmpS1772 - _M0L6_2atmpS1773;
    if (_M0L4mid3S598 < _M0L4mid4S601) {
      _M0L6_2atmpS1771 = 1ull;
    } else {
      _M0L6_2atmpS1771 = 0ull;
    }
    _M0L3hi4S602 = _M0L3hi3S599 - _M0L6_2atmpS1771;
    _M0L6_2atmpS1770 = _M0L1jS591 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid4S601, _M0L3hi4S602, _M0L6_2atmpS1770);
  }
  _M0L6_2atmpS1780 = _M0L1jS591 - 64;
  _M0L6_2atmpS1779 = _M0L6_2atmpS1780 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS603
  = _M0FPB13shiftright128(_M0L3midS585, _M0L2hiS586, _M0L6_2atmpS1779);
  _M0L6_2atmpS1778 = _M0Lm2vmS592;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS603,
                                                .$1 = _M0L2vpS590,
                                                .$2 = _M0L6_2atmpS1778};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS572,
  int32_t _M0L1pS573
) {
  uint64_t _M0L6_2atmpS1764;
  uint64_t _M0L6_2atmpS1763;
  uint64_t _M0L6_2atmpS1762;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1764 = 1ull << (_M0L1pS573 & 63);
  _M0L6_2atmpS1763 = _M0L6_2atmpS1764 - 1ull;
  _M0L6_2atmpS1762 = _M0L5valueS572 & _M0L6_2atmpS1763;
  return _M0L6_2atmpS1762 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS570,
  int32_t _M0L1pS571
) {
  int32_t _M0L6_2atmpS1761;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1761 = _M0FPB10pow5Factor(_M0L5valueS570);
  return _M0L6_2atmpS1761 >= _M0L1pS571;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS565) {
  uint64_t _M0L6_2atmpS1752;
  uint64_t _M0L6_2atmpS1753;
  uint64_t _M0L6_2atmpS1754;
  uint64_t _M0L6_2atmpS1755;
  uint64_t _M0L6_2atmpS1760;
  int32_t _M0L5countS566;
  uint64_t _M0L1vS567;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1752 = _M0L5valueS565 % 5ull;
  if (_M0L6_2atmpS1752 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1753 = _M0L5valueS565 % 25ull;
  if (_M0L6_2atmpS1753 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1754 = _M0L5valueS565 % 125ull;
  if (_M0L6_2atmpS1754 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1755 = _M0L5valueS565 % 625ull;
  if (_M0L6_2atmpS1755 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1760 = _M0L5valueS565 / 625ull;
  _M0L5countS566 = 4;
  _M0L1vS567 = _M0L6_2atmpS1760;
  while (1) {
    if (_M0L1vS567 > 0ull) {
      uint64_t _M0L6_2atmpS1756 = _M0L1vS567 % 5ull;
      int32_t _M0L6_2atmpS1757;
      uint64_t _M0L6_2atmpS1758;
      if (_M0L6_2atmpS1756 != 0ull) {
        return _M0L5countS566;
      }
      _M0L6_2atmpS1757 = _M0L5countS566 + 1;
      _M0L6_2atmpS1758 = _M0L1vS567 / 5ull;
      _M0L5countS566 = _M0L6_2atmpS1757;
      _M0L1vS567 = _M0L6_2atmpS1758;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS569;
      moonbit_string_t _M0L6_2atmpS1759;
      int32_t _result_2646;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS569
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS569, (moonbit_string_t)moonbit_string_literal_15.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS569, _M0L5valueS565);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1759
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS569);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS569);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2646 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1759);
      moonbit_decref_cycle_free(_M0L6_2atmpS1759);
      return _result_2646;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS564,
  uint64_t _M0L2hiS562,
  int32_t _M0L4distS563
) {
  int32_t _M0L6_2atmpS1751;
  uint64_t _M0L6_2atmpS1749;
  uint64_t _M0L6_2atmpS1750;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1751 = 64 - _M0L4distS563;
  _M0L6_2atmpS1749 = _M0L2hiS562 << (_M0L6_2atmpS1751 & 63);
  _M0L6_2atmpS1750 = _M0L2loS564 >> (_M0L4distS563 & 63);
  return _M0L6_2atmpS1749 | _M0L6_2atmpS1750;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS552,
  uint64_t _M0L1bS555
) {
  uint64_t _M0L3aLoS551;
  uint64_t _M0L3aHiS553;
  uint64_t _M0L3bLoS554;
  uint64_t _M0L3bHiS556;
  uint64_t _M0L1xS557;
  uint64_t _M0L6_2atmpS1747;
  uint64_t _M0L6_2atmpS1748;
  uint64_t _M0L1yS558;
  uint64_t _M0L6_2atmpS1745;
  uint64_t _M0L6_2atmpS1746;
  uint64_t _M0L1zS559;
  uint64_t _M0L6_2atmpS1743;
  uint64_t _M0L6_2atmpS1744;
  uint64_t _M0L6_2atmpS1741;
  uint64_t _M0L6_2atmpS1742;
  uint64_t _M0L1wS560;
  uint64_t _M0L2loS561;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS551 = _M0L1aS552 & 4294967295ull;
  _M0L3aHiS553 = _M0L1aS552 >> 32;
  _M0L3bLoS554 = _M0L1bS555 & 4294967295ull;
  _M0L3bHiS556 = _M0L1bS555 >> 32;
  _M0L1xS557 = _M0L3aLoS551 * _M0L3bLoS554;
  _M0L6_2atmpS1747 = _M0L3aHiS553 * _M0L3bLoS554;
  _M0L6_2atmpS1748 = _M0L1xS557 >> 32;
  _M0L1yS558 = _M0L6_2atmpS1747 + _M0L6_2atmpS1748;
  _M0L6_2atmpS1745 = _M0L3aLoS551 * _M0L3bHiS556;
  _M0L6_2atmpS1746 = _M0L1yS558 & 4294967295ull;
  _M0L1zS559 = _M0L6_2atmpS1745 + _M0L6_2atmpS1746;
  _M0L6_2atmpS1743 = _M0L3aHiS553 * _M0L3bHiS556;
  _M0L6_2atmpS1744 = _M0L1yS558 >> 32;
  _M0L6_2atmpS1741 = _M0L6_2atmpS1743 + _M0L6_2atmpS1744;
  _M0L6_2atmpS1742 = _M0L1zS559 >> 32;
  _M0L1wS560 = _M0L6_2atmpS1741 + _M0L6_2atmpS1742;
  _M0L2loS561 = _M0L1aS552 * _M0L1bS555;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS561, .$1 = _M0L1wS560};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS549,
  int32_t _M0L4fromS546,
  int32_t _M0L2toS545
) {
  int32_t _M0L3lenS544;
  int32_t _M0L6_2atmpS1740;
  uint16_t* _M0L6bufferS547;
  int32_t _M0L1iS548;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS544 = _M0L2toS545 - _M0L4fromS546;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1740 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS547
  = (uint16_t*)moonbit_make_string(_M0L3lenS544, _M0L6_2atmpS1740);
  _M0L1iS548 = 0;
  while (1) {
    if (_M0L1iS548 < _M0L3lenS544) {
      int32_t _M0L6_2atmpS1738 = _M0L4fromS546 + _M0L1iS548;
      int32_t _M0L6_2atmpS1737;
      int32_t _M0L6_2atmpS1736;
      int32_t _M0L6_2atmpS1739;
      if (
        _M0L6_2atmpS1738 < 0
        || _M0L6_2atmpS1738 >= Moonbit_array_length(_M0L5bytesS549)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1737 = (int32_t)_M0L5bytesS549[_M0L6_2atmpS1738];
      _M0L6_2atmpS1736 = (uint16_t)_M0L6_2atmpS1737;
      if (
        _M0L1iS548 < 0 || _M0L1iS548 >= Moonbit_array_length(_M0L6bufferS547)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS547[_M0L1iS548] = _M0L6_2atmpS1736;
      _M0L6_2atmpS1739 = _M0L1iS548 + 1;
      _M0L1iS548 = _M0L6_2atmpS1739;
      continue;
    }
    break;
  }
  return _M0L6bufferS547;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS543) {
  int32_t _M0L6_2atmpS1735;
  uint32_t _M0L6_2atmpS1734;
  uint32_t _M0L6_2atmpS1733;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1735 = _M0L1eS543 * 78913;
  _M0L6_2atmpS1734 = *(uint32_t*)&_M0L6_2atmpS1735;
  _M0L6_2atmpS1733 = _M0L6_2atmpS1734 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1733;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS1732;
  uint32_t _M0L6_2atmpS1731;
  uint32_t _M0L6_2atmpS1730;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1732 = _M0L1eS542 * 732923;
  _M0L6_2atmpS1731 = *(uint32_t*)&_M0L6_2atmpS1732;
  _M0L6_2atmpS1730 = _M0L6_2atmpS1731 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1730;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS540,
  int32_t _M0L8exponentS541,
  int32_t _M0L8mantissaS538
) {
  moonbit_string_t _M0L1sS539;
  moonbit_string_t _result_2649;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS538) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
  if (_M0L4signS540) {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_17.data;
  } else {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS541) {
    moonbit_string_t _result_2648;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2648
    = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_18.data);
    moonbit_decref_cycle_free(_M0L1sS539);
    return _result_2648;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2649
  = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_19.data);
  moonbit_decref_cycle_free(_M0L1sS539);
  return _result_2649;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS537) {
  int32_t _M0L6_2atmpS1729;
  uint32_t _M0L6_2atmpS1728;
  uint32_t _M0L6_2atmpS1727;
  int32_t _M0L6_2atmpS1726;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1729 = _M0L1eS537 * 1217359;
  _M0L6_2atmpS1728 = *(uint32_t*)&_M0L6_2atmpS1729;
  _M0L6_2atmpS1727 = _M0L6_2atmpS1728 >> 19;
  _M0L6_2atmpS1726 = *(int32_t*)&_M0L6_2atmpS1727;
  return _M0L6_2atmpS1726 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS536) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS536 != _M0L4selfS536) {
    return 0;
  } else if (_M0L4selfS536 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS536 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS536;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS535) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS535 != _M0L4selfS535) {
    return 0ll;
  } else if (_M0L4selfS535 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS535 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS535;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS531
) {
  float* _M0L6_2atmpS1722;
  struct _M0TPB5ArrayGfE* _block_2650;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1722 = (float*)moonbit_make_float_array_raw(_M0L3lenS531);
  _block_2650
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2650)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 43, 0);
  _block_2650->$0 = _M0L6_2atmpS1722;
  _block_2650->$1 = _M0L3lenS531;
  return _block_2650;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS532
) {
  uint8_t* _M0L6_2atmpS1723;
  struct _M0TPB5ArrayGbE* _block_2651;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1723 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS532);
  _block_2651
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2651)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 64, 0);
  _block_2651->$0 = _M0L6_2atmpS1723;
  _block_2651->$1 = _M0L3lenS532;
  return _block_2651;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS533
) {
  int32_t* _M0L6_2atmpS1724;
  struct _M0TPB5ArrayGiE* _block_2652;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1724 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS533);
  _block_2652
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2652)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 52, 0);
  _block_2652->$0 = _M0L6_2atmpS1724;
  _block_2652->$1 = _M0L3lenS533;
  return _block_2652;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS534
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1725;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2653;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1725
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS534, 0);
  _block_2653
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2653)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 67, 0);
  _block_2653->$0 = _M0L6_2atmpS1725;
  _block_2653->$1 = _M0L3lenS534;
  return _block_2653;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint64_t* _M0L6_2atmpS1720;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1720 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS1720)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1720[_M0L5indexS528];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS529,
  int32_t _M0L5indexS530
) {
  uint32_t* _M0L6_2atmpS1721;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1721 = _M0L4selfS529;
  if (
    _M0L5indexS530 < 0
    || _M0L5indexS530 >= Moonbit_array_length(_M0L6_2atmpS1721)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1721[_M0L5indexS530];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS526
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS526, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS525) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS525, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS524) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS524) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_21.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS523) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS523;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS511,
  moonbit_string_t _M0L5valueS513
) {
  int32_t _M0L3lenS1692;
  moonbit_string_t* _M0L6_2atmpS1694;
  int32_t _M0L6_2atmpS1693;
  int32_t _M0L6lengthS512;
  moonbit_string_t* _M0L3bufS1697;
  moonbit_string_t _M0L6_2aoldS2522;
  int32_t _M0L6_2atmpS1698;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1692 = _M0L4selfS511->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1694 = _M0MPC15array5Array6bufferGsE(_M0L4selfS511);
  _M0L6_2atmpS1693 = Moonbit_array_length(_M0L6_2atmpS1694);
  moonbit_decref_cycle_free(_M0L6_2atmpS1694);
  if (_M0L3lenS1692 == _M0L6_2atmpS1693) {
    int32_t _M0L3lenS1696 = _M0L4selfS511->$1;
    int32_t _M0L6_2atmpS1695 = _M0L3lenS1696 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS511, _M0L6_2atmpS1695);
  }
  _M0L6lengthS512 = _M0L4selfS511->$1;
  _M0L3bufS1697 = _M0L4selfS511->$0;
  _M0L6_2aoldS2522 = (moonbit_string_t)_M0L3bufS1697[_M0L6lengthS512];
  moonbit_decref_cycle_free(_M0L6_2aoldS2522);
  _M0L3bufS1697[_M0L6lengthS512] = _M0L5valueS513;
  _M0L6_2atmpS1698 = _M0L6lengthS512 + 1;
  _M0L4selfS511->$1 = _M0L6_2atmpS1698;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS514,
  struct _M0TUsiE* _M0L5valueS516
) {
  int32_t _M0L3lenS1699;
  struct _M0TUsiE** _M0L6_2atmpS1701;
  int32_t _M0L6_2atmpS1700;
  int32_t _M0L6lengthS515;
  struct _M0TUsiE** _M0L3bufS1704;
  struct _M0TUsiE* _M0L6_2aoldS2523;
  int32_t _M0L6_2atmpS1705;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1699 = _M0L4selfS514->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1701 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS514);
  _M0L6_2atmpS1700 = Moonbit_array_length(_M0L6_2atmpS1701);
  moonbit_decref_cycle_free(_M0L6_2atmpS1701);
  if (_M0L3lenS1699 == _M0L6_2atmpS1700) {
    int32_t _M0L3lenS1703 = _M0L4selfS514->$1;
    int32_t _M0L6_2atmpS1702 = _M0L3lenS1703 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS514, _M0L6_2atmpS1702);
  }
  _M0L6lengthS515 = _M0L4selfS514->$1;
  _M0L3bufS1704 = _M0L4selfS514->$0;
  _M0L6_2aoldS2523 = (struct _M0TUsiE*)_M0L3bufS1704[_M0L6lengthS515];
  if (_M0L6_2aoldS2523) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2523);
  }
  _M0L3bufS1704[_M0L6lengthS515] = _M0L5valueS516;
  _M0L6_2atmpS1705 = _M0L6lengthS515 + 1;
  _M0L4selfS514->$1 = _M0L6_2atmpS1705;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS517,
  float _M0L5valueS519
) {
  int32_t _M0L3lenS1706;
  float* _M0L6_2atmpS1708;
  int32_t _M0L6_2atmpS1707;
  int32_t _M0L6lengthS518;
  float* _M0L3bufS1711;
  int32_t _M0L6_2atmpS1712;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1706 = _M0L4selfS517->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1708 = _M0MPC15array5Array6bufferGfE(_M0L4selfS517);
  _M0L6_2atmpS1707 = Moonbit_array_length(_M0L6_2atmpS1708);
  moonbit_decref_cycle_free(_M0L6_2atmpS1708);
  if (_M0L3lenS1706 == _M0L6_2atmpS1707) {
    int32_t _M0L3lenS1710 = _M0L4selfS517->$1;
    int32_t _M0L6_2atmpS1709 = _M0L3lenS1710 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS517, _M0L6_2atmpS1709);
  }
  _M0L6lengthS518 = _M0L4selfS517->$1;
  _M0L3bufS1711 = _M0L4selfS517->$0;
  _M0L3bufS1711[_M0L6lengthS518] = _M0L5valueS519;
  _M0L6_2atmpS1712 = _M0L6lengthS518 + 1;
  _M0L4selfS517->$1 = _M0L6_2atmpS1712;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS520,
  int32_t _M0L5valueS522
) {
  int32_t _M0L3lenS1713;
  int32_t* _M0L6_2atmpS1715;
  int32_t _M0L6_2atmpS1714;
  int32_t _M0L6lengthS521;
  int32_t* _M0L3bufS1718;
  int32_t _M0L6_2atmpS1719;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1713 = _M0L4selfS520->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1715 = _M0MPC15array5Array6bufferGiE(_M0L4selfS520);
  _M0L6_2atmpS1714 = Moonbit_array_length(_M0L6_2atmpS1715);
  moonbit_decref_cycle_free(_M0L6_2atmpS1715);
  if (_M0L3lenS1713 == _M0L6_2atmpS1714) {
    int32_t _M0L3lenS1717 = _M0L4selfS520->$1;
    int32_t _M0L6_2atmpS1716 = _M0L3lenS1717 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS520, _M0L6_2atmpS1716);
  }
  _M0L6lengthS521 = _M0L4selfS520->$1;
  _M0L3bufS1718 = _M0L4selfS520->$0;
  _M0L3bufS1718[_M0L6lengthS521] = _M0L5valueS522;
  _M0L6_2atmpS1719 = _M0L6lengthS521 + 1;
  _M0L4selfS520->$1 = _M0L6_2atmpS1719;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS496,
  int32_t _M0L8requiredS498
) {
  int32_t _M0L8old__capS495;
  int32_t _M0L3lenS1688;
  int32_t _M0L8new__capS497;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS495 = _M0MPC15array5Array8capacityGsE(_M0L4selfS496);
  _M0L3lenS1688 = _M0L4selfS496->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS497
  = _M0FPB23array__growth__capacity(_M0L8old__capS495, _M0L3lenS1688, _M0L8requiredS498);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS496, _M0L8new__capS497);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS500,
  int32_t _M0L8requiredS502
) {
  int32_t _M0L8old__capS499;
  int32_t _M0L3lenS1689;
  int32_t _M0L8new__capS501;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS499 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS500);
  _M0L3lenS1689 = _M0L4selfS500->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS501
  = _M0FPB23array__growth__capacity(_M0L8old__capS499, _M0L3lenS1689, _M0L8requiredS502);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS500, _M0L8new__capS501);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS504,
  int32_t _M0L8requiredS506
) {
  int32_t _M0L8old__capS503;
  int32_t _M0L3lenS1690;
  int32_t _M0L8new__capS505;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS503 = _M0MPC15array5Array8capacityGfE(_M0L4selfS504);
  _M0L3lenS1690 = _M0L4selfS504->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS505
  = _M0FPB23array__growth__capacity(_M0L8old__capS503, _M0L3lenS1690, _M0L8requiredS506);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS504, _M0L8new__capS505);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS508,
  int32_t _M0L8requiredS510
) {
  int32_t _M0L8old__capS507;
  int32_t _M0L3lenS1691;
  int32_t _M0L8new__capS509;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS507 = _M0MPC15array5Array8capacityGiE(_M0L4selfS508);
  _M0L3lenS1691 = _M0L4selfS508->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS509
  = _M0FPB23array__growth__capacity(_M0L8old__capS507, _M0L3lenS1691, _M0L8requiredS510);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS508, _M0L8new__capS509);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS472,
  int32_t _M0L13new__capacityS475
) {
  moonbit_string_t* _M0L8old__bufS471;
  int32_t _M0L3lenS473;
  int32_t _M0L9copy__lenS474;
  moonbit_string_t* _M0L8new__bufS476;
  moonbit_string_t* _M0L6_2aoldS2524;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS471 = _M0L4selfS472->$0;
  _M0L3lenS473 = _M0L4selfS472->$1;
  if (_M0L3lenS473 < _M0L13new__capacityS475) {
    _M0L9copy__lenS474 = _M0L3lenS473;
  } else {
    _M0L9copy__lenS474 = _M0L13new__capacityS475;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS471);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS476
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS471, _M0L13new__capacityS475, _M0L9copy__lenS474, 0, 0);
  _M0L6_2aoldS2524 = _M0L4selfS472->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2524);
  _M0L4selfS472->$0 = _M0L8new__bufS476;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS478,
  int32_t _M0L13new__capacityS481
) {
  struct _M0TUsiE** _M0L8old__bufS477;
  int32_t _M0L3lenS479;
  int32_t _M0L9copy__lenS480;
  struct _M0TUsiE** _M0L8new__bufS482;
  struct _M0TUsiE** _M0L6_2aoldS2525;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS477 = _M0L4selfS478->$0;
  _M0L3lenS479 = _M0L4selfS478->$1;
  if (_M0L3lenS479 < _M0L13new__capacityS481) {
    _M0L9copy__lenS480 = _M0L3lenS479;
  } else {
    _M0L9copy__lenS480 = _M0L13new__capacityS481;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS477);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS482
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS477, _M0L13new__capacityS481, _M0L9copy__lenS480, 0, 0);
  _M0L6_2aoldS2525 = _M0L4selfS478->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2525);
  _M0L4selfS478->$0 = _M0L8new__bufS482;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS484,
  int32_t _M0L13new__capacityS487
) {
  float* _M0L8old__bufS483;
  int32_t _M0L3lenS485;
  int32_t _M0L9copy__lenS486;
  float* _M0L8new__bufS488;
  float* _M0L6_2aoldS2526;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS483 = _M0L4selfS484->$0;
  _M0L3lenS485 = _M0L4selfS484->$1;
  if (_M0L3lenS485 < _M0L13new__capacityS487) {
    _M0L9copy__lenS486 = _M0L3lenS485;
  } else {
    _M0L9copy__lenS486 = _M0L13new__capacityS487;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS483);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS488
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS483, _M0L13new__capacityS487, _M0L9copy__lenS486, 0, 0);
  _M0L6_2aoldS2526 = _M0L4selfS484->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2526);
  _M0L4selfS484->$0 = _M0L8new__bufS488;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS490,
  int32_t _M0L13new__capacityS493
) {
  int32_t* _M0L8old__bufS489;
  int32_t _M0L3lenS491;
  int32_t _M0L9copy__lenS492;
  int32_t* _M0L8new__bufS494;
  int32_t* _M0L6_2aoldS2527;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS489 = _M0L4selfS490->$0;
  _M0L3lenS491 = _M0L4selfS490->$1;
  if (_M0L3lenS491 < _M0L13new__capacityS493) {
    _M0L9copy__lenS492 = _M0L3lenS491;
  } else {
    _M0L9copy__lenS492 = _M0L13new__capacityS493;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS489);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS494
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS489, _M0L13new__capacityS493, _M0L9copy__lenS492, 0, 0);
  _M0L6_2aoldS2527 = _M0L4selfS490->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2527);
  _M0L4selfS490->$0 = _M0L8new__bufS494;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS467
) {
  moonbit_string_t* _M0L6_2atmpS1684;
  int32_t _result_2654;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1684 = _M0MPC15array5Array6bufferGsE(_M0L4selfS467);
  _result_2654 = Moonbit_array_length(_M0L6_2atmpS1684);
  moonbit_decref_cycle_free(_M0L6_2atmpS1684);
  return _result_2654;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS468
) {
  struct _M0TUsiE** _M0L6_2atmpS1685;
  int32_t _result_2655;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1685 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS468);
  _result_2655 = Moonbit_array_length(_M0L6_2atmpS1685);
  moonbit_decref_cycle_free(_M0L6_2atmpS1685);
  return _result_2655;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS469
) {
  float* _M0L6_2atmpS1686;
  int32_t _result_2656;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1686 = _M0MPC15array5Array6bufferGfE(_M0L4selfS469);
  _result_2656 = Moonbit_array_length(_M0L6_2atmpS1686);
  moonbit_decref_cycle_free(_M0L6_2atmpS1686);
  return _result_2656;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS470
) {
  int32_t* _M0L6_2atmpS1687;
  int32_t _result_2657;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1687 = _M0MPC15array5Array6bufferGiE(_M0L4selfS470);
  _result_2657 = Moonbit_array_length(_M0L6_2atmpS1687);
  moonbit_decref_cycle_free(_M0L6_2atmpS1687);
  return _result_2657;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS463,
  int32_t _M0L3lenS461,
  int32_t _M0L8requiredS460
) {
  int32_t _M0L5startS462;
  int32_t _M0L5spaceS464;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS460 < _M0L3lenS461) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L7currentS463 == 0) {
    _M0L5startS462 = 8;
  } else {
    _M0L5startS462 = _M0L7currentS463;
  }
  _M0L5spaceS464 = _M0L5startS462;
  while (1) {
    if (_M0L5spaceS464 < _M0L8requiredS460) {
      int32_t _M0L4nextS465 = _M0L5spaceS464 * 2;
      if (_M0L4nextS465 <= _M0L5spaceS464) {
        return _M0L8requiredS460;
      }
      _M0L5spaceS464 = _M0L4nextS465;
      continue;
    } else {
      return _M0L5spaceS464;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS459) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS459->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS2528;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2528 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2528);
  return _M0L8_2afieldS2528;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS453) {
  uint8_t* _M0L8_2afieldS2529;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2529 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2529);
  return _M0L8_2afieldS2529;
}

struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt11MonitorAdExE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* _M0L4selfS454
) {
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0L8_2afieldS2530;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2530 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2530);
  return _M0L8_2afieldS2530;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS2531;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2531 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2531);
  return _M0L8_2afieldS2531;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS2532;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2532 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2532);
  return _M0L8_2afieldS2532;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS457
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2533;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2533 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2533);
  return _M0L8_2afieldS2533;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS458) {
  int32_t* _M0L8_2afieldS2534;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2534 = _M0L4selfS458->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2534);
  return _M0L8_2afieldS2534;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS451
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS451);
  return _M0L4selfS451;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS450,
  struct _M0TPC16string10StringView _M0L3strS448
) {
  int32_t _M0L3endS1682;
  int32_t _M0L5startS1683;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1681;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1674;
  int32_t _M0L6_2atmpS1673;
  int32_t _if__result_2659;
  uint16_t* _M0L4dataS1675;
  int32_t _M0L3lenS1676;
  moonbit_string_t _M0L6_2atmpS1677;
  int32_t _M0L6_2atmpS1678;
  int32_t _M0L3lenS1680;
  int32_t _M0L6_2atmpS1679;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1682 = _M0L3strS448.$2;
  _M0L5startS1683 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1682 - _M0L5startS1683;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1681 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1681 + _M0L8str__lenS447;
  _M0L4dataS1674 = _M0L4selfS450->$0;
  _M0L6_2atmpS1673 = Moonbit_array_length(_M0L4dataS1674);
  if (_M0L8requiredS449 > _M0L6_2atmpS1673) {
    _if__result_2659 = 1;
  } else {
    int32_t _M0L3lenS1672 = _M0L4selfS450->$1;
    _if__result_2659 = _M0L8requiredS449 < _M0L3lenS1672;
  }
  if (_if__result_2659) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1675 = _M0L4selfS450->$0;
  _M0L3lenS1676 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1675);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1677 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1678 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1675, _M0L3lenS1676, _M0L6_2atmpS1677, _M0L6_2atmpS1678, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1675);
  moonbit_decref_cycle_free(_M0L6_2atmpS1677);
  _M0L3lenS1680 = _M0L4selfS450->$1;
  _M0L6_2atmpS1679 = _M0L3lenS1680 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1679;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2660;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1671;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1670;
  moonbit_string_t _result_2661;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1669 = Moonbit_array_length(_M0L3strS444);
    _if__result_2660 = _M0L3endS443 == _M0L6_2atmpS1669;
  } else {
    _if__result_2660 = 0;
  }
  if (_if__result_2660) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1671 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1671, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1670 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2661
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1670, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1670);
  return _result_2661;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2662;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS436 = Moonbit_array_length(_M0L4selfS437);
  if (_M0L6lengthS439 == 4294967296ll) {
    _M0L6lengthS438 = _M0L3lenS436 - _M0L6offsetS441;
  } else {
    int64_t _M0L7_2aSomeS440 = _M0L6lengthS439;
    _M0L6lengthS438 = (int32_t)_M0L7_2aSomeS440;
  }
  if (_M0L6offsetS441 >= 0) {
    if (_M0L6lengthS438 >= 0) {
      int32_t _M0L6_2atmpS1668 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2662 = _M0L6_2atmpS1668 <= _M0L3lenS436;
    } else {
      _if__result_2662 = 0;
    }
  } else {
    _if__result_2662 = 0;
  }
  if (_if__result_2662) {
    moonbit_incref_cycle_free(_M0L4selfS437);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS437, _M0L6offsetS441, _M0L6lengthS438);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS428,
  int32_t _M0L13bytes__offsetS423,
  moonbit_string_t _M0L3strS430,
  int32_t _M0L11str__offsetS426,
  int32_t _M0L6lengthS424
) {
  int32_t _M0L6_2atmpS1667;
  int32_t _M0L6_2atmpS1666;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1665;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1667 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1666 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1667;
  _M0L2e1S422 = _M0L6_2atmpS1666 - 1;
  _M0L6_2atmpS1665 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1665 - 1;
  _M0L4len1S427 = Moonbit_array_length(_M0L4selfS428);
  _M0L4len2S429 = Moonbit_array_length(_M0L3strS430);
  if (
    _M0L6lengthS424 >= 0
    && _M0L13bytes__offsetS423 >= 0
    && _M0L2e1S422 < _M0L4len1S427
    && _M0L11str__offsetS426 >= 0
    && _M0L2e2S425 < _M0L4len2S429
  ) {
    int32_t _M0L16end__str__offsetS431 =
      _M0L11str__offsetS426 + _M0L6lengthS424;
    int32_t _M0L1iS432 = _M0L11str__offsetS426;
    int32_t _M0L1jS433 = _M0L13bytes__offsetS423;
    while (1) {
      if (_M0L1iS432 < _M0L16end__str__offsetS431) {
        int32_t _M0L6_2atmpS1662 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1661 = (int32_t)_M0L6_2atmpS1662;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1661;
        uint32_t _M0L6_2atmpS1657 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1656;
        int32_t _M0L6_2atmpS1658;
        uint32_t _M0L6_2atmpS1660;
        int32_t _M0L6_2atmpS1659;
        int32_t _M0L6_2atmpS1663;
        int32_t _M0L6_2atmpS1664;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1656 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1657);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1656;
        _M0L6_2atmpS1658 = _M0L1jS433 + 1;
        _M0L6_2atmpS1660 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1659 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1660);
        if (
          _M0L6_2atmpS1658 < 0
          || _M0L6_2atmpS1658 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1658] = _M0L6_2atmpS1659;
        _M0L6_2atmpS1663 = _M0L1iS432 + 1;
        _M0L6_2atmpS1664 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1663;
        _M0L1jS433 = _M0L6_2atmpS1664;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS421) {
  int32_t _M0L6_2atmpS1655;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1655 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1655 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  switch (_M0L5radixS412) {
    case 10: {
      int32_t _M0L3lenS415;
      uint16_t* _M0L6bufferS416;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS415 = _M0FPB12dec__count64(_M0L4selfS413);
      _M0L6bufferS416 = (uint16_t*)moonbit_make_string(_M0L3lenS415, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS416, _M0L4selfS413, 0, _M0L3lenS415);
      _M0L6bufferS414 = _M0L6bufferS416;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS417;
      uint16_t* _M0L6bufferS418;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS417 = _M0FPB12hex__count64(_M0L4selfS413);
      _M0L6bufferS418 = (uint16_t*)moonbit_make_string(_M0L3lenS417, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS418, _M0L4selfS413, 0, _M0L3lenS417);
      _M0L6bufferS414 = _M0L6bufferS418;
      break;
    }
    default: {
      int32_t _M0L3lenS419;
      uint16_t* _M0L6bufferS420;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS419 = _M0FPB14radix__count64(_M0L4selfS413, _M0L5radixS412);
      _M0L6bufferS420 = (uint16_t*)moonbit_make_string(_M0L3lenS419, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS420, _M0L4selfS413, 0, _M0L3lenS419, _M0L5radixS412);
      _M0L6bufferS414 = _M0L6bufferS420;
      break;
    }
  }
  return _M0L6bufferS414;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS396,
  int32_t _M0L5radixS395
) {
  int32_t _M0L12is__negativeS397;
  uint64_t _M0L3numS398;
  uint16_t* _M0L6bufferS399;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS395 < 2 || _M0L5radixS395 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1654 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1654;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1651;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1651 = 1;
      } else {
        _M0L6_2atmpS1651 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1651;
      _M0L6bufferS402
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS401, 0);
      if (_M0L12is__negativeS397) {
        _M0L12digit__startS403 = 1;
      } else {
        _M0L12digit__startS403 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS402, _M0L3numS398, _M0L12digit__startS403, _M0L10total__lenS401);
      _M0L6bufferS399 = _M0L6bufferS402;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS404;
      int32_t _M0L6_2atmpS1652;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1652 = 1;
      } else {
        _M0L6_2atmpS1652 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1652;
      _M0L6bufferS406
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS405, 0);
      if (_M0L12is__negativeS397) {
        _M0L12digit__startS407 = 1;
      } else {
        _M0L12digit__startS407 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS406, _M0L3numS398, _M0L12digit__startS407, _M0L10total__lenS405);
      _M0L6bufferS399 = _M0L6bufferS406;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS408;
      int32_t _M0L6_2atmpS1653;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1653 = 1;
      } else {
        _M0L6_2atmpS1653 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1653;
      _M0L6bufferS410
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS409, 0);
      if (_M0L12is__negativeS397) {
        _M0L12digit__startS411 = 1;
      } else {
        _M0L12digit__startS411 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS410, _M0L3numS398, _M0L12digit__startS411, _M0L10total__lenS409, _M0L5radixS395);
      _M0L6bufferS399 = _M0L6bufferS410;
      break;
    }
  }
  if (_M0L12is__negativeS397) {
    _M0L6bufferS399[0] = 45;
  }
  return _M0L6bufferS399;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS381,
  uint64_t _M0L3numS393,
  int32_t _M0L12digit__startS382,
  int32_t _M0L10total__lenS394
) {
  int32_t _M0L6_2atmpS1650;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1650 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1650;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1627 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1627;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1626 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1625 = 48 + _M0L6_2atmpS1626;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1625;
      int32_t _M0L6_2atmpS1624 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1623 = 48 + _M0L6_2atmpS1624;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1623;
      int32_t _M0L6_2atmpS1622 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1621 = 48 + _M0L6_2atmpS1622;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1621;
      int32_t _M0L6_2atmpS1620 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1619 = 48 + _M0L6_2atmpS1620;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1619;
      int32_t _M0L6_2atmpS1611 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1610 = _M0L6_2atmpS1611 - 4;
      int32_t _M0L6_2atmpS1613;
      int32_t _M0L6_2atmpS1612;
      int32_t _M0L6_2atmpS1615;
      int32_t _M0L6_2atmpS1614;
      int32_t _M0L6_2atmpS1617;
      int32_t _M0L6_2atmpS1616;
      int32_t _M0L6_2atmpS1618;
      _M0L6bufferS381[_M0L6_2atmpS1610] = _M0L6d1__hiS377;
      _M0L6_2atmpS1613 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1612 = _M0L6_2atmpS1613 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1612] = _M0L6d1__loS378;
      _M0L6_2atmpS1615 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1614 = _M0L6_2atmpS1615 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1614] = _M0L6d2__hiS379;
      _M0L6_2atmpS1617 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1616 = _M0L6_2atmpS1617 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1616] = _M0L6d2__loS380;
      _M0L6_2atmpS1618 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1618;
      continue;
    } else {
      int32_t _M0L6_2atmpS1649 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1649;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1636 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1635 = 48 + _M0L6_2atmpS1636;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1635;
          int32_t _M0L6_2atmpS1634 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1633 = 48 + _M0L6_2atmpS1634;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1633;
          int32_t _M0L6_2atmpS1629 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1628 = _M0L6_2atmpS1629 - 2;
          int32_t _M0L6_2atmpS1631;
          int32_t _M0L6_2atmpS1630;
          int32_t _M0L6_2atmpS1632;
          _M0L6bufferS381[_M0L6_2atmpS1628] = _M0L5d__hiS388;
          _M0L6_2atmpS1631 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1630 = _M0L6_2atmpS1631 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1630] = _M0L5d__loS389;
          _M0L6_2atmpS1632 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1632;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1644 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1643 = 48 + _M0L6_2atmpS1644;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1643;
          int32_t _M0L6_2atmpS1642 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1641 = 48 + _M0L6_2atmpS1642;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1641;
          int32_t _M0L6_2atmpS1638 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1637 = _M0L6_2atmpS1638 - 2;
          int32_t _M0L6_2atmpS1640;
          int32_t _M0L6_2atmpS1639;
          _M0L6bufferS381[_M0L6_2atmpS1637] = _M0L5d__hiS391;
          _M0L6_2atmpS1640 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1639 = _M0L6_2atmpS1640 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1639] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1648 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1645 = _M0L6_2atmpS1648 - 1;
          int32_t _M0L6_2atmpS1647 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1646 = (uint16_t)_M0L6_2atmpS1647;
          _M0L6bufferS381[_M0L6_2atmpS1645] = _M0L6_2atmpS1646;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS361,
  uint64_t _M0L3numS365,
  int32_t _M0L12digit__startS362,
  int32_t _M0L10total__lenS364,
  int32_t _M0L5radixS355
) {
  uint64_t _M0L4baseS354;
  int32_t _M0L6_2atmpS1595;
  int32_t _M0L6_2atmpS1594;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1595 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1594 = _M0L5radixS355 & _M0L6_2atmpS1595;
  if (_M0L6_2atmpS1594 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1602;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1602 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1602;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1601 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1601;
        int32_t _M0L6_2atmpS1598 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1596 = _M0L6_2atmpS1598 - 1;
        int32_t _M0L6_2atmpS1597 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1599;
        uint64_t _M0L6_2atmpS1600;
        _M0L6bufferS361[_M0L6_2atmpS1596] = _M0L6_2atmpS1597;
        _M0L6_2atmpS1599 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1600 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1599;
        _M0L1nS359 = _M0L6_2atmpS1600;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1609 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1609;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1608 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1607 = _M0L1nS367 - _M0L6_2atmpS1608;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1607;
        int32_t _M0L6_2atmpS1605 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1603 = _M0L6_2atmpS1605 - 1;
        int32_t _M0L6_2atmpS1604 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1606;
        _M0L6bufferS361[_M0L6_2atmpS1603] = _M0L6_2atmpS1604;
        _M0L6_2atmpS1606 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1606;
        _M0L1nS367 = _M0L1qS368;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS348,
  uint64_t _M0L3numS353,
  int32_t _M0L12digit__startS349,
  int32_t _M0L10total__lenS352
) {
  int32_t _M0L6_2atmpS1593;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1593 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1593;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1590 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1590;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1584 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1582 = _M0L6_2atmpS1584 - 2;
      int32_t _M0L6_2atmpS1583 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1587;
      int32_t _M0L6_2atmpS1585;
      int32_t _M0L6_2atmpS1586;
      int32_t _M0L6_2atmpS1588;
      uint64_t _M0L6_2atmpS1589;
      _M0L6bufferS348[_M0L6_2atmpS1582] = _M0L6_2atmpS1583;
      _M0L6_2atmpS1587 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1585 = _M0L6_2atmpS1587 - 1;
      _M0L6_2atmpS1586
      = ((moonbit_string_t)moonbit_string_literal_24.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1585] = _M0L6_2atmpS1586;
      _M0L6_2atmpS1588 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1589 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1588;
      _M0L1nS344 = _M0L6_2atmpS1589;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1592 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1592;
      int32_t _M0L6_2atmpS1591 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1591;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS337,
  int32_t _M0L5radixS339
) {
  uint64_t _M0L4baseS338;
  uint64_t _M0L3numS340;
  int32_t _M0L5countS341;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS337 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS338 = _M0MPC13int3Int10to__uint64(_M0L5radixS339);
  _M0L3numS340 = _M0L5valueS337;
  _M0L5countS341 = 0;
  while (1) {
    if (_M0L3numS340 > 0ull) {
      uint64_t _M0L6_2atmpS1580 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1581 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1580;
      _M0L5countS341 = _M0L6_2atmpS1581;
      continue;
    } else {
      return _M0L5countS341;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS335) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS335 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS336;
    int32_t _M0L6_2atmpS1579;
    int32_t _M0L6_2atmpS1578;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1579 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1578 = _M0L6_2atmpS1579 / 4;
    return _M0L6_2atmpS1578 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS334) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS334 >= 10000000000ull) {
    if (_M0L5valueS334 >= 100000000000000ull) {
      if (_M0L5valueS334 >= 10000000000000000ull) {
        if (_M0L5valueS334 >= 1000000000000000000ull) {
          if (_M0L5valueS334 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS334 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS334 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS334 >= 1000000000000ull) {
      if (_M0L5valueS334 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS334 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS334 >= 100000ull) {
    if (_M0L5valueS334 >= 10000000ull) {
      if (_M0L5valueS334 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS334 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS334 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS334 >= 1000ull) {
    if (_M0L5valueS334 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS334 >= 100ull) {
    return 3;
  } else if (_M0L5valueS334 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS318,
  int32_t _M0L5radixS317
) {
  int32_t _M0L12is__negativeS319;
  uint32_t _M0L3numS320;
  uint16_t* _M0L6bufferS321;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS317 < 2 || _M0L5radixS317 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1577 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1577;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1574;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1574 = 1;
      } else {
        _M0L6_2atmpS1574 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1574;
      _M0L6bufferS324
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS323, 0);
      if (_M0L12is__negativeS319) {
        _M0L12digit__startS325 = 1;
      } else {
        _M0L12digit__startS325 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS324, _M0L3numS320, _M0L12digit__startS325, _M0L10total__lenS323);
      _M0L6bufferS321 = _M0L6bufferS324;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS326;
      int32_t _M0L6_2atmpS1575;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1575 = 1;
      } else {
        _M0L6_2atmpS1575 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1575;
      _M0L6bufferS328
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS327, 0);
      if (_M0L12is__negativeS319) {
        _M0L12digit__startS329 = 1;
      } else {
        _M0L12digit__startS329 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS328, _M0L3numS320, _M0L12digit__startS329, _M0L10total__lenS327);
      _M0L6bufferS321 = _M0L6bufferS328;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS330;
      int32_t _M0L6_2atmpS1576;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1576 = 1;
      } else {
        _M0L6_2atmpS1576 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1576;
      _M0L6bufferS332
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS331, 0);
      if (_M0L12is__negativeS319) {
        _M0L12digit__startS333 = 1;
      } else {
        _M0L12digit__startS333 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS332, _M0L3numS320, _M0L12digit__startS333, _M0L10total__lenS331, _M0L5radixS317);
      _M0L6bufferS321 = _M0L6bufferS332;
      break;
    }
  }
  if (_M0L12is__negativeS319) {
    _M0L6bufferS321[0] = 45;
  }
  return _M0L6bufferS321;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS311,
  int32_t _M0L5radixS313
) {
  uint32_t _M0L4baseS312;
  uint32_t _M0L3numS314;
  int32_t _M0L5countS315;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS311 == 0u) {
    return 1;
  }
  _M0L4baseS312 = *(uint32_t*)&_M0L5radixS313;
  _M0L3numS314 = _M0L5valueS311;
  _M0L5countS315 = 0;
  while (1) {
    if (_M0L3numS314 > 0u) {
      uint32_t _M0L6_2atmpS1572 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1573 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1572;
      _M0L5countS315 = _M0L6_2atmpS1573;
      continue;
    } else {
      return _M0L5countS315;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS309) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS309 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS310;
    int32_t _M0L6_2atmpS1571;
    int32_t _M0L6_2atmpS1570;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1571 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1570 = _M0L6_2atmpS1571 / 4;
    return _M0L6_2atmpS1570 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS308) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS308 >= 100000u) {
    if (_M0L5valueS308 >= 10000000u) {
      if (_M0L5valueS308 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS308 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS308 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS308 >= 1000u) {
    if (_M0L5valueS308 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS308 >= 100u) {
    return 3;
  } else if (_M0L5valueS308 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS294,
  uint32_t _M0L3numS306,
  int32_t _M0L12digit__startS295,
  int32_t _M0L10total__lenS307
) {
  int32_t _M0L6_2atmpS1569;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1569 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1569;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1546 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1546;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1545 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1544 = 48 + _M0L6_2atmpS1545;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1544;
      int32_t _M0L6_2atmpS1543 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1542 = 48 + _M0L6_2atmpS1543;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1542;
      int32_t _M0L6_2atmpS1541 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1540 = 48 + _M0L6_2atmpS1541;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1540;
      int32_t _M0L6_2atmpS1539 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1538 = 48 + _M0L6_2atmpS1539;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1538;
      int32_t _M0L6_2atmpS1530 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1529 = _M0L6_2atmpS1530 - 4;
      int32_t _M0L6_2atmpS1532;
      int32_t _M0L6_2atmpS1531;
      int32_t _M0L6_2atmpS1534;
      int32_t _M0L6_2atmpS1533;
      int32_t _M0L6_2atmpS1536;
      int32_t _M0L6_2atmpS1535;
      int32_t _M0L6_2atmpS1537;
      _M0L6bufferS294[_M0L6_2atmpS1529] = _M0L6d1__hiS290;
      _M0L6_2atmpS1532 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1531 = _M0L6_2atmpS1532 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1531] = _M0L6d1__loS291;
      _M0L6_2atmpS1534 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1533 = _M0L6_2atmpS1534 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1533] = _M0L6d2__hiS292;
      _M0L6_2atmpS1536 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1535 = _M0L6_2atmpS1536 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1535] = _M0L6d2__loS293;
      _M0L6_2atmpS1537 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1537;
      continue;
    } else {
      int32_t _M0L6_2atmpS1568 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1568;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1555 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1554 = 48 + _M0L6_2atmpS1555;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1554;
          int32_t _M0L6_2atmpS1553 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1552 = 48 + _M0L6_2atmpS1553;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1552;
          int32_t _M0L6_2atmpS1548 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1547 = _M0L6_2atmpS1548 - 2;
          int32_t _M0L6_2atmpS1550;
          int32_t _M0L6_2atmpS1549;
          int32_t _M0L6_2atmpS1551;
          _M0L6bufferS294[_M0L6_2atmpS1547] = _M0L5d__hiS301;
          _M0L6_2atmpS1550 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1549 = _M0L6_2atmpS1550 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1549] = _M0L5d__loS302;
          _M0L6_2atmpS1551 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1551;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1563 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1562 = 48 + _M0L6_2atmpS1563;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1562;
          int32_t _M0L6_2atmpS1561 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1560 = 48 + _M0L6_2atmpS1561;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1560;
          int32_t _M0L6_2atmpS1557 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1556 = _M0L6_2atmpS1557 - 2;
          int32_t _M0L6_2atmpS1559;
          int32_t _M0L6_2atmpS1558;
          _M0L6bufferS294[_M0L6_2atmpS1556] = _M0L5d__hiS304;
          _M0L6_2atmpS1559 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1558 = _M0L6_2atmpS1559 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1558] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1567 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1564 = _M0L6_2atmpS1567 - 1;
          int32_t _M0L6_2atmpS1566 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1565 = (uint16_t)_M0L6_2atmpS1566;
          _M0L6bufferS294[_M0L6_2atmpS1564] = _M0L6_2atmpS1565;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS274,
  uint32_t _M0L3numS278,
  int32_t _M0L12digit__startS275,
  int32_t _M0L10total__lenS277,
  int32_t _M0L5radixS268
) {
  uint32_t _M0L4baseS267;
  int32_t _M0L6_2atmpS1514;
  int32_t _M0L6_2atmpS1513;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1514 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1513 = _M0L5radixS268 & _M0L6_2atmpS1514;
  if (_M0L6_2atmpS1513 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1521;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1521 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1521;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1520 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1520;
        int32_t _M0L6_2atmpS1517 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1515 = _M0L6_2atmpS1517 - 1;
        int32_t _M0L6_2atmpS1516 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1518;
        uint32_t _M0L6_2atmpS1519;
        _M0L6bufferS274[_M0L6_2atmpS1515] = _M0L6_2atmpS1516;
        _M0L6_2atmpS1518 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1519 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1518;
        _M0L1nS272 = _M0L6_2atmpS1519;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1528 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1528;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1527 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1526 = _M0L1nS280 - _M0L6_2atmpS1527;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1526;
        int32_t _M0L6_2atmpS1524 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1522 = _M0L6_2atmpS1524 - 1;
        int32_t _M0L6_2atmpS1523 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1525;
        _M0L6bufferS274[_M0L6_2atmpS1522] = _M0L6_2atmpS1523;
        _M0L6_2atmpS1525 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1525;
        _M0L1nS280 = _M0L1qS281;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS261,
  uint32_t _M0L3numS266,
  int32_t _M0L12digit__startS262,
  int32_t _M0L10total__lenS265
) {
  int32_t _M0L6_2atmpS1512;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1512 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1512;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1509 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1509;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1503 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1501 = _M0L6_2atmpS1503 - 2;
      int32_t _M0L6_2atmpS1502 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1506;
      int32_t _M0L6_2atmpS1504;
      int32_t _M0L6_2atmpS1505;
      int32_t _M0L6_2atmpS1507;
      uint32_t _M0L6_2atmpS1508;
      _M0L6bufferS261[_M0L6_2atmpS1501] = _M0L6_2atmpS1502;
      _M0L6_2atmpS1506 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1504 = _M0L6_2atmpS1506 - 1;
      _M0L6_2atmpS1505
      = ((moonbit_string_t)moonbit_string_literal_24.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1504] = _M0L6_2atmpS1505;
      _M0L6_2atmpS1507 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1508 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1507;
      _M0L1nS257 = _M0L6_2atmpS1508;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1511 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1511;
      int32_t _M0L6_2atmpS1510 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1510;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1500;
  moonbit_string_t _result_2676;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1500
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1500);
  if (_M0L6_2atmpS1500.$1) {
    moonbit_decref(_M0L6_2atmpS1500.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2676 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2676;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1497;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1497 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1497);
  moonbit_decref_cycle_free(_M0L6_2atmpS1497);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1498;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1498 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1498);
  moonbit_decref_cycle_free(_M0L6_2atmpS1498);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1499;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1499 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1499);
  moonbit_decref_cycle_free(_M0L6_2atmpS1499);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS247
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS247.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS246
) {
  moonbit_string_t _M0L8_2afieldS2535;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2535 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2535);
  return _M0L8_2afieldS2535;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1496;
  int64_t _M0L6_2atmpS1495;
  struct _M0TPC16string10StringView _M0L6_2atmpS1494;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1496 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1495 = (int64_t)_M0L6_2atmpS1496;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1494
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1495);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1494);
  moonbit_decref_cycle_free(_M0L6_2atmpS1494.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS235,
  int32_t _M0L5startS237,
  int64_t _M0L3endS239
) {
  int32_t _M0L3lenS234;
  int32_t _M0Lm2loS236;
  int32_t _M0Lm2hiS238;
  int32_t _M0L6_2atmpS1478;
  int32_t _if__result_2677;
  int32_t _M0L6_2atmpS1486;
  int32_t _if__result_2678;
  int32_t _M0L6_2atmpS1488;
  int32_t _M0L6_2atmpS1489;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS234 = Moonbit_array_length(_M0L4selfS235);
  if (_M0L5startS237 < 0) {
    _M0Lm2loS236 = 0;
  } else if (_M0L5startS237 > _M0L3lenS234) {
    _M0Lm2loS236 = _M0L3lenS234;
  } else {
    _M0Lm2loS236 = _M0L5startS237;
  }
  if (_M0L3endS239 == 4294967296ll) {
    _M0Lm2hiS238 = _M0L3lenS234;
  } else {
    int64_t _M0L7_2aSomeS240 = _M0L3endS239;
    int32_t _M0L4_2aeS241 = (int32_t)_M0L7_2aSomeS240;
    if (_M0L4_2aeS241 < 0) {
      _M0Lm2hiS238 = 0;
    } else if (_M0L4_2aeS241 > _M0L3lenS234) {
      _M0Lm2hiS238 = _M0L3lenS234;
    } else {
      _M0Lm2hiS238 = _M0L4_2aeS241;
    }
  }
  _M0L6_2atmpS1478 = _M0Lm2loS236;
  if (_M0L6_2atmpS1478 > 0) {
    int32_t _M0L6_2atmpS1477 = _M0Lm2loS236;
    if (_M0L6_2atmpS1477 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1476 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1475 = _M0L4selfS235[_M0L6_2atmpS1476];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1475)) {
        int32_t _M0L6_2atmpS1474 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1473 = _M0L6_2atmpS1474 - 1;
        int32_t _M0L6_2atmpS1472 = _M0L4selfS235[_M0L6_2atmpS1473];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2677
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1472);
      } else {
        _if__result_2677 = 0;
      }
    } else {
      _if__result_2677 = 0;
    }
  } else {
    _if__result_2677 = 0;
  }
  if (_if__result_2677) {
    int32_t _M0L6_2atmpS1479 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1479 + 1;
  }
  _M0L6_2atmpS1486 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1486 > 0) {
    int32_t _M0L6_2atmpS1485 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1485 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1484 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1483 = _M0L4selfS235[_M0L6_2atmpS1484];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1483)) {
        int32_t _M0L6_2atmpS1482 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1481 = _M0L6_2atmpS1482 - 1;
        int32_t _M0L6_2atmpS1480 = _M0L4selfS235[_M0L6_2atmpS1481];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2678
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1480);
      } else {
        _if__result_2678 = 0;
      }
    } else {
      _if__result_2678 = 0;
    }
  } else {
    _if__result_2678 = 0;
  }
  if (_if__result_2678) {
    int32_t _M0L6_2atmpS1487 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1487 - 1;
  }
  _M0L6_2atmpS1488 = _M0Lm2loS236;
  _M0L6_2atmpS1489 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1488 >= _M0L6_2atmpS1489) {
    int32_t _M0L6_2atmpS1490 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1491 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1490,
                                                 .$2 = _M0L6_2atmpS1491};
  } else {
    int32_t _M0L6_2atmpS1492 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1493 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1492,
                                                 .$2 = _M0L6_2atmpS1493};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1471;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1471
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1471);
  if (_M0L6_2atmpS1471.$1) {
    moonbit_decref(_M0L6_2atmpS1471.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1470;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1470
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1470);
  if (_M0L6_2atmpS1470.$1) {
    moonbit_decref(_M0L6_2atmpS1470.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1469;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1469 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1469;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS227,
  int32_t _M0L5quoteS228
) {
  struct _M0TPB13StringBuilder* _M0L3bufS226;
  int32_t _M0L6_2atmpS1468;
  struct _M0TPC16string10StringView _M0L6_2atmpS1466;
  struct _M0TPB6Logger _M0L6_2atmpS1467;
  moonbit_string_t _result_2679;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1468 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1466
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1468
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1467
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1466, _M0L6_2atmpS1467, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1466.$0);
  if (_M0L6_2atmpS1467.$1) {
    moonbit_decref(_M0L6_2atmpS1467.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2679 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2679;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1464;
  int32_t _M0L5startS1465;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1464 = _M0L4selfS218.$2;
  _M0L5startS1465 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1464 - _M0L5startS1465;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 77, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1461;
    int32_t _M0L5startS1463;
    int32_t _M0L6_2atmpS1462;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1445;
    int32_t _M0L6_2atmpS1446;
    int32_t _M0L6_2atmpS1447;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1461 = _M0L4selfS218.$0;
    _M0L5startS1463 = _M0L4selfS218.$1;
    _M0L6_2atmpS1462 = _M0L5startS1463 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1461[_M0L6_2atmpS1462];
    switch (_M0L4codeS223) {
      case 34: {
        _M0L1cS225 = _M0L4codeS223;
        goto join_224;
        break;
      }
      
      case 92: {
        _M0L1cS225 = _M0L4codeS223;
        goto join_224;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1448;
        int32_t _M0L6_2atmpS1449;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1448 = _M0L1iS220 + 1;
        _M0L6_2atmpS1449 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1448;
        _M0L3segS221 = _M0L6_2atmpS1449;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1450;
        int32_t _M0L6_2atmpS1451;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_26.data);
        _M0L6_2atmpS1450 = _M0L1iS220 + 1;
        _M0L6_2atmpS1451 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1450;
        _M0L3segS221 = _M0L6_2atmpS1451;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1452;
        int32_t _M0L6_2atmpS1453;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_27.data);
        _M0L6_2atmpS1452 = _M0L1iS220 + 1;
        _M0L6_2atmpS1453 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1452;
        _M0L3segS221 = _M0L6_2atmpS1453;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1454;
        int32_t _M0L6_2atmpS1455;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_28.data);
        _M0L6_2atmpS1454 = _M0L1iS220 + 1;
        _M0L6_2atmpS1455 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1454;
        _M0L3segS221 = _M0L6_2atmpS1455;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1457;
          moonbit_string_t _M0L6_2atmpS1456;
          int32_t _M0L6_2atmpS1458;
          int32_t _M0L6_2atmpS1459;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_29.data);
          _M0L6_2atmpS1457 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1456 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1457);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1456);
          moonbit_decref_cycle_free(_M0L6_2atmpS1456);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1458 = _M0L1iS220 + 1;
          _M0L6_2atmpS1459 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1458;
          _M0L3segS221 = _M0L6_2atmpS1459;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1460 = _M0L1iS220 + 1;
          int32_t _tmp_2682 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1460;
          _M0L3segS221 = _tmp_2682;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2681;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1445 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1445);
    _M0L6_2atmpS1446 = _M0L1iS220 + 1;
    _M0L6_2atmpS1447 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1446;
    _M0L3segS221 = _M0L6_2atmpS1447;
    continue;
    joinlet_2681:;
    break;
  }
  if (_M0L5quoteS215) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS211,
  int32_t _M0L3segS214,
  int32_t _M0L1iS213
) {
  struct _M0TPB6Logger _M0L6loggerS210;
  struct _M0TPC16string10StringView _M0L4selfS212;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS210 = _M0L6_2aenvS211->$1;
  _M0L4selfS212 = _M0L6_2aenvS211->$0;
  if (_M0L1iS213 > _M0L3segS214) {
    int64_t _M0L6_2atmpS1444 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1443;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1443
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1444);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1443);
    moonbit_decref_cycle_free(_M0L6_2atmpS1443.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1441;
  int32_t _M0L5startS1442;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1419;
  int32_t _if__result_2683;
  int32_t _M0L6_2atmpS1429;
  int32_t _if__result_2684;
  int32_t _M0L6_2atmpS1431;
  int32_t _M0L6_2atmpS1432;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1441 = _M0L4selfS201.$2;
  _M0L5startS1442 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1441 - _M0L5startS1442;
  if (_M0L5startS203 < 0) {
    _M0Lm2loS202 = 0;
  } else if (_M0L5startS203 > _M0L3lenS200) {
    _M0Lm2loS202 = _M0L3lenS200;
  } else {
    _M0Lm2loS202 = _M0L5startS203;
  }
  if (_M0L3endS205 == 4294967296ll) {
    _M0Lm2hiS204 = _M0L3lenS200;
  } else {
    int64_t _M0L7_2aSomeS206 = _M0L3endS205;
    int32_t _M0L4_2aeS207 = (int32_t)_M0L7_2aSomeS206;
    if (_M0L4_2aeS207 < 0) {
      _M0Lm2hiS204 = 0;
    } else if (_M0L4_2aeS207 > _M0L3lenS200) {
      _M0Lm2hiS204 = _M0L3lenS200;
    } else {
      _M0Lm2hiS204 = _M0L4_2aeS207;
    }
  }
  _M0L3strS208 = _M0L4selfS201.$0;
  _M0L4baseS209 = _M0L4selfS201.$1;
  _M0L6_2atmpS1419 = _M0Lm2loS202;
  if (_M0L6_2atmpS1419 > 0) {
    int32_t _M0L6_2atmpS1418 = _M0Lm2loS202;
    if (_M0L6_2atmpS1418 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1417 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1416 = _M0L4baseS209 + _M0L6_2atmpS1417;
      int32_t _M0L6_2atmpS1415 = _M0L3strS208[_M0L6_2atmpS1416];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1415)) {
        int32_t _M0L6_2atmpS1414 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1413 = _M0L4baseS209 + _M0L6_2atmpS1414;
        int32_t _M0L6_2atmpS1412 = _M0L6_2atmpS1413 - 1;
        int32_t _M0L6_2atmpS1411 = _M0L3strS208[_M0L6_2atmpS1412];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2683
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1411);
      } else {
        _if__result_2683 = 0;
      }
    } else {
      _if__result_2683 = 0;
    }
  } else {
    _if__result_2683 = 0;
  }
  if (_if__result_2683) {
    int32_t _M0L6_2atmpS1420 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1420 + 1;
  }
  _M0L6_2atmpS1429 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1429 > 0) {
    int32_t _M0L6_2atmpS1428 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1428 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1427 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1426 = _M0L4baseS209 + _M0L6_2atmpS1427;
      int32_t _M0L6_2atmpS1425 = _M0L3strS208[_M0L6_2atmpS1426];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1425)) {
        int32_t _M0L6_2atmpS1424 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1423 = _M0L4baseS209 + _M0L6_2atmpS1424;
        int32_t _M0L6_2atmpS1422 = _M0L6_2atmpS1423 - 1;
        int32_t _M0L6_2atmpS1421 = _M0L3strS208[_M0L6_2atmpS1422];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2684
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1421);
      } else {
        _if__result_2684 = 0;
      }
    } else {
      _if__result_2684 = 0;
    }
  } else {
    _if__result_2684 = 0;
  }
  if (_if__result_2684) {
    int32_t _M0L6_2atmpS1430 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1430 - 1;
  }
  _M0L6_2atmpS1431 = _M0Lm2loS202;
  _M0L6_2atmpS1432 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1431 >= _M0L6_2atmpS1432) {
    int32_t _M0L6_2atmpS1436 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1433 = _M0L4baseS209 + _M0L6_2atmpS1436;
    int32_t _M0L6_2atmpS1435 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1434 = _M0L4baseS209 + _M0L6_2atmpS1435;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1433,
                                                 .$2 = _M0L6_2atmpS1434};
  } else {
    int32_t _M0L6_2atmpS1440 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1437 = _M0L4baseS209 + _M0L6_2atmpS1440;
    int32_t _M0L6_2atmpS1439 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1438 = _M0L4baseS209 + _M0L6_2atmpS1439;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1437,
                                                 .$2 = _M0L6_2atmpS1438};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1408;
  int32_t _M0L6_2atmpS1407;
  int32_t _M0L6_2atmpS1410;
  int32_t _M0L6_2atmpS1409;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1406;
  moonbit_string_t _result_2685;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1408 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1407
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1408);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1407);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1410 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1409
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1410);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1409);
  _M0L6_2atmpS1406 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2685 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1406);
  moonbit_decref_cycle_free(_M0L6_2atmpS1406);
  return _result_2685;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1403;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1403 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1403);
  } else {
    int32_t _M0L6_2atmpS1405;
    int32_t _M0L6_2atmpS1404;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1405 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1404 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1405, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1404);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1401;
  int32_t _M0L6_2atmpS1402;
  int32_t _M0L6_2atmpS1400;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1401 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1402 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1400 = _M0L6_2atmpS1401 - _M0L6_2atmpS1402;
  return _M0L6_2atmpS1400 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1398;
  int32_t _M0L6_2atmpS1399;
  int32_t _M0L6_2atmpS1397;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1398 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1399 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1397 = _M0L6_2atmpS1398 % _M0L6_2atmpS1399;
  return _M0L6_2atmpS1397 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1395;
  int32_t _M0L6_2atmpS1396;
  int32_t _M0L6_2atmpS1394;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1395 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1396 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1394 = _M0L6_2atmpS1395 / _M0L6_2atmpS1396;
  return _M0L6_2atmpS1394 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1392;
  int32_t _M0L6_2atmpS1393;
  int32_t _M0L6_2atmpS1391;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1392 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1393 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1391 = _M0L6_2atmpS1392 + _M0L6_2atmpS1393;
  return _M0L6_2atmpS1391 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1390;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1390 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1390;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS187) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS187 >= 56320 && _M0L4selfS187 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS186) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS186 >= 55296 && _M0L4selfS186 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS185,
  moonbit_string_t _M0L3strS183
) {
  int32_t _M0L8str__lenS182;
  int32_t _M0L3lenS1389;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1384;
  int32_t _M0L6_2atmpS1383;
  int32_t _if__result_2686;
  uint16_t* _M0L4dataS1385;
  int32_t _M0L3lenS1386;
  int32_t _M0L3lenS1388;
  int32_t _M0L6_2atmpS1387;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1389 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1389 + _M0L8str__lenS182;
  _M0L4dataS1384 = _M0L4selfS185->$0;
  _M0L6_2atmpS1383 = Moonbit_array_length(_M0L4dataS1384);
  if (_M0L8requiredS184 > _M0L6_2atmpS1383) {
    _if__result_2686 = 1;
  } else {
    int32_t _M0L3lenS1382 = _M0L4selfS185->$1;
    _if__result_2686 = _M0L8requiredS184 < _M0L3lenS1382;
  }
  if (_if__result_2686) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1385 = _M0L4selfS185->$0;
  _M0L3lenS1386 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1385);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1385, _M0L3lenS1386, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1385);
  _M0L3lenS1388 = _M0L4selfS185->$1;
  _M0L6_2atmpS1387 = _M0L3lenS1388 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1387;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS178,
  int32_t _M0L11dst__offsetS181,
  moonbit_string_t _M0L3strS179,
  int32_t _M0L11str__offsetS174,
  int32_t _M0L3lenS175
) {
  int32_t _M0L16end__str__offsetS173;
  int32_t _M0L1iS176;
  int32_t _M0L1jS177;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS173 = _M0L11str__offsetS174 + _M0L3lenS175;
  _M0L1iS176 = _M0L11str__offsetS174;
  _M0L1jS177 = _M0L11dst__offsetS181;
  while (1) {
    if (_M0L1iS176 < _M0L16end__str__offsetS173) {
      int32_t _M0L6_2atmpS1379 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1380;
      int32_t _M0L6_2atmpS1381;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1379;
      _M0L6_2atmpS1380 = _M0L1iS176 + 1;
      _M0L6_2atmpS1381 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1380;
      _M0L1jS177 = _M0L6_2atmpS1381;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS171,
  int32_t _M0L2chS170
) {
  uint32_t _M0L4codeS169;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS169 = _M0MPC14char4Char8to__uint(_M0L2chS170);
  if (_M0L4codeS169 <= 65535u) {
    int32_t _M0L3lenS1350 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1352 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1351 = Moonbit_array_length(_M0L4dataS1352);
    uint16_t* _M0L4dataS1355;
    int32_t _M0L3lenS1356;
    int32_t _M0L6_2atmpS1357;
    int32_t _M0L3lenS1359;
    int32_t _M0L6_2atmpS1358;
    if (_M0L3lenS1350 >= _M0L6_2atmpS1351) {
      int32_t _M0L3lenS1354 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1353 = _M0L3lenS1354 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1353);
    }
    _M0L4dataS1355 = _M0L4selfS171->$0;
    _M0L3lenS1356 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1355);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1357 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1356 < 0
      || _M0L3lenS1356 >= Moonbit_array_length(_M0L4dataS1355)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1355[_M0L3lenS1356] = _M0L6_2atmpS1357;
    moonbit_decref_cycle_free(_M0L4dataS1355);
    _M0L3lenS1359 = _M0L4selfS171->$1;
    _M0L6_2atmpS1358 = _M0L3lenS1359 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1358;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1363 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1361 = Moonbit_array_length(_M0L4dataS1363);
    int32_t _M0L3lenS1362 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1360 = _M0L6_2atmpS1361 - _M0L3lenS1362;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1366;
    int32_t _M0L3lenS1367;
    uint32_t _M0L6_2atmpS1370;
    uint32_t _M0L6_2atmpS1369;
    int32_t _M0L6_2atmpS1368;
    uint16_t* _M0L4dataS1371;
    int32_t _M0L3lenS1376;
    int32_t _M0L6_2atmpS1372;
    uint32_t _M0L6_2atmpS1375;
    uint32_t _M0L6_2atmpS1374;
    int32_t _M0L6_2atmpS1373;
    int32_t _M0L3lenS1378;
    int32_t _M0L6_2atmpS1377;
    if (_M0L6_2atmpS1360 < 2) {
      int32_t _M0L3lenS1365 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1364 = _M0L3lenS1365 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1364);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1366 = _M0L4selfS171->$0;
    _M0L3lenS1367 = _M0L4selfS171->$1;
    _M0L6_2atmpS1370 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1369 = 55296u + _M0L6_2atmpS1370;
    moonbit_incref_cycle_free(_M0L4dataS1366);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1368 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1369);
    if (
      _M0L3lenS1367 < 0
      || _M0L3lenS1367 >= Moonbit_array_length(_M0L4dataS1366)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1366[_M0L3lenS1367] = _M0L6_2atmpS1368;
    moonbit_decref_cycle_free(_M0L4dataS1366);
    _M0L4dataS1371 = _M0L4selfS171->$0;
    _M0L3lenS1376 = _M0L4selfS171->$1;
    _M0L6_2atmpS1372 = _M0L3lenS1376 + 1;
    _M0L6_2atmpS1375 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1374 = 56320u + _M0L6_2atmpS1375;
    moonbit_incref_cycle_free(_M0L4dataS1371);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1373 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1374);
    if (
      _M0L6_2atmpS1372 < 0
      || _M0L6_2atmpS1372 >= Moonbit_array_length(_M0L4dataS1371)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1371[_M0L6_2atmpS1372] = _M0L6_2atmpS1373;
    moonbit_decref_cycle_free(_M0L4dataS1371);
    _M0L3lenS1378 = _M0L4selfS171->$1;
    _M0L6_2atmpS1377 = _M0L3lenS1378 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1377;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_30.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1349;
  int32_t _M0L6_2atmpS1347;
  int32_t _M0L3lenS1348;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1344;
  int32_t _M0L6_2atmpS1345;
  int32_t _M0L3lenS1346;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2536;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1349 = _M0L4selfS166->$0;
  _M0L6_2atmpS1347 = Moonbit_array_length(_M0L4dataS1349);
  _M0L3lenS1348 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1347, _M0L3lenS1348, _M0L8requiredS167);
  _M0L4dataS1344 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1344);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1345 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1346 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1344, _M0L13new__capacityS165, _M0L6_2atmpS1345, _M0L3lenS1346, 0, 0);
  _M0L6_2aoldS2536 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2536);
  _M0L4selfS166->$0 = _M0L9new__dataS168;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS164,
  int32_t _M0L3lenS160,
  int32_t _M0L8requiredS159
) {
  int32_t _M0L5spaceS161;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS159 < _M0L3lenS160) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_31.data);
  }
  _M0L5spaceS161 = _M0L7currentS164;
  while (1) {
    if (_M0L5spaceS161 < _M0L8requiredS159) {
      int32_t _M0L4nextS162 = _M0L5spaceS161 * 2;
      if (_M0L4nextS162 <= _M0L5spaceS161) {
        return _M0L8requiredS159;
      }
      _M0L5spaceS161 = _M0L4nextS162;
      continue;
    } else {
      return _M0L5spaceS161;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS158) {
  int32_t _M0L6_2atmpS1343;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1343 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1343;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1342;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1342 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1342;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1333;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1333 = _M0L4selfS155->$1;
  if (_M0L3lenS1333 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1334 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1336 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1335 = Moonbit_array_length(_M0L4dataS1336);
    if (_M0L3lenS1334 == _M0L6_2atmpS1335) {
      uint16_t* _M0L4dataS1337 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1337);
      return _M0L4dataS1337;
    } else {
      uint16_t* _M0L4dataS1338 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1339 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1340;
      int32_t _M0L3lenS1341;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1338);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1340 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1341 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1338, _M0L3lenS1339, _M0L6_2atmpS1340, _M0L3lenS1341, 0, 0);
      return _M0L4dataS156;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS152,
  int32_t _M0L13allocate__lenS148,
  int32_t _M0L4initS153,
  int32_t _M0L3lenS149,
  int32_t _M0L11src__offsetS150,
  int32_t _M0L11dst__offsetS151
) {
  int32_t _if__result_2689;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1329 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1330 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1329 <= _M0L6_2atmpS1330) {
            int32_t _M0L6_2atmpS1328 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2689 = _M0L6_2atmpS1328 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2689 = 0;
          }
        } else {
          _if__result_2689 = 0;
        }
      } else {
        _if__result_2689 = 0;
      }
    } else {
      _if__result_2689 = 0;
    }
  } else {
    _if__result_2689 = 0;
  }
  if (_if__result_2689) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1332;
    moonbit_string_t _M0L6_2atmpS1331;
    uint16_t* _result_2690;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_36.data);
    _M0L6_2atmpS1332 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1332);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1331
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2690 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1331);
    moonbit_decref_cycle_free(_M0L6_2atmpS1331);
    return _result_2690;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS145,
  int32_t _M0L13allocate__lenS142,
  int32_t _M0L4initS143,
  int32_t _M0L11src__offsetS146,
  int32_t _M0L11dst__offsetS144,
  int32_t _M0L9blit__lenS147
) {
  uint16_t* _M0L3dstS141;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS141
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS142, _M0L4initS143);
  moonbit_incref_cycle_free(_M0L3dstS141);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS141, _M0L11dst__offsetS144, _M0L3srcS145, _M0L11src__offsetS146, _M0L9blit__lenS147, sizeof(uint16_t));
  return _M0L3dstS141;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS139
) {
  int32_t _M0L7initialS138;
  uint16_t* _M0L4dataS140;
  struct _M0TPB13StringBuilder* _block_2691;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1327 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1327 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2691
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2691)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 82, 0);
  _block_2691->$0 = _M0L4dataS140;
  _block_2691->$1 = 0;
  return _block_2691;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1326;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1326 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1326;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2692;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1307 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1308;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1308
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS1307 <= _M0L6_2atmpS1308) {
            int32_t _M0L6_2atmpS1306 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2692 = _M0L6_2atmpS1306 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2692 = 0;
          }
        } else {
          _if__result_2692 = 0;
        }
      } else {
        _if__result_2692 = 0;
      }
    } else {
      _if__result_2692 = 0;
    }
  } else {
    _if__result_2692 = 0;
  }
  if (_if__result_2692) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1310;
    moonbit_string_t _M0L6_2atmpS1309;
    moonbit_string_t* _result_2693;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1310 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1310);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1309
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2693
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1309);
    moonbit_decref_cycle_free(_M0L6_2atmpS1309);
    return _result_2693;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2694;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1312 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1313;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1313
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS1312 <= _M0L6_2atmpS1313) {
            int32_t _M0L6_2atmpS1311 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2694 = _M0L6_2atmpS1311 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2694 = 0;
          }
        } else {
          _if__result_2694 = 0;
        }
      } else {
        _if__result_2694 = 0;
      }
    } else {
      _if__result_2694 = 0;
    }
  } else {
    _if__result_2694 = 0;
  }
  if (_if__result_2694) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1315;
    moonbit_string_t _M0L6_2atmpS1314;
    struct _M0TUsiE** _result_2695;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1315 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1315);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1314
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2695
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1314);
    moonbit_decref_cycle_free(_M0L6_2atmpS1314);
    return _result_2695;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2696;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1317 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1318;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1318
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS129);
          if (_M0L6_2atmpS1317 <= _M0L6_2atmpS1318) {
            int32_t _M0L6_2atmpS1316 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2696 = _M0L6_2atmpS1316 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2696 = 0;
          }
        } else {
          _if__result_2696 = 0;
        }
      } else {
        _if__result_2696 = 0;
      }
    } else {
      _if__result_2696 = 0;
    }
  } else {
    _if__result_2696 = 0;
  }
  if (_if__result_2696) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1320;
    moonbit_string_t _M0L6_2atmpS1319;
    float* _result_2697;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1320 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1320);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1319
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2697
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1319);
    moonbit_decref_cycle_free(_M0L6_2atmpS1319);
    return _result_2697;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2698;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1322 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1323;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1323
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
          if (_M0L6_2atmpS1322 <= _M0L6_2atmpS1323) {
            int32_t _M0L6_2atmpS1321 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2698 = _M0L6_2atmpS1321 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2698 = 0;
          }
        } else {
          _if__result_2698 = 0;
        }
      } else {
        _if__result_2698 = 0;
      }
    } else {
      _if__result_2698 = 0;
    }
  } else {
    _if__result_2698 = 0;
  }
  if (_if__result_2698) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1325;
    moonbit_string_t _M0L6_2atmpS1324;
    int32_t* _result_2699;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1325 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1325);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1324
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2699
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1324);
    moonbit_decref_cycle_free(_M0L6_2atmpS1324);
    return _result_2699;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1303;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1303
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1303);
  if (_M0L6_2atmpS1303.$1) {
    moonbit_decref(_M0L6_2atmpS1303.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1304;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1304
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1304);
  if (_M0L6_2atmpS1304.$1) {
    moonbit_decref(_M0L6_2atmpS1304.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1305;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1305
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1305);
  if (_M0L6_2atmpS1305.$1) {
    moonbit_decref(_M0L6_2atmpS1305.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS86,
  int32_t _M0L13allocate__lenS84,
  int32_t _M0L11src__offsetS87,
  int32_t _M0L11dst__offsetS85,
  int32_t _M0L9blit__lenS88
) {
  moonbit_string_t* _M0L3dstS83;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS83
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS84, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS83, _M0L11dst__offsetS85, _M0L3srcS86, _M0L11src__offsetS87, _M0L9blit__lenS88);
  moonbit_decref_cycle_free(_M0L3srcS86);
  return _M0L3dstS83;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS92,
  int32_t _M0L13allocate__lenS90,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS91,
  int32_t _M0L9blit__lenS94
) {
  struct _M0TUsiE** _M0L3dstS89;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS89
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS90, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS89, _M0L11dst__offsetS91, _M0L3srcS92, _M0L11src__offsetS93, _M0L9blit__lenS94);
  moonbit_decref_cycle_free(_M0L3srcS92);
  return _M0L3dstS89;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  float* _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS96);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
  moonbit_decref_cycle_free(_M0L3srcS98);
  return _M0L3dstS95;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS104,
  int32_t _M0L13allocate__lenS102,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS103,
  int32_t _M0L9blit__lenS106
) {
  int32_t* _M0L3dstS101;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS101
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS102);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS101, _M0L11dst__offsetS103, _M0L3srcS104, _M0L11src__offsetS105, _M0L9blit__lenS106);
  moonbit_decref_cycle_free(_M0L3srcS104);
  return _M0L3dstS101;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS63,
  int32_t _M0L11dst__offsetS64,
  moonbit_string_t* _M0L3srcS65,
  int32_t _M0L11src__offsetS66,
  int32_t _M0L3lenS67
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS65);
  moonbit_incref_cycle_free(_M0L3dstS63);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS63, _M0L11dst__offsetS64, _M0L3srcS65, _M0L11src__offsetS66, _M0L3lenS67);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  struct _M0TUsiE** _M0L3srcS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L3lenS72
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS70);
  moonbit_incref_cycle_free(_M0L3dstS68);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS68, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L3lenS72);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  float* _M0L3srcS75,
  int32_t _M0L11src__offsetS76,
  int32_t _M0L3lenS77
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS75);
  moonbit_incref_cycle_free(_M0L3dstS73);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS73, _M0L11dst__offsetS74, _M0L3srcS75, _M0L11src__offsetS76, _M0L3lenS77, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS78,
  int32_t _M0L11dst__offsetS79,
  int32_t* _M0L3srcS80,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L3lenS82
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS80);
  moonbit_incref_cycle_free(_M0L3dstS78);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS78, _M0L11dst__offsetS79, _M0L3srcS80, _M0L11src__offsetS81, _M0L3lenS82, sizeof(int32_t));
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS18,
  int32_t _M0L11dst__offsetS20,
  uint16_t* _M0L3srcS19,
  int32_t _M0L11src__offsetS21,
  int32_t _M0L3lenS23
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS18 == _M0L3srcS19 && _M0L11dst__offsetS20 < _M0L11src__offsetS21
  ) {
    int32_t _M0L1iS22 = 0;
    while (1) {
      if (_M0L1iS22 < _M0L3lenS23) {
        int32_t _M0L6_2atmpS1258 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1260 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1259;
        int32_t _M0L6_2atmpS1261;
        if (
          _M0L6_2atmpS1260 < 0
          || _M0L6_2atmpS1260 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1259 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1260];
        if (
          _M0L6_2atmpS1258 < 0
          || _M0L6_2atmpS1258 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1258] = _M0L6_2atmpS1259;
        _M0L6_2atmpS1261 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1261;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1266 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1266;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1262 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1264 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1263;
        int32_t _M0L6_2atmpS1265;
        if (
          _M0L6_2atmpS1264 < 0
          || _M0L6_2atmpS1264 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1263 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1264];
        if (
          _M0L6_2atmpS1262 < 0
          || _M0L6_2atmpS1262 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1262] = _M0L6_2atmpS1263;
        _M0L6_2atmpS1265 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1265;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS27,
  int32_t _M0L11dst__offsetS29,
  moonbit_string_t* _M0L3srcS28,
  int32_t _M0L11src__offsetS30,
  int32_t _M0L3lenS32
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS27 == _M0L3srcS28 && _M0L11dst__offsetS29 < _M0L11src__offsetS30
  ) {
    int32_t _M0L1iS31 = 0;
    while (1) {
      if (_M0L1iS31 < _M0L3lenS32) {
        int32_t _M0L6_2atmpS1267 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1269 = _M0L11src__offsetS30 + _M0L1iS31;
        moonbit_string_t _M0L6_2atmpS1268;
        moonbit_string_t _M0L6_2aoldS2537;
        int32_t _M0L6_2atmpS1270;
        if (
          _M0L6_2atmpS1269 < 0
          || _M0L6_2atmpS1269 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1268 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1269];
        if (
          _M0L6_2atmpS1267 < 0
          || _M0L6_2atmpS1267 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2537 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1267];
        moonbit_incref_cycle_free(_M0L6_2atmpS1268);
        moonbit_decref_cycle_free(_M0L6_2aoldS2537);
        _M0L3dstS27[_M0L6_2atmpS1267] = _M0L6_2atmpS1268;
        _M0L6_2atmpS1270 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1270;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1275 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1275;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1271 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1273 = _M0L11src__offsetS30 + _M0L1iS34;
        moonbit_string_t _M0L6_2atmpS1272;
        moonbit_string_t _M0L6_2aoldS2538;
        int32_t _M0L6_2atmpS1274;
        if (
          _M0L6_2atmpS1273 < 0
          || _M0L6_2atmpS1273 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1272 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1273];
        if (
          _M0L6_2atmpS1271 < 0
          || _M0L6_2atmpS1271 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2538 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1271];
        moonbit_incref_cycle_free(_M0L6_2atmpS1272);
        moonbit_decref_cycle_free(_M0L6_2aoldS2538);
        _M0L3dstS27[_M0L6_2atmpS1271] = _M0L6_2atmpS1272;
        _M0L6_2atmpS1274 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1274;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  struct _M0TUsiE** _M0L3srcS37,
  int32_t _M0L11src__offsetS39,
  int32_t _M0L3lenS41
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS36 == _M0L3srcS37 && _M0L11dst__offsetS38 < _M0L11src__offsetS39
  ) {
    int32_t _M0L1iS40 = 0;
    while (1) {
      if (_M0L1iS40 < _M0L3lenS41) {
        int32_t _M0L6_2atmpS1276 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1278 = _M0L11src__offsetS39 + _M0L1iS40;
        struct _M0TUsiE* _M0L6_2atmpS1277;
        struct _M0TUsiE* _M0L6_2aoldS2539;
        int32_t _M0L6_2atmpS1279;
        if (
          _M0L6_2atmpS1278 < 0
          || _M0L6_2atmpS1278 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1277 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1278];
        if (
          _M0L6_2atmpS1276 < 0
          || _M0L6_2atmpS1276 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2539 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1276];
        if (_M0L6_2atmpS1277) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1277);
        }
        if (_M0L6_2aoldS2539) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2539);
        }
        _M0L3dstS36[_M0L6_2atmpS1276] = _M0L6_2atmpS1277;
        _M0L6_2atmpS1279 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1279;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1284 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1284;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1280 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1282 = _M0L11src__offsetS39 + _M0L1iS43;
        struct _M0TUsiE* _M0L6_2atmpS1281;
        struct _M0TUsiE* _M0L6_2aoldS2540;
        int32_t _M0L6_2atmpS1283;
        if (
          _M0L6_2atmpS1282 < 0
          || _M0L6_2atmpS1282 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1281 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1282];
        if (
          _M0L6_2atmpS1280 < 0
          || _M0L6_2atmpS1280 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2540 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1280];
        if (_M0L6_2atmpS1281) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1281);
        }
        if (_M0L6_2aoldS2540) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2540);
        }
        _M0L3dstS36[_M0L6_2atmpS1280] = _M0L6_2atmpS1281;
        _M0L6_2atmpS1283 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1283;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  float* _M0L3srcS46,
  int32_t _M0L11src__offsetS48,
  int32_t _M0L3lenS50
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS45 == _M0L3srcS46 && _M0L11dst__offsetS47 < _M0L11src__offsetS48
  ) {
    int32_t _M0L1iS49 = 0;
    while (1) {
      if (_M0L1iS49 < _M0L3lenS50) {
        int32_t _M0L6_2atmpS1285 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1287 = _M0L11src__offsetS48 + _M0L1iS49;
        float _M0L6_2atmpS1286;
        int32_t _M0L6_2atmpS1288;
        if (
          _M0L6_2atmpS1287 < 0
          || _M0L6_2atmpS1287 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1286 = (float)_M0L3srcS46[_M0L6_2atmpS1287];
        if (
          _M0L6_2atmpS1285 < 0
          || _M0L6_2atmpS1285 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1285] = _M0L6_2atmpS1286;
        _M0L6_2atmpS1288 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1288;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1293 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1293;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1289 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1291 = _M0L11src__offsetS48 + _M0L1iS52;
        float _M0L6_2atmpS1290;
        int32_t _M0L6_2atmpS1292;
        if (
          _M0L6_2atmpS1291 < 0
          || _M0L6_2atmpS1291 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1290 = (float)_M0L3srcS46[_M0L6_2atmpS1291];
        if (
          _M0L6_2atmpS1289 < 0
          || _M0L6_2atmpS1289 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1289] = _M0L6_2atmpS1290;
        _M0L6_2atmpS1292 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1292;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS54,
  int32_t _M0L11dst__offsetS56,
  int32_t* _M0L3srcS55,
  int32_t _M0L11src__offsetS57,
  int32_t _M0L3lenS59
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS54 == _M0L3srcS55 && _M0L11dst__offsetS56 < _M0L11src__offsetS57
  ) {
    int32_t _M0L1iS58 = 0;
    while (1) {
      if (_M0L1iS58 < _M0L3lenS59) {
        int32_t _M0L6_2atmpS1294 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1296 = _M0L11src__offsetS57 + _M0L1iS58;
        int32_t _M0L6_2atmpS1295;
        int32_t _M0L6_2atmpS1297;
        if (
          _M0L6_2atmpS1296 < 0
          || _M0L6_2atmpS1296 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1295 = (int32_t)_M0L3srcS55[_M0L6_2atmpS1296];
        if (
          _M0L6_2atmpS1294 < 0
          || _M0L6_2atmpS1294 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1294] = _M0L6_2atmpS1295;
        _M0L6_2atmpS1297 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1297;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1302 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1302;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1298 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1300 = _M0L11src__offsetS57 + _M0L1iS61;
        int32_t _M0L6_2atmpS1299;
        int32_t _M0L6_2atmpS1301;
        if (
          _M0L6_2atmpS1300 < 0
          || _M0L6_2atmpS1300 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1299 = (int32_t)_M0L3srcS55[_M0L6_2atmpS1300];
        if (
          _M0L6_2atmpS1298 < 0
          || _M0L6_2atmpS1298 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1298] = _M0L6_2atmpS1299;
        _M0L6_2atmpS1301 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1301;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS14) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS14);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS15
) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS16) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS16);
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS17) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS17);
}

int32_t _M0IPB7FailurePB4Show6output(
  void* _M0L10_2ax__6387S10,
  struct _M0TPB6Logger _M0L10_2ax__6388S13
) {
  struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS11;
  moonbit_string_t _M0L15_2a_2aarg__6389S12;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2aFailureS11
  = (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L10_2ax__6387S10;
  _M0L15_2a_2aarg__6389S12 = _M0L10_2aFailureS11->$0;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_37.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_38.data);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger _M0L4selfS9,
  moonbit_string_t _M0L3objS8
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS8, _M0L4selfS9);
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS7
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1227) {
  switch (Moonbit_object_tag(_M0L4_2aeS1227)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1227);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_40.data;
      break;
    }
    
    case 3: {
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
  void* _M0L11_2aobj__ptrS1253,
  struct _M0TPB4Show _M0L8_2aparamS1252
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1251 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1253;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1251, _M0L8_2aparamS1252);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1250,
  struct _M0TPB4Show _M0L8_2aparamS1249
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1248 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1250;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1248, _M0L8_2aparamS1249);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1247,
  int32_t _M0L8_2aparamS1246
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1245 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1247;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1245, _M0L8_2aparamS1246);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1244,
  struct _M0TPC16string10StringView _M0L8_2aparamS1243
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1242 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1244;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1242, _M0L8_2aparamS1243);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1241,
  moonbit_string_t _M0L8_2aparamS1238,
  int32_t _M0L8_2aparamS1239,
  int32_t _M0L8_2aparamS1240
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1237 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1241;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1237, _M0L8_2aparamS1238, _M0L8_2aparamS1239, _M0L8_2aparamS1240);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1236,
  moonbit_string_t _M0L8_2aparamS1235
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1234 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1236;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1234, _M0L8_2aparamS1235);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2710 = 9218868437227405311ll;
  int64_t _tmp_2711;
  int64_t _tmp_2712;
  int64_t _tmp_2713;
  int64_t _tmp_2714;
  _M0FPB18double__max__value = *(double*)&_tmp_2710;
  _tmp_2711 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2711;
  _tmp_2712 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2712;
  _tmp_2713 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2713;
  _tmp_2714 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2714;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1257;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1220;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1221;
  int32_t _M0L7_2abindS1222;
  struct _M0TUsiE** _M0L7_2abindS1223;
  int32_t _M0L6_2acntS2545;
  int32_t _M0L2__S1224;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1257
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1220
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1220)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 85, 0);
  _M0L12async__testsS1220->$0 = _M0L6_2atmpS1257;
  _M0L12async__testsS1220->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1221
  = _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1222 = _M0L7_2abindS1221->$1;
  _M0L7_2abindS1223 = _M0L7_2abindS1221->$0;
  _M0L6_2acntS2545
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1221));
  if (_M0L6_2acntS2545 > 1) {
    int32_t _M0L11_2anew__cntS2546 = _M0L6_2acntS2545 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1221), _M0L11_2anew__cntS2546);
    moonbit_incref_cycle_free(_M0L7_2abindS1223);
  } else if (_M0L6_2acntS2545 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1221);
  }
  _M0L2__S1224 = 0;
  while (1) {
    if (_M0L2__S1224 < _M0L7_2abindS1222) {
      struct _M0TUsiE* _M0L3argS1225 =
        (struct _M0TUsiE*)_M0L7_2abindS1223[_M0L2__S1224];
      moonbit_string_t _M0L6_2atmpS1254 = _M0L3argS1225->$0;
      int32_t _M0L6_2atmpS1255 = _M0L3argS1225->$1;
      int32_t _M0L6_2atmpS1256;
      moonbit_incref_cycle_free(_M0L6_2atmpS1254);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples25adex__net__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1220, _M0L6_2atmpS1254, _M0L6_2atmpS1255);
      moonbit_decref_cycle_free(_M0L6_2atmpS1254);
      _M0L6_2atmpS1256 = _M0L2__S1224 + 1;
      _M0L2__S1224 = _M0L6_2atmpS1256;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1223);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples25adex__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25adex__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1220);
  moonbit_decref_cycle_free(_M0L12async__testsS1220);
  moonbit_flush_cycles();
  return 0;
}