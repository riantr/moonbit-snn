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
struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0TP26RiantR8snn__mbt4AdEx;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TP26RiantR8snn__mbt11WCParameter;

struct _M0TWRPC15error5ErrorEs;

struct _M0TP26RiantR8snn__mbt12PoissonLayer;

struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter;

struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry;

struct _M0TWssbEu;

struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__;

struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__;

struct _M0TUsiE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt15HetRecParameter;

struct _M0TP26RiantR8snn__mbt16BalancedStimulus;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__;

struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__;

struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables;

struct _M0TUmmmmE;

struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064;

struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059;

struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__;

struct _M0TP26RiantR8snn__mbt11HHParameter;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__;

struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__;

struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__;

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter;

struct _M0TP26RiantR8snn__mbt10AdExSinExp;

struct _M0TP26RiantR8snn__mbt13STDPSymmetric;

struct _M0TPB5ArrayGbE;

struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__;

struct _M0TP26RiantR8snn__mbt11WilsonCowan;

struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter;

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__;

struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TP26RiantR8snn__mbt2HH;

struct _M0TP26RiantR8snn__mbt2IZ;

struct _M0TWEu;

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

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0TP26RiantR8snn__mbt11IZParameter;

struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__;

struct _M0TUdiE;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__;

struct _M0TP26RiantR8snn__mbt9STDPEntry;

struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TP26RiantR8snn__mbt9IstdpRate;

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables;

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep;

struct _M0BTPB6Logger;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

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

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE;

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus;

struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric;

struct _M0TWRPC15error5ErrorEu;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

struct _M0TP26RiantR8snn__mbt11MorrisLecar;

struct _M0TP26RiantR8snn__mbt7Poisson;

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__;

struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__;

struct _M0TPB8MutLocalGdE;

struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables;

struct _M0TP26RiantR8snn__mbt12PoissonFixed;

struct _M0TP26RiantR8snn__mbt14STDPMexicanHat;

struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables;

struct _M0TPB5ArrayGsE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE;

struct _M0TP26RiantR8snn__mbt14IstdpPotential;

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* $3;
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
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

struct _M0TP26RiantR8snn__mbt11WCParameter {
  float $0;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
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

struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  
};

struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* $1;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* $2;
  
};

struct _M0TWssbEu {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__ {
  struct _M0TP26RiantR8snn__mbt6HetRec* $0;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__ {
  struct _M0TP26RiantR8snn__mbt2HH* $0;
  
};

struct _M0TUsiE {
  moonbit_string_t $0;
  int32_t $1;
  
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

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok {
  int32_t $0;
  
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

struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError {
  moonbit_string_t $0;
  
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

struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* $3;
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__ {
  struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* $0;
  
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

struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__ {
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* $0;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
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

struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__ {
  struct _M0TP26RiantR8snn__mbt7Poisson* $0;
  
};

struct _M0TP26RiantR8snn__mbt11WilsonCowan {
  struct _M0TP26RiantR8snn__mbt11WCParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  
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
  struct _M0TPB5ArrayGiE* $8;
  int32_t $9;
  
};

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
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

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* $1;
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* $2;
  
};

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
};

struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__ {
  struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
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

struct _M0TWRPC15error5ErrorEu {
  int32_t(* code)(struct _M0TWRPC15error5ErrorEu*, void*);
  
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

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
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

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err {
  void* $0;
  
};

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__ {
  struct _M0TP26RiantR8snn__mbt9STDPEntry* $0;
  
};

struct _M0TPB8MutLocalGdE {
  double $0;
  
};

struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__ {
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* $0;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
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

struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables {
  int32_t $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGbE* $4;
  
};

struct _M0TPB5ArrayGsE {
  moonbit_string_t* $0;
  int32_t $1;
  
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

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2071(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2064(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2059(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2036(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2029(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples34afferent__response__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

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

int32_t _M0FP26RiantR8snn__mbt20ca__plasticity__step(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables*,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter*,
  float,
  float
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

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  int32_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

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

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

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

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
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

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t*,
  int32_t,
  int32_t*,
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t*,
  int32_t,
  int32_t*,
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

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

double cos(double);

moonbit_string_t* moonbit_rt_get_cli_args();

float logf(float);

double sin(double);

float tanhf(float);

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
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_12 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[122]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 121, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 97, 102, 102, 101, 114, 101, 110, 
    116, 95, 114, 101, 115, 112, 111, 110, 115, 101, 95, 98, 108, 97, 
    99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 
    114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 
    115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_34 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[124]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 123, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 97, 102, 102, 101, 114, 101, 110, 
    116, 95, 114, 101, 115, 112, 111, 110, 115, 101, 95, 98, 108, 97, 
    99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_32 =
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
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 118, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 115, 116, 97, 114, 116, 34, 44, 34, 
    102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_18 =
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
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2071$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2071
  };

uint32_t const moonbit_layout_table_data[123] =
  {
    sizeof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059)
    / 4, 1,
    offsetof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059, $1)
    / 4
    * 2,
    sizeof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064)
    / 4, 1,
    offsetof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
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
    sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $3) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonFixed, $2) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4Time) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $1) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS5676
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS2092,
  moonbit_string_t _M0L8filenameS2061,
  int32_t _M0L5indexS2063
) {
  struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059* _closure_5887;
  struct _M0TWEu* _M0L13handle__startS2059;
  struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064* _closure_5888;
  struct _M0TWssbEu* _M0L14handle__resultS2064;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS2071;
  void* _M0L11_2atry__errS2086;
  struct moonbit_result_0 _tmp_5890;
  int32_t _handle__error__result_5891;
  int32_t _M0L6_2atmpS5664;
  void* _M0L3errS2087;
  moonbit_string_t _M0L4nameS2089;
  struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS2090;
  moonbit_string_t _M0L7_2anameS2091;
  int32_t _M0L6_2acntS5709;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS2061);
  _closure_5887
  = (struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059*)moonbit_malloc(sizeof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059));
  Moonbit_object_header(_closure_5887)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_5887->code
  = &_M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2059;
  _closure_5887->$0 = _M0L5indexS2063;
  _closure_5887->$1 = _M0L8filenameS2061;
  _M0L13handle__startS2059 = (struct _M0TWEu*)_closure_5887;
  moonbit_incref_cycle_free(_M0L8filenameS2061);
  _closure_5888
  = (struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064*)moonbit_malloc(sizeof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064));
  Moonbit_object_header(_closure_5888)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_5888->code
  = &_M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2064;
  _closure_5888->$0 = _M0L5indexS2063;
  _closure_5888->$1 = _M0L8filenameS2061;
  _M0L14handle__resultS2064 = (struct _M0TWssbEu*)_closure_5888;
  _M0L17error__to__stringS2071
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2071$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _tmp_5890
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS2092, _M0L8filenameS2061, _M0L5indexS2063, _M0L13handle__startS2059, _M0L14handle__resultS2064, _M0L17error__to__stringS2071);
  if (_tmp_5890.tag) {
    int32_t const _M0L5_2aokS5673 = _tmp_5890.data.ok;
    _handle__error__result_5891 = _M0L5_2aokS5673;
  } else {
    void* const _M0L6_2aerrS5674 = _tmp_5890.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
    moonbit_decref_cycle_free(_M0L13handle__startS2059);
    _M0L11_2atry__errS2086 = _M0L6_2aerrS5674;
    goto join_2085;
  }
  if (_handle__error__result_5891) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
    moonbit_decref_cycle_free(_M0L13handle__startS2059);
    _M0L6_2atmpS5664 = 1;
  } else {
    struct moonbit_result_0 _tmp_5892;
    int32_t _handle__error__result_5893;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
    _tmp_5892
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS2092, _M0L8filenameS2061, _M0L5indexS2063, _M0L13handle__startS2059, _M0L14handle__resultS2064, _M0L17error__to__stringS2071);
    if (_tmp_5892.tag) {
      int32_t const _M0L5_2aokS5671 = _tmp_5892.data.ok;
      _handle__error__result_5893 = _M0L5_2aokS5671;
    } else {
      void* const _M0L6_2aerrS5672 = _tmp_5892.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
      moonbit_decref_cycle_free(_M0L13handle__startS2059);
      _M0L11_2atry__errS2086 = _M0L6_2aerrS5672;
      goto join_2085;
    }
    if (_handle__error__result_5893) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
      moonbit_decref_cycle_free(_M0L13handle__startS2059);
      _M0L6_2atmpS5664 = 1;
    } else {
      struct moonbit_result_0 _tmp_5894;
      int32_t _handle__error__result_5895;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _tmp_5894
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS2092, _M0L8filenameS2061, _M0L5indexS2063, _M0L13handle__startS2059, _M0L14handle__resultS2064, _M0L17error__to__stringS2071);
      if (_tmp_5894.tag) {
        int32_t const _M0L5_2aokS5669 = _tmp_5894.data.ok;
        _handle__error__result_5895 = _M0L5_2aokS5669;
      } else {
        void* const _M0L6_2aerrS5670 = _tmp_5894.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
        moonbit_decref_cycle_free(_M0L13handle__startS2059);
        _M0L11_2atry__errS2086 = _M0L6_2aerrS5670;
        goto join_2085;
      }
      if (_handle__error__result_5895) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
        moonbit_decref_cycle_free(_M0L13handle__startS2059);
        _M0L6_2atmpS5664 = 1;
      } else {
        struct moonbit_result_0 _tmp_5896;
        int32_t _handle__error__result_5897;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
        _tmp_5896
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS2092, _M0L8filenameS2061, _M0L5indexS2063, _M0L13handle__startS2059, _M0L14handle__resultS2064, _M0L17error__to__stringS2071);
        if (_tmp_5896.tag) {
          int32_t const _M0L5_2aokS5667 = _tmp_5896.data.ok;
          _handle__error__result_5897 = _M0L5_2aokS5667;
        } else {
          void* const _M0L6_2aerrS5668 = _tmp_5896.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
          moonbit_decref_cycle_free(_M0L13handle__startS2059);
          _M0L11_2atry__errS2086 = _M0L6_2aerrS5668;
          goto join_2085;
        }
        if (_handle__error__result_5897) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
          moonbit_decref_cycle_free(_M0L13handle__startS2059);
          _M0L6_2atmpS5664 = 1;
        } else {
          struct moonbit_result_0 _tmp_5898;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
          _tmp_5898
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS2092, _M0L8filenameS2061, _M0L5indexS2063, _M0L13handle__startS2059, _M0L14handle__resultS2064, _M0L17error__to__stringS2071);
          moonbit_decref_cycle_free(_M0L13handle__startS2059);
          moonbit_decref_cycle_free(_M0L17error__to__stringS2071);
          if (_tmp_5898.tag) {
            int32_t const _M0L5_2aokS5665 = _tmp_5898.data.ok;
            _M0L6_2atmpS5664 = _M0L5_2aokS5665;
          } else {
            void* const _M0L6_2aerrS5666 = _tmp_5898.data.err;
            _M0L11_2atry__errS2086 = _M0L6_2aerrS5666;
            goto join_2085;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS5664) {
    void* _M0L137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5675 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5675)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5675)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS2086
    = _M0L137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5675;
    goto join_2085;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS2064);
  }
  goto joinlet_5889;
  join_2085:;
  _M0L3errS2087 = _M0L11_2atry__errS2086;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS2090
  = (struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS2087;
  _M0L7_2anameS2091 = _M0L36_2aMoonBitTestDriverInternalSkipTestS2090->$0;
  _M0L6_2acntS5709
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS2090));
  if (_M0L6_2acntS5709 > 1) {
    int32_t _M0L11_2anew__cntS5710 = _M0L6_2acntS5709 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS2090), _M0L11_2anew__cntS5710);
    moonbit_incref_cycle_free(_M0L7_2anameS2091);
  } else if (_M0L6_2acntS5709 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS2090);
  }
  _M0L4nameS2089 = _M0L7_2anameS2091;
  goto join_2088;
  goto joinlet_5899;
  join_2088:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2064(_M0L14handle__resultS2064, _M0L4nameS2089, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS2064);
  moonbit_decref_cycle_free(_M0L4nameS2089);
  joinlet_5899:;
  joinlet_5889:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2071(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS5663,
  void* _M0L3errS2072
) {
  void* _M0L1eS2074;
  moonbit_string_t _M0L1eS2076;
  moonbit_string_t _result_5902;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS2072)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS2077 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS2072;
      moonbit_string_t _M0L4_2aeS2078 = _M0L10_2aFailureS2077->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2078);
      _M0L1eS2076 = _M0L4_2aeS2078;
      goto join_2075;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS2079 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS2072;
      moonbit_string_t _M0L4_2aeS2080 = _M0L15_2aInspectErrorS2079->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2080);
      _M0L1eS2076 = _M0L4_2aeS2080;
      goto join_2075;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS2081 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS2072;
      moonbit_string_t _M0L4_2aeS2082 = _M0L16_2aSnapshotErrorS2081->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2082);
      _M0L1eS2076 = _M0L4_2aeS2082;
      goto join_2075;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS2083 =
        (struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS2072;
      moonbit_string_t _M0L4_2aeS2084 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS2083->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2084);
      _M0L1eS2076 = _M0L4_2aeS2084;
      goto join_2075;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS2072);
      _M0L1eS2074 = _M0L3errS2072;
      goto join_2073;
      break;
    }
  }
  join_2075:;
  return _M0L1eS2076;
  join_2073:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _result_5902 = _M0FP15Error10to__string(_M0L1eS2074);
  moonbit_decref_cycle_free(_M0L1eS2074);
  return _result_5902;
}

int32_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2064(
  struct _M0TWssbEu* _M0L6_2aenvS5660,
  moonbit_string_t _M0L10__testnameS2065,
  moonbit_string_t _M0L7messageS2066,
  int32_t _M0L7skippedS2067
) {
  struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064* _M0L14_2acasted__envS5661;
  moonbit_string_t _M0L8filenameS2061;
  int32_t _M0L5indexS2063;
  moonbit_string_t _M0L10file__nameS2068;
  moonbit_string_t _M0L7messageS2069;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS2070;
  moonbit_string_t _M0L6_2atmpS5662;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5661
  = (struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2064*)_M0L6_2aenvS5660;
  _M0L8filenameS2061 = _M0L14_2acasted__envS5661->$1;
  _M0L5indexS2063 = _M0L14_2acasted__envS5661->$0;
  if (!_M0L7skippedS2067 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS2068
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS2061, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS2069
  = _M0MPC16string6String14escape_2einner(_M0L7messageS2066, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS2070
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2070, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2070, _M0L10file__nameS2068);
  moonbit_decref_cycle_free(_M0L10file__nameS2068);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2070, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS2070, _M0L5indexS2063);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2070, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2070, _M0L7messageS2069);
  moonbit_decref_cycle_free(_M0L7messageS2069);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2070, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5662
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS2070);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS2070);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5662);
  moonbit_decref_cycle_free(_M0L6_2atmpS5662);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2059(
  struct _M0TWEu* _M0L6_2aenvS5657
) {
  struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059* _M0L14_2acasted__envS5658;
  moonbit_string_t _M0L8filenameS2061;
  int32_t _M0L5indexS2063;
  moonbit_string_t _M0L10file__nameS2060;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS2062;
  moonbit_string_t _M0L6_2atmpS5659;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5658
  = (struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fafferent__response__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2059*)_M0L6_2aenvS5657;
  _M0L8filenameS2061 = _M0L14_2acasted__envS5658->$1;
  _M0L5indexS2063 = _M0L14_2acasted__envS5658->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS2060
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS2061, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS2062
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2062, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2062, _M0L10file__nameS2060);
  moonbit_decref_cycle_free(_M0L10file__nameS2060);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2062, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS2062, _M0L5indexS2063);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2062, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5659
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS2062);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS2062);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5659);
  moonbit_decref_cycle_free(_M0L6_2atmpS5659);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S2029;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS2036;
  struct _M0TUsiE** _M0L6_2atmpS5656;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS2043;
  moonbit_string_t* _M0L9cli__argsS2044;
  moonbit_string_t _M0L6_2atmpS5655;
  moonbit_string_t _M0L6_2atmpS5654;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS2045;
  int32_t _M0L7_2abindS2046;
  moonbit_string_t* _M0L7_2abindS2047;
  int32_t _M0L6_2acntS5711;
  int32_t _M0L2__S2048;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S2029 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS2036 = 0;
  _M0L6_2atmpS5656 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS2043
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS2043)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS2043->$0 = _M0L6_2atmpS5656;
  _M0L16file__and__indexS2043->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS2044
  = _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS2044)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS5655 = (moonbit_string_t)_M0L9cli__argsS2044[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS5655);
  moonbit_decref_cycle_free(_M0L9cli__argsS2044);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5654
  = _M0MP46RiantR8snn__mbt8examples34afferent__response__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS5655);
  moonbit_decref_cycle_free(_M0L6_2atmpS5655);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS2045
  = _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2036(_M0L51moonbit__test__driver__internal__split__mbt__stringS2036, _M0L6_2atmpS5654, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS5654);
  _M0L7_2abindS2046 = _M0L10test__argsS2045->$1;
  _M0L7_2abindS2047 = _M0L10test__argsS2045->$0;
  _M0L6_2acntS5711
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS2045));
  if (_M0L6_2acntS5711 > 1) {
    int32_t _M0L11_2anew__cntS5712 = _M0L6_2acntS5711 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS2045), _M0L11_2anew__cntS5712);
    moonbit_incref_cycle_free(_M0L7_2abindS2047);
  } else if (_M0L6_2acntS5711 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS2045);
  }
  _M0L2__S2048 = 0;
  while (1) {
    if (_M0L2__S2048 < _M0L7_2abindS2046) {
      moonbit_string_t _M0L3argS2049 =
        (moonbit_string_t)_M0L7_2abindS2047[_M0L2__S2048];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS2050;
      moonbit_string_t _M0L4fileS2051;
      moonbit_string_t _M0L5rangeS2052;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS2053;
      moonbit_string_t _M0L6_2atmpS5652;
      int32_t _M0L5startS2054;
      moonbit_string_t _M0L6_2atmpS5651;
      int32_t _M0L3endS2055;
      int32_t _M0L1iS2056;
      int32_t _M0L6_2atmpS5653;
      moonbit_incref_cycle_free(_M0L3argS2049);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS2050
      = _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2036(_M0L51moonbit__test__driver__internal__split__mbt__stringS2036, _M0L3argS2049, 58);
      moonbit_decref_cycle_free(_M0L3argS2049);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS2051
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS2050, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS2052
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS2050, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS2050);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS2053
      = _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2036(_M0L51moonbit__test__driver__internal__split__mbt__stringS2036, _M0L5rangeS2052, 45);
      moonbit_decref_cycle_free(_M0L5rangeS2052);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5652
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS2053, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS2054
      = _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2029(_M0L45moonbit__test__driver__internal__parse__int__S2029, _M0L6_2atmpS5652);
      moonbit_decref_cycle_free(_M0L6_2atmpS5652);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5651
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS2053, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS2053);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS2055
      = _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2029(_M0L45moonbit__test__driver__internal__parse__int__S2029, _M0L6_2atmpS5651);
      moonbit_decref_cycle_free(_M0L6_2atmpS5651);
      _M0L1iS2056 = _M0L5startS2054;
      while (1) {
        if (_M0L1iS2056 < _M0L3endS2055) {
          struct _M0TUsiE* _M0L8_2atupleS5649;
          int32_t _M0L6_2atmpS5650;
          moonbit_incref_cycle_free(_M0L4fileS2051);
          _M0L8_2atupleS5649
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS5649)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS5649->$0 = _M0L4fileS2051;
          _M0L8_2atupleS5649->$1 = _M0L1iS2056;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS2043, _M0L8_2atupleS5649);
          _M0L6_2atmpS5650 = _M0L1iS2056 + 1;
          _M0L1iS2056 = _M0L6_2atmpS5650;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS2051);
        }
        break;
      }
      _M0L6_2atmpS5653 = _M0L2__S2048 + 1;
      _M0L2__S2048 = _M0L6_2atmpS5653;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS2047);
    }
    break;
  }
  return _M0L16file__and__indexS2043;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2036(
  int32_t _M0L6_2aenvS5630,
  moonbit_string_t _M0L1sS2037,
  int32_t _M0L3sepS2038
) {
  moonbit_string_t* _M0L6_2atmpS5648;
  struct _M0TPB5ArrayGsE* _M0L3resS2039;
  struct _M0TPB8MutLocalGiE* _M0L1iS2040;
  struct _M0TPB8MutLocalGiE* _M0L5startS2041;
  int32_t _M0L3valS5643;
  int32_t _M0L6_2atmpS5644;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5648 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS2039
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS2039)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS2039->$0 = _M0L6_2atmpS5648;
  _M0L3resS2039->$1 = 0;
  _M0L1iS2040
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS2040)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS2040->$0 = 0;
  _M0L5startS2041
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS2041)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS2041->$0 = 0;
  while (1) {
    int32_t _M0L3valS5631 = _M0L1iS2040->$0;
    int32_t _M0L6_2atmpS5632 = Moonbit_array_length(_M0L1sS2037);
    if (_M0L3valS5631 < _M0L6_2atmpS5632) {
      int32_t _M0L3valS5635 = _M0L1iS2040->$0;
      int32_t _M0L6_2atmpS5634;
      int32_t _M0L6_2atmpS5633;
      int32_t _M0L3valS5642;
      int32_t _M0L6_2atmpS5641;
      if (
        _M0L3valS5635 < 0
        || _M0L3valS5635 >= Moonbit_array_length(_M0L1sS2037)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5634 = _M0L1sS2037[_M0L3valS5635];
      _M0L6_2atmpS5633 = _M0L6_2atmpS5634;
      if (_M0L6_2atmpS5633 == _M0L3sepS2038) {
        int32_t _M0L3valS5637 = _M0L5startS2041->$0;
        int32_t _M0L3valS5638 = _M0L1iS2040->$0;
        moonbit_string_t _M0L6_2atmpS5636;
        int32_t _M0L3valS5640;
        int32_t _M0L6_2atmpS5639;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS5636
        = _M0MPC16string6String17unsafe__substring(_M0L1sS2037, _M0L3valS5637, _M0L3valS5638);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS2039, _M0L6_2atmpS5636);
        _M0L3valS5640 = _M0L1iS2040->$0;
        _M0L6_2atmpS5639 = _M0L3valS5640 + 1;
        _M0L5startS2041->$0 = _M0L6_2atmpS5639;
      }
      _M0L3valS5642 = _M0L1iS2040->$0;
      _M0L6_2atmpS5641 = _M0L3valS5642 + 1;
      _M0L1iS2040->$0 = _M0L6_2atmpS5641;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS2040);
    }
    break;
  }
  _M0L3valS5643 = _M0L5startS2041->$0;
  _M0L6_2atmpS5644 = Moonbit_array_length(_M0L1sS2037);
  if (_M0L3valS5643 < _M0L6_2atmpS5644) {
    int32_t _M0L3valS5646 = _M0L5startS2041->$0;
    int32_t _M0L6_2atmpS5647;
    moonbit_string_t _M0L6_2atmpS5645;
    moonbit_decref_cycle_free(_M0L5startS2041);
    _M0L6_2atmpS5647 = Moonbit_array_length(_M0L1sS2037);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS5645
    = _M0MPC16string6String17unsafe__substring(_M0L1sS2037, _M0L3valS5646, _M0L6_2atmpS5647);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS2039, _M0L6_2atmpS5645);
  } else {
    moonbit_decref_cycle_free(_M0L5startS2041);
  }
  return _M0L3resS2039;
}

int32_t _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2029(
  int32_t _M0L6_2aenvS5623,
  moonbit_string_t _M0L1sS2030
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS2031;
  int32_t _M0L3lenS2032;
  int32_t _M0L7_2abindS2033;
  int32_t _M0L1iS2034;
  int32_t _result_5907;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS2031
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS2031)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS2031->$0 = 0;
  _M0L3lenS2032 = Moonbit_array_length(_M0L1sS2030);
  _M0L7_2abindS2033 = 0;
  _M0L1iS2034 = _M0L7_2abindS2033;
  while (1) {
    if (_M0L1iS2034 < _M0L3lenS2032) {
      int32_t _M0L3valS5628 = _M0L3resS2031->$0;
      int32_t _M0L6_2atmpS5625 = _M0L3valS5628 * 10;
      int32_t _M0L6_2atmpS5627;
      int32_t _M0L6_2atmpS5626;
      int32_t _M0L6_2atmpS5624;
      int32_t _M0L6_2atmpS5629;
      if (
        _M0L1iS2034 < 0 || _M0L1iS2034 >= Moonbit_array_length(_M0L1sS2030)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5627 = _M0L1sS2030[_M0L1iS2034];
      _M0L6_2atmpS5626 = _M0L6_2atmpS5627 - 48;
      _M0L6_2atmpS5624 = _M0L6_2atmpS5625 + _M0L6_2atmpS5626;
      _M0L3resS2031->$0 = _M0L6_2atmpS5624;
      _M0L6_2atmpS5629 = _M0L1iS2034 + 1;
      _M0L1iS2034 = _M0L6_2atmpS5629;
      continue;
    }
    break;
  }
  _result_5907 = _M0L3resS2031->$0;
  moonbit_decref_cycle_free(_M0L3resS2031);
  return _result_5907;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples34afferent__response__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS2028
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS2028);
  return _M0L4selfS2028;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1998,
  moonbit_string_t _M0L12_2adiscard__S1999,
  int32_t _M0L12_2adiscard__S2000,
  struct _M0TWEu* _M0L12_2adiscard__S2001,
  struct _M0TWssbEu* _M0L12_2adiscard__S2002,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2003
) {
  struct moonbit_result_0 _result_5908;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _result_5908.tag = 1;
  _result_5908.data.ok = 0;
  return _result_5908;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2004,
  moonbit_string_t _M0L12_2adiscard__S2005,
  int32_t _M0L12_2adiscard__S2006,
  struct _M0TWEu* _M0L12_2adiscard__S2007,
  struct _M0TWssbEu* _M0L12_2adiscard__S2008,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2009
) {
  struct moonbit_result_0 _result_5909;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _result_5909.tag = 1;
  _result_5909.data.ok = 0;
  return _result_5909;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2010,
  moonbit_string_t _M0L12_2adiscard__S2011,
  int32_t _M0L12_2adiscard__S2012,
  struct _M0TWEu* _M0L12_2adiscard__S2013,
  struct _M0TWssbEu* _M0L12_2adiscard__S2014,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2015
) {
  struct moonbit_result_0 _result_5910;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _result_5910.tag = 1;
  _result_5910.data.ok = 0;
  return _result_5910;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2016,
  moonbit_string_t _M0L12_2adiscard__S2017,
  int32_t _M0L12_2adiscard__S2018,
  struct _M0TWEu* _M0L12_2adiscard__S2019,
  struct _M0TWssbEu* _M0L12_2adiscard__S2020,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2021
) {
  struct moonbit_result_0 _result_5911;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _result_5911.tag = 1;
  _result_5911.data.ok = 0;
  return _result_5911;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2022,
  moonbit_string_t _M0L12_2adiscard__S2023,
  int32_t _M0L12_2adiscard__S2024,
  struct _M0TWEu* _M0L12_2adiscard__S2025,
  struct _M0TWssbEu* _M0L12_2adiscard__S2026,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2027
) {
  struct moonbit_result_0 _result_5912;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _result_5912.tag = 1;
  _result_5912.data.ok = 0;
  return _result_5912;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1997
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1870,
  float _M0L2dtS1875
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2abindS1869;
  int32_t _M0L7_2abindS1871;
  void** _M0L7_2abindS1872;
  int32_t _M0L2__S1873;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1877;
  int32_t _M0L7_2abindS1878;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1879;
  int32_t _M0L2__S1880;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2abindS1883;
  int32_t _M0L7_2abindS1884;
  void** _M0L7_2abindS1885;
  int32_t _M0L2__S1886;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1904;
  int32_t _M0L7_2abindS1905;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1906;
  int32_t _M0L2__S1907;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2abindS1910;
  int32_t _M0L7_2abindS1911;
  void** _M0L7_2abindS1912;
  int32_t _M0L2__S1913;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L7_2abindS1956;
  int32_t _M0L7_2abindS1957;
  void** _M0L7_2abindS1958;
  int32_t _M0L2__S1959;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1962;
  int32_t _M0L7_2abindS1963;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1964;
  int32_t _M0L2__S1965;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5622;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L7_2abindS1869 = _M0L1mS1870->$2;
  _M0L7_2abindS1871 = _M0L7_2abindS1869->$1;
  _M0L7_2abindS1872 = _M0L7_2abindS1869->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1872);
  _M0L2__S1873 = 0;
  while (1) {
    if (_M0L2__S1873 < _M0L7_2abindS1871) {
      void* _M0L1sS1874 = (void*)_M0L7_2abindS1872[_M0L2__S1873];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5431 = _M0L1mS1870->$3;
      int32_t _M0L6_2atmpS5432;
      moonbit_incref_cycle_free(_M0L1sS1874);
      #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14stimulate__any(_M0L1sS1874, _M0L4timeS5431, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L1sS1874);
      _M0L6_2atmpS5432 = _M0L2__S1873 + 1;
      _M0L2__S1873 = _M0L6_2atmpS5432;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1872);
    }
    break;
  }
  _M0L7_2abindS1877 = _M0L1mS1870->$1;
  _M0L7_2abindS1878 = _M0L7_2abindS1877->$1;
  _M0L7_2abindS1879 = _M0L7_2abindS1877->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1879);
  _M0L2__S1880 = 0;
  while (1) {
    if (_M0L2__S1880 < _M0L7_2abindS1878) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1881 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1879[
          _M0L2__S1880
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5434 = _M0L1mS1870->$3;
      float _M0L6_2atmpS5433;
      int32_t _M0L6_2atmpS5435;
      moonbit_incref_cycle_free(_M0L1cS1881);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5433 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5434);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1881, _M0L6_2atmpS5433);
      moonbit_decref_cycle_free(_M0L1cS1881);
      _M0L6_2atmpS5435 = _M0L2__S1880 + 1;
      _M0L2__S1880 = _M0L6_2atmpS5435;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1879);
    }
    break;
  }
  _M0L7_2abindS1883 = _M0L1mS1870->$6;
  _M0L7_2abindS1884 = _M0L7_2abindS1883->$1;
  _M0L7_2abindS1885 = _M0L7_2abindS1883->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1885);
  _M0L2__S1886 = 0;
  while (1) {
    if (_M0L2__S1886 < _M0L7_2abindS1884) {
      void* _M0L5entryS1887 = (void*)_M0L7_2abindS1885[_M0L2__S1886];
      struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L1eS1889;
      struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L1eS1892;
      struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L1eS1895;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5452;
      int32_t _M0L11conn__indexS5453;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1896;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5448;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS5449;
      int32_t _M0L6_2acntS5717;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5451;
      float _M0L6_2atmpS5450;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5446;
      int32_t _M0L11conn__indexS5447;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1893;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5442;
      struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS5443;
      int32_t _M0L6_2acntS5715;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5445;
      float _M0L6_2atmpS5444;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5440;
      int32_t _M0L11conn__indexS5441;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1890;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5436;
      struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS5437;
      int32_t _M0L6_2acntS5713;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5439;
      float _M0L6_2atmpS5438;
      int32_t _M0L6_2atmpS5454;
      switch (Moonbit_object_tag(_M0L5entryS1887)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__* _M0L15_2aMarkramSTP__S1897 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__*)_M0L5entryS1887;
          struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L4_2aeS1898 =
            _M0L15_2aMarkramSTP__S1897->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1898);
          _M0L1eS1895 = _M0L4_2aeS1898;
          goto join_1894;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__* _M0L18_2aMarkramSTPHet__S1899 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__*)_M0L5entryS1887;
          struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L4_2aeS1900 =
            _M0L18_2aMarkramSTPHet__S1899->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1900);
          _M0L1eS1892 = _M0L4_2aeS1900;
          goto join_1891;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__* _M0L23_2aMarkramSTPTimestep__S1901 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__*)_M0L5entryS1887;
          struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L4_2aeS1902 =
            _M0L23_2aMarkramSTPTimestep__S1901->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1902);
          _M0L1eS1889 = _M0L4_2aeS1902;
          goto join_1888;
          break;
        }
      }
      goto joinlet_5918;
      join_1894:;
      _M0L5connsS5452 = _M0L1mS1870->$1;
      _M0L11conn__indexS5453 = _M0L1eS1895->$0;
      #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1896
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5452, _M0L11conn__indexS5453);
      _M0L4varsS5448 = _M0L1eS1895->$1;
      _M0L5paramS5449 = _M0L1eS1895->$2;
      _M0L6_2acntS5717 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1895));
      if (_M0L6_2acntS5717 > 1) {
        int32_t _M0L11_2anew__cntS5718 = _M0L6_2acntS5717 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1895), _M0L11_2anew__cntS5718);
        moonbit_incref_cycle_free(_M0L5paramS5449);
        moonbit_incref_cycle_free(_M0L4varsS5448);
      } else if (_M0L6_2acntS5717 == 1) {
        #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1895);
      }
      _M0L4timeS5451 = _M0L1mS1870->$3;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5450 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5451);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt18markram__stp__step(_M0L3synS1896, _M0L4varsS5448, _M0L5paramS5449, _M0L6_2atmpS5450);
      moonbit_decref_cycle_free(_M0L3synS1896);
      moonbit_decref_cycle_free(_M0L4varsS5448);
      moonbit_decref_cycle_free(_M0L5paramS5449);
      joinlet_5918:;
      goto joinlet_5917;
      join_1891:;
      _M0L5connsS5446 = _M0L1mS1870->$1;
      _M0L11conn__indexS5447 = _M0L1eS1892->$0;
      #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1893
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5446, _M0L11conn__indexS5447);
      _M0L4varsS5442 = _M0L1eS1892->$1;
      _M0L5paramS5443 = _M0L1eS1892->$2;
      _M0L6_2acntS5715 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1892));
      if (_M0L6_2acntS5715 > 1) {
        int32_t _M0L11_2anew__cntS5716 = _M0L6_2acntS5715 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1892), _M0L11_2anew__cntS5716);
        moonbit_incref_cycle_free(_M0L5paramS5443);
        moonbit_incref_cycle_free(_M0L4varsS5442);
      } else if (_M0L6_2acntS5715 == 1) {
        #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1892);
      }
      _M0L4timeS5445 = _M0L1mS1870->$3;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5444 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5445);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt23markram__stp__step__het(_M0L3synS1893, _M0L4varsS5442, _M0L5paramS5443, _M0L6_2atmpS5444);
      moonbit_decref_cycle_free(_M0L3synS1893);
      moonbit_decref_cycle_free(_M0L4varsS5442);
      moonbit_decref_cycle_free(_M0L5paramS5443);
      joinlet_5917:;
      goto joinlet_5916;
      join_1888:;
      _M0L5connsS5440 = _M0L1mS1870->$1;
      _M0L11conn__indexS5441 = _M0L1eS1889->$0;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1890
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5440, _M0L11conn__indexS5441);
      _M0L4varsS5436 = _M0L1eS1889->$1;
      _M0L5paramS5437 = _M0L1eS1889->$2;
      _M0L6_2acntS5713 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1889));
      if (_M0L6_2acntS5713 > 1) {
        int32_t _M0L11_2anew__cntS5714 = _M0L6_2acntS5713 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1889), _M0L11_2anew__cntS5714);
        moonbit_incref_cycle_free(_M0L5paramS5437);
        moonbit_incref_cycle_free(_M0L4varsS5436);
      } else if (_M0L6_2acntS5713 == 1) {
        #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1889);
      }
      _M0L4timeS5439 = _M0L1mS1870->$3;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5438 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5439);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(_M0L3synS1890, _M0L4varsS5436, _M0L5paramS5437, _M0L6_2atmpS5438, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L3synS1890);
      moonbit_decref_cycle_free(_M0L4varsS5436);
      moonbit_decref_cycle_free(_M0L5paramS5437);
      joinlet_5916:;
      _M0L6_2atmpS5454 = _M0L2__S1886 + 1;
      _M0L2__S1886 = _M0L6_2atmpS5454;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1885);
    }
    break;
  }
  _M0L7_2abindS1904 = _M0L1mS1870->$1;
  _M0L7_2abindS1905 = _M0L7_2abindS1904->$1;
  _M0L7_2abindS1906 = _M0L7_2abindS1904->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1906);
  _M0L2__S1907 = 0;
  while (1) {
    if (_M0L2__S1907 < _M0L7_2abindS1905) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1908 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1906[
          _M0L2__S1907
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5456 = _M0L1mS1870->$3;
      float _M0L6_2atmpS5455;
      int32_t _M0L6_2atmpS5457;
      moonbit_incref_cycle_free(_M0L1cS1908);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5455 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5456);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1908, _M0L6_2atmpS5455);
      moonbit_decref_cycle_free(_M0L1cS1908);
      _M0L6_2atmpS5457 = _M0L2__S1907 + 1;
      _M0L2__S1907 = _M0L6_2atmpS5457;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1906);
    }
    break;
  }
  _M0L7_2abindS1910 = _M0L1mS1870->$5;
  _M0L7_2abindS1911 = _M0L7_2abindS1910->$1;
  _M0L7_2abindS1912 = _M0L7_2abindS1910->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1912);
  _M0L2__S1913 = 0;
  while (1) {
    if (_M0L2__S1913 < _M0L7_2abindS1911) {
      void* _M0L5entryS1914 = (void*)_M0L7_2abindS1912[_M0L2__S1913];
      struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L1eS1916;
      struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L1eS1919;
      struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L1eS1922;
      struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L1eS1925;
      struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L1eS1928;
      struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L1eS1931;
      struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L1eS1934;
      struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L1eS1937;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5615;
      int32_t _M0L11conn__indexS5616;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1938;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5610;
      struct _M0TPB5ArrayGfE* _M0L4valsS5597;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5609;
      struct _M0TPB5ArrayGbE* _M0L4fireS5598;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5608;
      struct _M0TPB5ArrayGbE* _M0L4fireS5599;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5607;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5600;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5606;
      int32_t _M0L6_2acntS5866;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5601;
      int32_t _M0L6_2acntS5877;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5602;
      struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS5603;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5605;
      float _M0L6_2atmpS5604;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5611;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5614;
      int32_t _M0L6_2acntS5881;
      float _M0L6_2atmpS5613;
      float _M0L6_2atmpS5612;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5595;
      int32_t _M0L11conn__indexS5596;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1935;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5590;
      struct _M0TPB5ArrayGfE* _M0L4valsS5578;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5589;
      struct _M0TPB5ArrayGbE* _M0L4fireS5579;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5588;
      struct _M0TPB5ArrayGbE* _M0L4fireS5580;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5587;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5581;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5586;
      int32_t _M0L6_2acntS5846;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5582;
      int32_t _M0L6_2acntS5857;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5583;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5584;
      struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS5585;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5591;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5594;
      int32_t _M0L6_2acntS5861;
      float _M0L6_2atmpS5593;
      float _M0L6_2atmpS5592;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5576;
      int32_t _M0L11conn__indexS5577;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1932;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5571;
      struct _M0TPB5ArrayGfE* _M0L4valsS5560;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5570;
      struct _M0TPB5ArrayGbE* _M0L4fireS5561;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5569;
      struct _M0TPB5ArrayGbE* _M0L4fireS5562;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5568;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5563;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5567;
      int32_t _M0L6_2acntS5827;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5564;
      int32_t _M0L6_2acntS5838;
      struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS5565;
      struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS5566;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5572;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5575;
      int32_t _M0L6_2acntS5842;
      float _M0L6_2atmpS5574;
      float _M0L6_2atmpS5573;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5558;
      int32_t _M0L11conn__indexS5559;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1929;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5553;
      struct _M0TPB5ArrayGfE* _M0L4valsS5540;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5552;
      struct _M0TPB5ArrayGbE* _M0L4fireS5541;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5551;
      struct _M0TPB5ArrayGbE* _M0L4fireS5542;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5550;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5543;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5549;
      int32_t _M0L6_2acntS5808;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5544;
      int32_t _M0L6_2acntS5819;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5545;
      struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS5546;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5548;
      float _M0L6_2atmpS5547;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5554;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5557;
      int32_t _M0L6_2acntS5823;
      float _M0L6_2atmpS5556;
      float _M0L6_2atmpS5555;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5538;
      int32_t _M0L11conn__indexS5539;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1926;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5533;
      struct _M0TPB5ArrayGfE* _M0L4valsS5520;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5532;
      struct _M0TPB5ArrayGbE* _M0L4fireS5521;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5531;
      struct _M0TPB5ArrayGbE* _M0L4fireS5522;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5530;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5523;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5529;
      int32_t _M0L6_2acntS5789;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5524;
      int32_t _M0L6_2acntS5800;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS5525;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS5526;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5528;
      float _M0L6_2atmpS5527;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5534;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5537;
      int32_t _M0L6_2acntS5804;
      float _M0L6_2atmpS5536;
      float _M0L6_2atmpS5535;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5518;
      int32_t _M0L11conn__indexS5519;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1923;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5513;
      struct _M0TPB5ArrayGfE* _M0L4valsS5498;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5512;
      struct _M0TPB5ArrayGbE* _M0L4fireS5499;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5511;
      struct _M0TPB5ArrayGbE* _M0L4fireS5500;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5510;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5501;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5509;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5502;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5508;
      int32_t _M0L6_2acntS5757;
      struct _M0TPB5ArrayGfE* _M0L1vS5503;
      int32_t _M0L6_2acntS5768;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS5504;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS5505;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5507;
      float _M0L6_2atmpS5506;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5514;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5517;
      int32_t _M0L6_2acntS5785;
      float _M0L6_2atmpS5516;
      float _M0L6_2atmpS5515;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5496;
      int32_t _M0L11conn__indexS5497;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1920;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5491;
      struct _M0TPB5ArrayGfE* _M0L4valsS5478;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5490;
      struct _M0TPB5ArrayGbE* _M0L4fireS5479;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5489;
      struct _M0TPB5ArrayGbE* _M0L4fireS5480;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5488;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5481;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5487;
      int32_t _M0L6_2acntS5738;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5482;
      int32_t _M0L6_2acntS5749;
      struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS5483;
      struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS5484;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5486;
      float _M0L6_2atmpS5485;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5492;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5495;
      int32_t _M0L6_2acntS5753;
      float _M0L6_2atmpS5494;
      float _M0L6_2atmpS5493;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5476;
      int32_t _M0L11conn__indexS5477;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1917;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5471;
      struct _M0TPB5ArrayGfE* _M0L4valsS5458;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5470;
      struct _M0TPB5ArrayGbE* _M0L4fireS5459;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5469;
      struct _M0TPB5ArrayGbE* _M0L4fireS5460;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5468;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5461;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5467;
      int32_t _M0L6_2acntS5719;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5462;
      int32_t _M0L6_2acntS5730;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS5463;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS5464;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5466;
      float _M0L6_2atmpS5465;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5472;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5475;
      int32_t _M0L6_2acntS5734;
      float _M0L6_2atmpS5474;
      float _M0L6_2atmpS5473;
      int32_t _M0L6_2atmpS5617;
      switch (Moonbit_object_tag(_M0L5entryS1914)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__* _M0L13_2aGerstner__S1939 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L5entryS1914;
          struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L4_2aeS1940 =
            _M0L13_2aGerstner__S1939->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1940);
          _M0L1eS1937 = _M0L4_2aeS1940;
          goto join_1936;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__* _M0L15_2aMexicanHat__S1941 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__*)_M0L5entryS1914;
          struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L4_2aeS1942 =
            _M0L15_2aMexicanHat__S1941->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1942);
          _M0L1eS1934 = _M0L4_2aeS1942;
          goto join_1933;
          break;
        }
        
        case 2: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__* _M0L18_2aAntiSymmetric__S1943 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__*)_M0L5entryS1914;
          struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L4_2aeS1944 =
            _M0L18_2aAntiSymmetric__S1943->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1944);
          _M0L1eS1931 = _M0L4_2aeS1944;
          goto join_1930;
          break;
        }
        
        case 3: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__* _M0L19_2aConfavreux2025__S1945 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__*)_M0L5entryS1914;
          struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L4_2aeS1946 =
            _M0L19_2aConfavreux2025__S1945->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1946);
          _M0L1eS1928 = _M0L4_2aeS1946;
          goto join_1927;
          break;
        }
        
        case 4: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__* _M0L14_2aIstdpRate__S1947 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__*)_M0L5entryS1914;
          struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L4_2aeS1948 =
            _M0L14_2aIstdpRate__S1947->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1948);
          _M0L1eS1925 = _M0L4_2aeS1948;
          goto join_1924;
          break;
        }
        
        case 5: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__* _M0L19_2aIstdpPotential__S1949 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__*)_M0L5entryS1914;
          struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L4_2aeS1950 =
            _M0L19_2aIstdpPotential__S1949->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1950);
          _M0L1eS1922 = _M0L4_2aeS1950;
          goto join_1921;
          break;
        }
        
        case 6: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__* _M0L14_2aSymmetric__S1951 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__*)_M0L5entryS1914;
          struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L4_2aeS1952 =
            _M0L14_2aSymmetric__S1951->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1952);
          _M0L1eS1919 = _M0L4_2aeS1952;
          goto join_1918;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__* _M0L17_2aCaPlasticity__S1953 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__*)_M0L5entryS1914;
          struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L4_2aeS1954 =
            _M0L17_2aCaPlasticity__S1953->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1954);
          _M0L1eS1916 = _M0L4_2aeS1954;
          goto join_1915;
          break;
        }
      }
      goto joinlet_5928;
      join_1936:;
      _M0L5connsS5615 = _M0L1mS1870->$1;
      _M0L11conn__indexS5616 = _M0L1eS1937->$0;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1938
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5615, _M0L11conn__indexS5616);
      _M0L6matrixS5610 = _M0L3synS1938->$4;
      _M0L4valsS5597 = _M0L6matrixS5610->$4;
      _M0L3preS5609 = _M0L3synS1938->$0;
      _M0L4fireS5598 = _M0L3preS5609->$5;
      _M0L4postS5608 = _M0L3synS1938->$1;
      _M0L4fireS5599 = _M0L4postS5608->$5;
      _M0L6matrixS5607 = _M0L3synS1938->$4;
      _M0L6colptrS5600 = _M0L6matrixS5607->$3;
      _M0L6matrixS5606 = _M0L3synS1938->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5600);
      moonbit_incref_cycle_free(_M0L4fireS5599);
      moonbit_incref_cycle_free(_M0L4fireS5598);
      moonbit_incref_cycle_free(_M0L4valsS5597);
      _M0L6_2acntS5866
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1938));
      if (_M0L6_2acntS5866 > 1) {
        int32_t _M0L11_2anew__cntS5876 = _M0L6_2acntS5866 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1938), _M0L11_2anew__cntS5876);
        moonbit_incref_cycle_free(_M0L6matrixS5606);
      } else if (_M0L6_2acntS5866 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5875 = _M0L3synS1938->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5874;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5873;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5872;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5871;
        moonbit_string_t _M0L8_2afieldS5870;
        moonbit_string_t _M0L8_2afieldS5869;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5868;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5867;
        moonbit_decref_cycle_free(_M0L8_2afieldS5875);
        _M0L8_2afieldS5874 = _M0L3synS1938->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5874);
        _M0L8_2afieldS5873 = _M0L3synS1938->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5873);
        _M0L8_2afieldS5872 = _M0L3synS1938->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5872);
        _M0L8_2afieldS5871 = _M0L3synS1938->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5871);
        _M0L8_2afieldS5870 = _M0L3synS1938->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5870);
        _M0L8_2afieldS5869 = _M0L3synS1938->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5869);
        _M0L8_2afieldS5868 = _M0L3synS1938->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5868);
        _M0L8_2afieldS5867 = _M0L3synS1938->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5867);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1938);
      }
      _M0L6rowptrS5601 = _M0L6matrixS5606->$2;
      _M0L6_2acntS5877
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5606));
      if (_M0L6_2acntS5877 > 1) {
        int32_t _M0L11_2anew__cntS5880 = _M0L6_2acntS5877 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5606), _M0L11_2anew__cntS5880);
        moonbit_incref_cycle_free(_M0L6rowptrS5601);
      } else if (_M0L6_2acntS5877 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5879 = _M0L6matrixS5606->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5878;
        moonbit_decref_cycle_free(_M0L8_2afieldS5879);
        _M0L8_2afieldS5878 = _M0L6matrixS5606->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5878);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5606);
      }
      _M0L4varsS5602 = _M0L1eS1937->$3;
      _M0L5paramS5603 = _M0L1eS1937->$4;
      _M0L6t__nowS5605 = _M0L1eS1937->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5605);
      moonbit_incref_cycle_free(_M0L5paramS5603);
      moonbit_incref_cycle_free(_M0L4varsS5602);
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5604 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5605, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5605);
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt10stdp__step(_M0L4valsS5597, _M0L4fireS5598, _M0L4fireS5599, _M0L6colptrS5600, _M0L6rowptrS5601, _M0L4varsS5602, _M0L5paramS5603, _M0L6_2atmpS5604, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L4valsS5597);
      moonbit_decref_cycle_free(_M0L4fireS5598);
      moonbit_decref_cycle_free(_M0L4fireS5599);
      moonbit_decref_cycle_free(_M0L6colptrS5600);
      moonbit_decref_cycle_free(_M0L6rowptrS5601);
      moonbit_decref_cycle_free(_M0L4varsS5602);
      moonbit_decref_cycle_free(_M0L5paramS5603);
      _M0L6t__nowS5611 = _M0L1eS1937->$5;
      _M0L6t__nowS5614 = _M0L1eS1937->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5611);
      _M0L6_2acntS5881 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1937));
      if (_M0L6_2acntS5881 > 1) {
        int32_t _M0L11_2anew__cntS5884 = _M0L6_2acntS5881 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1937), _M0L11_2anew__cntS5884);
        moonbit_incref_cycle_free(_M0L6t__nowS5614);
      } else if (_M0L6_2acntS5881 == 1) {
        struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L8_2afieldS5883 =
          _M0L1eS1937->$4;
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5882;
        moonbit_decref_cycle_free(_M0L8_2afieldS5883);
        _M0L8_2afieldS5882 = _M0L1eS1937->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5882);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1937);
      }
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5613 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5614, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5614);
      _M0L6_2atmpS5612 = _M0L6_2atmpS5613 + _M0L2dtS1875;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5611, 0, _M0L6_2atmpS5612);
      moonbit_decref_cycle_free(_M0L6t__nowS5611);
      joinlet_5928:;
      goto joinlet_5927;
      join_1933:;
      _M0L5connsS5595 = _M0L1mS1870->$1;
      _M0L11conn__indexS5596 = _M0L1eS1934->$0;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1935
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5595, _M0L11conn__indexS5596);
      _M0L6matrixS5590 = _M0L3synS1935->$4;
      _M0L4valsS5578 = _M0L6matrixS5590->$4;
      _M0L3preS5589 = _M0L3synS1935->$0;
      _M0L4fireS5579 = _M0L3preS5589->$5;
      _M0L4postS5588 = _M0L3synS1935->$1;
      _M0L4fireS5580 = _M0L4postS5588->$5;
      _M0L6matrixS5587 = _M0L3synS1935->$4;
      _M0L6colptrS5581 = _M0L6matrixS5587->$3;
      _M0L6matrixS5586 = _M0L3synS1935->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5581);
      moonbit_incref_cycle_free(_M0L4fireS5580);
      moonbit_incref_cycle_free(_M0L4fireS5579);
      moonbit_incref_cycle_free(_M0L4valsS5578);
      _M0L6_2acntS5846
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1935));
      if (_M0L6_2acntS5846 > 1) {
        int32_t _M0L11_2anew__cntS5856 = _M0L6_2acntS5846 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1935), _M0L11_2anew__cntS5856);
        moonbit_incref_cycle_free(_M0L6matrixS5586);
      } else if (_M0L6_2acntS5846 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5855 = _M0L3synS1935->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5854;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5853;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5852;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5851;
        moonbit_string_t _M0L8_2afieldS5850;
        moonbit_string_t _M0L8_2afieldS5849;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5848;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5847;
        moonbit_decref_cycle_free(_M0L8_2afieldS5855);
        _M0L8_2afieldS5854 = _M0L3synS1935->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5854);
        _M0L8_2afieldS5853 = _M0L3synS1935->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5853);
        _M0L8_2afieldS5852 = _M0L3synS1935->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5852);
        _M0L8_2afieldS5851 = _M0L3synS1935->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5851);
        _M0L8_2afieldS5850 = _M0L3synS1935->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5850);
        _M0L8_2afieldS5849 = _M0L3synS1935->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5849);
        _M0L8_2afieldS5848 = _M0L3synS1935->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5848);
        _M0L8_2afieldS5847 = _M0L3synS1935->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5847);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1935);
      }
      _M0L6rowptrS5582 = _M0L6matrixS5586->$2;
      _M0L6_2acntS5857
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5586));
      if (_M0L6_2acntS5857 > 1) {
        int32_t _M0L11_2anew__cntS5860 = _M0L6_2acntS5857 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5586), _M0L11_2anew__cntS5860);
        moonbit_incref_cycle_free(_M0L6rowptrS5582);
      } else if (_M0L6_2acntS5857 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5859 = _M0L6matrixS5586->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5858;
        moonbit_decref_cycle_free(_M0L8_2afieldS5859);
        _M0L8_2afieldS5858 = _M0L6matrixS5586->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5858);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5586);
      }
      _M0L4tpreS5583 = _M0L1eS1934->$4;
      _M0L5tpostS5584 = _M0L1eS1934->$5;
      _M0L5paramS5585 = _M0L1eS1934->$3;
      moonbit_incref_cycle_free(_M0L5paramS5585);
      moonbit_incref_cycle_free(_M0L5tpostS5584);
      moonbit_incref_cycle_free(_M0L4tpreS5583);
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(_M0L4valsS5578, _M0L4fireS5579, _M0L4fireS5580, _M0L6colptrS5581, _M0L6rowptrS5582, _M0L4tpreS5583, _M0L5tpostS5584, _M0L5paramS5585, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L4valsS5578);
      moonbit_decref_cycle_free(_M0L4fireS5579);
      moonbit_decref_cycle_free(_M0L4fireS5580);
      moonbit_decref_cycle_free(_M0L6colptrS5581);
      moonbit_decref_cycle_free(_M0L6rowptrS5582);
      moonbit_decref_cycle_free(_M0L4tpreS5583);
      moonbit_decref_cycle_free(_M0L5tpostS5584);
      moonbit_decref_cycle_free(_M0L5paramS5585);
      _M0L6t__nowS5591 = _M0L1eS1934->$6;
      _M0L6t__nowS5594 = _M0L1eS1934->$6;
      moonbit_incref_cycle_free(_M0L6t__nowS5591);
      _M0L6_2acntS5861 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1934));
      if (_M0L6_2acntS5861 > 1) {
        int32_t _M0L11_2anew__cntS5865 = _M0L6_2acntS5861 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1934), _M0L11_2anew__cntS5865);
        moonbit_incref_cycle_free(_M0L6t__nowS5594);
      } else if (_M0L6_2acntS5861 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5864 = _M0L1eS1934->$5;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5863;
        struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L8_2afieldS5862;
        moonbit_decref_cycle_free(_M0L8_2afieldS5864);
        _M0L8_2afieldS5863 = _M0L1eS1934->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5863);
        _M0L8_2afieldS5862 = _M0L1eS1934->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5862);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1934);
      }
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5593 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5594, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5594);
      _M0L6_2atmpS5592 = _M0L6_2atmpS5593 + _M0L2dtS1875;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5591, 0, _M0L6_2atmpS5592);
      moonbit_decref_cycle_free(_M0L6t__nowS5591);
      joinlet_5927:;
      goto joinlet_5926;
      join_1930:;
      _M0L5connsS5576 = _M0L1mS1870->$1;
      _M0L11conn__indexS5577 = _M0L1eS1931->$0;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1932
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5576, _M0L11conn__indexS5577);
      _M0L6matrixS5571 = _M0L3synS1932->$4;
      _M0L4valsS5560 = _M0L6matrixS5571->$4;
      _M0L3preS5570 = _M0L3synS1932->$0;
      _M0L4fireS5561 = _M0L3preS5570->$5;
      _M0L4postS5569 = _M0L3synS1932->$1;
      _M0L4fireS5562 = _M0L4postS5569->$5;
      _M0L6matrixS5568 = _M0L3synS1932->$4;
      _M0L6colptrS5563 = _M0L6matrixS5568->$3;
      _M0L6matrixS5567 = _M0L3synS1932->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5563);
      moonbit_incref_cycle_free(_M0L4fireS5562);
      moonbit_incref_cycle_free(_M0L4fireS5561);
      moonbit_incref_cycle_free(_M0L4valsS5560);
      _M0L6_2acntS5827
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1932));
      if (_M0L6_2acntS5827 > 1) {
        int32_t _M0L11_2anew__cntS5837 = _M0L6_2acntS5827 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1932), _M0L11_2anew__cntS5837);
        moonbit_incref_cycle_free(_M0L6matrixS5567);
      } else if (_M0L6_2acntS5827 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5836 = _M0L3synS1932->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5835;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5834;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5833;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5832;
        moonbit_string_t _M0L8_2afieldS5831;
        moonbit_string_t _M0L8_2afieldS5830;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5829;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5828;
        moonbit_decref_cycle_free(_M0L8_2afieldS5836);
        _M0L8_2afieldS5835 = _M0L3synS1932->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5835);
        _M0L8_2afieldS5834 = _M0L3synS1932->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5834);
        _M0L8_2afieldS5833 = _M0L3synS1932->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5833);
        _M0L8_2afieldS5832 = _M0L3synS1932->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5832);
        _M0L8_2afieldS5831 = _M0L3synS1932->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5831);
        _M0L8_2afieldS5830 = _M0L3synS1932->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5830);
        _M0L8_2afieldS5829 = _M0L3synS1932->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5829);
        _M0L8_2afieldS5828 = _M0L3synS1932->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5828);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1932);
      }
      _M0L6rowptrS5564 = _M0L6matrixS5567->$2;
      _M0L6_2acntS5838
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5567));
      if (_M0L6_2acntS5838 > 1) {
        int32_t _M0L11_2anew__cntS5841 = _M0L6_2acntS5838 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5567), _M0L11_2anew__cntS5841);
        moonbit_incref_cycle_free(_M0L6rowptrS5564);
      } else if (_M0L6_2acntS5838 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5840 = _M0L6matrixS5567->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5839;
        moonbit_decref_cycle_free(_M0L8_2afieldS5840);
        _M0L8_2afieldS5839 = _M0L6matrixS5567->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5839);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5567);
      }
      _M0L4varsS5565 = _M0L1eS1931->$4;
      _M0L5paramS5566 = _M0L1eS1931->$3;
      moonbit_incref_cycle_free(_M0L5paramS5566);
      moonbit_incref_cycle_free(_M0L4varsS5565);
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(_M0L4valsS5560, _M0L4fireS5561, _M0L4fireS5562, _M0L6colptrS5563, _M0L6rowptrS5564, _M0L4varsS5565, _M0L5paramS5566, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L4valsS5560);
      moonbit_decref_cycle_free(_M0L4fireS5561);
      moonbit_decref_cycle_free(_M0L4fireS5562);
      moonbit_decref_cycle_free(_M0L6colptrS5563);
      moonbit_decref_cycle_free(_M0L6rowptrS5564);
      moonbit_decref_cycle_free(_M0L4varsS5565);
      moonbit_decref_cycle_free(_M0L5paramS5566);
      _M0L6t__nowS5572 = _M0L1eS1931->$5;
      _M0L6t__nowS5575 = _M0L1eS1931->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5572);
      _M0L6_2acntS5842 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1931));
      if (_M0L6_2acntS5842 > 1) {
        int32_t _M0L11_2anew__cntS5845 = _M0L6_2acntS5842 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1931), _M0L11_2anew__cntS5845);
        moonbit_incref_cycle_free(_M0L6t__nowS5575);
      } else if (_M0L6_2acntS5842 == 1) {
        struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L8_2afieldS5844 =
          _M0L1eS1931->$4;
        struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L8_2afieldS5843;
        moonbit_decref_cycle_free(_M0L8_2afieldS5844);
        _M0L8_2afieldS5843 = _M0L1eS1931->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5843);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1931);
      }
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5574 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5575, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5575);
      _M0L6_2atmpS5573 = _M0L6_2atmpS5574 + _M0L2dtS1875;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5572, 0, _M0L6_2atmpS5573);
      moonbit_decref_cycle_free(_M0L6t__nowS5572);
      joinlet_5926:;
      goto joinlet_5925;
      join_1927:;
      _M0L5connsS5558 = _M0L1mS1870->$1;
      _M0L11conn__indexS5559 = _M0L1eS1928->$0;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1929
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5558, _M0L11conn__indexS5559);
      _M0L6matrixS5553 = _M0L3synS1929->$4;
      _M0L4valsS5540 = _M0L6matrixS5553->$4;
      _M0L3preS5552 = _M0L3synS1929->$0;
      _M0L4fireS5541 = _M0L3preS5552->$5;
      _M0L4postS5551 = _M0L3synS1929->$1;
      _M0L4fireS5542 = _M0L4postS5551->$5;
      _M0L6matrixS5550 = _M0L3synS1929->$4;
      _M0L6colptrS5543 = _M0L6matrixS5550->$3;
      _M0L6matrixS5549 = _M0L3synS1929->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5543);
      moonbit_incref_cycle_free(_M0L4fireS5542);
      moonbit_incref_cycle_free(_M0L4fireS5541);
      moonbit_incref_cycle_free(_M0L4valsS5540);
      _M0L6_2acntS5808
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1929));
      if (_M0L6_2acntS5808 > 1) {
        int32_t _M0L11_2anew__cntS5818 = _M0L6_2acntS5808 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1929), _M0L11_2anew__cntS5818);
        moonbit_incref_cycle_free(_M0L6matrixS5549);
      } else if (_M0L6_2acntS5808 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5817 = _M0L3synS1929->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5816;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5815;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5814;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5813;
        moonbit_string_t _M0L8_2afieldS5812;
        moonbit_string_t _M0L8_2afieldS5811;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5810;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5809;
        moonbit_decref_cycle_free(_M0L8_2afieldS5817);
        _M0L8_2afieldS5816 = _M0L3synS1929->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5816);
        _M0L8_2afieldS5815 = _M0L3synS1929->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5815);
        _M0L8_2afieldS5814 = _M0L3synS1929->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5814);
        _M0L8_2afieldS5813 = _M0L3synS1929->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5813);
        _M0L8_2afieldS5812 = _M0L3synS1929->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5812);
        _M0L8_2afieldS5811 = _M0L3synS1929->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5811);
        _M0L8_2afieldS5810 = _M0L3synS1929->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5810);
        _M0L8_2afieldS5809 = _M0L3synS1929->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5809);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1929);
      }
      _M0L6rowptrS5544 = _M0L6matrixS5549->$2;
      _M0L6_2acntS5819
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5549));
      if (_M0L6_2acntS5819 > 1) {
        int32_t _M0L11_2anew__cntS5822 = _M0L6_2acntS5819 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5549), _M0L11_2anew__cntS5822);
        moonbit_incref_cycle_free(_M0L6rowptrS5544);
      } else if (_M0L6_2acntS5819 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5821 = _M0L6matrixS5549->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5820;
        moonbit_decref_cycle_free(_M0L8_2afieldS5821);
        _M0L8_2afieldS5820 = _M0L6matrixS5549->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5820);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5549);
      }
      _M0L4varsS5545 = _M0L1eS1928->$4;
      _M0L5paramS5546 = _M0L1eS1928->$3;
      _M0L6t__nowS5548 = _M0L1eS1928->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5548);
      moonbit_incref_cycle_free(_M0L5paramS5546);
      moonbit_incref_cycle_free(_M0L4varsS5545);
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5547 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5548, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5548);
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22stdp__confavreux__step(_M0L4valsS5540, _M0L4fireS5541, _M0L4fireS5542, _M0L6colptrS5543, _M0L6rowptrS5544, _M0L4varsS5545, _M0L5paramS5546, _M0L6_2atmpS5547, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L4valsS5540);
      moonbit_decref_cycle_free(_M0L4fireS5541);
      moonbit_decref_cycle_free(_M0L4fireS5542);
      moonbit_decref_cycle_free(_M0L6colptrS5543);
      moonbit_decref_cycle_free(_M0L6rowptrS5544);
      moonbit_decref_cycle_free(_M0L4varsS5545);
      moonbit_decref_cycle_free(_M0L5paramS5546);
      _M0L6t__nowS5554 = _M0L1eS1928->$5;
      _M0L6t__nowS5557 = _M0L1eS1928->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5554);
      _M0L6_2acntS5823 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1928));
      if (_M0L6_2acntS5823 > 1) {
        int32_t _M0L11_2anew__cntS5826 = _M0L6_2acntS5823 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1928), _M0L11_2anew__cntS5826);
        moonbit_incref_cycle_free(_M0L6t__nowS5557);
      } else if (_M0L6_2acntS5823 == 1) {
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5825 =
          _M0L1eS1928->$4;
        struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L8_2afieldS5824;
        moonbit_decref_cycle_free(_M0L8_2afieldS5825);
        _M0L8_2afieldS5824 = _M0L1eS1928->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5824);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1928);
      }
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5556 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5557, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5557);
      _M0L6_2atmpS5555 = _M0L6_2atmpS5556 + _M0L2dtS1875;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5554, 0, _M0L6_2atmpS5555);
      moonbit_decref_cycle_free(_M0L6t__nowS5554);
      joinlet_5925:;
      goto joinlet_5924;
      join_1924:;
      _M0L5connsS5538 = _M0L1mS1870->$1;
      _M0L11conn__indexS5539 = _M0L1eS1925->$0;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1926
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5538, _M0L11conn__indexS5539);
      _M0L6matrixS5533 = _M0L3synS1926->$4;
      _M0L4valsS5520 = _M0L6matrixS5533->$4;
      _M0L3preS5532 = _M0L3synS1926->$0;
      _M0L4fireS5521 = _M0L3preS5532->$5;
      _M0L4postS5531 = _M0L3synS1926->$1;
      _M0L4fireS5522 = _M0L4postS5531->$5;
      _M0L6matrixS5530 = _M0L3synS1926->$4;
      _M0L6colptrS5523 = _M0L6matrixS5530->$3;
      _M0L6matrixS5529 = _M0L3synS1926->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5523);
      moonbit_incref_cycle_free(_M0L4fireS5522);
      moonbit_incref_cycle_free(_M0L4fireS5521);
      moonbit_incref_cycle_free(_M0L4valsS5520);
      _M0L6_2acntS5789
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1926));
      if (_M0L6_2acntS5789 > 1) {
        int32_t _M0L11_2anew__cntS5799 = _M0L6_2acntS5789 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1926), _M0L11_2anew__cntS5799);
        moonbit_incref_cycle_free(_M0L6matrixS5529);
      } else if (_M0L6_2acntS5789 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5798 = _M0L3synS1926->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5797;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5796;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5795;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5794;
        moonbit_string_t _M0L8_2afieldS5793;
        moonbit_string_t _M0L8_2afieldS5792;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5791;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5790;
        moonbit_decref_cycle_free(_M0L8_2afieldS5798);
        _M0L8_2afieldS5797 = _M0L3synS1926->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5797);
        _M0L8_2afieldS5796 = _M0L3synS1926->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5796);
        _M0L8_2afieldS5795 = _M0L3synS1926->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5795);
        _M0L8_2afieldS5794 = _M0L3synS1926->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5794);
        _M0L8_2afieldS5793 = _M0L3synS1926->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5793);
        _M0L8_2afieldS5792 = _M0L3synS1926->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5792);
        _M0L8_2afieldS5791 = _M0L3synS1926->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5791);
        _M0L8_2afieldS5790 = _M0L3synS1926->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5790);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1926);
      }
      _M0L6rowptrS5524 = _M0L6matrixS5529->$2;
      _M0L6_2acntS5800
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5529));
      if (_M0L6_2acntS5800 > 1) {
        int32_t _M0L11_2anew__cntS5803 = _M0L6_2acntS5800 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5529), _M0L11_2anew__cntS5803);
        moonbit_incref_cycle_free(_M0L6rowptrS5524);
      } else if (_M0L6_2acntS5800 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5802 = _M0L6matrixS5529->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5801;
        moonbit_decref_cycle_free(_M0L8_2afieldS5802);
        _M0L8_2afieldS5801 = _M0L6matrixS5529->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5801);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5529);
      }
      _M0L4varsS5525 = _M0L1eS1925->$4;
      _M0L5paramS5526 = _M0L1eS1925->$3;
      _M0L6t__nowS5528 = _M0L1eS1925->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5528);
      moonbit_incref_cycle_free(_M0L5paramS5526);
      moonbit_incref_cycle_free(_M0L4varsS5525);
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5527 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5528, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5528);
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS5520, _M0L4fireS5521, _M0L4fireS5522, _M0L6colptrS5523, _M0L6rowptrS5524, _M0L4varsS5525, _M0L5paramS5526, _M0L6_2atmpS5527, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L4valsS5520);
      moonbit_decref_cycle_free(_M0L4fireS5521);
      moonbit_decref_cycle_free(_M0L4fireS5522);
      moonbit_decref_cycle_free(_M0L6colptrS5523);
      moonbit_decref_cycle_free(_M0L6rowptrS5524);
      moonbit_decref_cycle_free(_M0L4varsS5525);
      moonbit_decref_cycle_free(_M0L5paramS5526);
      _M0L6t__nowS5534 = _M0L1eS1925->$5;
      _M0L6t__nowS5537 = _M0L1eS1925->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5534);
      _M0L6_2acntS5804 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1925));
      if (_M0L6_2acntS5804 > 1) {
        int32_t _M0L11_2anew__cntS5807 = _M0L6_2acntS5804 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1925), _M0L11_2anew__cntS5807);
        moonbit_incref_cycle_free(_M0L6t__nowS5537);
      } else if (_M0L6_2acntS5804 == 1) {
        struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L8_2afieldS5806 =
          _M0L1eS1925->$4;
        struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L8_2afieldS5805;
        moonbit_decref_cycle_free(_M0L8_2afieldS5806);
        _M0L8_2afieldS5805 = _M0L1eS1925->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5805);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1925);
      }
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5536 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5537, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5537);
      _M0L6_2atmpS5535 = _M0L6_2atmpS5536 + _M0L2dtS1875;
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5534, 0, _M0L6_2atmpS5535);
      moonbit_decref_cycle_free(_M0L6t__nowS5534);
      joinlet_5924:;
      goto joinlet_5923;
      join_1921:;
      _M0L5connsS5518 = _M0L1mS1870->$1;
      _M0L11conn__indexS5519 = _M0L1eS1922->$0;
      #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1923
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5518, _M0L11conn__indexS5519);
      _M0L6matrixS5513 = _M0L3synS1923->$4;
      _M0L4valsS5498 = _M0L6matrixS5513->$4;
      _M0L3preS5512 = _M0L3synS1923->$0;
      _M0L4fireS5499 = _M0L3preS5512->$5;
      _M0L4postS5511 = _M0L3synS1923->$1;
      _M0L4fireS5500 = _M0L4postS5511->$5;
      _M0L6matrixS5510 = _M0L3synS1923->$4;
      _M0L6colptrS5501 = _M0L6matrixS5510->$3;
      _M0L6matrixS5509 = _M0L3synS1923->$4;
      _M0L6rowptrS5502 = _M0L6matrixS5509->$2;
      _M0L4postS5508 = _M0L3synS1923->$1;
      moonbit_incref_cycle_free(_M0L6rowptrS5502);
      moonbit_incref_cycle_free(_M0L6colptrS5501);
      moonbit_incref_cycle_free(_M0L4fireS5500);
      moonbit_incref_cycle_free(_M0L4fireS5499);
      moonbit_incref_cycle_free(_M0L4valsS5498);
      _M0L6_2acntS5757
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1923));
      if (_M0L6_2acntS5757 > 1) {
        int32_t _M0L11_2anew__cntS5767 = _M0L6_2acntS5757 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1923), _M0L11_2anew__cntS5767);
        moonbit_incref_cycle_free(_M0L4postS5508);
      } else if (_M0L6_2acntS5757 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5766 = _M0L3synS1923->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5765;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5764;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5763;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5762;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L8_2afieldS5761;
        moonbit_string_t _M0L8_2afieldS5760;
        moonbit_string_t _M0L8_2afieldS5759;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5758;
        moonbit_decref_cycle_free(_M0L8_2afieldS5766);
        _M0L8_2afieldS5765 = _M0L3synS1923->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5765);
        _M0L8_2afieldS5764 = _M0L3synS1923->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5764);
        _M0L8_2afieldS5763 = _M0L3synS1923->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5763);
        _M0L8_2afieldS5762 = _M0L3synS1923->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5762);
        _M0L8_2afieldS5761 = _M0L3synS1923->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5761);
        _M0L8_2afieldS5760 = _M0L3synS1923->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5760);
        _M0L8_2afieldS5759 = _M0L3synS1923->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5759);
        _M0L8_2afieldS5758 = _M0L3synS1923->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5758);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1923);
      }
      _M0L1vS5503 = _M0L4postS5508->$3;
      _M0L6_2acntS5768
      = Moonbit_rc_count(Moonbit_object_header(_M0L4postS5508));
      if (_M0L6_2acntS5768 > 1) {
        int32_t _M0L11_2anew__cntS5784 = _M0L6_2acntS5768 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L4postS5508), _M0L11_2anew__cntS5784);
        moonbit_incref_cycle_free(_M0L1vS5503);
      } else if (_M0L6_2acntS5768 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5783 = _M0L4postS5508->$16;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5782;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5781;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5780;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5779;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5778;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5777;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5776;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5775;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5774;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5773;
        struct _M0TPB5ArrayGbE* _M0L8_2afieldS5772;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5771;
        struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS5770;
        struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS5769;
        moonbit_decref_cycle_free(_M0L8_2afieldS5783);
        _M0L8_2afieldS5782 = _M0L4postS5508->$15;
        moonbit_decref_cycle_free(_M0L8_2afieldS5782);
        _M0L8_2afieldS5781 = _M0L4postS5508->$14;
        moonbit_decref_cycle_free(_M0L8_2afieldS5781);
        _M0L8_2afieldS5780 = _M0L4postS5508->$13;
        moonbit_decref_cycle_free(_M0L8_2afieldS5780);
        _M0L8_2afieldS5779 = _M0L4postS5508->$12;
        moonbit_decref_cycle_free(_M0L8_2afieldS5779);
        _M0L8_2afieldS5778 = _M0L4postS5508->$11;
        moonbit_decref_cycle_free(_M0L8_2afieldS5778);
        _M0L8_2afieldS5777 = _M0L4postS5508->$10;
        moonbit_decref_cycle_free(_M0L8_2afieldS5777);
        _M0L8_2afieldS5776 = _M0L4postS5508->$9;
        moonbit_decref_cycle_free(_M0L8_2afieldS5776);
        _M0L8_2afieldS5775 = _M0L4postS5508->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5775);
        _M0L8_2afieldS5774 = _M0L4postS5508->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5774);
        _M0L8_2afieldS5773 = _M0L4postS5508->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5773);
        _M0L8_2afieldS5772 = _M0L4postS5508->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5772);
        _M0L8_2afieldS5771 = _M0L4postS5508->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5771);
        _M0L8_2afieldS5770 = _M0L4postS5508->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5770);
        _M0L8_2afieldS5769 = _M0L4postS5508->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5769);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L4postS5508);
      }
      _M0L4varsS5504 = _M0L1eS1922->$4;
      _M0L5paramS5505 = _M0L1eS1922->$3;
      _M0L6t__nowS5507 = _M0L1eS1922->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5507);
      moonbit_incref_cycle_free(_M0L5paramS5505);
      moonbit_incref_cycle_free(_M0L4varsS5504);
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5506 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5507, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5507);
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS5498, _M0L4fireS5499, _M0L4fireS5500, _M0L6colptrS5501, _M0L6rowptrS5502, _M0L1vS5503, _M0L4varsS5504, _M0L5paramS5505, _M0L6_2atmpS5506, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L4valsS5498);
      moonbit_decref_cycle_free(_M0L4fireS5499);
      moonbit_decref_cycle_free(_M0L4fireS5500);
      moonbit_decref_cycle_free(_M0L6colptrS5501);
      moonbit_decref_cycle_free(_M0L6rowptrS5502);
      moonbit_decref_cycle_free(_M0L1vS5503);
      moonbit_decref_cycle_free(_M0L4varsS5504);
      moonbit_decref_cycle_free(_M0L5paramS5505);
      _M0L6t__nowS5514 = _M0L1eS1922->$5;
      _M0L6t__nowS5517 = _M0L1eS1922->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5514);
      _M0L6_2acntS5785 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1922));
      if (_M0L6_2acntS5785 > 1) {
        int32_t _M0L11_2anew__cntS5788 = _M0L6_2acntS5785 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1922), _M0L11_2anew__cntS5788);
        moonbit_incref_cycle_free(_M0L6t__nowS5517);
      } else if (_M0L6_2acntS5785 == 1) {
        struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L8_2afieldS5787 =
          _M0L1eS1922->$4;
        struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L8_2afieldS5786;
        moonbit_decref_cycle_free(_M0L8_2afieldS5787);
        _M0L8_2afieldS5786 = _M0L1eS1922->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5786);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1922);
      }
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5516 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5517, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5517);
      _M0L6_2atmpS5515 = _M0L6_2atmpS5516 + _M0L2dtS1875;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5514, 0, _M0L6_2atmpS5515);
      moonbit_decref_cycle_free(_M0L6t__nowS5514);
      joinlet_5923:;
      goto joinlet_5922;
      join_1918:;
      _M0L5connsS5496 = _M0L1mS1870->$1;
      _M0L11conn__indexS5497 = _M0L1eS1919->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1920
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5496, _M0L11conn__indexS5497);
      _M0L6matrixS5491 = _M0L3synS1920->$4;
      _M0L4valsS5478 = _M0L6matrixS5491->$4;
      _M0L3preS5490 = _M0L3synS1920->$0;
      _M0L4fireS5479 = _M0L3preS5490->$5;
      _M0L4postS5489 = _M0L3synS1920->$1;
      _M0L4fireS5480 = _M0L4postS5489->$5;
      _M0L6matrixS5488 = _M0L3synS1920->$4;
      _M0L6colptrS5481 = _M0L6matrixS5488->$3;
      _M0L6matrixS5487 = _M0L3synS1920->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5481);
      moonbit_incref_cycle_free(_M0L4fireS5480);
      moonbit_incref_cycle_free(_M0L4fireS5479);
      moonbit_incref_cycle_free(_M0L4valsS5478);
      _M0L6_2acntS5738
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1920));
      if (_M0L6_2acntS5738 > 1) {
        int32_t _M0L11_2anew__cntS5748 = _M0L6_2acntS5738 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1920), _M0L11_2anew__cntS5748);
        moonbit_incref_cycle_free(_M0L6matrixS5487);
      } else if (_M0L6_2acntS5738 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5747 = _M0L3synS1920->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5746;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5745;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5744;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5743;
        moonbit_string_t _M0L8_2afieldS5742;
        moonbit_string_t _M0L8_2afieldS5741;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5740;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5739;
        moonbit_decref_cycle_free(_M0L8_2afieldS5747);
        _M0L8_2afieldS5746 = _M0L3synS1920->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5746);
        _M0L8_2afieldS5745 = _M0L3synS1920->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5745);
        _M0L8_2afieldS5744 = _M0L3synS1920->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5744);
        _M0L8_2afieldS5743 = _M0L3synS1920->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5743);
        _M0L8_2afieldS5742 = _M0L3synS1920->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5742);
        _M0L8_2afieldS5741 = _M0L3synS1920->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5741);
        _M0L8_2afieldS5740 = _M0L3synS1920->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5740);
        _M0L8_2afieldS5739 = _M0L3synS1920->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5739);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1920);
      }
      _M0L6rowptrS5482 = _M0L6matrixS5487->$2;
      _M0L6_2acntS5749
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5487));
      if (_M0L6_2acntS5749 > 1) {
        int32_t _M0L11_2anew__cntS5752 = _M0L6_2acntS5749 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5487), _M0L11_2anew__cntS5752);
        moonbit_incref_cycle_free(_M0L6rowptrS5482);
      } else if (_M0L6_2acntS5749 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5751 = _M0L6matrixS5487->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5750;
        moonbit_decref_cycle_free(_M0L8_2afieldS5751);
        _M0L8_2afieldS5750 = _M0L6matrixS5487->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5750);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5487);
      }
      _M0L4varsS5483 = _M0L1eS1919->$4;
      _M0L5paramS5484 = _M0L1eS1919->$3;
      _M0L6t__nowS5486 = _M0L1eS1919->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5486);
      moonbit_incref_cycle_free(_M0L5paramS5484);
      moonbit_incref_cycle_free(_M0L4varsS5483);
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5485 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5486, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5486);
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt21stdp__symmetric__step(_M0L4valsS5478, _M0L4fireS5479, _M0L4fireS5480, _M0L6colptrS5481, _M0L6rowptrS5482, _M0L4varsS5483, _M0L5paramS5484, _M0L6_2atmpS5485, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L4valsS5478);
      moonbit_decref_cycle_free(_M0L4fireS5479);
      moonbit_decref_cycle_free(_M0L4fireS5480);
      moonbit_decref_cycle_free(_M0L6colptrS5481);
      moonbit_decref_cycle_free(_M0L6rowptrS5482);
      moonbit_decref_cycle_free(_M0L4varsS5483);
      moonbit_decref_cycle_free(_M0L5paramS5484);
      _M0L6t__nowS5492 = _M0L1eS1919->$5;
      _M0L6t__nowS5495 = _M0L1eS1919->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5492);
      _M0L6_2acntS5753 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1919));
      if (_M0L6_2acntS5753 > 1) {
        int32_t _M0L11_2anew__cntS5756 = _M0L6_2acntS5753 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1919), _M0L11_2anew__cntS5756);
        moonbit_incref_cycle_free(_M0L6t__nowS5495);
      } else if (_M0L6_2acntS5753 == 1) {
        struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L8_2afieldS5755 =
          _M0L1eS1919->$4;
        struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L8_2afieldS5754;
        moonbit_decref_cycle_free(_M0L8_2afieldS5755);
        _M0L8_2afieldS5754 = _M0L1eS1919->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5754);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1919);
      }
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5494 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5495, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5495);
      _M0L6_2atmpS5493 = _M0L6_2atmpS5494 + _M0L2dtS1875;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5492, 0, _M0L6_2atmpS5493);
      moonbit_decref_cycle_free(_M0L6t__nowS5492);
      joinlet_5922:;
      goto joinlet_5921;
      join_1915:;
      _M0L5connsS5476 = _M0L1mS1870->$1;
      _M0L11conn__indexS5477 = _M0L1eS1916->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1917
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5476, _M0L11conn__indexS5477);
      _M0L6matrixS5471 = _M0L3synS1917->$4;
      _M0L4valsS5458 = _M0L6matrixS5471->$4;
      _M0L3preS5470 = _M0L3synS1917->$0;
      _M0L4fireS5459 = _M0L3preS5470->$5;
      _M0L4postS5469 = _M0L3synS1917->$1;
      _M0L4fireS5460 = _M0L4postS5469->$5;
      _M0L6matrixS5468 = _M0L3synS1917->$4;
      _M0L6colptrS5461 = _M0L6matrixS5468->$3;
      _M0L6matrixS5467 = _M0L3synS1917->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5461);
      moonbit_incref_cycle_free(_M0L4fireS5460);
      moonbit_incref_cycle_free(_M0L4fireS5459);
      moonbit_incref_cycle_free(_M0L4valsS5458);
      _M0L6_2acntS5719
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1917));
      if (_M0L6_2acntS5719 > 1) {
        int32_t _M0L11_2anew__cntS5729 = _M0L6_2acntS5719 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1917), _M0L11_2anew__cntS5729);
        moonbit_incref_cycle_free(_M0L6matrixS5467);
      } else if (_M0L6_2acntS5719 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5728 = _M0L3synS1917->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5727;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5726;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5725;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5724;
        moonbit_string_t _M0L8_2afieldS5723;
        moonbit_string_t _M0L8_2afieldS5722;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5721;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5720;
        moonbit_decref_cycle_free(_M0L8_2afieldS5728);
        _M0L8_2afieldS5727 = _M0L3synS1917->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5727);
        _M0L8_2afieldS5726 = _M0L3synS1917->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5726);
        _M0L8_2afieldS5725 = _M0L3synS1917->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5725);
        _M0L8_2afieldS5724 = _M0L3synS1917->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5724);
        _M0L8_2afieldS5723 = _M0L3synS1917->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5723);
        _M0L8_2afieldS5722 = _M0L3synS1917->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5722);
        _M0L8_2afieldS5721 = _M0L3synS1917->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5721);
        _M0L8_2afieldS5720 = _M0L3synS1917->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5720);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1917);
      }
      _M0L6rowptrS5462 = _M0L6matrixS5467->$2;
      _M0L6_2acntS5730
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5467));
      if (_M0L6_2acntS5730 > 1) {
        int32_t _M0L11_2anew__cntS5733 = _M0L6_2acntS5730 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5467), _M0L11_2anew__cntS5733);
        moonbit_incref_cycle_free(_M0L6rowptrS5462);
      } else if (_M0L6_2acntS5730 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5732 = _M0L6matrixS5467->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5731;
        moonbit_decref_cycle_free(_M0L8_2afieldS5732);
        _M0L8_2afieldS5731 = _M0L6matrixS5467->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5731);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5467);
      }
      _M0L4varsS5463 = _M0L1eS1916->$4;
      _M0L5paramS5464 = _M0L1eS1916->$3;
      _M0L6t__nowS5466 = _M0L1eS1916->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5466);
      moonbit_incref_cycle_free(_M0L5paramS5464);
      moonbit_incref_cycle_free(_M0L4varsS5463);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5465 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5466, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5466);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt20ca__plasticity__step(_M0L4valsS5458, _M0L4fireS5459, _M0L4fireS5460, _M0L6colptrS5461, _M0L6rowptrS5462, _M0L4varsS5463, _M0L5paramS5464, _M0L6_2atmpS5465, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L4valsS5458);
      moonbit_decref_cycle_free(_M0L4fireS5459);
      moonbit_decref_cycle_free(_M0L4fireS5460);
      moonbit_decref_cycle_free(_M0L6colptrS5461);
      moonbit_decref_cycle_free(_M0L6rowptrS5462);
      moonbit_decref_cycle_free(_M0L4varsS5463);
      moonbit_decref_cycle_free(_M0L5paramS5464);
      _M0L6t__nowS5472 = _M0L1eS1916->$5;
      _M0L6t__nowS5475 = _M0L1eS1916->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5472);
      _M0L6_2acntS5734 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1916));
      if (_M0L6_2acntS5734 > 1) {
        int32_t _M0L11_2anew__cntS5737 = _M0L6_2acntS5734 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1916), _M0L11_2anew__cntS5737);
        moonbit_incref_cycle_free(_M0L6t__nowS5475);
      } else if (_M0L6_2acntS5734 == 1) {
        struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L8_2afieldS5736 =
          _M0L1eS1916->$4;
        struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L8_2afieldS5735;
        moonbit_decref_cycle_free(_M0L8_2afieldS5736);
        _M0L8_2afieldS5735 = _M0L1eS1916->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5735);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1916);
      }
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5474 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5475, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5475);
      _M0L6_2atmpS5473 = _M0L6_2atmpS5474 + _M0L2dtS1875;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5472, 0, _M0L6_2atmpS5473);
      moonbit_decref_cycle_free(_M0L6t__nowS5472);
      joinlet_5921:;
      _M0L6_2atmpS5617 = _M0L2__S1913 + 1;
      _M0L2__S1913 = _M0L6_2atmpS5617;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1912);
    }
    break;
  }
  _M0L7_2abindS1956 = _M0L1mS1870->$0;
  _M0L7_2abindS1957 = _M0L7_2abindS1956->$1;
  _M0L7_2abindS1958 = _M0L7_2abindS1956->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1958);
  _M0L2__S1959 = 0;
  while (1) {
    if (_M0L2__S1959 < _M0L7_2abindS1957) {
      void* _M0L1pS1960 = (void*)_M0L7_2abindS1958[_M0L2__S1959];
      int32_t _M0L6_2atmpS5618;
      moonbit_incref_cycle_free(_M0L1pS1960);
      #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14integrate__any(_M0L1pS1960, _M0L2dtS1875);
      moonbit_decref_cycle_free(_M0L1pS1960);
      _M0L6_2atmpS5618 = _M0L2__S1959 + 1;
      _M0L2__S1959 = _M0L6_2atmpS5618;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1958);
    }
    break;
  }
  _M0L7_2abindS1962 = _M0L1mS1870->$4;
  _M0L7_2abindS1963 = _M0L7_2abindS1962->$1;
  _M0L7_2abindS1964 = _M0L7_2abindS1962->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1964);
  _M0L2__S1965 = 0;
  while (1) {
    if (_M0L2__S1965 < _M0L7_2abindS1963) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L3monS1966 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1964[
          _M0L2__S1965
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5620 = _M0L1mS1870->$3;
      float _M0L6_2atmpS5619;
      int32_t _M0L6_2atmpS5621;
      moonbit_incref_cycle_free(_M0L3monS1966);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5619 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5620);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L3monS1966, _M0L6_2atmpS5619);
      moonbit_decref_cycle_free(_M0L3monS1966);
      _M0L6_2atmpS5621 = _M0L2__S1965 + 1;
      _M0L2__S1965 = _M0L6_2atmpS5621;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1964);
    }
    break;
  }
  _M0L4timeS5622 = _M0L1mS1870->$3;
  #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS5622, _M0L2dtS1875);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1867,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1868,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L11stims_2eoptS1856,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L14monitors_2eoptS1859,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L10stdp_2eoptS1862,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L9stp_2eoptS1865
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1855;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1858;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1861;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1864;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _result_5931;
  if (_M0L11stims_2eoptS1856 == 0) {
    void** _M0L6_2atmpS5430 = (void**)moonbit_empty_ref_array;
    _M0L5stimsS1855
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
    Moonbit_object_header(_M0L5stimsS1855)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _M0L5stimsS1855->$0 = _M0L6_2atmpS5430;
    _M0L5stimsS1855->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2aSomeS1857 =
      _M0L11stims_2eoptS1856;
    if (_M0L7_2aSomeS1857) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1857);
    }
    _M0L5stimsS1855 = _M0L7_2aSomeS1857;
  }
  if (_M0L14monitors_2eoptS1859 == 0) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS5429 =
      (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_empty_ref_array;
    _M0L8monitorsS1858
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
    Moonbit_object_header(_M0L8monitorsS1858)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _M0L8monitorsS1858->$0 = _M0L6_2atmpS5429;
    _M0L8monitorsS1858->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2aSomeS1860 =
      _M0L14monitors_2eoptS1859;
    if (_M0L7_2aSomeS1860) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1860);
    }
    _M0L8monitorsS1858 = _M0L7_2aSomeS1860;
  }
  if (_M0L10stdp_2eoptS1862 == 0) {
    void** _M0L6_2atmpS5428 = (void**)moonbit_empty_ref_array;
    _M0L4stdpS1861
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
    Moonbit_object_header(_M0L4stdpS1861)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
    _M0L4stdpS1861->$0 = _M0L6_2atmpS5428;
    _M0L4stdpS1861->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2aSomeS1863 =
      _M0L10stdp_2eoptS1862;
    if (_M0L7_2aSomeS1863) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1863);
    }
    _M0L4stdpS1861 = _M0L7_2aSomeS1863;
  }
  if (_M0L9stp_2eoptS1865 == 0) {
    void** _M0L6_2atmpS5427 = (void**)moonbit_empty_ref_array;
    _M0L3stpS1864
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE));
    Moonbit_object_header(_M0L3stpS1864)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
    _M0L3stpS1864->$0 = _M0L6_2atmpS5427;
    _M0L3stpS1864->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2aSomeS1866 =
      _M0L9stp_2eoptS1865;
    if (_M0L7_2aSomeS1866) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1866);
    }
    _M0L3stpS1864 = _M0L7_2aSomeS1866;
  }
  _result_5931
  = _M0FP26RiantR8snn__mbt15compose_2einner(_M0L4popsS1867, _M0L5connsS1868, _M0L5stimsS1855, _M0L8monitorsS1858, _M0L4stdpS1861, _M0L3stpS1864);
  moonbit_decref_cycle_free(_M0L5stimsS1855);
  moonbit_decref_cycle_free(_M0L8monitorsS1858);
  moonbit_decref_cycle_free(_M0L4stdpS1861);
  moonbit_decref_cycle_free(_M0L3stpS1864);
  return _result_5931;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1849,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1850,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1851,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1852,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1853,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1854
) {
  struct _M0TP26RiantR8snn__mbt4Time* _M0L6_2atmpS5426;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _block_5932;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5426 = _M0MP26RiantR8snn__mbt4Time3new();
  moonbit_incref_cycle_free(_M0L4popsS1849);
  moonbit_incref_cycle_free(_M0L5connsS1850);
  moonbit_incref_cycle_free(_M0L5stimsS1851);
  moonbit_incref_cycle_free(_M0L8monitorsS1852);
  moonbit_incref_cycle_free(_M0L4stdpS1853);
  moonbit_incref_cycle_free(_M0L3stpS1854);
  _block_5932
  = (struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel));
  Moonbit_object_header(_block_5932)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_5932->$0 = _M0L4popsS1849;
  _block_5932->$1 = _M0L5connsS1850;
  _block_5932->$2 = _M0L5stimsS1851;
  _block_5932->$3 = _M0L6_2atmpS5426;
  _block_5932->$4 = _M0L8monitorsS1852;
  _block_5932->$5 = _M0L4stdpS1853;
  _block_5932->$6 = _M0L3stpS1854;
  return _block_5932;
}

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void* _M0L1sS1835,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS1823,
  float _M0L2dtS1830
) {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1xS1821;
  float _M0L1wS1822;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1xS1825;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1xS1827;
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1xS1829;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1xS1832;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1xS1834;
  float _M0L6_2atmpS5425;
  float _M0L6_2atmpS5424;
  float _M0L6_2atmpS5423;
  float _M0L6_2atmpS5422;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  switch (Moonbit_object_tag(_M0L1sS1835)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__* _M0L14_2aPoissonIF__S1836 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L1sS1835;
      struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L4_2axS1837 =
        _M0L14_2aPoissonIF__S1836->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1837);
      _M0L1xS1834 = _M0L4_2axS1837;
      goto join_1833;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__* _M0L17_2aPoissonLayer__S1838 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__*)_M0L1sS1835;
      struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L4_2axS1839 =
        _M0L17_2aPoissonLayer__S1838->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1839);
      _M0L1xS1832 = _M0L4_2axS1839;
      goto join_1831;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__* _M0L15_2aBalancedIF__S1840 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__*)_M0L1sS1835;
      struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L4_2axS1841 =
        _M0L15_2aBalancedIF__S1840->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1841);
      _M0L1xS1829 = _M0L4_2axS1841;
      goto join_1828;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__* _M0L14_2aCurrentIF__S1842 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__*)_M0L1sS1835;
      struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L4_2axS1843 =
        _M0L14_2aCurrentIF__S1842->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1843);
      _M0L1xS1827 = _M0L4_2axS1843;
      goto join_1826;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__* _M0L15_2aCurrentArr__S1844 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__*)_M0L1sS1835;
      struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L4_2axS1845 =
        _M0L15_2aCurrentArr__S1844->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1845);
      _M0L1xS1825 = _M0L4_2axS1845;
      goto join_1824;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__* _M0L14_2aTimedStim__S1846 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__*)_M0L1sS1835;
      struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L4_2axS1847 =
        _M0L14_2aTimedStim__S1846->$0;
      float _M0L4_2awS1848 = _M0L14_2aTimedStim__S1846->$1;
      moonbit_incref_cycle_free(_M0L4_2axS1847);
      _M0L1xS1821 = _M0L4_2axS1847;
      _M0L1wS1822 = _M0L4_2awS1848;
      goto join_1820;
      break;
    }
  }
  goto joinlet_5938;
  join_1833:;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5425 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1823);
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt13stimulate__if(_M0L1xS1834, _M0L6_2atmpS5425, _M0L2dtS1830);
  moonbit_decref_cycle_free(_M0L1xS1834);
  joinlet_5938:;
  goto joinlet_5937;
  join_1831:;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5424 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1823);
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt16stimulate__layer(_M0L1xS1832, _M0L6_2atmpS5424, _M0L2dtS1830);
  moonbit_decref_cycle_free(_M0L1xS1832);
  joinlet_5937:;
  goto joinlet_5936;
  join_1828:;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5423 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1823);
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt19stimulate__balanced(_M0L1xS1829, _M0L6_2atmpS5423, _M0L2dtS1830);
  moonbit_decref_cycle_free(_M0L1xS1829);
  joinlet_5936:;
  goto joinlet_5935;
  join_1826:;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt22stimulate__current__if(_M0L1xS1827);
  moonbit_decref_cycle_free(_M0L1xS1827);
  joinlet_5935:;
  goto joinlet_5934;
  join_1824:;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt25stimulate__current__array(_M0L1xS1825);
  moonbit_decref_cycle_free(_M0L1xS1825);
  joinlet_5934:;
  goto joinlet_5933;
  join_1820:;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5422 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1823);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L1xS1821, _M0L6_2atmpS5422, _M0L1wS1822);
  moonbit_decref_cycle_free(_M0L1xS1821);
  joinlet_5933:;
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1813,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1814,
  moonbit_string_t _M0L3symS1819,
  float _M0L2muS1815,
  float _M0L5sigmaS1816,
  float _M0L1pS1817,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1818
) {
  int32_t _M0L1nS5420;
  int32_t _M0L1nS5421;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1812;
  float* _M0L6_2atmpS5419;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5410;
  float* _M0L6_2atmpS5418;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5411;
  float* _M0L6_2atmpS5417;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5412;
  int32_t* _M0L6_2atmpS5416;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS5413;
  float* _M0L6_2atmpS5415;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5414;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_5939;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS5420 = _M0L3preS1813->$2;
  _M0L1nS5421 = _M0L4postS1814->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1812
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS5420, _M0L1nS5421, _M0L2muS1815, _M0L5sigmaS1816, _M0L1pS1817, _M0L3rngS1818);
  _M0L6_2atmpS5419 = moonbit_empty_float_array;
  _M0L6_2atmpS5410
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5410)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5410->$0 = _M0L6_2atmpS5419;
  _M0L6_2atmpS5410->$1 = 0;
  _M0L6_2atmpS5418 = moonbit_empty_float_array;
  _M0L6_2atmpS5411
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5411)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5411->$0 = _M0L6_2atmpS5418;
  _M0L6_2atmpS5411->$1 = 0;
  _M0L6_2atmpS5417 = moonbit_empty_float_array;
  _M0L6_2atmpS5412
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5412)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5412->$0 = _M0L6_2atmpS5417;
  _M0L6_2atmpS5412->$1 = 0;
  _M0L6_2atmpS5416 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS5413
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS5413)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS5413->$0 = _M0L6_2atmpS5416;
  _M0L6_2atmpS5413->$1 = 0;
  _M0L6_2atmpS5415 = moonbit_empty_float_array;
  _M0L6_2atmpS5414
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5414)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5414->$0 = _M0L6_2atmpS5415;
  _M0L6_2atmpS5414->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1813);
  moonbit_incref_cycle_free(_M0L4postS1814);
  moonbit_incref_cycle_free(_M0L3symS1819);
  _block_5939
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_5939)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _block_5939->$0 = _M0L3preS1813;
  _block_5939->$1 = _M0L4postS1814;
  _block_5939->$2 = _M0L3symS1819;
  _block_5939->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_5939->$4 = _M0L6matrixS1812;
  _block_5939->$5 = _M0L6_2atmpS5410;
  _block_5939->$6 = _M0L6_2atmpS5411;
  _block_5939->$7 = _M0L6_2atmpS5412;
  _block_5939->$8 = _M0L6_2atmpS5413;
  _block_5939->$9 = _M0L6_2atmpS5414;
  return _block_5939;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1808,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1785,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1787,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1804,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1798,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1795,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1791,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1789,
  float _M0L6t__nowS1783,
  float _M0L2dtS1792
) {
  int32_t _M0L6n__preS1784;
  int32_t _M0L7n__postS1786;
  float _M0L6tau__yS5409;
  float _M0L11inv__tau__yS1788;
  struct _M0TPB8MutLocalGiE* _M0L1jS1790;
  struct _M0TPB8MutLocalGiE* _M0L1iS1794;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1784 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1785);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1786 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1787);
  _M0L6tau__yS5409 = _M0L5paramS1789->$2;
  _M0L11inv__tau__yS1788 = 0x1p+0f / _M0L6tau__yS5409;
  _M0L1jS1790
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1790)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1790->$0 = 0;
  while (1) {
    int32_t _M0L3valS5326 = _M0L1jS1790->$0;
    if (_M0L3valS5326 < _M0L6n__preS1784) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5327 = _M0L4varsS1791->$0;
      int32_t _M0L3valS5328 = _M0L1jS1790->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5337 = _M0L4varsS1791->$0;
      int32_t _M0L3valS5338 = _M0L1jS1790->$0;
      float _M0L6_2atmpS5330;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5335;
      int32_t _M0L3valS5336;
      float _M0L6_2atmpS5334;
      float _M0L6_2atmpS5333;
      float _M0L6_2atmpS5332;
      float _M0L6_2atmpS5331;
      float _M0L6_2atmpS5329;
      int32_t _M0L3valS5339;
      int32_t _M0L3valS5347;
      int32_t _M0L6_2atmpS5346;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5330
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5337, _M0L3valS5338);
      _M0L4tpreS5335 = _M0L4varsS1791->$0;
      _M0L3valS5336 = _M0L1jS1790->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5334
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5335, _M0L3valS5336);
      _M0L6_2atmpS5333 = -_M0L6_2atmpS5334;
      _M0L6_2atmpS5332 = _M0L2dtS1792 * _M0L6_2atmpS5333;
      _M0L6_2atmpS5331 = _M0L6_2atmpS5332 * _M0L11inv__tau__yS1788;
      _M0L6_2atmpS5329 = _M0L6_2atmpS5330 + _M0L6_2atmpS5331;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5327, _M0L3valS5328, _M0L6_2atmpS5329);
      _M0L3valS5339 = _M0L1jS1790->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1785, _M0L3valS5339)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5340 = _M0L4varsS1791->$0;
        int32_t _M0L3valS5341 = _M0L1jS1790->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5344 = _M0L4varsS1791->$0;
        int32_t _M0L3valS5345 = _M0L1jS1790->$0;
        float _M0L6_2atmpS5343;
        float _M0L6_2atmpS5342;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5343
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5344, _M0L3valS5345);
        _M0L6_2atmpS5342 = _M0L6_2atmpS5343 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5340, _M0L3valS5341, _M0L6_2atmpS5342);
      }
      _M0L3valS5347 = _M0L1jS1790->$0;
      _M0L6_2atmpS5346 = _M0L3valS5347 + 1;
      _M0L1jS1790->$0 = _M0L6_2atmpS5346;
      continue;
    }
    break;
  }
  _M0L1iS1794
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1794)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1794->$0 = 0;
  while (1) {
    int32_t _M0L3valS5348 = _M0L1iS1794->$0;
    if (_M0L3valS5348 < _M0L7n__postS1786) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5349 = _M0L4varsS1791->$1;
      int32_t _M0L3valS5350 = _M0L1iS1794->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5362 = _M0L4varsS1791->$1;
      int32_t _M0L3valS5363 = _M0L1iS1794->$0;
      float _M0L6_2atmpS5352;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5360;
      int32_t _M0L3valS5361;
      float _M0L6_2atmpS5357;
      int32_t _M0L3valS5359;
      float _M0L6_2atmpS5358;
      float _M0L6_2atmpS5356;
      float _M0L6_2atmpS5355;
      float _M0L6_2atmpS5354;
      float _M0L6_2atmpS5353;
      float _M0L6_2atmpS5351;
      int32_t _M0L3valS5364;
      int32_t _M0L3valS5372;
      int32_t _M0L6_2atmpS5371;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5352
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5362, _M0L3valS5363);
      _M0L5tpostS5360 = _M0L4varsS1791->$1;
      _M0L3valS5361 = _M0L1iS1794->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5357
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5360, _M0L3valS5361);
      _M0L3valS5359 = _M0L1iS1794->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5358
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1795, _M0L3valS5359);
      _M0L6_2atmpS5356 = _M0L6_2atmpS5357 - _M0L6_2atmpS5358;
      _M0L6_2atmpS5355 = -_M0L6_2atmpS5356;
      _M0L6_2atmpS5354 = _M0L2dtS1792 * _M0L6_2atmpS5355;
      _M0L6_2atmpS5353 = _M0L6_2atmpS5354 * _M0L11inv__tau__yS1788;
      _M0L6_2atmpS5351 = _M0L6_2atmpS5352 + _M0L6_2atmpS5353;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5349, _M0L3valS5350, _M0L6_2atmpS5351);
      _M0L3valS5364 = _M0L1iS1794->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1787, _M0L3valS5364)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5365 = _M0L4varsS1791->$1;
        int32_t _M0L3valS5366 = _M0L1iS1794->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5369 = _M0L4varsS1791->$1;
        int32_t _M0L3valS5370 = _M0L1iS1794->$0;
        float _M0L6_2atmpS5368;
        float _M0L6_2atmpS5367;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5368
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5369, _M0L3valS5370);
        _M0L6_2atmpS5367 = _M0L6_2atmpS5368 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5365, _M0L3valS5366, _M0L6_2atmpS5367);
      }
      _M0L3valS5372 = _M0L1iS1794->$0;
      _M0L6_2atmpS5371 = _M0L3valS5372 + 1;
      _M0L1iS1794->$0 = _M0L6_2atmpS5371;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1794);
    }
    break;
  }
  _M0L1jS1790->$0 = 0;
  while (1) {
    int32_t _M0L3valS5373 = _M0L1jS1790->$0;
    if (_M0L3valS5373 < _M0L6n__preS1784) {
      int32_t _M0L3valS5408 = _M0L1jS1790->$0;
      int32_t _M0L5startS1797;
      int32_t _M0L3valS5407;
      int32_t _M0L6_2atmpS5406;
      int32_t _M0L3endS1799;
      int32_t _M0L3valS5405;
      int32_t _M0L10pre__firedS1800;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5403;
      int32_t _M0L3valS5404;
      float _M0L7tpre__jS1801;
      struct _M0TPB8MutLocalGiE* _M0L1sS1802;
      int32_t _M0L3valS5402;
      int32_t _M0L6_2atmpS5401;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1797
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1798, _M0L3valS5408);
      _M0L3valS5407 = _M0L1jS1790->$0;
      _M0L6_2atmpS5406 = _M0L3valS5407 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1799
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1798, _M0L6_2atmpS5406);
      _M0L3valS5405 = _M0L1jS1790->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1800
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1785, _M0L3valS5405);
      _M0L4tpreS5403 = _M0L4varsS1791->$0;
      _M0L3valS5404 = _M0L1jS1790->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1801
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5403, _M0L3valS5404);
      _M0L1sS1802
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1802)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1802->$0 = _M0L5startS1797;
      while (1) {
        int32_t _M0L3valS5374 = _M0L1sS1802->$0;
        if (_M0L3valS5374 < _M0L3endS1799) {
          int32_t _M0L3valS5400 = _M0L1sS1802->$0;
          int32_t _M0L9post__idxS1803;
          int32_t _M0L11post__firedS1805;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5399;
          float _M0L8tpost__iS1806;
          int32_t _M0L3valS5389;
          float _M0L6_2atmpS5387;
          float _M0L6w__minS5388;
          int32_t _M0L3valS5394;
          float _M0L6_2atmpS5392;
          float _M0L6w__maxS5393;
          int32_t _M0L3valS5398;
          int32_t _M0L6_2atmpS5397;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1803
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1804, _M0L3valS5400);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1805
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1787, _M0L9post__idxS1803);
          _M0L5tpostS5399 = _M0L4varsS1791->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1806
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5399, _M0L9post__idxS1803);
          if (_M0L10pre__firedS1800) {
            float _M0L3etaS5379 = _M0L5paramS1789->$0;
            float _M0L2v0S5381 = _M0L5paramS1789->$1;
            float _M0L6_2atmpS5380 = _M0L8tpost__iS1806 - _M0L2v0S5381;
            float _M0L2dwS1807 = _M0L3etaS5379 * _M0L6_2atmpS5380;
            int32_t _M0L3valS5375 = _M0L1sS1802->$0;
            int32_t _M0L3valS5378 = _M0L1sS1802->$0;
            float _M0L6_2atmpS5377;
            float _M0L6_2atmpS5376;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5377
            = _M0MPC15array5Array2atGfE(_M0L1wS1808, _M0L3valS5378);
            _M0L6_2atmpS5376 = _M0L6_2atmpS5377 + _M0L2dwS1807;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1808, _M0L3valS5375, _M0L6_2atmpS5376);
          }
          if (_M0L11post__firedS1805) {
            float _M0L3etaS5386 = _M0L5paramS1789->$0;
            float _M0L2dwS1809 = _M0L3etaS5386 * _M0L7tpre__jS1801;
            int32_t _M0L3valS5382 = _M0L1sS1802->$0;
            int32_t _M0L3valS5385 = _M0L1sS1802->$0;
            float _M0L6_2atmpS5384;
            float _M0L6_2atmpS5383;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5384
            = _M0MPC15array5Array2atGfE(_M0L1wS1808, _M0L3valS5385);
            _M0L6_2atmpS5383 = _M0L6_2atmpS5384 + _M0L2dwS1809;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1808, _M0L3valS5382, _M0L6_2atmpS5383);
          }
          _M0L3valS5389 = _M0L1sS1802->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5387
          = _M0MPC15array5Array2atGfE(_M0L1wS1808, _M0L3valS5389);
          _M0L6w__minS5388 = _M0L5paramS1789->$4;
          if (_M0L6_2atmpS5387 < _M0L6w__minS5388) {
            int32_t _M0L3valS5390 = _M0L1sS1802->$0;
            float _M0L6w__minS5391 = _M0L5paramS1789->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1808, _M0L3valS5390, _M0L6w__minS5391);
          }
          _M0L3valS5394 = _M0L1sS1802->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5392
          = _M0MPC15array5Array2atGfE(_M0L1wS1808, _M0L3valS5394);
          _M0L6w__maxS5393 = _M0L5paramS1789->$3;
          if (_M0L6_2atmpS5392 > _M0L6w__maxS5393) {
            int32_t _M0L3valS5395 = _M0L1sS1802->$0;
            float _M0L6w__maxS5396 = _M0L5paramS1789->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1808, _M0L3valS5395, _M0L6w__maxS5396);
          }
          _M0L3valS5398 = _M0L1sS1802->$0;
          _M0L6_2atmpS5397 = _M0L3valS5398 + 1;
          _M0L1sS1802->$0 = _M0L6_2atmpS5397;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1802);
        }
        break;
      }
      _M0L3valS5402 = _M0L1jS1790->$0;
      _M0L6_2atmpS5401 = _M0L3valS5402 + 1;
      _M0L1jS1790->$0 = _M0L6_2atmpS5401;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1790);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1779,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1757,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1759,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1775,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1769,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1763,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1761,
  float _M0L6t__nowS1755,
  float _M0L2dtS1764
) {
  int32_t _M0L6n__preS1756;
  int32_t _M0L7n__postS1758;
  float _M0L6tau__yS5325;
  float _M0L11inv__tau__yS1760;
  struct _M0TPB8MutLocalGiE* _M0L1jS1762;
  struct _M0TPB8MutLocalGiE* _M0L1iS1766;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1756 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1757);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1758 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1759);
  _M0L6tau__yS5325 = _M0L5paramS1761->$2;
  _M0L11inv__tau__yS1760 = 0x1p+0f / _M0L6tau__yS5325;
  _M0L1jS1762
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1762)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1762->$0 = 0;
  while (1) {
    int32_t _M0L3valS5242 = _M0L1jS1762->$0;
    if (_M0L3valS5242 < _M0L6n__preS1756) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5243 = _M0L4varsS1763->$0;
      int32_t _M0L3valS5244 = _M0L1jS1762->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5253 = _M0L4varsS1763->$0;
      int32_t _M0L3valS5254 = _M0L1jS1762->$0;
      float _M0L6_2atmpS5246;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5251;
      int32_t _M0L3valS5252;
      float _M0L6_2atmpS5250;
      float _M0L6_2atmpS5249;
      float _M0L6_2atmpS5248;
      float _M0L6_2atmpS5247;
      float _M0L6_2atmpS5245;
      int32_t _M0L3valS5255;
      int32_t _M0L3valS5263;
      int32_t _M0L6_2atmpS5262;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5246
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5253, _M0L3valS5254);
      _M0L4tpreS5251 = _M0L4varsS1763->$0;
      _M0L3valS5252 = _M0L1jS1762->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5250
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5251, _M0L3valS5252);
      _M0L6_2atmpS5249 = -_M0L6_2atmpS5250;
      _M0L6_2atmpS5248 = _M0L2dtS1764 * _M0L6_2atmpS5249;
      _M0L6_2atmpS5247 = _M0L6_2atmpS5248 * _M0L11inv__tau__yS1760;
      _M0L6_2atmpS5245 = _M0L6_2atmpS5246 + _M0L6_2atmpS5247;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5243, _M0L3valS5244, _M0L6_2atmpS5245);
      _M0L3valS5255 = _M0L1jS1762->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1757, _M0L3valS5255)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5256 = _M0L4varsS1763->$0;
        int32_t _M0L3valS5257 = _M0L1jS1762->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5260 = _M0L4varsS1763->$0;
        int32_t _M0L3valS5261 = _M0L1jS1762->$0;
        float _M0L6_2atmpS5259;
        float _M0L6_2atmpS5258;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5259
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5260, _M0L3valS5261);
        _M0L6_2atmpS5258 = _M0L6_2atmpS5259 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5256, _M0L3valS5257, _M0L6_2atmpS5258);
      }
      _M0L3valS5263 = _M0L1jS1762->$0;
      _M0L6_2atmpS5262 = _M0L3valS5263 + 1;
      _M0L1jS1762->$0 = _M0L6_2atmpS5262;
      continue;
    }
    break;
  }
  _M0L1iS1766
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1766)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1766->$0 = 0;
  while (1) {
    int32_t _M0L3valS5264 = _M0L1iS1766->$0;
    if (_M0L3valS5264 < _M0L7n__postS1758) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5265 = _M0L4varsS1763->$1;
      int32_t _M0L3valS5266 = _M0L1iS1766->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5275 = _M0L4varsS1763->$1;
      int32_t _M0L3valS5276 = _M0L1iS1766->$0;
      float _M0L6_2atmpS5268;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5273;
      int32_t _M0L3valS5274;
      float _M0L6_2atmpS5272;
      float _M0L6_2atmpS5271;
      float _M0L6_2atmpS5270;
      float _M0L6_2atmpS5269;
      float _M0L6_2atmpS5267;
      int32_t _M0L3valS5277;
      int32_t _M0L3valS5285;
      int32_t _M0L6_2atmpS5284;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5268
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5275, _M0L3valS5276);
      _M0L5tpostS5273 = _M0L4varsS1763->$1;
      _M0L3valS5274 = _M0L1iS1766->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5272
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5273, _M0L3valS5274);
      _M0L6_2atmpS5271 = -_M0L6_2atmpS5272;
      _M0L6_2atmpS5270 = _M0L2dtS1764 * _M0L6_2atmpS5271;
      _M0L6_2atmpS5269 = _M0L6_2atmpS5270 * _M0L11inv__tau__yS1760;
      _M0L6_2atmpS5267 = _M0L6_2atmpS5268 + _M0L6_2atmpS5269;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5265, _M0L3valS5266, _M0L6_2atmpS5267);
      _M0L3valS5277 = _M0L1iS1766->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1759, _M0L3valS5277)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5278 = _M0L4varsS1763->$1;
        int32_t _M0L3valS5279 = _M0L1iS1766->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5282 = _M0L4varsS1763->$1;
        int32_t _M0L3valS5283 = _M0L1iS1766->$0;
        float _M0L6_2atmpS5281;
        float _M0L6_2atmpS5280;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5281
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5282, _M0L3valS5283);
        _M0L6_2atmpS5280 = _M0L6_2atmpS5281 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5278, _M0L3valS5279, _M0L6_2atmpS5280);
      }
      _M0L3valS5285 = _M0L1iS1766->$0;
      _M0L6_2atmpS5284 = _M0L3valS5285 + 1;
      _M0L1iS1766->$0 = _M0L6_2atmpS5284;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1766);
    }
    break;
  }
  _M0L1jS1762->$0 = 0;
  while (1) {
    int32_t _M0L3valS5286 = _M0L1jS1762->$0;
    if (_M0L3valS5286 < _M0L6n__preS1756) {
      int32_t _M0L3valS5324 = _M0L1jS1762->$0;
      int32_t _M0L5startS1768;
      int32_t _M0L3valS5323;
      int32_t _M0L6_2atmpS5322;
      int32_t _M0L3endS1770;
      int32_t _M0L3valS5321;
      int32_t _M0L10pre__firedS1771;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5319;
      int32_t _M0L3valS5320;
      float _M0L7tpre__jS1772;
      struct _M0TPB8MutLocalGiE* _M0L1sS1773;
      int32_t _M0L3valS5318;
      int32_t _M0L6_2atmpS5317;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1768
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1769, _M0L3valS5324);
      _M0L3valS5323 = _M0L1jS1762->$0;
      _M0L6_2atmpS5322 = _M0L3valS5323 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1770
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1769, _M0L6_2atmpS5322);
      _M0L3valS5321 = _M0L1jS1762->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1771
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1757, _M0L3valS5321);
      _M0L4tpreS5319 = _M0L4varsS1763->$0;
      _M0L3valS5320 = _M0L1jS1762->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1772
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5319, _M0L3valS5320);
      _M0L1sS1773
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1773)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1773->$0 = _M0L5startS1768;
      while (1) {
        int32_t _M0L3valS5287 = _M0L1sS1773->$0;
        if (_M0L3valS5287 < _M0L3endS1770) {
          int32_t _M0L3valS5316 = _M0L1sS1773->$0;
          int32_t _M0L9post__idxS1774;
          int32_t _M0L11post__firedS1776;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5315;
          float _M0L8tpost__iS1777;
          int32_t _M0L3valS5305;
          float _M0L6_2atmpS5303;
          float _M0L6w__minS5304;
          int32_t _M0L3valS5310;
          float _M0L6_2atmpS5308;
          float _M0L6w__maxS5309;
          int32_t _M0L3valS5314;
          int32_t _M0L6_2atmpS5313;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1774
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1775, _M0L3valS5316);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1776
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1759, _M0L9post__idxS1774);
          _M0L5tpostS5315 = _M0L4varsS1763->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1777
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5315, _M0L9post__idxS1774);
          if (_M0L10pre__firedS1771) {
            float _M0L3etaS5292 = _M0L5paramS1761->$0;
            float _M0L1rS5297 = _M0L5paramS1761->$1;
            float _M0L6_2atmpS5295 = 0x1p+1f * _M0L1rS5297;
            float _M0L6tau__yS5296 = _M0L5paramS1761->$2;
            float _M0L6_2atmpS5294 = _M0L6_2atmpS5295 * _M0L6tau__yS5296;
            float _M0L6_2atmpS5293 = _M0L8tpost__iS1777 - _M0L6_2atmpS5294;
            float _M0L2dwS1778 = _M0L3etaS5292 * _M0L6_2atmpS5293;
            int32_t _M0L3valS5288 = _M0L1sS1773->$0;
            int32_t _M0L3valS5291 = _M0L1sS1773->$0;
            float _M0L6_2atmpS5290;
            float _M0L6_2atmpS5289;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5290
            = _M0MPC15array5Array2atGfE(_M0L1wS1779, _M0L3valS5291);
            _M0L6_2atmpS5289 = _M0L6_2atmpS5290 + _M0L2dwS1778;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1779, _M0L3valS5288, _M0L6_2atmpS5289);
          }
          if (_M0L11post__firedS1776) {
            float _M0L3etaS5302 = _M0L5paramS1761->$0;
            float _M0L2dwS1780 = _M0L3etaS5302 * _M0L7tpre__jS1772;
            int32_t _M0L3valS5298 = _M0L1sS1773->$0;
            int32_t _M0L3valS5301 = _M0L1sS1773->$0;
            float _M0L6_2atmpS5300;
            float _M0L6_2atmpS5299;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5300
            = _M0MPC15array5Array2atGfE(_M0L1wS1779, _M0L3valS5301);
            _M0L6_2atmpS5299 = _M0L6_2atmpS5300 + _M0L2dwS1780;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1779, _M0L3valS5298, _M0L6_2atmpS5299);
          }
          _M0L3valS5305 = _M0L1sS1773->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5303
          = _M0MPC15array5Array2atGfE(_M0L1wS1779, _M0L3valS5305);
          _M0L6w__minS5304 = _M0L5paramS1761->$4;
          if (_M0L6_2atmpS5303 < _M0L6w__minS5304) {
            int32_t _M0L3valS5306 = _M0L1sS1773->$0;
            float _M0L6w__minS5307 = _M0L5paramS1761->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1779, _M0L3valS5306, _M0L6w__minS5307);
          }
          _M0L3valS5310 = _M0L1sS1773->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5308
          = _M0MPC15array5Array2atGfE(_M0L1wS1779, _M0L3valS5310);
          _M0L6w__maxS5309 = _M0L5paramS1761->$3;
          if (_M0L6_2atmpS5308 > _M0L6w__maxS5309) {
            int32_t _M0L3valS5311 = _M0L1sS1773->$0;
            float _M0L6w__maxS5312 = _M0L5paramS1761->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1779, _M0L3valS5311, _M0L6w__maxS5312);
          }
          _M0L3valS5314 = _M0L1sS1773->$0;
          _M0L6_2atmpS5313 = _M0L3valS5314 + 1;
          _M0L1sS1773->$0 = _M0L6_2atmpS5313;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1773);
        }
        break;
      }
      _M0L3valS5318 = _M0L1jS1762->$0;
      _M0L6_2atmpS5317 = _M0L3valS5318 + 1;
      _M0L1jS1762->$0 = _M0L6_2atmpS5317;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1762);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1753;
  float _M0L2glS1754;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_5948;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1753 = -0x1p+0f;
  _M0L2glS1754 = -0x1p+0f;
  _block_5948
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_5948)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5948->$0 = _M0L1cS1753;
  _block_5948->$1 = _M0L2glS1754;
  _block_5948->$2 = 0x1.ep+3f;
  _block_5948->$3 = -0x1.9p+5f;
  _block_5948->$4 = -0x1.ep+5f;
  _block_5948->$5 = -0x1.18p+6f;
  _block_5948->$6 = 0x1.eb851eb851eb8p-5f;
  _block_5948->$7 = 0x1p+1f;
  _block_5948->$8 = 0x0p+0f;
  _block_5948->$9 = 0x0p+0f;
  _block_5948->$10 = 0x0p+0f;
  return _block_5948;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1727,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1729,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1732
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1726;
  float _M0L2vtS5240;
  float _M0L2vrS5241;
  float _M0L6spreadS1728;
  int32_t _M0L7_2abindS1730;
  int32_t _M0L1kS1731;
  struct _M0TPB5ArrayGfE* _M0L1wS1734;
  struct _M0TPB5ArrayGbE* _M0L4fireS1735;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1736;
  struct _M0TPB5ArrayGfE* _M0L1iS1737;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1738;
  struct _M0TPB5ArrayGfE* _M0L2geS1739;
  struct _M0TPB5ArrayGfE* _M0L2giS1740;
  struct _M0TPB5ArrayGfE* _M0L2heS1741;
  struct _M0TPB5ArrayGfE* _M0L2hiS1742;
  struct _M0TPB5ArrayGfE* _M0L3gluS1743;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1744;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1745;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1746;
  float _M0L4e__eS1747;
  float _M0L4e__iS1748;
  float _M0L3treS1749;
  float _M0L3tdeS1750;
  float _M0L3triS1751;
  float _M0L3tdiS1752;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS5239;
  struct _M0TP26RiantR8snn__mbt2IF* _block_5950;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1726 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  _M0L2vtS5240 = _M0L5paramS1729->$3;
  _M0L2vrS5241 = _M0L5paramS1729->$4;
  _M0L6spreadS1728 = _M0L2vtS5240 - _M0L2vrS5241;
  _M0L7_2abindS1730 = 0;
  _M0L1kS1731 = _M0L7_2abindS1730;
  while (1) {
    if (_M0L1kS1731 < _M0L1nS1727) {
      float _M0L2vrS5235 = _M0L5paramS1729->$4;
      float _M0L6_2atmpS5237;
      float _M0L6_2atmpS5236;
      float _M0L6_2atmpS5234;
      int32_t _M0L6_2atmpS5238;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS5237 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1732);
      _M0L6_2atmpS5236 = _M0L6_2atmpS5237 * _M0L6spreadS1728;
      _M0L6_2atmpS5234 = _M0L2vrS5235 + _M0L6_2atmpS5236;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1726, _M0L1kS1731, _M0L6_2atmpS5234);
      _M0L6_2atmpS5238 = _M0L1kS1731 + 1;
      _M0L1kS1731 = _M0L6_2atmpS5238;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1734 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1735 = _M0MPC15array5Array4makeGbE(_M0L1nS1727, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1736 = _M0MPC15array5Array4makeGiE(_M0L1nS1727, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1737 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1738 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1739 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1740 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1741 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1742 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1743 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1744 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1745 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1746 = _M0MPC15array5Array4makeGfE(_M0L1nS1727, 0x1p+0f);
  _M0L4e__eS1747 = 0x0p+0f;
  _M0L4e__iS1748 = -0x1.2cp+6f;
  _M0L3treS1749 = 0x1p+0f;
  _M0L3tdeS1750 = 0x1.8p+2f;
  _M0L3triS1751 = 0x1p-1f;
  _M0L3tdiS1752 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS5239 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1729);
  _block_5950
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_5950)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_5950->$0 = _M0L5paramS1729;
  _block_5950->$1 = _M0L6_2atmpS5239;
  _block_5950->$2 = _M0L1nS1727;
  _block_5950->$3 = _M0L1vS1726;
  _block_5950->$4 = _M0L1wS1734;
  _block_5950->$5 = _M0L4fireS1735;
  _block_5950->$6 = _M0L4tabsS1736;
  _block_5950->$7 = _M0L1iS1737;
  _block_5950->$8 = _M0L9syn__currS1738;
  _block_5950->$9 = _M0L2geS1739;
  _block_5950->$10 = _M0L2giS1740;
  _block_5950->$11 = _M0L2heS1741;
  _block_5950->$12 = _M0L2hiS1742;
  _block_5950->$13 = _M0L3gluS1743;
  _block_5950->$14 = _M0L4gabaS1744;
  _block_5950->$15 = _M0L7gsyn__eS1745;
  _block_5950->$16 = _M0L7gsyn__iS1746;
  _block_5950->$17 = _M0L4e__eS1747;
  _block_5950->$18 = _M0L4e__iS1748;
  _block_5950->$19 = _M0L3treS1749;
  _block_5950->$20 = _M0L3tdeS1750;
  _block_5950->$21 = _M0L3triS1751;
  _block_5950->$22 = _M0L3tdiS1752;
  return _block_5950;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_5951;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_5951
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_5951)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5951->$0 = 0x1p+1f;
  return _block_5951;
}

int32_t _M0FP26RiantR8snn__mbt14integrate__any(
  void* _M0L1pS1707,
  float _M0L2dtS1690
) {
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1xS1689;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1xS1692;
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1xS1694;
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1xS1696;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1xS1698;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1xS1700;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1xS1702;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1xS1704;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1xS1706;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  switch (Moonbit_object_tag(_M0L1pS1707)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__* _M0L7_2aIF__S1708 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4_2axS1709 =
        _M0L7_2aIF__S1708->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1709);
      _M0L1xS1706 = _M0L4_2axS1709;
      goto join_1705;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__* _M0L9_2aAdEx__S1710 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4_2axS1711 =
        _M0L9_2aAdEx__S1710->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1711);
      _M0L1xS1704 = _M0L4_2axS1711;
      goto join_1703;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__* _M0L15_2aAdExSinExp__S1712 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4_2axS1713 =
        _M0L15_2aAdExSinExp__S1712->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1713);
      _M0L1xS1702 = _M0L4_2axS1713;
      goto join_1701;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__* _M0L7_2aIZ__S1714 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt2IZ* _M0L4_2axS1715 =
        _M0L7_2aIZ__S1714->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1715);
      _M0L1xS1700 = _M0L4_2axS1715;
      goto join_1699;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__* _M0L7_2aHH__S1716 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt2HH* _M0L4_2axS1717 =
        _M0L7_2aHH__S1716->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1717);
      _M0L1xS1698 = _M0L4_2axS1717;
      goto join_1697;
      break;
    }
    
    case 5: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__* _M0L7_2aML__S1718 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L4_2axS1719 =
        _M0L7_2aML__S1718->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1719);
      _M0L1xS1696 = _M0L4_2axS1719;
      goto join_1695;
      break;
    }
    
    case 6: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__* _M0L12_2aPoisson__S1720 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt7Poisson* _M0L4_2axS1721 =
        _M0L12_2aPoisson__S1720->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1721);
      _M0L1xS1694 = _M0L4_2axS1721;
      goto join_1693;
      break;
    }
    
    case 7: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__* _M0L7_2aWC__S1722 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4_2axS1723 =
        _M0L7_2aWC__S1722->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1723);
      _M0L1xS1692 = _M0L4_2axS1723;
      goto join_1691;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__* _M0L11_2aHetRec__S1724 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__*)_M0L1pS1707;
      struct _M0TP26RiantR8snn__mbt6HetRec* _M0L4_2axS1725 =
        _M0L11_2aHetRec__S1724->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1725);
      _M0L1xS1689 = _M0L4_2axS1725;
      goto join_1688;
      break;
    }
  }
  goto joinlet_5960;
  join_1705:;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L1xS1706, _M0L2dtS1690);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1xS1706);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L1xS1706, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1706);
  joinlet_5960:;
  goto joinlet_5959;
  join_1703:;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1xS1704, _M0L2dtS1690);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1xS1704);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt10step__adex(_M0L1xS1704, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1704);
  joinlet_5959:;
  goto joinlet_5958;
  join_1701:;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L1xS1702, _M0L2dtS1690);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L1xS1702);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L1xS1702, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1702);
  joinlet_5958:;
  goto joinlet_5957;
  join_1699:;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__iz(_M0L1xS1700, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1700);
  joinlet_5957:;
  goto joinlet_5956;
  join_1697:;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__hh(_M0L1xS1698, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1698);
  joinlet_5956:;
  goto joinlet_5955;
  join_1695:;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__ml(_M0L1xS1696, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1696);
  joinlet_5955:;
  goto joinlet_5954;
  join_1693:;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt13step__poisson(_M0L1xS1694, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1694);
  joinlet_5954:;
  goto joinlet_5953;
  join_1691:;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__wc(_M0L1xS1692, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1692);
  joinlet_5953:;
  goto joinlet_5952;
  join_1688:;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__hetrec(_M0L1xS1689, _M0L2dtS1690);
  moonbit_decref_cycle_free(_M0L1xS1689);
  joinlet_5952:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS1683,
  float _M0L2dtS1686
) {
  int32_t _M0L1nS1682;
  int32_t _M0L7_2abindS1684;
  int32_t _M0L1kS1685;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS1682 = _M0L1pS1683->$1;
  _M0L7_2abindS1684 = 0;
  _M0L1kS1685 = _M0L7_2abindS1684;
  while (1) {
    if (_M0L1kS1685 < _M0L1nS1682) {
      struct _M0TPB5ArrayGfE* _M0L1xS5214 = _M0L1pS1683->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS5227 = _M0L1pS1683->$2;
      float _M0L6_2atmpS5216;
      struct _M0TPB5ArrayGfE* _M0L1xS5226;
      float _M0L6_2atmpS5225;
      float _M0L6_2atmpS5222;
      struct _M0TPB5ArrayGfE* _M0L1gS5224;
      float _M0L6_2atmpS5223;
      float _M0L6_2atmpS5219;
      struct _M0TPB5ArrayGfE* _M0L1iS5221;
      float _M0L6_2atmpS5220;
      float _M0L6_2atmpS5218;
      float _M0L6_2atmpS5217;
      float _M0L6_2atmpS5215;
      struct _M0TPB5ArrayGfE* _M0L1rS5228;
      struct _M0TPB5ArrayGfE* _M0L1xS5231;
      float _M0L6_2atmpS5230;
      float _M0L6_2atmpS5229;
      struct _M0TPB5ArrayGfE* _M0L1gS5232;
      int32_t _M0L6_2atmpS5233;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5216 = _M0MPC15array5Array2atGfE(_M0L1xS5227, _M0L1kS1685);
      _M0L1xS5226 = _M0L1pS1683->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5225 = _M0MPC15array5Array2atGfE(_M0L1xS5226, _M0L1kS1685);
      _M0L6_2atmpS5222 = -_M0L6_2atmpS5225;
      _M0L1gS5224 = _M0L1pS1683->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5223 = _M0MPC15array5Array2atGfE(_M0L1gS5224, _M0L1kS1685);
      _M0L6_2atmpS5219 = _M0L6_2atmpS5222 + _M0L6_2atmpS5223;
      _M0L1iS5221 = _M0L1pS1683->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5220 = _M0MPC15array5Array2atGfE(_M0L1iS5221, _M0L1kS1685);
      _M0L6_2atmpS5218 = _M0L6_2atmpS5219 + _M0L6_2atmpS5220;
      _M0L6_2atmpS5217 = _M0L2dtS1686 * _M0L6_2atmpS5218;
      _M0L6_2atmpS5215 = _M0L6_2atmpS5216 + _M0L6_2atmpS5217;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS5214, _M0L1kS1685, _M0L6_2atmpS5215);
      _M0L1rS5228 = _M0L1pS1683->$3;
      _M0L1xS5231 = _M0L1pS1683->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5230 = _M0MPC15array5Array2atGfE(_M0L1xS5231, _M0L1kS1685);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5229 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5230);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS5228, _M0L1kS1685, _M0L6_2atmpS5229);
      _M0L1gS5232 = _M0L1pS1683->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS5232, _M0L1kS1685, 0x0p+0f);
      _M0L6_2atmpS5233 = _M0L1kS1685 + 1;
      _M0L1kS1685 = _M0L6_2atmpS5233;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS1675,
  float _M0L2dtS1677
) {
  int32_t _M0L1nS1674;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS5213;
  float _M0L4rateS5212;
  float _M0L8rate__dtS1676;
  int32_t _M0L7_2abindS1678;
  int32_t _M0L1iS1679;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS1674 = _M0L1pS1675->$1;
  _M0L5paramS5213 = _M0L1pS1675->$0;
  _M0L4rateS5212 = _M0L5paramS5213->$0;
  _M0L8rate__dtS1676 = _M0L4rateS5212 * _M0L2dtS1677;
  _M0L7_2abindS1678 = 0;
  _M0L1iS1679 = _M0L7_2abindS1678;
  while (1) {
    if (_M0L1iS1679 < _M0L1nS1674) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS5210 = _M0L1pS1675->$4;
      float _M0L1uS1680;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS5207;
      struct _M0TPB5ArrayGbE* _M0L4fireS5208;
      int32_t _M0L6_2atmpS5209;
      int32_t _M0L6_2atmpS5211;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS1680 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS5210);
      _M0L9randcacheS5207 = _M0L1pS1675->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS5207, _M0L1iS1679, _M0L1uS1680);
      _M0L4fireS5208 = _M0L1pS1675->$2;
      _M0L6_2atmpS5209 = _M0L1uS1680 < _M0L8rate__dtS1676;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5208, _M0L1iS1679, _M0L6_2atmpS5209);
      _M0L6_2atmpS5211 = _M0L1iS1679 + 1;
      _M0L1iS1679 = _M0L6_2atmpS5211;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1pS1634,
  float _M0L2dtS1663
) {
  int32_t _M0L1nS1633;
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* _M0L3p__S1635;
  float _M0L2cmS1636;
  float _M0L2elS1637;
  float _M0L2ekS1638;
  float _M0L3ecaS1639;
  float _M0L2glS1640;
  float _M0L2gkS1641;
  float _M0L3gcaS1642;
  float _M0L6tau__eS1643;
  float _M0L6tau__iS1644;
  float _M0L2v1S1645;
  float _M0L2v2S1646;
  float _M0L2v3S1647;
  float _M0L2v4S1648;
  float _M0L3phiS1649;
  float _M0L4e__eS1650;
  float _M0L4e__iS1651;
  int32_t _M0L7_2abindS1652;
  int32_t _M0L1iS1653;
  int32_t _M0L7_2abindS1665;
  int32_t _M0L1iS1666;
  int32_t _M0L7_2abindS1668;
  int32_t _M0L1iS1669;
  int32_t _M0L7_2abindS1671;
  int32_t _M0L1iS1672;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
  _M0L1nS1633 = _M0L1pS1634->$1;
  _M0L3p__S1635 = _M0L1pS1634->$0;
  _M0L2cmS1636 = _M0L3p__S1635->$0;
  _M0L2elS1637 = _M0L3p__S1635->$1;
  _M0L2ekS1638 = _M0L3p__S1635->$2;
  _M0L3ecaS1639 = _M0L3p__S1635->$3;
  _M0L2glS1640 = _M0L3p__S1635->$4;
  _M0L2gkS1641 = _M0L3p__S1635->$5;
  _M0L3gcaS1642 = _M0L3p__S1635->$6;
  _M0L6tau__eS1643 = _M0L3p__S1635->$7;
  _M0L6tau__iS1644 = _M0L3p__S1635->$8;
  _M0L2v1S1645 = _M0L3p__S1635->$9;
  _M0L2v2S1646 = _M0L3p__S1635->$10;
  _M0L2v3S1647 = _M0L3p__S1635->$11;
  _M0L2v4S1648 = _M0L3p__S1635->$12;
  _M0L3phiS1649 = _M0L3p__S1635->$13;
  _M0L4e__eS1650 = _M0L3p__S1635->$14;
  _M0L4e__iS1651 = _M0L3p__S1635->$15;
  _M0L7_2abindS1652 = 0;
  _M0L1iS1653 = _M0L7_2abindS1652;
  while (1) {
    if (_M0L1iS1653 < _M0L1nS1633) {
      struct _M0TPB5ArrayGfE* _M0L1vS5161 = _M0L1pS1634->$2;
      float _M0L1vS1654;
      struct _M0TPB5ArrayGfE* _M0L1wS5160;
      float _M0L1wS1655;
      float _M0L6_2atmpS5159;
      float _M0L6_2atmpS5158;
      float _M0L6_2atmpS5157;
      float _M0L6_2atmpS5156;
      float _M0L5m__ssS1656;
      struct _M0TPB5ArrayGfE* _M0L1iS5155;
      float _M0L6_2atmpS5152;
      float _M0L6_2atmpS5154;
      float _M0L6_2atmpS5153;
      float _M0L6_2atmpS5148;
      float _M0L6_2atmpS5151;
      float _M0L6_2atmpS5150;
      float _M0L6_2atmpS5149;
      float _M0L6_2atmpS5144;
      float _M0L6_2atmpS5147;
      float _M0L6_2atmpS5146;
      float _M0L6_2atmpS5145;
      float _M0L2dvS1657;
      float _M0L6_2atmpS5143;
      float _M0L6_2atmpS5142;
      float _M0L6_2atmpS5141;
      float _M0L6_2atmpS5140;
      float _M0L5n__ssS1658;
      float _M0L6_2atmpS5138;
      float _M0L6_2atmpS5139;
      float _M0L9cosh__argS1659;
      float _M0L6_2atmpS5135;
      float _M0L6_2atmpS5137;
      float _M0L6_2atmpS5136;
      float _M0L6_2atmpS5134;
      float _M0L9cosh__valS1660;
      float _M0L6_2atmpS5132;
      float _M0L3tauS1661;
      float _M0L6_2atmpS5131;
      float _M0L2dwS1662;
      struct _M0TPB5ArrayGfE* _M0L1vS5124;
      float _M0L6_2atmpS5127;
      float _M0L6_2atmpS5126;
      float _M0L6_2atmpS5125;
      struct _M0TPB5ArrayGfE* _M0L1wS5128;
      float _M0L6_2atmpS5130;
      float _M0L6_2atmpS5129;
      int32_t _M0L6_2atmpS5162;
      #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1vS1654 = _M0MPC15array5Array2atGfE(_M0L1vS5161, _M0L1iS1653);
      _M0L1wS5160 = _M0L1pS1634->$3;
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1wS1655 = _M0MPC15array5Array2atGfE(_M0L1wS5160, _M0L1iS1653);
      _M0L6_2atmpS5159 = _M0L1vS1654 - _M0L2v1S1645;
      _M0L6_2atmpS5158 = _M0L6_2atmpS5159 / _M0L2v2S1646;
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5157 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5158);
      _M0L6_2atmpS5156 = 0x1p+0f + _M0L6_2atmpS5157;
      _M0L5m__ssS1656 = 0x1p-1f * _M0L6_2atmpS5156;
      _M0L1iS5155 = _M0L1pS1634->$5;
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5152 = _M0MPC15array5Array2atGfE(_M0L1iS5155, _M0L1iS1653);
      _M0L6_2atmpS5154 = _M0L2elS1637 - _M0L1vS1654;
      _M0L6_2atmpS5153 = _M0L2glS1640 * _M0L6_2atmpS5154;
      _M0L6_2atmpS5148 = _M0L6_2atmpS5152 + _M0L6_2atmpS5153;
      _M0L6_2atmpS5151 = _M0L3ecaS1639 - _M0L1vS1654;
      _M0L6_2atmpS5150 = _M0L3gcaS1642 * _M0L6_2atmpS5151;
      _M0L6_2atmpS5149 = _M0L6_2atmpS5150 * _M0L5m__ssS1656;
      _M0L6_2atmpS5144 = _M0L6_2atmpS5148 + _M0L6_2atmpS5149;
      _M0L6_2atmpS5147 = _M0L2ekS1638 - _M0L1vS1654;
      _M0L6_2atmpS5146 = _M0L2gkS1641 * _M0L6_2atmpS5147;
      _M0L6_2atmpS5145 = _M0L6_2atmpS5146 * _M0L1wS1655;
      _M0L2dvS1657 = _M0L6_2atmpS5144 + _M0L6_2atmpS5145;
      _M0L6_2atmpS5143 = _M0L1vS1654 - _M0L2v3S1647;
      _M0L6_2atmpS5142 = _M0L6_2atmpS5143 / _M0L2v4S1648;
      #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5141 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5142);
      _M0L6_2atmpS5140 = 0x1p+0f + _M0L6_2atmpS5141;
      _M0L5n__ssS1658 = 0x1p-1f * _M0L6_2atmpS5140;
      _M0L6_2atmpS5138 = _M0L1vS1654 - _M0L2v3S1647;
      _M0L6_2atmpS5139 = 0x1p+1f * _M0L2v4S1648;
      _M0L9cosh__argS1659 = _M0L6_2atmpS5138 / _M0L6_2atmpS5139;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5135 = _M0FP26RiantR8snn__mbt4expf(_M0L9cosh__argS1659);
      _M0L6_2atmpS5137 = -_M0L9cosh__argS1659;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5136 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5137);
      _M0L6_2atmpS5134 = _M0L6_2atmpS5135 + _M0L6_2atmpS5136;
      _M0L9cosh__valS1660 = 0x1p-1f * _M0L6_2atmpS5134;
      _M0L6_2atmpS5132 = _M0L3phiS1649 * _M0L9cosh__valS1660;
      if (_M0L6_2atmpS5132 != 0x0p+0f) {
        float _M0L6_2atmpS5133 = _M0L3phiS1649 * _M0L9cosh__valS1660;
        _M0L3tauS1661 = 0x1p+0f / _M0L6_2atmpS5133;
      } else {
        _M0L3tauS1661 = 0x0p+0f;
      }
      _M0L6_2atmpS5131 = _M0L5n__ssS1658 - _M0L1wS1655;
      _M0L2dwS1662 = _M0L6_2atmpS5131 / _M0L3tauS1661;
      _M0L1vS5124 = _M0L1pS1634->$2;
      _M0L6_2atmpS5127 = _M0L2dtS1663 / _M0L2cmS1636;
      _M0L6_2atmpS5126 = _M0L6_2atmpS5127 * _M0L2dvS1657;
      _M0L6_2atmpS5125 = _M0L1vS1654 + _M0L6_2atmpS5126;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5124, _M0L1iS1653, _M0L6_2atmpS5125);
      _M0L1wS5128 = _M0L1pS1634->$3;
      _M0L6_2atmpS5130 = _M0L2dtS1663 * _M0L2dwS1662;
      _M0L6_2atmpS5129 = _M0L1wS1655 + _M0L6_2atmpS5130;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS5128, _M0L1iS1653, _M0L6_2atmpS5129);
      _M0L6_2atmpS5162 = _M0L1iS1653 + 1;
      _M0L1iS1653 = _M0L6_2atmpS5162;
      continue;
    }
    break;
  }
  _M0L7_2abindS1665 = 0;
  _M0L1iS1666 = _M0L7_2abindS1665;
  while (1) {
    if (_M0L1iS1666 < _M0L1nS1633) {
      struct _M0TPB5ArrayGfE* _M0L1vS5163 = _M0L1pS1634->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5181 = _M0L1pS1634->$2;
      float _M0L6_2atmpS5165;
      float _M0L6_2atmpS5167;
      struct _M0TPB5ArrayGfE* _M0L2geS5180;
      float _M0L6_2atmpS5176;
      struct _M0TPB5ArrayGfE* _M0L1vS5179;
      float _M0L6_2atmpS5178;
      float _M0L6_2atmpS5177;
      float _M0L6_2atmpS5169;
      struct _M0TPB5ArrayGfE* _M0L2giS5175;
      float _M0L6_2atmpS5171;
      struct _M0TPB5ArrayGfE* _M0L1vS5174;
      float _M0L6_2atmpS5173;
      float _M0L6_2atmpS5172;
      float _M0L6_2atmpS5170;
      float _M0L6_2atmpS5168;
      float _M0L6_2atmpS5166;
      float _M0L6_2atmpS5164;
      int32_t _M0L6_2atmpS5182;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5165 = _M0MPC15array5Array2atGfE(_M0L1vS5181, _M0L1iS1666);
      _M0L6_2atmpS5167 = _M0L2dtS1663 / _M0L2cmS1636;
      _M0L2geS5180 = _M0L1pS1634->$6;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5176 = _M0MPC15array5Array2atGfE(_M0L2geS5180, _M0L1iS1666);
      _M0L1vS5179 = _M0L1pS1634->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5178 = _M0MPC15array5Array2atGfE(_M0L1vS5179, _M0L1iS1666);
      _M0L6_2atmpS5177 = _M0L4e__eS1650 - _M0L6_2atmpS5178;
      _M0L6_2atmpS5169 = _M0L6_2atmpS5176 * _M0L6_2atmpS5177;
      _M0L2giS5175 = _M0L1pS1634->$7;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5171 = _M0MPC15array5Array2atGfE(_M0L2giS5175, _M0L1iS1666);
      _M0L1vS5174 = _M0L1pS1634->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5173 = _M0MPC15array5Array2atGfE(_M0L1vS5174, _M0L1iS1666);
      _M0L6_2atmpS5172 = _M0L4e__iS1651 - _M0L6_2atmpS5173;
      _M0L6_2atmpS5170 = _M0L6_2atmpS5171 * _M0L6_2atmpS5172;
      _M0L6_2atmpS5168 = _M0L6_2atmpS5169 + _M0L6_2atmpS5170;
      _M0L6_2atmpS5166 = _M0L6_2atmpS5167 * _M0L6_2atmpS5168;
      _M0L6_2atmpS5164 = _M0L6_2atmpS5165 + _M0L6_2atmpS5166;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5163, _M0L1iS1666, _M0L6_2atmpS5164);
      _M0L6_2atmpS5182 = _M0L1iS1666 + 1;
      _M0L1iS1666 = _M0L6_2atmpS5182;
      continue;
    }
    break;
  }
  _M0L7_2abindS1668 = 0;
  _M0L1iS1669 = _M0L7_2abindS1668;
  while (1) {
    if (_M0L1iS1669 < _M0L1nS1633) {
      struct _M0TPB5ArrayGfE* _M0L2geS5183 = _M0L1pS1634->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS5191 = _M0L1pS1634->$6;
      float _M0L6_2atmpS5185;
      struct _M0TPB5ArrayGfE* _M0L2geS5190;
      float _M0L6_2atmpS5189;
      float _M0L6_2atmpS5188;
      float _M0L6_2atmpS5187;
      float _M0L6_2atmpS5186;
      float _M0L6_2atmpS5184;
      struct _M0TPB5ArrayGfE* _M0L2giS5192;
      struct _M0TPB5ArrayGfE* _M0L2giS5200;
      float _M0L6_2atmpS5194;
      struct _M0TPB5ArrayGfE* _M0L2giS5199;
      float _M0L6_2atmpS5198;
      float _M0L6_2atmpS5197;
      float _M0L6_2atmpS5196;
      float _M0L6_2atmpS5195;
      float _M0L6_2atmpS5193;
      int32_t _M0L6_2atmpS5201;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5185 = _M0MPC15array5Array2atGfE(_M0L2geS5191, _M0L1iS1669);
      _M0L2geS5190 = _M0L1pS1634->$6;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5189 = _M0MPC15array5Array2atGfE(_M0L2geS5190, _M0L1iS1669);
      _M0L6_2atmpS5188 = -_M0L6_2atmpS5189;
      _M0L6_2atmpS5187 = _M0L6_2atmpS5188 / _M0L6tau__eS1643;
      _M0L6_2atmpS5186 = _M0L2dtS1663 * _M0L6_2atmpS5187;
      _M0L6_2atmpS5184 = _M0L6_2atmpS5185 + _M0L6_2atmpS5186;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS5183, _M0L1iS1669, _M0L6_2atmpS5184);
      _M0L2giS5192 = _M0L1pS1634->$7;
      _M0L2giS5200 = _M0L1pS1634->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5194 = _M0MPC15array5Array2atGfE(_M0L2giS5200, _M0L1iS1669);
      _M0L2giS5199 = _M0L1pS1634->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5198 = _M0MPC15array5Array2atGfE(_M0L2giS5199, _M0L1iS1669);
      _M0L6_2atmpS5197 = -_M0L6_2atmpS5198;
      _M0L6_2atmpS5196 = _M0L6_2atmpS5197 / _M0L6tau__iS1644;
      _M0L6_2atmpS5195 = _M0L2dtS1663 * _M0L6_2atmpS5196;
      _M0L6_2atmpS5193 = _M0L6_2atmpS5194 + _M0L6_2atmpS5195;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS5192, _M0L1iS1669, _M0L6_2atmpS5193);
      _M0L6_2atmpS5201 = _M0L1iS1669 + 1;
      _M0L1iS1669 = _M0L6_2atmpS5201;
      continue;
    }
    break;
  }
  _M0L7_2abindS1671 = 0;
  _M0L1iS1672 = _M0L7_2abindS1671;
  while (1) {
    if (_M0L1iS1672 < _M0L1nS1633) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5202 = _M0L1pS1634->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5205 = _M0L1pS1634->$2;
      float _M0L6_2atmpS5204;
      int32_t _M0L6_2atmpS5203;
      int32_t _M0L6_2atmpS5206;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5204 = _M0MPC15array5Array2atGfE(_M0L1vS5205, _M0L1iS1672);
      _M0L6_2atmpS5203 = _M0L6_2atmpS5204 > 0x1.4p+4f;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5202, _M0L1iS1672, _M0L6_2atmpS5203);
      _M0L6_2atmpS5206 = _M0L1iS1672 + 1;
      _M0L1iS1672 = _M0L6_2atmpS5206;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS1602,
  float _M0L2dtS1614
) {
  int32_t _M0L1nS1601;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S1603;
  float _M0L1aS1604;
  float _M0L1bS1605;
  float _M0L1cS1606;
  float _M0L1dS1607;
  float _M0L6tau__eS1608;
  float _M0L6tau__iS1609;
  float _M0L4e__eS1610;
  float _M0L4e__iS1611;
  int32_t _M0L7_2abindS1612;
  int32_t _M0L1iS1613;
  int32_t _M0L7_2abindS1616;
  int32_t _M0L1iS1617;
  int32_t _M0L7_2abindS1623;
  int32_t _M0L1iS1624;
  int32_t _M0L7_2abindS1627;
  int32_t _M0L1iS1628;
  int32_t _M0L7_2abindS1630;
  int32_t _M0L1iS1631;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS1601 = _M0L1pS1602->$1;
  _M0L3p__S1603 = _M0L1pS1602->$0;
  _M0L1aS1604 = _M0L3p__S1603->$0;
  _M0L1bS1605 = _M0L3p__S1603->$1;
  _M0L1cS1606 = _M0L3p__S1603->$2;
  _M0L1dS1607 = _M0L3p__S1603->$3;
  _M0L6tau__eS1608 = _M0L3p__S1603->$4;
  _M0L6tau__iS1609 = _M0L3p__S1603->$5;
  _M0L4e__eS1610 = _M0L3p__S1603->$6;
  _M0L4e__iS1611 = _M0L3p__S1603->$7;
  _M0L7_2abindS1612 = 0;
  _M0L1iS1613 = _M0L7_2abindS1612;
  while (1) {
    if (_M0L1iS1613 < _M0L1nS1601) {
      struct _M0TPB5ArrayGfE* _M0L2geS5032 = _M0L1pS1602->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS5040 = _M0L1pS1602->$6;
      float _M0L6_2atmpS5034;
      struct _M0TPB5ArrayGfE* _M0L2geS5039;
      float _M0L6_2atmpS5038;
      float _M0L6_2atmpS5037;
      float _M0L6_2atmpS5036;
      float _M0L6_2atmpS5035;
      float _M0L6_2atmpS5033;
      struct _M0TPB5ArrayGfE* _M0L2giS5041;
      struct _M0TPB5ArrayGfE* _M0L2giS5049;
      float _M0L6_2atmpS5043;
      struct _M0TPB5ArrayGfE* _M0L2giS5048;
      float _M0L6_2atmpS5047;
      float _M0L6_2atmpS5046;
      float _M0L6_2atmpS5045;
      float _M0L6_2atmpS5044;
      float _M0L6_2atmpS5042;
      int32_t _M0L6_2atmpS5050;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5034 = _M0MPC15array5Array2atGfE(_M0L2geS5040, _M0L1iS1613);
      _M0L2geS5039 = _M0L1pS1602->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5038 = _M0MPC15array5Array2atGfE(_M0L2geS5039, _M0L1iS1613);
      _M0L6_2atmpS5037 = -_M0L6_2atmpS5038;
      _M0L6_2atmpS5036 = _M0L2dtS1614 * _M0L6_2atmpS5037;
      _M0L6_2atmpS5035 = _M0L6_2atmpS5036 / _M0L6tau__eS1608;
      _M0L6_2atmpS5033 = _M0L6_2atmpS5034 + _M0L6_2atmpS5035;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS5032, _M0L1iS1613, _M0L6_2atmpS5033);
      _M0L2giS5041 = _M0L1pS1602->$7;
      _M0L2giS5049 = _M0L1pS1602->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5043 = _M0MPC15array5Array2atGfE(_M0L2giS5049, _M0L1iS1613);
      _M0L2giS5048 = _M0L1pS1602->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5047 = _M0MPC15array5Array2atGfE(_M0L2giS5048, _M0L1iS1613);
      _M0L6_2atmpS5046 = -_M0L6_2atmpS5047;
      _M0L6_2atmpS5045 = _M0L2dtS1614 * _M0L6_2atmpS5046;
      _M0L6_2atmpS5044 = _M0L6_2atmpS5045 / _M0L6tau__iS1609;
      _M0L6_2atmpS5042 = _M0L6_2atmpS5043 + _M0L6_2atmpS5044;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS5041, _M0L1iS1613, _M0L6_2atmpS5042);
      _M0L6_2atmpS5050 = _M0L1iS1613 + 1;
      _M0L1iS1613 = _M0L6_2atmpS5050;
      continue;
    }
    break;
  }
  _M0L7_2abindS1616 = 0;
  _M0L1iS1617 = _M0L7_2abindS1616;
  while (1) {
    if (_M0L1iS1617 < _M0L1nS1601) {
      struct _M0TPB5ArrayGfE* _M0L1vS5076 = _M0L1pS1602->$2;
      float _M0L1vS1618;
      struct _M0TPB5ArrayGfE* _M0L1uS5075;
      float _M0L1uS1619;
      struct _M0TPB5ArrayGfE* _M0L1iS5074;
      float _M0L2iiS1620;
      struct _M0TPB5ArrayGfE* _M0L1vS5051;
      float _M0L6_2atmpS5054;
      float _M0L6_2atmpS5061;
      float _M0L6_2atmpS5059;
      float _M0L6_2atmpS5060;
      float _M0L6_2atmpS5058;
      float _M0L6_2atmpS5057;
      float _M0L6_2atmpS5056;
      float _M0L6_2atmpS5055;
      float _M0L6_2atmpS5053;
      float _M0L6_2atmpS5052;
      struct _M0TPB5ArrayGfE* _M0L1vS5073;
      float _M0L2v2S1621;
      struct _M0TPB5ArrayGfE* _M0L1vS5062;
      float _M0L6_2atmpS5065;
      float _M0L6_2atmpS5072;
      float _M0L6_2atmpS5070;
      float _M0L6_2atmpS5071;
      float _M0L6_2atmpS5069;
      float _M0L6_2atmpS5068;
      float _M0L6_2atmpS5067;
      float _M0L6_2atmpS5066;
      float _M0L6_2atmpS5064;
      float _M0L6_2atmpS5063;
      int32_t _M0L6_2atmpS5077;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1618 = _M0MPC15array5Array2atGfE(_M0L1vS5076, _M0L1iS1617);
      _M0L1uS5075 = _M0L1pS1602->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS1619 = _M0MPC15array5Array2atGfE(_M0L1uS5075, _M0L1iS1617);
      _M0L1iS5074 = _M0L1pS1602->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS1620 = _M0MPC15array5Array2atGfE(_M0L1iS5074, _M0L1iS1617);
      _M0L1vS5051 = _M0L1pS1602->$2;
      _M0L6_2atmpS5054 = 0x1p-1f * _M0L2dtS1614;
      _M0L6_2atmpS5061 = 0x1.47ae147ae147bp-5f * _M0L1vS1618;
      _M0L6_2atmpS5059 = _M0L6_2atmpS5061 * _M0L1vS1618;
      _M0L6_2atmpS5060 = 0x1.4p+2f * _M0L1vS1618;
      _M0L6_2atmpS5058 = _M0L6_2atmpS5059 + _M0L6_2atmpS5060;
      _M0L6_2atmpS5057 = _M0L6_2atmpS5058 + 0x1.18p+7f;
      _M0L6_2atmpS5056 = _M0L6_2atmpS5057 - _M0L1uS1619;
      _M0L6_2atmpS5055 = _M0L6_2atmpS5056 + _M0L2iiS1620;
      _M0L6_2atmpS5053 = _M0L6_2atmpS5054 * _M0L6_2atmpS5055;
      _M0L6_2atmpS5052 = _M0L1vS1618 + _M0L6_2atmpS5053;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5051, _M0L1iS1617, _M0L6_2atmpS5052);
      _M0L1vS5073 = _M0L1pS1602->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S1621 = _M0MPC15array5Array2atGfE(_M0L1vS5073, _M0L1iS1617);
      _M0L1vS5062 = _M0L1pS1602->$2;
      _M0L6_2atmpS5065 = 0x1p-1f * _M0L2dtS1614;
      _M0L6_2atmpS5072 = 0x1.47ae147ae147bp-5f * _M0L2v2S1621;
      _M0L6_2atmpS5070 = _M0L6_2atmpS5072 * _M0L2v2S1621;
      _M0L6_2atmpS5071 = 0x1.4p+2f * _M0L2v2S1621;
      _M0L6_2atmpS5069 = _M0L6_2atmpS5070 + _M0L6_2atmpS5071;
      _M0L6_2atmpS5068 = _M0L6_2atmpS5069 + 0x1.18p+7f;
      _M0L6_2atmpS5067 = _M0L6_2atmpS5068 - _M0L1uS1619;
      _M0L6_2atmpS5066 = _M0L6_2atmpS5067 + _M0L2iiS1620;
      _M0L6_2atmpS5064 = _M0L6_2atmpS5065 * _M0L6_2atmpS5066;
      _M0L6_2atmpS5063 = _M0L2v2S1621 + _M0L6_2atmpS5064;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5062, _M0L1iS1617, _M0L6_2atmpS5063);
      _M0L6_2atmpS5077 = _M0L1iS1617 + 1;
      _M0L1iS1617 = _M0L6_2atmpS5077;
      continue;
    }
    break;
  }
  _M0L7_2abindS1623 = 0;
  _M0L1iS1624 = _M0L7_2abindS1623;
  while (1) {
    if (_M0L1iS1624 < _M0L1nS1601) {
      struct _M0TPB5ArrayGfE* _M0L1vS5088 = _M0L1pS1602->$2;
      float _M0L1vS1625;
      struct _M0TPB5ArrayGfE* _M0L1uS5078;
      struct _M0TPB5ArrayGfE* _M0L1uS5087;
      float _M0L6_2atmpS5080;
      float _M0L6_2atmpS5082;
      float _M0L6_2atmpS5084;
      struct _M0TPB5ArrayGfE* _M0L1uS5086;
      float _M0L6_2atmpS5085;
      float _M0L6_2atmpS5083;
      float _M0L6_2atmpS5081;
      float _M0L6_2atmpS5079;
      int32_t _M0L6_2atmpS5089;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1625 = _M0MPC15array5Array2atGfE(_M0L1vS5088, _M0L1iS1624);
      _M0L1uS5078 = _M0L1pS1602->$3;
      _M0L1uS5087 = _M0L1pS1602->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5080 = _M0MPC15array5Array2atGfE(_M0L1uS5087, _M0L1iS1624);
      _M0L6_2atmpS5082 = _M0L2dtS1614 * _M0L1aS1604;
      _M0L6_2atmpS5084 = _M0L1bS1605 * _M0L1vS1625;
      _M0L1uS5086 = _M0L1pS1602->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5085 = _M0MPC15array5Array2atGfE(_M0L1uS5086, _M0L1iS1624);
      _M0L6_2atmpS5083 = _M0L6_2atmpS5084 - _M0L6_2atmpS5085;
      _M0L6_2atmpS5081 = _M0L6_2atmpS5082 * _M0L6_2atmpS5083;
      _M0L6_2atmpS5079 = _M0L6_2atmpS5080 + _M0L6_2atmpS5081;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS5078, _M0L1iS1624, _M0L6_2atmpS5079);
      _M0L6_2atmpS5089 = _M0L1iS1624 + 1;
      _M0L1iS1624 = _M0L6_2atmpS5089;
      continue;
    }
    break;
  }
  _M0L7_2abindS1627 = 0;
  _M0L1iS1628 = _M0L7_2abindS1627;
  while (1) {
    if (_M0L1iS1628 < _M0L1nS1601) {
      struct _M0TPB5ArrayGfE* _M0L1vS5090 = _M0L1pS1602->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5107 = _M0L1pS1602->$2;
      float _M0L6_2atmpS5092;
      struct _M0TPB5ArrayGfE* _M0L2geS5106;
      float _M0L6_2atmpS5102;
      struct _M0TPB5ArrayGfE* _M0L1vS5105;
      float _M0L6_2atmpS5104;
      float _M0L6_2atmpS5103;
      float _M0L6_2atmpS5095;
      struct _M0TPB5ArrayGfE* _M0L2giS5101;
      float _M0L6_2atmpS5097;
      struct _M0TPB5ArrayGfE* _M0L1vS5100;
      float _M0L6_2atmpS5099;
      float _M0L6_2atmpS5098;
      float _M0L6_2atmpS5096;
      float _M0L6_2atmpS5094;
      float _M0L6_2atmpS5093;
      float _M0L6_2atmpS5091;
      int32_t _M0L6_2atmpS5108;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5092 = _M0MPC15array5Array2atGfE(_M0L1vS5107, _M0L1iS1628);
      _M0L2geS5106 = _M0L1pS1602->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5102 = _M0MPC15array5Array2atGfE(_M0L2geS5106, _M0L1iS1628);
      _M0L1vS5105 = _M0L1pS1602->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5104 = _M0MPC15array5Array2atGfE(_M0L1vS5105, _M0L1iS1628);
      _M0L6_2atmpS5103 = _M0L4e__eS1610 - _M0L6_2atmpS5104;
      _M0L6_2atmpS5095 = _M0L6_2atmpS5102 * _M0L6_2atmpS5103;
      _M0L2giS5101 = _M0L1pS1602->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5097 = _M0MPC15array5Array2atGfE(_M0L2giS5101, _M0L1iS1628);
      _M0L1vS5100 = _M0L1pS1602->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5099 = _M0MPC15array5Array2atGfE(_M0L1vS5100, _M0L1iS1628);
      _M0L6_2atmpS5098 = _M0L4e__iS1611 - _M0L6_2atmpS5099;
      _M0L6_2atmpS5096 = _M0L6_2atmpS5097 * _M0L6_2atmpS5098;
      _M0L6_2atmpS5094 = _M0L6_2atmpS5095 + _M0L6_2atmpS5096;
      _M0L6_2atmpS5093 = _M0L2dtS1614 * _M0L6_2atmpS5094;
      _M0L6_2atmpS5091 = _M0L6_2atmpS5092 + _M0L6_2atmpS5093;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5090, _M0L1iS1628, _M0L6_2atmpS5091);
      _M0L6_2atmpS5108 = _M0L1iS1628 + 1;
      _M0L1iS1628 = _M0L6_2atmpS5108;
      continue;
    }
    break;
  }
  _M0L7_2abindS1630 = 0;
  _M0L1iS1631 = _M0L7_2abindS1630;
  while (1) {
    if (_M0L1iS1631 < _M0L1nS1601) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5109 = _M0L1pS1602->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5112 = _M0L1pS1602->$2;
      float _M0L6_2atmpS5111;
      int32_t _M0L6_2atmpS5110;
      struct _M0TPB5ArrayGfE* _M0L1vS5113;
      struct _M0TPB5ArrayGbE* _M0L4fireS5115;
      float _M0L6_2atmpS5114;
      struct _M0TPB5ArrayGfE* _M0L1uS5117;
      struct _M0TPB5ArrayGfE* _M0L1uS5122;
      float _M0L6_2atmpS5119;
      struct _M0TPB5ArrayGbE* _M0L4fireS5121;
      float _M0L6_2atmpS5120;
      float _M0L6_2atmpS5118;
      int32_t _M0L6_2atmpS5123;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5111 = _M0MPC15array5Array2atGfE(_M0L1vS5112, _M0L1iS1631);
      _M0L6_2atmpS5110 = _M0L6_2atmpS5111 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5109, _M0L1iS1631, _M0L6_2atmpS5110);
      _M0L1vS5113 = _M0L1pS1602->$2;
      _M0L4fireS5115 = _M0L1pS1602->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS5115, _M0L1iS1631)) {
        _M0L6_2atmpS5114 = _M0L1cS1606;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS5116 = _M0L1pS1602->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS5114
        = _M0MPC15array5Array2atGfE(_M0L1vS5116, _M0L1iS1631);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5113, _M0L1iS1631, _M0L6_2atmpS5114);
      _M0L1uS5117 = _M0L1pS1602->$3;
      _M0L1uS5122 = _M0L1pS1602->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5119 = _M0MPC15array5Array2atGfE(_M0L1uS5122, _M0L1iS1631);
      _M0L4fireS5121 = _M0L1pS1602->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS5121, _M0L1iS1631)) {
        _M0L6_2atmpS5120 = _M0L1dS1607;
      } else {
        _M0L6_2atmpS5120 = 0x0p+0f;
      }
      _M0L6_2atmpS5118 = _M0L6_2atmpS5119 + _M0L6_2atmpS5120;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS5117, _M0L1iS1631, _M0L6_2atmpS5118);
      _M0L6_2atmpS5123 = _M0L1iS1631 + 1;
      _M0L1iS1631 = _M0L6_2atmpS5123;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS1558,
  float _M0L2dtS1584
) {
  int32_t _M0L1nS1557;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S1559;
  float _M0L2cmS1560;
  float _M0L2glS1561;
  float _M0L2elS1562;
  float _M0L2ekS1563;
  float _M0L2enS1564;
  float _M0L2gnS1565;
  float _M0L2gkS1566;
  float _M0L2vtS1567;
  float _M0L6tau__eS1568;
  float _M0L6tau__iS1569;
  float _M0L4e__eS1570;
  float _M0L4e__iS1571;
  int32_t _M0L7_2abindS1572;
  int32_t _M0L1iS1573;
  int32_t _M0L7_2abindS1598;
  int32_t _M0L1iS1599;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS1557 = _M0L1pS1558->$1;
  _M0L3p__S1559 = _M0L1pS1558->$0;
  _M0L2cmS1560 = _M0L3p__S1559->$0;
  _M0L2glS1561 = _M0L3p__S1559->$1;
  _M0L2elS1562 = _M0L3p__S1559->$2;
  _M0L2ekS1563 = _M0L3p__S1559->$3;
  _M0L2enS1564 = _M0L3p__S1559->$4;
  _M0L2gnS1565 = _M0L3p__S1559->$5;
  _M0L2gkS1566 = _M0L3p__S1559->$6;
  _M0L2vtS1567 = _M0L3p__S1559->$7;
  _M0L6tau__eS1568 = _M0L3p__S1559->$8;
  _M0L6tau__iS1569 = _M0L3p__S1559->$9;
  _M0L4e__eS1570 = _M0L3p__S1559->$10;
  _M0L4e__iS1571 = _M0L3p__S1559->$11;
  _M0L7_2abindS1572 = 0;
  _M0L1iS1573 = _M0L7_2abindS1572;
  while (1) {
    if (_M0L1iS1573 < _M0L1nS1557) {
      struct _M0TPB5ArrayGfE* _M0L1vS5025 = _M0L1pS1558->$2;
      float _M0L1vS1574;
      struct _M0TPB5ArrayGfE* _M0L1mS5024;
      float _M0L1mS1575;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS5023;
      float _M0L2nnS1576;
      struct _M0TPB5ArrayGfE* _M0L1hS5022;
      float _M0L1hS1577;
      struct _M0TPB5ArrayGfE* _M0L2geS5021;
      float _M0L2geS1578;
      struct _M0TPB5ArrayGfE* _M0L2giS5020;
      float _M0L2giS1579;
      struct _M0TPB5ArrayGbE* _M0L4fireS4923;
      float _M0L6_2atmpS5019;
      float _M0L7am__numS1580;
      float _M0L6_2atmpS5018;
      float _M0L7bm__numS1581;
      float _M0L6_2atmpS5013;
      float _M0L6_2atmpS5012;
      float _M0L6_2atmpS5011;
      float _M0L2amS1582;
      float _M0L6_2atmpS5006;
      float _M0L6_2atmpS5005;
      float _M0L6_2atmpS5004;
      float _M0L2bmS1583;
      struct _M0TPB5ArrayGfE* _M0L1mS4924;
      float _M0L6_2atmpS4930;
      float _M0L6_2atmpS4928;
      float _M0L6_2atmpS4929;
      float _M0L6_2atmpS4927;
      float _M0L6_2atmpS4926;
      float _M0L6_2atmpS4925;
      float _M0L6_2atmpS5003;
      float _M0L7an__numS1585;
      float _M0L6_2atmpS4998;
      float _M0L6_2atmpS4997;
      float _M0L6_2atmpS4996;
      float _M0L2anS1586;
      float _M0L6_2atmpS4995;
      float _M0L6_2atmpS4994;
      float _M0L6_2atmpS4993;
      float _M0L6_2atmpS4992;
      float _M0L2bnS1587;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4931;
      float _M0L6_2atmpS4937;
      float _M0L6_2atmpS4935;
      float _M0L6_2atmpS4936;
      float _M0L6_2atmpS4934;
      float _M0L6_2atmpS4933;
      float _M0L6_2atmpS4932;
      float _M0L6_2atmpS4991;
      float _M0L6_2atmpS4990;
      float _M0L6_2atmpS4989;
      float _M0L6_2atmpS4988;
      float _M0L2ahS1588;
      float _M0L6_2atmpS4987;
      float _M0L6_2atmpS4986;
      float _M0L6_2atmpS4985;
      float _M0L6_2atmpS4984;
      float _M0L9bh__denomS1589;
      float _M0L2bhS1590;
      struct _M0TPB5ArrayGfE* _M0L1hS4938;
      float _M0L6_2atmpS4944;
      float _M0L6_2atmpS4942;
      float _M0L6_2atmpS4943;
      float _M0L6_2atmpS4941;
      float _M0L6_2atmpS4940;
      float _M0L6_2atmpS4939;
      struct _M0TPB5ArrayGfE* _M0L1mS4983;
      float _M0L6m__newS1591;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4982;
      float _M0L6n__newS1592;
      struct _M0TPB5ArrayGfE* _M0L1hS4981;
      float _M0L6h__newS1593;
      float _M0L6_2atmpS4980;
      float _M0L6_2atmpS4979;
      float _M0L3m3hS1594;
      float _M0L6_2atmpS4978;
      float _M0L6_2atmpS4977;
      float _M0L2n4S1595;
      struct _M0TPB5ArrayGfE* _M0L1iS4976;
      float _M0L6_2atmpS4973;
      float _M0L6_2atmpS4975;
      float _M0L6_2atmpS4974;
      float _M0L6_2atmpS4970;
      float _M0L6_2atmpS4972;
      float _M0L6_2atmpS4971;
      float _M0L6_2atmpS4967;
      float _M0L6_2atmpS4969;
      float _M0L6_2atmpS4968;
      float _M0L6_2atmpS4963;
      float _M0L6_2atmpS4965;
      float _M0L6_2atmpS4966;
      float _M0L6_2atmpS4964;
      float _M0L6_2atmpS4959;
      float _M0L6_2atmpS4961;
      float _M0L6_2atmpS4962;
      float _M0L6_2atmpS4960;
      float _M0L7currentS1596;
      struct _M0TPB5ArrayGfE* _M0L1vS4945;
      float _M0L6_2atmpS4948;
      float _M0L6_2atmpS4947;
      float _M0L6_2atmpS4946;
      struct _M0TPB5ArrayGfE* _M0L2geS4949;
      float _M0L6_2atmpS4953;
      float _M0L6_2atmpS4952;
      float _M0L6_2atmpS4951;
      float _M0L6_2atmpS4950;
      struct _M0TPB5ArrayGfE* _M0L2giS4954;
      float _M0L6_2atmpS4958;
      float _M0L6_2atmpS4957;
      float _M0L6_2atmpS4956;
      float _M0L6_2atmpS4955;
      int32_t _M0L6_2atmpS5026;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS1574 = _M0MPC15array5Array2atGfE(_M0L1vS5025, _M0L1iS1573);
      _M0L1mS5024 = _M0L1pS1558->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS1575 = _M0MPC15array5Array2atGfE(_M0L1mS5024, _M0L1iS1573);
      _M0L7n__gateS5023 = _M0L1pS1558->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1576
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS5023, _M0L1iS1573);
      _M0L1hS5022 = _M0L1pS1558->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1577 = _M0MPC15array5Array2atGfE(_M0L1hS5022, _M0L1iS1573);
      _M0L2geS5021 = _M0L1pS1558->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1578 = _M0MPC15array5Array2atGfE(_M0L2geS5021, _M0L1iS1573);
      _M0L2giS5020 = _M0L1pS1558->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1579 = _M0MPC15array5Array2atGfE(_M0L2giS5020, _M0L1iS1573);
      _M0L4fireS4923 = _M0L1pS1558->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4923, _M0L1iS1573, 0);
      _M0L6_2atmpS5019 = 0x1.ap+3f - _M0L1vS1574;
      _M0L7am__numS1580 = _M0L6_2atmpS5019 + _M0L2vtS1567;
      _M0L6_2atmpS5018 = _M0L1vS1574 - _M0L2vtS1567;
      _M0L7bm__numS1581 = _M0L6_2atmpS5018 - 0x1.4p+5f;
      _M0L6_2atmpS5013 = _M0L7am__numS1580 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5012 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5013);
      _M0L6_2atmpS5011 = _M0L6_2atmpS5012 - 0x1p+0f;
      if (_M0L6_2atmpS5011 != 0x0p+0f) {
        float _M0L6_2atmpS5014 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1580;
        float _M0L6_2atmpS5017 = _M0L7am__numS1580 / 0x1p+2f;
        float _M0L6_2atmpS5016;
        float _M0L6_2atmpS5015;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS5016 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5017);
        _M0L6_2atmpS5015 = _M0L6_2atmpS5016 - 0x1p+0f;
        _M0L2amS1582 = _M0L6_2atmpS5014 / _M0L6_2atmpS5015;
      } else {
        _M0L2amS1582 = 0x0p+0f;
      }
      _M0L6_2atmpS5006 = _M0L7bm__numS1581 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5005 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5006);
      _M0L6_2atmpS5004 = _M0L6_2atmpS5005 - 0x1p+0f;
      if (_M0L6_2atmpS5004 != 0x0p+0f) {
        float _M0L6_2atmpS5007 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1581;
        float _M0L6_2atmpS5010 = _M0L7bm__numS1581 / 0x1.4p+2f;
        float _M0L6_2atmpS5009;
        float _M0L6_2atmpS5008;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS5009 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5010);
        _M0L6_2atmpS5008 = _M0L6_2atmpS5009 - 0x1p+0f;
        _M0L2bmS1583 = _M0L6_2atmpS5007 / _M0L6_2atmpS5008;
      } else {
        _M0L2bmS1583 = 0x0p+0f;
      }
      _M0L1mS4924 = _M0L1pS1558->$3;
      _M0L6_2atmpS4930 = 0x1p+0f - _M0L1mS1575;
      _M0L6_2atmpS4928 = _M0L2amS1582 * _M0L6_2atmpS4930;
      _M0L6_2atmpS4929 = _M0L2bmS1583 * _M0L1mS1575;
      _M0L6_2atmpS4927 = _M0L6_2atmpS4928 - _M0L6_2atmpS4929;
      _M0L6_2atmpS4926 = _M0L2dtS1584 * _M0L6_2atmpS4927;
      _M0L6_2atmpS4925 = _M0L1mS1575 + _M0L6_2atmpS4926;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS4924, _M0L1iS1573, _M0L6_2atmpS4925);
      _M0L6_2atmpS5003 = 0x1.ep+3f - _M0L1vS1574;
      _M0L7an__numS1585 = _M0L6_2atmpS5003 + _M0L2vtS1567;
      _M0L6_2atmpS4998 = _M0L7an__numS1585 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4997 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4998);
      _M0L6_2atmpS4996 = _M0L6_2atmpS4997 - 0x1p+0f;
      if (_M0L6_2atmpS4996 != 0x0p+0f) {
        float _M0L6_2atmpS4999 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1585;
        float _M0L6_2atmpS5002 = _M0L7an__numS1585 / 0x1.4p+2f;
        float _M0L6_2atmpS5001;
        float _M0L6_2atmpS5000;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS5001 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5002);
        _M0L6_2atmpS5000 = _M0L6_2atmpS5001 - 0x1p+0f;
        _M0L2anS1586 = _M0L6_2atmpS4999 / _M0L6_2atmpS5000;
      } else {
        _M0L2anS1586 = 0x0p+0f;
      }
      _M0L6_2atmpS4995 = 0x1.4p+3f - _M0L1vS1574;
      _M0L6_2atmpS4994 = _M0L6_2atmpS4995 + _M0L2vtS1567;
      _M0L6_2atmpS4993 = _M0L6_2atmpS4994 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4992 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4993);
      _M0L2bnS1587 = 0x1p-1f * _M0L6_2atmpS4992;
      _M0L7n__gateS4931 = _M0L1pS1558->$4;
      _M0L6_2atmpS4937 = 0x1p+0f - _M0L2nnS1576;
      _M0L6_2atmpS4935 = _M0L2anS1586 * _M0L6_2atmpS4937;
      _M0L6_2atmpS4936 = _M0L2bnS1587 * _M0L2nnS1576;
      _M0L6_2atmpS4934 = _M0L6_2atmpS4935 - _M0L6_2atmpS4936;
      _M0L6_2atmpS4933 = _M0L2dtS1584 * _M0L6_2atmpS4934;
      _M0L6_2atmpS4932 = _M0L2nnS1576 + _M0L6_2atmpS4933;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS4931, _M0L1iS1573, _M0L6_2atmpS4932);
      _M0L6_2atmpS4991 = 0x1.1p+4f - _M0L1vS1574;
      _M0L6_2atmpS4990 = _M0L6_2atmpS4991 + _M0L2vtS1567;
      _M0L6_2atmpS4989 = _M0L6_2atmpS4990 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4988 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4989);
      _M0L2ahS1588 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS4988;
      _M0L6_2atmpS4987 = 0x1.4p+5f - _M0L1vS1574;
      _M0L6_2atmpS4986 = _M0L6_2atmpS4987 + _M0L2vtS1567;
      _M0L6_2atmpS4985 = _M0L6_2atmpS4986 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4984 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4985);
      _M0L9bh__denomS1589 = 0x1p+0f + _M0L6_2atmpS4984;
      if (_M0L9bh__denomS1589 != 0x0p+0f) {
        _M0L2bhS1590 = 0x1p+2f / _M0L9bh__denomS1589;
      } else {
        _M0L2bhS1590 = 0x0p+0f;
      }
      _M0L1hS4938 = _M0L1pS1558->$5;
      _M0L6_2atmpS4944 = 0x1p+0f - _M0L1hS1577;
      _M0L6_2atmpS4942 = _M0L2ahS1588 * _M0L6_2atmpS4944;
      _M0L6_2atmpS4943 = _M0L2bhS1590 * _M0L1hS1577;
      _M0L6_2atmpS4941 = _M0L6_2atmpS4942 - _M0L6_2atmpS4943;
      _M0L6_2atmpS4940 = _M0L2dtS1584 * _M0L6_2atmpS4941;
      _M0L6_2atmpS4939 = _M0L1hS1577 + _M0L6_2atmpS4940;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS4938, _M0L1iS1573, _M0L6_2atmpS4939);
      _M0L1mS4983 = _M0L1pS1558->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1591 = _M0MPC15array5Array2atGfE(_M0L1mS4983, _M0L1iS1573);
      _M0L7n__gateS4982 = _M0L1pS1558->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1592
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4982, _M0L1iS1573);
      _M0L1hS4981 = _M0L1pS1558->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1593 = _M0MPC15array5Array2atGfE(_M0L1hS4981, _M0L1iS1573);
      _M0L6_2atmpS4980 = _M0L6m__newS1591 * _M0L6m__newS1591;
      _M0L6_2atmpS4979 = _M0L6_2atmpS4980 * _M0L6m__newS1591;
      _M0L3m3hS1594 = _M0L6_2atmpS4979 * _M0L6h__newS1593;
      _M0L6_2atmpS4978 = _M0L6n__newS1592 * _M0L6n__newS1592;
      _M0L6_2atmpS4977 = _M0L6_2atmpS4978 * _M0L6n__newS1592;
      _M0L2n4S1595 = _M0L6_2atmpS4977 * _M0L6n__newS1592;
      _M0L1iS4976 = _M0L1pS1558->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4973 = _M0MPC15array5Array2atGfE(_M0L1iS4976, _M0L1iS1573);
      _M0L6_2atmpS4975 = _M0L2elS1562 - _M0L1vS1574;
      _M0L6_2atmpS4974 = _M0L2glS1561 * _M0L6_2atmpS4975;
      _M0L6_2atmpS4970 = _M0L6_2atmpS4973 + _M0L6_2atmpS4974;
      _M0L6_2atmpS4972 = _M0L4e__eS1570 - _M0L1vS1574;
      _M0L6_2atmpS4971 = _M0L2geS1578 * _M0L6_2atmpS4972;
      _M0L6_2atmpS4967 = _M0L6_2atmpS4970 + _M0L6_2atmpS4971;
      _M0L6_2atmpS4969 = _M0L4e__iS1571 - _M0L1vS1574;
      _M0L6_2atmpS4968 = _M0L2giS1579 * _M0L6_2atmpS4969;
      _M0L6_2atmpS4963 = _M0L6_2atmpS4967 + _M0L6_2atmpS4968;
      _M0L6_2atmpS4965 = _M0L2gnS1565 * _M0L3m3hS1594;
      _M0L6_2atmpS4966 = _M0L2enS1564 - _M0L1vS1574;
      _M0L6_2atmpS4964 = _M0L6_2atmpS4965 * _M0L6_2atmpS4966;
      _M0L6_2atmpS4959 = _M0L6_2atmpS4963 + _M0L6_2atmpS4964;
      _M0L6_2atmpS4961 = _M0L2gkS1566 * _M0L2n4S1595;
      _M0L6_2atmpS4962 = _M0L2ekS1563 - _M0L1vS1574;
      _M0L6_2atmpS4960 = _M0L6_2atmpS4961 * _M0L6_2atmpS4962;
      _M0L7currentS1596 = _M0L6_2atmpS4959 + _M0L6_2atmpS4960;
      _M0L1vS4945 = _M0L1pS1558->$2;
      _M0L6_2atmpS4948 = _M0L2dtS1584 / _M0L2cmS1560;
      _M0L6_2atmpS4947 = _M0L6_2atmpS4948 * _M0L7currentS1596;
      _M0L6_2atmpS4946 = _M0L1vS1574 + _M0L6_2atmpS4947;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4945, _M0L1iS1573, _M0L6_2atmpS4946);
      _M0L2geS4949 = _M0L1pS1558->$8;
      _M0L6_2atmpS4953 = -_M0L2geS1578;
      _M0L6_2atmpS4952 = _M0L6_2atmpS4953 / _M0L6tau__eS1568;
      _M0L6_2atmpS4951 = _M0L2dtS1584 * _M0L6_2atmpS4952;
      _M0L6_2atmpS4950 = _M0L2geS1578 + _M0L6_2atmpS4951;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4949, _M0L1iS1573, _M0L6_2atmpS4950);
      _M0L2giS4954 = _M0L1pS1558->$9;
      _M0L6_2atmpS4958 = -_M0L2giS1579;
      _M0L6_2atmpS4957 = _M0L6_2atmpS4958 / _M0L6tau__iS1569;
      _M0L6_2atmpS4956 = _M0L2dtS1584 * _M0L6_2atmpS4957;
      _M0L6_2atmpS4955 = _M0L2giS1579 + _M0L6_2atmpS4956;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4954, _M0L1iS1573, _M0L6_2atmpS4955);
      _M0L6_2atmpS5026 = _M0L1iS1573 + 1;
      _M0L1iS1573 = _M0L6_2atmpS5026;
      continue;
    }
    break;
  }
  _M0L7_2abindS1598 = 0;
  _M0L1iS1599 = _M0L7_2abindS1598;
  while (1) {
    if (_M0L1iS1599 < _M0L1nS1557) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5027 = _M0L1pS1558->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS5030 = _M0L1pS1558->$2;
      float _M0L6_2atmpS5029;
      int32_t _M0L6_2atmpS5028;
      int32_t _M0L6_2atmpS5031;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5029 = _M0MPC15array5Array2atGfE(_M0L1vS5030, _M0L1iS1599);
      _M0L6_2atmpS5028 = _M0L6_2atmpS5029 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5027, _M0L1iS1599, _M0L6_2atmpS5028);
      _M0L6_2atmpS5031 = _M0L1iS1599 + 1;
      _M0L1iS1599 = _M0L6_2atmpS5031;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS1528,
  float _M0L2dtS1536
) {
  int32_t _M0L1nS1527;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4922;
  int32_t _M0L2ndS1529;
  int32_t _M0L8total__dS1530;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4921;
  float _M0L9steepnessS1531;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4920;
  float _M0L6tau__mS1532;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4919;
  float _M0L9tau__rateS1533;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4918;
  float _M0L8tau__absS1534;
  float _M0L6_2atmpS4917;
  int32_t _M0L11tabs__stepsS1535;
  int32_t _M0L7_2abindS1537;
  int32_t _M0L1iS1538;
  int32_t _M0L7_2abindS1541;
  int32_t _M0L1iS1542;
  int32_t _M0L7_2abindS1551;
  int32_t _M0L1iS1552;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS1527 = _M0L1pS1528->$1;
  _M0L5paramS4922 = _M0L1pS1528->$0;
  _M0L2ndS1529 = _M0L5paramS4922->$0;
  _M0L8total__dS1530 = _M0L1nS1527 * _M0L2ndS1529;
  _M0L5paramS4921 = _M0L1pS1528->$0;
  _M0L9steepnessS1531 = _M0L5paramS4921->$7;
  _M0L5paramS4920 = _M0L1pS1528->$0;
  _M0L6tau__mS1532 = _M0L5paramS4920->$8;
  _M0L5paramS4919 = _M0L1pS1528->$0;
  _M0L9tau__rateS1533 = _M0L5paramS4919->$9;
  _M0L5paramS4918 = _M0L1pS1528->$0;
  _M0L8tau__absS1534 = _M0L5paramS4918->$6;
  _M0L6_2atmpS4917 = _M0L8tau__absS1534 / _M0L2dtS1536;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS1535 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4917);
  _M0L7_2abindS1537 = 0;
  _M0L1iS1538 = _M0L7_2abindS1537;
  while (1) {
    if (_M0L1iS1538 < _M0L8total__dS1530) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS4845 = _M0L1pS1528->$6;
      float _M0L7tau__diS1539;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4833;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4844;
      float _M0L6_2atmpS4835;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4843;
      float _M0L6_2atmpS4842;
      float _M0L6_2atmpS4839;
      struct _M0TPB5ArrayGfE* _M0L4is__S4841;
      float _M0L6_2atmpS4840;
      float _M0L6_2atmpS4838;
      float _M0L6_2atmpS4837;
      float _M0L6_2atmpS4836;
      float _M0L6_2atmpS4834;
      int32_t _M0L6_2atmpS4846;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS1539
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS4845, _M0L1iS1538);
      _M0L4v__dS4833 = _M0L1pS1528->$2;
      _M0L4v__dS4844 = _M0L1pS1528->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4835
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4844, _M0L1iS1538);
      _M0L4v__dS4843 = _M0L1pS1528->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4842
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4843, _M0L1iS1538);
      _M0L6_2atmpS4839 = -_M0L6_2atmpS4842;
      _M0L4is__S4841 = _M0L1pS1528->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4840
      = _M0MPC15array5Array2atGfE(_M0L4is__S4841, _M0L1iS1538);
      _M0L6_2atmpS4838 = _M0L6_2atmpS4839 - _M0L6_2atmpS4840;
      _M0L6_2atmpS4837 = _M0L2dtS1536 * _M0L6_2atmpS4838;
      _M0L6_2atmpS4836 = _M0L6_2atmpS4837 / _M0L7tau__diS1539;
      _M0L6_2atmpS4834 = _M0L6_2atmpS4835 + _M0L6_2atmpS4836;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS4833, _M0L1iS1538, _M0L6_2atmpS4834);
      _M0L6_2atmpS4846 = _M0L1iS1538 + 1;
      _M0L1iS1538 = _M0L6_2atmpS4846;
      continue;
    }
    break;
  }
  _M0L7_2abindS1541 = 0;
  _M0L1iS1542 = _M0L7_2abindS1541;
  while (1) {
    if (_M0L1iS1542 < _M0L1nS1527) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4867 = _M0L1pS1528->$11;
      int32_t _M0L5startS1543;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4865;
      int32_t _M0L6_2atmpS4866;
      int32_t _M0L3endS1544;
      float _M0L16dt__over__tau__mS1545;
      struct _M0TPB8MutLocalGiE* _M0L1sS1546;
      int32_t _M0L6_2atmpS4868;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS1543
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4867, _M0L1iS1542);
      _M0L6colptrS4865 = _M0L1pS1528->$11;
      _M0L6_2atmpS4866 = _M0L1iS1542 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS1544
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4865, _M0L6_2atmpS4866);
      _M0L16dt__over__tau__mS1545 = _M0L2dtS1536 / _M0L6tau__mS1532;
      _M0L1sS1546
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1546)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1546->$0 = _M0L5startS1543;
      while (1) {
        int32_t _M0L3valS4847 = _M0L1sS1546->$0;
        if (_M0L3valS4847 < _M0L3endS1544) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS4863 = _M0L1pS1528->$12;
          int32_t _M0L3valS4864 = _M0L1sS1546->$0;
          int32_t _M0L9dend__idxS1547;
          struct _M0TPB5ArrayGfE* _M0L6w__synS4861;
          int32_t _M0L3valS4862;
          float _M0L1wS1548;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4848;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4858;
          float _M0L6_2atmpS4850;
          struct _M0TPB5ArrayGfE* _M0L4v__dS4857;
          float _M0L6_2atmpS4856;
          float _M0L6_2atmpS4853;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4855;
          float _M0L6_2atmpS4854;
          float _M0L6_2atmpS4852;
          float _M0L6_2atmpS4851;
          float _M0L6_2atmpS4849;
          int32_t _M0L3valS4860;
          int32_t _M0L6_2atmpS4859;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS1547
          = _M0MPC15array5Array2atGiE(_M0L6i__synS4863, _M0L3valS4864);
          _M0L6w__synS4861 = _M0L1pS1528->$13;
          _M0L3valS4862 = _M0L1sS1546->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS1548
          = _M0MPC15array5Array2atGfE(_M0L6w__synS4861, _M0L3valS4862);
          _M0L4v__sS4848 = _M0L1pS1528->$3;
          _M0L4v__sS4858 = _M0L1pS1528->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4850
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4858, _M0L1iS1542);
          _M0L4v__dS4857 = _M0L1pS1528->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4856
          = _M0MPC15array5Array2atGfE(_M0L4v__dS4857, _M0L9dend__idxS1547);
          _M0L6_2atmpS4853 = _M0L1wS1548 * _M0L6_2atmpS4856;
          _M0L4v__sS4855 = _M0L1pS1528->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4854
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4855, _M0L1iS1542);
          _M0L6_2atmpS4852 = _M0L6_2atmpS4853 - _M0L6_2atmpS4854;
          _M0L6_2atmpS4851 = _M0L6_2atmpS4852 * _M0L16dt__over__tau__mS1545;
          _M0L6_2atmpS4849 = _M0L6_2atmpS4850 + _M0L6_2atmpS4851;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS4848, _M0L1iS1542, _M0L6_2atmpS4849);
          _M0L3valS4860 = _M0L1sS1546->$0;
          _M0L6_2atmpS4859 = _M0L3valS4860 + 1;
          _M0L1sS1546->$0 = _M0L6_2atmpS4859;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1546);
        }
        break;
      }
      _M0L6_2atmpS4868 = _M0L1iS1542 + 1;
      _M0L1iS1542 = _M0L6_2atmpS4868;
      continue;
    }
    break;
  }
  _M0L7_2abindS1551 = 0;
  _M0L1iS1552 = _M0L7_2abindS1551;
  while (1) {
    if (_M0L1iS1552 < _M0L1nS1527) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4870 = _M0L1pS1528->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4873 = _M0L1pS1528->$8;
      int32_t _M0L6_2atmpS4872;
      int32_t _M0L6_2atmpS4871;
      struct _M0TPB5ArrayGbE* _M0L4fireS4874;
      struct _M0TPB5ArrayGfE* _M0L5traceS4875;
      struct _M0TPB5ArrayGfE* _M0L5traceS4883;
      float _M0L6_2atmpS4877;
      struct _M0TPB5ArrayGfE* _M0L5traceS4882;
      float _M0L6_2atmpS4881;
      float _M0L6_2atmpS4880;
      float _M0L6_2atmpS4879;
      float _M0L6_2atmpS4878;
      float _M0L6_2atmpS4876;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4885;
      int32_t _M0L6_2atmpS4884;
      struct _M0TPB5ArrayGfE* _M0L5traceS4886;
      struct _M0TPB5ArrayGfE* _M0L5traceS4895;
      float _M0L6_2atmpS4888;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4894;
      float _M0L6_2atmpS4891;
      struct _M0TPB5ArrayGfE* _M0L5traceS4893;
      float _M0L6_2atmpS4892;
      float _M0L6_2atmpS4890;
      float _M0L6_2atmpS4889;
      float _M0L6_2atmpS4887;
      float _M0L6_2atmpS4911;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4916;
      float _M0L6_2atmpS4913;
      struct _M0TPB5ArrayGfE* _M0L5traceS4915;
      float _M0L6_2atmpS4914;
      float _M0L6_2atmpS4912;
      float _M0L12sigmoid__argS1555;
      float _M0L4rateS1556;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4897;
      float _M0L6_2atmpS4896;
      int32_t _M0L6_2atmpS4869;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4872
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4873, _M0L1iS1552);
      _M0L6_2atmpS4871 = _M0L6_2atmpS4872 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4870, _M0L1iS1552, _M0L6_2atmpS4871);
      _M0L4fireS4874 = _M0L1pS1528->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4874, _M0L1iS1552, 0);
      _M0L5traceS4875 = _M0L1pS1528->$9;
      _M0L5traceS4883 = _M0L1pS1528->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4877
      = _M0MPC15array5Array2atGfE(_M0L5traceS4883, _M0L1iS1552);
      _M0L5traceS4882 = _M0L1pS1528->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4881
      = _M0MPC15array5Array2atGfE(_M0L5traceS4882, _M0L1iS1552);
      _M0L6_2atmpS4880 = -_M0L6_2atmpS4881;
      _M0L6_2atmpS4879 = _M0L6_2atmpS4880 / _M0L9tau__rateS1533;
      _M0L6_2atmpS4878 = _M0L2dtS1536 * _M0L6_2atmpS4879;
      _M0L6_2atmpS4876 = _M0L6_2atmpS4877 + _M0L6_2atmpS4878;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4875, _M0L1iS1552, _M0L6_2atmpS4876);
      _M0L4tabsS4885 = _M0L1pS1528->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4884
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4885, _M0L1iS1552);
      if (_M0L6_2atmpS4884 > 0) {
        goto join_1553;
      }
      _M0L5traceS4886 = _M0L1pS1528->$9;
      _M0L5traceS4895 = _M0L1pS1528->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4888
      = _M0MPC15array5Array2atGfE(_M0L5traceS4895, _M0L1iS1552);
      _M0L4v__sS4894 = _M0L1pS1528->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4891
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4894, _M0L1iS1552);
      _M0L5traceS4893 = _M0L1pS1528->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4892
      = _M0MPC15array5Array2atGfE(_M0L5traceS4893, _M0L1iS1552);
      _M0L6_2atmpS4890 = _M0L6_2atmpS4891 - _M0L6_2atmpS4892;
      _M0L6_2atmpS4889 = _M0L6_2atmpS4890 / _M0L9tau__rateS1533;
      _M0L6_2atmpS4887 = _M0L6_2atmpS4888 + _M0L6_2atmpS4889;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4886, _M0L1iS1552, _M0L6_2atmpS4887);
      _M0L6_2atmpS4911 = -_M0L9steepnessS1531;
      _M0L4v__sS4916 = _M0L1pS1528->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4913
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4916, _M0L1iS1552);
      _M0L5traceS4915 = _M0L1pS1528->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4914
      = _M0MPC15array5Array2atGfE(_M0L5traceS4915, _M0L1iS1552);
      _M0L6_2atmpS4912 = _M0L6_2atmpS4913 - _M0L6_2atmpS4914;
      _M0L12sigmoid__argS1555 = _M0L6_2atmpS4911 * _M0L6_2atmpS4912;
      if (_M0L12sigmoid__argS1555 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS4905 = _M0L1pS1528->$5;
        float _M0L6_2atmpS4904;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4904
        = _M0MPC15array5Array2atGfE(_M0L1rS4905, _M0L1iS1552);
        _M0L4rateS1556 = _M0L6_2atmpS4904 * _M0L2dtS1536;
      } else if (_M0L12sigmoid__argS1555 < -0x1.6p+6f) {
        _M0L4rateS1556 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS4910 = _M0L1pS1528->$5;
        float _M0L6_2atmpS4909;
        float _M0L6_2atmpS4906;
        float _M0L6_2atmpS4908;
        float _M0L6_2atmpS4907;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4909
        = _M0MPC15array5Array2atGfE(_M0L1rS4910, _M0L1iS1552);
        _M0L6_2atmpS4906 = _M0L6_2atmpS4909 * _M0L2dtS1536;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4908
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS1555);
        _M0L6_2atmpS4907 = 0x1p+0f + _M0L6_2atmpS4908;
        _M0L4rateS1556 = _M0L6_2atmpS4906 / _M0L6_2atmpS4907;
      }
      _M0L9randcacheS4897 = _M0L1pS1528->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4896
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS4897, _M0L1iS1552);
      if (_M0L6_2atmpS4896 < _M0L4rateS1556) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4898 = _M0L1pS1528->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4899;
        struct _M0TPB5ArrayGfE* _M0L5traceS4900;
        struct _M0TPB5ArrayGfE* _M0L5traceS4903;
        float _M0L6_2atmpS4902;
        float _M0L6_2atmpS4901;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4898, _M0L1iS1552, 1);
        _M0L4tabsS4899 = _M0L1pS1528->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4899, _M0L1iS1552, _M0L11tabs__stepsS1535);
        _M0L5traceS4900 = _M0L1pS1528->$9;
        _M0L5traceS4903 = _M0L1pS1528->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4902
        = _M0MPC15array5Array2atGfE(_M0L5traceS4903, _M0L1iS1552);
        _M0L6_2atmpS4901 = _M0L6_2atmpS4902 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS4900, _M0L1iS1552, _M0L6_2atmpS4901);
      }
      goto join_1553;
      goto joinlet_5978;
      join_1553:;
      _M0L6_2atmpS4869 = _M0L1iS1552 + 1;
      _M0L1iS1552 = _M0L6_2atmpS4869;
      continue;
      joinlet_5978:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1506,
  float _M0L2dtS1521
) {
  int32_t _M0L1nS1505;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S1507;
  float _M0L2tmS1508;
  float _M0L2vtS1509;
  float _M0L2vrS1510;
  float _M0L2elS1511;
  float _M0L1rS1512;
  float _M0L9dt__slopeS1513;
  float _M0L2twS1514;
  float _M0L1aS1515;
  float _M0L1bS1516;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4832;
  float _M0L2atS1517;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4831;
  float _M0L6tau__aS1518;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4830;
  float _M0L11tabs__constS1519;
  float _M0L6_2atmpS4829;
  int32_t _M0L11tabs__stepsS1520;
  int32_t _M0L7_2abindS1522;
  int32_t _M0L1iS1523;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1505 = _M0L1pS1506->$2;
  _M0L3p__S1507 = _M0L1pS1506->$0;
  _M0L2tmS1508 = _M0L3p__S1507->$5;
  _M0L2vtS1509 = _M0L3p__S1507->$2;
  _M0L2vrS1510 = _M0L3p__S1507->$3;
  _M0L2elS1511 = _M0L3p__S1507->$4;
  _M0L1rS1512 = _M0L3p__S1507->$6;
  _M0L9dt__slopeS1513 = _M0L3p__S1507->$7;
  _M0L2twS1514 = _M0L3p__S1507->$8;
  _M0L1aS1515 = _M0L3p__S1507->$9;
  _M0L1bS1516 = _M0L3p__S1507->$10;
  _M0L5spikeS4832 = _M0L1pS1506->$1;
  _M0L2atS1517 = _M0L5spikeS4832->$0;
  _M0L5spikeS4831 = _M0L1pS1506->$1;
  _M0L6tau__aS1518 = _M0L5spikeS4831->$1;
  _M0L5spikeS4830 = _M0L1pS1506->$1;
  _M0L11tabs__constS1519 = _M0L5spikeS4830->$3;
  _M0L6_2atmpS4829 = _M0L11tabs__constS1519 / _M0L2dtS1521;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS1520 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4829);
  _M0L7_2abindS1522 = 0;
  _M0L1iS1523 = _M0L7_2abindS1522;
  while (1) {
    if (_M0L1iS1523 < _M0L1nS1505) {
      struct _M0TPB5ArrayGfE* _M0L1vS4742 = _M0L1pS1506->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4744 = _M0L1pS1506->$5;
      float _M0L6_2atmpS4743;
      struct _M0TPB5ArrayGbE* _M0L4fireS4746;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4747;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4750;
      int32_t _M0L6_2atmpS4749;
      int32_t _M0L6_2atmpS4748;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4752;
      int32_t _M0L6_2atmpS4751;
      struct _M0TPB5ArrayGfE* _M0L1wS4753;
      struct _M0TPB5ArrayGfE* _M0L1wS4765;
      float _M0L6_2atmpS4755;
      struct _M0TPB5ArrayGfE* _M0L1vS4764;
      float _M0L6_2atmpS4763;
      float _M0L6_2atmpS4762;
      float _M0L6_2atmpS4759;
      struct _M0TPB5ArrayGfE* _M0L1wS4761;
      float _M0L6_2atmpS4760;
      float _M0L6_2atmpS4758;
      float _M0L6_2atmpS4757;
      float _M0L6_2atmpS4756;
      float _M0L6_2atmpS4754;
      float _M0L9exp__termS1526;
      struct _M0TPB5ArrayGfE* _M0L1vS4766;
      struct _M0TPB5ArrayGfE* _M0L1vS4788;
      float _M0L6_2atmpS4768;
      struct _M0TPB5ArrayGfE* _M0L1vS4787;
      float _M0L6_2atmpS4786;
      float _M0L6_2atmpS4785;
      float _M0L6_2atmpS4784;
      float _M0L6_2atmpS4780;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4783;
      float _M0L6_2atmpS4782;
      float _M0L6_2atmpS4781;
      float _M0L6_2atmpS4776;
      struct _M0TPB5ArrayGfE* _M0L1wS4779;
      float _M0L6_2atmpS4778;
      float _M0L6_2atmpS4777;
      float _M0L6_2atmpS4772;
      struct _M0TPB5ArrayGfE* _M0L1iS4775;
      float _M0L6_2atmpS4774;
      float _M0L6_2atmpS4773;
      float _M0L6_2atmpS4771;
      float _M0L6_2atmpS4770;
      float _M0L6_2atmpS4769;
      float _M0L6_2atmpS4767;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4789;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4797;
      float _M0L6_2atmpS4791;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4796;
      float _M0L6_2atmpS4795;
      float _M0L6_2atmpS4794;
      float _M0L6_2atmpS4793;
      float _M0L6_2atmpS4792;
      float _M0L6_2atmpS4790;
      struct _M0TPB5ArrayGbE* _M0L4fireS4798;
      struct _M0TPB5ArrayGfE* _M0L1vS4801;
      float _M0L6_2atmpS4800;
      int32_t _M0L6_2atmpS4799;
      struct _M0TPB5ArrayGfE* _M0L1vS4802;
      struct _M0TPB5ArrayGbE* _M0L4fireS4804;
      float _M0L6_2atmpS4803;
      struct _M0TPB5ArrayGfE* _M0L1wS4806;
      struct _M0TPB5ArrayGbE* _M0L4fireS4808;
      float _M0L6_2atmpS4807;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4812;
      struct _M0TPB5ArrayGbE* _M0L4fireS4814;
      float _M0L6_2atmpS4813;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4818;
      struct _M0TPB5ArrayGbE* _M0L4fireS4820;
      int32_t _M0L6_2atmpS4819;
      int32_t _M0L6_2atmpS4741;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4744, _M0L1iS1523)) {
        _M0L6_2atmpS4743 = _M0L2vrS1510;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4745 = _M0L1pS1506->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4743
        = _M0MPC15array5Array2atGfE(_M0L1vS4745, _M0L1iS1523);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4742, _M0L1iS1523, _M0L6_2atmpS4743);
      _M0L4fireS4746 = _M0L1pS1506->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4746, _M0L1iS1523, 0);
      _M0L4tabsS4747 = _M0L1pS1506->$7;
      _M0L4tabsS4750 = _M0L1pS1506->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4749
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4750, _M0L1iS1523);
      _M0L6_2atmpS4748 = _M0L6_2atmpS4749 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4747, _M0L1iS1523, _M0L6_2atmpS4748);
      _M0L4tabsS4752 = _M0L1pS1506->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4751
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4752, _M0L1iS1523);
      if (_M0L6_2atmpS4751 > 0) {
        goto join_1524;
      }
      _M0L1wS4753 = _M0L1pS1506->$4;
      _M0L1wS4765 = _M0L1pS1506->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4755 = _M0MPC15array5Array2atGfE(_M0L1wS4765, _M0L1iS1523);
      _M0L1vS4764 = _M0L1pS1506->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4763 = _M0MPC15array5Array2atGfE(_M0L1vS4764, _M0L1iS1523);
      _M0L6_2atmpS4762 = _M0L6_2atmpS4763 - _M0L2elS1511;
      _M0L6_2atmpS4759 = _M0L1aS1515 * _M0L6_2atmpS4762;
      _M0L1wS4761 = _M0L1pS1506->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4760 = _M0MPC15array5Array2atGfE(_M0L1wS4761, _M0L1iS1523);
      _M0L6_2atmpS4758 = _M0L6_2atmpS4759 - _M0L6_2atmpS4760;
      _M0L6_2atmpS4757 = _M0L2dtS1521 * _M0L6_2atmpS4758;
      _M0L6_2atmpS4756 = _M0L6_2atmpS4757 / _M0L2twS1514;
      _M0L6_2atmpS4754 = _M0L6_2atmpS4755 + _M0L6_2atmpS4756;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4753, _M0L1iS1523, _M0L6_2atmpS4754);
      if (_M0L9dt__slopeS1513 < 0x0p+0f) {
        _M0L9exp__termS1526 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4828 = _M0L1pS1506->$3;
        float _M0L6_2atmpS4825;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4827;
        float _M0L6_2atmpS4826;
        float _M0L6_2atmpS4824;
        float _M0L6_2atmpS4823;
        float _M0L6_2atmpS4822;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4825
        = _M0MPC15array5Array2atGfE(_M0L1vS4828, _M0L1iS1523);
        _M0L9thresholdS4827 = _M0L1pS1506->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4826
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4827, _M0L1iS1523);
        _M0L6_2atmpS4824 = _M0L6_2atmpS4825 - _M0L6_2atmpS4826;
        _M0L6_2atmpS4823 = _M0L6_2atmpS4824 / _M0L9dt__slopeS1513;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4822 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4823);
        _M0L9exp__termS1526 = _M0L9dt__slopeS1513 * _M0L6_2atmpS4822;
      }
      _M0L1vS4766 = _M0L1pS1506->$3;
      _M0L1vS4788 = _M0L1pS1506->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4768 = _M0MPC15array5Array2atGfE(_M0L1vS4788, _M0L1iS1523);
      _M0L1vS4787 = _M0L1pS1506->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4786 = _M0MPC15array5Array2atGfE(_M0L1vS4787, _M0L1iS1523);
      _M0L6_2atmpS4785 = _M0L6_2atmpS4786 - _M0L2elS1511;
      _M0L6_2atmpS4784 = -_M0L6_2atmpS4785;
      _M0L6_2atmpS4780 = _M0L6_2atmpS4784 + _M0L9exp__termS1526;
      _M0L9syn__currS4783 = _M0L1pS1506->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4782
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4783, _M0L1iS1523);
      _M0L6_2atmpS4781 = _M0L1rS1512 * _M0L6_2atmpS4782;
      _M0L6_2atmpS4776 = _M0L6_2atmpS4780 - _M0L6_2atmpS4781;
      _M0L1wS4779 = _M0L1pS1506->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4778 = _M0MPC15array5Array2atGfE(_M0L1wS4779, _M0L1iS1523);
      _M0L6_2atmpS4777 = _M0L1rS1512 * _M0L6_2atmpS4778;
      _M0L6_2atmpS4772 = _M0L6_2atmpS4776 - _M0L6_2atmpS4777;
      _M0L1iS4775 = _M0L1pS1506->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4774 = _M0MPC15array5Array2atGfE(_M0L1iS4775, _M0L1iS1523);
      _M0L6_2atmpS4773 = _M0L1rS1512 * _M0L6_2atmpS4774;
      _M0L6_2atmpS4771 = _M0L6_2atmpS4772 + _M0L6_2atmpS4773;
      _M0L6_2atmpS4770 = _M0L2dtS1521 * _M0L6_2atmpS4771;
      _M0L6_2atmpS4769 = _M0L6_2atmpS4770 / _M0L2tmS1508;
      _M0L6_2atmpS4767 = _M0L6_2atmpS4768 + _M0L6_2atmpS4769;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4766, _M0L1iS1523, _M0L6_2atmpS4767);
      _M0L9thresholdS4789 = _M0L1pS1506->$6;
      _M0L9thresholdS4797 = _M0L1pS1506->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4791
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4797, _M0L1iS1523);
      _M0L9thresholdS4796 = _M0L1pS1506->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4795
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4796, _M0L1iS1523);
      _M0L6_2atmpS4794 = _M0L2vtS1509 - _M0L6_2atmpS4795;
      _M0L6_2atmpS4793 = _M0L2dtS1521 * _M0L6_2atmpS4794;
      _M0L6_2atmpS4792 = _M0L6_2atmpS4793 / _M0L6tau__aS1518;
      _M0L6_2atmpS4790 = _M0L6_2atmpS4791 + _M0L6_2atmpS4792;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4789, _M0L1iS1523, _M0L6_2atmpS4790);
      _M0L4fireS4798 = _M0L1pS1506->$5;
      _M0L1vS4801 = _M0L1pS1506->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4800 = _M0MPC15array5Array2atGfE(_M0L1vS4801, _M0L1iS1523);
      _M0L6_2atmpS4799 = _M0L6_2atmpS4800 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4798, _M0L1iS1523, _M0L6_2atmpS4799);
      _M0L1vS4802 = _M0L1pS1506->$3;
      _M0L4fireS4804 = _M0L1pS1506->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4804, _M0L1iS1523)) {
        _M0L6_2atmpS4803 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4805 = _M0L1pS1506->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4803
        = _M0MPC15array5Array2atGfE(_M0L1vS4805, _M0L1iS1523);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4802, _M0L1iS1523, _M0L6_2atmpS4803);
      _M0L1wS4806 = _M0L1pS1506->$4;
      _M0L4fireS4808 = _M0L1pS1506->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4808, _M0L1iS1523)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4810 = _M0L1pS1506->$4;
        float _M0L6_2atmpS4809;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4809
        = _M0MPC15array5Array2atGfE(_M0L1wS4810, _M0L1iS1523);
        _M0L6_2atmpS4807 = _M0L6_2atmpS4809 + _M0L1bS1516;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4811 = _M0L1pS1506->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4807
        = _M0MPC15array5Array2atGfE(_M0L1wS4811, _M0L1iS1523);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4806, _M0L1iS1523, _M0L6_2atmpS4807);
      _M0L9thresholdS4812 = _M0L1pS1506->$6;
      _M0L4fireS4814 = _M0L1pS1506->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4814, _M0L1iS1523)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4816 = _M0L1pS1506->$6;
        float _M0L6_2atmpS4815;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4815
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4816, _M0L1iS1523);
        _M0L6_2atmpS4813 = _M0L6_2atmpS4815 + _M0L2atS1517;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4817 = _M0L1pS1506->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4813
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4817, _M0L1iS1523);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4812, _M0L1iS1523, _M0L6_2atmpS4813);
      _M0L4tabsS4818 = _M0L1pS1506->$7;
      _M0L4fireS4820 = _M0L1pS1506->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4820, _M0L1iS1523)) {
        _M0L6_2atmpS4819 = _M0L11tabs__stepsS1520;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4821 = _M0L1pS1506->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4819
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4821, _M0L1iS1523);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4818, _M0L1iS1523, _M0L6_2atmpS4819);
      goto join_1524;
      goto joinlet_5980;
      join_1524:;
      _M0L6_2atmpS4741 = _M0L1iS1523 + 1;
      _M0L1iS1523 = _M0L6_2atmpS4741;
      continue;
      joinlet_5980:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1501
) {
  int32_t _M0L1nS1500;
  int32_t _M0L7_2abindS1502;
  int32_t _M0L1iS1503;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1500 = _M0L1pS1501->$2;
  _M0L7_2abindS1502 = 0;
  _M0L1iS1503 = _M0L7_2abindS1502;
  while (1) {
    if (_M0L1iS1503 < _M0L1nS1500) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4718 = _M0L1pS1501->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4739 = _M0L1pS1501->$10;
      float _M0L6_2atmpS4734;
      struct _M0TPB5ArrayGfE* _M0L1vS4738;
      float _M0L6_2atmpS4736;
      float _M0L4e__eS4737;
      float _M0L6_2atmpS4735;
      float _M0L6_2atmpS4731;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4733;
      float _M0L6_2atmpS4732;
      float _M0L6_2atmpS4720;
      struct _M0TPB5ArrayGfE* _M0L2giS4730;
      float _M0L6_2atmpS4725;
      struct _M0TPB5ArrayGfE* _M0L1vS4729;
      float _M0L6_2atmpS4727;
      float _M0L4e__iS4728;
      float _M0L6_2atmpS4726;
      float _M0L6_2atmpS4722;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4724;
      float _M0L6_2atmpS4723;
      float _M0L6_2atmpS4721;
      float _M0L6_2atmpS4719;
      int32_t _M0L6_2atmpS4740;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4734 = _M0MPC15array5Array2atGfE(_M0L2geS4739, _M0L1iS1503);
      _M0L1vS4738 = _M0L1pS1501->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4736 = _M0MPC15array5Array2atGfE(_M0L1vS4738, _M0L1iS1503);
      _M0L4e__eS4737 = _M0L1pS1501->$16;
      _M0L6_2atmpS4735 = _M0L6_2atmpS4736 - _M0L4e__eS4737;
      _M0L6_2atmpS4731 = _M0L6_2atmpS4734 * _M0L6_2atmpS4735;
      _M0L7gsyn__eS4733 = _M0L1pS1501->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4732
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4733, _M0L1iS1503);
      _M0L6_2atmpS4720 = _M0L6_2atmpS4731 * _M0L6_2atmpS4732;
      _M0L2giS4730 = _M0L1pS1501->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4725 = _M0MPC15array5Array2atGfE(_M0L2giS4730, _M0L1iS1503);
      _M0L1vS4729 = _M0L1pS1501->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4727 = _M0MPC15array5Array2atGfE(_M0L1vS4729, _M0L1iS1503);
      _M0L4e__iS4728 = _M0L1pS1501->$17;
      _M0L6_2atmpS4726 = _M0L6_2atmpS4727 - _M0L4e__iS4728;
      _M0L6_2atmpS4722 = _M0L6_2atmpS4725 * _M0L6_2atmpS4726;
      _M0L7gsyn__iS4724 = _M0L1pS1501->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4723
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4724, _M0L1iS1503);
      _M0L6_2atmpS4721 = _M0L6_2atmpS4722 * _M0L6_2atmpS4723;
      _M0L6_2atmpS4719 = _M0L6_2atmpS4720 + _M0L6_2atmpS4721;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4718, _M0L1iS1503, _M0L6_2atmpS4719);
      _M0L6_2atmpS4740 = _M0L1iS1503 + 1;
      _M0L1iS1503 = _M0L6_2atmpS4740;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1490,
  float _M0L2dtS1495
) {
  int32_t _M0L1nS1489;
  float _M0L6tau__eS1491;
  float _M0L6tau__iS1492;
  int32_t _M0L7_2abindS1493;
  int32_t _M0L1iS1494;
  int32_t _M0L7_2abindS1497;
  int32_t _M0L1iS1498;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1489 = _M0L1pS1490->$2;
  _M0L6tau__eS1491 = _M0L1pS1490->$18;
  _M0L6tau__iS1492 = _M0L1pS1490->$19;
  _M0L7_2abindS1493 = 0;
  _M0L1iS1494 = _M0L7_2abindS1493;
  while (1) {
    if (_M0L1iS1494 < _M0L1nS1489) {
      struct _M0TPB5ArrayGfE* _M0L2geS4684 = _M0L1pS1490->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS4689 = _M0L1pS1490->$10;
      float _M0L6_2atmpS4686;
      struct _M0TPB5ArrayGfE* _M0L3gluS4688;
      float _M0L6_2atmpS4687;
      float _M0L6_2atmpS4685;
      struct _M0TPB5ArrayGfE* _M0L2giS4690;
      struct _M0TPB5ArrayGfE* _M0L2giS4695;
      float _M0L6_2atmpS4692;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4694;
      float _M0L6_2atmpS4693;
      float _M0L6_2atmpS4691;
      struct _M0TPB5ArrayGfE* _M0L2geS4696;
      struct _M0TPB5ArrayGfE* _M0L2geS4704;
      float _M0L6_2atmpS4698;
      struct _M0TPB5ArrayGfE* _M0L2geS4703;
      float _M0L6_2atmpS4702;
      float _M0L6_2atmpS4701;
      float _M0L6_2atmpS4700;
      float _M0L6_2atmpS4699;
      float _M0L6_2atmpS4697;
      struct _M0TPB5ArrayGfE* _M0L2giS4705;
      struct _M0TPB5ArrayGfE* _M0L2giS4713;
      float _M0L6_2atmpS4707;
      struct _M0TPB5ArrayGfE* _M0L2giS4712;
      float _M0L6_2atmpS4711;
      float _M0L6_2atmpS4710;
      float _M0L6_2atmpS4709;
      float _M0L6_2atmpS4708;
      float _M0L6_2atmpS4706;
      int32_t _M0L6_2atmpS4714;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4686 = _M0MPC15array5Array2atGfE(_M0L2geS4689, _M0L1iS1494);
      _M0L3gluS4688 = _M0L1pS1490->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4687
      = _M0MPC15array5Array2atGfE(_M0L3gluS4688, _M0L1iS1494);
      _M0L6_2atmpS4685 = _M0L6_2atmpS4686 + _M0L6_2atmpS4687;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4684, _M0L1iS1494, _M0L6_2atmpS4685);
      _M0L2giS4690 = _M0L1pS1490->$11;
      _M0L2giS4695 = _M0L1pS1490->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4692 = _M0MPC15array5Array2atGfE(_M0L2giS4695, _M0L1iS1494);
      _M0L4gabaS4694 = _M0L1pS1490->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4693
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4694, _M0L1iS1494);
      _M0L6_2atmpS4691 = _M0L6_2atmpS4692 + _M0L6_2atmpS4693;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4690, _M0L1iS1494, _M0L6_2atmpS4691);
      _M0L2geS4696 = _M0L1pS1490->$10;
      _M0L2geS4704 = _M0L1pS1490->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4698 = _M0MPC15array5Array2atGfE(_M0L2geS4704, _M0L1iS1494);
      _M0L2geS4703 = _M0L1pS1490->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4702 = _M0MPC15array5Array2atGfE(_M0L2geS4703, _M0L1iS1494);
      _M0L6_2atmpS4701 = -_M0L6_2atmpS4702;
      _M0L6_2atmpS4700 = _M0L6_2atmpS4701 / _M0L6tau__eS1491;
      _M0L6_2atmpS4699 = _M0L2dtS1495 * _M0L6_2atmpS4700;
      _M0L6_2atmpS4697 = _M0L6_2atmpS4698 + _M0L6_2atmpS4699;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4696, _M0L1iS1494, _M0L6_2atmpS4697);
      _M0L2giS4705 = _M0L1pS1490->$11;
      _M0L2giS4713 = _M0L1pS1490->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4707 = _M0MPC15array5Array2atGfE(_M0L2giS4713, _M0L1iS1494);
      _M0L2giS4712 = _M0L1pS1490->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4711 = _M0MPC15array5Array2atGfE(_M0L2giS4712, _M0L1iS1494);
      _M0L6_2atmpS4710 = -_M0L6_2atmpS4711;
      _M0L6_2atmpS4709 = _M0L6_2atmpS4710 / _M0L6tau__iS1492;
      _M0L6_2atmpS4708 = _M0L2dtS1495 * _M0L6_2atmpS4709;
      _M0L6_2atmpS4706 = _M0L6_2atmpS4707 + _M0L6_2atmpS4708;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4705, _M0L1iS1494, _M0L6_2atmpS4706);
      _M0L6_2atmpS4714 = _M0L1iS1494 + 1;
      _M0L1iS1494 = _M0L6_2atmpS4714;
      continue;
    }
    break;
  }
  _M0L7_2abindS1497 = 0;
  _M0L1iS1498 = _M0L7_2abindS1497;
  while (1) {
    if (_M0L1iS1498 < _M0L1nS1489) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4715 = _M0L1pS1490->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4716;
      int32_t _M0L6_2atmpS4717;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4715, _M0L1iS1498, 0x0p+0f);
      _M0L4gabaS4716 = _M0L1pS1490->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4716, _M0L1iS1498, 0x0p+0f);
      _M0L6_2atmpS4717 = _M0L1iS1498 + 1;
      _M0L1iS1498 = _M0L6_2atmpS4717;
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
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1468,
  float _M0L2dtS1483
) {
  int32_t _M0L1nS1467;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1469;
  float _M0L2tmS1470;
  float _M0L2vtS1471;
  float _M0L2vrS1472;
  float _M0L2elS1473;
  float _M0L1rS1474;
  float _M0L9dt__slopeS1475;
  float _M0L2twS1476;
  float _M0L1aS1477;
  float _M0L1bS1478;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4683;
  float _M0L2atS1479;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4682;
  float _M0L6tau__aS1480;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4681;
  float _M0L11tabs__constS1481;
  float _M0L6_2atmpS4680;
  int32_t _M0L11tabs__stepsS1482;
  int32_t _M0L7_2abindS1484;
  int32_t _M0L1iS1485;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1467 = _M0L1pS1468->$2;
  _M0L3p__S1469 = _M0L1pS1468->$0;
  _M0L2tmS1470 = _M0L3p__S1469->$5;
  _M0L2vtS1471 = _M0L3p__S1469->$2;
  _M0L2vrS1472 = _M0L3p__S1469->$3;
  _M0L2elS1473 = _M0L3p__S1469->$4;
  _M0L1rS1474 = _M0L3p__S1469->$6;
  _M0L9dt__slopeS1475 = _M0L3p__S1469->$7;
  _M0L2twS1476 = _M0L3p__S1469->$8;
  _M0L1aS1477 = _M0L3p__S1469->$9;
  _M0L1bS1478 = _M0L3p__S1469->$10;
  _M0L5spikeS4683 = _M0L1pS1468->$1;
  _M0L2atS1479 = _M0L5spikeS4683->$0;
  _M0L5spikeS4682 = _M0L1pS1468->$1;
  _M0L6tau__aS1480 = _M0L5spikeS4682->$1;
  _M0L5spikeS4681 = _M0L1pS1468->$1;
  _M0L11tabs__constS1481 = _M0L5spikeS4681->$3;
  _M0L6_2atmpS4680 = _M0L11tabs__constS1481 / _M0L2dtS1483;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1482 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4680);
  _M0L7_2abindS1484 = 0;
  _M0L1iS1485 = _M0L7_2abindS1484;
  while (1) {
    if (_M0L1iS1485 < _M0L1nS1467) {
      struct _M0TPB5ArrayGfE* _M0L1vS4593 = _M0L1pS1468->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4595 = _M0L1pS1468->$5;
      float _M0L6_2atmpS4594;
      struct _M0TPB5ArrayGbE* _M0L4fireS4597;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4598;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4601;
      int32_t _M0L6_2atmpS4600;
      int32_t _M0L6_2atmpS4599;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4603;
      int32_t _M0L6_2atmpS4602;
      struct _M0TPB5ArrayGfE* _M0L1wS4604;
      struct _M0TPB5ArrayGfE* _M0L1wS4616;
      float _M0L6_2atmpS4606;
      struct _M0TPB5ArrayGfE* _M0L1vS4615;
      float _M0L6_2atmpS4614;
      float _M0L6_2atmpS4613;
      float _M0L6_2atmpS4610;
      struct _M0TPB5ArrayGfE* _M0L1wS4612;
      float _M0L6_2atmpS4611;
      float _M0L6_2atmpS4609;
      float _M0L6_2atmpS4608;
      float _M0L6_2atmpS4607;
      float _M0L6_2atmpS4605;
      float _M0L9exp__termS1488;
      struct _M0TPB5ArrayGfE* _M0L1vS4617;
      struct _M0TPB5ArrayGfE* _M0L1vS4639;
      float _M0L6_2atmpS4619;
      struct _M0TPB5ArrayGfE* _M0L1vS4638;
      float _M0L6_2atmpS4637;
      float _M0L6_2atmpS4636;
      float _M0L6_2atmpS4635;
      float _M0L6_2atmpS4631;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4634;
      float _M0L6_2atmpS4633;
      float _M0L6_2atmpS4632;
      float _M0L6_2atmpS4627;
      struct _M0TPB5ArrayGfE* _M0L1wS4630;
      float _M0L6_2atmpS4629;
      float _M0L6_2atmpS4628;
      float _M0L6_2atmpS4623;
      struct _M0TPB5ArrayGfE* _M0L1iS4626;
      float _M0L6_2atmpS4625;
      float _M0L6_2atmpS4624;
      float _M0L6_2atmpS4622;
      float _M0L6_2atmpS4621;
      float _M0L6_2atmpS4620;
      float _M0L6_2atmpS4618;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4640;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4648;
      float _M0L6_2atmpS4642;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4647;
      float _M0L6_2atmpS4646;
      float _M0L6_2atmpS4645;
      float _M0L6_2atmpS4644;
      float _M0L6_2atmpS4643;
      float _M0L6_2atmpS4641;
      struct _M0TPB5ArrayGbE* _M0L4fireS4649;
      struct _M0TPB5ArrayGfE* _M0L1vS4652;
      float _M0L6_2atmpS4651;
      int32_t _M0L6_2atmpS4650;
      struct _M0TPB5ArrayGfE* _M0L1vS4653;
      struct _M0TPB5ArrayGbE* _M0L4fireS4655;
      float _M0L6_2atmpS4654;
      struct _M0TPB5ArrayGfE* _M0L1wS4657;
      struct _M0TPB5ArrayGbE* _M0L4fireS4659;
      float _M0L6_2atmpS4658;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4663;
      struct _M0TPB5ArrayGbE* _M0L4fireS4665;
      float _M0L6_2atmpS4664;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4669;
      struct _M0TPB5ArrayGbE* _M0L4fireS4671;
      int32_t _M0L6_2atmpS4670;
      int32_t _M0L6_2atmpS4592;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4595, _M0L1iS1485)) {
        _M0L6_2atmpS4594 = _M0L2vrS1472;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4596 = _M0L1pS1468->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4594
        = _M0MPC15array5Array2atGfE(_M0L1vS4596, _M0L1iS1485);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4593, _M0L1iS1485, _M0L6_2atmpS4594);
      _M0L4fireS4597 = _M0L1pS1468->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4597, _M0L1iS1485, 0);
      _M0L4tabsS4598 = _M0L1pS1468->$7;
      _M0L4tabsS4601 = _M0L1pS1468->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4600
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4601, _M0L1iS1485);
      _M0L6_2atmpS4599 = _M0L6_2atmpS4600 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4598, _M0L1iS1485, _M0L6_2atmpS4599);
      _M0L4tabsS4603 = _M0L1pS1468->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4602
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4603, _M0L1iS1485);
      if (_M0L6_2atmpS4602 > 0) {
        goto join_1486;
      }
      _M0L1wS4604 = _M0L1pS1468->$4;
      _M0L1wS4616 = _M0L1pS1468->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4606 = _M0MPC15array5Array2atGfE(_M0L1wS4616, _M0L1iS1485);
      _M0L1vS4615 = _M0L1pS1468->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4614 = _M0MPC15array5Array2atGfE(_M0L1vS4615, _M0L1iS1485);
      _M0L6_2atmpS4613 = _M0L6_2atmpS4614 - _M0L2elS1473;
      _M0L6_2atmpS4610 = _M0L1aS1477 * _M0L6_2atmpS4613;
      _M0L1wS4612 = _M0L1pS1468->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4611 = _M0MPC15array5Array2atGfE(_M0L1wS4612, _M0L1iS1485);
      _M0L6_2atmpS4609 = _M0L6_2atmpS4610 - _M0L6_2atmpS4611;
      _M0L6_2atmpS4608 = _M0L2dtS1483 * _M0L6_2atmpS4609;
      _M0L6_2atmpS4607 = _M0L6_2atmpS4608 / _M0L2twS1476;
      _M0L6_2atmpS4605 = _M0L6_2atmpS4606 + _M0L6_2atmpS4607;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4604, _M0L1iS1485, _M0L6_2atmpS4605);
      if (_M0L9dt__slopeS1475 < 0x0p+0f) {
        _M0L9exp__termS1488 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4679 = _M0L1pS1468->$3;
        float _M0L6_2atmpS4676;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4678;
        float _M0L6_2atmpS4677;
        float _M0L6_2atmpS4675;
        float _M0L6_2atmpS4674;
        float _M0L6_2atmpS4673;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4676
        = _M0MPC15array5Array2atGfE(_M0L1vS4679, _M0L1iS1485);
        _M0L9thresholdS4678 = _M0L1pS1468->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4677
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4678, _M0L1iS1485);
        _M0L6_2atmpS4675 = _M0L6_2atmpS4676 - _M0L6_2atmpS4677;
        _M0L6_2atmpS4674 = _M0L6_2atmpS4675 / _M0L9dt__slopeS1475;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4673 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4674);
        _M0L9exp__termS1488 = _M0L9dt__slopeS1475 * _M0L6_2atmpS4673;
      }
      _M0L1vS4617 = _M0L1pS1468->$3;
      _M0L1vS4639 = _M0L1pS1468->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4619 = _M0MPC15array5Array2atGfE(_M0L1vS4639, _M0L1iS1485);
      _M0L1vS4638 = _M0L1pS1468->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4637 = _M0MPC15array5Array2atGfE(_M0L1vS4638, _M0L1iS1485);
      _M0L6_2atmpS4636 = _M0L6_2atmpS4637 - _M0L2elS1473;
      _M0L6_2atmpS4635 = -_M0L6_2atmpS4636;
      _M0L6_2atmpS4631 = _M0L6_2atmpS4635 + _M0L9exp__termS1488;
      _M0L9syn__currS4634 = _M0L1pS1468->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4633
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4634, _M0L1iS1485);
      _M0L6_2atmpS4632 = _M0L1rS1474 * _M0L6_2atmpS4633;
      _M0L6_2atmpS4627 = _M0L6_2atmpS4631 - _M0L6_2atmpS4632;
      _M0L1wS4630 = _M0L1pS1468->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4629 = _M0MPC15array5Array2atGfE(_M0L1wS4630, _M0L1iS1485);
      _M0L6_2atmpS4628 = _M0L1rS1474 * _M0L6_2atmpS4629;
      _M0L6_2atmpS4623 = _M0L6_2atmpS4627 - _M0L6_2atmpS4628;
      _M0L1iS4626 = _M0L1pS1468->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4625 = _M0MPC15array5Array2atGfE(_M0L1iS4626, _M0L1iS1485);
      _M0L6_2atmpS4624 = _M0L1rS1474 * _M0L6_2atmpS4625;
      _M0L6_2atmpS4622 = _M0L6_2atmpS4623 + _M0L6_2atmpS4624;
      _M0L6_2atmpS4621 = _M0L2dtS1483 * _M0L6_2atmpS4622;
      _M0L6_2atmpS4620 = _M0L6_2atmpS4621 / _M0L2tmS1470;
      _M0L6_2atmpS4618 = _M0L6_2atmpS4619 + _M0L6_2atmpS4620;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4617, _M0L1iS1485, _M0L6_2atmpS4618);
      _M0L9thresholdS4640 = _M0L1pS1468->$6;
      _M0L9thresholdS4648 = _M0L1pS1468->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4642
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4648, _M0L1iS1485);
      _M0L9thresholdS4647 = _M0L1pS1468->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4646
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4647, _M0L1iS1485);
      _M0L6_2atmpS4645 = _M0L2vtS1471 - _M0L6_2atmpS4646;
      _M0L6_2atmpS4644 = _M0L2dtS1483 * _M0L6_2atmpS4645;
      _M0L6_2atmpS4643 = _M0L6_2atmpS4644 / _M0L6tau__aS1480;
      _M0L6_2atmpS4641 = _M0L6_2atmpS4642 + _M0L6_2atmpS4643;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4640, _M0L1iS1485, _M0L6_2atmpS4641);
      _M0L4fireS4649 = _M0L1pS1468->$5;
      _M0L1vS4652 = _M0L1pS1468->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4651 = _M0MPC15array5Array2atGfE(_M0L1vS4652, _M0L1iS1485);
      _M0L6_2atmpS4650 = _M0L6_2atmpS4651 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4649, _M0L1iS1485, _M0L6_2atmpS4650);
      _M0L1vS4653 = _M0L1pS1468->$3;
      _M0L4fireS4655 = _M0L1pS1468->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4655, _M0L1iS1485)) {
        _M0L6_2atmpS4654 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4656 = _M0L1pS1468->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4654
        = _M0MPC15array5Array2atGfE(_M0L1vS4656, _M0L1iS1485);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4653, _M0L1iS1485, _M0L6_2atmpS4654);
      _M0L1wS4657 = _M0L1pS1468->$4;
      _M0L4fireS4659 = _M0L1pS1468->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4659, _M0L1iS1485)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4661 = _M0L1pS1468->$4;
        float _M0L6_2atmpS4660;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4660
        = _M0MPC15array5Array2atGfE(_M0L1wS4661, _M0L1iS1485);
        _M0L6_2atmpS4658 = _M0L6_2atmpS4660 + _M0L1bS1478;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4662 = _M0L1pS1468->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4658
        = _M0MPC15array5Array2atGfE(_M0L1wS4662, _M0L1iS1485);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4657, _M0L1iS1485, _M0L6_2atmpS4658);
      _M0L9thresholdS4663 = _M0L1pS1468->$6;
      _M0L4fireS4665 = _M0L1pS1468->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4665, _M0L1iS1485)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4667 = _M0L1pS1468->$6;
        float _M0L6_2atmpS4666;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4666
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4667, _M0L1iS1485);
        _M0L6_2atmpS4664 = _M0L6_2atmpS4666 + _M0L2atS1479;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4668 = _M0L1pS1468->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4664
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4668, _M0L1iS1485);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4663, _M0L1iS1485, _M0L6_2atmpS4664);
      _M0L4tabsS4669 = _M0L1pS1468->$7;
      _M0L4fireS4671 = _M0L1pS1468->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4671, _M0L1iS1485)) {
        _M0L6_2atmpS4670 = _M0L11tabs__stepsS1482;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4672 = _M0L1pS1468->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4670
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4672, _M0L1iS1485);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4669, _M0L1iS1485, _M0L6_2atmpS4670);
      goto join_1486;
      goto joinlet_5985;
      join_1486:;
      _M0L6_2atmpS4592 = _M0L1iS1485 + 1;
      _M0L1iS1485 = _M0L6_2atmpS4592;
      continue;
      joinlet_5985:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1463
) {
  int32_t _M0L1nS1462;
  int32_t _M0L7_2abindS1464;
  int32_t _M0L1iS1465;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1462 = _M0L1pS1463->$2;
  _M0L7_2abindS1464 = 0;
  _M0L1iS1465 = _M0L7_2abindS1464;
  while (1) {
    if (_M0L1iS1465 < _M0L1nS1462) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4569 = _M0L1pS1463->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4590 = _M0L1pS1463->$10;
      float _M0L6_2atmpS4585;
      struct _M0TPB5ArrayGfE* _M0L1vS4589;
      float _M0L6_2atmpS4587;
      float _M0L4e__eS4588;
      float _M0L6_2atmpS4586;
      float _M0L6_2atmpS4582;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4584;
      float _M0L6_2atmpS4583;
      float _M0L6_2atmpS4571;
      struct _M0TPB5ArrayGfE* _M0L2giS4581;
      float _M0L6_2atmpS4576;
      struct _M0TPB5ArrayGfE* _M0L1vS4580;
      float _M0L6_2atmpS4578;
      float _M0L4e__iS4579;
      float _M0L6_2atmpS4577;
      float _M0L6_2atmpS4573;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4575;
      float _M0L6_2atmpS4574;
      float _M0L6_2atmpS4572;
      float _M0L6_2atmpS4570;
      int32_t _M0L6_2atmpS4591;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4585 = _M0MPC15array5Array2atGfE(_M0L2geS4590, _M0L1iS1465);
      _M0L1vS4589 = _M0L1pS1463->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4587 = _M0MPC15array5Array2atGfE(_M0L1vS4589, _M0L1iS1465);
      _M0L4e__eS4588 = _M0L1pS1463->$18;
      _M0L6_2atmpS4586 = _M0L6_2atmpS4587 - _M0L4e__eS4588;
      _M0L6_2atmpS4582 = _M0L6_2atmpS4585 * _M0L6_2atmpS4586;
      _M0L7gsyn__eS4584 = _M0L1pS1463->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4583
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4584, _M0L1iS1465);
      _M0L6_2atmpS4571 = _M0L6_2atmpS4582 * _M0L6_2atmpS4583;
      _M0L2giS4581 = _M0L1pS1463->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4576 = _M0MPC15array5Array2atGfE(_M0L2giS4581, _M0L1iS1465);
      _M0L1vS4580 = _M0L1pS1463->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4578 = _M0MPC15array5Array2atGfE(_M0L1vS4580, _M0L1iS1465);
      _M0L4e__iS4579 = _M0L1pS1463->$19;
      _M0L6_2atmpS4577 = _M0L6_2atmpS4578 - _M0L4e__iS4579;
      _M0L6_2atmpS4573 = _M0L6_2atmpS4576 * _M0L6_2atmpS4577;
      _M0L7gsyn__iS4575 = _M0L1pS1463->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4574
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4575, _M0L1iS1465);
      _M0L6_2atmpS4572 = _M0L6_2atmpS4573 * _M0L6_2atmpS4574;
      _M0L6_2atmpS4570 = _M0L6_2atmpS4571 + _M0L6_2atmpS4572;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4569, _M0L1iS1465, _M0L6_2atmpS4570);
      _M0L6_2atmpS4591 = _M0L1iS1465 + 1;
      _M0L1iS1465 = _M0L6_2atmpS4591;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1454,
  float _M0L2dtS1457
) {
  int32_t _M0L1nS1453;
  int32_t _M0L7_2abindS1455;
  int32_t _M0L1iS1456;
  int32_t _M0L7_2abindS1459;
  int32_t _M0L1iS1460;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1453 = _M0L1pS1454->$2;
  _M0L7_2abindS1455 = 0;
  _M0L1iS1456 = _M0L7_2abindS1455;
  while (1) {
    if (_M0L1iS1456 < _M0L1nS1453) {
      struct _M0TPB5ArrayGfE* _M0L2heS4507 = _M0L1pS1454->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS4512 = _M0L1pS1454->$12;
      float _M0L6_2atmpS4509;
      struct _M0TPB5ArrayGfE* _M0L3gluS4511;
      float _M0L6_2atmpS4510;
      float _M0L6_2atmpS4508;
      struct _M0TPB5ArrayGfE* _M0L2hiS4513;
      struct _M0TPB5ArrayGfE* _M0L2hiS4518;
      float _M0L6_2atmpS4515;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4517;
      float _M0L6_2atmpS4516;
      float _M0L6_2atmpS4514;
      struct _M0TPB5ArrayGfE* _M0L2geS4519;
      struct _M0TPB5ArrayGfE* _M0L2geS4531;
      float _M0L6_2atmpS4521;
      struct _M0TPB5ArrayGfE* _M0L2geS4530;
      float _M0L6_2atmpS4529;
      float _M0L6_2atmpS4527;
      float _M0L3tdeS4528;
      float _M0L6_2atmpS4524;
      struct _M0TPB5ArrayGfE* _M0L2heS4526;
      float _M0L6_2atmpS4525;
      float _M0L6_2atmpS4523;
      float _M0L6_2atmpS4522;
      float _M0L6_2atmpS4520;
      struct _M0TPB5ArrayGfE* _M0L2heS4532;
      struct _M0TPB5ArrayGfE* _M0L2heS4541;
      float _M0L6_2atmpS4534;
      struct _M0TPB5ArrayGfE* _M0L2heS4540;
      float _M0L6_2atmpS4539;
      float _M0L6_2atmpS4537;
      float _M0L3treS4538;
      float _M0L6_2atmpS4536;
      float _M0L6_2atmpS4535;
      float _M0L6_2atmpS4533;
      struct _M0TPB5ArrayGfE* _M0L2giS4542;
      struct _M0TPB5ArrayGfE* _M0L2giS4554;
      float _M0L6_2atmpS4544;
      struct _M0TPB5ArrayGfE* _M0L2giS4553;
      float _M0L6_2atmpS4552;
      float _M0L6_2atmpS4550;
      float _M0L3tdiS4551;
      float _M0L6_2atmpS4547;
      struct _M0TPB5ArrayGfE* _M0L2hiS4549;
      float _M0L6_2atmpS4548;
      float _M0L6_2atmpS4546;
      float _M0L6_2atmpS4545;
      float _M0L6_2atmpS4543;
      struct _M0TPB5ArrayGfE* _M0L2hiS4555;
      struct _M0TPB5ArrayGfE* _M0L2hiS4564;
      float _M0L6_2atmpS4557;
      struct _M0TPB5ArrayGfE* _M0L2hiS4563;
      float _M0L6_2atmpS4562;
      float _M0L6_2atmpS4560;
      float _M0L3triS4561;
      float _M0L6_2atmpS4559;
      float _M0L6_2atmpS4558;
      float _M0L6_2atmpS4556;
      int32_t _M0L6_2atmpS4565;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4509 = _M0MPC15array5Array2atGfE(_M0L2heS4512, _M0L1iS1456);
      _M0L3gluS4511 = _M0L1pS1454->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4510
      = _M0MPC15array5Array2atGfE(_M0L3gluS4511, _M0L1iS1456);
      _M0L6_2atmpS4508 = _M0L6_2atmpS4509 + _M0L6_2atmpS4510;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4507, _M0L1iS1456, _M0L6_2atmpS4508);
      _M0L2hiS4513 = _M0L1pS1454->$13;
      _M0L2hiS4518 = _M0L1pS1454->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4515 = _M0MPC15array5Array2atGfE(_M0L2hiS4518, _M0L1iS1456);
      _M0L4gabaS4517 = _M0L1pS1454->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4516
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4517, _M0L1iS1456);
      _M0L6_2atmpS4514 = _M0L6_2atmpS4515 + _M0L6_2atmpS4516;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4513, _M0L1iS1456, _M0L6_2atmpS4514);
      _M0L2geS4519 = _M0L1pS1454->$10;
      _M0L2geS4531 = _M0L1pS1454->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4521 = _M0MPC15array5Array2atGfE(_M0L2geS4531, _M0L1iS1456);
      _M0L2geS4530 = _M0L1pS1454->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4529 = _M0MPC15array5Array2atGfE(_M0L2geS4530, _M0L1iS1456);
      _M0L6_2atmpS4527 = -_M0L6_2atmpS4529;
      _M0L3tdeS4528 = _M0L1pS1454->$21;
      _M0L6_2atmpS4524 = _M0L6_2atmpS4527 / _M0L3tdeS4528;
      _M0L2heS4526 = _M0L1pS1454->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4525 = _M0MPC15array5Array2atGfE(_M0L2heS4526, _M0L1iS1456);
      _M0L6_2atmpS4523 = _M0L6_2atmpS4524 + _M0L6_2atmpS4525;
      _M0L6_2atmpS4522 = _M0L2dtS1457 * _M0L6_2atmpS4523;
      _M0L6_2atmpS4520 = _M0L6_2atmpS4521 + _M0L6_2atmpS4522;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4519, _M0L1iS1456, _M0L6_2atmpS4520);
      _M0L2heS4532 = _M0L1pS1454->$12;
      _M0L2heS4541 = _M0L1pS1454->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4534 = _M0MPC15array5Array2atGfE(_M0L2heS4541, _M0L1iS1456);
      _M0L2heS4540 = _M0L1pS1454->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4539 = _M0MPC15array5Array2atGfE(_M0L2heS4540, _M0L1iS1456);
      _M0L6_2atmpS4537 = -_M0L6_2atmpS4539;
      _M0L3treS4538 = _M0L1pS1454->$20;
      _M0L6_2atmpS4536 = _M0L6_2atmpS4537 / _M0L3treS4538;
      _M0L6_2atmpS4535 = _M0L2dtS1457 * _M0L6_2atmpS4536;
      _M0L6_2atmpS4533 = _M0L6_2atmpS4534 + _M0L6_2atmpS4535;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4532, _M0L1iS1456, _M0L6_2atmpS4533);
      _M0L2giS4542 = _M0L1pS1454->$11;
      _M0L2giS4554 = _M0L1pS1454->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4544 = _M0MPC15array5Array2atGfE(_M0L2giS4554, _M0L1iS1456);
      _M0L2giS4553 = _M0L1pS1454->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4552 = _M0MPC15array5Array2atGfE(_M0L2giS4553, _M0L1iS1456);
      _M0L6_2atmpS4550 = -_M0L6_2atmpS4552;
      _M0L3tdiS4551 = _M0L1pS1454->$23;
      _M0L6_2atmpS4547 = _M0L6_2atmpS4550 / _M0L3tdiS4551;
      _M0L2hiS4549 = _M0L1pS1454->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4548 = _M0MPC15array5Array2atGfE(_M0L2hiS4549, _M0L1iS1456);
      _M0L6_2atmpS4546 = _M0L6_2atmpS4547 + _M0L6_2atmpS4548;
      _M0L6_2atmpS4545 = _M0L2dtS1457 * _M0L6_2atmpS4546;
      _M0L6_2atmpS4543 = _M0L6_2atmpS4544 + _M0L6_2atmpS4545;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4542, _M0L1iS1456, _M0L6_2atmpS4543);
      _M0L2hiS4555 = _M0L1pS1454->$13;
      _M0L2hiS4564 = _M0L1pS1454->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4557 = _M0MPC15array5Array2atGfE(_M0L2hiS4564, _M0L1iS1456);
      _M0L2hiS4563 = _M0L1pS1454->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4562 = _M0MPC15array5Array2atGfE(_M0L2hiS4563, _M0L1iS1456);
      _M0L6_2atmpS4560 = -_M0L6_2atmpS4562;
      _M0L3triS4561 = _M0L1pS1454->$22;
      _M0L6_2atmpS4559 = _M0L6_2atmpS4560 / _M0L3triS4561;
      _M0L6_2atmpS4558 = _M0L2dtS1457 * _M0L6_2atmpS4559;
      _M0L6_2atmpS4556 = _M0L6_2atmpS4557 + _M0L6_2atmpS4558;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4555, _M0L1iS1456, _M0L6_2atmpS4556);
      _M0L6_2atmpS4565 = _M0L1iS1456 + 1;
      _M0L1iS1456 = _M0L6_2atmpS4565;
      continue;
    }
    break;
  }
  _M0L7_2abindS1459 = 0;
  _M0L1iS1460 = _M0L7_2abindS1459;
  while (1) {
    if (_M0L1iS1460 < _M0L1nS1453) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4566 = _M0L1pS1454->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4567;
      int32_t _M0L6_2atmpS4568;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4566, _M0L1iS1460, 0x0p+0f);
      _M0L4gabaS4567 = _M0L1pS1454->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4567, _M0L1iS1460, 0x0p+0f);
      _M0L6_2atmpS4568 = _M0L1iS1460 + 1;
      _M0L1iS1460 = _M0L6_2atmpS4568;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1429,
  float _M0L6t__nowS1440
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS4506;
  int32_t _M0L6_2atmpS4505;
  int32_t _M0L10use__delayS1428;
  struct _M0TPB5ArrayGfE* _M0L3rhoS4504;
  int32_t _M0L6_2atmpS4503;
  int32_t _M0L8use__rhoS1430;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS4506 = _M0L1cS1429->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4505 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS4506);
  _M0L10use__delayS1428 = _M0L6_2atmpS4505 > 0;
  _M0L3rhoS4504 = _M0L1cS1429->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4503 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS4504);
  _M0L8use__rhoS1430 = _M0L6_2atmpS4503 > 0;
  if (_M0L10use__delayS1428) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4466 = _M0L1cS1429->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS4465 = _M0L3preS4466->$5;
    int32_t _M0L6n__preS1431;
    struct _M0TPB8MutLocalGiE* _M0L1jS1432;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1431 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4465);
    _M0L1jS1432
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1432)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1432->$0 = 0;
    while (1) {
      int32_t _M0L3valS4434 = _M0L1jS1432->$0;
      if (_M0L3valS4434 < _M0L6n__preS1431) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4437 = _M0L1cS1429->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS4435 = _M0L3preS4437->$5;
        int32_t _M0L3valS4436 = _M0L1jS1432->$0;
        int32_t _M0L3valS4464;
        int32_t _M0L6_2atmpS4463;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS4435, _M0L3valS4436)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4462 =
            _M0L1cS1429->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4460 = _M0L6matrixS4462->$2;
          int32_t _M0L3valS4461 = _M0L1jS1432->$0;
          int32_t _M0L5startS1433;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4459;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4456;
          int32_t _M0L3valS4458;
          int32_t _M0L6_2atmpS4457;
          int32_t _M0L3endS1434;
          struct _M0TPB8MutLocalGiE* _M0L1sS1435;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1433
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4460, _M0L3valS4461);
          _M0L6matrixS4459 = _M0L1cS1429->$4;
          _M0L6rowptrS4456 = _M0L6matrixS4459->$2;
          _M0L3valS4458 = _M0L1jS1432->$0;
          _M0L6_2atmpS4457 = _M0L3valS4458 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1434
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4456, _M0L6_2atmpS4457);
          _M0L1sS1435
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1435)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1435->$0 = _M0L5startS1433;
          while (1) {
            int32_t _M0L3valS4438 = _M0L1sS1435->$0;
            if (_M0L3valS4438 < _M0L3endS1434) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4455 =
                _M0L1cS1429->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS4453 = _M0L6matrixS4455->$3;
              int32_t _M0L3valS4454 = _M0L1sS1435->$0;
              int32_t _M0L9post__idxS1436;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4452;
              struct _M0TPB5ArrayGfE* _M0L4valsS4450;
              int32_t _M0L3valS4451;
              float _M0L1wS1437;
              struct _M0TPB5ArrayGfE* _M0L6delaysS4448;
              int32_t _M0L3valS4449;
              float _M0L1dS1438;
              float _M0L9w__scaledS1439;
              int32_t _M0L3valS4444;
              int32_t _M0L6_2atmpS4443;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1436
              = _M0MPC15array5Array2atGiE(_M0L6colptrS4453, _M0L3valS4454);
              _M0L6matrixS4452 = _M0L1cS1429->$4;
              _M0L4valsS4450 = _M0L6matrixS4452->$4;
              _M0L3valS4451 = _M0L1sS1435->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1437
              = _M0MPC15array5Array2atGfE(_M0L4valsS4450, _M0L3valS4451);
              _M0L6delaysS4448 = _M0L1cS1429->$5;
              _M0L3valS4449 = _M0L1sS1435->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1438
              = _M0MPC15array5Array2atGfE(_M0L6delaysS4448, _M0L3valS4449);
              if (_M0L8use__rhoS1430) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS4446 = _M0L1cS1429->$6;
                int32_t _M0L3valS4447 = _M0L1sS1435->$0;
                float _M0L6_2atmpS4445;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4445
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4446, _M0L3valS4447);
                _M0L9w__scaledS1439 = _M0L1wS1437 * _M0L6_2atmpS4445;
              } else {
                _M0L9w__scaledS1439 = _M0L1wS1437;
              }
              if (_M0L1dS1438 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1429, _M0L9post__idxS1436, _M0L9w__scaledS1439);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS4439 =
                  _M0L1cS1429->$7;
                float _M0L6_2atmpS4440 = _M0L6t__nowS1440 + _M0L1dS1438;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS4441;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4442;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS4439, _M0L6_2atmpS4440);
                _M0L14pending__postsS4441 = _M0L1cS1429->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS4441, _M0L9post__idxS1436);
                _M0L16pending__weightsS4442 = _M0L1cS1429->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS4442, _M0L9w__scaledS1439);
              }
              _M0L3valS4444 = _M0L1sS1435->$0;
              _M0L6_2atmpS4443 = _M0L3valS4444 + 1;
              _M0L1sS1435->$0 = _M0L6_2atmpS4443;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1435);
            }
            break;
          }
        }
        _M0L3valS4464 = _M0L1jS1432->$0;
        _M0L6_2atmpS4463 = _M0L3valS4464 + 1;
        _M0L1jS1432->$0 = _M0L6_2atmpS4463;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1432);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS4500 = _M0L1cS1429->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1443;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS4500 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS4500)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS4500, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4500) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4501 = _M0L1cS1429->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5677 = _M0L4postS4501->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS5677);
      _M0L6targetS1443 = _M0L8_2afieldS5677;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4502 = _M0L1cS1429->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5678 = _M0L4postS4502->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS5678);
      _M0L6targetS1443 = _M0L8_2afieldS5678;
    }
    if (_M0L8use__rhoS1430) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4496 = _M0L1cS1429->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4495 = _M0L3preS4496->$5;
      int32_t _M0L6n__preS1444;
      struct _M0TPB8MutLocalGiE* _M0L1jS1445;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1444 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4495);
      _M0L1jS1445
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1445)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1445->$0 = 0;
      while (1) {
        int32_t _M0L3valS4467 = _M0L1jS1445->$0;
        if (_M0L3valS4467 < _M0L6n__preS1444) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4470 = _M0L1cS1429->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS4468 = _M0L3preS4470->$5;
          int32_t _M0L3valS4469 = _M0L1jS1445->$0;
          int32_t _M0L3valS4494;
          int32_t _M0L6_2atmpS4493;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS4468, _M0L3valS4469)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4492 =
              _M0L1cS1429->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4490 = _M0L6matrixS4492->$2;
            int32_t _M0L3valS4491 = _M0L1jS1445->$0;
            int32_t _M0L5startS1446;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4489;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4486;
            int32_t _M0L3valS4488;
            int32_t _M0L6_2atmpS4487;
            int32_t _M0L3endS1447;
            struct _M0TPB8MutLocalGiE* _M0L1sS1448;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1446
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4490, _M0L3valS4491);
            _M0L6matrixS4489 = _M0L1cS1429->$4;
            _M0L6rowptrS4486 = _M0L6matrixS4489->$2;
            _M0L3valS4488 = _M0L1jS1445->$0;
            _M0L6_2atmpS4487 = _M0L3valS4488 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1447
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4486, _M0L6_2atmpS4487);
            _M0L1sS1448
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1448)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1448->$0 = _M0L5startS1446;
            while (1) {
              int32_t _M0L3valS4471 = _M0L1sS1448->$0;
              if (_M0L3valS4471 < _M0L3endS1447) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4485 =
                  _M0L1cS1429->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS4483 =
                  _M0L6matrixS4485->$3;
                int32_t _M0L3valS4484 = _M0L1sS1448->$0;
                int32_t _M0L9post__idxS1449;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4482;
                struct _M0TPB5ArrayGfE* _M0L4valsS4480;
                int32_t _M0L3valS4481;
                float _M0L6_2atmpS4476;
                struct _M0TPB5ArrayGfE* _M0L3rhoS4478;
                int32_t _M0L3valS4479;
                float _M0L6_2atmpS4477;
                float _M0L9w__scaledS1450;
                float _M0L6_2atmpS4473;
                float _M0L6_2atmpS4472;
                int32_t _M0L3valS4475;
                int32_t _M0L6_2atmpS4474;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1449
                = _M0MPC15array5Array2atGiE(_M0L6colptrS4483, _M0L3valS4484);
                _M0L6matrixS4482 = _M0L1cS1429->$4;
                _M0L4valsS4480 = _M0L6matrixS4482->$4;
                _M0L3valS4481 = _M0L1sS1448->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4476
                = _M0MPC15array5Array2atGfE(_M0L4valsS4480, _M0L3valS4481);
                _M0L3rhoS4478 = _M0L1cS1429->$6;
                _M0L3valS4479 = _M0L1sS1448->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4477
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4478, _M0L3valS4479);
                _M0L9w__scaledS1450 = _M0L6_2atmpS4476 * _M0L6_2atmpS4477;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4473
                = _M0MPC15array5Array2atGfE(_M0L6targetS1443, _M0L9post__idxS1449);
                _M0L6_2atmpS4472 = _M0L6_2atmpS4473 + _M0L9w__scaledS1450;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1443, _M0L9post__idxS1449, _M0L6_2atmpS4472);
                _M0L3valS4475 = _M0L1sS1448->$0;
                _M0L6_2atmpS4474 = _M0L3valS4475 + 1;
                _M0L1sS1448->$0 = _M0L6_2atmpS4474;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1448);
              }
              break;
            }
          }
          _M0L3valS4494 = _M0L1jS1445->$0;
          _M0L6_2atmpS4493 = _M0L3valS4494 + 1;
          _M0L1jS1445->$0 = _M0L6_2atmpS4493;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1445);
          moonbit_decref_cycle_free(_M0L6targetS1443);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4497 =
        _M0L1cS1429->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4499 = _M0L1cS1429->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4498 = _M0L3preS4499->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS4497, _M0L4fireS4498, _M0L6targetS1443);
      moonbit_decref_cycle_free(_M0L6targetS1443);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1421,
  float _M0L6t__nowS1424
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS4433;
  int32_t _M0L1nS1420;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1422;
  struct _M0TPB8MutLocalGiE* _M0L1kS1423;
  int32_t _M0L3valS4432;
  int32_t _M0L6_2atmpS4431;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1426;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS4433 = _M0L1cS1421->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1420 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS4433);
  if (_M0L1nS1420 == 0) {
    return 0;
  }
  _M0L4keptS1422
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1422)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1422->$0 = 0;
  _M0L1kS1423
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1423)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1423->$0 = 0;
  while (1) {
    int32_t _M0L3valS4394 = _M0L1kS1423->$0;
    if (_M0L3valS4394 < _M0L1nS1420) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4396 = _M0L1cS1421->$7;
      int32_t _M0L3valS4397 = _M0L1kS1423->$0;
      float _M0L6_2atmpS4395;
      int32_t _M0L3valS4424;
      int32_t _M0L6_2atmpS4423;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS4395
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4396, _M0L3valS4397);
      if (_M0L6_2atmpS4395 <= _M0L6t__nowS1424) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS4402 = _M0L1cS1421->$8;
        int32_t _M0L3valS4403 = _M0L1kS1423->$0;
        int32_t _M0L6_2atmpS4398;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4400;
        int32_t _M0L3valS4401;
        float _M0L6_2atmpS4399;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4398
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4402, _M0L3valS4403);
        _M0L16pending__weightsS4400 = _M0L1cS1421->$9;
        _M0L3valS4401 = _M0L1kS1423->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4399
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4400, _M0L3valS4401);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1421, _M0L6_2atmpS4398, _M0L6_2atmpS4399);
      } else {
        int32_t _M0L3valS4404 = _M0L4keptS1422->$0;
        int32_t _M0L3valS4405 = _M0L1kS1423->$0;
        int32_t _M0L3valS4422;
        int32_t _M0L6_2atmpS4421;
        if (_M0L3valS4404 != _M0L3valS4405) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4406 = _M0L1cS1421->$7;
          int32_t _M0L3valS4407 = _M0L4keptS1422->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4409 = _M0L1cS1421->$7;
          int32_t _M0L3valS4410 = _M0L1kS1423->$0;
          float _M0L6_2atmpS4408;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4411;
          int32_t _M0L3valS4412;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4414;
          int32_t _M0L3valS4415;
          int32_t _M0L6_2atmpS4413;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4416;
          int32_t _M0L3valS4417;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4419;
          int32_t _M0L3valS4420;
          float _M0L6_2atmpS4418;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4408
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4409, _M0L3valS4410);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS4406, _M0L3valS4407, _M0L6_2atmpS4408);
          _M0L14pending__postsS4411 = _M0L1cS1421->$8;
          _M0L3valS4412 = _M0L4keptS1422->$0;
          _M0L14pending__postsS4414 = _M0L1cS1421->$8;
          _M0L3valS4415 = _M0L1kS1423->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4413
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4414, _M0L3valS4415);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS4411, _M0L3valS4412, _M0L6_2atmpS4413);
          _M0L16pending__weightsS4416 = _M0L1cS1421->$9;
          _M0L3valS4417 = _M0L4keptS1422->$0;
          _M0L16pending__weightsS4419 = _M0L1cS1421->$9;
          _M0L3valS4420 = _M0L1kS1423->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4418
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4419, _M0L3valS4420);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS4416, _M0L3valS4417, _M0L6_2atmpS4418);
        }
        _M0L3valS4422 = _M0L4keptS1422->$0;
        _M0L6_2atmpS4421 = _M0L3valS4422 + 1;
        _M0L4keptS1422->$0 = _M0L6_2atmpS4421;
      }
      _M0L3valS4424 = _M0L1kS1423->$0;
      _M0L6_2atmpS4423 = _M0L3valS4424 + 1;
      _M0L1kS1423->$0 = _M0L6_2atmpS4423;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1423);
    }
    break;
  }
  _M0L3valS4432 = _M0L4keptS1422->$0;
  moonbit_decref_cycle_free(_M0L4keptS1422);
  _M0L6_2atmpS4431 = _M0L1nS1420 - _M0L3valS4432;
  _M0L4dropS1426
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1426)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1426->$0 = _M0L6_2atmpS4431;
  while (1) {
    int32_t _M0L3valS4425 = _M0L4dropS1426->$0;
    if (_M0L3valS4425 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4426 = _M0L1cS1421->$7;
      void* _M0L6_2atmpS5680;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS4427;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4428;
      void* _M0L6_2atmpS5679;
      int32_t _M0L3valS4430;
      int32_t _M0L6_2atmpS4429;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5680
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS4426);
      moonbit_decref_cycle_free(_M0L6_2atmpS5680);
      _M0L14pending__postsS4427 = _M0L1cS1421->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS4427);
      _M0L16pending__weightsS4428 = _M0L1cS1421->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5679
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS4428);
      moonbit_decref_cycle_free(_M0L6_2atmpS5679);
      _M0L3valS4430 = _M0L4dropS1426->$0;
      _M0L6_2atmpS4429 = _M0L3valS4430 - 1;
      _M0L4dropS1426->$0 = _M0L6_2atmpS4429;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS1426);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1417,
  int32_t _M0L9post__idxS1418,
  float _M0L1wS1419
) {
  moonbit_string_t _M0L3symS4381;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS4381 = _M0L1cS1417->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS4381 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS4381)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS4381, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4381) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4387 = _M0L1cS1417->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4382 = _M0L4postS4387->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4386 = _M0L1cS1417->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4385 = _M0L4postS4386->$13;
    float _M0L6_2atmpS4384;
    float _M0L6_2atmpS4383;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4384
    = _M0MPC15array5Array2atGfE(_M0L3gluS4385, _M0L9post__idxS1418);
    _M0L6_2atmpS4383 = _M0L6_2atmpS4384 + _M0L1wS1419;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS4382, _M0L9post__idxS1418, _M0L6_2atmpS4383);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4393 = _M0L1cS1417->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4388 = _M0L4postS4393->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4392 = _M0L1cS1417->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4391 = _M0L4postS4392->$14;
    float _M0L6_2atmpS4390;
    float _M0L6_2atmpS4389;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4390
    = _M0MPC15array5Array2atGfE(_M0L4gabaS4391, _M0L9post__idxS1418);
    _M0L6_2atmpS4389 = _M0L6_2atmpS4390 + _M0L1wS1419;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS4388, _M0L9post__idxS1418, _M0L6_2atmpS4389);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1414,
  float _M0L1tS1416
) {
  int32_t _M0L11step__countS4367;
  int32_t _M0L6_2atmpS4366;
  int32_t _M0L11step__countS4369;
  int32_t _M0L9rec__stepS4370;
  int32_t _M0L6_2atmpS4368;
  moonbit_string_t _M0L3symS4373;
  float _M0L1vS1415;
  struct _M0TPB5ArrayGfE* _M0L4dataS4371;
  struct _M0TPB5ArrayGfE* _M0L5timesS4372;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS4367 = _M0L1mS1414->$6;
  _M0L6_2atmpS4366 = _M0L11step__countS4367 + 1;
  _M0L1mS1414->$6 = _M0L6_2atmpS4366;
  _M0L11step__countS4369 = _M0L1mS1414->$6;
  _M0L9rec__stepS4370 = _M0L1mS1414->$5;
  _M0L6_2atmpS4368 = _M0L11step__countS4369 % _M0L9rec__stepS4370;
  if (_M0L6_2atmpS4368 != 0) {
    return 0;
  }
  _M0L3symS4373 = _M0L1mS1414->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS4373 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS4373)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS4373, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS4373) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4376 = _M0L1mS1414->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS4374 = _M0L3popS4376->$3;
    int32_t _M0L6neuronS4375 = _M0L1mS1414->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1415 = _M0MPC15array5Array2atGfE(_M0L1vS4374, _M0L6neuronS4375);
  } else {
    moonbit_string_t _M0L3symS4377 = _M0L1mS1414->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS4377 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS4377)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS4377, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS4377) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4380 = _M0L1mS1414->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4378 = _M0L3popS4380->$5;
      int32_t _M0L6neuronS4379 = _M0L1mS1414->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4378, _M0L6neuronS4379)) {
        _M0L1vS1415 = 0x1p+0f;
      } else {
        _M0L1vS1415 = 0x0p+0f;
      }
    } else {
      _M0L1vS1415 = 0x0p+0f;
    }
  }
  _M0L4dataS4371 = _M0L1mS1414->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS4371, _M0L1vS1415);
  _M0L5timesS4372 = _M0L1mS1414->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS4372, _M0L1tS1416);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1412,
  int32_t _M0L6neuronS1413
) {
  float* _M0L6_2atmpS4365;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4362;
  float* _M0L6_2atmpS4364;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4363;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_5995;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS4365 = moonbit_empty_float_array;
  _M0L6_2atmpS4362
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4362)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4362->$0 = _M0L6_2atmpS4365;
  _M0L6_2atmpS4362->$1 = 0;
  _M0L6_2atmpS4364 = moonbit_empty_float_array;
  _M0L6_2atmpS4363
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4363)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4363->$0 = _M0L6_2atmpS4364;
  _M0L6_2atmpS4363->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1412);
  _block_5995
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_5995)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 75, 0);
  _block_5995->$0 = _M0L3popS1412;
  _block_5995->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_5995->$2 = _M0L6_2atmpS4362;
  _block_5995->$3 = _M0L6_2atmpS4363;
  _block_5995->$4 = _M0L6neuronS1413;
  _block_5995->$5 = 1;
  _block_5995->$6 = 0;
  return _block_5995;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1408
) {
  int32_t _M0L1nS1407;
  int32_t _M0L7_2abindS1409;
  int32_t _M0L1iS1410;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1407 = _M0L1pS1408->$2;
  _M0L7_2abindS1409 = 0;
  _M0L1iS1410 = _M0L7_2abindS1409;
  while (1) {
    if (_M0L1iS1410 < _M0L1nS1407) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4339 = _M0L1pS1408->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS4360 = _M0L1pS1408->$9;
      float _M0L6_2atmpS4355;
      struct _M0TPB5ArrayGfE* _M0L1vS4359;
      float _M0L6_2atmpS4357;
      float _M0L4e__eS4358;
      float _M0L6_2atmpS4356;
      float _M0L6_2atmpS4352;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4354;
      float _M0L6_2atmpS4353;
      float _M0L6_2atmpS4341;
      struct _M0TPB5ArrayGfE* _M0L2giS4351;
      float _M0L6_2atmpS4346;
      struct _M0TPB5ArrayGfE* _M0L1vS4350;
      float _M0L6_2atmpS4348;
      float _M0L4e__iS4349;
      float _M0L6_2atmpS4347;
      float _M0L6_2atmpS4343;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4345;
      float _M0L6_2atmpS4344;
      float _M0L6_2atmpS4342;
      float _M0L6_2atmpS4340;
      int32_t _M0L6_2atmpS4361;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4355 = _M0MPC15array5Array2atGfE(_M0L2geS4360, _M0L1iS1410);
      _M0L1vS4359 = _M0L1pS1408->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4357 = _M0MPC15array5Array2atGfE(_M0L1vS4359, _M0L1iS1410);
      _M0L4e__eS4358 = _M0L1pS1408->$17;
      _M0L6_2atmpS4356 = _M0L6_2atmpS4357 - _M0L4e__eS4358;
      _M0L6_2atmpS4352 = _M0L6_2atmpS4355 * _M0L6_2atmpS4356;
      _M0L7gsyn__eS4354 = _M0L1pS1408->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4353
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4354, _M0L1iS1410);
      _M0L6_2atmpS4341 = _M0L6_2atmpS4352 * _M0L6_2atmpS4353;
      _M0L2giS4351 = _M0L1pS1408->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4346 = _M0MPC15array5Array2atGfE(_M0L2giS4351, _M0L1iS1410);
      _M0L1vS4350 = _M0L1pS1408->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4348 = _M0MPC15array5Array2atGfE(_M0L1vS4350, _M0L1iS1410);
      _M0L4e__iS4349 = _M0L1pS1408->$18;
      _M0L6_2atmpS4347 = _M0L6_2atmpS4348 - _M0L4e__iS4349;
      _M0L6_2atmpS4343 = _M0L6_2atmpS4346 * _M0L6_2atmpS4347;
      _M0L7gsyn__iS4345 = _M0L1pS1408->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4344
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4345, _M0L1iS1410);
      _M0L6_2atmpS4342 = _M0L6_2atmpS4343 * _M0L6_2atmpS4344;
      _M0L6_2atmpS4340 = _M0L6_2atmpS4341 + _M0L6_2atmpS4342;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4339, _M0L1iS1410, _M0L6_2atmpS4340);
      _M0L6_2atmpS4361 = _M0L1iS1410 + 1;
      _M0L1iS1410 = _M0L6_2atmpS4361;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1399,
  float _M0L2dtS1402
) {
  int32_t _M0L1nS1398;
  int32_t _M0L7_2abindS1400;
  int32_t _M0L1iS1401;
  int32_t _M0L7_2abindS1404;
  int32_t _M0L1iS1405;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1398 = _M0L1pS1399->$2;
  _M0L7_2abindS1400 = 0;
  _M0L1iS1401 = _M0L7_2abindS1400;
  while (1) {
    if (_M0L1iS1401 < _M0L1nS1398) {
      struct _M0TPB5ArrayGfE* _M0L2heS4277 = _M0L1pS1399->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS4282 = _M0L1pS1399->$11;
      float _M0L6_2atmpS4279;
      struct _M0TPB5ArrayGfE* _M0L3gluS4281;
      float _M0L6_2atmpS4280;
      float _M0L6_2atmpS4278;
      struct _M0TPB5ArrayGfE* _M0L2hiS4283;
      struct _M0TPB5ArrayGfE* _M0L2hiS4288;
      float _M0L6_2atmpS4285;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4287;
      float _M0L6_2atmpS4286;
      float _M0L6_2atmpS4284;
      struct _M0TPB5ArrayGfE* _M0L2geS4289;
      struct _M0TPB5ArrayGfE* _M0L2geS4301;
      float _M0L6_2atmpS4291;
      struct _M0TPB5ArrayGfE* _M0L2geS4300;
      float _M0L6_2atmpS4299;
      float _M0L6_2atmpS4297;
      float _M0L3tdeS4298;
      float _M0L6_2atmpS4294;
      struct _M0TPB5ArrayGfE* _M0L2heS4296;
      float _M0L6_2atmpS4295;
      float _M0L6_2atmpS4293;
      float _M0L6_2atmpS4292;
      float _M0L6_2atmpS4290;
      struct _M0TPB5ArrayGfE* _M0L2heS4302;
      struct _M0TPB5ArrayGfE* _M0L2heS4311;
      float _M0L6_2atmpS4304;
      struct _M0TPB5ArrayGfE* _M0L2heS4310;
      float _M0L6_2atmpS4309;
      float _M0L6_2atmpS4307;
      float _M0L3treS4308;
      float _M0L6_2atmpS4306;
      float _M0L6_2atmpS4305;
      float _M0L6_2atmpS4303;
      struct _M0TPB5ArrayGfE* _M0L2giS4312;
      struct _M0TPB5ArrayGfE* _M0L2giS4324;
      float _M0L6_2atmpS4314;
      struct _M0TPB5ArrayGfE* _M0L2giS4323;
      float _M0L6_2atmpS4322;
      float _M0L6_2atmpS4320;
      float _M0L3tdiS4321;
      float _M0L6_2atmpS4317;
      struct _M0TPB5ArrayGfE* _M0L2hiS4319;
      float _M0L6_2atmpS4318;
      float _M0L6_2atmpS4316;
      float _M0L6_2atmpS4315;
      float _M0L6_2atmpS4313;
      struct _M0TPB5ArrayGfE* _M0L2hiS4325;
      struct _M0TPB5ArrayGfE* _M0L2hiS4334;
      float _M0L6_2atmpS4327;
      struct _M0TPB5ArrayGfE* _M0L2hiS4333;
      float _M0L6_2atmpS4332;
      float _M0L6_2atmpS4330;
      float _M0L3triS4331;
      float _M0L6_2atmpS4329;
      float _M0L6_2atmpS4328;
      float _M0L6_2atmpS4326;
      int32_t _M0L6_2atmpS4335;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4279 = _M0MPC15array5Array2atGfE(_M0L2heS4282, _M0L1iS1401);
      _M0L3gluS4281 = _M0L1pS1399->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4280
      = _M0MPC15array5Array2atGfE(_M0L3gluS4281, _M0L1iS1401);
      _M0L6_2atmpS4278 = _M0L6_2atmpS4279 + _M0L6_2atmpS4280;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4277, _M0L1iS1401, _M0L6_2atmpS4278);
      _M0L2hiS4283 = _M0L1pS1399->$12;
      _M0L2hiS4288 = _M0L1pS1399->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4285 = _M0MPC15array5Array2atGfE(_M0L2hiS4288, _M0L1iS1401);
      _M0L4gabaS4287 = _M0L1pS1399->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4286
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4287, _M0L1iS1401);
      _M0L6_2atmpS4284 = _M0L6_2atmpS4285 + _M0L6_2atmpS4286;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4283, _M0L1iS1401, _M0L6_2atmpS4284);
      _M0L2geS4289 = _M0L1pS1399->$9;
      _M0L2geS4301 = _M0L1pS1399->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4291 = _M0MPC15array5Array2atGfE(_M0L2geS4301, _M0L1iS1401);
      _M0L2geS4300 = _M0L1pS1399->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4299 = _M0MPC15array5Array2atGfE(_M0L2geS4300, _M0L1iS1401);
      _M0L6_2atmpS4297 = -_M0L6_2atmpS4299;
      _M0L3tdeS4298 = _M0L1pS1399->$20;
      _M0L6_2atmpS4294 = _M0L6_2atmpS4297 / _M0L3tdeS4298;
      _M0L2heS4296 = _M0L1pS1399->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4295 = _M0MPC15array5Array2atGfE(_M0L2heS4296, _M0L1iS1401);
      _M0L6_2atmpS4293 = _M0L6_2atmpS4294 + _M0L6_2atmpS4295;
      _M0L6_2atmpS4292 = _M0L2dtS1402 * _M0L6_2atmpS4293;
      _M0L6_2atmpS4290 = _M0L6_2atmpS4291 + _M0L6_2atmpS4292;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4289, _M0L1iS1401, _M0L6_2atmpS4290);
      _M0L2heS4302 = _M0L1pS1399->$11;
      _M0L2heS4311 = _M0L1pS1399->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4304 = _M0MPC15array5Array2atGfE(_M0L2heS4311, _M0L1iS1401);
      _M0L2heS4310 = _M0L1pS1399->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4309 = _M0MPC15array5Array2atGfE(_M0L2heS4310, _M0L1iS1401);
      _M0L6_2atmpS4307 = -_M0L6_2atmpS4309;
      _M0L3treS4308 = _M0L1pS1399->$19;
      _M0L6_2atmpS4306 = _M0L6_2atmpS4307 / _M0L3treS4308;
      _M0L6_2atmpS4305 = _M0L2dtS1402 * _M0L6_2atmpS4306;
      _M0L6_2atmpS4303 = _M0L6_2atmpS4304 + _M0L6_2atmpS4305;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4302, _M0L1iS1401, _M0L6_2atmpS4303);
      _M0L2giS4312 = _M0L1pS1399->$10;
      _M0L2giS4324 = _M0L1pS1399->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4314 = _M0MPC15array5Array2atGfE(_M0L2giS4324, _M0L1iS1401);
      _M0L2giS4323 = _M0L1pS1399->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4322 = _M0MPC15array5Array2atGfE(_M0L2giS4323, _M0L1iS1401);
      _M0L6_2atmpS4320 = -_M0L6_2atmpS4322;
      _M0L3tdiS4321 = _M0L1pS1399->$22;
      _M0L6_2atmpS4317 = _M0L6_2atmpS4320 / _M0L3tdiS4321;
      _M0L2hiS4319 = _M0L1pS1399->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4318 = _M0MPC15array5Array2atGfE(_M0L2hiS4319, _M0L1iS1401);
      _M0L6_2atmpS4316 = _M0L6_2atmpS4317 + _M0L6_2atmpS4318;
      _M0L6_2atmpS4315 = _M0L2dtS1402 * _M0L6_2atmpS4316;
      _M0L6_2atmpS4313 = _M0L6_2atmpS4314 + _M0L6_2atmpS4315;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4312, _M0L1iS1401, _M0L6_2atmpS4313);
      _M0L2hiS4325 = _M0L1pS1399->$12;
      _M0L2hiS4334 = _M0L1pS1399->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4327 = _M0MPC15array5Array2atGfE(_M0L2hiS4334, _M0L1iS1401);
      _M0L2hiS4333 = _M0L1pS1399->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4332 = _M0MPC15array5Array2atGfE(_M0L2hiS4333, _M0L1iS1401);
      _M0L6_2atmpS4330 = -_M0L6_2atmpS4332;
      _M0L3triS4331 = _M0L1pS1399->$21;
      _M0L6_2atmpS4329 = _M0L6_2atmpS4330 / _M0L3triS4331;
      _M0L6_2atmpS4328 = _M0L2dtS1402 * _M0L6_2atmpS4329;
      _M0L6_2atmpS4326 = _M0L6_2atmpS4327 + _M0L6_2atmpS4328;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4325, _M0L1iS1401, _M0L6_2atmpS4326);
      _M0L6_2atmpS4335 = _M0L1iS1401 + 1;
      _M0L1iS1401 = _M0L6_2atmpS4335;
      continue;
    }
    break;
  }
  _M0L7_2abindS1404 = 0;
  _M0L1iS1405 = _M0L7_2abindS1404;
  while (1) {
    if (_M0L1iS1405 < _M0L1nS1398) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4336 = _M0L1pS1399->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4337;
      int32_t _M0L6_2atmpS4338;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4336, _M0L1iS1405, 0x0p+0f);
      _M0L4gabaS4337 = _M0L1pS1399->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4337, _M0L1iS1405, 0x0p+0f);
      _M0L6_2atmpS4338 = _M0L1iS1405 + 1;
      _M0L1iS1405 = _M0L6_2atmpS4338;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1384,
  float _M0L2dtS1393
) {
  int32_t _M0L1nS1383;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1385;
  float _M0L2tmS1386;
  float _M0L2elS1387;
  float _M0L1rS1388;
  float _M0L2vtS1389;
  float _M0L2vrS1390;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS4276;
  float _M0L11tabs__constS1391;
  float _M0L6_2atmpS4275;
  int32_t _M0L11tabs__stepsS1392;
  int32_t _M0L7_2abindS1394;
  int32_t _M0L1iS1395;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1383 = _M0L1pS1384->$2;
  _M0L3p__S1385 = _M0L1pS1384->$0;
  _M0L2tmS1386 = _M0L3p__S1385->$2;
  _M0L2elS1387 = _M0L3p__S1385->$5;
  _M0L1rS1388 = _M0L3p__S1385->$6;
  _M0L2vtS1389 = _M0L3p__S1385->$3;
  _M0L2vrS1390 = _M0L3p__S1385->$4;
  _M0L5spikeS4276 = _M0L1pS1384->$1;
  _M0L11tabs__constS1391 = _M0L5spikeS4276->$0;
  _M0L6_2atmpS4275 = _M0L11tabs__constS1391 / _M0L2dtS1393;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1392 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4275);
  _M0L7_2abindS1394 = 0;
  _M0L1iS1395 = _M0L7_2abindS1394;
  while (1) {
    if (_M0L1iS1395 < _M0L1nS1383) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4235 = _M0L1pS1384->$6;
      int32_t _M0L6_2atmpS4234;
      struct _M0TPB5ArrayGfE* _M0L1vS4241;
      struct _M0TPB5ArrayGfE* _M0L1vS4262;
      float _M0L6_2atmpS4243;
      float _M0L6_2atmpS4245;
      struct _M0TPB5ArrayGfE* _M0L1vS4261;
      float _M0L6_2atmpS4260;
      float _M0L6_2atmpS4259;
      float _M0L6_2atmpS4251;
      struct _M0TPB5ArrayGfE* _M0L1wS4258;
      float _M0L6_2atmpS4257;
      float _M0L6_2atmpS4254;
      struct _M0TPB5ArrayGfE* _M0L1iS4256;
      float _M0L6_2atmpS4255;
      float _M0L6_2atmpS4253;
      float _M0L6_2atmpS4252;
      float _M0L6_2atmpS4247;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4250;
      float _M0L6_2atmpS4249;
      float _M0L6_2atmpS4248;
      float _M0L6_2atmpS4246;
      float _M0L6_2atmpS4244;
      float _M0L6_2atmpS4242;
      struct _M0TPB5ArrayGbE* _M0L4fireS4263;
      struct _M0TPB5ArrayGfE* _M0L1vS4266;
      float _M0L6_2atmpS4265;
      int32_t _M0L6_2atmpS4264;
      struct _M0TPB5ArrayGfE* _M0L1vS4267;
      struct _M0TPB5ArrayGbE* _M0L4fireS4269;
      float _M0L6_2atmpS4268;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4271;
      struct _M0TPB5ArrayGbE* _M0L4fireS4273;
      int32_t _M0L6_2atmpS4272;
      int32_t _M0L6_2atmpS4233;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4234
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4235, _M0L1iS1395);
      if (_M0L6_2atmpS4234 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4236 = _M0L1pS1384->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4237;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4240;
        int32_t _M0L6_2atmpS4239;
        int32_t _M0L6_2atmpS4238;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4236, _M0L1iS1395, 0);
        _M0L4tabsS4237 = _M0L1pS1384->$6;
        _M0L4tabsS4240 = _M0L1pS1384->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4239
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4240, _M0L1iS1395);
        _M0L6_2atmpS4238 = _M0L6_2atmpS4239 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4237, _M0L1iS1395, _M0L6_2atmpS4238);
        goto join_1396;
      }
      _M0L1vS4241 = _M0L1pS1384->$3;
      _M0L1vS4262 = _M0L1pS1384->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4243 = _M0MPC15array5Array2atGfE(_M0L1vS4262, _M0L1iS1395);
      _M0L6_2atmpS4245 = _M0L2dtS1393 / _M0L2tmS1386;
      _M0L1vS4261 = _M0L1pS1384->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4260 = _M0MPC15array5Array2atGfE(_M0L1vS4261, _M0L1iS1395);
      _M0L6_2atmpS4259 = _M0L6_2atmpS4260 - _M0L2elS1387;
      _M0L6_2atmpS4251 = -_M0L6_2atmpS4259;
      _M0L1wS4258 = _M0L1pS1384->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4257 = _M0MPC15array5Array2atGfE(_M0L1wS4258, _M0L1iS1395);
      _M0L6_2atmpS4254 = -_M0L6_2atmpS4257;
      _M0L1iS4256 = _M0L1pS1384->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4255 = _M0MPC15array5Array2atGfE(_M0L1iS4256, _M0L1iS1395);
      _M0L6_2atmpS4253 = _M0L6_2atmpS4254 + _M0L6_2atmpS4255;
      _M0L6_2atmpS4252 = _M0L1rS1388 * _M0L6_2atmpS4253;
      _M0L6_2atmpS4247 = _M0L6_2atmpS4251 + _M0L6_2atmpS4252;
      _M0L9syn__currS4250 = _M0L1pS1384->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4249
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4250, _M0L1iS1395);
      _M0L6_2atmpS4248 = _M0L1rS1388 * _M0L6_2atmpS4249;
      _M0L6_2atmpS4246 = _M0L6_2atmpS4247 - _M0L6_2atmpS4248;
      _M0L6_2atmpS4244 = _M0L6_2atmpS4245 * _M0L6_2atmpS4246;
      _M0L6_2atmpS4242 = _M0L6_2atmpS4243 + _M0L6_2atmpS4244;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4241, _M0L1iS1395, _M0L6_2atmpS4242);
      _M0L4fireS4263 = _M0L1pS1384->$5;
      _M0L1vS4266 = _M0L1pS1384->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4265 = _M0MPC15array5Array2atGfE(_M0L1vS4266, _M0L1iS1395);
      _M0L6_2atmpS4264 = _M0L6_2atmpS4265 > _M0L2vtS1389;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4263, _M0L1iS1395, _M0L6_2atmpS4264);
      _M0L1vS4267 = _M0L1pS1384->$3;
      _M0L4fireS4269 = _M0L1pS1384->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4269, _M0L1iS1395)) {
        _M0L6_2atmpS4268 = _M0L2vrS1390;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4270 = _M0L1pS1384->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4268
        = _M0MPC15array5Array2atGfE(_M0L1vS4270, _M0L1iS1395);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4267, _M0L1iS1395, _M0L6_2atmpS4268);
      _M0L4tabsS4271 = _M0L1pS1384->$6;
      _M0L4fireS4273 = _M0L1pS1384->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4273, _M0L1iS1395)) {
        _M0L6_2atmpS4272 = _M0L11tabs__stepsS1392;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4274 = _M0L1pS1384->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4272
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4274, _M0L1iS1395);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4271, _M0L1iS1395, _M0L6_2atmpS4272);
      goto join_1396;
      goto joinlet_6000;
      join_1396:;
      _M0L6_2atmpS4233 = _M0L1iS1395 + 1;
      _M0L1iS1395 = _M0L6_2atmpS4233;
      continue;
      joinlet_6000:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1371,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1374,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1380
) {
  int32_t _M0L4rowsS1370;
  int32_t _M0L7_2abindS1372;
  int32_t _M0L1iS1373;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1370 = _M0L1mS1371->$0;
  _M0L7_2abindS1372 = 0;
  _M0L1iS1373 = _M0L7_2abindS1372;
  while (1) {
    if (_M0L1iS1373 < _M0L4rowsS1370) {
      int32_t _M0L6_2atmpS4232;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1374, _M0L1iS1373)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4231 = _M0L1mS1371->$2;
        int32_t _M0L5startS1375;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4229;
        int32_t _M0L6_2atmpS4230;
        int32_t _M0L3endS1376;
        int32_t _M0L1kS1377;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1375
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4231, _M0L1iS1373);
        _M0L6rowptrS4229 = _M0L1mS1371->$2;
        _M0L6_2atmpS4230 = _M0L1iS1373 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1376
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4229, _M0L6_2atmpS4230);
        _M0L1kS1377 = _M0L5startS1375;
        while (1) {
          if (_M0L1kS1377 < _M0L3endS1376) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS4227 = _M0L1mS1371->$3;
            int32_t _M0L9post__idxS1378;
            struct _M0TPB5ArrayGfE* _M0L4valsS4226;
            float _M0L1wS1379;
            float _M0L6_2atmpS4225;
            float _M0L6_2atmpS4224;
            int32_t _M0L6_2atmpS4228;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1378
            = _M0MPC15array5Array2atGiE(_M0L6colptrS4227, _M0L1kS1377);
            _M0L4valsS4226 = _M0L1mS1371->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1379
            = _M0MPC15array5Array2atGfE(_M0L4valsS4226, _M0L1kS1377);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS4225
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1380, _M0L9post__idxS1378);
            _M0L6_2atmpS4224 = _M0L6_2atmpS4225 + _M0L1wS1379;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1380, _M0L9post__idxS1378, _M0L6_2atmpS4224);
            _M0L6_2atmpS4228 = _M0L1kS1377 + 1;
            _M0L1kS1377 = _M0L6_2atmpS4228;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS4232 = _M0L1iS1373 + 1;
      _M0L1iS1373 = _M0L6_2atmpS4232;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS1364,
  int32_t _M0L4colsS1365,
  float _M0L2muS1366,
  float _M0L5sigmaS1367,
  float _M0L1pS1368,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1369
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS1364, _M0L4colsS1365, _M0L2muS1366, _M0L5sigmaS1367, _M0L1pS1368, 0, _M0L3rngS1369);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS1278,
  int32_t _M0L4colsS1282,
  float _M0L2muS1288,
  float _M0L5sigmaS1289,
  float _M0L1pS1301,
  int32_t _M0L4ruleS1295,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1291
) {
  float* _M0L6_2atmpS4223;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4222;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS1277;
  int32_t _M0L7_2abindS1279;
  int32_t _M0L1iS1280;
  int32_t _M0L6_2atmpS4221;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1354;
  int32_t* _M0L6_2atmpS4220;
  struct _M0TPB5ArrayGiE* _M0L6colptrS1355;
  float* _M0L6_2atmpS4219;
  struct _M0TPB5ArrayGfE* _M0L4valsS1356;
  int32_t _M0L7_2abindS1357;
  int32_t _M0L1iS1358;
  int32_t _M0L6_2atmpS4218;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_6022;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS4223 = moonbit_empty_float_array;
  _M0L6_2atmpS4222
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4222)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4222->$0 = _M0L6_2atmpS4223;
  _M0L6_2atmpS4222->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS1277
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS1278, _M0L6_2atmpS4222);
  _M0L7_2abindS1279 = 0;
  _M0L1iS1280 = _M0L7_2abindS1279;
  while (1) {
    if (_M0L1iS1280 < _M0L4rowsS1278) {
      struct _M0TPB5ArrayGfE* _M0L3rowS1281;
      int32_t _M0L7_2abindS1283;
      int32_t _M0L1jS1284;
      int32_t _M0L6_2atmpS4174;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS1281 = _M0MPC15array5Array4makeGfE(_M0L4colsS1282, 0x0p+0f);
      _M0L7_2abindS1283 = 0;
      _M0L1jS1284 = _M0L7_2abindS1283;
      while (1) {
        if (_M0L1jS1284 < _M0L4colsS1282) {
          double _M0L2z1S1286;
          struct _M0TUddE* _M0L7_2abindS1290;
          double _M0L5_2az1S1292;
          float _M0L6_2atmpS4172;
          float _M0L6_2atmpS4171;
          float _M0L1wS1287;
          int32_t _M0L6_2atmpS4173;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS1290
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS1291);
          _M0L5_2az1S1292 = _M0L7_2abindS1290->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS1290);
          _M0L2z1S1286 = _M0L5_2az1S1292;
          goto join_1285;
          goto joinlet_6005;
          join_1285:;
          _M0L6_2atmpS4172 = (float)_M0L2z1S1286;
          _M0L6_2atmpS4171 = _M0L5sigmaS1289 * _M0L6_2atmpS4172;
          _M0L1wS1287 = _M0L2muS1288 + _M0L6_2atmpS4171;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS1281, _M0L1jS1284, _M0L1wS1287);
          joinlet_6005:;
          _M0L6_2atmpS4173 = _M0L1jS1284 + 1;
          _M0L1jS1284 = _M0L6_2atmpS4173;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS1277, _M0L1iS1280, _M0L3rowS1281);
      _M0L6_2atmpS4174 = _M0L1iS1280 + 1;
      _M0L1iS1280 = _M0L6_2atmpS4174;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS1295) {
    case 0: {
      int32_t _M0L7_2abindS1296 = 0;
      int32_t _M0L1iS1297 = _M0L7_2abindS1296;
      while (1) {
        if (_M0L1iS1297 < _M0L4rowsS1278) {
          int32_t _M0L7_2abindS1298 = 0;
          int32_t _M0L1jS1299 = _M0L7_2abindS1298;
          int32_t _M0L6_2atmpS4177;
          while (1) {
            if (_M0L1jS1299 < _M0L4colsS1282) {
              float _M0L1uS1300;
              int32_t _M0L6_2atmpS4176;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS1300 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1291);
              if (_M0L1uS1300 >= _M0L1pS1301) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4175;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4175
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1277, _M0L1iS1297);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4175, _M0L1jS1299, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4175);
              }
              _M0L6_2atmpS4176 = _M0L1jS1299 + 1;
              _M0L1jS1299 = _M0L6_2atmpS4176;
              continue;
            }
            break;
          }
          _M0L6_2atmpS4177 = _M0L1iS1297 + 1;
          _M0L1iS1297 = _M0L6_2atmpS4177;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS4195 = (float)_M0L4rowsS1278;
      float _M0L6_2atmpS4194 = _M0L6_2atmpS4195 * _M0L1pS1301;
      int32_t _M0L7n__keepS1304;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS1304 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4194);
      if (_M0L7n__keepS1304 > 0 && _M0L7n__keepS1304 <= _M0L4rowsS1278) {
        int32_t _M0L7_2abindS1305 = 0;
        int32_t _M0L1jS1306 = _M0L7_2abindS1305;
        while (1) {
          if (_M0L1jS1306 < _M0L4colsS1282) {
            int32_t* _M0L6_2atmpS4189 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS1307 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS1308;
            int32_t _M0L1kS1309;
            int32_t _M0L7n__dropS1311;
            int32_t _M0L7_2abindS1312;
            int32_t _M0L1kS1313;
            int32_t _M0L7_2abindS1319;
            int32_t _M0L1kS1320;
            int32_t _M0L6_2atmpS4190;
            Moonbit_object_header(_M0L8pre__idxS1307)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
            _M0L8pre__idxS1307->$0 = _M0L6_2atmpS4189;
            _M0L8pre__idxS1307->$1 = 0;
            _M0L7_2abindS1308 = 0;
            _M0L1kS1309 = _M0L7_2abindS1308;
            while (1) {
              if (_M0L1kS1309 < _M0L4rowsS1278) {
                int32_t _M0L6_2atmpS4178;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS1307, _M0L1kS1309);
                _M0L6_2atmpS4178 = _M0L1kS1309 + 1;
                _M0L1kS1309 = _M0L6_2atmpS4178;
                continue;
              }
              break;
            }
            _M0L7n__dropS1311 = _M0L4rowsS1278 - _M0L7n__keepS1304;
            _M0L7_2abindS1312 = 0;
            _M0L1kS1313 = _M0L7_2abindS1312;
            while (1) {
              if (_M0L1kS1313 < _M0L7n__dropS1311) {
                float _M0L1uS1314;
                float _M0L6_2atmpS4182;
                float _M0L6_2atmpS4184;
                float _M0L6_2atmpS4183;
                float _M0L6_2atmpS4181;
                int32_t _M0L6_2atmpS4180;
                int32_t _M0L6r__idxS1315;
                int32_t _M0L10r__clampedS1316;
                int32_t _M0L3tmpS1317;
                int32_t _M0L6_2atmpS4179;
                int32_t _M0L6_2atmpS4185;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS1314 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1291);
                _M0L6_2atmpS4182 = (float)_M0L4rowsS1278;
                _M0L6_2atmpS4184 = (float)_M0L1kS1313;
                _M0L6_2atmpS4183 = _M0L6_2atmpS4184 * _M0L1uS1314;
                _M0L6_2atmpS4181 = _M0L6_2atmpS4182 - _M0L6_2atmpS4183;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4180
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS4181);
                _M0L6r__idxS1315 = _M0L1kS1313 + _M0L6_2atmpS4180;
                if (_M0L6r__idxS1315 >= _M0L4rowsS1278) {
                  _M0L10r__clampedS1316 = _M0L4rowsS1278 - 1;
                } else {
                  _M0L10r__clampedS1316 = _M0L6r__idxS1315;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1317
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1307, _M0L1kS1313);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4179
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1307, _M0L10r__clampedS1316);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS1307, _M0L1kS1313, _M0L6_2atmpS4179);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS1307, _M0L10r__clampedS1316, _M0L3tmpS1317);
                _M0L6_2atmpS4185 = _M0L1kS1313 + 1;
                _M0L1kS1313 = _M0L6_2atmpS4185;
                continue;
              }
              break;
            }
            _M0L7_2abindS1319 = 0;
            _M0L1kS1320 = _M0L7_2abindS1319;
            while (1) {
              if (_M0L1kS1320 < _M0L7n__dropS1311) {
                int32_t _M0L6_2atmpS4187;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4186;
                int32_t _M0L6_2atmpS4188;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4187
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1307, _M0L1kS1320);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4186
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1277, _M0L6_2atmpS4187);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4186, _M0L1jS1306, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4186);
                _M0L6_2atmpS4188 = _M0L1kS1320 + 1;
                _M0L1kS1320 = _M0L6_2atmpS4188;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS1307);
              }
              break;
            }
            _M0L6_2atmpS4190 = _M0L1jS1306 + 1;
            _M0L1jS1306 = _M0L6_2atmpS4190;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS1304 == 0) {
        int32_t _M0L7_2abindS1323 = 0;
        int32_t _M0L1iS1324 = _M0L7_2abindS1323;
        while (1) {
          if (_M0L1iS1324 < _M0L4rowsS1278) {
            int32_t _M0L7_2abindS1325 = 0;
            int32_t _M0L1jS1326 = _M0L7_2abindS1325;
            int32_t _M0L6_2atmpS4193;
            while (1) {
              if (_M0L1jS1326 < _M0L4colsS1282) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4191;
                int32_t _M0L6_2atmpS4192;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4191
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1277, _M0L1iS1324);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4191, _M0L1jS1326, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4191);
                _M0L6_2atmpS4192 = _M0L1jS1326 + 1;
                _M0L1jS1326 = _M0L6_2atmpS4192;
                continue;
              }
              break;
            }
            _M0L6_2atmpS4193 = _M0L1iS1324 + 1;
            _M0L1iS1324 = _M0L6_2atmpS4193;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS4213 = (float)_M0L4colsS1282;
      float _M0L6_2atmpS4212 = _M0L6_2atmpS4213 * _M0L1pS1301;
      int32_t _M0L7n__keepS1329;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS1329 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4212);
      if (_M0L7n__keepS1329 > 0 && _M0L7n__keepS1329 <= _M0L4colsS1282) {
        int32_t _M0L7_2abindS1330 = 0;
        int32_t _M0L1iS1331 = _M0L7_2abindS1330;
        while (1) {
          if (_M0L1iS1331 < _M0L4rowsS1278) {
            int32_t* _M0L6_2atmpS4207 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS1332 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS1333;
            int32_t _M0L1kS1334;
            int32_t _M0L7n__dropS1336;
            int32_t _M0L7_2abindS1337;
            int32_t _M0L1kS1338;
            int32_t _M0L7_2abindS1344;
            int32_t _M0L1kS1345;
            int32_t _M0L6_2atmpS4208;
            Moonbit_object_header(_M0L9post__idxS1332)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
            _M0L9post__idxS1332->$0 = _M0L6_2atmpS4207;
            _M0L9post__idxS1332->$1 = 0;
            _M0L7_2abindS1333 = 0;
            _M0L1kS1334 = _M0L7_2abindS1333;
            while (1) {
              if (_M0L1kS1334 < _M0L4colsS1282) {
                int32_t _M0L6_2atmpS4196;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS1332, _M0L1kS1334);
                _M0L6_2atmpS4196 = _M0L1kS1334 + 1;
                _M0L1kS1334 = _M0L6_2atmpS4196;
                continue;
              }
              break;
            }
            _M0L7n__dropS1336 = _M0L4colsS1282 - _M0L7n__keepS1329;
            _M0L7_2abindS1337 = 0;
            _M0L1kS1338 = _M0L7_2abindS1337;
            while (1) {
              if (_M0L1kS1338 < _M0L7n__dropS1336) {
                float _M0L1uS1339;
                float _M0L6_2atmpS4200;
                float _M0L6_2atmpS4202;
                float _M0L6_2atmpS4201;
                float _M0L6_2atmpS4199;
                int32_t _M0L6_2atmpS4198;
                int32_t _M0L6r__idxS1340;
                int32_t _M0L10r__clampedS1341;
                int32_t _M0L3tmpS1342;
                int32_t _M0L6_2atmpS4197;
                int32_t _M0L6_2atmpS4203;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS1339 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1291);
                _M0L6_2atmpS4200 = (float)_M0L4colsS1282;
                _M0L6_2atmpS4202 = (float)_M0L1kS1338;
                _M0L6_2atmpS4201 = _M0L6_2atmpS4202 * _M0L1uS1339;
                _M0L6_2atmpS4199 = _M0L6_2atmpS4200 - _M0L6_2atmpS4201;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4198
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS4199);
                _M0L6r__idxS1340 = _M0L1kS1338 + _M0L6_2atmpS4198;
                if (_M0L6r__idxS1340 >= _M0L4colsS1282) {
                  _M0L10r__clampedS1341 = _M0L4colsS1282 - 1;
                } else {
                  _M0L10r__clampedS1341 = _M0L6r__idxS1340;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1342
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1332, _M0L1kS1338);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4197
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1332, _M0L10r__clampedS1341);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS1332, _M0L1kS1338, _M0L6_2atmpS4197);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS1332, _M0L10r__clampedS1341, _M0L3tmpS1342);
                _M0L6_2atmpS4203 = _M0L1kS1338 + 1;
                _M0L1kS1338 = _M0L6_2atmpS4203;
                continue;
              }
              break;
            }
            _M0L7_2abindS1344 = 0;
            _M0L1kS1345 = _M0L7_2abindS1344;
            while (1) {
              if (_M0L1kS1345 < _M0L7n__dropS1336) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4204;
                int32_t _M0L6_2atmpS4205;
                int32_t _M0L6_2atmpS4206;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4204
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1277, _M0L1iS1331);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4205
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1332, _M0L1kS1345);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4204, _M0L6_2atmpS4205, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4204);
                _M0L6_2atmpS4206 = _M0L1kS1345 + 1;
                _M0L1kS1345 = _M0L6_2atmpS4206;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS1332);
              }
              break;
            }
            _M0L6_2atmpS4208 = _M0L1iS1331 + 1;
            _M0L1iS1331 = _M0L6_2atmpS4208;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS1329 == 0) {
        int32_t _M0L7_2abindS1348 = 0;
        int32_t _M0L1iS1349 = _M0L7_2abindS1348;
        while (1) {
          if (_M0L1iS1349 < _M0L4rowsS1278) {
            int32_t _M0L7_2abindS1350 = 0;
            int32_t _M0L1jS1351 = _M0L7_2abindS1350;
            int32_t _M0L6_2atmpS4211;
            while (1) {
              if (_M0L1jS1351 < _M0L4colsS1282) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4209;
                int32_t _M0L6_2atmpS4210;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4209
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1277, _M0L1iS1349);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4209, _M0L1jS1351, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4209);
                _M0L6_2atmpS4210 = _M0L1jS1351 + 1;
                _M0L1jS1351 = _M0L6_2atmpS4210;
                continue;
              }
              break;
            }
            _M0L6_2atmpS4211 = _M0L1iS1349 + 1;
            _M0L1iS1349 = _M0L6_2atmpS4211;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS4221 = _M0L4rowsS1278 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS1354 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS4221, 0);
  _M0L6_2atmpS4220 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS1355
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS1355)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6colptrS1355->$0 = _M0L6_2atmpS4220;
  _M0L6colptrS1355->$1 = 0;
  _M0L6_2atmpS4219 = moonbit_empty_float_array;
  _M0L4valsS1356
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS1356)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L4valsS1356->$0 = _M0L6_2atmpS4219;
  _M0L4valsS1356->$1 = 0;
  _M0L7_2abindS1357 = 0;
  _M0L1iS1358 = _M0L7_2abindS1357;
  while (1) {
    if (_M0L1iS1358 < _M0L4rowsS1278) {
      int32_t _M0L6_2atmpS4214;
      int32_t _M0L7_2abindS1359;
      int32_t _M0L1jS1360;
      int32_t _M0L6_2atmpS4217;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS4214 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1356);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS1354, _M0L1iS1358, _M0L6_2atmpS4214);
      _M0L7_2abindS1359 = 0;
      _M0L1jS1360 = _M0L7_2abindS1359;
      while (1) {
        if (_M0L1jS1360 < _M0L4colsS1282) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS4215;
          float _M0L1vS1361;
          int32_t _M0L6_2atmpS4216;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS4215
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1277, _M0L1iS1358);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS1361
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS4215, _M0L1jS1360);
          moonbit_decref_cycle_free(_M0L6_2atmpS4215);
          if (_M0L1vS1361 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS1355, _M0L1jS1360);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS1356, _M0L1vS1361);
          }
          _M0L6_2atmpS4216 = _M0L1jS1360 + 1;
          _M0L1jS1360 = _M0L6_2atmpS4216;
          continue;
        }
        break;
      }
      _M0L6_2atmpS4217 = _M0L1iS1358 + 1;
      _M0L1iS1358 = _M0L6_2atmpS4217;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS1277);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS4218 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1356);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS1354, _M0L4rowsS1278, _M0L6_2atmpS4218);
  _block_6022
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_6022)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 81, 0);
  _block_6022->$0 = _M0L4rowsS1278;
  _block_6022->$1 = _M0L4colsS1282;
  _block_6022->$2 = _M0L6rowptrS1354;
  _block_6022->$3 = _M0L6colptrS1355;
  _block_6022->$4 = _M0L4valsS1356;
  return _block_6022;
}

int32_t _M0FP26RiantR8snn__mbt20ca__plasticity__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1255,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1242,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1244,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1254,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1250,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS1239,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS1246,
  float _M0L6t__nowS1240,
  float _M0L2dtS1267
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS4062;
  int32_t _M0L6_2atmpS4061;
  int32_t _if__result_6023;
  int32_t _M0L6n__preS1241;
  int32_t _M0L7n__postS1243;
  float _M0L8tau__preS4170;
  float _M0L13inv__tau__preS1245;
  float _M0L9tau__postS4169;
  float _M0L14inv__tau__postS1247;
  struct _M0TPB8MutLocalGiE* _M0L1jS1248;
  struct _M0TPB8MutLocalGiE* _M0L1kS1258;
  struct _M0TPB8MutLocalGiE* _M0L2jjS1266;
  struct _M0TPB8MutLocalGiE* _M0L2iiS1269;
  struct _M0TPB8MutLocalGiE* _M0L3jj2S1271;
  struct _M0TPB8MutLocalGiE* _M0L3ii2S1273;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1275;
  #line 1495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS4062 = _M0L4varsS1239->$4;
  #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS4061 = _M0MPC15array5Array6lengthGbE(_M0L6activeS4062);
  if (_M0L6_2atmpS4061 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS4060 = _M0L4varsS1239->$4;
    int32_t _M0L6_2atmpS4059;
    #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4059 = _M0MPC15array5Array2atGbE(_M0L6activeS4060, 0);
    _if__result_6023 = !_M0L6_2atmpS4059;
  } else {
    _if__result_6023 = 0;
  }
  if (_if__result_6023) {
    return 0;
  }
  #line 1509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1241 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1242);
  #line 1510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1243 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1244);
  _M0L8tau__preS4170 = _M0L5paramS1246->$2;
  _M0L13inv__tau__preS1245 = 0x1p+0f / _M0L8tau__preS4170;
  _M0L9tau__postS4169 = _M0L5paramS1246->$3;
  _M0L14inv__tau__postS1247 = 0x1p+0f / _M0L9tau__postS4169;
  _M0L1jS1248
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1248)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1248->$0 = 0;
  while (1) {
    int32_t _M0L3valS4063 = _M0L1jS1248->$0;
    if (_M0L3valS4063 < _M0L6n__preS1241) {
      int32_t _M0L3valS4064 = _M0L1jS1248->$0;
      int32_t _M0L3valS4079;
      int32_t _M0L6_2atmpS4078;
      #line 1516 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1242, _M0L3valS4064)) {
        int32_t _M0L3valS4077 = _M0L1jS1248->$0;
        int32_t _M0L5startS1249;
        int32_t _M0L3valS4076;
        int32_t _M0L6_2atmpS4075;
        int32_t _M0L5end__S1251;
        struct _M0TPB8MutLocalGiE* _M0L1sS1252;
        #line 1517 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1249
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1250, _M0L3valS4077);
        _M0L3valS4076 = _M0L1jS1248->$0;
        _M0L6_2atmpS4075 = _M0L3valS4076 + 1;
        #line 1518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5end__S1251
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1250, _M0L6_2atmpS4075);
        _M0L1sS1252
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1252)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1252->$0 = _M0L5startS1249;
        while (1) {
          int32_t _M0L3valS4065 = _M0L1sS1252->$0;
          if (_M0L3valS4065 < _M0L5end__S1251) {
            int32_t _M0L3valS4074 = _M0L1sS1252->$0;
            int32_t _M0L1iS1253;
            int32_t _M0L3valS4066;
            int32_t _M0L3valS4071;
            float _M0L6_2atmpS4068;
            struct _M0TPB5ArrayGfE* _M0L5tpostS4070;
            float _M0L6_2atmpS4069;
            float _M0L6_2atmpS4067;
            int32_t _M0L3valS4073;
            int32_t _M0L6_2atmpS4072;
            #line 1521 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L1iS1253
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1254, _M0L3valS4074);
            _M0L3valS4066 = _M0L1sS1252->$0;
            _M0L3valS4071 = _M0L1sS1252->$0;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4068
            = _M0MPC15array5Array2atGfE(_M0L1wS1255, _M0L3valS4071);
            _M0L5tpostS4070 = _M0L4varsS1239->$3;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4069
            = _M0MPC15array5Array2atGfE(_M0L5tpostS4070, _M0L1iS1253);
            _M0L6_2atmpS4067 = _M0L6_2atmpS4068 + _M0L6_2atmpS4069;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1255, _M0L3valS4066, _M0L6_2atmpS4067);
            _M0L3valS4073 = _M0L1sS1252->$0;
            _M0L6_2atmpS4072 = _M0L3valS4073 + 1;
            _M0L1sS1252->$0 = _M0L6_2atmpS4072;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1252);
          }
          break;
        }
      }
      _M0L3valS4079 = _M0L1jS1248->$0;
      _M0L6_2atmpS4078 = _M0L3valS4079 + 1;
      _M0L1jS1248->$0 = _M0L6_2atmpS4078;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1248);
    }
    break;
  }
  _M0L1kS1258
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1258)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1258->$0 = 0;
  while (1) {
    int32_t _M0L3valS4080 = _M0L1kS1258->$0;
    if (_M0L3valS4080 < _M0L7n__postS1243) {
      int32_t _M0L3valS4081 = _M0L1kS1258->$0;
      int32_t _M0L3valS4102;
      int32_t _M0L6_2atmpS4101;
      #line 1531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1244, _M0L3valS4081)) {
        struct _M0TPB8MutLocalGiE* _M0L2j2S1259 =
          (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L2j2S1259)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2j2S1259->$0 = 0;
        while (1) {
          int32_t _M0L3valS4082 = _M0L2j2S1259->$0;
          if (_M0L3valS4082 < _M0L6n__preS1241) {
            int32_t _M0L3valS4100 = _M0L2j2S1259->$0;
            int32_t _M0L5startS1260;
            int32_t _M0L3valS4099;
            int32_t _M0L6_2atmpS4098;
            int32_t _M0L5end__S1261;
            struct _M0TPB8MutLocalGiE* _M0L1sS1262;
            int32_t _M0L3valS4097;
            int32_t _M0L6_2atmpS4096;
            #line 1537 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5startS1260
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1250, _M0L3valS4100);
            _M0L3valS4099 = _M0L2j2S1259->$0;
            _M0L6_2atmpS4098 = _M0L3valS4099 + 1;
            #line 1538 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5end__S1261
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1250, _M0L6_2atmpS4098);
            _M0L1sS1262
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1262)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1262->$0 = _M0L5startS1260;
            while (1) {
              int32_t _M0L3valS4083 = _M0L1sS1262->$0;
              if (_M0L3valS4083 < _M0L5end__S1261) {
                int32_t _M0L3valS4086 = _M0L1sS1262->$0;
                int32_t _M0L6_2atmpS4084;
                int32_t _M0L3valS4085;
                int32_t _M0L3valS4095;
                int32_t _M0L6_2atmpS4094;
                #line 1541 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                _M0L6_2atmpS4084
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1254, _M0L3valS4086);
                _M0L3valS4085 = _M0L1kS1258->$0;
                if (_M0L6_2atmpS4084 == _M0L3valS4085) {
                  int32_t _M0L3valS4087 = _M0L1sS1262->$0;
                  int32_t _M0L3valS4093 = _M0L1sS1262->$0;
                  float _M0L6_2atmpS4089;
                  struct _M0TPB5ArrayGfE* _M0L4tpreS4091;
                  int32_t _M0L3valS4092;
                  float _M0L6_2atmpS4090;
                  float _M0L6_2atmpS4088;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4089
                  = _M0MPC15array5Array2atGfE(_M0L1wS1255, _M0L3valS4093);
                  _M0L4tpreS4091 = _M0L4varsS1239->$2;
                  _M0L3valS4092 = _M0L2j2S1259->$0;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4090
                  = _M0MPC15array5Array2atGfE(_M0L4tpreS4091, _M0L3valS4092);
                  _M0L6_2atmpS4088 = _M0L6_2atmpS4089 + _M0L6_2atmpS4090;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0MPC15array5Array3setGfE(_M0L1wS1255, _M0L3valS4087, _M0L6_2atmpS4088);
                }
                _M0L3valS4095 = _M0L1sS1262->$0;
                _M0L6_2atmpS4094 = _M0L3valS4095 + 1;
                _M0L1sS1262->$0 = _M0L6_2atmpS4094;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1262);
              }
              break;
            }
            _M0L3valS4097 = _M0L2j2S1259->$0;
            _M0L6_2atmpS4096 = _M0L3valS4097 + 1;
            _M0L2j2S1259->$0 = _M0L6_2atmpS4096;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L2j2S1259);
          }
          break;
        }
      }
      _M0L3valS4102 = _M0L1kS1258->$0;
      _M0L6_2atmpS4101 = _M0L3valS4102 + 1;
      _M0L1kS1258->$0 = _M0L6_2atmpS4101;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1258);
    }
    break;
  }
  _M0L2jjS1266
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2jjS1266)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2jjS1266->$0 = 0;
  while (1) {
    int32_t _M0L3valS4103 = _M0L2jjS1266->$0;
    if (_M0L3valS4103 < _M0L6n__preS1241) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4104 = _M0L4varsS1239->$2;
      int32_t _M0L3valS4105 = _M0L2jjS1266->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4114 = _M0L4varsS1239->$2;
      int32_t _M0L3valS4115 = _M0L2jjS1266->$0;
      float _M0L6_2atmpS4107;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4112;
      int32_t _M0L3valS4113;
      float _M0L6_2atmpS4111;
      float _M0L6_2atmpS4110;
      float _M0L6_2atmpS4109;
      float _M0L6_2atmpS4108;
      float _M0L6_2atmpS4106;
      int32_t _M0L3valS4117;
      int32_t _M0L6_2atmpS4116;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4107
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4114, _M0L3valS4115);
      _M0L4tpreS4112 = _M0L4varsS1239->$2;
      _M0L3valS4113 = _M0L2jjS1266->$0;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4111
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4112, _M0L3valS4113);
      _M0L6_2atmpS4110 = -_M0L6_2atmpS4111;
      _M0L6_2atmpS4109 = _M0L2dtS1267 * _M0L6_2atmpS4110;
      _M0L6_2atmpS4108 = _M0L6_2atmpS4109 * _M0L13inv__tau__preS1245;
      _M0L6_2atmpS4106 = _M0L6_2atmpS4107 + _M0L6_2atmpS4108;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4104, _M0L3valS4105, _M0L6_2atmpS4106);
      _M0L3valS4117 = _M0L2jjS1266->$0;
      _M0L6_2atmpS4116 = _M0L3valS4117 + 1;
      _M0L2jjS1266->$0 = _M0L6_2atmpS4116;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2jjS1266);
    }
    break;
  }
  _M0L2iiS1269
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2iiS1269)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2iiS1269->$0 = 0;
  while (1) {
    int32_t _M0L3valS4118 = _M0L2iiS1269->$0;
    if (_M0L3valS4118 < _M0L7n__postS1243) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4119 = _M0L4varsS1239->$3;
      int32_t _M0L3valS4120 = _M0L2iiS1269->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4129 = _M0L4varsS1239->$3;
      int32_t _M0L3valS4130 = _M0L2iiS1269->$0;
      float _M0L6_2atmpS4122;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4127;
      int32_t _M0L3valS4128;
      float _M0L6_2atmpS4126;
      float _M0L6_2atmpS4125;
      float _M0L6_2atmpS4124;
      float _M0L6_2atmpS4123;
      float _M0L6_2atmpS4121;
      int32_t _M0L3valS4132;
      int32_t _M0L6_2atmpS4131;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4122
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4129, _M0L3valS4130);
      _M0L5tpostS4127 = _M0L4varsS1239->$3;
      _M0L3valS4128 = _M0L2iiS1269->$0;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4126
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4127, _M0L3valS4128);
      _M0L6_2atmpS4125 = -_M0L6_2atmpS4126;
      _M0L6_2atmpS4124 = _M0L2dtS1267 * _M0L6_2atmpS4125;
      _M0L6_2atmpS4123 = _M0L6_2atmpS4124 * _M0L14inv__tau__postS1247;
      _M0L6_2atmpS4121 = _M0L6_2atmpS4122 + _M0L6_2atmpS4123;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4119, _M0L3valS4120, _M0L6_2atmpS4121);
      _M0L3valS4132 = _M0L2iiS1269->$0;
      _M0L6_2atmpS4131 = _M0L3valS4132 + 1;
      _M0L2iiS1269->$0 = _M0L6_2atmpS4131;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2iiS1269);
    }
    break;
  }
  _M0L3jj2S1271
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3jj2S1271)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3jj2S1271->$0 = 0;
  while (1) {
    int32_t _M0L3valS4133 = _M0L3jj2S1271->$0;
    if (_M0L3valS4133 < _M0L6n__preS1241) {
      int32_t _M0L3valS4134 = _M0L3jj2S1271->$0;
      int32_t _M0L3valS4143;
      int32_t _M0L6_2atmpS4142;
      #line 1565 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1242, _M0L3valS4134)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4135 = _M0L4varsS1239->$2;
        int32_t _M0L3valS4136 = _M0L3jj2S1271->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4140 = _M0L4varsS1239->$2;
        int32_t _M0L3valS4141 = _M0L3jj2S1271->$0;
        float _M0L6_2atmpS4138;
        float _M0L6a__preS4139;
        float _M0L6_2atmpS4137;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4138
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4140, _M0L3valS4141);
        _M0L6a__preS4139 = _M0L5paramS1246->$0;
        _M0L6_2atmpS4137 = _M0L6_2atmpS4138 + _M0L6a__preS4139;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4135, _M0L3valS4136, _M0L6_2atmpS4137);
      }
      _M0L3valS4143 = _M0L3jj2S1271->$0;
      _M0L6_2atmpS4142 = _M0L3valS4143 + 1;
      _M0L3jj2S1271->$0 = _M0L6_2atmpS4142;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3jj2S1271);
    }
    break;
  }
  _M0L3ii2S1273
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3ii2S1273)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3ii2S1273->$0 = 0;
  while (1) {
    int32_t _M0L3valS4144 = _M0L3ii2S1273->$0;
    if (_M0L3valS4144 < _M0L7n__postS1243) {
      int32_t _M0L3valS4145 = _M0L3ii2S1273->$0;
      int32_t _M0L3valS4154;
      int32_t _M0L6_2atmpS4153;
      #line 1572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1244, _M0L3valS4145)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4146 = _M0L4varsS1239->$3;
        int32_t _M0L3valS4147 = _M0L3ii2S1273->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4151 = _M0L4varsS1239->$3;
        int32_t _M0L3valS4152 = _M0L3ii2S1273->$0;
        float _M0L6_2atmpS4149;
        float _M0L7a__postS4150;
        float _M0L6_2atmpS4148;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4149
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4151, _M0L3valS4152);
        _M0L7a__postS4150 = _M0L5paramS1246->$1;
        _M0L6_2atmpS4148 = _M0L6_2atmpS4149 + _M0L7a__postS4150;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4146, _M0L3valS4147, _M0L6_2atmpS4148);
      }
      _M0L3valS4154 = _M0L3ii2S1273->$0;
      _M0L6_2atmpS4153 = _M0L3valS4154 + 1;
      _M0L3ii2S1273->$0 = _M0L6_2atmpS4153;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3ii2S1273);
    }
    break;
  }
  _M0L2s2S1275
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1275)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1275->$0 = 0;
  while (1) {
    int32_t _M0L3valS4155 = _M0L2s2S1275->$0;
    int32_t _M0L6_2atmpS4156;
    #line 1579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4156 = _M0MPC15array5Array6lengthGfE(_M0L1wS1255);
    if (_M0L3valS4155 < _M0L6_2atmpS4156) {
      int32_t _M0L3valS4159 = _M0L2s2S1275->$0;
      float _M0L6_2atmpS4157;
      float _M0L6w__minS4158;
      int32_t _M0L3valS4164;
      float _M0L6_2atmpS4162;
      float _M0L6w__maxS4163;
      int32_t _M0L3valS4168;
      int32_t _M0L6_2atmpS4167;
      #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4157
      = _M0MPC15array5Array2atGfE(_M0L1wS1255, _M0L3valS4159);
      _M0L6w__minS4158 = _M0L5paramS1246->$5;
      if (_M0L6_2atmpS4157 < _M0L6w__minS4158) {
        int32_t _M0L3valS4160 = _M0L2s2S1275->$0;
        float _M0L6w__minS4161 = _M0L5paramS1246->$5;
        #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1255, _M0L3valS4160, _M0L6w__minS4161);
      }
      _M0L3valS4164 = _M0L2s2S1275->$0;
      #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4162
      = _M0MPC15array5Array2atGfE(_M0L1wS1255, _M0L3valS4164);
      _M0L6w__maxS4163 = _M0L5paramS1246->$4;
      if (_M0L6_2atmpS4162 > _M0L6w__maxS4163) {
        int32_t _M0L3valS4165 = _M0L2s2S1275->$0;
        float _M0L6w__maxS4166 = _M0L5paramS1246->$4;
        #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1255, _M0L3valS4165, _M0L6w__maxS4166);
      }
      _M0L3valS4168 = _M0L2s2S1275->$0;
      _M0L6_2atmpS4167 = _M0L3valS4168 + 1;
      _M0L2s2S1275->$0 = _M0L6_2atmpS4167;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1275);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1235,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1208,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1210,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1230,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1223,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS1215,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS1212,
  float _M0L6t__nowS1206,
  float _M0L2dtS1216
) {
  int32_t _M0L6n__preS1207;
  int32_t _M0L7n__postS1209;
  float _M0L6tau__xS4058;
  float _M0L11inv__tau__xS1211;
  float _M0L6tau__yS4057;
  float _M0L11inv__tau__yS1213;
  struct _M0TPB8MutLocalGiE* _M0L1jS1214;
  struct _M0TPB8MutLocalGiE* _M0L1iS1218;
  float _M0L4a__xS4054;
  float _M0L6tau__xS4056;
  float _M0L6_2atmpS4055;
  float _M0L7coef__xS1220;
  float _M0L4a__yS4051;
  float _M0L6tau__yS4053;
  float _M0L6_2atmpS4052;
  float _M0L7coef__yS1221;
  #line 1296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1207 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1208);
  #line 1309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1209 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1210);
  _M0L6tau__xS4058 = _M0L5paramS1212->$2;
  _M0L11inv__tau__xS1211 = 0x1p+0f / _M0L6tau__xS4058;
  _M0L6tau__yS4057 = _M0L5paramS1212->$3;
  _M0L11inv__tau__yS1213 = 0x1p+0f / _M0L6tau__yS4057;
  _M0L1jS1214
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1214)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1214->$0 = 0;
  while (1) {
    int32_t _M0L3valS3928 = _M0L1jS1214->$0;
    if (_M0L3valS3928 < _M0L6n__preS1207) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3929 = _M0L4varsS1215->$0;
      int32_t _M0L3valS3930 = _M0L1jS1214->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3939 = _M0L4varsS1215->$0;
      int32_t _M0L3valS3940 = _M0L1jS1214->$0;
      float _M0L6_2atmpS3932;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3937;
      int32_t _M0L3valS3938;
      float _M0L6_2atmpS3936;
      float _M0L6_2atmpS3935;
      float _M0L6_2atmpS3934;
      float _M0L6_2atmpS3933;
      float _M0L6_2atmpS3931;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3941;
      int32_t _M0L3valS3942;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3951;
      int32_t _M0L3valS3952;
      float _M0L6_2atmpS3944;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3949;
      int32_t _M0L3valS3950;
      float _M0L6_2atmpS3948;
      float _M0L6_2atmpS3947;
      float _M0L6_2atmpS3946;
      float _M0L6_2atmpS3945;
      float _M0L6_2atmpS3943;
      int32_t _M0L3valS3953;
      int32_t _M0L3valS3967;
      int32_t _M0L6_2atmpS3966;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3932
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3939, _M0L3valS3940);
      _M0L5tr__xS3937 = _M0L4varsS1215->$0;
      _M0L3valS3938 = _M0L1jS1214->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3936
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3937, _M0L3valS3938);
      _M0L6_2atmpS3935 = -_M0L6_2atmpS3936;
      _M0L6_2atmpS3934 = _M0L2dtS1216 * _M0L6_2atmpS3935;
      _M0L6_2atmpS3933 = _M0L6_2atmpS3934 * _M0L11inv__tau__xS1211;
      _M0L6_2atmpS3931 = _M0L6_2atmpS3932 + _M0L6_2atmpS3933;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3929, _M0L3valS3930, _M0L6_2atmpS3931);
      _M0L5tr__yS3941 = _M0L4varsS1215->$1;
      _M0L3valS3942 = _M0L1jS1214->$0;
      _M0L5tr__yS3951 = _M0L4varsS1215->$1;
      _M0L3valS3952 = _M0L1jS1214->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3944
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3951, _M0L3valS3952);
      _M0L5tr__yS3949 = _M0L4varsS1215->$1;
      _M0L3valS3950 = _M0L1jS1214->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3948
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3949, _M0L3valS3950);
      _M0L6_2atmpS3947 = -_M0L6_2atmpS3948;
      _M0L6_2atmpS3946 = _M0L2dtS1216 * _M0L6_2atmpS3947;
      _M0L6_2atmpS3945 = _M0L6_2atmpS3946 * _M0L11inv__tau__yS1213;
      _M0L6_2atmpS3943 = _M0L6_2atmpS3944 + _M0L6_2atmpS3945;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__yS3941, _M0L3valS3942, _M0L6_2atmpS3943);
      _M0L3valS3953 = _M0L1jS1214->$0;
      #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1208, _M0L3valS3953)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3954 = _M0L4varsS1215->$0;
        int32_t _M0L3valS3955 = _M0L1jS1214->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3958 = _M0L4varsS1215->$0;
        int32_t _M0L3valS3959 = _M0L1jS1214->$0;
        float _M0L6_2atmpS3957;
        float _M0L6_2atmpS3956;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3960;
        int32_t _M0L3valS3961;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3964;
        int32_t _M0L3valS3965;
        float _M0L6_2atmpS3963;
        float _M0L6_2atmpS3962;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3957
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3958, _M0L3valS3959);
        _M0L6_2atmpS3956 = _M0L6_2atmpS3957 + 0x1p+0f;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3954, _M0L3valS3955, _M0L6_2atmpS3956);
        _M0L5tr__yS3960 = _M0L4varsS1215->$1;
        _M0L3valS3961 = _M0L1jS1214->$0;
        _M0L5tr__yS3964 = _M0L4varsS1215->$1;
        _M0L3valS3965 = _M0L1jS1214->$0;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3963
        = _M0MPC15array5Array2atGfE(_M0L5tr__yS3964, _M0L3valS3965);
        _M0L6_2atmpS3962 = _M0L6_2atmpS3963 + 0x1p+0f;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__yS3960, _M0L3valS3961, _M0L6_2atmpS3962);
      }
      _M0L3valS3967 = _M0L1jS1214->$0;
      _M0L6_2atmpS3966 = _M0L3valS3967 + 1;
      _M0L1jS1214->$0 = _M0L6_2atmpS3966;
      continue;
    }
    break;
  }
  _M0L1iS1218
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1218)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1218->$0 = 0;
  while (1) {
    int32_t _M0L3valS3968 = _M0L1iS1218->$0;
    if (_M0L3valS3968 < _M0L7n__postS1209) {
      struct _M0TPB5ArrayGfE* _M0L5to__xS3969 = _M0L4varsS1215->$2;
      int32_t _M0L3valS3970 = _M0L1iS1218->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3979 = _M0L4varsS1215->$2;
      int32_t _M0L3valS3980 = _M0L1iS1218->$0;
      float _M0L6_2atmpS3972;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3977;
      int32_t _M0L3valS3978;
      float _M0L6_2atmpS3976;
      float _M0L6_2atmpS3975;
      float _M0L6_2atmpS3974;
      float _M0L6_2atmpS3973;
      float _M0L6_2atmpS3971;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3981;
      int32_t _M0L3valS3982;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3991;
      int32_t _M0L3valS3992;
      float _M0L6_2atmpS3984;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3989;
      int32_t _M0L3valS3990;
      float _M0L6_2atmpS3988;
      float _M0L6_2atmpS3987;
      float _M0L6_2atmpS3986;
      float _M0L6_2atmpS3985;
      float _M0L6_2atmpS3983;
      int32_t _M0L3valS3993;
      int32_t _M0L3valS4007;
      int32_t _M0L6_2atmpS4006;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3972
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3979, _M0L3valS3980);
      _M0L5to__xS3977 = _M0L4varsS1215->$2;
      _M0L3valS3978 = _M0L1iS1218->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3976
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3977, _M0L3valS3978);
      _M0L6_2atmpS3975 = -_M0L6_2atmpS3976;
      _M0L6_2atmpS3974 = _M0L2dtS1216 * _M0L6_2atmpS3975;
      _M0L6_2atmpS3973 = _M0L6_2atmpS3974 * _M0L11inv__tau__xS1211;
      _M0L6_2atmpS3971 = _M0L6_2atmpS3972 + _M0L6_2atmpS3973;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__xS3969, _M0L3valS3970, _M0L6_2atmpS3971);
      _M0L5to__yS3981 = _M0L4varsS1215->$3;
      _M0L3valS3982 = _M0L1iS1218->$0;
      _M0L5to__yS3991 = _M0L4varsS1215->$3;
      _M0L3valS3992 = _M0L1iS1218->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3984
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3991, _M0L3valS3992);
      _M0L5to__yS3989 = _M0L4varsS1215->$3;
      _M0L3valS3990 = _M0L1iS1218->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3988
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3989, _M0L3valS3990);
      _M0L6_2atmpS3987 = -_M0L6_2atmpS3988;
      _M0L6_2atmpS3986 = _M0L2dtS1216 * _M0L6_2atmpS3987;
      _M0L6_2atmpS3985 = _M0L6_2atmpS3986 * _M0L11inv__tau__yS1213;
      _M0L6_2atmpS3983 = _M0L6_2atmpS3984 + _M0L6_2atmpS3985;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3981, _M0L3valS3982, _M0L6_2atmpS3983);
      _M0L3valS3993 = _M0L1iS1218->$0;
      #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1210, _M0L3valS3993)) {
        struct _M0TPB5ArrayGfE* _M0L5to__xS3994 = _M0L4varsS1215->$2;
        int32_t _M0L3valS3995 = _M0L1iS1218->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__xS3998 = _M0L4varsS1215->$2;
        int32_t _M0L3valS3999 = _M0L1iS1218->$0;
        float _M0L6_2atmpS3997;
        float _M0L6_2atmpS3996;
        struct _M0TPB5ArrayGfE* _M0L5to__yS4000;
        int32_t _M0L3valS4001;
        struct _M0TPB5ArrayGfE* _M0L5to__yS4004;
        int32_t _M0L3valS4005;
        float _M0L6_2atmpS4003;
        float _M0L6_2atmpS4002;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3997
        = _M0MPC15array5Array2atGfE(_M0L5to__xS3998, _M0L3valS3999);
        _M0L6_2atmpS3996 = _M0L6_2atmpS3997 + 0x1p+0f;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__xS3994, _M0L3valS3995, _M0L6_2atmpS3996);
        _M0L5to__yS4000 = _M0L4varsS1215->$3;
        _M0L3valS4001 = _M0L1iS1218->$0;
        _M0L5to__yS4004 = _M0L4varsS1215->$3;
        _M0L3valS4005 = _M0L1iS1218->$0;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4003
        = _M0MPC15array5Array2atGfE(_M0L5to__yS4004, _M0L3valS4005);
        _M0L6_2atmpS4002 = _M0L6_2atmpS4003 + 0x1p+0f;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS4000, _M0L3valS4001, _M0L6_2atmpS4002);
      }
      _M0L3valS4007 = _M0L1iS1218->$0;
      _M0L6_2atmpS4006 = _M0L3valS4007 + 1;
      _M0L1iS1218->$0 = _M0L6_2atmpS4006;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1218);
    }
    break;
  }
  _M0L4a__xS4054 = _M0L5paramS1212->$0;
  _M0L6tau__xS4056 = _M0L5paramS1212->$2;
  _M0L6_2atmpS4055 = 0x1p+1f * _M0L6tau__xS4056;
  _M0L7coef__xS1220 = _M0L4a__xS4054 / _M0L6_2atmpS4055;
  _M0L4a__yS4051 = _M0L5paramS1212->$1;
  _M0L6tau__yS4053 = _M0L5paramS1212->$3;
  _M0L6_2atmpS4052 = 0x1p+1f * _M0L6tau__yS4053;
  _M0L7coef__yS1221 = _M0L4a__yS4051 / _M0L6_2atmpS4052;
  _M0L1jS1214->$0 = 0;
  while (1) {
    int32_t _M0L3valS4008 = _M0L1jS1214->$0;
    if (_M0L3valS4008 < _M0L6n__preS1207) {
      int32_t _M0L3valS4050 = _M0L1jS1214->$0;
      int32_t _M0L5startS1222;
      int32_t _M0L3valS4049;
      int32_t _M0L6_2atmpS4048;
      int32_t _M0L3endS1224;
      int32_t _M0L3valS4047;
      int32_t _M0L10pre__firedS1225;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS4045;
      int32_t _M0L3valS4046;
      float _M0L8tr__x__jS1226;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS4043;
      int32_t _M0L3valS4044;
      float _M0L8tr__y__jS1227;
      struct _M0TPB8MutLocalGiE* _M0L1sS1228;
      int32_t _M0L3valS4042;
      int32_t _M0L6_2atmpS4041;
      #line 1346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1222
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1223, _M0L3valS4050);
      _M0L3valS4049 = _M0L1jS1214->$0;
      _M0L6_2atmpS4048 = _M0L3valS4049 + 1;
      #line 1347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1224
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1223, _M0L6_2atmpS4048);
      _M0L3valS4047 = _M0L1jS1214->$0;
      #line 1348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1225
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1208, _M0L3valS4047);
      _M0L5tr__xS4045 = _M0L4varsS1215->$0;
      _M0L3valS4046 = _M0L1jS1214->$0;
      #line 1349 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__x__jS1226
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS4045, _M0L3valS4046);
      _M0L5tr__yS4043 = _M0L4varsS1215->$1;
      _M0L3valS4044 = _M0L1jS1214->$0;
      #line 1350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__y__jS1227
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS4043, _M0L3valS4044);
      _M0L1sS1228
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1228)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1228->$0 = _M0L5startS1222;
      while (1) {
        int32_t _M0L3valS4009 = _M0L1sS1228->$0;
        if (_M0L3valS4009 < _M0L3endS1224) {
          int32_t _M0L3valS4040 = _M0L1sS1228->$0;
          int32_t _M0L9post__idxS1229;
          int32_t _M0L11post__firedS1231;
          struct _M0TPB5ArrayGfE* _M0L5to__xS4039;
          float _M0L8to__x__iS1232;
          struct _M0TPB5ArrayGfE* _M0L5to__yS4038;
          float _M0L8to__y__iS1233;
          int32_t _M0L3valS4028;
          float _M0L6_2atmpS4026;
          float _M0L6w__minS4027;
          int32_t _M0L3valS4033;
          float _M0L6_2atmpS4031;
          float _M0L6w__maxS4032;
          int32_t _M0L3valS4037;
          int32_t _M0L6_2atmpS4036;
          #line 1353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1229
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1230, _M0L3valS4040);
          #line 1354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1231
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1210, _M0L9post__idxS1229);
          _M0L5to__xS4039 = _M0L4varsS1215->$2;
          #line 1355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__x__iS1232
          = _M0MPC15array5Array2atGfE(_M0L5to__xS4039, _M0L9post__idxS1229);
          _M0L5to__yS4038 = _M0L4varsS1215->$3;
          #line 1356 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__y__iS1233
          = _M0MPC15array5Array2atGfE(_M0L5to__yS4038, _M0L9post__idxS1229);
          if (_M0L10pre__firedS1225) {
            float _M0L10alpha__preS4016 = _M0L5paramS1212->$4;
            float _M0L6_2atmpS4017 = _M0L7coef__xS1220 * _M0L8to__x__iS1232;
            float _M0L6_2atmpS4014 = _M0L10alpha__preS4016 + _M0L6_2atmpS4017;
            float _M0L6_2atmpS4015 = _M0L7coef__yS1221 * _M0L8to__y__iS1233;
            float _M0L2dwS1234 = _M0L6_2atmpS4014 - _M0L6_2atmpS4015;
            int32_t _M0L3valS4010 = _M0L1sS1228->$0;
            int32_t _M0L3valS4013 = _M0L1sS1228->$0;
            float _M0L6_2atmpS4012;
            float _M0L6_2atmpS4011;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4012
            = _M0MPC15array5Array2atGfE(_M0L1wS1235, _M0L3valS4013);
            _M0L6_2atmpS4011 = _M0L6_2atmpS4012 + _M0L2dwS1234;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1235, _M0L3valS4010, _M0L6_2atmpS4011);
          }
          if (_M0L11post__firedS1231) {
            float _M0L11alpha__postS4024 = _M0L5paramS1212->$5;
            float _M0L6_2atmpS4025 = _M0L7coef__xS1220 * _M0L8tr__x__jS1226;
            float _M0L6_2atmpS4022 =
              _M0L11alpha__postS4024 + _M0L6_2atmpS4025;
            float _M0L6_2atmpS4023 = _M0L7coef__yS1221 * _M0L8tr__y__jS1227;
            float _M0L2dwS1236 = _M0L6_2atmpS4022 - _M0L6_2atmpS4023;
            int32_t _M0L3valS4018 = _M0L1sS1228->$0;
            int32_t _M0L3valS4021 = _M0L1sS1228->$0;
            float _M0L6_2atmpS4020;
            float _M0L6_2atmpS4019;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4020
            = _M0MPC15array5Array2atGfE(_M0L1wS1235, _M0L3valS4021);
            _M0L6_2atmpS4019 = _M0L6_2atmpS4020 + _M0L2dwS1236;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1235, _M0L3valS4018, _M0L6_2atmpS4019);
          }
          _M0L3valS4028 = _M0L1sS1228->$0;
          #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS4026
          = _M0MPC15array5Array2atGfE(_M0L1wS1235, _M0L3valS4028);
          _M0L6w__minS4027 = _M0L5paramS1212->$7;
          if (_M0L6_2atmpS4026 < _M0L6w__minS4027) {
            int32_t _M0L3valS4029 = _M0L1sS1228->$0;
            float _M0L6w__minS4030 = _M0L5paramS1212->$7;
            #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1235, _M0L3valS4029, _M0L6w__minS4030);
          }
          _M0L3valS4033 = _M0L1sS1228->$0;
          #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS4031
          = _M0MPC15array5Array2atGfE(_M0L1wS1235, _M0L3valS4033);
          _M0L6w__maxS4032 = _M0L5paramS1212->$6;
          if (_M0L6_2atmpS4031 > _M0L6w__maxS4032) {
            int32_t _M0L3valS4034 = _M0L1sS1228->$0;
            float _M0L6w__maxS4035 = _M0L5paramS1212->$6;
            #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1235, _M0L3valS4034, _M0L6w__maxS4035);
          }
          _M0L3valS4037 = _M0L1sS1228->$0;
          _M0L6_2atmpS4036 = _M0L3valS4037 + 1;
          _M0L1sS1228->$0 = _M0L6_2atmpS4036;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1228);
        }
        break;
      }
      _M0L3valS4042 = _M0L1jS1214->$0;
      _M0L6_2atmpS4041 = _M0L3valS4042 + 1;
      _M0L1jS1214->$0 = _M0L6_2atmpS4041;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1214);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1203,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1180,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1182,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1200,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1194,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1188,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS1185,
  float _M0L6t__nowS1189,
  float _M0L2dtS1184
) {
  int32_t _M0L6n__preS1179;
  int32_t _M0L7n__postS1181;
  float _M0L6_2atmpS3926;
  float _M0L8tau__preS3927;
  float _M0L6_2atmpS3925;
  float _M0L10decay__preS1183;
  float _M0L6_2atmpS3923;
  float _M0L9tau__postS3924;
  float _M0L6_2atmpS3922;
  float _M0L11decay__postS1186;
  struct _M0TPB8MutLocalGiE* _M0L1jS1187;
  struct _M0TPB8MutLocalGiE* _M0L1iS1191;
  #line 1080 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1091 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1179 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1180);
  #line 1092 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1181 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1182);
  _M0L6_2atmpS3926 = -_M0L2dtS1184;
  _M0L8tau__preS3927 = _M0L5paramS1185->$5;
  _M0L6_2atmpS3925 = _M0L6_2atmpS3926 / _M0L8tau__preS3927;
  #line 1093 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1183 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3925);
  _M0L6_2atmpS3923 = -_M0L2dtS1184;
  _M0L9tau__postS3924 = _M0L5paramS1185->$6;
  _M0L6_2atmpS3922 = _M0L6_2atmpS3923 / _M0L9tau__postS3924;
  #line 1094 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1186 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3922);
  _M0L1jS1187
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1187)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1187->$0 = 0;
  while (1) {
    int32_t _M0L3valS3842 = _M0L1jS1187->$0;
    if (_M0L3valS3842 < _M0L6n__preS1179) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3843 = _M0L4varsS1188->$0;
      int32_t _M0L3valS3844 = _M0L1jS1187->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3847 = _M0L4varsS1188->$0;
      int32_t _M0L3valS3848 = _M0L1jS1187->$0;
      float _M0L6_2atmpS3846;
      float _M0L6_2atmpS3845;
      int32_t _M0L3valS3849;
      int32_t _M0L3valS3859;
      int32_t _M0L6_2atmpS3858;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3846
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3847, _M0L3valS3848);
      _M0L6_2atmpS3845 = _M0L6_2atmpS3846 * _M0L10decay__preS1183;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3843, _M0L3valS3844, _M0L6_2atmpS3845);
      _M0L3valS3849 = _M0L1jS1187->$0;
      #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1180, _M0L3valS3849)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3850 = _M0L4varsS1188->$0;
        int32_t _M0L3valS3851 = _M0L1jS1187->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3854 = _M0L4varsS1188->$0;
        int32_t _M0L3valS3855 = _M0L1jS1187->$0;
        float _M0L6_2atmpS3853;
        float _M0L6_2atmpS3852;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3856;
        int32_t _M0L3valS3857;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3853
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3854, _M0L3valS3855);
        _M0L6_2atmpS3852 = _M0L6_2atmpS3853 + 0x1p+0f;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3850, _M0L3valS3851, _M0L6_2atmpS3852);
        _M0L9last__preS3856 = _M0L4varsS1188->$2;
        _M0L3valS3857 = _M0L1jS1187->$0;
        #line 1101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3856, _M0L3valS3857, _M0L6t__nowS1189);
      }
      _M0L3valS3859 = _M0L1jS1187->$0;
      _M0L6_2atmpS3858 = _M0L3valS3859 + 1;
      _M0L1jS1187->$0 = _M0L6_2atmpS3858;
      continue;
    }
    break;
  }
  _M0L1iS1191
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1191->$0 = 0;
  while (1) {
    int32_t _M0L3valS3860 = _M0L1iS1191->$0;
    if (_M0L3valS3860 < _M0L7n__postS1181) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3861 = _M0L4varsS1188->$1;
      int32_t _M0L3valS3862 = _M0L1iS1191->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3865 = _M0L4varsS1188->$1;
      int32_t _M0L3valS3866 = _M0L1iS1191->$0;
      float _M0L6_2atmpS3864;
      float _M0L6_2atmpS3863;
      int32_t _M0L3valS3867;
      int32_t _M0L3valS3877;
      int32_t _M0L6_2atmpS3876;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3864
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3865, _M0L3valS3866);
      _M0L6_2atmpS3863 = _M0L6_2atmpS3864 * _M0L11decay__postS1186;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3861, _M0L3valS3862, _M0L6_2atmpS3863);
      _M0L3valS3867 = _M0L1iS1191->$0;
      #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1182, _M0L3valS3867)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3868 = _M0L4varsS1188->$1;
        int32_t _M0L3valS3869 = _M0L1iS1191->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3872 = _M0L4varsS1188->$1;
        int32_t _M0L3valS3873 = _M0L1iS1191->$0;
        float _M0L6_2atmpS3871;
        float _M0L6_2atmpS3870;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3874;
        int32_t _M0L3valS3875;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3871
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3872, _M0L3valS3873);
        _M0L6_2atmpS3870 = _M0L6_2atmpS3871 + 0x1p+0f;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3868, _M0L3valS3869, _M0L6_2atmpS3870);
        _M0L10last__postS3874 = _M0L4varsS1188->$3;
        _M0L3valS3875 = _M0L1iS1191->$0;
        #line 1110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3874, _M0L3valS3875, _M0L6t__nowS1189);
      }
      _M0L3valS3877 = _M0L1iS1191->$0;
      _M0L6_2atmpS3876 = _M0L3valS3877 + 1;
      _M0L1iS1191->$0 = _M0L6_2atmpS3876;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1191);
    }
    break;
  }
  _M0L1jS1187->$0 = 0;
  while (1) {
    int32_t _M0L3valS3878 = _M0L1jS1187->$0;
    if (_M0L3valS3878 < _M0L6n__preS1179) {
      int32_t _M0L3valS3921 = _M0L1jS1187->$0;
      int32_t _M0L5startS1193;
      int32_t _M0L3valS3920;
      int32_t _M0L6_2atmpS3919;
      int32_t _M0L3endS1195;
      int32_t _M0L3valS3918;
      int32_t _M0L10pre__firedS1196;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3916;
      int32_t _M0L3valS3917;
      float _M0L7tpre__jS1197;
      struct _M0TPB8MutLocalGiE* _M0L1sS1198;
      int32_t _M0L3valS3915;
      int32_t _M0L6_2atmpS3914;
      #line 1120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1193
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1194, _M0L3valS3921);
      _M0L3valS3920 = _M0L1jS1187->$0;
      _M0L6_2atmpS3919 = _M0L3valS3920 + 1;
      #line 1121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1195
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1194, _M0L6_2atmpS3919);
      _M0L3valS3918 = _M0L1jS1187->$0;
      #line 1122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1196
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1180, _M0L3valS3918);
      _M0L4tpreS3916 = _M0L4varsS1188->$0;
      _M0L3valS3917 = _M0L1jS1187->$0;
      #line 1123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L7tpre__jS1197
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3916, _M0L3valS3917);
      _M0L1sS1198
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1198)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1198->$0 = _M0L5startS1193;
      while (1) {
        int32_t _M0L3valS3879 = _M0L1sS1198->$0;
        if (_M0L3valS3879 < _M0L3endS1195) {
          int32_t _M0L3valS3913 = _M0L1sS1198->$0;
          int32_t _M0L9post__idxS1199;
          int32_t _M0L11post__firedS1201;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3912;
          float _M0L8tpost__iS1202;
          int32_t _M0L3valS3902;
          float _M0L6_2atmpS3900;
          float _M0L6w__minS3901;
          int32_t _M0L3valS3907;
          float _M0L6_2atmpS3905;
          float _M0L6w__maxS3906;
          int32_t _M0L3valS3911;
          int32_t _M0L6_2atmpS3910;
          #line 1126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1199
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1200, _M0L3valS3913);
          #line 1127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1201
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1182, _M0L9post__idxS1199);
          _M0L5tpostS3912 = _M0L4varsS1188->$1;
          #line 1128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8tpost__iS1202
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3912, _M0L9post__idxS1199);
          if (_M0L10pre__firedS1196) {
            int32_t _M0L3valS3880 = _M0L1sS1198->$0;
            int32_t _M0L3valS3889 = _M0L1sS1198->$0;
            float _M0L6_2atmpS3882;
            float _M0L3etaS3884;
            float _M0L5kappaS3888;
            float _M0L6_2atmpS3886;
            float _M0L5alphaS3887;
            float _M0L6_2atmpS3885;
            float _M0L6_2atmpS3883;
            float _M0L6_2atmpS3881;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3882
            = _M0MPC15array5Array2atGfE(_M0L1wS1203, _M0L3valS3889);
            _M0L3etaS3884 = _M0L5paramS1185->$0;
            _M0L5kappaS3888 = _M0L5paramS1185->$3;
            _M0L6_2atmpS3886 = _M0L5kappaS3888 * _M0L8tpost__iS1202;
            _M0L5alphaS3887 = _M0L5paramS1185->$1;
            _M0L6_2atmpS3885 = _M0L6_2atmpS3886 + _M0L5alphaS3887;
            _M0L6_2atmpS3883 = _M0L3etaS3884 * _M0L6_2atmpS3885;
            _M0L6_2atmpS3881 = _M0L6_2atmpS3882 + _M0L6_2atmpS3883;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1203, _M0L3valS3880, _M0L6_2atmpS3881);
          }
          if (_M0L11post__firedS1201) {
            int32_t _M0L3valS3890 = _M0L1sS1198->$0;
            int32_t _M0L3valS3899 = _M0L1sS1198->$0;
            float _M0L6_2atmpS3892;
            float _M0L3etaS3894;
            float _M0L5gammaS3898;
            float _M0L6_2atmpS3896;
            float _M0L4betaS3897;
            float _M0L6_2atmpS3895;
            float _M0L6_2atmpS3893;
            float _M0L6_2atmpS3891;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3892
            = _M0MPC15array5Array2atGfE(_M0L1wS1203, _M0L3valS3899);
            _M0L3etaS3894 = _M0L5paramS1185->$0;
            _M0L5gammaS3898 = _M0L5paramS1185->$4;
            _M0L6_2atmpS3896 = _M0L5gammaS3898 * _M0L7tpre__jS1197;
            _M0L4betaS3897 = _M0L5paramS1185->$2;
            _M0L6_2atmpS3895 = _M0L6_2atmpS3896 + _M0L4betaS3897;
            _M0L6_2atmpS3893 = _M0L3etaS3894 * _M0L6_2atmpS3895;
            _M0L6_2atmpS3891 = _M0L6_2atmpS3892 + _M0L6_2atmpS3893;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1203, _M0L3valS3890, _M0L6_2atmpS3891);
          }
          _M0L3valS3902 = _M0L1sS1198->$0;
          #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3900
          = _M0MPC15array5Array2atGfE(_M0L1wS1203, _M0L3valS3902);
          _M0L6w__minS3901 = _M0L5paramS1185->$8;
          if (_M0L6_2atmpS3900 < _M0L6w__minS3901) {
            int32_t _M0L3valS3903 = _M0L1sS1198->$0;
            float _M0L6w__minS3904 = _M0L5paramS1185->$8;
            #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1203, _M0L3valS3903, _M0L6w__minS3904);
          }
          _M0L3valS3907 = _M0L1sS1198->$0;
          #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3905
          = _M0MPC15array5Array2atGfE(_M0L1wS1203, _M0L3valS3907);
          _M0L6w__maxS3906 = _M0L5paramS1185->$7;
          if (_M0L6_2atmpS3905 > _M0L6w__maxS3906) {
            int32_t _M0L3valS3908 = _M0L1sS1198->$0;
            float _M0L6w__maxS3909 = _M0L5paramS1185->$7;
            #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1203, _M0L3valS3908, _M0L6w__maxS3909);
          }
          _M0L3valS3911 = _M0L1sS1198->$0;
          _M0L6_2atmpS3910 = _M0L3valS3911 + 1;
          _M0L1sS1198->$0 = _M0L6_2atmpS3910;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1198);
        }
        break;
      }
      _M0L3valS3915 = _M0L1jS1187->$0;
      _M0L6_2atmpS3914 = _M0L3valS3915 + 1;
      _M0L1jS1187->$0 = _M0L6_2atmpS3914;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1187);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1176,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1156,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1158,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1173,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1169,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1154,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS1161,
  float _M0L6t__nowS1164,
  float _M0L2dtS1160
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3759;
  int32_t _M0L6_2atmpS3758;
  int32_t _if__result_6042;
  int32_t _M0L6n__preS1155;
  int32_t _M0L7n__postS1157;
  float _M0L6_2atmpS3840;
  float _M0L8tau__preS3841;
  float _M0L6_2atmpS3839;
  float _M0L10decay__preS1159;
  float _M0L6_2atmpS3837;
  float _M0L9tau__postS3838;
  float _M0L6_2atmpS3836;
  float _M0L11decay__postS1162;
  struct _M0TPB8MutLocalGiE* _M0L1jS1163;
  struct _M0TPB8MutLocalGiE* _M0L1iS1166;
  #line 905 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3759 = _M0L4varsS1154->$4;
  #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3758 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3759);
  if (_M0L6_2atmpS3758 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3757 = _M0L4varsS1154->$4;
    int32_t _M0L6_2atmpS3756;
    #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3756 = _M0MPC15array5Array2atGbE(_M0L6activeS3757, 0);
    _if__result_6042 = !_M0L6_2atmpS3756;
  } else {
    _if__result_6042 = 0;
  }
  if (_if__result_6042) {
    return 0;
  }
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1155 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1156);
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1157 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1158);
  _M0L6_2atmpS3840 = -_M0L2dtS1160;
  _M0L8tau__preS3841 = _M0L5paramS1161->$2;
  _M0L6_2atmpS3839 = _M0L6_2atmpS3840 / _M0L8tau__preS3841;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1159 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3839);
  _M0L6_2atmpS3837 = -_M0L2dtS1160;
  _M0L9tau__postS3838 = _M0L5paramS1161->$3;
  _M0L6_2atmpS3836 = _M0L6_2atmpS3837 / _M0L9tau__postS3838;
  #line 924 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1162 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3836);
  _M0L1jS1163
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1163->$0 = 0;
  while (1) {
    int32_t _M0L3valS3760 = _M0L1jS1163->$0;
    if (_M0L3valS3760 < _M0L6n__preS1155) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3761 = _M0L4varsS1154->$0;
      int32_t _M0L3valS3762 = _M0L1jS1163->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3765 = _M0L4varsS1154->$0;
      int32_t _M0L3valS3766 = _M0L1jS1163->$0;
      float _M0L6_2atmpS3764;
      float _M0L6_2atmpS3763;
      int32_t _M0L3valS3767;
      int32_t _M0L3valS3778;
      int32_t _M0L6_2atmpS3777;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3764
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3765, _M0L3valS3766);
      _M0L6_2atmpS3763 = _M0L6_2atmpS3764 * _M0L10decay__preS1159;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3761, _M0L3valS3762, _M0L6_2atmpS3763);
      _M0L3valS3767 = _M0L1jS1163->$0;
      #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1156, _M0L3valS3767)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3768 = _M0L4varsS1154->$0;
        int32_t _M0L3valS3769 = _M0L1jS1163->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3773 = _M0L4varsS1154->$0;
        int32_t _M0L3valS3774 = _M0L1jS1163->$0;
        float _M0L6_2atmpS3771;
        float _M0L6a__preS3772;
        float _M0L6_2atmpS3770;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3775;
        int32_t _M0L3valS3776;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3771
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3773, _M0L3valS3774);
        _M0L6a__preS3772 = _M0L5paramS1161->$0;
        _M0L6_2atmpS3770 = _M0L6_2atmpS3771 + _M0L6a__preS3772;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3768, _M0L3valS3769, _M0L6_2atmpS3770);
        _M0L9last__preS3775 = _M0L4varsS1154->$2;
        _M0L3valS3776 = _M0L1jS1163->$0;
        #line 930 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3775, _M0L3valS3776, _M0L6t__nowS1164);
      }
      _M0L3valS3778 = _M0L1jS1163->$0;
      _M0L6_2atmpS3777 = _M0L3valS3778 + 1;
      _M0L1jS1163->$0 = _M0L6_2atmpS3777;
      continue;
    }
    break;
  }
  _M0L1iS1166
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1166)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1166->$0 = 0;
  while (1) {
    int32_t _M0L3valS3779 = _M0L1iS1166->$0;
    if (_M0L3valS3779 < _M0L7n__postS1157) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3780 = _M0L4varsS1154->$1;
      int32_t _M0L3valS3781 = _M0L1iS1166->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3784 = _M0L4varsS1154->$1;
      int32_t _M0L3valS3785 = _M0L1iS1166->$0;
      float _M0L6_2atmpS3783;
      float _M0L6_2atmpS3782;
      int32_t _M0L3valS3786;
      int32_t _M0L3valS3797;
      int32_t _M0L6_2atmpS3796;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3783
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3784, _M0L3valS3785);
      _M0L6_2atmpS3782 = _M0L6_2atmpS3783 * _M0L11decay__postS1162;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3780, _M0L3valS3781, _M0L6_2atmpS3782);
      _M0L3valS3786 = _M0L1iS1166->$0;
      #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1158, _M0L3valS3786)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3787 = _M0L4varsS1154->$1;
        int32_t _M0L3valS3788 = _M0L1iS1166->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3792 = _M0L4varsS1154->$1;
        int32_t _M0L3valS3793 = _M0L1iS1166->$0;
        float _M0L6_2atmpS3790;
        float _M0L7a__postS3791;
        float _M0L6_2atmpS3789;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3794;
        int32_t _M0L3valS3795;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3790
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3792, _M0L3valS3793);
        _M0L7a__postS3791 = _M0L5paramS1161->$1;
        _M0L6_2atmpS3789 = _M0L6_2atmpS3790 + _M0L7a__postS3791;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3787, _M0L3valS3788, _M0L6_2atmpS3789);
        _M0L10last__postS3794 = _M0L4varsS1154->$3;
        _M0L3valS3795 = _M0L1iS1166->$0;
        #line 939 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3794, _M0L3valS3795, _M0L6t__nowS1164);
      }
      _M0L3valS3797 = _M0L1iS1166->$0;
      _M0L6_2atmpS3796 = _M0L3valS3797 + 1;
      _M0L1iS1166->$0 = _M0L6_2atmpS3796;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1166);
    }
    break;
  }
  _M0L1jS1163->$0 = 0;
  while (1) {
    int32_t _M0L3valS3798 = _M0L1jS1163->$0;
    if (_M0L3valS3798 < _M0L6n__preS1155) {
      int32_t _M0L3valS3835 = _M0L1jS1163->$0;
      int32_t _M0L5startS1168;
      int32_t _M0L3valS3834;
      int32_t _M0L6_2atmpS3833;
      int32_t _M0L3endS1170;
      struct _M0TPB8MutLocalGiE* _M0L1sS1171;
      int32_t _M0L3valS3832;
      int32_t _M0L6_2atmpS3831;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1168
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1169, _M0L3valS3835);
      _M0L3valS3834 = _M0L1jS1163->$0;
      _M0L6_2atmpS3833 = _M0L3valS3834 + 1;
      #line 948 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1170
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1169, _M0L6_2atmpS3833);
      _M0L1sS1171
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1171)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1171->$0 = _M0L5startS1168;
      while (1) {
        int32_t _M0L3valS3799 = _M0L1sS1171->$0;
        if (_M0L3valS3799 < _M0L3endS1170) {
          int32_t _M0L3valS3830 = _M0L1sS1171->$0;
          int32_t _M0L9post__idxS1172;
          int32_t _M0L3valS3829;
          int32_t _M0L10pre__firedS1174;
          int32_t _M0L11post__firedS1175;
          int32_t _M0L3valS3819;
          float _M0L6_2atmpS3817;
          float _M0L6w__minS3818;
          int32_t _M0L3valS3824;
          float _M0L6_2atmpS3822;
          float _M0L6w__maxS3823;
          int32_t _M0L3valS3828;
          int32_t _M0L6_2atmpS3827;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1172
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1173, _M0L3valS3830);
          _M0L3valS3829 = _M0L1jS1163->$0;
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS1174
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1156, _M0L3valS3829);
          #line 953 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1175
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1158, _M0L9post__idxS1172);
          if (_M0L10pre__firedS1174) {
            int32_t _M0L3valS3800 = _M0L1sS1171->$0;
            int32_t _M0L3valS3807 = _M0L1sS1171->$0;
            float _M0L6_2atmpS3802;
            float _M0L7a__postS3804;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3806;
            float _M0L6_2atmpS3805;
            float _M0L6_2atmpS3803;
            float _M0L6_2atmpS3801;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3802
            = _M0MPC15array5Array2atGfE(_M0L1wS1176, _M0L3valS3807);
            _M0L7a__postS3804 = _M0L5paramS1161->$1;
            _M0L5tpostS3806 = _M0L4varsS1154->$1;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3805
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3806, _M0L9post__idxS1172);
            _M0L6_2atmpS3803 = _M0L7a__postS3804 * _M0L6_2atmpS3805;
            _M0L6_2atmpS3801 = _M0L6_2atmpS3802 + _M0L6_2atmpS3803;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1176, _M0L3valS3800, _M0L6_2atmpS3801);
          }
          if (_M0L11post__firedS1175) {
            int32_t _M0L3valS3808 = _M0L1sS1171->$0;
            int32_t _M0L3valS3816 = _M0L1sS1171->$0;
            float _M0L6_2atmpS3810;
            float _M0L6a__preS3812;
            struct _M0TPB5ArrayGfE* _M0L4tpreS3814;
            int32_t _M0L3valS3815;
            float _M0L6_2atmpS3813;
            float _M0L6_2atmpS3811;
            float _M0L6_2atmpS3809;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3810
            = _M0MPC15array5Array2atGfE(_M0L1wS1176, _M0L3valS3816);
            _M0L6a__preS3812 = _M0L5paramS1161->$0;
            _M0L4tpreS3814 = _M0L4varsS1154->$0;
            _M0L3valS3815 = _M0L1jS1163->$0;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3813
            = _M0MPC15array5Array2atGfE(_M0L4tpreS3814, _M0L3valS3815);
            _M0L6_2atmpS3811 = _M0L6a__preS3812 * _M0L6_2atmpS3813;
            _M0L6_2atmpS3809 = _M0L6_2atmpS3810 + _M0L6_2atmpS3811;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1176, _M0L3valS3808, _M0L6_2atmpS3809);
          }
          _M0L3valS3819 = _M0L1sS1171->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3817
          = _M0MPC15array5Array2atGfE(_M0L1wS1176, _M0L3valS3819);
          _M0L6w__minS3818 = _M0L5paramS1161->$5;
          if (_M0L6_2atmpS3817 < _M0L6w__minS3818) {
            int32_t _M0L3valS3820 = _M0L1sS1171->$0;
            float _M0L6w__minS3821 = _M0L5paramS1161->$5;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1176, _M0L3valS3820, _M0L6w__minS3821);
          }
          _M0L3valS3824 = _M0L1sS1171->$0;
          #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3822
          = _M0MPC15array5Array2atGfE(_M0L1wS1176, _M0L3valS3824);
          _M0L6w__maxS3823 = _M0L5paramS1161->$4;
          if (_M0L6_2atmpS3822 > _M0L6w__maxS3823) {
            int32_t _M0L3valS3825 = _M0L1sS1171->$0;
            float _M0L6w__maxS3826 = _M0L5paramS1161->$4;
            #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1176, _M0L3valS3825, _M0L6w__maxS3826);
          }
          _M0L3valS3828 = _M0L1sS1171->$0;
          _M0L6_2atmpS3827 = _M0L3valS3828 + 1;
          _M0L1sS1171->$0 = _M0L6_2atmpS3827;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1171);
        }
        break;
      }
      _M0L3valS3832 = _M0L1jS1163->$0;
      _M0L6_2atmpS3831 = _M0L3valS3832 + 1;
      _M0L1jS1163->$0 = _M0L6_2atmpS3831;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1163);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1133,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1123,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1125,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1132,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1128,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS1135,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS1134,
  float _M0L2dtS1147
) {
  int32_t _M0L6n__preS1122;
  int32_t _M0L7n__postS1124;
  struct _M0TPB8MutLocalGiE* _M0L1jS1126;
  int32_t _M0L3nnzS1138;
  float _M0L4a__xS3754;
  float _M0L6tau__xS3755;
  float _M0L18a__x__over__tau__xS1139;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1140;
  float _M0L6tau__xS3753;
  float _M0L11inv__tau__xS1144;
  float _M0L6tau__yS3752;
  float _M0L11inv__tau__yS1145;
  struct _M0TPB8MutLocalGiE* _M0L1iS1146;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1152;
  #line 622 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 632 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1122 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1123);
  #line 633 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1124 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1125);
  _M0L1jS1126
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1126)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1126->$0 = 0;
  while (1) {
    int32_t _M0L3valS3652 = _M0L1jS1126->$0;
    if (_M0L3valS3652 < _M0L6n__preS1122) {
      int32_t _M0L3valS3653 = _M0L1jS1126->$0;
      int32_t _M0L3valS3674;
      int32_t _M0L6_2atmpS3673;
      #line 637 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1123, _M0L3valS3653)) {
        int32_t _M0L3valS3672 = _M0L1jS1126->$0;
        int32_t _M0L5startS1127;
        int32_t _M0L3valS3671;
        int32_t _M0L6_2atmpS3670;
        int32_t _M0L3endS1129;
        struct _M0TPB8MutLocalGiE* _M0L1sS1130;
        #line 638 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1127
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1128, _M0L3valS3672);
        _M0L3valS3671 = _M0L1jS1126->$0;
        _M0L6_2atmpS3670 = _M0L3valS3671 + 1;
        #line 639 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1129
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1128, _M0L6_2atmpS3670);
        _M0L1sS1130
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1130)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1130->$0 = _M0L5startS1127;
        while (1) {
          int32_t _M0L3valS3654 = _M0L1sS1130->$0;
          if (_M0L3valS3654 < _M0L3endS1129) {
            int32_t _M0L3valS3669 = _M0L1sS1130->$0;
            int32_t _M0L9post__idxS1131;
            int32_t _M0L3valS3655;
            int32_t _M0L3valS3666;
            float _M0L6_2atmpS3664;
            float _M0L10alpha__preS3665;
            float _M0L6_2atmpS3657;
            float _M0L4a__yS3662;
            float _M0L6tau__yS3663;
            float _M0L6_2atmpS3659;
            struct _M0TPB5ArrayGfE* _M0L5to__yS3661;
            float _M0L6_2atmpS3660;
            float _M0L6_2atmpS3658;
            float _M0L6_2atmpS3656;
            int32_t _M0L3valS3668;
            int32_t _M0L6_2atmpS3667;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1131
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1132, _M0L3valS3669);
            _M0L3valS3655 = _M0L1sS1130->$0;
            _M0L3valS3666 = _M0L1sS1130->$0;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3664
            = _M0MPC15array5Array2atGfE(_M0L1wS1133, _M0L3valS3666);
            _M0L10alpha__preS3665 = _M0L5paramS1134->$4;
            _M0L6_2atmpS3657 = _M0L6_2atmpS3664 + _M0L10alpha__preS3665;
            _M0L4a__yS3662 = _M0L5paramS1134->$1;
            _M0L6tau__yS3663 = _M0L5paramS1134->$3;
            _M0L6_2atmpS3659 = _M0L4a__yS3662 / _M0L6tau__yS3663;
            _M0L5to__yS3661 = _M0L4varsS1135->$1;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3660
            = _M0MPC15array5Array2atGfE(_M0L5to__yS3661, _M0L9post__idxS1131);
            _M0L6_2atmpS3658 = _M0L6_2atmpS3659 * _M0L6_2atmpS3660;
            _M0L6_2atmpS3656 = _M0L6_2atmpS3657 - _M0L6_2atmpS3658;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1133, _M0L3valS3655, _M0L6_2atmpS3656);
            _M0L3valS3668 = _M0L1sS1130->$0;
            _M0L6_2atmpS3667 = _M0L3valS3668 + 1;
            _M0L1sS1130->$0 = _M0L6_2atmpS3667;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1130);
          }
          break;
        }
      }
      _M0L3valS3674 = _M0L1jS1126->$0;
      _M0L6_2atmpS3673 = _M0L3valS3674 + 1;
      _M0L1jS1126->$0 = _M0L6_2atmpS3673;
      continue;
    }
    break;
  }
  #line 650 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1138 = _M0MPC15array5Array6lengthGfE(_M0L1wS1133);
  _M0L4a__xS3754 = _M0L5paramS1134->$0;
  _M0L6tau__xS3755 = _M0L5paramS1134->$2;
  _M0L18a__x__over__tau__xS1139 = _M0L4a__xS3754 / _M0L6tau__xS3755;
  _M0L2s2S1140
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1140)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1140->$0 = 0;
  while (1) {
    int32_t _M0L3valS3675 = _M0L2s2S1140->$0;
    if (_M0L3valS3675 < _M0L3nnzS1138) {
      int32_t _M0L3valS3688 = _M0L2s2S1140->$0;
      int32_t _M0L9post__idxS1141;
      int32_t _M0L3valS3687;
      int32_t _M0L6_2atmpS3686;
      #line 654 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1141
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1132, _M0L3valS3688);
      #line 655 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1125, _M0L9post__idxS1141)
      ) {
        int32_t _M0L3valS3685 = _M0L2s2S1140->$0;
        int32_t _M0L6j__preS1142;
        int32_t _M0L3valS3676;
        int32_t _M0L3valS3684;
        float _M0L6_2atmpS3682;
        float _M0L11alpha__postS3683;
        float _M0L6_2atmpS3678;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3681;
        float _M0L6_2atmpS3680;
        float _M0L6_2atmpS3679;
        float _M0L6_2atmpS3677;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1142
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1128, _M0L3valS3685);
        _M0L3valS3676 = _M0L2s2S1140->$0;
        _M0L3valS3684 = _M0L2s2S1140->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3682
        = _M0MPC15array5Array2atGfE(_M0L1wS1133, _M0L3valS3684);
        _M0L11alpha__postS3683 = _M0L5paramS1134->$5;
        _M0L6_2atmpS3678 = _M0L6_2atmpS3682 + _M0L11alpha__postS3683;
        _M0L5tr__xS3681 = _M0L4varsS1135->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3680
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3681, _M0L6j__preS1142);
        _M0L6_2atmpS3679 = _M0L18a__x__over__tau__xS1139 * _M0L6_2atmpS3680;
        _M0L6_2atmpS3677 = _M0L6_2atmpS3678 + _M0L6_2atmpS3679;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1133, _M0L3valS3676, _M0L6_2atmpS3677);
      }
      _M0L3valS3687 = _M0L2s2S1140->$0;
      _M0L6_2atmpS3686 = _M0L3valS3687 + 1;
      _M0L2s2S1140->$0 = _M0L6_2atmpS3686;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1140);
    }
    break;
  }
  _M0L6tau__xS3753 = _M0L5paramS1134->$2;
  _M0L11inv__tau__xS1144 = 0x1p+0f / _M0L6tau__xS3753;
  _M0L6tau__yS3752 = _M0L5paramS1134->$3;
  _M0L11inv__tau__yS1145 = 0x1p+0f / _M0L6tau__yS3752;
  _M0L1iS1146
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1146)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1146->$0 = 0;
  while (1) {
    int32_t _M0L3valS3689 = _M0L1iS1146->$0;
    if (_M0L3valS3689 < _M0L7n__postS1124) {
      struct _M0TPB5ArrayGfE* _M0L5to__yS3690 = _M0L4varsS1135->$1;
      int32_t _M0L3valS3691 = _M0L1iS1146->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3700 = _M0L4varsS1135->$1;
      int32_t _M0L3valS3701 = _M0L1iS1146->$0;
      float _M0L6_2atmpS3693;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3698;
      int32_t _M0L3valS3699;
      float _M0L6_2atmpS3697;
      float _M0L6_2atmpS3696;
      float _M0L6_2atmpS3695;
      float _M0L6_2atmpS3694;
      float _M0L6_2atmpS3692;
      int32_t _M0L3valS3703;
      int32_t _M0L6_2atmpS3702;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3693
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3700, _M0L3valS3701);
      _M0L5to__yS3698 = _M0L4varsS1135->$1;
      _M0L3valS3699 = _M0L1iS1146->$0;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3697
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3698, _M0L3valS3699);
      _M0L6_2atmpS3696 = -_M0L6_2atmpS3697;
      _M0L6_2atmpS3695 = _M0L2dtS1147 * _M0L6_2atmpS3696;
      _M0L6_2atmpS3694 = _M0L6_2atmpS3695 * _M0L11inv__tau__yS1145;
      _M0L6_2atmpS3692 = _M0L6_2atmpS3693 + _M0L6_2atmpS3694;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3690, _M0L3valS3691, _M0L6_2atmpS3692);
      _M0L3valS3703 = _M0L1iS1146->$0;
      _M0L6_2atmpS3702 = _M0L3valS3703 + 1;
      _M0L1iS1146->$0 = _M0L6_2atmpS3702;
      continue;
    }
    break;
  }
  _M0L1jS1126->$0 = 0;
  while (1) {
    int32_t _M0L3valS3704 = _M0L1jS1126->$0;
    if (_M0L3valS3704 < _M0L6n__preS1122) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3705 = _M0L4varsS1135->$0;
      int32_t _M0L3valS3706 = _M0L1jS1126->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3715 = _M0L4varsS1135->$0;
      int32_t _M0L3valS3716 = _M0L1jS1126->$0;
      float _M0L6_2atmpS3708;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3713;
      int32_t _M0L3valS3714;
      float _M0L6_2atmpS3712;
      float _M0L6_2atmpS3711;
      float _M0L6_2atmpS3710;
      float _M0L6_2atmpS3709;
      float _M0L6_2atmpS3707;
      int32_t _M0L3valS3718;
      int32_t _M0L6_2atmpS3717;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3708
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3715, _M0L3valS3716);
      _M0L5tr__xS3713 = _M0L4varsS1135->$0;
      _M0L3valS3714 = _M0L1jS1126->$0;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3712
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3713, _M0L3valS3714);
      _M0L6_2atmpS3711 = -_M0L6_2atmpS3712;
      _M0L6_2atmpS3710 = _M0L2dtS1147 * _M0L6_2atmpS3711;
      _M0L6_2atmpS3709 = _M0L6_2atmpS3710 * _M0L11inv__tau__xS1144;
      _M0L6_2atmpS3707 = _M0L6_2atmpS3708 + _M0L6_2atmpS3709;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3705, _M0L3valS3706, _M0L6_2atmpS3707);
      _M0L3valS3718 = _M0L1jS1126->$0;
      _M0L6_2atmpS3717 = _M0L3valS3718 + 1;
      _M0L1jS1126->$0 = _M0L6_2atmpS3717;
      continue;
    }
    break;
  }
  _M0L1iS1146->$0 = 0;
  while (1) {
    int32_t _M0L3valS3719 = _M0L1iS1146->$0;
    if (_M0L3valS3719 < _M0L7n__postS1124) {
      int32_t _M0L3valS3720 = _M0L1iS1146->$0;
      int32_t _M0L3valS3728;
      int32_t _M0L6_2atmpS3727;
      #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1125, _M0L3valS3720)) {
        struct _M0TPB5ArrayGfE* _M0L5to__yS3721 = _M0L4varsS1135->$1;
        int32_t _M0L3valS3722 = _M0L1iS1146->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3725 = _M0L4varsS1135->$1;
        int32_t _M0L3valS3726 = _M0L1iS1146->$0;
        float _M0L6_2atmpS3724;
        float _M0L6_2atmpS3723;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3724
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3725, _M0L3valS3726);
        _M0L6_2atmpS3723 = _M0L6_2atmpS3724 + 0x1p+0f;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3721, _M0L3valS3722, _M0L6_2atmpS3723);
      }
      _M0L3valS3728 = _M0L1iS1146->$0;
      _M0L6_2atmpS3727 = _M0L3valS3728 + 1;
      _M0L1iS1146->$0 = _M0L6_2atmpS3727;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1146);
    }
    break;
  }
  _M0L1jS1126->$0 = 0;
  while (1) {
    int32_t _M0L3valS3729 = _M0L1jS1126->$0;
    if (_M0L3valS3729 < _M0L6n__preS1122) {
      int32_t _M0L3valS3730 = _M0L1jS1126->$0;
      int32_t _M0L3valS3738;
      int32_t _M0L6_2atmpS3737;
      #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1123, _M0L3valS3730)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3731 = _M0L4varsS1135->$0;
        int32_t _M0L3valS3732 = _M0L1jS1126->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3735 = _M0L4varsS1135->$0;
        int32_t _M0L3valS3736 = _M0L1jS1126->$0;
        float _M0L6_2atmpS3734;
        float _M0L6_2atmpS3733;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3734
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3735, _M0L3valS3736);
        _M0L6_2atmpS3733 = _M0L6_2atmpS3734 + 0x1p+0f;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3731, _M0L3valS3732, _M0L6_2atmpS3733);
      }
      _M0L3valS3738 = _M0L1jS1126->$0;
      _M0L6_2atmpS3737 = _M0L3valS3738 + 1;
      _M0L1jS1126->$0 = _M0L6_2atmpS3737;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1126);
    }
    break;
  }
  _M0L2s3S1152
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1152)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1152->$0 = 0;
  while (1) {
    int32_t _M0L3valS3739 = _M0L2s3S1152->$0;
    if (_M0L3valS3739 < _M0L3nnzS1138) {
      int32_t _M0L3valS3742 = _M0L2s3S1152->$0;
      float _M0L6_2atmpS3740;
      float _M0L6w__minS3741;
      int32_t _M0L3valS3751;
      int32_t _M0L6_2atmpS3750;
      #line 692 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3740
      = _M0MPC15array5Array2atGfE(_M0L1wS1133, _M0L3valS3742);
      _M0L6w__minS3741 = _M0L5paramS1134->$7;
      if (_M0L6_2atmpS3740 < _M0L6w__minS3741) {
        int32_t _M0L3valS3743 = _M0L2s3S1152->$0;
        float _M0L6w__minS3744 = _M0L5paramS1134->$7;
        #line 693 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1133, _M0L3valS3743, _M0L6w__minS3744);
      } else {
        int32_t _M0L3valS3747 = _M0L2s3S1152->$0;
        float _M0L6_2atmpS3745;
        float _M0L6w__maxS3746;
        #line 694 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3745
        = _M0MPC15array5Array2atGfE(_M0L1wS1133, _M0L3valS3747);
        _M0L6w__maxS3746 = _M0L5paramS1134->$6;
        if (_M0L6_2atmpS3745 > _M0L6w__maxS3746) {
          int32_t _M0L3valS3748 = _M0L2s3S1152->$0;
          float _M0L6w__maxS3749 = _M0L5paramS1134->$6;
          #line 695 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1133, _M0L3valS3748, _M0L6w__maxS3749);
        }
      }
      _M0L3valS3751 = _M0L2s3S1152->$0;
      _M0L6_2atmpS3750 = _M0L3valS3751 + 1;
      _M0L2s3S1152->$0 = _M0L6_2atmpS3750;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1152);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1108,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1084,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1086,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1103,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1099,
  struct _M0TPB5ArrayGfE* _M0L4tpreS1094,
  struct _M0TPB5ArrayGfE* _M0L5tpostS1090,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS1088,
  float _M0L2dtS1091
) {
  int32_t _M0L6n__preS1083;
  int32_t _M0L7n__postS1085;
  float _M0L3tauS3651;
  float _M0L8inv__tauS1087;
  struct _M0TPB8MutLocalGiE* _M0L1iS1089;
  struct _M0TPB8MutLocalGiE* _M0L1jS1093;
  int32_t _M0L3nnzS1111;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1112;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1120;
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 461 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1083 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1084);
  #line 462 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1085 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1086);
  _M0L3tauS3651 = _M0L5paramS1088->$1;
  _M0L8inv__tauS1087 = 0x1p+0f / _M0L3tauS3651;
  _M0L1iS1089
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1089)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1089->$0 = 0;
  while (1) {
    int32_t _M0L3valS3565 = _M0L1iS1089->$0;
    if (_M0L3valS3565 < _M0L7n__postS1085) {
      int32_t _M0L3valS3566 = _M0L1iS1089->$0;
      int32_t _M0L3valS3574 = _M0L1iS1089->$0;
      float _M0L6_2atmpS3568;
      int32_t _M0L3valS3573;
      float _M0L6_2atmpS3572;
      float _M0L6_2atmpS3571;
      float _M0L6_2atmpS3570;
      float _M0L6_2atmpS3569;
      float _M0L6_2atmpS3567;
      int32_t _M0L3valS3576;
      int32_t _M0L6_2atmpS3575;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3568
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1090, _M0L3valS3574);
      _M0L3valS3573 = _M0L1iS1089->$0;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3572
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1090, _M0L3valS3573);
      _M0L6_2atmpS3571 = -_M0L6_2atmpS3572;
      _M0L6_2atmpS3570 = _M0L2dtS1091 * _M0L6_2atmpS3571;
      _M0L6_2atmpS3569 = _M0L6_2atmpS3570 * _M0L8inv__tauS1087;
      _M0L6_2atmpS3567 = _M0L6_2atmpS3568 + _M0L6_2atmpS3569;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS1090, _M0L3valS3566, _M0L6_2atmpS3567);
      _M0L3valS3576 = _M0L1iS1089->$0;
      _M0L6_2atmpS3575 = _M0L3valS3576 + 1;
      _M0L1iS1089->$0 = _M0L6_2atmpS3575;
      continue;
    }
    break;
  }
  _M0L1jS1093
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1093)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1093->$0 = 0;
  while (1) {
    int32_t _M0L3valS3577 = _M0L1jS1093->$0;
    if (_M0L3valS3577 < _M0L6n__preS1083) {
      int32_t _M0L3valS3578 = _M0L1jS1093->$0;
      int32_t _M0L3valS3586 = _M0L1jS1093->$0;
      float _M0L6_2atmpS3580;
      int32_t _M0L3valS3585;
      float _M0L6_2atmpS3584;
      float _M0L6_2atmpS3583;
      float _M0L6_2atmpS3582;
      float _M0L6_2atmpS3581;
      float _M0L6_2atmpS3579;
      int32_t _M0L3valS3588;
      int32_t _M0L6_2atmpS3587;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3580
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1094, _M0L3valS3586);
      _M0L3valS3585 = _M0L1jS1093->$0;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3584
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1094, _M0L3valS3585);
      _M0L6_2atmpS3583 = -_M0L6_2atmpS3584;
      _M0L6_2atmpS3582 = _M0L2dtS1091 * _M0L6_2atmpS3583;
      _M0L6_2atmpS3581 = _M0L6_2atmpS3582 * _M0L8inv__tauS1087;
      _M0L6_2atmpS3579 = _M0L6_2atmpS3580 + _M0L6_2atmpS3581;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS1094, _M0L3valS3578, _M0L6_2atmpS3579);
      _M0L3valS3588 = _M0L1jS1093->$0;
      _M0L6_2atmpS3587 = _M0L3valS3588 + 1;
      _M0L1jS1093->$0 = _M0L6_2atmpS3587;
      continue;
    }
    break;
  }
  _M0L1iS1089->$0 = 0;
  while (1) {
    int32_t _M0L3valS3589 = _M0L1iS1089->$0;
    if (_M0L3valS3589 < _M0L7n__postS1085) {
      int32_t _M0L3valS3590 = _M0L1iS1089->$0;
      int32_t _M0L3valS3596;
      int32_t _M0L6_2atmpS3595;
      #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1086, _M0L3valS3590)) {
        int32_t _M0L3valS3591 = _M0L1iS1089->$0;
        int32_t _M0L3valS3594 = _M0L1iS1089->$0;
        float _M0L6_2atmpS3593;
        float _M0L6_2atmpS3592;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3593
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1090, _M0L3valS3594);
        _M0L6_2atmpS3592 = _M0L6_2atmpS3593 + 0x1p+0f;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS1090, _M0L3valS3591, _M0L6_2atmpS3592);
      }
      _M0L3valS3596 = _M0L1iS1089->$0;
      _M0L6_2atmpS3595 = _M0L3valS3596 + 1;
      _M0L1iS1089->$0 = _M0L6_2atmpS3595;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1089);
    }
    break;
  }
  _M0L1jS1093->$0 = 0;
  while (1) {
    int32_t _M0L3valS3597 = _M0L1jS1093->$0;
    if (_M0L3valS3597 < _M0L6n__preS1083) {
      int32_t _M0L3valS3598 = _M0L1jS1093->$0;
      int32_t _M0L3valS3604;
      int32_t _M0L6_2atmpS3603;
      #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1084, _M0L3valS3598)) {
        int32_t _M0L3valS3599 = _M0L1jS1093->$0;
        int32_t _M0L3valS3602 = _M0L1jS1093->$0;
        float _M0L6_2atmpS3601;
        float _M0L6_2atmpS3600;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3601
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1094, _M0L3valS3602);
        _M0L6_2atmpS3600 = _M0L6_2atmpS3601 + 0x1p+0f;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS1094, _M0L3valS3599, _M0L6_2atmpS3600);
      }
      _M0L3valS3604 = _M0L1jS1093->$0;
      _M0L6_2atmpS3603 = _M0L3valS3604 + 1;
      _M0L1jS1093->$0 = _M0L6_2atmpS3603;
      continue;
    }
    break;
  }
  _M0L1jS1093->$0 = 0;
  while (1) {
    int32_t _M0L3valS3605 = _M0L1jS1093->$0;
    if (_M0L3valS3605 < _M0L6n__preS1083) {
      int32_t _M0L3valS3606 = _M0L1jS1093->$0;
      int32_t _M0L3valS3624;
      int32_t _M0L6_2atmpS3623;
      #line 493 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1084, _M0L3valS3606)) {
        int32_t _M0L3valS3622 = _M0L1jS1093->$0;
        int32_t _M0L5startS1098;
        int32_t _M0L3valS3621;
        int32_t _M0L6_2atmpS3620;
        int32_t _M0L3endS1100;
        struct _M0TPB8MutLocalGiE* _M0L1sS1101;
        #line 494 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1098
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1099, _M0L3valS3622);
        _M0L3valS3621 = _M0L1jS1093->$0;
        _M0L6_2atmpS3620 = _M0L3valS3621 + 1;
        #line 495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1100
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1099, _M0L6_2atmpS3620);
        _M0L1sS1101
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1101)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1101->$0 = _M0L5startS1098;
        while (1) {
          int32_t _M0L3valS3607 = _M0L1sS1101->$0;
          if (_M0L3valS3607 < _M0L3endS1100) {
            int32_t _M0L3valS3619 = _M0L1sS1101->$0;
            int32_t _M0L9post__idxS1102;
            int32_t _M0L3valS3618;
            float _M0L6_2atmpS3616;
            float _M0L6_2atmpS3617;
            float _M0L5ratioS1104;
            float _M0L3lnxS1105;
            float _M0L1xS1106;
            float _M0L1aS3614;
            float _M0L6_2atmpS3615;
            float _M0L2dwS1107;
            int32_t _M0L3valS3608;
            int32_t _M0L3valS3611;
            float _M0L6_2atmpS3610;
            float _M0L6_2atmpS3609;
            int32_t _M0L3valS3613;
            int32_t _M0L6_2atmpS3612;
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1102
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1103, _M0L3valS3619);
            _M0L3valS3618 = _M0L1jS1093->$0;
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3616
            = _M0MPC15array5Array2atGfE(_M0L4tpreS1094, _M0L3valS3618);
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3617
            = _M0MPC15array5Array2atGfE(_M0L5tpostS1090, _M0L9post__idxS1102);
            _M0L5ratioS1104 = _M0L6_2atmpS3616 / _M0L6_2atmpS3617;
            #line 500 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L3lnxS1105 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1104);
            _M0L1xS1106 = _M0L3lnxS1105 * _M0L3lnxS1105;
            _M0L1aS3614 = _M0L5paramS1088->$0;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3615
            = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1106);
            _M0L2dwS1107 = _M0L1aS3614 * _M0L6_2atmpS3615;
            _M0L3valS3608 = _M0L1sS1101->$0;
            _M0L3valS3611 = _M0L1sS1101->$0;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3610
            = _M0MPC15array5Array2atGfE(_M0L1wS1108, _M0L3valS3611);
            _M0L6_2atmpS3609 = _M0L6_2atmpS3610 + _M0L2dwS1107;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1108, _M0L3valS3608, _M0L6_2atmpS3609);
            _M0L3valS3613 = _M0L1sS1101->$0;
            _M0L6_2atmpS3612 = _M0L3valS3613 + 1;
            _M0L1sS1101->$0 = _M0L6_2atmpS3612;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1101);
          }
          break;
        }
      }
      _M0L3valS3624 = _M0L1jS1093->$0;
      _M0L6_2atmpS3623 = _M0L3valS3624 + 1;
      _M0L1jS1093->$0 = _M0L6_2atmpS3623;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1093);
    }
    break;
  }
  #line 511 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1111 = _M0MPC15array5Array6lengthGfE(_M0L1wS1108);
  _M0L2s2S1112
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1112)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1112->$0 = 0;
  while (1) {
    int32_t _M0L3valS3625 = _M0L2s2S1112->$0;
    if (_M0L3valS3625 < _M0L3nnzS1111) {
      int32_t _M0L3valS3637 = _M0L2s2S1112->$0;
      int32_t _M0L9post__idxS1113;
      int32_t _M0L3valS3636;
      int32_t _M0L6_2atmpS3635;
      #line 514 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1113
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1103, _M0L3valS3637);
      #line 515 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1086, _M0L9post__idxS1113)
      ) {
        int32_t _M0L3valS3634 = _M0L2s2S1112->$0;
        int32_t _M0L6j__preS1114;
        float _M0L6_2atmpS3632;
        float _M0L6_2atmpS3633;
        float _M0L5ratioS1115;
        float _M0L3lnxS1116;
        float _M0L1xS1117;
        float _M0L1aS3630;
        float _M0L6_2atmpS3631;
        float _M0L2dwS1118;
        int32_t _M0L3valS3626;
        int32_t _M0L3valS3629;
        float _M0L6_2atmpS3628;
        float _M0L6_2atmpS3627;
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1114
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1099, _M0L3valS3634);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3632
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1094, _M0L6j__preS1114);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3633
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1090, _M0L9post__idxS1113);
        _M0L5ratioS1115 = _M0L6_2atmpS3632 / _M0L6_2atmpS3633;
        #line 520 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3lnxS1116 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1115);
        _M0L1xS1117 = _M0L3lnxS1116 * _M0L3lnxS1116;
        _M0L1aS3630 = _M0L5paramS1088->$0;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3631
        = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1117);
        _M0L2dwS1118 = _M0L1aS3630 * _M0L6_2atmpS3631;
        _M0L3valS3626 = _M0L2s2S1112->$0;
        _M0L3valS3629 = _M0L2s2S1112->$0;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3628
        = _M0MPC15array5Array2atGfE(_M0L1wS1108, _M0L3valS3629);
        _M0L6_2atmpS3627 = _M0L6_2atmpS3628 + _M0L2dwS1118;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1108, _M0L3valS3626, _M0L6_2atmpS3627);
      }
      _M0L3valS3636 = _M0L2s2S1112->$0;
      _M0L6_2atmpS3635 = _M0L3valS3636 + 1;
      _M0L2s2S1112->$0 = _M0L6_2atmpS3635;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1112);
    }
    break;
  }
  _M0L2s3S1120
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1120)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1120->$0 = 0;
  while (1) {
    int32_t _M0L3valS3638 = _M0L2s3S1120->$0;
    if (_M0L3valS3638 < _M0L3nnzS1111) {
      int32_t _M0L3valS3641 = _M0L2s3S1120->$0;
      float _M0L6_2atmpS3639;
      float _M0L6w__minS3640;
      int32_t _M0L3valS3650;
      int32_t _M0L6_2atmpS3649;
      #line 530 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3639
      = _M0MPC15array5Array2atGfE(_M0L1wS1108, _M0L3valS3641);
      _M0L6w__minS3640 = _M0L5paramS1088->$3;
      if (_M0L6_2atmpS3639 < _M0L6w__minS3640) {
        int32_t _M0L3valS3642 = _M0L2s3S1120->$0;
        float _M0L6w__minS3643 = _M0L5paramS1088->$3;
        #line 531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1108, _M0L3valS3642, _M0L6w__minS3643);
      } else {
        int32_t _M0L3valS3646 = _M0L2s3S1120->$0;
        float _M0L6_2atmpS3644;
        float _M0L6w__maxS3645;
        #line 532 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3644
        = _M0MPC15array5Array2atGfE(_M0L1wS1108, _M0L3valS3646);
        _M0L6w__maxS3645 = _M0L5paramS1088->$2;
        if (_M0L6_2atmpS3644 > _M0L6w__maxS3645) {
          int32_t _M0L3valS3647 = _M0L2s3S1120->$0;
          float _M0L6w__maxS3648 = _M0L5paramS1088->$2;
          #line 533 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1108, _M0L3valS3647, _M0L6w__maxS3648);
        }
      }
      _M0L3valS3650 = _M0L2s3S1120->$0;
      _M0L6_2atmpS3649 = _M0L3valS3650 + 1;
      _M0L2s3S1120->$0 = _M0L6_2atmpS3649;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1120);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1077,
  int32_t _M0L1sS1081
) {
  int32_t _M0L6_2atmpS3564;
  int32_t _M0L1nS1076;
  struct _M0TPB8MutLocalGiE* _M0L2loS1078;
  struct _M0TPB8MutLocalGiE* _M0L2hiS1079;
  int32_t _result_6064;
  #line 542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 543 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3564 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS1077);
  _M0L1nS1076 = _M0L6_2atmpS3564 - 1;
  _M0L2loS1078
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2loS1078)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2loS1078->$0 = 0;
  _M0L2hiS1079
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2hiS1079)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2hiS1079->$0 = _M0L1nS1076;
  while (1) {
    int32_t _M0L3valS3556 = _M0L2loS1078->$0;
    int32_t _M0L3valS3557 = _M0L2hiS1079->$0;
    if (_M0L3valS3556 < _M0L3valS3557) {
      int32_t _M0L3valS3562 = _M0L2loS1078->$0;
      int32_t _M0L3valS3563 = _M0L2hiS1079->$0;
      int32_t _M0L6_2atmpS3561 = _M0L3valS3562 + _M0L3valS3563;
      int32_t _M0L6_2atmpS3560 = _M0L6_2atmpS3561 + 1;
      int32_t _M0L3midS1080 = _M0L6_2atmpS3560 / 2;
      int32_t _M0L6_2atmpS3558;
      #line 548 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3558
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1077, _M0L3midS1080);
      if (_M0L6_2atmpS3558 <= _M0L1sS1081) {
        _M0L2loS1078->$0 = _M0L3midS1080;
      } else {
        int32_t _M0L6_2atmpS3559 = _M0L3midS1080 - 1;
        _M0L2hiS1079->$0 = _M0L6_2atmpS3559;
      }
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2hiS1079);
    }
    break;
  }
  _result_6064 = _M0L2loS1078->$0;
  moonbit_decref_cycle_free(_M0L2loS1078);
  return _result_6064;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS1073) {
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 428 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS1073)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS3555 = -_M0L1xS1073;
    float _M0L3argS1074 = _M0L6_2atmpS3555 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS3553 = 0x1p+0f - _M0L1xS1073;
    float _M0L6_2atmpS3554;
    float _M0L1vS1075;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3554 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS1074);
    _M0L1vS1075 = _M0L6_2atmpS3553 * _M0L6_2atmpS3554;
    #line 433 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS1075)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS1075;
    }
  }
}

int32_t _M0FP26RiantR8snn__mbt19stimulate__balanced(
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1sS1040,
  float _M0L4timeS1038,
  float _M0L2dtS1049
) {
  int32_t _M0L1nS1039;
  struct _M0TP26RiantR8snn__mbt17BalancedParameter* _M0L5paramS1041;
  float _M0L3kIES1042;
  float _M0L4betaS1043;
  float _M0L3tauS1044;
  float _M0L2r0S1045;
  float _M0L1wS1046;
  float _M0L3wIES1047;
  float _M0L6_2atmpS3552;
  float _M0L11inh__lambdaS1048;
  int32_t _M0L7_2abindS1050;
  int32_t _M0L1kS1051;
  float _M0L6_2atmpS3551;
  float _M0L2ccS1055;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
  _M0L1nS1039 = _M0L1sS1040->$1;
  _M0L5paramS1041 = _M0L1sS1040->$0;
  _M0L3kIES1042 = _M0L5paramS1041->$0;
  _M0L4betaS1043 = _M0L5paramS1041->$1;
  _M0L3tauS1044 = _M0L5paramS1041->$2;
  _M0L2r0S1045 = _M0L5paramS1041->$3;
  _M0L1wS1046 = _M0L5paramS1041->$4;
  _M0L3wIES1047 = _M0L5paramS1041->$5;
  _M0L6_2atmpS3552 = _M0L2r0S1045 * _M0L3kIES1042;
  _M0L11inh__lambdaS1048 = _M0L6_2atmpS3552 * _M0L2dtS1049;
  _M0L7_2abindS1050 = 0;
  _M0L1kS1051 = _M0L7_2abindS1050;
  while (1) {
    if (_M0L1kS1051 < _M0L1nS1039) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3465 = _M0L1sS1040->$4;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3473;
      int32_t _M0L1mS1054;
      int32_t _M0L6_2atmpS3464;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3465, _M0L1kS1051, 0);
      if (_M0L11inh__lambdaS1048 <= 0x0p+0f) {
        goto join_1052;
      }
      _M0L3rngS3473 = _M0L1sS1040->$7;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0L1mS1054
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3473, _M0L11inh__lambdaS1048);
      if (_M0L1mS1054 > 0) {
        struct _M0TPB5ArrayGfE* _M0L2giS3466 = _M0L1sS1040->$3;
        struct _M0TPB5ArrayGfE* _M0L2giS3472 = _M0L1sS1040->$3;
        float _M0L6_2atmpS3468;
        float _M0L6_2atmpS3471;
        float _M0L6_2atmpS3470;
        float _M0L6_2atmpS3469;
        float _M0L6_2atmpS3467;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3468
        = _M0MPC15array5Array2atGfE(_M0L2giS3472, _M0L1kS1051);
        _M0L6_2atmpS3471 = (float)_M0L1mS1054;
        _M0L6_2atmpS3470 = _M0L1wS1046 * _M0L6_2atmpS3471;
        _M0L6_2atmpS3469 = _M0L6_2atmpS3470 * _M0L3wIES1047;
        _M0L6_2atmpS3467 = _M0L6_2atmpS3468 + _M0L6_2atmpS3469;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L2giS3466, _M0L1kS1051, _M0L6_2atmpS3467);
      }
      goto join_1052;
      goto joinlet_6066;
      join_1052:;
      _M0L6_2atmpS3464 = _M0L1kS1051 + 1;
      _M0L1kS1051 = _M0L6_2atmpS3464;
      continue;
      joinlet_6066:;
    }
    break;
  }
  _M0L6_2atmpS3551 = _M0L2dtS1049 / _M0L3tauS1044;
  _M0L2ccS1055 = 0x1p+0f - _M0L6_2atmpS3551;
  if (_M0L5paramS1041->$6) {
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3511 = _M0L1sS1040->$7;
    double _M0L6_2atmpS3510;
    float _M0L6_2atmpS3509;
    float _M0L2reS1056;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3474;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3479;
    float _M0L6_2atmpS3478;
    float _M0L6_2atmpS3477;
    float _M0L6_2atmpS3476;
    float _M0L6_2atmpS3475;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3508;
    float _M0L6_2atmpS3507;
    float _M0L6_2atmpS3506;
    struct _M0TPB8MutLocalGfE* _M0L2nbS1057;
    float _M0L3valS3480;
    float _M0L3valS3481;
    float _M0L6_2atmpS3504;
    float _M0L3valS3505;
    float _M0L6_2atmpS3501;
    struct _M0TPB5ArrayGfE* _M0L1rS3503;
    float _M0L6_2atmpS3502;
    float _M0L6_2atmpS3500;
    struct _M0TPB8MutLocalGfE* _M0L5erateS1058;
    float _M0L3valS3482;
    struct _M0TPB5ArrayGfE* _M0L1rS3483;
    struct _M0TPB5ArrayGfE* _M0L1rS3490;
    float _M0L6_2atmpS3485;
    float _M0L3valS3489;
    float _M0L6_2atmpS3488;
    float _M0L6_2atmpS3487;
    float _M0L6_2atmpS3486;
    float _M0L6_2atmpS3484;
    float _M0L3valS3499;
    float _M0L11exc__lambdaS1059;
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3498;
    int32_t _M0L1mS1060;
    #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3510 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3511);
    _M0L6_2atmpS3509 = (float)_M0L6_2atmpS3510;
    _M0L2reS1056 = _M0L6_2atmpS3509 - 0x1p-1f;
    _M0L5noiseS3474 = _M0L1sS1040->$6;
    _M0L5noiseS3479 = _M0L1sS1040->$6;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3478 = _M0MPC15array5Array2atGfE(_M0L5noiseS3479, 0);
    _M0L6_2atmpS3477 = _M0L6_2atmpS3478 - _M0L2reS1056;
    _M0L6_2atmpS3476 = _M0L6_2atmpS3477 * _M0L2ccS1055;
    _M0L6_2atmpS3475 = _M0L6_2atmpS3476 + _M0L2reS1056;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L5noiseS3474, 0, _M0L6_2atmpS3475);
    _M0L5noiseS3508 = _M0L1sS1040->$6;
    #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3507 = _M0MPC15array5Array2atGfE(_M0L5noiseS3508, 0);
    _M0L6_2atmpS3506 = _M0L6_2atmpS3507 * _M0L4betaS1043;
    _M0L2nbS1057
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L2nbS1057)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L2nbS1057->$0 = _M0L6_2atmpS3506;
    _M0L3valS3480 = _M0L2nbS1057->$0;
    if (_M0L3valS3480 > 0x1p+0f) {
      _M0L2nbS1057->$0 = 0x1p+0f;
    }
    _M0L3valS3481 = _M0L2nbS1057->$0;
    if (_M0L3valS3481 < 0x0p+0f) {
      _M0L2nbS1057->$0 = 0x0p+0f;
    }
    _M0L6_2atmpS3504 = _M0L2r0S1045 / 0x1p+1f;
    _M0L3valS3505 = _M0L2nbS1057->$0;
    moonbit_decref_cycle_free(_M0L2nbS1057);
    _M0L6_2atmpS3501 = _M0L6_2atmpS3504 * _M0L3valS3505;
    _M0L1rS3503 = _M0L1sS1040->$5;
    #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3502 = _M0MPC15array5Array2atGfE(_M0L1rS3503, 0);
    _M0L6_2atmpS3500 = _M0L6_2atmpS3501 + _M0L6_2atmpS3502;
    _M0L5erateS1058
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L5erateS1058)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L5erateS1058->$0 = _M0L6_2atmpS3500;
    _M0L3valS3482 = _M0L5erateS1058->$0;
    if (_M0L3valS3482 < 0x0p+0f) {
      _M0L5erateS1058->$0 = 0x0p+0f;
    }
    _M0L1rS3483 = _M0L1sS1040->$5;
    _M0L1rS3490 = _M0L1sS1040->$5;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3485 = _M0MPC15array5Array2atGfE(_M0L1rS3490, 0);
    _M0L3valS3489 = _M0L5erateS1058->$0;
    _M0L6_2atmpS3488 = _M0L2r0S1045 - _M0L3valS3489;
    _M0L6_2atmpS3487 = _M0L6_2atmpS3488 / 0x1.9p+8f;
    _M0L6_2atmpS3486 = _M0L6_2atmpS3487 * _M0L2dtS1049;
    _M0L6_2atmpS3484 = _M0L6_2atmpS3485 + _M0L6_2atmpS3486;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L1rS3483, 0, _M0L6_2atmpS3484);
    _M0L3valS3499 = _M0L5erateS1058->$0;
    moonbit_decref_cycle_free(_M0L5erateS1058);
    _M0L11exc__lambdaS1059 = _M0L3valS3499 * _M0L2dtS1049;
    _M0L3rngS3498 = _M0L1sS1040->$7;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L1mS1060
    = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3498, _M0L11exc__lambdaS1059);
    if (_M0L1mS1060 > 0) {
      float _M0L6_2atmpS3497 = (float)_M0L1mS1060;
      float _M0L3addS1061 = _M0L1wS1046 * _M0L6_2atmpS3497;
      int32_t _M0L7_2abindS1062 = 0;
      int32_t _M0L1iS1063 = _M0L7_2abindS1062;
      while (1) {
        if (_M0L1iS1063 < _M0L1nS1039) {
          struct _M0TPB5ArrayGfE* _M0L2geS3491 = _M0L1sS1040->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3494 = _M0L1sS1040->$2;
          float _M0L6_2atmpS3493;
          float _M0L6_2atmpS3492;
          struct _M0TPB5ArrayGbE* _M0L4fireS3495;
          int32_t _M0L6_2atmpS3496;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3493
          = _M0MPC15array5Array2atGfE(_M0L2geS3494, _M0L1iS1063);
          _M0L6_2atmpS3492 = _M0L6_2atmpS3493 + _M0L3addS1061;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3491, _M0L1iS1063, _M0L6_2atmpS3492);
          _M0L4fireS3495 = _M0L1sS1040->$4;
          #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3495, _M0L1iS1063, 1);
          _M0L6_2atmpS3496 = _M0L1iS1063 + 1;
          _M0L1iS1063 = _M0L6_2atmpS3496;
          continue;
        }
        break;
      }
    }
  } else {
    int32_t _M0L7_2abindS1065 = 0;
    int32_t _M0L1iS1066 = _M0L7_2abindS1065;
    while (1) {
      if (_M0L1iS1066 < _M0L1nS1039) {
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3549 =
          _M0L1sS1040->$7;
        double _M0L6_2atmpS3548;
        float _M0L6_2atmpS3547;
        float _M0L2reS1067;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3512;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3517;
        float _M0L6_2atmpS3516;
        float _M0L6_2atmpS3515;
        float _M0L6_2atmpS3514;
        float _M0L6_2atmpS3513;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3546;
        float _M0L6_2atmpS3545;
        float _M0L6_2atmpS3544;
        struct _M0TPB8MutLocalGfE* _M0L2nbS1068;
        float _M0L3valS3518;
        float _M0L3valS3519;
        float _M0L6_2atmpS3542;
        float _M0L3valS3543;
        float _M0L6_2atmpS3539;
        struct _M0TPB5ArrayGfE* _M0L1rS3541;
        float _M0L6_2atmpS3540;
        float _M0L6_2atmpS3538;
        struct _M0TPB8MutLocalGfE* _M0L5erateS1069;
        float _M0L3valS3520;
        struct _M0TPB5ArrayGfE* _M0L1rS3521;
        struct _M0TPB5ArrayGfE* _M0L1rS3528;
        float _M0L6_2atmpS3523;
        float _M0L3valS3527;
        float _M0L6_2atmpS3526;
        float _M0L6_2atmpS3525;
        float _M0L6_2atmpS3524;
        float _M0L6_2atmpS3522;
        float _M0L3valS3537;
        float _M0L11exc__lambdaS1070;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3536;
        int32_t _M0L1mS1071;
        int32_t _M0L6_2atmpS3550;
        #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3548 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3549);
        _M0L6_2atmpS3547 = (float)_M0L6_2atmpS3548;
        _M0L2reS1067 = _M0L6_2atmpS3547 - 0x1p-1f;
        _M0L5noiseS3512 = _M0L1sS1040->$6;
        _M0L5noiseS3517 = _M0L1sS1040->$6;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3516
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3517, _M0L1iS1066);
        _M0L6_2atmpS3515 = _M0L6_2atmpS3516 - _M0L2reS1067;
        _M0L6_2atmpS3514 = _M0L6_2atmpS3515 * _M0L2ccS1055;
        _M0L6_2atmpS3513 = _M0L6_2atmpS3514 + _M0L2reS1067;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L5noiseS3512, _M0L1iS1066, _M0L6_2atmpS3513);
        _M0L5noiseS3546 = _M0L1sS1040->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3545
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3546, _M0L1iS1066);
        _M0L6_2atmpS3544 = _M0L6_2atmpS3545 * _M0L4betaS1043;
        _M0L2nbS1068
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L2nbS1068)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2nbS1068->$0 = _M0L6_2atmpS3544;
        _M0L3valS3518 = _M0L2nbS1068->$0;
        if (_M0L3valS3518 > 0x1p+0f) {
          _M0L2nbS1068->$0 = 0x1p+0f;
        }
        _M0L3valS3519 = _M0L2nbS1068->$0;
        if (_M0L3valS3519 < 0x0p+0f) {
          _M0L2nbS1068->$0 = 0x0p+0f;
        }
        _M0L6_2atmpS3542 = _M0L2r0S1045 / 0x1p+1f;
        _M0L3valS3543 = _M0L2nbS1068->$0;
        moonbit_decref_cycle_free(_M0L2nbS1068);
        _M0L6_2atmpS3539 = _M0L6_2atmpS3542 * _M0L3valS3543;
        _M0L1rS3541 = _M0L1sS1040->$5;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3540
        = _M0MPC15array5Array2atGfE(_M0L1rS3541, _M0L1iS1066);
        _M0L6_2atmpS3538 = _M0L6_2atmpS3539 + _M0L6_2atmpS3540;
        _M0L5erateS1069
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L5erateS1069)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L5erateS1069->$0 = _M0L6_2atmpS3538;
        _M0L3valS3520 = _M0L5erateS1069->$0;
        if (_M0L3valS3520 < 0x0p+0f) {
          _M0L5erateS1069->$0 = 0x0p+0f;
        }
        _M0L1rS3521 = _M0L1sS1040->$5;
        _M0L1rS3528 = _M0L1sS1040->$5;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3523
        = _M0MPC15array5Array2atGfE(_M0L1rS3528, _M0L1iS1066);
        _M0L3valS3527 = _M0L5erateS1069->$0;
        _M0L6_2atmpS3526 = _M0L2r0S1045 - _M0L3valS3527;
        _M0L6_2atmpS3525 = _M0L6_2atmpS3526 / 0x1.9p+8f;
        _M0L6_2atmpS3524 = _M0L6_2atmpS3525 * _M0L2dtS1049;
        _M0L6_2atmpS3522 = _M0L6_2atmpS3523 + _M0L6_2atmpS3524;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L1rS3521, _M0L1iS1066, _M0L6_2atmpS3522);
        _M0L3valS3537 = _M0L5erateS1069->$0;
        moonbit_decref_cycle_free(_M0L5erateS1069);
        _M0L11exc__lambdaS1070 = _M0L3valS3537 * _M0L2dtS1049;
        _M0L3rngS3536 = _M0L1sS1040->$7;
        #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L1mS1071
        = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3536, _M0L11exc__lambdaS1070);
        if (_M0L1mS1071 > 0) {
          struct _M0TPB5ArrayGfE* _M0L2geS3529 = _M0L1sS1040->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3534 = _M0L1sS1040->$2;
          float _M0L6_2atmpS3531;
          float _M0L6_2atmpS3533;
          float _M0L6_2atmpS3532;
          float _M0L6_2atmpS3530;
          struct _M0TPB5ArrayGbE* _M0L4fireS3535;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3531
          = _M0MPC15array5Array2atGfE(_M0L2geS3534, _M0L1iS1066);
          _M0L6_2atmpS3533 = (float)_M0L1mS1071;
          _M0L6_2atmpS3532 = _M0L1wS1046 * _M0L6_2atmpS3533;
          _M0L6_2atmpS3530 = _M0L6_2atmpS3531 + _M0L6_2atmpS3532;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3529, _M0L1iS1066, _M0L6_2atmpS3530);
          _M0L4fireS3535 = _M0L1sS1040->$4;
          #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3535, _M0L1iS1066, 1);
        }
        _M0L6_2atmpS3550 = _M0L1iS1066 + 1;
        _M0L1iS1066 = _M0L6_2atmpS3550;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS1036
) {
  struct _M0TUmmmmE* _M0L1sS1035;
  uint64_t _M0L6_2atmpS3463;
  struct _M0TUmmmmE* _M0L1tS1037;
  uint64_t _M0L6_2atmpS3459;
  uint64_t _M0L6_2atmpS3460;
  uint64_t _M0L6_2atmpS3461;
  uint64_t _M0L6_2atmpS3462;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_6069;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS1035 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS1036);
  _M0L6_2atmpS3463 = _M0L1sS1035->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS1037 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS3463);
  _M0L6_2atmpS3459 = _M0L1sS1035->$0;
  _M0L6_2atmpS3460 = _M0L1sS1035->$1;
  _M0L6_2atmpS3461 = _M0L1sS1035->$2;
  moonbit_decref_cycle_free(_M0L1sS1035);
  _M0L6_2atmpS3462 = _M0L1tS1037->$0;
  moonbit_decref_cycle_free(_M0L1tS1037);
  _block_6069
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_6069)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6069->$0 = _M0L6_2atmpS3459;
  _block_6069->$1 = _M0L6_2atmpS3460;
  _block_6069->$2 = _M0L6_2atmpS3461;
  _block_6069->$3 = _M0L6_2atmpS3462;
  return _block_6069;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(
  uint64_t _M0L4seedS1027
) {
  uint64_t _M0L2s1S1026;
  uint64_t _M0L2z1S1028;
  uint64_t _M0L2s2S1029;
  uint64_t _M0L2z2S1030;
  uint64_t _M0L2s3S1031;
  uint64_t _M0L2z3S1032;
  uint64_t _M0L2s4S1033;
  uint64_t _M0L2z4S1034;
  struct _M0TUmmmmE* _block_6070;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S1026 = _M0L4seedS1027 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S1028 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S1026);
  _M0L2s2S1029 = _M0L2s1S1026 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S1030 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S1029);
  _M0L2s3S1031 = _M0L2s2S1029 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S1032 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S1031);
  _M0L2s4S1033 = _M0L2s3S1031 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S1034 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S1033);
  _block_6070 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_6070)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6070->$0 = _M0L2z1S1028;
  _block_6070->$1 = _M0L2z2S1030;
  _block_6070->$2 = _M0L2z3S1032;
  _block_6070->$3 = _M0L2z4S1034;
  return _block_6070;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS1024) {
  uint64_t _M0L6_2atmpS3458;
  uint64_t _M0L6_2atmpS3457;
  uint64_t _M0L1zS1023;
  uint64_t _M0L6_2atmpS3456;
  uint64_t _M0L6_2atmpS3455;
  uint64_t _M0L1zS1025;
  uint64_t _M0L6_2atmpS3454;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3458 = _M0L1zS1024 >> 30;
  _M0L6_2atmpS3457 = _M0L1zS1024 ^ _M0L6_2atmpS3458;
  _M0L1zS1023 = _M0L6_2atmpS3457 * 13787848793156543929ull;
  _M0L6_2atmpS3456 = _M0L1zS1023 >> 27;
  _M0L6_2atmpS3455 = _M0L1zS1023 ^ _M0L6_2atmpS3456;
  _M0L1zS1025 = _M0L6_2atmpS3455 * 10723151780598845931ull;
  _M0L6_2atmpS3454 = _M0L1zS1025 >> 31;
  return _M0L1zS1025 ^ _M0L6_2atmpS3454;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS1011
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3438;
  int32_t _M0L6_2atmpS3437;
  float _M0L12noise__sigmaS3439;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3438 = _M0L1sS1011->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3437 = _M0MPC15array5Array2atGbE(_M0L6activeS3438, 0);
  if (!_M0L6_2atmpS3437) {
    return 0;
  }
  _M0L12noise__sigmaS3439 = _M0L1sS1011->$4;
  if (_M0L12noise__sigmaS3439 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1012 = 0;
    int32_t _M0L7_2abindS1013 = _M0L1sS1011->$3;
    int32_t _M0L1kS1014 = _M0L7_2abindS1012;
    while (1) {
      if (_M0L1kS1014 < _M0L7_2abindS1013) {
        struct _M0TPB5ArrayGfE* _M0L1iS3440 = _M0L1sS1011->$2;
        float _M0L7i__baseS3441 = _M0L1sS1011->$0;
        int32_t _M0L6_2atmpS3442;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3440, _M0L1kS1014, _M0L7i__baseS3441);
        _M0L6_2atmpS3442 = _M0L1kS1014 + 1;
        _M0L1kS1014 = _M0L6_2atmpS3442;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS1016 = _M0L1sS1011->$4;
    struct _M0TPB8MutLocalGiE* _M0L1kS1017 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS1017)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS1017->$0 = 0;
    while (1) {
      int32_t _M0L3valS3443 = _M0L1kS1017->$0;
      int32_t _M0L1nS3444 = _M0L1sS1011->$3;
      if (_M0L3valS3443 < _M0L1nS3444) {
        double _M0L2z1S1019;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3453 =
          _M0L1sS1011->$5;
        struct _M0TUddE* _M0L7_2abindS1020;
        double _M0L5_2az1S1021;
        struct _M0TPB5ArrayGfE* _M0L1iS3445;
        int32_t _M0L3valS3446;
        float _M0L7i__baseS3448;
        float _M0L6_2atmpS3450;
        float _M0L6_2atmpS3449;
        float _M0L6_2atmpS3447;
        int32_t _M0L3valS3452;
        int32_t _M0L6_2atmpS3451;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1020
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3453);
        _M0L5_2az1S1021 = _M0L7_2abindS1020->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1020);
        _M0L2z1S1019 = _M0L5_2az1S1021;
        goto join_1018;
        goto joinlet_6073;
        join_1018:;
        _M0L1iS3445 = _M0L1sS1011->$2;
        _M0L3valS3446 = _M0L1kS1017->$0;
        _M0L7i__baseS3448 = _M0L1sS1011->$0;
        _M0L6_2atmpS3450 = (float)_M0L2z1S1019;
        _M0L6_2atmpS3449 = _M0L5sigmaS1016 * _M0L6_2atmpS3450;
        _M0L6_2atmpS3447 = _M0L7i__baseS3448 + _M0L6_2atmpS3449;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3445, _M0L3valS3446, _M0L6_2atmpS3447);
        _M0L3valS3452 = _M0L1kS1017->$0;
        _M0L6_2atmpS3451 = _M0L3valS3452 + 1;
        _M0L1kS1017->$0 = _M0L6_2atmpS3451;
        joinlet_6073:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS1017);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stimulate__current__if(
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1sS998
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3422;
  int32_t _M0L6_2atmpS3421;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS999;
  int32_t _M0L1nS1000;
  float _M0L12noise__sigmaS3423;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3422 = _M0L1sS998->$1;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3421 = _M0MPC15array5Array2atGbE(_M0L6activeS3422, 0);
  if (!_M0L6_2atmpS3421) {
    return 0;
  }
  _M0L3popS999 = _M0L1sS998->$2;
  _M0L1nS1000 = _M0L3popS999->$2;
  _M0L12noise__sigmaS3423 = _M0L1sS998->$3;
  if (_M0L12noise__sigmaS3423 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1001 = 0;
    int32_t _M0L1iS1002 = _M0L7_2abindS1001;
    while (1) {
      if (_M0L1iS1002 < _M0L1nS1000) {
        struct _M0TPB5ArrayGfE* _M0L1iS3424 = _M0L3popS999->$7;
        float _M0L7i__baseS3425 = _M0L1sS998->$0;
        int32_t _M0L6_2atmpS3426;
        #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3424, _M0L1iS1002, _M0L7i__baseS3425);
        _M0L6_2atmpS3426 = _M0L1iS1002 + 1;
        _M0L1iS1002 = _M0L6_2atmpS3426;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS1004 = _M0L1sS998->$3;
    struct _M0TPB8MutLocalGiE* _M0L1kS1005 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS1005)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS1005->$0 = 0;
    while (1) {
      int32_t _M0L3valS3427 = _M0L1kS1005->$0;
      if (_M0L3valS3427 < _M0L1nS1000) {
        double _M0L2z1S1007;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3436 = _M0L1sS998->$4;
        struct _M0TUddE* _M0L7_2abindS1008;
        double _M0L5_2az1S1009;
        struct _M0TPB5ArrayGfE* _M0L1iS3428;
        int32_t _M0L3valS3429;
        float _M0L7i__baseS3431;
        float _M0L6_2atmpS3433;
        float _M0L6_2atmpS3432;
        float _M0L6_2atmpS3430;
        int32_t _M0L3valS3435;
        int32_t _M0L6_2atmpS3434;
        #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1008
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3436);
        _M0L5_2az1S1009 = _M0L7_2abindS1008->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1008);
        _M0L2z1S1007 = _M0L5_2az1S1009;
        goto join_1006;
        goto joinlet_6076;
        join_1006:;
        _M0L1iS3428 = _M0L3popS999->$7;
        _M0L3valS3429 = _M0L1kS1005->$0;
        _M0L7i__baseS3431 = _M0L1sS998->$0;
        _M0L6_2atmpS3433 = (float)_M0L2z1S1007;
        _M0L6_2atmpS3432 = _M0L5sigmaS1004 * _M0L6_2atmpS3433;
        _M0L6_2atmpS3430 = _M0L7i__baseS3431 + _M0L6_2atmpS3432;
        #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3428, _M0L3valS3429, _M0L6_2atmpS3430);
        _M0L3valS3435 = _M0L1kS1005->$0;
        _M0L6_2atmpS3434 = _M0L3valS3435 + 1;
        _M0L1kS1005->$0 = _M0L6_2atmpS3434;
        joinlet_6076:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS1005);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS987,
  float _M0L4timeS997,
  float _M0L2dtS989
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3408;
  struct _M0TPB5ArrayGbE* _M0L6activeS3407;
  int32_t _M0L6_2atmpS3406;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3420;
  float _M0L4rateS3419;
  float _M0L6lambdaS988;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS990;
  int32_t _M0L7_2abindS991;
  int32_t* _M0L7_2abindS992;
  int32_t _M0L2__S993;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS3408 = _M0L1sS987->$0;
  _M0L6activeS3407 = _M0L5paramS3408->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3406 = _M0MPC15array5Array2atGbE(_M0L6activeS3407, 0);
  if (!_M0L6_2atmpS3406) {
    return 0;
  }
  _M0L5paramS3420 = _M0L1sS987->$0;
  _M0L4rateS3419 = _M0L5paramS3420->$0;
  _M0L6lambdaS988 = _M0L4rateS3419 * _M0L2dtS989;
  if (_M0L6lambdaS988 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS990 = _M0L1sS987->$1;
  _M0L7_2abindS991 = _M0L7_2abindS990->$1;
  _M0L7_2abindS992 = _M0L7_2abindS990->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS992);
  _M0L2__S993 = 0;
  while (1) {
    if (_M0L2__S993 < _M0L7_2abindS991) {
      int32_t _M0L1nS994 = (int32_t)_M0L7_2abindS992[_M0L2__S993];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3417 = _M0L1sS987->$3;
      int32_t _M0L1kS995;
      int32_t _M0L6_2atmpS3418;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS995
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3417, _M0L6lambdaS988);
      if (_M0L1kS995 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS3409 = _M0L1sS987->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS3416 = _M0L1sS987->$2;
        float _M0L6_2atmpS3411;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3415;
        float _M0L2muS3413;
        float _M0L6_2atmpS3414;
        float _M0L6_2atmpS3412;
        float _M0L6_2atmpS3410;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS3411 = _M0MPC15array5Array2atGfE(_M0L1gS3416, _M0L1nS994);
        _M0L5paramS3415 = _M0L1sS987->$0;
        _M0L2muS3413 = _M0L5paramS3415->$1;
        _M0L6_2atmpS3414 = (float)_M0L1kS995;
        _M0L6_2atmpS3412 = _M0L2muS3413 * _M0L6_2atmpS3414;
        _M0L6_2atmpS3410 = _M0L6_2atmpS3411 + _M0L6_2atmpS3412;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS3409, _M0L1nS994, _M0L6_2atmpS3410);
      }
      _M0L6_2atmpS3418 = _M0L2__S993 + 1;
      _M0L2__S993 = _M0L6_2atmpS3418;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS992);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS978,
  moonbit_string_t _M0L3symS984,
  float _M0L4rateS985,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS986
) {
  int32_t _M0L1nS977;
  int32_t* _M0L6_2atmpS3405;
  struct _M0TPB5ArrayGiE* _M0L7neuronsS979;
  int32_t _M0L7_2abindS980;
  int32_t _M0L1kS981;
  struct _M0TPB5ArrayGfE* _M0L1gS983;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L6_2atmpS3404;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _block_6079;
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1nS977 = _M0L3popS978->$2;
  _M0L6_2atmpS3405 = (int32_t*)moonbit_empty_int32_array;
  _M0L7neuronsS979
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L7neuronsS979)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L7neuronsS979->$0 = _M0L6_2atmpS3405;
  _M0L7neuronsS979->$1 = 0;
  _M0L7_2abindS980 = 0;
  _M0L1kS981 = _M0L7_2abindS980;
  while (1) {
    if (_M0L1kS981 < _M0L1nS977) {
      int32_t _M0L6_2atmpS3403;
      #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0MPC15array5Array4pushGiE(_M0L7neuronsS979, _M0L1kS981);
      _M0L6_2atmpS3403 = _M0L1kS981 + 1;
      _M0L1kS981 = _M0L6_2atmpS3403;
      continue;
    }
    break;
  }
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (
    _M0L3symS984 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS984)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS984, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS984) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5681 = _M0L3popS978->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5681);
    _M0L1gS983 = _M0L8_2afieldS5681;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5682 = _M0L3popS978->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5682);
    _M0L1gS983 = _M0L8_2afieldS5682;
  }
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3404 = _M0MP26RiantR8snn__mbt12PoissonFixed3new(_M0L4rateS985);
  moonbit_incref_cycle_free(_M0L3rngS986);
  _block_6079
  = (struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF));
  Moonbit_object_header(_block_6079)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 86, 0);
  _block_6079->$0 = _M0L6_2atmpS3404;
  _block_6079->$1 = _M0L7neuronsS979;
  _block_6079->$2 = _M0L1gS983;
  _block_6079->$3 = _M0L3rngS986;
  return _block_6079;
}

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float _M0L4rateS976
) {
  uint8_t* _M0L6_2atmpS3402;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS3401;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _block_6080;
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3402 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS3402[0] = 1;
  _M0L6_2atmpS3401
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS3401)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 92, 0);
  _M0L6_2atmpS3401->$0 = _M0L6_2atmpS3402;
  _M0L6_2atmpS3401->$1 = 1;
  _block_6080
  = (struct _M0TP26RiantR8snn__mbt12PoissonFixed*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed));
  Moonbit_object_header(_block_6080)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 95, 0);
  _block_6080->$0 = _M0L4rateS976;
  _block_6080->$1 = 0x1p+0f;
  _block_6080->$2 = _M0L6_2atmpS3401;
  return _block_6080;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS959,
  float _M0L4timeS957,
  float _M0L2dtS962
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3400;
  int32_t _M0L6n__preS958;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3399;
  int32_t _M0L7n__postS960;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3398;
  float _M0L4rateS3397;
  float _M0L6lambdaS961;
  int32_t _M0L7_2abindS963;
  int32_t _M0L1iS964;
  moonbit_string_t _M0L3symS3394;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS966;
  int32_t _M0L7_2abindS967;
  int32_t _M0L1iS968;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS3400 = _M0L1sS959->$0;
  _M0L6n__preS958 = _M0L5paramS3400->$1;
  _M0L4postS3399 = _M0L1sS959->$1;
  _M0L7n__postS960 = _M0L4postS3399->$2;
  _M0L5paramS3398 = _M0L1sS959->$0;
  _M0L4rateS3397 = _M0L5paramS3398->$0;
  _M0L6lambdaS961 = _M0L4rateS3397 * _M0L2dtS962;
  _M0L7_2abindS963 = 0;
  _M0L1iS964 = _M0L7_2abindS963;
  while (1) {
    if (_M0L1iS964 < _M0L6n__preS958) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3379 = _M0L1sS959->$3;
      int32_t _M0L6_2atmpS3380;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3379, _M0L1iS964, 0);
      _M0L6_2atmpS3380 = _M0L1iS964 + 1;
      _M0L1iS964 = _M0L6_2atmpS3380;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS961 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS3394 = _M0L1sS959->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS3394 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS3394)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS3394, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3394) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3395 = _M0L1sS959->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5683 = _M0L4postS3395->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5683);
    _M0L9g__targetS966 = _M0L8_2afieldS5683;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3396 = _M0L1sS959->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5684 = _M0L4postS3396->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5684);
    _M0L9g__targetS966 = _M0L8_2afieldS5684;
  }
  _M0L7_2abindS967 = 0;
  _M0L1iS968 = _M0L7_2abindS967;
  while (1) {
    if (_M0L1iS968 < _M0L6n__preS958) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3384 =
        _M0L1sS959->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS3383 = _M0L5paramS3384->$2;
      int32_t _M0L6_2atmpS3382;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3393;
      int32_t _M0L1kS971;
      int32_t _M0L6_2atmpS3381;
      moonbit_incref_cycle_free(_M0L6activeS3383);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS3382
      = _M0MPC15array5Array2atGbE(_M0L6activeS3383, _M0L1iS968);
      moonbit_decref_cycle_free(_M0L6activeS3383);
      if (!_M0L6_2atmpS3382) {
        goto join_969;
      }
      _M0L3rngS3393 = _M0L1sS959->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS971
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3393, _M0L6lambdaS961);
      if (_M0L1kS971 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS3385 = _M0L1sS959->$3;
        int32_t _M0L7_2abindS972;
        int32_t _M0L1jS973;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS3385, _M0L1iS968, 1);
        _M0L7_2abindS972 = 0;
        _M0L1jS973 = _M0L7_2abindS972;
        while (1) {
          if (_M0L1jS973 < _M0L7n__postS960) {
            int32_t _M0L6_2atmpS3391 = _M0L1jS973 * _M0L6n__preS958;
            int32_t _M0L3idxS974 = _M0L6_2atmpS3391 + _M0L1iS968;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS3386 = _M0L1sS959->$5;
            int32_t _M0L6_2atmpS3392;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS3386, _M0L3idxS974)
            ) {
              float _M0L6_2atmpS3388;
              struct _M0TPB5ArrayGfE* _M0L7weightsS3390;
              float _M0L6_2atmpS3389;
              float _M0L6_2atmpS3387;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3388
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS966, _M0L1jS973);
              _M0L7weightsS3390 = _M0L1sS959->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3389
              = _M0MPC15array5Array2atGfE(_M0L7weightsS3390, _M0L3idxS974);
              _M0L6_2atmpS3387 = _M0L6_2atmpS3388 + _M0L6_2atmpS3389;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS966, _M0L1jS973, _M0L6_2atmpS3387);
            }
            _M0L6_2atmpS3392 = _M0L1jS973 + 1;
            _M0L1jS973 = _M0L6_2atmpS3392;
            continue;
          }
          break;
        }
      }
      goto join_969;
      goto joinlet_6083;
      join_969:;
      _M0L6_2atmpS3381 = _M0L1iS968 + 1;
      _M0L1iS968 = _M0L6_2atmpS3381;
      continue;
      joinlet_6083:;
    } else {
      moonbit_decref_cycle_free(_M0L9g__targetS966);
    }
    break;
  }
  return 0;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS952
) {
  double _M0L2u1S951;
  double _M0L8u1__safeS953;
  double _M0L2u2S954;
  double _M0L6_2atmpS3378;
  double _M0L6_2atmpS3377;
  double _M0L1rS955;
  double _M0L5thetaS956;
  double _M0L6_2atmpS3376;
  double _M0L6_2atmpS3373;
  double _M0L6_2atmpS3375;
  double _M0L6_2atmpS3374;
  struct _M0TUddE* _block_6085;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S951 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS952);
  if (_M0L2u1S951 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS953 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS953 = _M0L2u1S951;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S954 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS952);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3378 = _M0FPC14math2ln(_M0L8u1__safeS953);
  _M0L6_2atmpS3377 = -0x1p+1 * _M0L6_2atmpS3378;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS955 = sqrt(_M0L6_2atmpS3377);
  _M0L5thetaS956 = 0x1.921fb54442d18p+2 * _M0L2u2S954;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3376 = _M0FPC14math3cos(_M0L5thetaS956);
  _M0L6_2atmpS3373 = _M0L1rS955 * _M0L6_2atmpS3376;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3375 = _M0FPC14math3sin(_M0L5thetaS956);
  _M0L6_2atmpS3374 = _M0L1rS955 * _M0L6_2atmpS3375;
  _block_6085 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_6085)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6085->$0 = _M0L6_2atmpS3373;
  _block_6085->$1 = _M0L6_2atmpS3374;
  return _block_6085;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS949,
  float _M0L6lambdaS943
) {
  float _M0L6_2atmpS3372;
  float _M0L6_2atmpS3371;
  double _M0L1lS944;
  struct _M0TPB8MutLocalGdE* _M0L1pS945;
  struct _M0TPB8MutLocalGiE* _M0L1kS946;
  float _M0L6_2atmpS3370;
  int32_t _M0L8ten__lamS948;
  int32_t _M0L3capS947;
  int32_t _M0L3valS3369;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS943 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS3372 = -_M0L6lambdaS943;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3371 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3372);
  _M0L1lS944 = (double)_M0L6_2atmpS3371;
  _M0L1pS945
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS945)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS945->$0 = 0x1p+0;
  _M0L1kS946
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS946)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS946->$0 = 0;
  _M0L6_2atmpS3370 = _M0L6lambdaS943 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS948 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3370);
  if (_M0L8ten__lamS948 > 100) {
    _M0L3capS947 = _M0L8ten__lamS948;
  } else {
    _M0L3capS947 = 100;
  }
  while (1) {
    int32_t _M0L3valS3361 = _M0L1kS946->$0;
    int32_t _M0L6_2atmpS3360 = _M0L3valS3361 + 1;
    double _M0L3valS3363;
    double _M0L6_2atmpS3364;
    double _M0L6_2atmpS3362;
    double _M0L3valS3365;
    int32_t _M0L3valS3367;
    _M0L1kS946->$0 = _M0L6_2atmpS3360;
    _M0L3valS3363 = _M0L1pS945->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS3364 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS949);
    _M0L6_2atmpS3362 = _M0L3valS3363 * _M0L6_2atmpS3364;
    _M0L1pS945->$0 = _M0L6_2atmpS3362;
    _M0L3valS3365 = _M0L1pS945->$0;
    if (_M0L3valS3365 < _M0L1lS944) {
      int32_t _M0L3valS3366;
      moonbit_decref_cycle_free(_M0L1pS945);
      _M0L3valS3366 = _M0L1kS946->$0;
      moonbit_decref_cycle_free(_M0L1kS946);
      return _M0L3valS3366 - 1;
    }
    _M0L3valS3367 = _M0L1kS946->$0;
    if (_M0L3valS3367 > _M0L3capS947) {
      int32_t _M0L3valS3368;
      moonbit_decref_cycle_free(_M0L1pS945);
      _M0L3valS3368 = _M0L1kS946->$0;
      moonbit_decref_cycle_free(_M0L1kS946);
      return _M0L3valS3368 - 1;
    }
    continue;
    break;
  }
  _M0L3valS3369 = _M0L1kS946->$0;
  moonbit_decref_cycle_free(_M0L1kS946);
  return _M0L3valS3369 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS941
) {
  uint64_t _M0L1uS940;
  uint64_t _M0L4bitsS942;
  double _M0L6_2atmpS3359;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS940 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS941);
  _M0L4bitsS942 = _M0L1uS940 >> 11;
  _M0L6_2atmpS3359 = (double)_M0L4bitsS942;
  return _M0L6_2atmpS3359 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS934,
  float _M0L1tS936,
  float _M0L1wS938
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS933;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS933
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS933)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS933->$0 = 0;
  while (1) {
    int32_t _M0L3valS3321 = _M0L1iS933->$0;
    int32_t _M0L1nS3322 = _M0L1sS934->$0;
    if (_M0L3valS3321 < _M0L1nS3322) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3323 = _M0L1sS934->$4;
      int32_t _M0L3valS3324 = _M0L1iS933->$0;
      int32_t _M0L3valS3326;
      int32_t _M0L6_2atmpS3325;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3323, _M0L3valS3324, 0);
      _M0L3valS3326 = _M0L1iS933->$0;
      _M0L6_2atmpS3325 = _M0L3valS3326 + 1;
      _M0L1iS933->$0 = _M0L6_2atmpS3325;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS933);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS3330 = _M0L1sS934->$3;
    int32_t _M0L6_2atmpS3329;
    int32_t _if__result_6089;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS3329 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3330, 0);
    if (_M0L6_2atmpS3329 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS3328 = _M0L1sS934->$2;
      float _M0L6_2atmpS3327;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3327 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS3328, 0);
      _if__result_6089 = _M0L6_2atmpS3327 <= _M0L1tS936;
    } else {
      _if__result_6089 = 0;
    }
    if (_if__result_6089) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3358 =
        _M0L1sS934->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS3355 = _M0L5paramS3358->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3357 = _M0L1sS934->$3;
      int32_t _M0L6_2atmpS3356;
      int32_t _M0L1jS937;
      struct _M0TPB5ArrayGbE* _M0L4fireS3331;
      struct _M0TPB5ArrayGfE* _M0L1gS3332;
      struct _M0TPB5ArrayGfE* _M0L1gS3335;
      float _M0L6_2atmpS3334;
      float _M0L6_2atmpS3333;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3341;
      int32_t _M0L6_2atmpS3340;
      int32_t _M0L6_2atmpS3336;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3339;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS3338;
      int32_t _M0L6_2atmpS3337;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3356 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3357, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS937
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS3355, _M0L6_2atmpS3356);
      _M0L4fireS3331 = _M0L1sS934->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3331, _M0L1jS937, 1);
      _M0L1gS3332 = _M0L1sS934->$5;
      _M0L1gS3335 = _M0L1sS934->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3334 = _M0MPC15array5Array2atGfE(_M0L1gS3335, _M0L1jS937);
      _M0L6_2atmpS3333 = _M0L6_2atmpS3334 + _M0L1wS938;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS3332, _M0L1jS937, _M0L6_2atmpS3333);
      _M0L11next__indexS3341 = _M0L1sS934->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3340 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3341, 0);
      _M0L6_2atmpS3336 = _M0L6_2atmpS3340 + 1;
      _M0L5paramS3339 = _M0L1sS934->$1;
      _M0L10spiketimesS3338 = _M0L5paramS3339->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3337 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS3338);
      if (_M0L6_2atmpS3336 < _M0L6_2atmpS3337) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3342 = _M0L1sS934->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3345 = _M0L1sS934->$3;
        int32_t _M0L6_2atmpS3344;
        int32_t _M0L6_2atmpS3343;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3346;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3351;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS3348;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3350;
        int32_t _M0L6_2atmpS3349;
        float _M0L6_2atmpS3347;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3344
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3345, 0);
        _M0L6_2atmpS3343 = _M0L6_2atmpS3344 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3342, 0, _M0L6_2atmpS3343);
        _M0L11next__spikeS3346 = _M0L1sS934->$2;
        _M0L5paramS3351 = _M0L1sS934->$1;
        _M0L10spiketimesS3348 = _M0L5paramS3351->$0;
        _M0L11next__indexS3350 = _M0L1sS934->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3349
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3350, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3347
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS3348, _M0L6_2atmpS3349);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3346, 0, _M0L6_2atmpS3347);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3352 = _M0L1sS934->$2;
        float _M0L6_2atmpS3353 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3354;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3352, 0, _M0L6_2atmpS3353);
        _M0L11next__indexS3354 = _M0L1sS934->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3354, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS920,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS918,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS922,
  float _M0L6t__nowS917,
  float _M0L2dtS927
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3234;
  int32_t _M0L6_2atmpS3233;
  int32_t _if__result_6090;
  int32_t _M0L6n__preS919;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3236;
  int32_t _M0L6_2atmpS3235;
  float _M0L11u__baselineS921;
  float _M0L6tau__fS3320;
  float _M0L11inv__tau__fS923;
  float _M0L6tau__dS3319;
  float _M0L11inv__tau__dS924;
  struct _M0TPB8MutLocalGiE* _M0L1jS925;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3234 = _M0L4varsS918->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3233 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3234);
  if (_M0L6_2atmpS3233 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3232 = _M0L4varsS918->$6;
    int32_t _M0L6_2atmpS3231;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3231 = _M0MPC15array5Array2atGbE(_M0L6activeS3232, 0);
    _if__result_6090 = !_M0L6_2atmpS3231;
  } else {
    _if__result_6090 = 0;
  }
  if (_if__result_6090) {
    return 0;
  }
  _M0L6n__preS919 = _M0L4varsS918->$0;
  _M0L3rhoS3236 = _M0L3synS920->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3235 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3236);
  if (_M0L6_2atmpS3235 == 0) {
    return 0;
  }
  _M0L11u__baselineS921 = _M0L5paramS922->$0;
  _M0L6tau__fS3320 = _M0L5paramS922->$1;
  _M0L11inv__tau__fS923 = 0x1p+0f / _M0L6tau__fS3320;
  _M0L6tau__dS3319 = _M0L5paramS922->$2;
  _M0L11inv__tau__dS924 = 0x1p+0f / _M0L6tau__dS3319;
  _M0L1jS925
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS925)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS925->$0 = 0;
  while (1) {
    int32_t _M0L3valS3237 = _M0L1jS925->$0;
    if (_M0L3valS3237 < _M0L6n__preS919) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3240 = _M0L3synS920->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3238 = _M0L3preS3240->$5;
      int32_t _M0L3valS3239 = _M0L1jS925->$0;
      int32_t _M0L3valS3267;
      int32_t _M0L6_2atmpS3266;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3238, _M0L3valS3239)) {
        struct _M0TPB5ArrayGfE* _M0L1uS3241 = _M0L4varsS918->$2;
        int32_t _M0L3valS3242 = _M0L1jS925->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS3250 = _M0L4varsS918->$2;
        int32_t _M0L3valS3251 = _M0L1jS925->$0;
        float _M0L6_2atmpS3244;
        struct _M0TPB5ArrayGfE* _M0L1uS3248;
        int32_t _M0L3valS3249;
        float _M0L6_2atmpS3247;
        float _M0L6_2atmpS3246;
        float _M0L6_2atmpS3245;
        float _M0L6_2atmpS3243;
        struct _M0TPB5ArrayGfE* _M0L1xS3252;
        int32_t _M0L3valS3253;
        struct _M0TPB5ArrayGfE* _M0L1xS3264;
        int32_t _M0L3valS3265;
        float _M0L6_2atmpS3255;
        struct _M0TPB5ArrayGfE* _M0L1uS3262;
        int32_t _M0L3valS3263;
        float _M0L6_2atmpS3261;
        float _M0L6_2atmpS3257;
        struct _M0TPB5ArrayGfE* _M0L1xS3259;
        int32_t _M0L3valS3260;
        float _M0L6_2atmpS3258;
        float _M0L6_2atmpS3256;
        float _M0L6_2atmpS3254;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3244
        = _M0MPC15array5Array2atGfE(_M0L1uS3250, _M0L3valS3251);
        _M0L1uS3248 = _M0L4varsS918->$2;
        _M0L3valS3249 = _M0L1jS925->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3247
        = _M0MPC15array5Array2atGfE(_M0L1uS3248, _M0L3valS3249);
        _M0L6_2atmpS3246 = 0x1p+0f - _M0L6_2atmpS3247;
        _M0L6_2atmpS3245 = _M0L11u__baselineS921 * _M0L6_2atmpS3246;
        _M0L6_2atmpS3243 = _M0L6_2atmpS3244 + _M0L6_2atmpS3245;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3241, _M0L3valS3242, _M0L6_2atmpS3243);
        _M0L1xS3252 = _M0L4varsS918->$3;
        _M0L3valS3253 = _M0L1jS925->$0;
        _M0L1xS3264 = _M0L4varsS918->$3;
        _M0L3valS3265 = _M0L1jS925->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3255
        = _M0MPC15array5Array2atGfE(_M0L1xS3264, _M0L3valS3265);
        _M0L1uS3262 = _M0L4varsS918->$2;
        _M0L3valS3263 = _M0L1jS925->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3261
        = _M0MPC15array5Array2atGfE(_M0L1uS3262, _M0L3valS3263);
        _M0L6_2atmpS3257 = -_M0L6_2atmpS3261;
        _M0L1xS3259 = _M0L4varsS918->$3;
        _M0L3valS3260 = _M0L1jS925->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3258
        = _M0MPC15array5Array2atGfE(_M0L1xS3259, _M0L3valS3260);
        _M0L6_2atmpS3256 = _M0L6_2atmpS3257 * _M0L6_2atmpS3258;
        _M0L6_2atmpS3254 = _M0L6_2atmpS3255 + _M0L6_2atmpS3256;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3252, _M0L3valS3253, _M0L6_2atmpS3254);
      }
      _M0L3valS3267 = _M0L1jS925->$0;
      _M0L6_2atmpS3266 = _M0L3valS3267 + 1;
      _M0L1jS925->$0 = _M0L6_2atmpS3266;
      continue;
    }
    break;
  }
  _M0L1jS925->$0 = 0;
  while (1) {
    int32_t _M0L3valS3268 = _M0L1jS925->$0;
    if (_M0L3valS3268 < _M0L6n__preS919) {
      struct _M0TPB5ArrayGfE* _M0L1uS3269 = _M0L4varsS918->$2;
      int32_t _M0L3valS3270 = _M0L1jS925->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS3279 = _M0L4varsS918->$2;
      int32_t _M0L3valS3280 = _M0L1jS925->$0;
      float _M0L6_2atmpS3272;
      struct _M0TPB5ArrayGfE* _M0L1uS3277;
      int32_t _M0L3valS3278;
      float _M0L6_2atmpS3276;
      float _M0L6_2atmpS3275;
      float _M0L6_2atmpS3274;
      float _M0L6_2atmpS3273;
      float _M0L6_2atmpS3271;
      struct _M0TPB5ArrayGfE* _M0L1xS3281;
      int32_t _M0L3valS3282;
      struct _M0TPB5ArrayGfE* _M0L1xS3291;
      int32_t _M0L3valS3292;
      float _M0L6_2atmpS3284;
      struct _M0TPB5ArrayGfE* _M0L1xS3289;
      int32_t _M0L3valS3290;
      float _M0L6_2atmpS3288;
      float _M0L6_2atmpS3287;
      float _M0L6_2atmpS3286;
      float _M0L6_2atmpS3285;
      float _M0L6_2atmpS3283;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS3293;
      int32_t _M0L3valS3294;
      struct _M0TPB5ArrayGfE* _M0L1uS3300;
      int32_t _M0L3valS3301;
      float _M0L6_2atmpS3296;
      struct _M0TPB5ArrayGfE* _M0L1xS3298;
      int32_t _M0L3valS3299;
      float _M0L6_2atmpS3297;
      float _M0L6_2atmpS3295;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3318;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3316;
      int32_t _M0L3valS3317;
      int32_t _M0L5startS928;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3315;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3312;
      int32_t _M0L3valS3314;
      int32_t _M0L6_2atmpS3313;
      int32_t _M0L3endS929;
      struct _M0TPB8MutLocalGiE* _M0L1sS930;
      int32_t _M0L3valS3311;
      int32_t _M0L6_2atmpS3310;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3272
      = _M0MPC15array5Array2atGfE(_M0L1uS3279, _M0L3valS3280);
      _M0L1uS3277 = _M0L4varsS918->$2;
      _M0L3valS3278 = _M0L1jS925->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3276
      = _M0MPC15array5Array2atGfE(_M0L1uS3277, _M0L3valS3278);
      _M0L6_2atmpS3275 = _M0L11u__baselineS921 - _M0L6_2atmpS3276;
      _M0L6_2atmpS3274 = _M0L2dtS927 * _M0L6_2atmpS3275;
      _M0L6_2atmpS3273 = _M0L6_2atmpS3274 * _M0L11inv__tau__fS923;
      _M0L6_2atmpS3271 = _M0L6_2atmpS3272 + _M0L6_2atmpS3273;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS3269, _M0L3valS3270, _M0L6_2atmpS3271);
      _M0L1xS3281 = _M0L4varsS918->$3;
      _M0L3valS3282 = _M0L1jS925->$0;
      _M0L1xS3291 = _M0L4varsS918->$3;
      _M0L3valS3292 = _M0L1jS925->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3284
      = _M0MPC15array5Array2atGfE(_M0L1xS3291, _M0L3valS3292);
      _M0L1xS3289 = _M0L4varsS918->$3;
      _M0L3valS3290 = _M0L1jS925->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3288
      = _M0MPC15array5Array2atGfE(_M0L1xS3289, _M0L3valS3290);
      _M0L6_2atmpS3287 = 0x1p+0f - _M0L6_2atmpS3288;
      _M0L6_2atmpS3286 = _M0L2dtS927 * _M0L6_2atmpS3287;
      _M0L6_2atmpS3285 = _M0L6_2atmpS3286 * _M0L11inv__tau__dS924;
      _M0L6_2atmpS3283 = _M0L6_2atmpS3284 + _M0L6_2atmpS3285;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS3281, _M0L3valS3282, _M0L6_2atmpS3283);
      _M0L8rho__preS3293 = _M0L4varsS918->$4;
      _M0L3valS3294 = _M0L1jS925->$0;
      _M0L1uS3300 = _M0L4varsS918->$2;
      _M0L3valS3301 = _M0L1jS925->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3296
      = _M0MPC15array5Array2atGfE(_M0L1uS3300, _M0L3valS3301);
      _M0L1xS3298 = _M0L4varsS918->$3;
      _M0L3valS3299 = _M0L1jS925->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3297
      = _M0MPC15array5Array2atGfE(_M0L1xS3298, _M0L3valS3299);
      _M0L6_2atmpS3295 = _M0L6_2atmpS3296 * _M0L6_2atmpS3297;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS3293, _M0L3valS3294, _M0L6_2atmpS3295);
      _M0L6matrixS3318 = _M0L3synS920->$4;
      _M0L6rowptrS3316 = _M0L6matrixS3318->$2;
      _M0L3valS3317 = _M0L1jS925->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS928
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3316, _M0L3valS3317);
      _M0L6matrixS3315 = _M0L3synS920->$4;
      _M0L6rowptrS3312 = _M0L6matrixS3315->$2;
      _M0L3valS3314 = _M0L1jS925->$0;
      _M0L6_2atmpS3313 = _M0L3valS3314 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS929
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3312, _M0L6_2atmpS3313);
      _M0L1sS930
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS930)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS930->$0 = _M0L5startS928;
      while (1) {
        int32_t _M0L3valS3302 = _M0L1sS930->$0;
        if (_M0L3valS3302 < _M0L3endS929) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS3303 = _M0L3synS920->$6;
          int32_t _M0L3valS3304 = _M0L1sS930->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS3306 = _M0L4varsS918->$4;
          int32_t _M0L3valS3307 = _M0L1jS925->$0;
          float _M0L6_2atmpS3305;
          int32_t _M0L3valS3309;
          int32_t _M0L6_2atmpS3308;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS3305
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS3306, _M0L3valS3307);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS3303, _M0L3valS3304, _M0L6_2atmpS3305);
          _M0L3valS3309 = _M0L1sS930->$0;
          _M0L6_2atmpS3308 = _M0L3valS3309 + 1;
          _M0L1sS930->$0 = _M0L6_2atmpS3308;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS930);
        }
        break;
      }
      _M0L3valS3311 = _M0L1jS925->$0;
      _M0L6_2atmpS3310 = _M0L3valS3311 + 1;
      _M0L1jS925->$0 = _M0L6_2atmpS3310;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS925);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23markram__stp__step__het(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS901,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS899,
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS904,
  float _M0L6t__nowS908
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3142;
  int32_t _M0L6_2atmpS3141;
  int32_t _if__result_6094;
  int32_t _M0L6n__preS900;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3144;
  int32_t _M0L6_2atmpS3143;
  struct _M0TPB8MutLocalGiE* _M0L1jS902;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3142 = _M0L4varsS899->$6;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3141 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3142);
  if (_M0L6_2atmpS3141 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3140 = _M0L4varsS899->$6;
    int32_t _M0L6_2atmpS3139;
    #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3139 = _M0MPC15array5Array2atGbE(_M0L6activeS3140, 0);
    _if__result_6094 = !_M0L6_2atmpS3139;
  } else {
    _if__result_6094 = 0;
  }
  if (_if__result_6094) {
    return 0;
  }
  _M0L6n__preS900 = _M0L4varsS899->$0;
  _M0L3rhoS3144 = _M0L3synS901->$6;
  #line 357 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3143 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3144);
  if (_M0L6_2atmpS3143 == 0) {
    return 0;
  }
  _M0L1jS902
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS902)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS902->$0 = 0;
  while (1) {
    int32_t _M0L3valS3145 = _M0L1jS902->$0;
    if (_M0L3valS3145 < _M0L6n__preS900) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3148 = _M0L3synS901->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3146 = _M0L3preS3148->$5;
      int32_t _M0L3valS3147 = _M0L1jS902->$0;
      int32_t _M0L3valS3230;
      int32_t _M0L6_2atmpS3229;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3146, _M0L3valS3147)) {
        struct _M0TPB5ArrayGfE* _M0L6tau__dS3227 = _M0L5paramS904->$0;
        int32_t _M0L3valS3228 = _M0L1jS902->$0;
        float _M0L9tau__d__jS903;
        struct _M0TPB5ArrayGfE* _M0L6tau__fS3225;
        int32_t _M0L3valS3226;
        float _M0L9tau__f__jS905;
        struct _M0TPB5ArrayGfE* _M0L1uS3223;
        int32_t _M0L3valS3224;
        float _M0L14u__baseline__jS906;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3221;
        int32_t _M0L3valS3222;
        float _M0L6_2atmpS3220;
        float _M0L7dt__preS907;
        float _M0L7dt__preS909;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3149;
        int32_t _M0L3valS3150;
        float _M0L6_2atmpS3219;
        float _M0L6arg__fS910;
        struct _M0TPB5ArrayGfE* _M0L1uS3151;
        int32_t _M0L3valS3152;
        struct _M0TPB5ArrayGfE* _M0L1uS3158;
        int32_t _M0L3valS3159;
        float _M0L6_2atmpS3157;
        float _M0L6_2atmpS3155;
        float _M0L6_2atmpS3156;
        float _M0L6_2atmpS3154;
        float _M0L6_2atmpS3153;
        float _M0L6_2atmpS3218;
        float _M0L6arg__dS911;
        struct _M0TPB5ArrayGfE* _M0L1xS3160;
        int32_t _M0L3valS3161;
        struct _M0TPB5ArrayGfE* _M0L1xS3167;
        int32_t _M0L3valS3168;
        float _M0L6_2atmpS3166;
        float _M0L6_2atmpS3164;
        float _M0L6_2atmpS3165;
        float _M0L6_2atmpS3163;
        float _M0L6_2atmpS3162;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3169;
        int32_t _M0L3valS3170;
        struct _M0TPB5ArrayGfE* _M0L1uS3176;
        int32_t _M0L3valS3177;
        float _M0L6_2atmpS3172;
        struct _M0TPB5ArrayGfE* _M0L1xS3174;
        int32_t _M0L3valS3175;
        float _M0L6_2atmpS3173;
        float _M0L6_2atmpS3171;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3217;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3215;
        int32_t _M0L3valS3216;
        int32_t _M0L5startS912;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3214;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3211;
        int32_t _M0L3valS3213;
        int32_t _M0L6_2atmpS3212;
        int32_t _M0L3endS913;
        struct _M0TPB8MutLocalGiE* _M0L1sS914;
        struct _M0TPB5ArrayGfE* _M0L1uS3186;
        int32_t _M0L3valS3187;
        struct _M0TPB5ArrayGfE* _M0L1uS3195;
        int32_t _M0L3valS3196;
        float _M0L6_2atmpS3189;
        struct _M0TPB5ArrayGfE* _M0L1uS3193;
        int32_t _M0L3valS3194;
        float _M0L6_2atmpS3192;
        float _M0L6_2atmpS3191;
        float _M0L6_2atmpS3190;
        float _M0L6_2atmpS3188;
        struct _M0TPB5ArrayGfE* _M0L1xS3197;
        int32_t _M0L3valS3198;
        struct _M0TPB5ArrayGfE* _M0L1xS3209;
        int32_t _M0L3valS3210;
        float _M0L6_2atmpS3200;
        struct _M0TPB5ArrayGfE* _M0L1uS3207;
        int32_t _M0L3valS3208;
        float _M0L6_2atmpS3206;
        float _M0L6_2atmpS3202;
        struct _M0TPB5ArrayGfE* _M0L1xS3204;
        int32_t _M0L3valS3205;
        float _M0L6_2atmpS3203;
        float _M0L6_2atmpS3201;
        float _M0L6_2atmpS3199;
        #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__d__jS903
        = _M0MPC15array5Array2atGfE(_M0L6tau__dS3227, _M0L3valS3228);
        _M0L6tau__fS3225 = _M0L5paramS904->$1;
        _M0L3valS3226 = _M0L1jS902->$0;
        #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__f__jS905
        = _M0MPC15array5Array2atGfE(_M0L6tau__fS3225, _M0L3valS3226);
        _M0L1uS3223 = _M0L5paramS904->$2;
        _M0L3valS3224 = _M0L1jS902->$0;
        #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L14u__baseline__jS906
        = _M0MPC15array5Array2atGfE(_M0L1uS3223, _M0L3valS3224);
        _M0L11last__spikeS3221 = _M0L4varsS899->$5;
        _M0L3valS3222 = _M0L1jS902->$0;
        #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3220
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3221, _M0L3valS3222);
        _M0L7dt__preS907 = _M0L6t__nowS908 - _M0L6_2atmpS3220;
        if (_M0L7dt__preS907 < 0x0p+0f) {
          _M0L7dt__preS909 = 0x0p+0f;
        } else {
          _M0L7dt__preS909 = _M0L7dt__preS907;
        }
        _M0L11last__spikeS3149 = _M0L4varsS899->$5;
        _M0L3valS3150 = _M0L1jS902->$0;
        #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3149, _M0L3valS3150, _M0L6t__nowS908);
        _M0L6_2atmpS3219 = -_M0L7dt__preS909;
        _M0L6arg__fS910 = _M0L6_2atmpS3219 / _M0L9tau__f__jS905;
        _M0L1uS3151 = _M0L4varsS899->$2;
        _M0L3valS3152 = _M0L1jS902->$0;
        _M0L1uS3158 = _M0L4varsS899->$2;
        _M0L3valS3159 = _M0L1jS902->$0;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3157
        = _M0MPC15array5Array2atGfE(_M0L1uS3158, _M0L3valS3159);
        _M0L6_2atmpS3155 = _M0L14u__baseline__jS906 - _M0L6_2atmpS3157;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3156 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS910);
        _M0L6_2atmpS3154 = _M0L6_2atmpS3155 * _M0L6_2atmpS3156;
        _M0L6_2atmpS3153 = _M0L14u__baseline__jS906 - _M0L6_2atmpS3154;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3151, _M0L3valS3152, _M0L6_2atmpS3153);
        _M0L6_2atmpS3218 = -_M0L7dt__preS909;
        _M0L6arg__dS911 = _M0L6_2atmpS3218 / _M0L9tau__d__jS903;
        _M0L1xS3160 = _M0L4varsS899->$3;
        _M0L3valS3161 = _M0L1jS902->$0;
        _M0L1xS3167 = _M0L4varsS899->$3;
        _M0L3valS3168 = _M0L1jS902->$0;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3166
        = _M0MPC15array5Array2atGfE(_M0L1xS3167, _M0L3valS3168);
        _M0L6_2atmpS3164 = 0x1p+0f - _M0L6_2atmpS3166;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3165 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS911);
        _M0L6_2atmpS3163 = _M0L6_2atmpS3164 * _M0L6_2atmpS3165;
        _M0L6_2atmpS3162 = 0x1p+0f - _M0L6_2atmpS3163;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3160, _M0L3valS3161, _M0L6_2atmpS3162);
        _M0L8rho__preS3169 = _M0L4varsS899->$4;
        _M0L3valS3170 = _M0L1jS902->$0;
        _M0L1uS3176 = _M0L4varsS899->$2;
        _M0L3valS3177 = _M0L1jS902->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3172
        = _M0MPC15array5Array2atGfE(_M0L1uS3176, _M0L3valS3177);
        _M0L1xS3174 = _M0L4varsS899->$3;
        _M0L3valS3175 = _M0L1jS902->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3173
        = _M0MPC15array5Array2atGfE(_M0L1xS3174, _M0L3valS3175);
        _M0L6_2atmpS3171 = _M0L6_2atmpS3172 * _M0L6_2atmpS3173;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3169, _M0L3valS3170, _M0L6_2atmpS3171);
        _M0L6matrixS3217 = _M0L3synS901->$4;
        _M0L6rowptrS3215 = _M0L6matrixS3217->$2;
        _M0L3valS3216 = _M0L1jS902->$0;
        #line 374 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS912
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3215, _M0L3valS3216);
        _M0L6matrixS3214 = _M0L3synS901->$4;
        _M0L6rowptrS3211 = _M0L6matrixS3214->$2;
        _M0L3valS3213 = _M0L1jS902->$0;
        _M0L6_2atmpS3212 = _M0L3valS3213 + 1;
        #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS913
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3211, _M0L6_2atmpS3212);
        _M0L1sS914
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS914)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS914->$0 = _M0L5startS912;
        while (1) {
          int32_t _M0L3valS3178 = _M0L1sS914->$0;
          if (_M0L3valS3178 < _M0L3endS913) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3179 = _M0L3synS901->$6;
            int32_t _M0L3valS3180 = _M0L1sS914->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3182 = _M0L4varsS899->$4;
            int32_t _M0L3valS3183 = _M0L1jS902->$0;
            float _M0L6_2atmpS3181;
            int32_t _M0L3valS3185;
            int32_t _M0L6_2atmpS3184;
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3181
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3182, _M0L3valS3183);
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3179, _M0L3valS3180, _M0L6_2atmpS3181);
            _M0L3valS3185 = _M0L1sS914->$0;
            _M0L6_2atmpS3184 = _M0L3valS3185 + 1;
            _M0L1sS914->$0 = _M0L6_2atmpS3184;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS914);
          }
          break;
        }
        _M0L1uS3186 = _M0L4varsS899->$2;
        _M0L3valS3187 = _M0L1jS902->$0;
        _M0L1uS3195 = _M0L4varsS899->$2;
        _M0L3valS3196 = _M0L1jS902->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3189
        = _M0MPC15array5Array2atGfE(_M0L1uS3195, _M0L3valS3196);
        _M0L1uS3193 = _M0L4varsS899->$2;
        _M0L3valS3194 = _M0L1jS902->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3192
        = _M0MPC15array5Array2atGfE(_M0L1uS3193, _M0L3valS3194);
        _M0L6_2atmpS3191 = 0x1p+0f - _M0L6_2atmpS3192;
        _M0L6_2atmpS3190 = _M0L14u__baseline__jS906 * _M0L6_2atmpS3191;
        _M0L6_2atmpS3188 = _M0L6_2atmpS3189 + _M0L6_2atmpS3190;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3186, _M0L3valS3187, _M0L6_2atmpS3188);
        _M0L1xS3197 = _M0L4varsS899->$3;
        _M0L3valS3198 = _M0L1jS902->$0;
        _M0L1xS3209 = _M0L4varsS899->$3;
        _M0L3valS3210 = _M0L1jS902->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3200
        = _M0MPC15array5Array2atGfE(_M0L1xS3209, _M0L3valS3210);
        _M0L1uS3207 = _M0L4varsS899->$2;
        _M0L3valS3208 = _M0L1jS902->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3206
        = _M0MPC15array5Array2atGfE(_M0L1uS3207, _M0L3valS3208);
        _M0L6_2atmpS3202 = -_M0L6_2atmpS3206;
        _M0L1xS3204 = _M0L4varsS899->$3;
        _M0L3valS3205 = _M0L1jS902->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3203
        = _M0MPC15array5Array2atGfE(_M0L1xS3204, _M0L3valS3205);
        _M0L6_2atmpS3201 = _M0L6_2atmpS3202 * _M0L6_2atmpS3203;
        _M0L6_2atmpS3199 = _M0L6_2atmpS3200 + _M0L6_2atmpS3201;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3197, _M0L3valS3198, _M0L6_2atmpS3199);
      }
      _M0L3valS3230 = _M0L1jS902->$0;
      _M0L6_2atmpS3229 = _M0L3valS3230 + 1;
      _M0L1jS902->$0 = _M0L6_2atmpS3229;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS902);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18markram__stp__step(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS883,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS881,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS885,
  float _M0L6t__nowS890
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3056;
  int32_t _M0L6_2atmpS3055;
  int32_t _if__result_6097;
  int32_t _M0L6n__preS882;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3058;
  int32_t _M0L6_2atmpS3057;
  float _M0L6tau__fS884;
  float _M0L6tau__dS886;
  float _M0L11u__baselineS887;
  struct _M0TPB8MutLocalGiE* _M0L1jS888;
  #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3056 = _M0L4varsS881->$6;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3055 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3056);
  if (_M0L6_2atmpS3055 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3054 = _M0L4varsS881->$6;
    int32_t _M0L6_2atmpS3053;
    #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3053 = _M0MPC15array5Array2atGbE(_M0L6activeS3054, 0);
    _if__result_6097 = !_M0L6_2atmpS3053;
  } else {
    _if__result_6097 = 0;
  }
  if (_if__result_6097) {
    return 0;
  }
  _M0L6n__preS882 = _M0L4varsS881->$0;
  _M0L3rhoS3058 = _M0L3synS883->$6;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3057 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3058);
  if (_M0L6_2atmpS3057 == 0) {
    return 0;
  }
  _M0L6tau__fS884 = _M0L5paramS885->$1;
  _M0L6tau__dS886 = _M0L5paramS885->$0;
  _M0L11u__baselineS887 = _M0L5paramS885->$2;
  _M0L1jS888
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS888)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS888->$0 = 0;
  while (1) {
    int32_t _M0L3valS3059 = _M0L1jS888->$0;
    if (_M0L3valS3059 < _M0L6n__preS882) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3062 = _M0L3synS883->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3060 = _M0L3preS3062->$5;
      int32_t _M0L3valS3061 = _M0L1jS888->$0;
      int32_t _M0L3valS3138;
      int32_t _M0L6_2atmpS3137;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3060, _M0L3valS3061)) {
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3135 = _M0L4varsS881->$5;
        int32_t _M0L3valS3136 = _M0L1jS888->$0;
        float _M0L6_2atmpS3134;
        float _M0L7dt__preS889;
        float _M0L7dt__preS891;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3063;
        int32_t _M0L3valS3064;
        float _M0L6_2atmpS3133;
        float _M0L6arg__fS892;
        struct _M0TPB5ArrayGfE* _M0L1uS3065;
        int32_t _M0L3valS3066;
        struct _M0TPB5ArrayGfE* _M0L1uS3072;
        int32_t _M0L3valS3073;
        float _M0L6_2atmpS3071;
        float _M0L6_2atmpS3069;
        float _M0L6_2atmpS3070;
        float _M0L6_2atmpS3068;
        float _M0L6_2atmpS3067;
        float _M0L6_2atmpS3132;
        float _M0L6arg__dS893;
        struct _M0TPB5ArrayGfE* _M0L1xS3074;
        int32_t _M0L3valS3075;
        struct _M0TPB5ArrayGfE* _M0L1xS3081;
        int32_t _M0L3valS3082;
        float _M0L6_2atmpS3080;
        float _M0L6_2atmpS3078;
        float _M0L6_2atmpS3079;
        float _M0L6_2atmpS3077;
        float _M0L6_2atmpS3076;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3083;
        int32_t _M0L3valS3084;
        struct _M0TPB5ArrayGfE* _M0L1uS3090;
        int32_t _M0L3valS3091;
        float _M0L6_2atmpS3086;
        struct _M0TPB5ArrayGfE* _M0L1xS3088;
        int32_t _M0L3valS3089;
        float _M0L6_2atmpS3087;
        float _M0L6_2atmpS3085;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3131;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3129;
        int32_t _M0L3valS3130;
        int32_t _M0L5startS894;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3128;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3125;
        int32_t _M0L3valS3127;
        int32_t _M0L6_2atmpS3126;
        int32_t _M0L3endS895;
        struct _M0TPB8MutLocalGiE* _M0L1sS896;
        struct _M0TPB5ArrayGfE* _M0L1uS3100;
        int32_t _M0L3valS3101;
        struct _M0TPB5ArrayGfE* _M0L1uS3109;
        int32_t _M0L3valS3110;
        float _M0L6_2atmpS3103;
        struct _M0TPB5ArrayGfE* _M0L1uS3107;
        int32_t _M0L3valS3108;
        float _M0L6_2atmpS3106;
        float _M0L6_2atmpS3105;
        float _M0L6_2atmpS3104;
        float _M0L6_2atmpS3102;
        struct _M0TPB5ArrayGfE* _M0L1xS3111;
        int32_t _M0L3valS3112;
        struct _M0TPB5ArrayGfE* _M0L1xS3123;
        int32_t _M0L3valS3124;
        float _M0L6_2atmpS3114;
        struct _M0TPB5ArrayGfE* _M0L1uS3121;
        int32_t _M0L3valS3122;
        float _M0L6_2atmpS3120;
        float _M0L6_2atmpS3116;
        struct _M0TPB5ArrayGfE* _M0L1xS3118;
        int32_t _M0L3valS3119;
        float _M0L6_2atmpS3117;
        float _M0L6_2atmpS3115;
        float _M0L6_2atmpS3113;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3134
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3135, _M0L3valS3136);
        _M0L7dt__preS889 = _M0L6t__nowS890 - _M0L6_2atmpS3134;
        if (_M0L7dt__preS889 < 0x0p+0f) {
          _M0L7dt__preS891 = 0x0p+0f;
        } else {
          _M0L7dt__preS891 = _M0L7dt__preS889;
        }
        _M0L11last__spikeS3063 = _M0L4varsS881->$5;
        _M0L3valS3064 = _M0L1jS888->$0;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3063, _M0L3valS3064, _M0L6t__nowS890);
        _M0L6_2atmpS3133 = -_M0L7dt__preS891;
        _M0L6arg__fS892 = _M0L6_2atmpS3133 / _M0L6tau__fS884;
        _M0L1uS3065 = _M0L4varsS881->$2;
        _M0L3valS3066 = _M0L1jS888->$0;
        _M0L1uS3072 = _M0L4varsS881->$2;
        _M0L3valS3073 = _M0L1jS888->$0;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3071
        = _M0MPC15array5Array2atGfE(_M0L1uS3072, _M0L3valS3073);
        _M0L6_2atmpS3069 = _M0L11u__baselineS887 - _M0L6_2atmpS3071;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3070 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS892);
        _M0L6_2atmpS3068 = _M0L6_2atmpS3069 * _M0L6_2atmpS3070;
        _M0L6_2atmpS3067 = _M0L11u__baselineS887 - _M0L6_2atmpS3068;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3065, _M0L3valS3066, _M0L6_2atmpS3067);
        _M0L6_2atmpS3132 = -_M0L7dt__preS891;
        _M0L6arg__dS893 = _M0L6_2atmpS3132 / _M0L6tau__dS886;
        _M0L1xS3074 = _M0L4varsS881->$3;
        _M0L3valS3075 = _M0L1jS888->$0;
        _M0L1xS3081 = _M0L4varsS881->$3;
        _M0L3valS3082 = _M0L1jS888->$0;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3080
        = _M0MPC15array5Array2atGfE(_M0L1xS3081, _M0L3valS3082);
        _M0L6_2atmpS3078 = 0x1p+0f - _M0L6_2atmpS3080;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3079 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS893);
        _M0L6_2atmpS3077 = _M0L6_2atmpS3078 * _M0L6_2atmpS3079;
        _M0L6_2atmpS3076 = 0x1p+0f - _M0L6_2atmpS3077;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3074, _M0L3valS3075, _M0L6_2atmpS3076);
        _M0L8rho__preS3083 = _M0L4varsS881->$4;
        _M0L3valS3084 = _M0L1jS888->$0;
        _M0L1uS3090 = _M0L4varsS881->$2;
        _M0L3valS3091 = _M0L1jS888->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3086
        = _M0MPC15array5Array2atGfE(_M0L1uS3090, _M0L3valS3091);
        _M0L1xS3088 = _M0L4varsS881->$3;
        _M0L3valS3089 = _M0L1jS888->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3087
        = _M0MPC15array5Array2atGfE(_M0L1xS3088, _M0L3valS3089);
        _M0L6_2atmpS3085 = _M0L6_2atmpS3086 * _M0L6_2atmpS3087;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3083, _M0L3valS3084, _M0L6_2atmpS3085);
        _M0L6matrixS3131 = _M0L3synS883->$4;
        _M0L6rowptrS3129 = _M0L6matrixS3131->$2;
        _M0L3valS3130 = _M0L1jS888->$0;
        #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS894
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3129, _M0L3valS3130);
        _M0L6matrixS3128 = _M0L3synS883->$4;
        _M0L6rowptrS3125 = _M0L6matrixS3128->$2;
        _M0L3valS3127 = _M0L1jS888->$0;
        _M0L6_2atmpS3126 = _M0L3valS3127 + 1;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS895
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3125, _M0L6_2atmpS3126);
        _M0L1sS896
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS896)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS896->$0 = _M0L5startS894;
        while (1) {
          int32_t _M0L3valS3092 = _M0L1sS896->$0;
          if (_M0L3valS3092 < _M0L3endS895) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3093 = _M0L3synS883->$6;
            int32_t _M0L3valS3094 = _M0L1sS896->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3096 = _M0L4varsS881->$4;
            int32_t _M0L3valS3097 = _M0L1jS888->$0;
            float _M0L6_2atmpS3095;
            int32_t _M0L3valS3099;
            int32_t _M0L6_2atmpS3098;
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3095
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3096, _M0L3valS3097);
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3093, _M0L3valS3094, _M0L6_2atmpS3095);
            _M0L3valS3099 = _M0L1sS896->$0;
            _M0L6_2atmpS3098 = _M0L3valS3099 + 1;
            _M0L1sS896->$0 = _M0L6_2atmpS3098;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS896);
          }
          break;
        }
        _M0L1uS3100 = _M0L4varsS881->$2;
        _M0L3valS3101 = _M0L1jS888->$0;
        _M0L1uS3109 = _M0L4varsS881->$2;
        _M0L3valS3110 = _M0L1jS888->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3103
        = _M0MPC15array5Array2atGfE(_M0L1uS3109, _M0L3valS3110);
        _M0L1uS3107 = _M0L4varsS881->$2;
        _M0L3valS3108 = _M0L1jS888->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3106
        = _M0MPC15array5Array2atGfE(_M0L1uS3107, _M0L3valS3108);
        _M0L6_2atmpS3105 = 0x1p+0f - _M0L6_2atmpS3106;
        _M0L6_2atmpS3104 = _M0L11u__baselineS887 * _M0L6_2atmpS3105;
        _M0L6_2atmpS3102 = _M0L6_2atmpS3103 + _M0L6_2atmpS3104;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3100, _M0L3valS3101, _M0L6_2atmpS3102);
        _M0L1xS3111 = _M0L4varsS881->$3;
        _M0L3valS3112 = _M0L1jS888->$0;
        _M0L1xS3123 = _M0L4varsS881->$3;
        _M0L3valS3124 = _M0L1jS888->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3114
        = _M0MPC15array5Array2atGfE(_M0L1xS3123, _M0L3valS3124);
        _M0L1uS3121 = _M0L4varsS881->$2;
        _M0L3valS3122 = _M0L1jS888->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3120
        = _M0MPC15array5Array2atGfE(_M0L1uS3121, _M0L3valS3122);
        _M0L6_2atmpS3116 = -_M0L6_2atmpS3120;
        _M0L1xS3118 = _M0L4varsS881->$3;
        _M0L3valS3119 = _M0L1jS888->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3117
        = _M0MPC15array5Array2atGfE(_M0L1xS3118, _M0L3valS3119);
        _M0L6_2atmpS3115 = _M0L6_2atmpS3116 * _M0L6_2atmpS3117;
        _M0L6_2atmpS3113 = _M0L6_2atmpS3114 + _M0L6_2atmpS3115;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3111, _M0L3valS3112, _M0L6_2atmpS3113);
      }
      _M0L3valS3138 = _M0L1jS888->$0;
      _M0L6_2atmpS3137 = _M0L3valS3138 + 1;
      _M0L1jS888->$0 = _M0L6_2atmpS3137;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS888);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS879,
  float _M0L2dtS880
) {
  struct _M0TPB5ArrayGfE* _M0L1tS3045;
  struct _M0TPB5ArrayGfE* _M0L1tS3048;
  float _M0L6_2atmpS3047;
  float _M0L6_2atmpS3046;
  struct _M0TPB5ArrayGiE* _M0L2ttS3049;
  struct _M0TPB5ArrayGiE* _M0L2ttS3052;
  int32_t _M0L6_2atmpS3051;
  int32_t _M0L6_2atmpS3050;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS3045 = _M0L1tS879->$0;
  _M0L1tS3048 = _M0L1tS879->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3047 = _M0MPC15array5Array2atGfE(_M0L1tS3048, 0);
  _M0L6_2atmpS3046 = _M0L6_2atmpS3047 + _M0L2dtS880;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS3045, 0, _M0L6_2atmpS3046);
  _M0L2ttS3049 = _M0L1tS879->$1;
  _M0L2ttS3052 = _M0L1tS879->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3051 = _M0MPC15array5Array2atGiE(_M0L2ttS3052, 0);
  _M0L6_2atmpS3050 = _M0L6_2atmpS3051 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS3049, 0, _M0L6_2atmpS3050);
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS878
) {
  struct _M0TPB5ArrayGfE* _M0L1tS3044;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS3044 = _M0L1tS878->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS3044, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS3043;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3040;
  int32_t* _M0L6_2atmpS3042;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS3041;
  struct _M0TP26RiantR8snn__mbt4Time* _block_6100;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3043 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3043[0] = 0x0p+0f;
  _M0L6_2atmpS3040
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3040)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS3040->$0 = _M0L6_2atmpS3043;
  _M0L6_2atmpS3040->$1 = 1;
  _M0L6_2atmpS3042 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS3042[0] = 0;
  _M0L6_2atmpS3041
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS3041)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS3041->$0 = _M0L6_2atmpS3042;
  _M0L6_2atmpS3041->$1 = 1;
  _block_6100
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_6100)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 98, 0);
  _block_6100->$0 = _M0L6_2atmpS3040;
  _block_6100->$1 = _M0L6_2atmpS3041;
  _block_6100->$2 = 0x1p-3f;
  return _block_6100;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS876
) {
  uint32_t _M0L1uS875;
  uint32_t _M0L4bitsS877;
  double _M0L6_2atmpS3039;
  double _M0L6_2atmpS3038;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS875 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS876);
  _M0L4bitsS877 = _M0L1uS875 >> 8;
  _M0L6_2atmpS3039 = (double)_M0L4bitsS877;
  _M0L6_2atmpS3038 = _M0L6_2atmpS3039 * 0x1p-24;
  return (float)_M0L6_2atmpS3038;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS874
) {
  uint64_t _M0L1uS873;
  uint64_t _M0L6_2atmpS3037;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS873 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS874);
  _M0L6_2atmpS3037 = _M0L1uS873 >> 32;
  return (uint32_t)_M0L6_2atmpS3037;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS866
) {
  uint64_t _M0L2s0S865;
  uint64_t _M0L2s1S867;
  uint64_t _M0L2s2S868;
  uint64_t _M0L2s3S869;
  uint64_t _M0L3tmpS870;
  uint64_t _M0L6_2atmpS3036;
  uint64_t _M0L3resS871;
  uint64_t _M0L1tS872;
  uint64_t _M0L6_2atmpS3026;
  uint64_t _M0L6_2atmpS3027;
  uint64_t _M0L2s2S3029;
  uint64_t _M0L6_2atmpS3028;
  uint64_t _M0L2s3S3031;
  uint64_t _M0L6_2atmpS3030;
  uint64_t _M0L2s2S3033;
  uint64_t _M0L6_2atmpS3032;
  uint64_t _M0L2s3S3035;
  uint64_t _M0L6_2atmpS3034;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S865 = _M0L1rS866->$0;
  _M0L2s1S867 = _M0L1rS866->$1;
  _M0L2s2S868 = _M0L1rS866->$2;
  _M0L2s3S869 = _M0L1rS866->$3;
  _M0L3tmpS870 = _M0L2s0S865 + _M0L2s3S869;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3036 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS870, 23);
  _M0L3resS871 = _M0L6_2atmpS3036 + _M0L2s0S865;
  _M0L1tS872 = _M0L2s1S867 << 17;
  _M0L6_2atmpS3026 = _M0L2s2S868 ^ _M0L2s0S865;
  _M0L1rS866->$2 = _M0L6_2atmpS3026;
  _M0L6_2atmpS3027 = _M0L2s3S869 ^ _M0L2s1S867;
  _M0L1rS866->$3 = _M0L6_2atmpS3027;
  _M0L2s2S3029 = _M0L1rS866->$2;
  _M0L6_2atmpS3028 = _M0L2s1S867 ^ _M0L2s2S3029;
  _M0L1rS866->$1 = _M0L6_2atmpS3028;
  _M0L2s3S3031 = _M0L1rS866->$3;
  _M0L6_2atmpS3030 = _M0L2s0S865 ^ _M0L2s3S3031;
  _M0L1rS866->$0 = _M0L6_2atmpS3030;
  _M0L2s2S3033 = _M0L1rS866->$2;
  _M0L6_2atmpS3032 = _M0L2s2S3033 ^ _M0L1tS872;
  _M0L1rS866->$2 = _M0L6_2atmpS3032;
  _M0L2s3S3035 = _M0L1rS866->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3034 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S3035, 45);
  _M0L1rS866->$3 = _M0L6_2atmpS3034;
  return _M0L3resS871;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS863, int32_t _M0L1kS864) {
  uint64_t _M0L6_2atmpS3023;
  int32_t _M0L6_2atmpS3025;
  uint64_t _M0L6_2atmpS3024;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3023 = _M0L1xS863 << (_M0L1kS864 & 63);
  _M0L6_2atmpS3025 = 64 - _M0L1kS864;
  _M0L6_2atmpS3024 = _M0L1xS863 >> (_M0L6_2atmpS3025 & 63);
  return _M0L6_2atmpS3023 | _M0L6_2atmpS3024;
}

float _M0MP26RiantR8snn__mbt7Monitor12firing__rate(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS859
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS3022;
  int32_t _M0L1nS858;
  struct _M0TPB5ArrayGfE* _M0L5timesS3021;
  int32_t _M0L4n__tS860;
  struct _M0TPB5ArrayGfE* _M0L5timesS3019;
  int32_t _M0L6_2atmpS3020;
  float _M0L6_2atmpS3016;
  struct _M0TPB5ArrayGfE* _M0L5timesS3018;
  float _M0L6_2atmpS3017;
  float _M0L9total__msS861;
  int32_t _M0L9n__spikesS862;
  float _M0L6_2atmpS3015;
  float _M0L6_2atmpS3014;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS3022 = _M0L1mS859->$2;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS858 = _M0MPC15array5Array6lengthGfE(_M0L4dataS3022);
  if (_M0L1nS858 < 2) {
    return 0x0p+0f;
  }
  _M0L5timesS3021 = _M0L1mS859->$3;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4n__tS860 = _M0MPC15array5Array6lengthGfE(_M0L5timesS3021);
  if (_M0L4n__tS860 < 2) {
    return 0x0p+0f;
  }
  _M0L5timesS3019 = _M0L1mS859->$3;
  _M0L6_2atmpS3020 = _M0L4n__tS860 - 1;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS3016
  = _M0MPC15array5Array2atGfE(_M0L5timesS3019, _M0L6_2atmpS3020);
  _M0L5timesS3018 = _M0L1mS859->$3;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS3017 = _M0MPC15array5Array2atGfE(_M0L5timesS3018, 0);
  _M0L9total__msS861 = _M0L6_2atmpS3016 - _M0L6_2atmpS3017;
  if (_M0L9total__msS861 <= 0x0p+0f) {
    return 0x0p+0f;
  }
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L9n__spikesS862
  = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L1mS859);
  _M0L6_2atmpS3015 = (float)_M0L9n__spikesS862;
  _M0L6_2atmpS3014 = _M0L6_2atmpS3015 * 0x1.f4p+9f;
  return _M0L6_2atmpS3014 / _M0L9total__msS861;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS851
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS3013;
  int32_t _M0L1nS850;
  struct _M0TPB8MutLocalGiE* _M0L5countS852;
  struct _M0TPB8MutLocalGfE* _M0L4prevS853;
  int32_t _M0L7_2abindS854;
  int32_t _M0L1iS855;
  int32_t _result_6102;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS3013 = _M0L1mS851->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS850 = _M0MPC15array5Array6lengthGfE(_M0L4dataS3013);
  if (_M0L1nS850 == 0) {
    return 0;
  }
  _M0L5countS852
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS852)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS852->$0 = 0;
  _M0L4prevS853
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS853)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS853->$0 = 0x0p+0f;
  _M0L7_2abindS854 = 0;
  _M0L1iS855 = _M0L7_2abindS854;
  while (1) {
    if (_M0L1iS855 < _M0L1nS850) {
      struct _M0TPB5ArrayGfE* _M0L4dataS3011 = _M0L1mS851->$2;
      float _M0L3curS856;
      float _M0L3valS3008;
      int32_t _M0L6_2atmpS3012;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS856 = _M0MPC15array5Array2atGfE(_M0L4dataS3011, _M0L1iS855);
      _M0L3valS3008 = _M0L4prevS853->$0;
      if (_M0L3valS3008 < 0x1p-1f && _M0L3curS856 >= 0x1p-1f) {
        int32_t _M0L3valS3010 = _M0L5countS852->$0;
        int32_t _M0L6_2atmpS3009 = _M0L3valS3010 + 1;
        _M0L5countS852->$0 = _M0L6_2atmpS3009;
      }
      _M0L4prevS853->$0 = _M0L3curS856;
      _M0L6_2atmpS3012 = _M0L1iS855 + 1;
      _M0L1iS855 = _M0L6_2atmpS3012;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS853);
    }
    break;
  }
  _result_6102 = _M0L5countS852->$0;
  moonbit_decref_cycle_free(_M0L5countS852);
  return _result_6102;
}

double _M0FPC14math2ln(double _M0L1xS836) {
  struct _M0TUdiE* _M0L7_2abindS837;
  double _M0L5_2af1S838;
  int32_t _M0L5_2akiS839;
  double _M0L1fS841;
  double _M0L1kS842;
  double _M0L6_2atmpS3001;
  double _M0L1sS843;
  double _M0L2s2S844;
  double _M0L2s4S845;
  double _M0L6_2atmpS3000;
  double _M0L6_2atmpS2999;
  double _M0L6_2atmpS2998;
  double _M0L6_2atmpS2997;
  double _M0L6_2atmpS2996;
  double _M0L6_2atmpS2995;
  double _M0L2t1S846;
  double _M0L6_2atmpS2994;
  double _M0L6_2atmpS2993;
  double _M0L6_2atmpS2992;
  double _M0L6_2atmpS2991;
  double _M0L2t2S847;
  double _M0L1rS848;
  double _M0L6_2atmpS2990;
  double _M0L4hfsqS849;
  double _M0L6_2atmpS2983;
  double _M0L6_2atmpS2989;
  double _M0L6_2atmpS2987;
  double _M0L6_2atmpS2988;
  double _M0L6_2atmpS2986;
  double _M0L6_2atmpS2985;
  double _M0L6_2atmpS2984;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS836 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS836)
      || _M0MPC16double6Double7is__inf(_M0L1xS836)
    ) {
      return _M0L1xS836;
    } else if (_M0L1xS836 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS837 = _M0FPC14math5frexp(_M0L1xS836);
  _M0L5_2af1S838 = _M0L7_2abindS837->$0;
  _M0L5_2akiS839 = _M0L7_2abindS837->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS837);
  if (_M0L5_2af1S838 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS3005 = _M0L5_2af1S838 * 0x1p+1;
    double _M0L6_2atmpS3002 = _M0L6_2atmpS3005 - 0x1p+0;
    int32_t _M0L6_2atmpS3004 = _M0L5_2akiS839 - 1;
    double _M0L6_2atmpS3003 = (double)_M0L6_2atmpS3004;
    _M0L1fS841 = _M0L6_2atmpS3002;
    _M0L1kS842 = _M0L6_2atmpS3003;
    goto join_840;
  } else {
    double _M0L6_2atmpS3006 = _M0L5_2af1S838 - 0x1p+0;
    double _M0L6_2atmpS3007 = (double)_M0L5_2akiS839;
    _M0L1fS841 = _M0L6_2atmpS3006;
    _M0L1kS842 = _M0L6_2atmpS3007;
    goto join_840;
  }
  join_840:;
  _M0L6_2atmpS3001 = 0x1p+1 + _M0L1fS841;
  _M0L1sS843 = _M0L1fS841 / _M0L6_2atmpS3001;
  _M0L2s2S844 = _M0L1sS843 * _M0L1sS843;
  _M0L2s4S845 = _M0L2s2S844 * _M0L2s2S844;
  _M0L6_2atmpS3000 = _M0L2s4S845 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2999 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS3000;
  _M0L6_2atmpS2998 = _M0L2s4S845 * _M0L6_2atmpS2999;
  _M0L6_2atmpS2997 = 0x1.2492494229359p-2 + _M0L6_2atmpS2998;
  _M0L6_2atmpS2996 = _M0L2s4S845 * _M0L6_2atmpS2997;
  _M0L6_2atmpS2995 = 0x1.5555555555593p-1 + _M0L6_2atmpS2996;
  _M0L2t1S846 = _M0L2s2S844 * _M0L6_2atmpS2995;
  _M0L6_2atmpS2994 = _M0L2s4S845 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2993 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2994;
  _M0L6_2atmpS2992 = _M0L2s4S845 * _M0L6_2atmpS2993;
  _M0L6_2atmpS2991 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2992;
  _M0L2t2S847 = _M0L2s4S845 * _M0L6_2atmpS2991;
  _M0L1rS848 = _M0L2t1S846 + _M0L2t2S847;
  _M0L6_2atmpS2990 = 0x1p-1 * _M0L1fS841;
  _M0L4hfsqS849 = _M0L6_2atmpS2990 * _M0L1fS841;
  _M0L6_2atmpS2983 = _M0L1kS842 * 0x1.62e42feep-1;
  _M0L6_2atmpS2989 = _M0L4hfsqS849 + _M0L1rS848;
  _M0L6_2atmpS2987 = _M0L1sS843 * _M0L6_2atmpS2989;
  _M0L6_2atmpS2988 = _M0L1kS842 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2986 = _M0L6_2atmpS2987 + _M0L6_2atmpS2988;
  _M0L6_2atmpS2985 = _M0L4hfsqS849 - _M0L6_2atmpS2986;
  _M0L6_2atmpS2984 = _M0L6_2atmpS2985 - _M0L1fS841;
  return _M0L6_2atmpS2983 - _M0L6_2atmpS2984;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS829) {
  struct _M0TUdiE* _M0L7_2abindS830;
  double _M0L10_2anorm__fS831;
  int32_t _M0L6_2aexpS832;
  uint64_t _M0L1uS833;
  uint64_t _M0L6_2atmpS2982;
  uint64_t _M0L6_2atmpS2981;
  int32_t _M0L6_2atmpS2980;
  int32_t _M0L6_2atmpS2979;
  int32_t _M0L3expS834;
  uint64_t _M0L6_2atmpS2978;
  uint64_t _M0L6_2atmpS2977;
  uint64_t _M0L6_2atmpS2976;
  double _M0L4fracS835;
  struct _M0TUdiE* _block_6105;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS829 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS829)
    || _M0MPC16double6Double7is__nan(_M0L1fS829)
  ) {
    struct _M0TUdiE* _block_6104 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_6104)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_6104->$0 = _M0L1fS829;
    _block_6104->$1 = 0;
    return _block_6104;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS830 = _M0FPC14math9normalize(_M0L1fS829);
  _M0L10_2anorm__fS831 = _M0L7_2abindS830->$0;
  _M0L6_2aexpS832 = _M0L7_2abindS830->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS830);
  _M0L1uS833 = *(int64_t*)&_M0L10_2anorm__fS831;
  _M0L6_2atmpS2982 = _M0L1uS833 >> 52;
  _M0L6_2atmpS2981 = _M0L6_2atmpS2982 & 2047ull;
  _M0L6_2atmpS2980 = (int32_t)_M0L6_2atmpS2981;
  _M0L6_2atmpS2979 = _M0L6_2aexpS832 + _M0L6_2atmpS2980;
  _M0L3expS834 = _M0L6_2atmpS2979 - 1022;
  _M0L6_2atmpS2978 = ~9218868437227405312ull;
  _M0L6_2atmpS2977 = _M0L1uS833 & _M0L6_2atmpS2978;
  _M0L6_2atmpS2976 = _M0L6_2atmpS2977 | 4602678819172646912ull;
  _M0L4fracS835 = *(double*)&_M0L6_2atmpS2976;
  _block_6105 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_6105)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6105->$0 = _M0L4fracS835;
  _block_6105->$1 = _M0L3expS834;
  return _block_6105;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS828) {
  double _M0L6_2atmpS2973;
  struct _M0TUdiE* _block_6107;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2973 = fabs(_M0L1fS828);
  if (_M0L6_2atmpS2973 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2975 = (double)4503599627370496ll;
    double _M0L6_2atmpS2974 = _M0L1fS828 * _M0L6_2atmpS2975;
    struct _M0TUdiE* _block_6106 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_6106)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_6106->$0 = _M0L6_2atmpS2974;
    _block_6106->$1 = -52;
    return _block_6106;
  }
  _block_6107 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_6107)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6107->$0 = _M0L1fS828;
  _block_6107->$1 = 0;
  return _block_6107;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS827) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS827 != _M0L4selfS827;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS826) {
  double _M0L6_2atmpS2972;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2972 = (double)_M0L4selfS826;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2972);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS825) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS825 != _M0L4selfS825) {
    return 0;
  } else if (_M0L4selfS825 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS825 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS825;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS806,
  float _M0L4elemS808
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS805;
  int32_t _M0L1iS807;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS805 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS806);
  _M0L1iS807 = 0;
  while (1) {
    if (_M0L1iS807 < _M0L3lenS806) {
      float* _M0L3bufS2964 = _M0L3arrS805->$0;
      int32_t _M0L6_2atmpS2965;
      _M0L3bufS2964[_M0L1iS807] = _M0L4elemS808;
      _M0L6_2atmpS2965 = _M0L1iS807 + 1;
      _M0L1iS807 = _M0L6_2atmpS2965;
      continue;
    }
    break;
  }
  return _M0L3arrS805;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS811,
  int32_t _M0L4elemS813
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS810;
  int32_t _M0L1iS812;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS810 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS811);
  _M0L1iS812 = 0;
  while (1) {
    if (_M0L1iS812 < _M0L3lenS811) {
      uint8_t* _M0L3bufS2966 = _M0L3arrS810->$0;
      int32_t _M0L6_2atmpS2967;
      _M0L3bufS2966[_M0L1iS812] = _M0L4elemS813;
      _M0L6_2atmpS2967 = _M0L1iS812 + 1;
      _M0L1iS812 = _M0L6_2atmpS2967;
      continue;
    }
    break;
  }
  return _M0L3arrS810;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS816,
  int32_t _M0L4elemS818
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS815;
  int32_t _M0L1iS817;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS815 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS816);
  _M0L1iS817 = 0;
  while (1) {
    if (_M0L1iS817 < _M0L3lenS816) {
      int32_t* _M0L3bufS2968 = _M0L3arrS815->$0;
      int32_t _M0L6_2atmpS2969;
      _M0L3bufS2968[_M0L1iS817] = _M0L4elemS818;
      _M0L6_2atmpS2969 = _M0L1iS817 + 1;
      _M0L1iS817 = _M0L6_2atmpS2969;
      continue;
    }
    break;
  }
  return _M0L3arrS815;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS821,
  struct _M0TPB5ArrayGfE* _M0L4elemS823
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS820;
  int32_t _M0L1iS822;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS820
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS821);
  _M0L1iS822 = 0;
  while (1) {
    if (_M0L1iS822 < _M0L3lenS821) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2970 = _M0L3arrS820->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS5685 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2970[_M0L1iS822];
      int32_t _M0L6_2atmpS2971;
      moonbit_incref_cycle_free(_M0L4elemS823);
      if (_M0L6_2aoldS5685) {
        moonbit_decref_cycle_free(_M0L6_2aoldS5685);
      }
      _M0L3bufS2970[_M0L1iS822] = _M0L4elemS823;
      _M0L6_2atmpS2971 = _M0L1iS822 + 1;
      _M0L1iS822 = _M0L6_2atmpS2971;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS823);
    }
    break;
  }
  return _M0L3arrS820;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS790,
  int32_t _M0L5indexS791,
  float _M0L5valueS792
) {
  int32_t _M0L3lenS789;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS789 = _M0L4selfS790->$1;
  if (_M0L5indexS791 >= 0 && _M0L5indexS791 < _M0L3lenS789) {
    float* _M0L6_2atmpS2960;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2960 = _M0MPC15array5Array6bufferGfE(_M0L4selfS790);
    _M0L6_2atmpS2960[_M0L5indexS791] = _M0L5valueS792;
    moonbit_decref_cycle_free(_M0L6_2atmpS2960);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS794,
  int32_t _M0L5indexS795,
  int32_t _M0L5valueS796
) {
  int32_t _M0L3lenS793;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS793 = _M0L4selfS794->$1;
  if (_M0L5indexS795 >= 0 && _M0L5indexS795 < _M0L3lenS793) {
    int32_t* _M0L6_2atmpS2961;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2961 = _M0MPC15array5Array6bufferGiE(_M0L4selfS794);
    _M0L6_2atmpS2961[_M0L5indexS795] = _M0L5valueS796;
    moonbit_decref_cycle_free(_M0L6_2atmpS2961);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS798,
  int32_t _M0L5indexS799,
  struct _M0TPB5ArrayGfE* _M0L5valueS800
) {
  int32_t _M0L3lenS797;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS797 = _M0L4selfS798->$1;
  if (_M0L5indexS799 >= 0 && _M0L5indexS799 < _M0L3lenS797) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2962;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS5686;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2962
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS798);
    _M0L6_2aoldS5686
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2962[_M0L5indexS799];
    if (_M0L6_2aoldS5686) {
      moonbit_decref_cycle_free(_M0L6_2aoldS5686);
    }
    _M0L6_2atmpS2962[_M0L5indexS799] = _M0L5valueS800;
    moonbit_decref_cycle_free(_M0L6_2atmpS2962);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS800);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS802,
  int32_t _M0L5indexS803,
  int32_t _M0L5valueS804
) {
  int32_t _M0L3lenS801;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS801 = _M0L4selfS802->$1;
  if (_M0L5indexS803 >= 0 && _M0L5indexS803 < _M0L3lenS801) {
    uint8_t* _M0L6_2atmpS2963;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2963 = _M0MPC15array5Array6bufferGbE(_M0L4selfS802);
    _M0L6_2atmpS2963[_M0L5indexS803] = _M0L5valueS804;
    moonbit_decref_cycle_free(_M0L6_2atmpS2963);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS782) {
  int32_t _M0L3lenS781;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS781 = _M0L4selfS782->$1;
  if (_M0L3lenS781 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS783 = _M0L3lenS781 - 1;
    float* _M0L3bufS2958 = _M0L4selfS782->$0;
    float _M0L1vS784 = (float)_M0L3bufS2958[_M0L5indexS783];
    void* _block_6112;
    _M0L4selfS782->$1 = _M0L5indexS783;
    _block_6112
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_6112)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_6112)->$0 = _M0L1vS784;
    return _block_6112;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS786) {
  int32_t _M0L3lenS785;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS785 = _M0L4selfS786->$1;
  if (_M0L3lenS785 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS787 = _M0L3lenS785 - 1;
    int32_t* _M0L3bufS2959 = _M0L4selfS786->$0;
    int32_t _M0L1vS788 = (int32_t)_M0L3bufS2959[_M0L5indexS787];
    _M0L4selfS786->$1 = _M0L5indexS787;
    return (int64_t)_M0L1vS788;
  }
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS764,
  int32_t _M0L5indexS765
) {
  int32_t _M0L3lenS763;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS763 = _M0L4selfS764->$1;
  if (_M0L5indexS765 >= 0 && _M0L5indexS765 < _M0L3lenS763) {
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS2952;
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L6_2atmpS5687;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2952
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L4selfS764);
    _M0L6_2atmpS5687
    = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L6_2atmpS2952[
        _M0L5indexS765
      ];
    if (_M0L6_2atmpS5687) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5687);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2952);
    return _M0L6_2atmpS5687;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS767,
  int32_t _M0L5indexS768
) {
  int32_t _M0L3lenS766;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS766 = _M0L4selfS767->$1;
  if (_M0L5indexS768 >= 0 && _M0L5indexS768 < _M0L3lenS766) {
    float* _M0L6_2atmpS2953;
    float _result_6113;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2953 = _M0MPC15array5Array6bufferGfE(_M0L4selfS767);
    _result_6113 = (float)_M0L6_2atmpS2953[_M0L5indexS768];
    moonbit_decref_cycle_free(_M0L6_2atmpS2953);
    return _result_6113;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS770,
  int32_t _M0L5indexS771
) {
  int32_t _M0L3lenS769;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS769 = _M0L4selfS770->$1;
  if (_M0L5indexS771 >= 0 && _M0L5indexS771 < _M0L3lenS769) {
    moonbit_string_t* _M0L6_2atmpS2954;
    moonbit_string_t _M0L6_2atmpS5688;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2954 = _M0MPC15array5Array6bufferGsE(_M0L4selfS770);
    _M0L6_2atmpS5688 = (moonbit_string_t)_M0L6_2atmpS2954[_M0L5indexS771];
    moonbit_incref_cycle_free(_M0L6_2atmpS5688);
    moonbit_decref_cycle_free(_M0L6_2atmpS2954);
    return _M0L6_2atmpS5688;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS773,
  int32_t _M0L5indexS774
) {
  int32_t _M0L3lenS772;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS772 = _M0L4selfS773->$1;
  if (_M0L5indexS774 >= 0 && _M0L5indexS774 < _M0L3lenS772) {
    int32_t* _M0L6_2atmpS2955;
    int32_t _result_6114;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2955 = _M0MPC15array5Array6bufferGiE(_M0L4selfS773);
    _result_6114 = (int32_t)_M0L6_2atmpS2955[_M0L5indexS774];
    moonbit_decref_cycle_free(_M0L6_2atmpS2955);
    return _result_6114;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS776,
  int32_t _M0L5indexS777
) {
  int32_t _M0L3lenS775;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS775 = _M0L4selfS776->$1;
  if (_M0L5indexS777 >= 0 && _M0L5indexS777 < _M0L3lenS775) {
    uint8_t* _M0L6_2atmpS2956;
    int32_t _result_6115;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2956 = _M0MPC15array5Array6bufferGbE(_M0L4selfS776);
    _result_6115 = (int32_t)_M0L6_2atmpS2956[_M0L5indexS777];
    moonbit_decref_cycle_free(_M0L6_2atmpS2956);
    return _result_6115;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS779,
  int32_t _M0L5indexS780
) {
  int32_t _M0L3lenS778;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS778 = _M0L4selfS779->$1;
  if (_M0L5indexS780 >= 0 && _M0L5indexS780 < _M0L3lenS778) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2957;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS5689;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2957
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS779);
    _M0L6_2atmpS5689
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2957[_M0L5indexS780];
    if (_M0L6_2atmpS5689) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5689);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2957);
    return _M0L6_2atmpS5689;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS762) {
  moonbit_string_t _M0L6_2atmpS2951;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2951 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS762);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2951);
  moonbit_decref_cycle_free(_M0L6_2atmpS2951);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS761) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS761);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS760) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS760 > _M0FPB18double__max__value
         || _M0L4selfS760 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS759) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS759 != _M0L4selfS759;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS744) {
  uint64_t _M0L4bitsS747;
  uint64_t _M0L6_2atmpS2950;
  uint64_t _M0L6_2atmpS2949;
  int32_t _M0L8ieeeSignS748;
  uint64_t _M0L12ieeeMantissaS749;
  uint64_t _M0L6_2atmpS2948;
  uint64_t _M0L6_2atmpS2947;
  int32_t _M0L12ieeeExponentS750;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS751;
  struct _M0TPB17FloatingDecimal64* _M0L1vS752;
  moonbit_string_t _result_6117;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS744 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L3valS744 >= -0x1p+53 && _M0L3valS744 <= 0x1p+53) {
    if (_M0L3valS744 >= -0x1p+31 && _M0L3valS744 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS745;
      double _M0L6_2atmpS2936;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS745 = _M0MPC16double6Double7to__int(_M0L3valS744);
      _M0L6_2atmpS2936 = (double)_M0L1iS745;
      if (_M0L6_2atmpS2936 == _M0L3valS744) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS745, 10);
      }
    } else {
      int64_t _M0L1iS746;
      double _M0L6_2atmpS2937;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS746 = _M0MPC16double6Double9to__int64(_M0L3valS744);
      _M0L6_2atmpS2937 = (double)_M0L1iS746;
      if (_M0L6_2atmpS2937 == _M0L3valS744) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS746, 10);
      }
    }
  }
  _M0L4bitsS747 = *(int64_t*)&_M0L3valS744;
  _M0L6_2atmpS2950 = _M0L4bitsS747 >> 63;
  _M0L6_2atmpS2949 = _M0L6_2atmpS2950 & 1ull;
  _M0L8ieeeSignS748 = _M0L6_2atmpS2949 != 0ull;
  _M0L12ieeeMantissaS749 = _M0L4bitsS747 & 4503599627370495ull;
  _M0L6_2atmpS2948 = _M0L4bitsS747 >> 52;
  _M0L6_2atmpS2947 = _M0L6_2atmpS2948 & 2047ull;
  _M0L12ieeeExponentS750 = (int32_t)_M0L6_2atmpS2947;
  if (
    _M0L12ieeeExponentS750 == 2047
    || _M0L12ieeeExponentS750 == 0 && _M0L12ieeeMantissaS749 == 0ull
  ) {
    int32_t _M0L6_2atmpS2938 = _M0L12ieeeExponentS750 != 0;
    int32_t _M0L6_2atmpS2939 = _M0L12ieeeMantissaS749 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS748, _M0L6_2atmpS2938, _M0L6_2atmpS2939);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS751
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS749, _M0L12ieeeExponentS750);
  if (_M0L7_2abindS751 == 0) {
    uint32_t _M0L6_2atmpS2940;
    if (_M0L7_2abindS751) {
      moonbit_decref_cycle_free(_M0L7_2abindS751);
    }
    _M0L6_2atmpS2940 = *(uint32_t*)&_M0L12ieeeExponentS750;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS752 = _M0FPB3d2d(_M0L12ieeeMantissaS749, _M0L6_2atmpS2940);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS753 = _M0L7_2abindS751;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS754 = _M0L7_2aSomeS753;
    struct _M0TPB17FloatingDecimal64* _M0L1xS755 = _M0L4_2afS754;
    while (1) {
      uint64_t _M0L8mantissaS2946 = _M0L1xS755->$0;
      uint64_t _M0L1qS756 = _M0L8mantissaS2946 / 10ull;
      uint64_t _M0L8mantissaS2944 = _M0L1xS755->$0;
      uint64_t _M0L6_2atmpS2945 = 10ull * _M0L1qS756;
      uint64_t _M0L1rS757 = _M0L8mantissaS2944 - _M0L6_2atmpS2945;
      int32_t _M0L8exponentS2943;
      int32_t _M0L6_2atmpS2942;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2941;
      if (_M0L1rS757 != 0ull) {
        _M0L1vS752 = _M0L1xS755;
        break;
      }
      _M0L8exponentS2943 = _M0L1xS755->$1;
      moonbit_decref_cycle_free(_M0L1xS755);
      _M0L6_2atmpS2942 = _M0L8exponentS2943 + 1;
      _M0L6_2atmpS2941
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2941)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2941->$0 = _M0L1qS756;
      _M0L6_2atmpS2941->$1 = _M0L6_2atmpS2942;
      _M0L1xS755 = _M0L6_2atmpS2941;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_6117 = _M0FPB9to__chars(_M0L1vS752, _M0L8ieeeSignS748);
  moonbit_decref_cycle_free(_M0L1vS752);
  return _result_6117;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS739,
  int32_t _M0L12ieeeExponentS741
) {
  uint64_t _M0L2m2S738;
  int32_t _M0L6_2atmpS2935;
  int32_t _M0L2e2S740;
  int32_t _M0L6_2atmpS2934;
  uint64_t _M0L6_2atmpS2933;
  uint64_t _M0L4maskS742;
  uint64_t _M0L8fractionS743;
  int32_t _M0L6_2atmpS2932;
  uint64_t _M0L6_2atmpS2931;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2930;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S738 = 4503599627370496ull | _M0L12ieeeMantissaS739;
  _M0L6_2atmpS2935 = _M0L12ieeeExponentS741 - 1023;
  _M0L2e2S740 = _M0L6_2atmpS2935 - 52;
  if (_M0L2e2S740 > 0) {
    return 0;
  }
  if (_M0L2e2S740 < -52) {
    return 0;
  }
  _M0L6_2atmpS2934 = -_M0L2e2S740;
  _M0L6_2atmpS2933 = 1ull << (_M0L6_2atmpS2934 & 63);
  _M0L4maskS742 = _M0L6_2atmpS2933 - 1ull;
  _M0L8fractionS743 = _M0L2m2S738 & _M0L4maskS742;
  if (_M0L8fractionS743 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2932 = -_M0L2e2S740;
  _M0L6_2atmpS2931 = _M0L2m2S738 >> (_M0L6_2atmpS2932 & 63);
  _M0L6_2atmpS2930
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2930)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2930->$0 = _M0L6_2atmpS2931;
  _M0L6_2atmpS2930->$1 = 0;
  return _M0L6_2atmpS2930;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS706,
  int32_t _M0L4signS704
) {
  moonbit_bytes_t _M0L6resultS702;
  int32_t _M0Lm5indexS703;
  uint64_t _M0L6outputS705;
  int32_t _M0L7olengthS707;
  int32_t _M0L8exponentS2929;
  int32_t _M0L6_2atmpS2928;
  int32_t _M0Lm3expS708;
  int32_t _M0L6_2atmpS2927;
  int32_t _M0L6_2atmpS2925;
  int32_t _M0L18scientificNotationS709;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS702 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS703 = 0;
  if (_M0L4signS704) {
    int32_t _M0L6_2atmpS2799 = _M0Lm5indexS703;
    int32_t _M0L6_2atmpS2800;
    if (
      _M0L6_2atmpS2799 < 0
      || _M0L6_2atmpS2799 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2799] = 45;
    _M0L6_2atmpS2800 = _M0Lm5indexS703;
    _M0Lm5indexS703 = _M0L6_2atmpS2800 + 1;
  }
  _M0L6outputS705 = _M0L1vS706->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS707 = _M0FPB17decimal__length17(_M0L6outputS705);
  _M0L8exponentS2929 = _M0L1vS706->$1;
  _M0L6_2atmpS2928 = _M0L8exponentS2929 + _M0L7olengthS707;
  _M0Lm3expS708 = _M0L6_2atmpS2928 - 1;
  _M0L6_2atmpS2927 = _M0Lm3expS708;
  if (_M0L6_2atmpS2927 >= -6) {
    int32_t _M0L6_2atmpS2926 = _M0Lm3expS708;
    _M0L6_2atmpS2925 = _M0L6_2atmpS2926 < 21;
  } else {
    _M0L6_2atmpS2925 = 0;
  }
  _M0L18scientificNotationS709 = !_M0L6_2atmpS2925;
  if (_M0L18scientificNotationS709) {
    int32_t _M0L7_2abindS710 = _M0L7olengthS707 - 1;
    uint64_t _M0L6outputS711;
    int32_t _M0L1iS712 = 0;
    uint64_t _M0L6outputS713 = _M0L6outputS705;
    int32_t _M0L6_2atmpS2801;
    int32_t _M0L6_2atmpS2805;
    int32_t _M0L6_2atmpS2804;
    int32_t _M0L6_2atmpS2803;
    int32_t _M0L6_2atmpS2802;
    int32_t _M0L6_2atmpS2809;
    int32_t _M0L6_2atmpS2810;
    int32_t _M0L6_2atmpS2811;
    int32_t _M0L6_2atmpS2812;
    int32_t _M0L6_2atmpS2813;
    int32_t _M0L6_2atmpS2819;
    int32_t _M0L6_2atmpS2852;
    moonbit_string_t _result_6119;
    while (1) {
      if (_M0L1iS712 < _M0L7_2abindS710) {
        uint64_t _M0L1cS714 = _M0L6outputS713 % 10ull;
        int32_t _M0L6_2atmpS2858 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2857 = _M0L6_2atmpS2858 + _M0L7olengthS707;
        int32_t _M0L6_2atmpS2853 = _M0L6_2atmpS2857 - _M0L1iS712;
        int32_t _M0L6_2atmpS2856 = (int32_t)_M0L1cS714;
        int32_t _M0L6_2atmpS2855 = 48 + _M0L6_2atmpS2856;
        int32_t _M0L6_2atmpS2854 = _M0L6_2atmpS2855 & 0xff;
        int32_t _M0L6_2atmpS2859;
        uint64_t _M0L6_2atmpS2860;
        if (
          _M0L6_2atmpS2853 < 0
          || _M0L6_2atmpS2853 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2853] = _M0L6_2atmpS2854;
        _M0L6_2atmpS2859 = _M0L1iS712 + 1;
        _M0L6_2atmpS2860 = _M0L6outputS713 / 10ull;
        _M0L1iS712 = _M0L6_2atmpS2859;
        _M0L6outputS713 = _M0L6_2atmpS2860;
        continue;
      } else {
        _M0L6outputS711 = _M0L6outputS713;
      }
      break;
    }
    _M0L6_2atmpS2801 = _M0Lm5indexS703;
    _M0L6_2atmpS2805 = (int32_t)_M0L6outputS711;
    _M0L6_2atmpS2804 = _M0L6_2atmpS2805 % 10;
    _M0L6_2atmpS2803 = 48 + _M0L6_2atmpS2804;
    _M0L6_2atmpS2802 = _M0L6_2atmpS2803 & 0xff;
    if (
      _M0L6_2atmpS2801 < 0
      || _M0L6_2atmpS2801 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2801] = _M0L6_2atmpS2802;
    if (_M0L7olengthS707 > 1) {
      int32_t _M0L6_2atmpS2807 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2806 = _M0L6_2atmpS2807 + 1;
      if (
        _M0L6_2atmpS2806 < 0
        || _M0L6_2atmpS2806 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2806] = 46;
    } else {
      int32_t _M0L6_2atmpS2808 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2808 - 1;
    }
    _M0L6_2atmpS2809 = _M0Lm5indexS703;
    _M0L6_2atmpS2810 = _M0L7olengthS707 + 1;
    _M0Lm5indexS703 = _M0L6_2atmpS2809 + _M0L6_2atmpS2810;
    _M0L6_2atmpS2811 = _M0Lm5indexS703;
    if (
      _M0L6_2atmpS2811 < 0
      || _M0L6_2atmpS2811 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2811] = 101;
    _M0L6_2atmpS2812 = _M0Lm5indexS703;
    _M0Lm5indexS703 = _M0L6_2atmpS2812 + 1;
    _M0L6_2atmpS2813 = _M0Lm3expS708;
    if (_M0L6_2atmpS2813 < 0) {
      int32_t _M0L6_2atmpS2814 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2815;
      int32_t _M0L6_2atmpS2816;
      if (
        _M0L6_2atmpS2814 < 0
        || _M0L6_2atmpS2814 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2814] = 45;
      _M0L6_2atmpS2815 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2815 + 1;
      _M0L6_2atmpS2816 = _M0Lm3expS708;
      _M0Lm3expS708 = -_M0L6_2atmpS2816;
    } else {
      int32_t _M0L6_2atmpS2817 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2818;
      if (
        _M0L6_2atmpS2817 < 0
        || _M0L6_2atmpS2817 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2817] = 43;
      _M0L6_2atmpS2818 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2818 + 1;
    }
    _M0L6_2atmpS2819 = _M0Lm3expS708;
    if (_M0L6_2atmpS2819 >= 100) {
      int32_t _M0L6_2atmpS2835 = _M0Lm3expS708;
      int32_t _M0L1aS716 = _M0L6_2atmpS2835 / 100;
      int32_t _M0L6_2atmpS2834 = _M0Lm3expS708;
      int32_t _M0L6_2atmpS2833 = _M0L6_2atmpS2834 / 10;
      int32_t _M0L1bS717 = _M0L6_2atmpS2833 % 10;
      int32_t _M0L6_2atmpS2832 = _M0Lm3expS708;
      int32_t _M0L1cS718 = _M0L6_2atmpS2832 % 10;
      int32_t _M0L6_2atmpS2820 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2822 = 48 + _M0L1aS716;
      int32_t _M0L6_2atmpS2821 = _M0L6_2atmpS2822 & 0xff;
      int32_t _M0L6_2atmpS2826;
      int32_t _M0L6_2atmpS2823;
      int32_t _M0L6_2atmpS2825;
      int32_t _M0L6_2atmpS2824;
      int32_t _M0L6_2atmpS2830;
      int32_t _M0L6_2atmpS2827;
      int32_t _M0L6_2atmpS2829;
      int32_t _M0L6_2atmpS2828;
      int32_t _M0L6_2atmpS2831;
      if (
        _M0L6_2atmpS2820 < 0
        || _M0L6_2atmpS2820 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2820] = _M0L6_2atmpS2821;
      _M0L6_2atmpS2826 = _M0Lm5indexS703;
      _M0L6_2atmpS2823 = _M0L6_2atmpS2826 + 1;
      _M0L6_2atmpS2825 = 48 + _M0L1bS717;
      _M0L6_2atmpS2824 = _M0L6_2atmpS2825 & 0xff;
      if (
        _M0L6_2atmpS2823 < 0
        || _M0L6_2atmpS2823 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2823] = _M0L6_2atmpS2824;
      _M0L6_2atmpS2830 = _M0Lm5indexS703;
      _M0L6_2atmpS2827 = _M0L6_2atmpS2830 + 2;
      _M0L6_2atmpS2829 = 48 + _M0L1cS718;
      _M0L6_2atmpS2828 = _M0L6_2atmpS2829 & 0xff;
      if (
        _M0L6_2atmpS2827 < 0
        || _M0L6_2atmpS2827 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2827] = _M0L6_2atmpS2828;
      _M0L6_2atmpS2831 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2831 + 3;
    } else {
      int32_t _M0L6_2atmpS2836 = _M0Lm3expS708;
      if (_M0L6_2atmpS2836 >= 10) {
        int32_t _M0L6_2atmpS2846 = _M0Lm3expS708;
        int32_t _M0L1aS719 = _M0L6_2atmpS2846 / 10;
        int32_t _M0L6_2atmpS2845 = _M0Lm3expS708;
        int32_t _M0L1bS720 = _M0L6_2atmpS2845 % 10;
        int32_t _M0L6_2atmpS2837 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2839 = 48 + _M0L1aS719;
        int32_t _M0L6_2atmpS2838 = _M0L6_2atmpS2839 & 0xff;
        int32_t _M0L6_2atmpS2843;
        int32_t _M0L6_2atmpS2840;
        int32_t _M0L6_2atmpS2842;
        int32_t _M0L6_2atmpS2841;
        int32_t _M0L6_2atmpS2844;
        if (
          _M0L6_2atmpS2837 < 0
          || _M0L6_2atmpS2837 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2837] = _M0L6_2atmpS2838;
        _M0L6_2atmpS2843 = _M0Lm5indexS703;
        _M0L6_2atmpS2840 = _M0L6_2atmpS2843 + 1;
        _M0L6_2atmpS2842 = 48 + _M0L1bS720;
        _M0L6_2atmpS2841 = _M0L6_2atmpS2842 & 0xff;
        if (
          _M0L6_2atmpS2840 < 0
          || _M0L6_2atmpS2840 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2840] = _M0L6_2atmpS2841;
        _M0L6_2atmpS2844 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2844 + 2;
      } else {
        int32_t _M0L6_2atmpS2847 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2850 = _M0Lm3expS708;
        int32_t _M0L6_2atmpS2849 = 48 + _M0L6_2atmpS2850;
        int32_t _M0L6_2atmpS2848 = _M0L6_2atmpS2849 & 0xff;
        int32_t _M0L6_2atmpS2851;
        if (
          _M0L6_2atmpS2847 < 0
          || _M0L6_2atmpS2847 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2847] = _M0L6_2atmpS2848;
        _M0L6_2atmpS2851 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2851 + 1;
      }
    }
    _M0L6_2atmpS2852 = _M0Lm5indexS703;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6119
    = _M0FPB19string__from__bytes(_M0L6resultS702, 0, _M0L6_2atmpS2852);
    moonbit_decref_cycle_free(_M0L6resultS702);
    return _result_6119;
  } else {
    int32_t _M0L6_2atmpS2861 = _M0Lm3expS708;
    int32_t _M0L6_2atmpS2924;
    moonbit_string_t _result_6125;
    if (_M0L6_2atmpS2861 < 0) {
      int32_t _M0L6_2atmpS2862 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2864;
      int32_t _M0L6_2atmpS2863;
      int32_t _M0L6_2atmpS2865;
      int32_t _M0L1iS721;
      int32_t _M0L6_2atmpS2880;
      int32_t _M0L6_2atmpS2882;
      int32_t _M0L6_2atmpS2881;
      int32_t _M0L7currentS723;
      int32_t _M0L1iS724;
      uint64_t _M0L6outputS725;
      if (
        _M0L6_2atmpS2862 < 0
        || _M0L6_2atmpS2862 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2862] = 48;
      _M0L6_2atmpS2864 = _M0Lm5indexS703;
      _M0L6_2atmpS2863 = _M0L6_2atmpS2864 + 1;
      if (
        _M0L6_2atmpS2863 < 0
        || _M0L6_2atmpS2863 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2863] = 46;
      _M0L6_2atmpS2865 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2865 + 2;
      _M0L1iS721 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2866 = _M0Lm3expS708;
        if (_M0L1iS721 > _M0L6_2atmpS2866) {
          int32_t _M0L6_2atmpS2869 = _M0Lm5indexS703;
          int32_t _M0L6_2atmpS2868 = _M0L6_2atmpS2869 - _M0L1iS721;
          int32_t _M0L6_2atmpS2867 = _M0L6_2atmpS2868 - 1;
          int32_t _M0L6_2atmpS2870;
          if (
            _M0L6_2atmpS2867 < 0
            || _M0L6_2atmpS2867 >= Moonbit_array_length(_M0L6resultS702)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS702[_M0L6_2atmpS2867] = 48;
          _M0L6_2atmpS2870 = _M0L1iS721 - 1;
          _M0L1iS721 = _M0L6_2atmpS2870;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2880 = _M0Lm5indexS703;
      _M0L6_2atmpS2882 = _M0Lm3expS708;
      _M0L6_2atmpS2881 = -1 - _M0L6_2atmpS2882;
      _M0L7currentS723 = _M0L6_2atmpS2880 + _M0L6_2atmpS2881;
      _M0L1iS724 = 0;
      _M0L6outputS725 = _M0L6outputS705;
      while (1) {
        if (_M0L1iS724 < _M0L7olengthS707) {
          int32_t _M0L6_2atmpS2877 = _M0L7currentS723 + _M0L7olengthS707;
          int32_t _M0L6_2atmpS2876 = _M0L6_2atmpS2877 - _M0L1iS724;
          int32_t _M0L6_2atmpS2871 = _M0L6_2atmpS2876 - 1;
          uint64_t _M0L6_2atmpS2875 = _M0L6outputS725 % 10ull;
          int32_t _M0L6_2atmpS2874 = (int32_t)_M0L6_2atmpS2875;
          int32_t _M0L6_2atmpS2873 = 48 + _M0L6_2atmpS2874;
          int32_t _M0L6_2atmpS2872 = _M0L6_2atmpS2873 & 0xff;
          int32_t _M0L6_2atmpS2878;
          uint64_t _M0L6_2atmpS2879;
          if (
            _M0L6_2atmpS2871 < 0
            || _M0L6_2atmpS2871 >= Moonbit_array_length(_M0L6resultS702)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS702[_M0L6_2atmpS2871] = _M0L6_2atmpS2872;
          _M0L6_2atmpS2878 = _M0L1iS724 + 1;
          _M0L6_2atmpS2879 = _M0L6outputS725 / 10ull;
          _M0L1iS724 = _M0L6_2atmpS2878;
          _M0L6outputS725 = _M0L6_2atmpS2879;
          continue;
        }
        break;
      }
      _M0Lm5indexS703 = _M0L7currentS723 + _M0L7olengthS707;
    } else {
      int32_t _M0L6_2atmpS2884 = _M0Lm3expS708;
      int32_t _M0L6_2atmpS2883 = _M0L6_2atmpS2884 + 1;
      if (_M0L6_2atmpS2883 >= _M0L7olengthS707) {
        int32_t _M0L1iS727 = 0;
        uint64_t _M0L6outputS728 = _M0L6outputS705;
        int32_t _M0L6_2atmpS2895;
        int32_t _M0L6_2atmpS2900;
        int32_t _M0L7_2abindS730;
        int32_t _M0L1iS731;
        int32_t _M0L6_2atmpS2901;
        int32_t _M0L6_2atmpS2904;
        int32_t _M0L6_2atmpS2903;
        int32_t _M0L6_2atmpS2902;
        while (1) {
          if (_M0L1iS727 < _M0L7olengthS707) {
            int32_t _M0L6_2atmpS2892 = _M0Lm5indexS703;
            int32_t _M0L6_2atmpS2891 = _M0L6_2atmpS2892 + _M0L7olengthS707;
            int32_t _M0L6_2atmpS2890 = _M0L6_2atmpS2891 - _M0L1iS727;
            int32_t _M0L6_2atmpS2885 = _M0L6_2atmpS2890 - 1;
            uint64_t _M0L6_2atmpS2889 = _M0L6outputS728 % 10ull;
            int32_t _M0L6_2atmpS2888 = (int32_t)_M0L6_2atmpS2889;
            int32_t _M0L6_2atmpS2887 = 48 + _M0L6_2atmpS2888;
            int32_t _M0L6_2atmpS2886 = _M0L6_2atmpS2887 & 0xff;
            int32_t _M0L6_2atmpS2893;
            uint64_t _M0L6_2atmpS2894;
            if (
              _M0L6_2atmpS2885 < 0
              || _M0L6_2atmpS2885 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2885] = _M0L6_2atmpS2886;
            _M0L6_2atmpS2893 = _M0L1iS727 + 1;
            _M0L6_2atmpS2894 = _M0L6outputS728 / 10ull;
            _M0L1iS727 = _M0L6_2atmpS2893;
            _M0L6outputS728 = _M0L6_2atmpS2894;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2895 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2895 + _M0L7olengthS707;
        _M0L6_2atmpS2900 = _M0Lm3expS708;
        _M0L7_2abindS730 = _M0L6_2atmpS2900 + 1;
        _M0L1iS731 = _M0L7olengthS707;
        while (1) {
          if (_M0L1iS731 < _M0L7_2abindS730) {
            int32_t _M0L6_2atmpS2898 = _M0Lm5indexS703;
            int32_t _M0L6_2atmpS2897 = _M0L6_2atmpS2898 + _M0L1iS731;
            int32_t _M0L6_2atmpS2896 = _M0L6_2atmpS2897 - _M0L7olengthS707;
            int32_t _M0L6_2atmpS2899;
            if (
              _M0L6_2atmpS2896 < 0
              || _M0L6_2atmpS2896 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2896] = 48;
            _M0L6_2atmpS2899 = _M0L1iS731 + 1;
            _M0L1iS731 = _M0L6_2atmpS2899;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2901 = _M0Lm5indexS703;
        _M0L6_2atmpS2904 = _M0Lm3expS708;
        _M0L6_2atmpS2903 = _M0L6_2atmpS2904 + 1;
        _M0L6_2atmpS2902 = _M0L6_2atmpS2903 - _M0L7olengthS707;
        _M0Lm5indexS703 = _M0L6_2atmpS2901 + _M0L6_2atmpS2902;
      } else {
        int32_t _M0L6_2atmpS2921 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2920 = _M0L6_2atmpS2921 + 1;
        int32_t _M0L1iS733 = 0;
        int32_t _M0L7currentS734 = _M0L6_2atmpS2920;
        uint64_t _M0L6outputS735 = _M0L6outputS705;
        int32_t _M0L6_2atmpS2922;
        int32_t _M0L6_2atmpS2923;
        while (1) {
          if (_M0L1iS733 < _M0L7olengthS707) {
            int32_t _M0L6_2atmpS2916 = _M0L7olengthS707 - _M0L1iS733;
            int32_t _M0L6_2atmpS2914 = _M0L6_2atmpS2916 - 1;
            int32_t _M0L6_2atmpS2915 = _M0Lm3expS708;
            int32_t _M0L7currentS736;
            int32_t _M0L6_2atmpS2911;
            int32_t _M0L6_2atmpS2910;
            int32_t _M0L6_2atmpS2905;
            uint64_t _M0L6_2atmpS2909;
            int32_t _M0L6_2atmpS2908;
            int32_t _M0L6_2atmpS2907;
            int32_t _M0L6_2atmpS2906;
            int32_t _M0L6_2atmpS2912;
            uint64_t _M0L6_2atmpS2913;
            if (_M0L6_2atmpS2914 == _M0L6_2atmpS2915) {
              int32_t _M0L6_2atmpS2919 = _M0L7currentS734 + _M0L7olengthS707;
              int32_t _M0L6_2atmpS2918 = _M0L6_2atmpS2919 - _M0L1iS733;
              int32_t _M0L6_2atmpS2917 = _M0L6_2atmpS2918 - 1;
              if (
                _M0L6_2atmpS2917 < 0
                || _M0L6_2atmpS2917 >= Moonbit_array_length(_M0L6resultS702)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS702[_M0L6_2atmpS2917] = 46;
              _M0L7currentS736 = _M0L7currentS734 - 1;
            } else {
              _M0L7currentS736 = _M0L7currentS734;
            }
            _M0L6_2atmpS2911 = _M0L7currentS736 + _M0L7olengthS707;
            _M0L6_2atmpS2910 = _M0L6_2atmpS2911 - _M0L1iS733;
            _M0L6_2atmpS2905 = _M0L6_2atmpS2910 - 1;
            _M0L6_2atmpS2909 = _M0L6outputS735 % 10ull;
            _M0L6_2atmpS2908 = (int32_t)_M0L6_2atmpS2909;
            _M0L6_2atmpS2907 = 48 + _M0L6_2atmpS2908;
            _M0L6_2atmpS2906 = _M0L6_2atmpS2907 & 0xff;
            if (
              _M0L6_2atmpS2905 < 0
              || _M0L6_2atmpS2905 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2905] = _M0L6_2atmpS2906;
            _M0L6_2atmpS2912 = _M0L1iS733 + 1;
            _M0L6_2atmpS2913 = _M0L6outputS735 / 10ull;
            _M0L1iS733 = _M0L6_2atmpS2912;
            _M0L7currentS734 = _M0L7currentS736;
            _M0L6outputS735 = _M0L6_2atmpS2913;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2922 = _M0Lm5indexS703;
        _M0L6_2atmpS2923 = _M0L7olengthS707 + 1;
        _M0Lm5indexS703 = _M0L6_2atmpS2922 + _M0L6_2atmpS2923;
      }
    }
    _M0L6_2atmpS2924 = _M0Lm5indexS703;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6125
    = _M0FPB19string__from__bytes(_M0L6resultS702, 0, _M0L6_2atmpS2924);
    moonbit_decref_cycle_free(_M0L6resultS702);
    return _result_6125;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS648,
  uint32_t _M0L12ieeeExponentS647
) {
  int32_t _M0Lm2e2S645;
  uint64_t _M0Lm2m2S646;
  uint64_t _M0L6_2atmpS2798;
  uint64_t _M0L6_2atmpS2797;
  int32_t _M0L4evenS649;
  uint64_t _M0L6_2atmpS2796;
  uint64_t _M0L2mvS650;
  int32_t _M0L7mmShiftS651;
  uint64_t _M0Lm2vrS652;
  uint64_t _M0Lm2vpS653;
  uint64_t _M0Lm2vmS654;
  int32_t _M0Lm3e10S655;
  int32_t _M0Lm17vmIsTrailingZerosS656;
  int32_t _M0Lm17vrIsTrailingZerosS657;
  int32_t _M0L6_2atmpS2698;
  int32_t _M0Lm7removedS676;
  int32_t _M0Lm16lastRemovedDigitS677;
  uint64_t _M0Lm6outputS678;
  int32_t _M0L6_2atmpS2794;
  int32_t _M0L6_2atmpS2795;
  int32_t _M0L3expS701;
  uint64_t _M0L6_2atmpS2793;
  struct _M0TPB17FloatingDecimal64* _block_6131;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S645 = 0;
  _M0Lm2m2S646 = 0ull;
  if (_M0L12ieeeExponentS647 == 0u) {
    _M0Lm2e2S645 = -1076;
    _M0Lm2m2S646 = _M0L12ieeeMantissaS648;
  } else {
    int32_t _M0L6_2atmpS2697 = *(int32_t*)&_M0L12ieeeExponentS647;
    int32_t _M0L6_2atmpS2696 = _M0L6_2atmpS2697 - 1023;
    int32_t _M0L6_2atmpS2695 = _M0L6_2atmpS2696 - 52;
    _M0Lm2e2S645 = _M0L6_2atmpS2695 - 2;
    _M0Lm2m2S646 = 4503599627370496ull | _M0L12ieeeMantissaS648;
  }
  _M0L6_2atmpS2798 = _M0Lm2m2S646;
  _M0L6_2atmpS2797 = _M0L6_2atmpS2798 & 1ull;
  _M0L4evenS649 = _M0L6_2atmpS2797 == 0ull;
  _M0L6_2atmpS2796 = _M0Lm2m2S646;
  _M0L2mvS650 = 4ull * _M0L6_2atmpS2796;
  _M0L7mmShiftS651
  = _M0L12ieeeMantissaS648 != 0ull || _M0L12ieeeExponentS647 <= 1u;
  _M0Lm2vrS652 = 0ull;
  _M0Lm2vpS653 = 0ull;
  _M0Lm2vmS654 = 0ull;
  _M0Lm3e10S655 = 0;
  _M0Lm17vmIsTrailingZerosS656 = 0;
  _M0Lm17vrIsTrailingZerosS657 = 0;
  _M0L6_2atmpS2698 = _M0Lm2e2S645;
  if (_M0L6_2atmpS2698 >= 0) {
    int32_t _M0L6_2atmpS2720 = _M0Lm2e2S645;
    int32_t _M0L6_2atmpS2716;
    int32_t _M0L6_2atmpS2719;
    int32_t _M0L6_2atmpS2718;
    int32_t _M0L6_2atmpS2717;
    int32_t _M0L1qS658;
    int32_t _M0L6_2atmpS2715;
    int32_t _M0L6_2atmpS2714;
    int32_t _M0L1kS659;
    int32_t _M0L6_2atmpS2713;
    int32_t _M0L6_2atmpS2712;
    int32_t _M0L6_2atmpS2711;
    int32_t _M0L1iS660;
    struct _M0TPB8Pow5Pair _M0L4pow5S661;
    uint64_t _M0L6_2atmpS2710;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS662;
    uint64_t _M0L8_2avrOutS663;
    uint64_t _M0L8_2avpOutS664;
    uint64_t _M0L8_2avmOutS665;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2716 = _M0FPB9log10Pow2(_M0L6_2atmpS2720);
    _M0L6_2atmpS2719 = _M0Lm2e2S645;
    _M0L6_2atmpS2718 = _M0L6_2atmpS2719 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2717 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2718);
    _M0L1qS658 = _M0L6_2atmpS2716 - _M0L6_2atmpS2717;
    _M0Lm3e10S655 = _M0L1qS658;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2715 = _M0FPB8pow5bits(_M0L1qS658);
    _M0L6_2atmpS2714 = 125 + _M0L6_2atmpS2715;
    _M0L1kS659 = _M0L6_2atmpS2714 - 1;
    _M0L6_2atmpS2713 = _M0Lm2e2S645;
    _M0L6_2atmpS2712 = -_M0L6_2atmpS2713;
    _M0L6_2atmpS2711 = _M0L6_2atmpS2712 + _M0L1qS658;
    _M0L1iS660 = _M0L6_2atmpS2711 + _M0L1kS659;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S661 = _M0FPB22double__computeInvPow5(_M0L1qS658);
    _M0L6_2atmpS2710 = _M0Lm2m2S646;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS662
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2710, _M0L4pow5S661, _M0L1iS660, _M0L7mmShiftS651);
    _M0L8_2avrOutS663 = _M0L7_2abindS662.$0;
    _M0L8_2avpOutS664 = _M0L7_2abindS662.$1;
    _M0L8_2avmOutS665 = _M0L7_2abindS662.$2;
    _M0Lm2vrS652 = _M0L8_2avrOutS663;
    _M0Lm2vpS653 = _M0L8_2avpOutS664;
    _M0Lm2vmS654 = _M0L8_2avmOutS665;
    if (_M0L1qS658 <= 21) {
      int32_t _M0L6_2atmpS2706 = (int32_t)_M0L2mvS650;
      uint64_t _M0L6_2atmpS2709 = _M0L2mvS650 / 5ull;
      int32_t _M0L6_2atmpS2708 = (int32_t)_M0L6_2atmpS2709;
      int32_t _M0L6_2atmpS2707 = 5 * _M0L6_2atmpS2708;
      int32_t _M0L6mvMod5S666 = _M0L6_2atmpS2706 - _M0L6_2atmpS2707;
      if (_M0L6mvMod5S666 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS657
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS650, _M0L1qS658);
      } else if (_M0L4evenS649) {
        uint64_t _M0L6_2atmpS2700 = _M0L2mvS650 - 1ull;
        uint64_t _M0L6_2atmpS2701;
        uint64_t _M0L6_2atmpS2699;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2701 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS651);
        _M0L6_2atmpS2699 = _M0L6_2atmpS2700 - _M0L6_2atmpS2701;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS656
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2699, _M0L1qS658);
      } else {
        uint64_t _M0L6_2atmpS2702 = _M0Lm2vpS653;
        uint64_t _M0L6_2atmpS2705 = _M0L2mvS650 + 2ull;
        int32_t _M0L6_2atmpS2704;
        uint64_t _M0L6_2atmpS2703;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2704
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2705, _M0L1qS658);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2703 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2704);
        _M0Lm2vpS653 = _M0L6_2atmpS2702 - _M0L6_2atmpS2703;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2734 = _M0Lm2e2S645;
    int32_t _M0L6_2atmpS2733 = -_M0L6_2atmpS2734;
    int32_t _M0L6_2atmpS2728;
    int32_t _M0L6_2atmpS2732;
    int32_t _M0L6_2atmpS2731;
    int32_t _M0L6_2atmpS2730;
    int32_t _M0L6_2atmpS2729;
    int32_t _M0L1qS667;
    int32_t _M0L6_2atmpS2721;
    int32_t _M0L6_2atmpS2727;
    int32_t _M0L6_2atmpS2726;
    int32_t _M0L1iS668;
    int32_t _M0L6_2atmpS2725;
    int32_t _M0L1kS669;
    int32_t _M0L1jS670;
    struct _M0TPB8Pow5Pair _M0L4pow5S671;
    uint64_t _M0L6_2atmpS2724;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS672;
    uint64_t _M0L8_2avrOutS673;
    uint64_t _M0L8_2avpOutS674;
    uint64_t _M0L8_2avmOutS675;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2728 = _M0FPB9log10Pow5(_M0L6_2atmpS2733);
    _M0L6_2atmpS2732 = _M0Lm2e2S645;
    _M0L6_2atmpS2731 = -_M0L6_2atmpS2732;
    _M0L6_2atmpS2730 = _M0L6_2atmpS2731 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2729 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2730);
    _M0L1qS667 = _M0L6_2atmpS2728 - _M0L6_2atmpS2729;
    _M0L6_2atmpS2721 = _M0Lm2e2S645;
    _M0Lm3e10S655 = _M0L1qS667 + _M0L6_2atmpS2721;
    _M0L6_2atmpS2727 = _M0Lm2e2S645;
    _M0L6_2atmpS2726 = -_M0L6_2atmpS2727;
    _M0L1iS668 = _M0L6_2atmpS2726 - _M0L1qS667;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2725 = _M0FPB8pow5bits(_M0L1iS668);
    _M0L1kS669 = _M0L6_2atmpS2725 - 125;
    _M0L1jS670 = _M0L1qS667 - _M0L1kS669;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S671 = _M0FPB19double__computePow5(_M0L1iS668);
    _M0L6_2atmpS2724 = _M0Lm2m2S646;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS672
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2724, _M0L4pow5S671, _M0L1jS670, _M0L7mmShiftS651);
    _M0L8_2avrOutS673 = _M0L7_2abindS672.$0;
    _M0L8_2avpOutS674 = _M0L7_2abindS672.$1;
    _M0L8_2avmOutS675 = _M0L7_2abindS672.$2;
    _M0Lm2vrS652 = _M0L8_2avrOutS673;
    _M0Lm2vpS653 = _M0L8_2avpOutS674;
    _M0Lm2vmS654 = _M0L8_2avmOutS675;
    if (_M0L1qS667 <= 1) {
      _M0Lm17vrIsTrailingZerosS657 = 1;
      if (_M0L4evenS649) {
        int32_t _M0L6_2atmpS2722;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2722 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS651);
        _M0Lm17vmIsTrailingZerosS656 = _M0L6_2atmpS2722 == 1;
      } else {
        uint64_t _M0L6_2atmpS2723 = _M0Lm2vpS653;
        _M0Lm2vpS653 = _M0L6_2atmpS2723 - 1ull;
      }
    } else if (_M0L1qS667 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS657
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS650, _M0L1qS667);
    }
  }
  _M0Lm7removedS676 = 0;
  _M0Lm16lastRemovedDigitS677 = 0;
  _M0Lm6outputS678 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS656 || _M0Lm17vrIsTrailingZerosS657) {
    int32_t _if__result_6128;
    uint64_t _M0L6_2atmpS2764;
    uint64_t _M0L6_2atmpS2770;
    uint64_t _M0L6_2atmpS2771;
    int32_t _if__result_6129;
    int32_t _M0L6_2atmpS2767;
    int64_t _M0L6_2atmpS2766;
    uint64_t _M0L6_2atmpS2765;
    while (1) {
      uint64_t _M0L6_2atmpS2747 = _M0Lm2vpS653;
      uint64_t _M0L7vpDiv10S679 = _M0L6_2atmpS2747 / 10ull;
      uint64_t _M0L6_2atmpS2746 = _M0Lm2vmS654;
      uint64_t _M0L7vmDiv10S680 = _M0L6_2atmpS2746 / 10ull;
      uint64_t _M0L6_2atmpS2745;
      int32_t _M0L6_2atmpS2742;
      int32_t _M0L6_2atmpS2744;
      int32_t _M0L6_2atmpS2743;
      int32_t _M0L7vmMod10S682;
      uint64_t _M0L6_2atmpS2741;
      uint64_t _M0L7vrDiv10S683;
      uint64_t _M0L6_2atmpS2740;
      int32_t _M0L6_2atmpS2737;
      int32_t _M0L6_2atmpS2739;
      int32_t _M0L6_2atmpS2738;
      int32_t _M0L7vrMod10S684;
      int32_t _M0L6_2atmpS2736;
      if (_M0L7vpDiv10S679 <= _M0L7vmDiv10S680) {
        break;
      }
      _M0L6_2atmpS2745 = _M0Lm2vmS654;
      _M0L6_2atmpS2742 = (int32_t)_M0L6_2atmpS2745;
      _M0L6_2atmpS2744 = (int32_t)_M0L7vmDiv10S680;
      _M0L6_2atmpS2743 = 10 * _M0L6_2atmpS2744;
      _M0L7vmMod10S682 = _M0L6_2atmpS2742 - _M0L6_2atmpS2743;
      _M0L6_2atmpS2741 = _M0Lm2vrS652;
      _M0L7vrDiv10S683 = _M0L6_2atmpS2741 / 10ull;
      _M0L6_2atmpS2740 = _M0Lm2vrS652;
      _M0L6_2atmpS2737 = (int32_t)_M0L6_2atmpS2740;
      _M0L6_2atmpS2739 = (int32_t)_M0L7vrDiv10S683;
      _M0L6_2atmpS2738 = 10 * _M0L6_2atmpS2739;
      _M0L7vrMod10S684 = _M0L6_2atmpS2737 - _M0L6_2atmpS2738;
      _M0Lm17vmIsTrailingZerosS656
      = _M0Lm17vmIsTrailingZerosS656 && _M0L7vmMod10S682 == 0;
      if (_M0Lm17vrIsTrailingZerosS657) {
        int32_t _M0L6_2atmpS2735 = _M0Lm16lastRemovedDigitS677;
        _M0Lm17vrIsTrailingZerosS657 = _M0L6_2atmpS2735 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS657 = 0;
      }
      _M0Lm16lastRemovedDigitS677 = _M0L7vrMod10S684;
      _M0Lm2vrS652 = _M0L7vrDiv10S683;
      _M0Lm2vpS653 = _M0L7vpDiv10S679;
      _M0Lm2vmS654 = _M0L7vmDiv10S680;
      _M0L6_2atmpS2736 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2736 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS656) {
      while (1) {
        uint64_t _M0L6_2atmpS2760 = _M0Lm2vmS654;
        uint64_t _M0L7vmDiv10S685 = _M0L6_2atmpS2760 / 10ull;
        uint64_t _M0L6_2atmpS2759 = _M0Lm2vmS654;
        int32_t _M0L6_2atmpS2756 = (int32_t)_M0L6_2atmpS2759;
        int32_t _M0L6_2atmpS2758 = (int32_t)_M0L7vmDiv10S685;
        int32_t _M0L6_2atmpS2757 = 10 * _M0L6_2atmpS2758;
        int32_t _M0L7vmMod10S686 = _M0L6_2atmpS2756 - _M0L6_2atmpS2757;
        uint64_t _M0L6_2atmpS2755;
        uint64_t _M0L7vpDiv10S688;
        uint64_t _M0L6_2atmpS2754;
        uint64_t _M0L7vrDiv10S689;
        uint64_t _M0L6_2atmpS2753;
        int32_t _M0L6_2atmpS2750;
        int32_t _M0L6_2atmpS2752;
        int32_t _M0L6_2atmpS2751;
        int32_t _M0L7vrMod10S690;
        int32_t _M0L6_2atmpS2749;
        if (_M0L7vmMod10S686 != 0) {
          break;
        }
        _M0L6_2atmpS2755 = _M0Lm2vpS653;
        _M0L7vpDiv10S688 = _M0L6_2atmpS2755 / 10ull;
        _M0L6_2atmpS2754 = _M0Lm2vrS652;
        _M0L7vrDiv10S689 = _M0L6_2atmpS2754 / 10ull;
        _M0L6_2atmpS2753 = _M0Lm2vrS652;
        _M0L6_2atmpS2750 = (int32_t)_M0L6_2atmpS2753;
        _M0L6_2atmpS2752 = (int32_t)_M0L7vrDiv10S689;
        _M0L6_2atmpS2751 = 10 * _M0L6_2atmpS2752;
        _M0L7vrMod10S690 = _M0L6_2atmpS2750 - _M0L6_2atmpS2751;
        if (_M0Lm17vrIsTrailingZerosS657) {
          int32_t _M0L6_2atmpS2748 = _M0Lm16lastRemovedDigitS677;
          _M0Lm17vrIsTrailingZerosS657 = _M0L6_2atmpS2748 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS657 = 0;
        }
        _M0Lm16lastRemovedDigitS677 = _M0L7vrMod10S690;
        _M0Lm2vrS652 = _M0L7vrDiv10S689;
        _M0Lm2vpS653 = _M0L7vpDiv10S688;
        _M0Lm2vmS654 = _M0L7vmDiv10S685;
        _M0L6_2atmpS2749 = _M0Lm7removedS676;
        _M0Lm7removedS676 = _M0L6_2atmpS2749 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS657) {
      int32_t _M0L6_2atmpS2763 = _M0Lm16lastRemovedDigitS677;
      if (_M0L6_2atmpS2763 == 5) {
        uint64_t _M0L6_2atmpS2762 = _M0Lm2vrS652;
        uint64_t _M0L6_2atmpS2761 = _M0L6_2atmpS2762 % 2ull;
        _if__result_6128 = _M0L6_2atmpS2761 == 0ull;
      } else {
        _if__result_6128 = 0;
      }
    } else {
      _if__result_6128 = 0;
    }
    if (_if__result_6128) {
      _M0Lm16lastRemovedDigitS677 = 4;
    }
    _M0L6_2atmpS2764 = _M0Lm2vrS652;
    _M0L6_2atmpS2770 = _M0Lm2vrS652;
    _M0L6_2atmpS2771 = _M0Lm2vmS654;
    if (_M0L6_2atmpS2770 == _M0L6_2atmpS2771) {
      if (!_M0L4evenS649) {
        _if__result_6129 = 1;
      } else {
        int32_t _M0L6_2atmpS2769 = _M0Lm17vmIsTrailingZerosS656;
        _if__result_6129 = !_M0L6_2atmpS2769;
      }
    } else {
      _if__result_6129 = 0;
    }
    if (_if__result_6129) {
      _M0L6_2atmpS2767 = 1;
    } else {
      int32_t _M0L6_2atmpS2768 = _M0Lm16lastRemovedDigitS677;
      _M0L6_2atmpS2767 = _M0L6_2atmpS2768 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2766 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2767);
    _M0L6_2atmpS2765 = *(uint64_t*)&_M0L6_2atmpS2766;
    _M0Lm6outputS678 = _M0L6_2atmpS2764 + _M0L6_2atmpS2765;
  } else {
    int32_t _M0Lm7roundUpS691 = 0;
    uint64_t _M0L6_2atmpS2792 = _M0Lm2vpS653;
    uint64_t _M0L8vpDiv100S692 = _M0L6_2atmpS2792 / 100ull;
    uint64_t _M0L6_2atmpS2791 = _M0Lm2vmS654;
    uint64_t _M0L8vmDiv100S693 = _M0L6_2atmpS2791 / 100ull;
    uint64_t _M0L6_2atmpS2786;
    uint64_t _M0L6_2atmpS2789;
    uint64_t _M0L6_2atmpS2790;
    int32_t _M0L6_2atmpS2788;
    uint64_t _M0L6_2atmpS2787;
    if (_M0L8vpDiv100S692 > _M0L8vmDiv100S693) {
      uint64_t _M0L6_2atmpS2777 = _M0Lm2vrS652;
      uint64_t _M0L8vrDiv100S694 = _M0L6_2atmpS2777 / 100ull;
      uint64_t _M0L6_2atmpS2776 = _M0Lm2vrS652;
      int32_t _M0L6_2atmpS2773 = (int32_t)_M0L6_2atmpS2776;
      int32_t _M0L6_2atmpS2775 = (int32_t)_M0L8vrDiv100S694;
      int32_t _M0L6_2atmpS2774 = 100 * _M0L6_2atmpS2775;
      int32_t _M0L8vrMod100S695 = _M0L6_2atmpS2773 - _M0L6_2atmpS2774;
      int32_t _M0L6_2atmpS2772;
      _M0Lm7roundUpS691 = _M0L8vrMod100S695 >= 50;
      _M0Lm2vrS652 = _M0L8vrDiv100S694;
      _M0Lm2vpS653 = _M0L8vpDiv100S692;
      _M0Lm2vmS654 = _M0L8vmDiv100S693;
      _M0L6_2atmpS2772 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2772 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2785 = _M0Lm2vpS653;
      uint64_t _M0L7vpDiv10S696 = _M0L6_2atmpS2785 / 10ull;
      uint64_t _M0L6_2atmpS2784 = _M0Lm2vmS654;
      uint64_t _M0L7vmDiv10S697 = _M0L6_2atmpS2784 / 10ull;
      uint64_t _M0L6_2atmpS2783;
      uint64_t _M0L7vrDiv10S699;
      uint64_t _M0L6_2atmpS2782;
      int32_t _M0L6_2atmpS2779;
      int32_t _M0L6_2atmpS2781;
      int32_t _M0L6_2atmpS2780;
      int32_t _M0L7vrMod10S700;
      int32_t _M0L6_2atmpS2778;
      if (_M0L7vpDiv10S696 <= _M0L7vmDiv10S697) {
        break;
      }
      _M0L6_2atmpS2783 = _M0Lm2vrS652;
      _M0L7vrDiv10S699 = _M0L6_2atmpS2783 / 10ull;
      _M0L6_2atmpS2782 = _M0Lm2vrS652;
      _M0L6_2atmpS2779 = (int32_t)_M0L6_2atmpS2782;
      _M0L6_2atmpS2781 = (int32_t)_M0L7vrDiv10S699;
      _M0L6_2atmpS2780 = 10 * _M0L6_2atmpS2781;
      _M0L7vrMod10S700 = _M0L6_2atmpS2779 - _M0L6_2atmpS2780;
      _M0Lm7roundUpS691 = _M0L7vrMod10S700 >= 5;
      _M0Lm2vrS652 = _M0L7vrDiv10S699;
      _M0Lm2vpS653 = _M0L7vpDiv10S696;
      _M0Lm2vmS654 = _M0L7vmDiv10S697;
      _M0L6_2atmpS2778 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2778 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2786 = _M0Lm2vrS652;
    _M0L6_2atmpS2789 = _M0Lm2vrS652;
    _M0L6_2atmpS2790 = _M0Lm2vmS654;
    _M0L6_2atmpS2788
    = _M0L6_2atmpS2789 == _M0L6_2atmpS2790 || _M0Lm7roundUpS691;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2787 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2788);
    _M0Lm6outputS678 = _M0L6_2atmpS2786 + _M0L6_2atmpS2787;
  }
  _M0L6_2atmpS2794 = _M0Lm3e10S655;
  _M0L6_2atmpS2795 = _M0Lm7removedS676;
  _M0L3expS701 = _M0L6_2atmpS2794 + _M0L6_2atmpS2795;
  _M0L6_2atmpS2793 = _M0Lm6outputS678;
  _block_6131
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_6131)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6131->$0 = _M0L6_2atmpS2793;
  _block_6131->$1 = _M0L3expS701;
  return _block_6131;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS644) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS644) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS643) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS643) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS642) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS642) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS641) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS641 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS641 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS641 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS641 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS641 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS641 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS641 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS641 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS641 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS641 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS641 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS641 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS641 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS641 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS641 >= 100ull) {
    return 3;
  }
  if (_M0L1vS641 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS624) {
  int32_t _M0L6_2atmpS2694;
  int32_t _M0L6_2atmpS2693;
  int32_t _M0L4baseS623;
  int32_t _M0L5base2S625;
  int32_t _M0L6offsetS626;
  int32_t _M0L6_2atmpS2692;
  uint64_t _M0L4mul0S627;
  int32_t _M0L6_2atmpS2691;
  int32_t _M0L6_2atmpS2690;
  uint64_t _M0L4mul1S628;
  uint64_t _M0L1mS629;
  struct _M0TPB7Umul128 _M0L7_2abindS630;
  uint64_t _M0L7_2alow1S631;
  uint64_t _M0L8_2ahigh1S632;
  struct _M0TPB7Umul128 _M0L7_2abindS633;
  uint64_t _M0L7_2alow0S634;
  uint64_t _M0L8_2ahigh0S635;
  uint64_t _M0L3sumS636;
  uint64_t _M0Lm5high1S637;
  int32_t _M0L6_2atmpS2688;
  int32_t _M0L6_2atmpS2689;
  int32_t _M0L5deltaS638;
  uint64_t _M0L6_2atmpS2687;
  uint64_t _M0L6_2atmpS2679;
  int32_t _M0L6_2atmpS2686;
  uint32_t _M0L6_2atmpS2683;
  int32_t _M0L6_2atmpS2685;
  int32_t _M0L6_2atmpS2684;
  uint32_t _M0L6_2atmpS2682;
  uint32_t _M0L6_2atmpS2681;
  uint64_t _M0L6_2atmpS2680;
  uint64_t _M0L1aS639;
  uint64_t _M0L6_2atmpS2678;
  uint64_t _M0L1bS640;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2694 = _M0L1iS624 + 26;
  _M0L6_2atmpS2693 = _M0L6_2atmpS2694 - 1;
  _M0L4baseS623 = _M0L6_2atmpS2693 / 26;
  _M0L5base2S625 = _M0L4baseS623 * 26;
  _M0L6offsetS626 = _M0L5base2S625 - _M0L1iS624;
  _M0L6_2atmpS2692 = _M0L4baseS623 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2692);
  _M0L6_2atmpS2691 = _M0L4baseS623 * 2;
  _M0L6_2atmpS2690 = _M0L6_2atmpS2691 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S628
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2690);
  if (_M0L6offsetS626 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S627, .$1 = _M0L4mul1S628};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS629
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS626);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS630 = _M0FPB7umul128(_M0L1mS629, _M0L4mul1S628);
  _M0L7_2alow1S631 = _M0L7_2abindS630.$0;
  _M0L8_2ahigh1S632 = _M0L7_2abindS630.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS633 = _M0FPB7umul128(_M0L1mS629, _M0L4mul0S627);
  _M0L7_2alow0S634 = _M0L7_2abindS633.$0;
  _M0L8_2ahigh0S635 = _M0L7_2abindS633.$1;
  _M0L3sumS636 = _M0L8_2ahigh0S635 + _M0L7_2alow1S631;
  _M0Lm5high1S637 = _M0L8_2ahigh1S632;
  if (_M0L3sumS636 < _M0L8_2ahigh0S635) {
    uint64_t _M0L6_2atmpS2677 = _M0Lm5high1S637;
    _M0Lm5high1S637 = _M0L6_2atmpS2677 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2688 = _M0FPB8pow5bits(_M0L5base2S625);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2689 = _M0FPB8pow5bits(_M0L1iS624);
  _M0L5deltaS638 = _M0L6_2atmpS2688 - _M0L6_2atmpS2689;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2687
  = _M0FPB13shiftright128(_M0L7_2alow0S634, _M0L3sumS636, _M0L5deltaS638);
  _M0L6_2atmpS2679 = _M0L6_2atmpS2687 + 1ull;
  _M0L6_2atmpS2686 = _M0L1iS624 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2683
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS2686);
  _M0L6_2atmpS2685 = _M0L1iS624 % 16;
  _M0L6_2atmpS2684 = _M0L6_2atmpS2685 << 1;
  _M0L6_2atmpS2682 = _M0L6_2atmpS2683 >> (_M0L6_2atmpS2684 & 31);
  _M0L6_2atmpS2681 = _M0L6_2atmpS2682 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2680 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2681);
  _M0L1aS639 = _M0L6_2atmpS2679 + _M0L6_2atmpS2680;
  _M0L6_2atmpS2678 = _M0Lm5high1S637;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS640
  = _M0FPB13shiftright128(_M0L3sumS636, _M0L6_2atmpS2678, _M0L5deltaS638);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS639, .$1 = _M0L1bS640};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS606) {
  int32_t _M0L4baseS605;
  int32_t _M0L5base2S607;
  int32_t _M0L6offsetS608;
  int32_t _M0L6_2atmpS2676;
  uint64_t _M0L4mul0S609;
  int32_t _M0L6_2atmpS2675;
  int32_t _M0L6_2atmpS2674;
  uint64_t _M0L4mul1S610;
  uint64_t _M0L1mS611;
  struct _M0TPB7Umul128 _M0L7_2abindS612;
  uint64_t _M0L7_2alow1S613;
  uint64_t _M0L8_2ahigh1S614;
  struct _M0TPB7Umul128 _M0L7_2abindS615;
  uint64_t _M0L7_2alow0S616;
  uint64_t _M0L8_2ahigh0S617;
  uint64_t _M0L3sumS618;
  uint64_t _M0Lm5high1S619;
  int32_t _M0L6_2atmpS2672;
  int32_t _M0L6_2atmpS2673;
  int32_t _M0L5deltaS620;
  uint64_t _M0L6_2atmpS2664;
  int32_t _M0L6_2atmpS2671;
  uint32_t _M0L6_2atmpS2668;
  int32_t _M0L6_2atmpS2670;
  int32_t _M0L6_2atmpS2669;
  uint32_t _M0L6_2atmpS2667;
  uint32_t _M0L6_2atmpS2666;
  uint64_t _M0L6_2atmpS2665;
  uint64_t _M0L1aS621;
  uint64_t _M0L6_2atmpS2663;
  uint64_t _M0L1bS622;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS605 = _M0L1iS606 / 26;
  _M0L5base2S607 = _M0L4baseS605 * 26;
  _M0L6offsetS608 = _M0L1iS606 - _M0L5base2S607;
  _M0L6_2atmpS2676 = _M0L4baseS605 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2676);
  _M0L6_2atmpS2675 = _M0L4baseS605 * 2;
  _M0L6_2atmpS2674 = _M0L6_2atmpS2675 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S610
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2674);
  if (_M0L6offsetS608 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S609, .$1 = _M0L4mul1S610};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS611
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS608);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS612 = _M0FPB7umul128(_M0L1mS611, _M0L4mul1S610);
  _M0L7_2alow1S613 = _M0L7_2abindS612.$0;
  _M0L8_2ahigh1S614 = _M0L7_2abindS612.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS615 = _M0FPB7umul128(_M0L1mS611, _M0L4mul0S609);
  _M0L7_2alow0S616 = _M0L7_2abindS615.$0;
  _M0L8_2ahigh0S617 = _M0L7_2abindS615.$1;
  _M0L3sumS618 = _M0L8_2ahigh0S617 + _M0L7_2alow1S613;
  _M0Lm5high1S619 = _M0L8_2ahigh1S614;
  if (_M0L3sumS618 < _M0L8_2ahigh0S617) {
    uint64_t _M0L6_2atmpS2662 = _M0Lm5high1S619;
    _M0Lm5high1S619 = _M0L6_2atmpS2662 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2672 = _M0FPB8pow5bits(_M0L1iS606);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2673 = _M0FPB8pow5bits(_M0L5base2S607);
  _M0L5deltaS620 = _M0L6_2atmpS2672 - _M0L6_2atmpS2673;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2664
  = _M0FPB13shiftright128(_M0L7_2alow0S616, _M0L3sumS618, _M0L5deltaS620);
  _M0L6_2atmpS2671 = _M0L1iS606 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2668
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS2671);
  _M0L6_2atmpS2670 = _M0L1iS606 % 16;
  _M0L6_2atmpS2669 = _M0L6_2atmpS2670 << 1;
  _M0L6_2atmpS2667 = _M0L6_2atmpS2668 >> (_M0L6_2atmpS2669 & 31);
  _M0L6_2atmpS2666 = _M0L6_2atmpS2667 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2665 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2666);
  _M0L1aS621 = _M0L6_2atmpS2664 + _M0L6_2atmpS2665;
  _M0L6_2atmpS2663 = _M0Lm5high1S619;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS622
  = _M0FPB13shiftright128(_M0L3sumS618, _M0L6_2atmpS2663, _M0L5deltaS620);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS621, .$1 = _M0L1bS622};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS579,
  struct _M0TPB8Pow5Pair _M0L3mulS576,
  int32_t _M0L1jS592,
  int32_t _M0L7mmShiftS594
) {
  uint64_t _M0L7_2amul0S575;
  uint64_t _M0L7_2amul1S577;
  uint64_t _M0L1mS578;
  struct _M0TPB7Umul128 _M0L7_2abindS580;
  uint64_t _M0L5_2aloS581;
  uint64_t _M0L6_2atmpS582;
  struct _M0TPB7Umul128 _M0L7_2abindS583;
  uint64_t _M0L6_2alo2S584;
  uint64_t _M0L6_2ahi2S585;
  uint64_t _M0L3midS586;
  uint64_t _M0L6_2atmpS2661;
  uint64_t _M0L2hiS587;
  uint64_t _M0L3lo2S588;
  uint64_t _M0L6_2atmpS2659;
  uint64_t _M0L6_2atmpS2660;
  uint64_t _M0L4mid2S589;
  uint64_t _M0L6_2atmpS2658;
  uint64_t _M0L3hi2S590;
  int32_t _M0L6_2atmpS2657;
  int32_t _M0L6_2atmpS2656;
  uint64_t _M0L2vpS591;
  uint64_t _M0Lm2vmS593;
  int32_t _M0L6_2atmpS2655;
  int32_t _M0L6_2atmpS2654;
  uint64_t _M0L2vrS604;
  uint64_t _M0L6_2atmpS2653;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S575 = _M0L3mulS576.$0;
  _M0L7_2amul1S577 = _M0L3mulS576.$1;
  _M0L1mS578 = _M0L1mS579 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS580 = _M0FPB7umul128(_M0L1mS578, _M0L7_2amul0S575);
  _M0L5_2aloS581 = _M0L7_2abindS580.$0;
  _M0L6_2atmpS582 = _M0L7_2abindS580.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS583 = _M0FPB7umul128(_M0L1mS578, _M0L7_2amul1S577);
  _M0L6_2alo2S584 = _M0L7_2abindS583.$0;
  _M0L6_2ahi2S585 = _M0L7_2abindS583.$1;
  _M0L3midS586 = _M0L6_2atmpS582 + _M0L6_2alo2S584;
  if (_M0L3midS586 < _M0L6_2atmpS582) {
    _M0L6_2atmpS2661 = 1ull;
  } else {
    _M0L6_2atmpS2661 = 0ull;
  }
  _M0L2hiS587 = _M0L6_2ahi2S585 + _M0L6_2atmpS2661;
  _M0L3lo2S588 = _M0L5_2aloS581 + _M0L7_2amul0S575;
  _M0L6_2atmpS2659 = _M0L3midS586 + _M0L7_2amul1S577;
  if (_M0L3lo2S588 < _M0L5_2aloS581) {
    _M0L6_2atmpS2660 = 1ull;
  } else {
    _M0L6_2atmpS2660 = 0ull;
  }
  _M0L4mid2S589 = _M0L6_2atmpS2659 + _M0L6_2atmpS2660;
  if (_M0L4mid2S589 < _M0L3midS586) {
    _M0L6_2atmpS2658 = 1ull;
  } else {
    _M0L6_2atmpS2658 = 0ull;
  }
  _M0L3hi2S590 = _M0L2hiS587 + _M0L6_2atmpS2658;
  _M0L6_2atmpS2657 = _M0L1jS592 - 64;
  _M0L6_2atmpS2656 = _M0L6_2atmpS2657 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS591
  = _M0FPB13shiftright128(_M0L4mid2S589, _M0L3hi2S590, _M0L6_2atmpS2656);
  _M0Lm2vmS593 = 0ull;
  if (_M0L7mmShiftS594) {
    uint64_t _M0L3lo3S595 = _M0L5_2aloS581 - _M0L7_2amul0S575;
    uint64_t _M0L6_2atmpS2643 = _M0L3midS586 - _M0L7_2amul1S577;
    uint64_t _M0L6_2atmpS2644;
    uint64_t _M0L4mid3S596;
    uint64_t _M0L6_2atmpS2642;
    uint64_t _M0L3hi3S597;
    int32_t _M0L6_2atmpS2641;
    int32_t _M0L6_2atmpS2640;
    if (_M0L5_2aloS581 < _M0L3lo3S595) {
      _M0L6_2atmpS2644 = 1ull;
    } else {
      _M0L6_2atmpS2644 = 0ull;
    }
    _M0L4mid3S596 = _M0L6_2atmpS2643 - _M0L6_2atmpS2644;
    if (_M0L3midS586 < _M0L4mid3S596) {
      _M0L6_2atmpS2642 = 1ull;
    } else {
      _M0L6_2atmpS2642 = 0ull;
    }
    _M0L3hi3S597 = _M0L2hiS587 - _M0L6_2atmpS2642;
    _M0L6_2atmpS2641 = _M0L1jS592 - 64;
    _M0L6_2atmpS2640 = _M0L6_2atmpS2641 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS593
    = _M0FPB13shiftright128(_M0L4mid3S596, _M0L3hi3S597, _M0L6_2atmpS2640);
  } else {
    uint64_t _M0L3lo3S598 = _M0L5_2aloS581 + _M0L5_2aloS581;
    uint64_t _M0L6_2atmpS2651 = _M0L3midS586 + _M0L3midS586;
    uint64_t _M0L6_2atmpS2652;
    uint64_t _M0L4mid3S599;
    uint64_t _M0L6_2atmpS2649;
    uint64_t _M0L6_2atmpS2650;
    uint64_t _M0L3hi3S600;
    uint64_t _M0L3lo4S601;
    uint64_t _M0L6_2atmpS2647;
    uint64_t _M0L6_2atmpS2648;
    uint64_t _M0L4mid4S602;
    uint64_t _M0L6_2atmpS2646;
    uint64_t _M0L3hi4S603;
    int32_t _M0L6_2atmpS2645;
    if (_M0L3lo3S598 < _M0L5_2aloS581) {
      _M0L6_2atmpS2652 = 1ull;
    } else {
      _M0L6_2atmpS2652 = 0ull;
    }
    _M0L4mid3S599 = _M0L6_2atmpS2651 + _M0L6_2atmpS2652;
    _M0L6_2atmpS2649 = _M0L2hiS587 + _M0L2hiS587;
    if (_M0L4mid3S599 < _M0L3midS586) {
      _M0L6_2atmpS2650 = 1ull;
    } else {
      _M0L6_2atmpS2650 = 0ull;
    }
    _M0L3hi3S600 = _M0L6_2atmpS2649 + _M0L6_2atmpS2650;
    _M0L3lo4S601 = _M0L3lo3S598 - _M0L7_2amul0S575;
    _M0L6_2atmpS2647 = _M0L4mid3S599 - _M0L7_2amul1S577;
    if (_M0L3lo3S598 < _M0L3lo4S601) {
      _M0L6_2atmpS2648 = 1ull;
    } else {
      _M0L6_2atmpS2648 = 0ull;
    }
    _M0L4mid4S602 = _M0L6_2atmpS2647 - _M0L6_2atmpS2648;
    if (_M0L4mid3S599 < _M0L4mid4S602) {
      _M0L6_2atmpS2646 = 1ull;
    } else {
      _M0L6_2atmpS2646 = 0ull;
    }
    _M0L3hi4S603 = _M0L3hi3S600 - _M0L6_2atmpS2646;
    _M0L6_2atmpS2645 = _M0L1jS592 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS593
    = _M0FPB13shiftright128(_M0L4mid4S602, _M0L3hi4S603, _M0L6_2atmpS2645);
  }
  _M0L6_2atmpS2655 = _M0L1jS592 - 64;
  _M0L6_2atmpS2654 = _M0L6_2atmpS2655 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS604
  = _M0FPB13shiftright128(_M0L3midS586, _M0L2hiS587, _M0L6_2atmpS2654);
  _M0L6_2atmpS2653 = _M0Lm2vmS593;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS604,
                                                .$1 = _M0L2vpS591,
                                                .$2 = _M0L6_2atmpS2653};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS573,
  int32_t _M0L1pS574
) {
  uint64_t _M0L6_2atmpS2639;
  uint64_t _M0L6_2atmpS2638;
  uint64_t _M0L6_2atmpS2637;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2639 = 1ull << (_M0L1pS574 & 63);
  _M0L6_2atmpS2638 = _M0L6_2atmpS2639 - 1ull;
  _M0L6_2atmpS2637 = _M0L5valueS573 & _M0L6_2atmpS2638;
  return _M0L6_2atmpS2637 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS571,
  int32_t _M0L1pS572
) {
  int32_t _M0L6_2atmpS2636;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2636 = _M0FPB10pow5Factor(_M0L5valueS571);
  return _M0L6_2atmpS2636 >= _M0L1pS572;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS566) {
  uint64_t _M0L6_2atmpS2627;
  uint64_t _M0L6_2atmpS2628;
  uint64_t _M0L6_2atmpS2629;
  uint64_t _M0L6_2atmpS2630;
  uint64_t _M0L6_2atmpS2635;
  int32_t _M0L5countS567;
  uint64_t _M0L1vS568;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2627 = _M0L5valueS566 % 5ull;
  if (_M0L6_2atmpS2627 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2628 = _M0L5valueS566 % 25ull;
  if (_M0L6_2atmpS2628 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS2629 = _M0L5valueS566 % 125ull;
  if (_M0L6_2atmpS2629 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS2630 = _M0L5valueS566 % 625ull;
  if (_M0L6_2atmpS2630 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS2635 = _M0L5valueS566 / 625ull;
  _M0L5countS567 = 4;
  _M0L1vS568 = _M0L6_2atmpS2635;
  while (1) {
    if (_M0L1vS568 > 0ull) {
      uint64_t _M0L6_2atmpS2631 = _M0L1vS568 % 5ull;
      int32_t _M0L6_2atmpS2632;
      uint64_t _M0L6_2atmpS2633;
      if (_M0L6_2atmpS2631 != 0ull) {
        return _M0L5countS567;
      }
      _M0L6_2atmpS2632 = _M0L5countS567 + 1;
      _M0L6_2atmpS2633 = _M0L1vS568 / 5ull;
      _M0L5countS567 = _M0L6_2atmpS2632;
      _M0L1vS568 = _M0L6_2atmpS2633;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS570;
      moonbit_string_t _M0L6_2atmpS2634;
      int32_t _result_6133;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS570
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS570, (moonbit_string_t)moonbit_string_literal_13.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS570, _M0L5valueS566);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS2634
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS570);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS570);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_6133 = _M0FPC15abort5abortGiE(_M0L6_2atmpS2634);
      moonbit_decref_cycle_free(_M0L6_2atmpS2634);
      return _result_6133;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS565,
  uint64_t _M0L2hiS563,
  int32_t _M0L4distS564
) {
  int32_t _M0L6_2atmpS2626;
  uint64_t _M0L6_2atmpS2624;
  uint64_t _M0L6_2atmpS2625;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2626 = 64 - _M0L4distS564;
  _M0L6_2atmpS2624 = _M0L2hiS563 << (_M0L6_2atmpS2626 & 63);
  _M0L6_2atmpS2625 = _M0L2loS565 >> (_M0L4distS564 & 63);
  return _M0L6_2atmpS2624 | _M0L6_2atmpS2625;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS553,
  uint64_t _M0L1bS556
) {
  uint64_t _M0L3aLoS552;
  uint64_t _M0L3aHiS554;
  uint64_t _M0L3bLoS555;
  uint64_t _M0L3bHiS557;
  uint64_t _M0L1xS558;
  uint64_t _M0L6_2atmpS2622;
  uint64_t _M0L6_2atmpS2623;
  uint64_t _M0L1yS559;
  uint64_t _M0L6_2atmpS2620;
  uint64_t _M0L6_2atmpS2621;
  uint64_t _M0L1zS560;
  uint64_t _M0L6_2atmpS2618;
  uint64_t _M0L6_2atmpS2619;
  uint64_t _M0L6_2atmpS2616;
  uint64_t _M0L6_2atmpS2617;
  uint64_t _M0L1wS561;
  uint64_t _M0L2loS562;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS552 = _M0L1aS553 & 4294967295ull;
  _M0L3aHiS554 = _M0L1aS553 >> 32;
  _M0L3bLoS555 = _M0L1bS556 & 4294967295ull;
  _M0L3bHiS557 = _M0L1bS556 >> 32;
  _M0L1xS558 = _M0L3aLoS552 * _M0L3bLoS555;
  _M0L6_2atmpS2622 = _M0L3aHiS554 * _M0L3bLoS555;
  _M0L6_2atmpS2623 = _M0L1xS558 >> 32;
  _M0L1yS559 = _M0L6_2atmpS2622 + _M0L6_2atmpS2623;
  _M0L6_2atmpS2620 = _M0L3aLoS552 * _M0L3bHiS557;
  _M0L6_2atmpS2621 = _M0L1yS559 & 4294967295ull;
  _M0L1zS560 = _M0L6_2atmpS2620 + _M0L6_2atmpS2621;
  _M0L6_2atmpS2618 = _M0L3aHiS554 * _M0L3bHiS557;
  _M0L6_2atmpS2619 = _M0L1yS559 >> 32;
  _M0L6_2atmpS2616 = _M0L6_2atmpS2618 + _M0L6_2atmpS2619;
  _M0L6_2atmpS2617 = _M0L1zS560 >> 32;
  _M0L1wS561 = _M0L6_2atmpS2616 + _M0L6_2atmpS2617;
  _M0L2loS562 = _M0L1aS553 * _M0L1bS556;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS562, .$1 = _M0L1wS561};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS550,
  int32_t _M0L4fromS547,
  int32_t _M0L2toS546
) {
  int32_t _M0L3lenS545;
  int32_t _M0L6_2atmpS2615;
  uint16_t* _M0L6bufferS548;
  int32_t _M0L1iS549;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS545 = _M0L2toS546 - _M0L4fromS547;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2615 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS548
  = (uint16_t*)moonbit_make_string(_M0L3lenS545, _M0L6_2atmpS2615);
  _M0L1iS549 = 0;
  while (1) {
    if (_M0L1iS549 < _M0L3lenS545) {
      int32_t _M0L6_2atmpS2613 = _M0L4fromS547 + _M0L1iS549;
      int32_t _M0L6_2atmpS2612;
      int32_t _M0L6_2atmpS2611;
      int32_t _M0L6_2atmpS2614;
      if (
        _M0L6_2atmpS2613 < 0
        || _M0L6_2atmpS2613 >= Moonbit_array_length(_M0L5bytesS550)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2612 = (int32_t)_M0L5bytesS550[_M0L6_2atmpS2613];
      _M0L6_2atmpS2611 = (uint16_t)_M0L6_2atmpS2612;
      if (
        _M0L1iS549 < 0 || _M0L1iS549 >= Moonbit_array_length(_M0L6bufferS548)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS548[_M0L1iS549] = _M0L6_2atmpS2611;
      _M0L6_2atmpS2614 = _M0L1iS549 + 1;
      _M0L1iS549 = _M0L6_2atmpS2614;
      continue;
    }
    break;
  }
  return _M0L6bufferS548;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS544) {
  int32_t _M0L6_2atmpS2610;
  uint32_t _M0L6_2atmpS2609;
  uint32_t _M0L6_2atmpS2608;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2610 = _M0L1eS544 * 78913;
  _M0L6_2atmpS2609 = *(uint32_t*)&_M0L6_2atmpS2610;
  _M0L6_2atmpS2608 = _M0L6_2atmpS2609 >> 18;
  return *(int32_t*)&_M0L6_2atmpS2608;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS543) {
  int32_t _M0L6_2atmpS2607;
  uint32_t _M0L6_2atmpS2606;
  uint32_t _M0L6_2atmpS2605;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2607 = _M0L1eS543 * 732923;
  _M0L6_2atmpS2606 = *(uint32_t*)&_M0L6_2atmpS2607;
  _M0L6_2atmpS2605 = _M0L6_2atmpS2606 >> 20;
  return *(int32_t*)&_M0L6_2atmpS2605;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS541,
  int32_t _M0L8exponentS542,
  int32_t _M0L8mantissaS539
) {
  moonbit_string_t _M0L1sS540;
  moonbit_string_t _result_6136;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS539) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L4signS541) {
    _M0L1sS540 = (moonbit_string_t)moonbit_string_literal_15.data;
  } else {
    _M0L1sS540 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS542) {
    moonbit_string_t _result_6135;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6135
    = moonbit_add_string(_M0L1sS540, (moonbit_string_t)moonbit_string_literal_16.data);
    moonbit_decref_cycle_free(_M0L1sS540);
    return _result_6135;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_6136
  = moonbit_add_string(_M0L1sS540, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref_cycle_free(_M0L1sS540);
  return _result_6136;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS538) {
  int32_t _M0L6_2atmpS2604;
  uint32_t _M0L6_2atmpS2603;
  uint32_t _M0L6_2atmpS2602;
  int32_t _M0L6_2atmpS2601;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2604 = _M0L1eS538 * 1217359;
  _M0L6_2atmpS2603 = *(uint32_t*)&_M0L6_2atmpS2604;
  _M0L6_2atmpS2602 = _M0L6_2atmpS2603 >> 19;
  _M0L6_2atmpS2601 = *(int32_t*)&_M0L6_2atmpS2602;
  return _M0L6_2atmpS2601 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS537) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS537 != _M0L4selfS537) {
    return 0;
  } else if (_M0L4selfS537 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS537 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS537;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS536) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS536 != _M0L4selfS536) {
    return 0ll;
  } else if (_M0L4selfS536 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS536 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS536;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS532
) {
  float* _M0L6_2atmpS2597;
  struct _M0TPB5ArrayGfE* _block_6137;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2597 = (float*)moonbit_make_float_array_raw(_M0L3lenS532);
  _block_6137
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_6137)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_6137->$0 = _M0L6_2atmpS2597;
  _block_6137->$1 = _M0L3lenS532;
  return _block_6137;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS533
) {
  uint8_t* _M0L6_2atmpS2598;
  struct _M0TPB5ArrayGbE* _block_6138;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2598 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS533);
  _block_6138
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_6138)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 92, 0);
  _block_6138->$0 = _M0L6_2atmpS2598;
  _block_6138->$1 = _M0L3lenS533;
  return _block_6138;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS534
) {
  int32_t* _M0L6_2atmpS2599;
  struct _M0TPB5ArrayGiE* _block_6139;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2599 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS534);
  _block_6139
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_6139)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_6139->$0 = _M0L6_2atmpS2599;
  _block_6139->$1 = _M0L3lenS534;
  return _block_6139;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS535
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS2600;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_6140;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2600
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS535, 0);
  _block_6140
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_6140)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 102, 0);
  _block_6140->$0 = _M0L6_2atmpS2600;
  _block_6140->$1 = _M0L3lenS535;
  return _block_6140;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS528,
  int32_t _M0L5indexS529
) {
  uint64_t* _M0L6_2atmpS2595;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2595 = _M0L4selfS528;
  if (
    _M0L5indexS529 < 0
    || _M0L5indexS529 >= Moonbit_array_length(_M0L6_2atmpS2595)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS2595[_M0L5indexS529];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS530,
  int32_t _M0L5indexS531
) {
  uint32_t* _M0L6_2atmpS2596;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2596 = _M0L4selfS530;
  if (
    _M0L5indexS531 < 0
    || _M0L5indexS531 >= Moonbit_array_length(_M0L6_2atmpS2596)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS2596[_M0L5indexS531];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS527
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS527, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS526) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS526, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS525) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS525;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS513,
  int32_t _M0L5valueS515
) {
  int32_t _M0L3lenS2567;
  int32_t* _M0L6_2atmpS2569;
  int32_t _M0L6_2atmpS2568;
  int32_t _M0L6lengthS514;
  int32_t* _M0L3bufS2572;
  int32_t _M0L6_2atmpS2573;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2567 = _M0L4selfS513->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2569 = _M0MPC15array5Array6bufferGiE(_M0L4selfS513);
  _M0L6_2atmpS2568 = Moonbit_array_length(_M0L6_2atmpS2569);
  moonbit_decref_cycle_free(_M0L6_2atmpS2569);
  if (_M0L3lenS2567 == _M0L6_2atmpS2568) {
    int32_t _M0L3lenS2571 = _M0L4selfS513->$1;
    int32_t _M0L6_2atmpS2570 = _M0L3lenS2571 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS513, _M0L6_2atmpS2570);
  }
  _M0L6lengthS514 = _M0L4selfS513->$1;
  _M0L3bufS2572 = _M0L4selfS513->$0;
  _M0L3bufS2572[_M0L6lengthS514] = _M0L5valueS515;
  _M0L6_2atmpS2573 = _M0L6lengthS514 + 1;
  _M0L4selfS513->$1 = _M0L6_2atmpS2573;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS516,
  moonbit_string_t _M0L5valueS518
) {
  int32_t _M0L3lenS2574;
  moonbit_string_t* _M0L6_2atmpS2576;
  int32_t _M0L6_2atmpS2575;
  int32_t _M0L6lengthS517;
  moonbit_string_t* _M0L3bufS2579;
  moonbit_string_t _M0L6_2aoldS5690;
  int32_t _M0L6_2atmpS2580;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2574 = _M0L4selfS516->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2576 = _M0MPC15array5Array6bufferGsE(_M0L4selfS516);
  _M0L6_2atmpS2575 = Moonbit_array_length(_M0L6_2atmpS2576);
  moonbit_decref_cycle_free(_M0L6_2atmpS2576);
  if (_M0L3lenS2574 == _M0L6_2atmpS2575) {
    int32_t _M0L3lenS2578 = _M0L4selfS516->$1;
    int32_t _M0L6_2atmpS2577 = _M0L3lenS2578 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS516, _M0L6_2atmpS2577);
  }
  _M0L6lengthS517 = _M0L4selfS516->$1;
  _M0L3bufS2579 = _M0L4selfS516->$0;
  _M0L6_2aoldS5690 = (moonbit_string_t)_M0L3bufS2579[_M0L6lengthS517];
  moonbit_decref_cycle_free(_M0L6_2aoldS5690);
  _M0L3bufS2579[_M0L6lengthS517] = _M0L5valueS518;
  _M0L6_2atmpS2580 = _M0L6lengthS517 + 1;
  _M0L4selfS516->$1 = _M0L6_2atmpS2580;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS519,
  struct _M0TUsiE* _M0L5valueS521
) {
  int32_t _M0L3lenS2581;
  struct _M0TUsiE** _M0L6_2atmpS2583;
  int32_t _M0L6_2atmpS2582;
  int32_t _M0L6lengthS520;
  struct _M0TUsiE** _M0L3bufS2586;
  struct _M0TUsiE* _M0L6_2aoldS5691;
  int32_t _M0L6_2atmpS2587;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2581 = _M0L4selfS519->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2583 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS519);
  _M0L6_2atmpS2582 = Moonbit_array_length(_M0L6_2atmpS2583);
  moonbit_decref_cycle_free(_M0L6_2atmpS2583);
  if (_M0L3lenS2581 == _M0L6_2atmpS2582) {
    int32_t _M0L3lenS2585 = _M0L4selfS519->$1;
    int32_t _M0L6_2atmpS2584 = _M0L3lenS2585 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS519, _M0L6_2atmpS2584);
  }
  _M0L6lengthS520 = _M0L4selfS519->$1;
  _M0L3bufS2586 = _M0L4selfS519->$0;
  _M0L6_2aoldS5691 = (struct _M0TUsiE*)_M0L3bufS2586[_M0L6lengthS520];
  if (_M0L6_2aoldS5691) {
    moonbit_decref_cycle_free(_M0L6_2aoldS5691);
  }
  _M0L3bufS2586[_M0L6lengthS520] = _M0L5valueS521;
  _M0L6_2atmpS2587 = _M0L6lengthS520 + 1;
  _M0L4selfS519->$1 = _M0L6_2atmpS2587;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS522,
  float _M0L5valueS524
) {
  int32_t _M0L3lenS2588;
  float* _M0L6_2atmpS2590;
  int32_t _M0L6_2atmpS2589;
  int32_t _M0L6lengthS523;
  float* _M0L3bufS2593;
  int32_t _M0L6_2atmpS2594;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2588 = _M0L4selfS522->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2590 = _M0MPC15array5Array6bufferGfE(_M0L4selfS522);
  _M0L6_2atmpS2589 = Moonbit_array_length(_M0L6_2atmpS2590);
  moonbit_decref_cycle_free(_M0L6_2atmpS2590);
  if (_M0L3lenS2588 == _M0L6_2atmpS2589) {
    int32_t _M0L3lenS2592 = _M0L4selfS522->$1;
    int32_t _M0L6_2atmpS2591 = _M0L3lenS2592 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS522, _M0L6_2atmpS2591);
  }
  _M0L6lengthS523 = _M0L4selfS522->$1;
  _M0L3bufS2593 = _M0L4selfS522->$0;
  _M0L3bufS2593[_M0L6lengthS523] = _M0L5valueS524;
  _M0L6_2atmpS2594 = _M0L6lengthS523 + 1;
  _M0L4selfS522->$1 = _M0L6_2atmpS2594;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS498,
  int32_t _M0L8requiredS500
) {
  int32_t _M0L8old__capS497;
  int32_t _M0L3lenS2563;
  int32_t _M0L8new__capS499;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS497 = _M0MPC15array5Array8capacityGiE(_M0L4selfS498);
  _M0L3lenS2563 = _M0L4selfS498->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS499
  = _M0FPB23array__growth__capacity(_M0L8old__capS497, _M0L3lenS2563, _M0L8requiredS500);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS498, _M0L8new__capS499);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS502,
  int32_t _M0L8requiredS504
) {
  int32_t _M0L8old__capS501;
  int32_t _M0L3lenS2564;
  int32_t _M0L8new__capS503;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS501 = _M0MPC15array5Array8capacityGsE(_M0L4selfS502);
  _M0L3lenS2564 = _M0L4selfS502->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS503
  = _M0FPB23array__growth__capacity(_M0L8old__capS501, _M0L3lenS2564, _M0L8requiredS504);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS502, _M0L8new__capS503);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS506,
  int32_t _M0L8requiredS508
) {
  int32_t _M0L8old__capS505;
  int32_t _M0L3lenS2565;
  int32_t _M0L8new__capS507;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS505 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS506);
  _M0L3lenS2565 = _M0L4selfS506->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS507
  = _M0FPB23array__growth__capacity(_M0L8old__capS505, _M0L3lenS2565, _M0L8requiredS508);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS506, _M0L8new__capS507);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS510,
  int32_t _M0L8requiredS512
) {
  int32_t _M0L8old__capS509;
  int32_t _M0L3lenS2566;
  int32_t _M0L8new__capS511;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS509 = _M0MPC15array5Array8capacityGfE(_M0L4selfS510);
  _M0L3lenS2566 = _M0L4selfS510->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS511
  = _M0FPB23array__growth__capacity(_M0L8old__capS509, _M0L3lenS2566, _M0L8requiredS512);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS510, _M0L8new__capS511);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS474,
  int32_t _M0L13new__capacityS477
) {
  int32_t* _M0L8old__bufS473;
  int32_t _M0L3lenS475;
  int32_t _M0L9copy__lenS476;
  int32_t* _M0L8new__bufS478;
  int32_t* _M0L6_2aoldS5692;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS473 = _M0L4selfS474->$0;
  _M0L3lenS475 = _M0L4selfS474->$1;
  if (_M0L3lenS475 < _M0L13new__capacityS477) {
    _M0L9copy__lenS476 = _M0L3lenS475;
  } else {
    _M0L9copy__lenS476 = _M0L13new__capacityS477;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS473);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS478
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS473, _M0L13new__capacityS477, _M0L9copy__lenS476, 0, 0);
  _M0L6_2aoldS5692 = _M0L4selfS474->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5692);
  _M0L4selfS474->$0 = _M0L8new__bufS478;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS480,
  int32_t _M0L13new__capacityS483
) {
  moonbit_string_t* _M0L8old__bufS479;
  int32_t _M0L3lenS481;
  int32_t _M0L9copy__lenS482;
  moonbit_string_t* _M0L8new__bufS484;
  moonbit_string_t* _M0L6_2aoldS5693;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS479 = _M0L4selfS480->$0;
  _M0L3lenS481 = _M0L4selfS480->$1;
  if (_M0L3lenS481 < _M0L13new__capacityS483) {
    _M0L9copy__lenS482 = _M0L3lenS481;
  } else {
    _M0L9copy__lenS482 = _M0L13new__capacityS483;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS479);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS484
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS479, _M0L13new__capacityS483, _M0L9copy__lenS482, 0, 0);
  _M0L6_2aoldS5693 = _M0L4selfS480->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5693);
  _M0L4selfS480->$0 = _M0L8new__bufS484;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS486,
  int32_t _M0L13new__capacityS489
) {
  struct _M0TUsiE** _M0L8old__bufS485;
  int32_t _M0L3lenS487;
  int32_t _M0L9copy__lenS488;
  struct _M0TUsiE** _M0L8new__bufS490;
  struct _M0TUsiE** _M0L6_2aoldS5694;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS485 = _M0L4selfS486->$0;
  _M0L3lenS487 = _M0L4selfS486->$1;
  if (_M0L3lenS487 < _M0L13new__capacityS489) {
    _M0L9copy__lenS488 = _M0L3lenS487;
  } else {
    _M0L9copy__lenS488 = _M0L13new__capacityS489;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS485);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS490
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS485, _M0L13new__capacityS489, _M0L9copy__lenS488, 0, 0);
  _M0L6_2aoldS5694 = _M0L4selfS486->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5694);
  _M0L4selfS486->$0 = _M0L8new__bufS490;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS492,
  int32_t _M0L13new__capacityS495
) {
  float* _M0L8old__bufS491;
  int32_t _M0L3lenS493;
  int32_t _M0L9copy__lenS494;
  float* _M0L8new__bufS496;
  float* _M0L6_2aoldS5695;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS491 = _M0L4selfS492->$0;
  _M0L3lenS493 = _M0L4selfS492->$1;
  if (_M0L3lenS493 < _M0L13new__capacityS495) {
    _M0L9copy__lenS494 = _M0L3lenS493;
  } else {
    _M0L9copy__lenS494 = _M0L13new__capacityS495;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS491);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS496
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS491, _M0L13new__capacityS495, _M0L9copy__lenS494, 0, 0);
  _M0L6_2aoldS5695 = _M0L4selfS492->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5695);
  _M0L4selfS492->$0 = _M0L8new__bufS496;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS469
) {
  int32_t* _M0L6_2atmpS2559;
  int32_t _result_6141;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2559 = _M0MPC15array5Array6bufferGiE(_M0L4selfS469);
  _result_6141 = Moonbit_array_length(_M0L6_2atmpS2559);
  moonbit_decref_cycle_free(_M0L6_2atmpS2559);
  return _result_6141;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS470
) {
  moonbit_string_t* _M0L6_2atmpS2560;
  int32_t _result_6142;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2560 = _M0MPC15array5Array6bufferGsE(_M0L4selfS470);
  _result_6142 = Moonbit_array_length(_M0L6_2atmpS2560);
  moonbit_decref_cycle_free(_M0L6_2atmpS2560);
  return _result_6142;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS471
) {
  struct _M0TUsiE** _M0L6_2atmpS2561;
  int32_t _result_6143;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2561 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS471);
  _result_6143 = Moonbit_array_length(_M0L6_2atmpS2561);
  moonbit_decref_cycle_free(_M0L6_2atmpS2561);
  return _result_6143;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS472
) {
  float* _M0L6_2atmpS2562;
  int32_t _result_6144;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2562 = _M0MPC15array5Array6bufferGfE(_M0L4selfS472);
  _result_6144 = Moonbit_array_length(_M0L6_2atmpS2562);
  moonbit_decref_cycle_free(_M0L6_2atmpS2562);
  return _result_6144;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS465,
  int32_t _M0L3lenS463,
  int32_t _M0L8requiredS462
) {
  int32_t _M0L5startS464;
  int32_t _M0L5spaceS466;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS462 < _M0L3lenS463) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L7currentS465 == 0) {
    _M0L5startS464 = 8;
  } else {
    _M0L5startS464 = _M0L7currentS465;
  }
  _M0L5spaceS466 = _M0L5startS464;
  while (1) {
    if (_M0L5spaceS466 < _M0L8requiredS462) {
      int32_t _M0L4nextS467 = _M0L5spaceS466 * 2;
      if (_M0L4nextS467 <= _M0L5spaceS466) {
        return _M0L8requiredS462;
      }
      _M0L5spaceS466 = _M0L4nextS467;
      continue;
    } else {
      return _M0L5spaceS466;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS459) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS459->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS460) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS460->$1;
}

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS461) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS461->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS5696;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5696 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5696);
  return _M0L8_2afieldS5696;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS453) {
  int32_t* _M0L8_2afieldS5697;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5697 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5697);
  return _M0L8_2afieldS5697;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS454
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L8_2afieldS5698;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5698 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5698);
  return _M0L8_2afieldS5698;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS5699;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5699 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5699);
  return _M0L8_2afieldS5699;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS5700;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5700 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5700);
  return _M0L8_2afieldS5700;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS457) {
  uint8_t* _M0L8_2afieldS5701;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5701 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5701);
  return _M0L8_2afieldS5701;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS458
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS5702;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5702 = _M0L4selfS458->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5702);
  return _M0L8_2afieldS5702;
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
  int32_t _M0L3endS2557;
  int32_t _M0L5startS2558;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS2556;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS2549;
  int32_t _M0L6_2atmpS2548;
  int32_t _if__result_6146;
  uint16_t* _M0L4dataS2550;
  int32_t _M0L3lenS2551;
  moonbit_string_t _M0L6_2atmpS2552;
  int32_t _M0L6_2atmpS2553;
  int32_t _M0L3lenS2555;
  int32_t _M0L6_2atmpS2554;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS2557 = _M0L3strS448.$2;
  _M0L5startS2558 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS2557 - _M0L5startS2558;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS2556 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS2556 + _M0L8str__lenS447;
  _M0L4dataS2549 = _M0L4selfS450->$0;
  _M0L6_2atmpS2548 = Moonbit_array_length(_M0L4dataS2549);
  if (_M0L8requiredS449 > _M0L6_2atmpS2548) {
    _if__result_6146 = 1;
  } else {
    int32_t _M0L3lenS2547 = _M0L4selfS450->$1;
    _if__result_6146 = _M0L8requiredS449 < _M0L3lenS2547;
  }
  if (_if__result_6146) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS2550 = _M0L4selfS450->$0;
  _M0L3lenS2551 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS2550);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2552 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2553 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2550, _M0L3lenS2551, _M0L6_2atmpS2552, _M0L6_2atmpS2553, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS2550);
  moonbit_decref_cycle_free(_M0L6_2atmpS2552);
  _M0L3lenS2555 = _M0L4selfS450->$1;
  _M0L6_2atmpS2554 = _M0L3lenS2555 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS2554;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_6147;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS2546;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS2545;
  moonbit_string_t _result_6148;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS2544 = Moonbit_array_length(_M0L3strS444);
    _if__result_6147 = _M0L3endS443 == _M0L6_2atmpS2544;
  } else {
    _if__result_6147 = 0;
  }
  if (_if__result_6147) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS2546 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS2546, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS2545 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_6148
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS2545, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS2545);
  return _result_6148;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_6149;
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
      int32_t _M0L6_2atmpS2543 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_6149 = _M0L6_2atmpS2543 <= _M0L3lenS436;
    } else {
      _if__result_6149 = 0;
    }
  } else {
    _if__result_6149 = 0;
  }
  if (_if__result_6149) {
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
  int32_t _M0L6_2atmpS2542;
  int32_t _M0L6_2atmpS2541;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS2540;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS2542 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS2541 = _M0L13bytes__offsetS423 + _M0L6_2atmpS2542;
  _M0L2e1S422 = _M0L6_2atmpS2541 - 1;
  _M0L6_2atmpS2540 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS2540 - 1;
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
        int32_t _M0L6_2atmpS2537 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS2536 = (int32_t)_M0L6_2atmpS2537;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS2536;
        uint32_t _M0L6_2atmpS2532 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS2531;
        int32_t _M0L6_2atmpS2533;
        uint32_t _M0L6_2atmpS2535;
        int32_t _M0L6_2atmpS2534;
        int32_t _M0L6_2atmpS2538;
        int32_t _M0L6_2atmpS2539;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2531 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2532);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS2531;
        _M0L6_2atmpS2533 = _M0L1jS433 + 1;
        _M0L6_2atmpS2535 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2534 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2535);
        if (
          _M0L6_2atmpS2533 < 0
          || _M0L6_2atmpS2533 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS2533] = _M0L6_2atmpS2534;
        _M0L6_2atmpS2538 = _M0L1iS432 + 1;
        _M0L6_2atmpS2539 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS2538;
        _M0L1jS433 = _M0L6_2atmpS2539;
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
  int32_t _M0L6_2atmpS2530;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2530 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS2530 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS2529 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS2529;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS2526;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2526 = 1;
      } else {
        _M0L6_2atmpS2526 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS2526;
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
      int32_t _M0L6_2atmpS2527;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2527 = 1;
      } else {
        _M0L6_2atmpS2527 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS2527;
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
      int32_t _M0L6_2atmpS2528;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2528 = 1;
      } else {
        _M0L6_2atmpS2528 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS2528;
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
  int32_t _M0L6_2atmpS2525;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2525 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS2525;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS2502 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS2502;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS2501 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS2500 = 48 + _M0L6_2atmpS2501;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS2500;
      int32_t _M0L6_2atmpS2499 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS2498 = 48 + _M0L6_2atmpS2499;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS2498;
      int32_t _M0L6_2atmpS2497 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS2496 = 48 + _M0L6_2atmpS2497;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS2496;
      int32_t _M0L6_2atmpS2495 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS2494 = 48 + _M0L6_2atmpS2495;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS2494;
      int32_t _M0L6_2atmpS2486 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS2485 = _M0L6_2atmpS2486 - 4;
      int32_t _M0L6_2atmpS2488;
      int32_t _M0L6_2atmpS2487;
      int32_t _M0L6_2atmpS2490;
      int32_t _M0L6_2atmpS2489;
      int32_t _M0L6_2atmpS2492;
      int32_t _M0L6_2atmpS2491;
      int32_t _M0L6_2atmpS2493;
      _M0L6bufferS381[_M0L6_2atmpS2485] = _M0L6d1__hiS377;
      _M0L6_2atmpS2488 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2487 = _M0L6_2atmpS2488 - 3;
      _M0L6bufferS381[_M0L6_2atmpS2487] = _M0L6d1__loS378;
      _M0L6_2atmpS2490 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2489 = _M0L6_2atmpS2490 - 2;
      _M0L6bufferS381[_M0L6_2atmpS2489] = _M0L6d2__hiS379;
      _M0L6_2atmpS2492 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2491 = _M0L6_2atmpS2492 - 1;
      _M0L6bufferS381[_M0L6_2atmpS2491] = _M0L6d2__loS380;
      _M0L6_2atmpS2493 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS2493;
      continue;
    } else {
      int32_t _M0L6_2atmpS2524 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS2524;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS2511 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS2510 = 48 + _M0L6_2atmpS2511;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS2510;
          int32_t _M0L6_2atmpS2509 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS2508 = 48 + _M0L6_2atmpS2509;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS2508;
          int32_t _M0L6_2atmpS2504 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2503 = _M0L6_2atmpS2504 - 2;
          int32_t _M0L6_2atmpS2506;
          int32_t _M0L6_2atmpS2505;
          int32_t _M0L6_2atmpS2507;
          _M0L6bufferS381[_M0L6_2atmpS2503] = _M0L5d__hiS388;
          _M0L6_2atmpS2506 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2505 = _M0L6_2atmpS2506 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2505] = _M0L5d__loS389;
          _M0L6_2atmpS2507 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS2507;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS2519 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS2518 = 48 + _M0L6_2atmpS2519;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS2518;
          int32_t _M0L6_2atmpS2517 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS2516 = 48 + _M0L6_2atmpS2517;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS2516;
          int32_t _M0L6_2atmpS2513 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2512 = _M0L6_2atmpS2513 - 2;
          int32_t _M0L6_2atmpS2515;
          int32_t _M0L6_2atmpS2514;
          _M0L6bufferS381[_M0L6_2atmpS2512] = _M0L5d__hiS391;
          _M0L6_2atmpS2515 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2514 = _M0L6_2atmpS2515 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2514] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS2523 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2520 = _M0L6_2atmpS2523 - 1;
          int32_t _M0L6_2atmpS2522 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS2521 = (uint16_t)_M0L6_2atmpS2522;
          _M0L6bufferS381[_M0L6_2atmpS2520] = _M0L6_2atmpS2521;
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
  int32_t _M0L6_2atmpS2470;
  int32_t _M0L6_2atmpS2469;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS2470 = _M0L5radixS355 - 1;
  _M0L6_2atmpS2469 = _M0L5radixS355 & _M0L6_2atmpS2470;
  if (_M0L6_2atmpS2469 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS2477;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS2477 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS2477;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS2476 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS2476;
        int32_t _M0L6_2atmpS2473 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS2471 = _M0L6_2atmpS2473 - 1;
        int32_t _M0L6_2atmpS2472 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS2474;
        uint64_t _M0L6_2atmpS2475;
        _M0L6bufferS361[_M0L6_2atmpS2471] = _M0L6_2atmpS2472;
        _M0L6_2atmpS2474 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS2475 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS2474;
        _M0L1nS359 = _M0L6_2atmpS2475;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2484 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS2484;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS2483 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS2482 = _M0L1nS367 - _M0L6_2atmpS2483;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS2482;
        int32_t _M0L6_2atmpS2480 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS2478 = _M0L6_2atmpS2480 - 1;
        int32_t _M0L6_2atmpS2479 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS2481;
        _M0L6bufferS361[_M0L6_2atmpS2478] = _M0L6_2atmpS2479;
        _M0L6_2atmpS2481 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS2481;
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
  int32_t _M0L6_2atmpS2468;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2468 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS2468;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS2465 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS2465;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS2459 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS2457 = _M0L6_2atmpS2459 - 2;
      int32_t _M0L6_2atmpS2458 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS2462;
      int32_t _M0L6_2atmpS2460;
      int32_t _M0L6_2atmpS2461;
      int32_t _M0L6_2atmpS2463;
      uint64_t _M0L6_2atmpS2464;
      _M0L6bufferS348[_M0L6_2atmpS2457] = _M0L6_2atmpS2458;
      _M0L6_2atmpS2462 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS2460 = _M0L6_2atmpS2462 - 1;
      _M0L6_2atmpS2461
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS2460] = _M0L6_2atmpS2461;
      _M0L6_2atmpS2463 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS2464 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS2463;
      _M0L1nS344 = _M0L6_2atmpS2464;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS2467 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS2467;
      int32_t _M0L6_2atmpS2466 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS2466;
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
      uint64_t _M0L6_2atmpS2455 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS2456 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS2455;
      _M0L5countS341 = _M0L6_2atmpS2456;
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
    int32_t _M0L6_2atmpS2454;
    int32_t _M0L6_2atmpS2453;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS2454 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS2453 = _M0L6_2atmpS2454 / 4;
    return _M0L6_2atmpS2453 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS2452 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS2452;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS2449;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2449 = 1;
      } else {
        _M0L6_2atmpS2449 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS2449;
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
      int32_t _M0L6_2atmpS2450;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2450 = 1;
      } else {
        _M0L6_2atmpS2450 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS2450;
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
      int32_t _M0L6_2atmpS2451;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2451 = 1;
      } else {
        _M0L6_2atmpS2451 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS2451;
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
      uint32_t _M0L6_2atmpS2447 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS2448 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS2447;
      _M0L5countS315 = _M0L6_2atmpS2448;
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
    int32_t _M0L6_2atmpS2446;
    int32_t _M0L6_2atmpS2445;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS2446 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS2445 = _M0L6_2atmpS2446 / 4;
    return _M0L6_2atmpS2445 + 1;
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
  int32_t _M0L6_2atmpS2444;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2444 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS2444;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS2421 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS2421;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS2420 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS2419 = 48 + _M0L6_2atmpS2420;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS2419;
      int32_t _M0L6_2atmpS2418 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS2417 = 48 + _M0L6_2atmpS2418;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS2417;
      int32_t _M0L6_2atmpS2416 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS2415 = 48 + _M0L6_2atmpS2416;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS2415;
      int32_t _M0L6_2atmpS2414 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS2413 = 48 + _M0L6_2atmpS2414;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS2413;
      int32_t _M0L6_2atmpS2405 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS2404 = _M0L6_2atmpS2405 - 4;
      int32_t _M0L6_2atmpS2407;
      int32_t _M0L6_2atmpS2406;
      int32_t _M0L6_2atmpS2409;
      int32_t _M0L6_2atmpS2408;
      int32_t _M0L6_2atmpS2411;
      int32_t _M0L6_2atmpS2410;
      int32_t _M0L6_2atmpS2412;
      _M0L6bufferS294[_M0L6_2atmpS2404] = _M0L6d1__hiS290;
      _M0L6_2atmpS2407 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2406 = _M0L6_2atmpS2407 - 3;
      _M0L6bufferS294[_M0L6_2atmpS2406] = _M0L6d1__loS291;
      _M0L6_2atmpS2409 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2408 = _M0L6_2atmpS2409 - 2;
      _M0L6bufferS294[_M0L6_2atmpS2408] = _M0L6d2__hiS292;
      _M0L6_2atmpS2411 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2410 = _M0L6_2atmpS2411 - 1;
      _M0L6bufferS294[_M0L6_2atmpS2410] = _M0L6d2__loS293;
      _M0L6_2atmpS2412 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS2412;
      continue;
    } else {
      int32_t _M0L6_2atmpS2443 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS2443;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS2430 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS2429 = 48 + _M0L6_2atmpS2430;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS2429;
          int32_t _M0L6_2atmpS2428 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS2427 = 48 + _M0L6_2atmpS2428;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS2427;
          int32_t _M0L6_2atmpS2423 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2422 = _M0L6_2atmpS2423 - 2;
          int32_t _M0L6_2atmpS2425;
          int32_t _M0L6_2atmpS2424;
          int32_t _M0L6_2atmpS2426;
          _M0L6bufferS294[_M0L6_2atmpS2422] = _M0L5d__hiS301;
          _M0L6_2atmpS2425 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2424 = _M0L6_2atmpS2425 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2424] = _M0L5d__loS302;
          _M0L6_2atmpS2426 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS2426;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS2438 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS2437 = 48 + _M0L6_2atmpS2438;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS2437;
          int32_t _M0L6_2atmpS2436 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS2435 = 48 + _M0L6_2atmpS2436;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS2435;
          int32_t _M0L6_2atmpS2432 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2431 = _M0L6_2atmpS2432 - 2;
          int32_t _M0L6_2atmpS2434;
          int32_t _M0L6_2atmpS2433;
          _M0L6bufferS294[_M0L6_2atmpS2431] = _M0L5d__hiS304;
          _M0L6_2atmpS2434 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2433 = _M0L6_2atmpS2434 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2433] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS2442 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2439 = _M0L6_2atmpS2442 - 1;
          int32_t _M0L6_2atmpS2441 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS2440 = (uint16_t)_M0L6_2atmpS2441;
          _M0L6bufferS294[_M0L6_2atmpS2439] = _M0L6_2atmpS2440;
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
  int32_t _M0L6_2atmpS2389;
  int32_t _M0L6_2atmpS2388;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS2389 = _M0L5radixS268 - 1;
  _M0L6_2atmpS2388 = _M0L5radixS268 & _M0L6_2atmpS2389;
  if (_M0L6_2atmpS2388 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS2396;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS2396 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS2396;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS2395 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS2395;
        int32_t _M0L6_2atmpS2392 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS2390 = _M0L6_2atmpS2392 - 1;
        int32_t _M0L6_2atmpS2391 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS2393;
        uint32_t _M0L6_2atmpS2394;
        _M0L6bufferS274[_M0L6_2atmpS2390] = _M0L6_2atmpS2391;
        _M0L6_2atmpS2393 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS2394 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS2393;
        _M0L1nS272 = _M0L6_2atmpS2394;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2403 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS2403;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS2402 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS2401 = _M0L1nS280 - _M0L6_2atmpS2402;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS2401;
        int32_t _M0L6_2atmpS2399 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS2397 = _M0L6_2atmpS2399 - 1;
        int32_t _M0L6_2atmpS2398 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS2400;
        _M0L6bufferS274[_M0L6_2atmpS2397] = _M0L6_2atmpS2398;
        _M0L6_2atmpS2400 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS2400;
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
  int32_t _M0L6_2atmpS2387;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2387 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS2387;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS2384 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS2384;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS2378 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS2376 = _M0L6_2atmpS2378 - 2;
      int32_t _M0L6_2atmpS2377 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS2381;
      int32_t _M0L6_2atmpS2379;
      int32_t _M0L6_2atmpS2380;
      int32_t _M0L6_2atmpS2382;
      uint32_t _M0L6_2atmpS2383;
      _M0L6bufferS261[_M0L6_2atmpS2376] = _M0L6_2atmpS2377;
      _M0L6_2atmpS2381 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS2379 = _M0L6_2atmpS2381 - 1;
      _M0L6_2atmpS2380
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS2379] = _M0L6_2atmpS2380;
      _M0L6_2atmpS2382 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS2383 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS2382;
      _M0L1nS257 = _M0L6_2atmpS2383;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS2386 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS2386;
      int32_t _M0L6_2atmpS2385 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS2385;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS2375;
  moonbit_string_t _result_6163;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS2375
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS2375);
  if (_M0L6_2atmpS2375.$1) {
    moonbit_decref(_M0L6_2atmpS2375.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_6163 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_6163;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS2372;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2372 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS2372);
  moonbit_decref_cycle_free(_M0L6_2atmpS2372);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS2373;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2373 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS2373);
  moonbit_decref_cycle_free(_M0L6_2atmpS2373);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS2374;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2374 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS2374);
  moonbit_decref_cycle_free(_M0L6_2atmpS2374);
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
  moonbit_string_t _M0L8_2afieldS5703;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS5703 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5703);
  return _M0L8_2afieldS5703;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS2371;
  int64_t _M0L6_2atmpS2370;
  struct _M0TPC16string10StringView _M0L6_2atmpS2369;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2371 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS2370 = (int64_t)_M0L6_2atmpS2371;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2369
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS2370);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS2369);
  moonbit_decref_cycle_free(_M0L6_2atmpS2369.$0);
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
  int32_t _M0L6_2atmpS2353;
  int32_t _if__result_6164;
  int32_t _M0L6_2atmpS2361;
  int32_t _if__result_6165;
  int32_t _M0L6_2atmpS2363;
  int32_t _M0L6_2atmpS2364;
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
  _M0L6_2atmpS2353 = _M0Lm2loS236;
  if (_M0L6_2atmpS2353 > 0) {
    int32_t _M0L6_2atmpS2352 = _M0Lm2loS236;
    if (_M0L6_2atmpS2352 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2351 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS2350 = _M0L4selfS235[_M0L6_2atmpS2351];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2350)) {
        int32_t _M0L6_2atmpS2349 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS2348 = _M0L6_2atmpS2349 - 1;
        int32_t _M0L6_2atmpS2347 = _M0L4selfS235[_M0L6_2atmpS2348];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6164
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2347);
      } else {
        _if__result_6164 = 0;
      }
    } else {
      _if__result_6164 = 0;
    }
  } else {
    _if__result_6164 = 0;
  }
  if (_if__result_6164) {
    int32_t _M0L6_2atmpS2354 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS2354 + 1;
  }
  _M0L6_2atmpS2361 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2361 > 0) {
    int32_t _M0L6_2atmpS2360 = _M0Lm2hiS238;
    if (_M0L6_2atmpS2360 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2359 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS2358 = _M0L4selfS235[_M0L6_2atmpS2359];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2358)) {
        int32_t _M0L6_2atmpS2357 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS2356 = _M0L6_2atmpS2357 - 1;
        int32_t _M0L6_2atmpS2355 = _M0L4selfS235[_M0L6_2atmpS2356];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6165
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2355);
      } else {
        _if__result_6165 = 0;
      }
    } else {
      _if__result_6165 = 0;
    }
  } else {
    _if__result_6165 = 0;
  }
  if (_if__result_6165) {
    int32_t _M0L6_2atmpS2362 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS2362 - 1;
  }
  _M0L6_2atmpS2363 = _M0Lm2loS236;
  _M0L6_2atmpS2364 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2363 >= _M0L6_2atmpS2364) {
    int32_t _M0L6_2atmpS2365 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2366 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2365,
                                                 .$2 = _M0L6_2atmpS2366};
  } else {
    int32_t _M0L6_2atmpS2367 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2368 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2367,
                                                 .$2 = _M0L6_2atmpS2368};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS2346;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS2346
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS2346);
  if (_M0L6_2atmpS2346.$1) {
    moonbit_decref(_M0L6_2atmpS2346.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS2345;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS2345
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS2345);
  if (_M0L6_2atmpS2345.$1) {
    moonbit_decref(_M0L6_2atmpS2345.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS2344;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2344 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS2344;
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
  int32_t _M0L6_2atmpS2343;
  struct _M0TPC16string10StringView _M0L6_2atmpS2341;
  struct _M0TPB6Logger _M0L6_2atmpS2342;
  moonbit_string_t _result_6166;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS2343 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS2341
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS2343
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS2342
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS2341, _M0L6_2atmpS2342, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS2341.$0);
  if (_M0L6_2atmpS2342.$1) {
    moonbit_decref(_M0L6_2atmpS2342.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6166 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_6166;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS2339;
  int32_t _M0L5startS2340;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS2339 = _M0L4selfS218.$2;
  _M0L5startS2340 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS2339 - _M0L5startS2340;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 112, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS2336;
    int32_t _M0L5startS2338;
    int32_t _M0L6_2atmpS2337;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS2320;
    int32_t _M0L6_2atmpS2321;
    int32_t _M0L6_2atmpS2322;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS2336 = _M0L4selfS218.$0;
    _M0L5startS2338 = _M0L4selfS218.$1;
    _M0L6_2atmpS2337 = _M0L5startS2338 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS2336[_M0L6_2atmpS2337];
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
        int32_t _M0L6_2atmpS2323;
        int32_t _M0L6_2atmpS2324;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS2323 = _M0L1iS220 + 1;
        _M0L6_2atmpS2324 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2323;
        _M0L3segS221 = _M0L6_2atmpS2324;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS2325;
        int32_t _M0L6_2atmpS2326;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS2325 = _M0L1iS220 + 1;
        _M0L6_2atmpS2326 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2325;
        _M0L3segS221 = _M0L6_2atmpS2326;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS2327;
        int32_t _M0L6_2atmpS2328;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS2327 = _M0L1iS220 + 1;
        _M0L6_2atmpS2328 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2327;
        _M0L3segS221 = _M0L6_2atmpS2328;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS2329;
        int32_t _M0L6_2atmpS2330;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS2329 = _M0L1iS220 + 1;
        _M0L6_2atmpS2330 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2329;
        _M0L3segS221 = _M0L6_2atmpS2330;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS2332;
          moonbit_string_t _M0L6_2atmpS2331;
          int32_t _M0L6_2atmpS2333;
          int32_t _M0L6_2atmpS2334;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
          _M0L6_2atmpS2332 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS2331 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS2332);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS2331);
          moonbit_decref_cycle_free(_M0L6_2atmpS2331);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS2333 = _M0L1iS220 + 1;
          _M0L6_2atmpS2334 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS2333;
          _M0L3segS221 = _M0L6_2atmpS2334;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS2335 = _M0L1iS220 + 1;
          int32_t _tmp_6169 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS2335;
          _M0L3segS221 = _tmp_6169;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_6168;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2320 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS2320);
    _M0L6_2atmpS2321 = _M0L1iS220 + 1;
    _M0L6_2atmpS2322 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS2321;
    _M0L3segS221 = _M0L6_2atmpS2322;
    continue;
    joinlet_6168:;
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
    int64_t _M0L6_2atmpS2319 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS2318;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2318
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS2319);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS2318);
    moonbit_decref_cycle_free(_M0L6_2atmpS2318.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS2316;
  int32_t _M0L5startS2317;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS2294;
  int32_t _if__result_6170;
  int32_t _M0L6_2atmpS2304;
  int32_t _if__result_6171;
  int32_t _M0L6_2atmpS2306;
  int32_t _M0L6_2atmpS2307;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS2316 = _M0L4selfS201.$2;
  _M0L5startS2317 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS2316 - _M0L5startS2317;
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
  _M0L6_2atmpS2294 = _M0Lm2loS202;
  if (_M0L6_2atmpS2294 > 0) {
    int32_t _M0L6_2atmpS2293 = _M0Lm2loS202;
    if (_M0L6_2atmpS2293 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2292 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS2291 = _M0L4baseS209 + _M0L6_2atmpS2292;
      int32_t _M0L6_2atmpS2290 = _M0L3strS208[_M0L6_2atmpS2291];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2290)) {
        int32_t _M0L6_2atmpS2289 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS2288 = _M0L4baseS209 + _M0L6_2atmpS2289;
        int32_t _M0L6_2atmpS2287 = _M0L6_2atmpS2288 - 1;
        int32_t _M0L6_2atmpS2286 = _M0L3strS208[_M0L6_2atmpS2287];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6170
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2286);
      } else {
        _if__result_6170 = 0;
      }
    } else {
      _if__result_6170 = 0;
    }
  } else {
    _if__result_6170 = 0;
  }
  if (_if__result_6170) {
    int32_t _M0L6_2atmpS2295 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS2295 + 1;
  }
  _M0L6_2atmpS2304 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2304 > 0) {
    int32_t _M0L6_2atmpS2303 = _M0Lm2hiS204;
    if (_M0L6_2atmpS2303 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2302 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS2301 = _M0L4baseS209 + _M0L6_2atmpS2302;
      int32_t _M0L6_2atmpS2300 = _M0L3strS208[_M0L6_2atmpS2301];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2300)) {
        int32_t _M0L6_2atmpS2299 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS2298 = _M0L4baseS209 + _M0L6_2atmpS2299;
        int32_t _M0L6_2atmpS2297 = _M0L6_2atmpS2298 - 1;
        int32_t _M0L6_2atmpS2296 = _M0L3strS208[_M0L6_2atmpS2297];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6171
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2296);
      } else {
        _if__result_6171 = 0;
      }
    } else {
      _if__result_6171 = 0;
    }
  } else {
    _if__result_6171 = 0;
  }
  if (_if__result_6171) {
    int32_t _M0L6_2atmpS2305 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS2305 - 1;
  }
  _M0L6_2atmpS2306 = _M0Lm2loS202;
  _M0L6_2atmpS2307 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2306 >= _M0L6_2atmpS2307) {
    int32_t _M0L6_2atmpS2311 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2308 = _M0L4baseS209 + _M0L6_2atmpS2311;
    int32_t _M0L6_2atmpS2310 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2309 = _M0L4baseS209 + _M0L6_2atmpS2310;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2308,
                                                 .$2 = _M0L6_2atmpS2309};
  } else {
    int32_t _M0L6_2atmpS2315 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2312 = _M0L4baseS209 + _M0L6_2atmpS2315;
    int32_t _M0L6_2atmpS2314 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS2313 = _M0L4baseS209 + _M0L6_2atmpS2314;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2312,
                                                 .$2 = _M0L6_2atmpS2313};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS2283;
  int32_t _M0L6_2atmpS2282;
  int32_t _M0L6_2atmpS2285;
  int32_t _M0L6_2atmpS2284;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS2281;
  moonbit_string_t _result_6172;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2283 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2282
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2283);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2282);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2285 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2284
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2285);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2284);
  _M0L6_2atmpS2281 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6172 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS2281);
  moonbit_decref_cycle_free(_M0L6_2atmpS2281);
  return _result_6172;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS2278;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2278 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2278);
  } else {
    int32_t _M0L6_2atmpS2280;
    int32_t _M0L6_2atmpS2279;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2280 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2279 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS2280, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2279);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS2276;
  int32_t _M0L6_2atmpS2277;
  int32_t _M0L6_2atmpS2275;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2276 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS2277 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS2275 = _M0L6_2atmpS2276 - _M0L6_2atmpS2277;
  return _M0L6_2atmpS2275 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS2273;
  int32_t _M0L6_2atmpS2274;
  int32_t _M0L6_2atmpS2272;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2273 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS2274 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS2272 = _M0L6_2atmpS2273 % _M0L6_2atmpS2274;
  return _M0L6_2atmpS2272 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS2270;
  int32_t _M0L6_2atmpS2271;
  int32_t _M0L6_2atmpS2269;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2270 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS2271 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS2269 = _M0L6_2atmpS2270 / _M0L6_2atmpS2271;
  return _M0L6_2atmpS2269 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS2267;
  int32_t _M0L6_2atmpS2268;
  int32_t _M0L6_2atmpS2266;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2267 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS2268 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS2266 = _M0L6_2atmpS2267 + _M0L6_2atmpS2268;
  return _M0L6_2atmpS2266 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS2265;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS2265 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS2265;
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
  int32_t _M0L3lenS2264;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS2259;
  int32_t _M0L6_2atmpS2258;
  int32_t _if__result_6173;
  uint16_t* _M0L4dataS2260;
  int32_t _M0L3lenS2261;
  int32_t _M0L3lenS2263;
  int32_t _M0L6_2atmpS2262;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS2264 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS2264 + _M0L8str__lenS182;
  _M0L4dataS2259 = _M0L4selfS185->$0;
  _M0L6_2atmpS2258 = Moonbit_array_length(_M0L4dataS2259);
  if (_M0L8requiredS184 > _M0L6_2atmpS2258) {
    _if__result_6173 = 1;
  } else {
    int32_t _M0L3lenS2257 = _M0L4selfS185->$1;
    _if__result_6173 = _M0L8requiredS184 < _M0L3lenS2257;
  }
  if (_if__result_6173) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS2260 = _M0L4selfS185->$0;
  _M0L3lenS2261 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS2260);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2260, _M0L3lenS2261, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS2260);
  _M0L3lenS2263 = _M0L4selfS185->$1;
  _M0L6_2atmpS2262 = _M0L3lenS2263 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS2262;
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
      int32_t _M0L6_2atmpS2254 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS2255;
      int32_t _M0L6_2atmpS2256;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS2254;
      _M0L6_2atmpS2255 = _M0L1iS176 + 1;
      _M0L6_2atmpS2256 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS2255;
      _M0L1jS177 = _M0L6_2atmpS2256;
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
    int32_t _M0L3lenS2225 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS2227 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2226 = Moonbit_array_length(_M0L4dataS2227);
    uint16_t* _M0L4dataS2230;
    int32_t _M0L3lenS2231;
    int32_t _M0L6_2atmpS2232;
    int32_t _M0L3lenS2234;
    int32_t _M0L6_2atmpS2233;
    if (_M0L3lenS2225 >= _M0L6_2atmpS2226) {
      int32_t _M0L3lenS2229 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2228 = _M0L3lenS2229 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2228);
    }
    _M0L4dataS2230 = _M0L4selfS171->$0;
    _M0L3lenS2231 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS2230);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2232 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS2231 < 0
      || _M0L3lenS2231 >= Moonbit_array_length(_M0L4dataS2230)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2230[_M0L3lenS2231] = _M0L6_2atmpS2232;
    moonbit_decref_cycle_free(_M0L4dataS2230);
    _M0L3lenS2234 = _M0L4selfS171->$1;
    _M0L6_2atmpS2233 = _M0L3lenS2234 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS2233;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS2238 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2236 = Moonbit_array_length(_M0L4dataS2238);
    int32_t _M0L3lenS2237 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS2235 = _M0L6_2atmpS2236 - _M0L3lenS2237;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS2241;
    int32_t _M0L3lenS2242;
    uint32_t _M0L6_2atmpS2245;
    uint32_t _M0L6_2atmpS2244;
    int32_t _M0L6_2atmpS2243;
    uint16_t* _M0L4dataS2246;
    int32_t _M0L3lenS2251;
    int32_t _M0L6_2atmpS2247;
    uint32_t _M0L6_2atmpS2250;
    uint32_t _M0L6_2atmpS2249;
    int32_t _M0L6_2atmpS2248;
    int32_t _M0L3lenS2253;
    int32_t _M0L6_2atmpS2252;
    if (_M0L6_2atmpS2235 < 2) {
      int32_t _M0L3lenS2240 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2239 = _M0L3lenS2240 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2239);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS2241 = _M0L4selfS171->$0;
    _M0L3lenS2242 = _M0L4selfS171->$1;
    _M0L6_2atmpS2245 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS2244 = 55296u + _M0L6_2atmpS2245;
    moonbit_incref_cycle_free(_M0L4dataS2241);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2243 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2244);
    if (
      _M0L3lenS2242 < 0
      || _M0L3lenS2242 >= Moonbit_array_length(_M0L4dataS2241)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2241[_M0L3lenS2242] = _M0L6_2atmpS2243;
    moonbit_decref_cycle_free(_M0L4dataS2241);
    _M0L4dataS2246 = _M0L4selfS171->$0;
    _M0L3lenS2251 = _M0L4selfS171->$1;
    _M0L6_2atmpS2247 = _M0L3lenS2251 + 1;
    _M0L6_2atmpS2250 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS2249 = 56320u + _M0L6_2atmpS2250;
    moonbit_incref_cycle_free(_M0L4dataS2246);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2248 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2249);
    if (
      _M0L6_2atmpS2247 < 0
      || _M0L6_2atmpS2247 >= Moonbit_array_length(_M0L4dataS2246)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2246[_M0L6_2atmpS2247] = _M0L6_2atmpS2248;
    moonbit_decref_cycle_free(_M0L4dataS2246);
    _M0L3lenS2253 = _M0L4selfS171->$1;
    _M0L6_2atmpS2252 = _M0L3lenS2253 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS2252;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS2224;
  int32_t _M0L6_2atmpS2222;
  int32_t _M0L3lenS2223;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS2219;
  int32_t _M0L6_2atmpS2220;
  int32_t _M0L3lenS2221;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS5704;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS2224 = _M0L4selfS166->$0;
  _M0L6_2atmpS2222 = Moonbit_array_length(_M0L4dataS2224);
  _M0L3lenS2223 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS2222, _M0L3lenS2223, _M0L8requiredS167);
  _M0L4dataS2219 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS2219);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2220 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS2221 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2219, _M0L13new__capacityS165, _M0L6_2atmpS2220, _M0L3lenS2221, 0, 0);
  _M0L6_2aoldS5704 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5704);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
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
  int32_t _M0L6_2atmpS2218;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2218 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS2218;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS2217;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2217 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS2217;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS2208;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS2208 = _M0L4selfS155->$1;
  if (_M0L3lenS2208 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS2209 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS2211 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS2210 = Moonbit_array_length(_M0L4dataS2211);
    if (_M0L3lenS2209 == _M0L6_2atmpS2210) {
      uint16_t* _M0L4dataS2212 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS2212);
      return _M0L4dataS2212;
    } else {
      uint16_t* _M0L4dataS2213 = _M0L4selfS155->$0;
      int32_t _M0L3lenS2214 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS2215;
      int32_t _M0L3lenS2216;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS2213);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS2215 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS2216 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2213, _M0L3lenS2214, _M0L6_2atmpS2215, _M0L3lenS2216, 0, 0);
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
  int32_t _if__result_6176;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS2204 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS2205 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS2204 <= _M0L6_2atmpS2205) {
            int32_t _M0L6_2atmpS2203 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_6176 = _M0L6_2atmpS2203 <= _M0L13allocate__lenS148;
          } else {
            _if__result_6176 = 0;
          }
        } else {
          _if__result_6176 = 0;
        }
      } else {
        _if__result_6176 = 0;
      }
    } else {
      _if__result_6176 = 0;
    }
  } else {
    _if__result_6176 = 0;
  }
  if (_if__result_6176) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS2207;
    moonbit_string_t _M0L6_2atmpS2206;
    uint16_t* _result_6177;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_32.data);
    _M0L6_2atmpS2207 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS2207);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS2206
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_6177 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS2206);
    moonbit_decref_cycle_free(_M0L6_2atmpS2206);
    return _result_6177;
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
  struct _M0TPB13StringBuilder* _block_6178;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS2202 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS2202 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_6178
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_6178)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 117, 0);
  _block_6178->$0 = _M0L4dataS140;
  _block_6178->$1 = 0;
  return _block_6178;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS2201;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2201 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS2201;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_6179;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS2182 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS2183;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2183
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
          if (_M0L6_2atmpS2182 <= _M0L6_2atmpS2183) {
            int32_t _M0L6_2atmpS2181 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_6179 = _M0L6_2atmpS2181 <= _M0L13allocate__lenS113;
          } else {
            _if__result_6179 = 0;
          }
        } else {
          _if__result_6179 = 0;
        }
      } else {
        _if__result_6179 = 0;
      }
    } else {
      _if__result_6179 = 0;
    }
  } else {
    _if__result_6179 = 0;
  }
  if (_if__result_6179) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS2185;
    moonbit_string_t _M0L6_2atmpS2184;
    int32_t* _result_6180;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2185 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS2185);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2184
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6180
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS2184);
    moonbit_decref_cycle_free(_M0L6_2atmpS2184);
    return _result_6180;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_6181;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS2187 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS2188;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2188
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
          if (_M0L6_2atmpS2187 <= _M0L6_2atmpS2188) {
            int32_t _M0L6_2atmpS2186 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_6181 = _M0L6_2atmpS2186 <= _M0L13allocate__lenS119;
          } else {
            _if__result_6181 = 0;
          }
        } else {
          _if__result_6181 = 0;
        }
      } else {
        _if__result_6181 = 0;
      }
    } else {
      _if__result_6181 = 0;
    }
  } else {
    _if__result_6181 = 0;
  }
  if (_if__result_6181) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS2190;
    moonbit_string_t _M0L6_2atmpS2189;
    moonbit_string_t* _result_6182;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2190 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS2190);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2189
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6182
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS2189);
    moonbit_decref_cycle_free(_M0L6_2atmpS2189);
    return _result_6182;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_6183;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS2192 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS2193;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2193
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
          if (_M0L6_2atmpS2192 <= _M0L6_2atmpS2193) {
            int32_t _M0L6_2atmpS2191 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_6183 = _M0L6_2atmpS2191 <= _M0L13allocate__lenS125;
          } else {
            _if__result_6183 = 0;
          }
        } else {
          _if__result_6183 = 0;
        }
      } else {
        _if__result_6183 = 0;
      }
    } else {
      _if__result_6183 = 0;
    }
  } else {
    _if__result_6183 = 0;
  }
  if (_if__result_6183) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, 0, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS2195;
    moonbit_string_t _M0L6_2atmpS2194;
    struct _M0TUsiE** _result_6184;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2195 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS2195);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2194
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6184
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS2194);
    moonbit_decref_cycle_free(_M0L6_2atmpS2194);
    return _result_6184;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_6185;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS2197 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS2198;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2198
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS2197 <= _M0L6_2atmpS2198) {
            int32_t _M0L6_2atmpS2196 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_6185 = _M0L6_2atmpS2196 <= _M0L13allocate__lenS131;
          } else {
            _if__result_6185 = 0;
          }
        } else {
          _if__result_6185 = 0;
        }
      } else {
        _if__result_6185 = 0;
      }
    } else {
      _if__result_6185 = 0;
    }
  } else {
    _if__result_6185 = 0;
  }
  if (_if__result_6185) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS2200;
    moonbit_string_t _M0L6_2atmpS2199;
    float* _result_6186;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2200 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS2200);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2199
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6186
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS2199);
    moonbit_decref_cycle_free(_M0L6_2atmpS2199);
    return _result_6186;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS2178;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS2178
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS2178);
  if (_M0L6_2atmpS2178.$1) {
    moonbit_decref(_M0L6_2atmpS2178.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS2179;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS2179
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS2179);
  if (_M0L6_2atmpS2179.$1) {
    moonbit_decref(_M0L6_2atmpS2179.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS2180;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS2180
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS2180);
  if (_M0L6_2atmpS2180.$1) {
    moonbit_decref(_M0L6_2atmpS2180.$1);
  }
  return 0;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS86,
  int32_t _M0L13allocate__lenS84,
  int32_t _M0L11src__offsetS87,
  int32_t _M0L11dst__offsetS85,
  int32_t _M0L9blit__lenS88
) {
  int32_t* _M0L3dstS83;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS83
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS84);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS83, _M0L11dst__offsetS85, _M0L3srcS86, _M0L11src__offsetS87, _M0L9blit__lenS88);
  moonbit_decref_cycle_free(_M0L3srcS86);
  return _M0L3dstS83;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS92,
  int32_t _M0L13allocate__lenS90,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS91,
  int32_t _M0L9blit__lenS94
) {
  moonbit_string_t* _M0L3dstS89;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS89
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS90, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS89, _M0L11dst__offsetS91, _M0L3srcS92, _M0L11src__offsetS93, _M0L9blit__lenS94);
  moonbit_decref_cycle_free(_M0L3srcS92);
  return _M0L3dstS89;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  struct _M0TUsiE** _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS96, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
  moonbit_decref_cycle_free(_M0L3srcS98);
  return _M0L3dstS95;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS104,
  int32_t _M0L13allocate__lenS102,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS103,
  int32_t _M0L9blit__lenS106
) {
  float* _M0L3dstS101;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS101
  = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS102);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS101, _M0L11dst__offsetS103, _M0L3srcS104, _M0L11src__offsetS105, _M0L9blit__lenS106);
  moonbit_decref_cycle_free(_M0L3srcS104);
  return _M0L3dstS101;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS63,
  int32_t _M0L11dst__offsetS64,
  int32_t* _M0L3srcS65,
  int32_t _M0L11src__offsetS66,
  int32_t _M0L3lenS67
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS65);
  moonbit_incref_cycle_free(_M0L3dstS63);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS63, _M0L11dst__offsetS64, _M0L3srcS65, _M0L11src__offsetS66, _M0L3lenS67, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  moonbit_string_t* _M0L3srcS70,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  struct _M0TUsiE** _M0L3srcS75,
  int32_t _M0L11src__offsetS76,
  int32_t _M0L3lenS77
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS75);
  moonbit_incref_cycle_free(_M0L3dstS73);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS73, _M0L11dst__offsetS74, _M0L3srcS75, _M0L11src__offsetS76, _M0L3lenS77);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS78,
  int32_t _M0L11dst__offsetS79,
  float* _M0L3srcS80,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L3lenS82
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS80);
  moonbit_incref_cycle_free(_M0L3dstS78);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS78, _M0L11dst__offsetS79, _M0L3srcS80, _M0L11src__offsetS81, _M0L3lenS82, sizeof(float));
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
        int32_t _M0L6_2atmpS2133 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS2135 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS2134;
        int32_t _M0L6_2atmpS2136;
        if (
          _M0L6_2atmpS2135 < 0
          || _M0L6_2atmpS2135 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2134 = (int32_t)_M0L3srcS19[_M0L6_2atmpS2135];
        if (
          _M0L6_2atmpS2133 < 0
          || _M0L6_2atmpS2133 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS2133] = _M0L6_2atmpS2134;
        _M0L6_2atmpS2136 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS2136;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2141 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS2141;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS2137 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS2139 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS2138;
        int32_t _M0L6_2atmpS2140;
        if (
          _M0L6_2atmpS2139 < 0
          || _M0L6_2atmpS2139 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2138 = (int32_t)_M0L3srcS19[_M0L6_2atmpS2139];
        if (
          _M0L6_2atmpS2137 < 0
          || _M0L6_2atmpS2137 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS2137] = _M0L6_2atmpS2138;
        _M0L6_2atmpS2140 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS2140;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS27,
  int32_t _M0L11dst__offsetS29,
  int32_t* _M0L3srcS28,
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
        int32_t _M0L6_2atmpS2142 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS2144 = _M0L11src__offsetS30 + _M0L1iS31;
        int32_t _M0L6_2atmpS2143;
        int32_t _M0L6_2atmpS2145;
        if (
          _M0L6_2atmpS2144 < 0
          || _M0L6_2atmpS2144 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2143 = (int32_t)_M0L3srcS28[_M0L6_2atmpS2144];
        if (
          _M0L6_2atmpS2142 < 0
          || _M0L6_2atmpS2142 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS2142] = _M0L6_2atmpS2143;
        _M0L6_2atmpS2145 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS2145;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2150 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS2150;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS2146 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS2148 = _M0L11src__offsetS30 + _M0L1iS34;
        int32_t _M0L6_2atmpS2147;
        int32_t _M0L6_2atmpS2149;
        if (
          _M0L6_2atmpS2148 < 0
          || _M0L6_2atmpS2148 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2147 = (int32_t)_M0L3srcS28[_M0L6_2atmpS2148];
        if (
          _M0L6_2atmpS2146 < 0
          || _M0L6_2atmpS2146 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS2146] = _M0L6_2atmpS2147;
        _M0L6_2atmpS2149 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS2149;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  moonbit_string_t* _M0L3srcS37,
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
        int32_t _M0L6_2atmpS2151 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS2153 = _M0L11src__offsetS39 + _M0L1iS40;
        moonbit_string_t _M0L6_2atmpS2152;
        moonbit_string_t _M0L6_2aoldS5705;
        int32_t _M0L6_2atmpS2154;
        if (
          _M0L6_2atmpS2153 < 0
          || _M0L6_2atmpS2153 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2152 = (moonbit_string_t)_M0L3srcS37[_M0L6_2atmpS2153];
        if (
          _M0L6_2atmpS2151 < 0
          || _M0L6_2atmpS2151 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5705 = (moonbit_string_t)_M0L3dstS36[_M0L6_2atmpS2151];
        moonbit_incref_cycle_free(_M0L6_2atmpS2152);
        moonbit_decref_cycle_free(_M0L6_2aoldS5705);
        _M0L3dstS36[_M0L6_2atmpS2151] = _M0L6_2atmpS2152;
        _M0L6_2atmpS2154 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS2154;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2159 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS2159;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS2155 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS2157 = _M0L11src__offsetS39 + _M0L1iS43;
        moonbit_string_t _M0L6_2atmpS2156;
        moonbit_string_t _M0L6_2aoldS5706;
        int32_t _M0L6_2atmpS2158;
        if (
          _M0L6_2atmpS2157 < 0
          || _M0L6_2atmpS2157 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2156 = (moonbit_string_t)_M0L3srcS37[_M0L6_2atmpS2157];
        if (
          _M0L6_2atmpS2155 < 0
          || _M0L6_2atmpS2155 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5706 = (moonbit_string_t)_M0L3dstS36[_M0L6_2atmpS2155];
        moonbit_incref_cycle_free(_M0L6_2atmpS2156);
        moonbit_decref_cycle_free(_M0L6_2aoldS5706);
        _M0L3dstS36[_M0L6_2atmpS2155] = _M0L6_2atmpS2156;
        _M0L6_2atmpS2158 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS2158;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  struct _M0TUsiE** _M0L3srcS46,
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
        int32_t _M0L6_2atmpS2160 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS2162 = _M0L11src__offsetS48 + _M0L1iS49;
        struct _M0TUsiE* _M0L6_2atmpS2161;
        struct _M0TUsiE* _M0L6_2aoldS5707;
        int32_t _M0L6_2atmpS2163;
        if (
          _M0L6_2atmpS2162 < 0
          || _M0L6_2atmpS2162 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2161 = (struct _M0TUsiE*)_M0L3srcS46[_M0L6_2atmpS2162];
        if (
          _M0L6_2atmpS2160 < 0
          || _M0L6_2atmpS2160 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5707 = (struct _M0TUsiE*)_M0L3dstS45[_M0L6_2atmpS2160];
        if (_M0L6_2atmpS2161) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2161);
        }
        if (_M0L6_2aoldS5707) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5707);
        }
        _M0L3dstS45[_M0L6_2atmpS2160] = _M0L6_2atmpS2161;
        _M0L6_2atmpS2163 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS2163;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2168 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS2168;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS2164 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS2166 = _M0L11src__offsetS48 + _M0L1iS52;
        struct _M0TUsiE* _M0L6_2atmpS2165;
        struct _M0TUsiE* _M0L6_2aoldS5708;
        int32_t _M0L6_2atmpS2167;
        if (
          _M0L6_2atmpS2166 < 0
          || _M0L6_2atmpS2166 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2165 = (struct _M0TUsiE*)_M0L3srcS46[_M0L6_2atmpS2166];
        if (
          _M0L6_2atmpS2164 < 0
          || _M0L6_2atmpS2164 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5708 = (struct _M0TUsiE*)_M0L3dstS45[_M0L6_2atmpS2164];
        if (_M0L6_2atmpS2165) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2165);
        }
        if (_M0L6_2aoldS5708) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5708);
        }
        _M0L3dstS45[_M0L6_2atmpS2164] = _M0L6_2atmpS2165;
        _M0L6_2atmpS2167 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS2167;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS54,
  int32_t _M0L11dst__offsetS56,
  float* _M0L3srcS55,
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
        int32_t _M0L6_2atmpS2169 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS2171 = _M0L11src__offsetS57 + _M0L1iS58;
        float _M0L6_2atmpS2170;
        int32_t _M0L6_2atmpS2172;
        if (
          _M0L6_2atmpS2171 < 0
          || _M0L6_2atmpS2171 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2170 = (float)_M0L3srcS55[_M0L6_2atmpS2171];
        if (
          _M0L6_2atmpS2169 < 0
          || _M0L6_2atmpS2169 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS2169] = _M0L6_2atmpS2170;
        _M0L6_2atmpS2172 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS2172;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2177 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS2177;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS2173 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS2175 = _M0L11src__offsetS57 + _M0L1iS61;
        float _M0L6_2atmpS2174;
        int32_t _M0L6_2atmpS2176;
        if (
          _M0L6_2atmpS2175 < 0
          || _M0L6_2atmpS2175 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2174 = (float)_M0L3srcS55[_M0L6_2atmpS2175];
        if (
          _M0L6_2atmpS2173 < 0
          || _M0L6_2atmpS2173 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS2173] = _M0L6_2atmpS2174;
        _M0L6_2atmpS2176 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS2176;
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS14) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS14);
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS15) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS16
) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS16);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS17) {
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_33.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_34.data);
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS3
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t _M0L3msgS4
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS4);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t _M0L3msgS5
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS7) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS2100) {
  switch (Moonbit_object_tag(_M0L4_2aeS2100)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS2100);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2128,
  struct _M0TPB4Show _M0L8_2aparamS2127
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2126 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2128;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS2126, _M0L8_2aparamS2127);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2125,
  struct _M0TPB4Show _M0L8_2aparamS2124
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2123 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2125;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS2123, _M0L8_2aparamS2124);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2122,
  int32_t _M0L8_2aparamS2121
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2120 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2122;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS2120, _M0L8_2aparamS2121);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2119,
  struct _M0TPC16string10StringView _M0L8_2aparamS2118
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2117 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2119;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS2117, _M0L8_2aparamS2118);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2116,
  moonbit_string_t _M0L8_2aparamS2113,
  int32_t _M0L8_2aparamS2114,
  int32_t _M0L8_2aparamS2115
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2112 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2116;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS2112, _M0L8_2aparamS2113, _M0L8_2aparamS2114, _M0L8_2aparamS2115);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2111,
  moonbit_string_t _M0L8_2aparamS2110
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2109 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2111;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS2109, _M0L8_2aparamS2110);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_6197 = 9218868437227405311ll;
  int64_t _tmp_6198;
  int64_t _tmp_6199;
  int64_t _tmp_6200;
  int64_t _tmp_6201;
  _M0FPB18double__max__value = *(double*)&_tmp_6197;
  _tmp_6198 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_6198;
  _tmp_6199 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_6199;
  _tmp_6200 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_6200;
  _tmp_6201 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_6201;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS2132;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS2093;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS2094;
  int32_t _M0L7_2abindS2095;
  struct _M0TUsiE** _M0L7_2abindS2096;
  int32_t _M0L6_2acntS5885;
  int32_t _M0L2__S2097;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS2132
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS2093
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS2093)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 120, 0);
  _M0L12async__testsS2093->$0 = _M0L6_2atmpS2132;
  _M0L12async__testsS2093->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS2094
  = _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS2095 = _M0L7_2abindS2094->$1;
  _M0L7_2abindS2096 = _M0L7_2abindS2094->$0;
  _M0L6_2acntS5885
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS2094));
  if (_M0L6_2acntS5885 > 1) {
    int32_t _M0L11_2anew__cntS5886 = _M0L6_2acntS5885 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS2094), _M0L11_2anew__cntS5886);
    moonbit_incref_cycle_free(_M0L7_2abindS2096);
  } else if (_M0L6_2acntS5885 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS2094);
  }
  _M0L2__S2097 = 0;
  while (1) {
    if (_M0L2__S2097 < _M0L7_2abindS2095) {
      struct _M0TUsiE* _M0L3argS2098 =
        (struct _M0TUsiE*)_M0L7_2abindS2096[_M0L2__S2097];
      moonbit_string_t _M0L6_2atmpS2129 = _M0L3argS2098->$0;
      int32_t _M0L6_2atmpS2130 = _M0L3argS2098->$1;
      int32_t _M0L6_2atmpS2131;
      moonbit_incref_cycle_free(_M0L6_2atmpS2129);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples34afferent__response__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS2093, _M0L6_2atmpS2129, _M0L6_2atmpS2130);
      moonbit_decref_cycle_free(_M0L6_2atmpS2129);
      _M0L6_2atmpS2131 = _M0L2__S2097 + 1;
      _M0L2__S2097 = _M0L6_2atmpS2131;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS2096);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\afferent_response\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples34afferent__response__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34afferent__response__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS2093);
  moonbit_decref_cycle_free(_M0L12async__testsS2093);
  moonbit_flush_cycles();
  return 0;
}