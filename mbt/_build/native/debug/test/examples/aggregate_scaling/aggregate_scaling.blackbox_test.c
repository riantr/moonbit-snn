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

struct _M0TWRPC15error5ErrorEs;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt16AggregateScaling;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TUdiE;

struct _M0BTPB6Logger;

struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt13SynapseTarget;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TPB8MutLocalGbE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247;

struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter;

struct _M0TUddE;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt16AggregateScaling {
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  int32_t $7;
  int32_t $8;
  
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

struct _M0TUdiE {
  double $0;
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

struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0TP26RiantR8snn__mbt13SynapseTarget {
  int32_t $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGiE* $2;
  
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

struct _M0DTPC16option6OptionGfE4Some {
  float $0;
  
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

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE {
  struct _M0TP26RiantR8snn__mbt13SynapseTarget** $0;
  int32_t $1;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
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

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TPB8MutLocalGfE {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0TPB8MutLocalGbE {
  int32_t $0;
  
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

struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter {
  float $0;
  float $1;
  float $2;
  struct _M0TPB5ArrayGfE* $3;
  float $4;
  float $5;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1259(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1252(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1247(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1224(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1217(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt30aggregate__scaling__plasticity(
  struct _M0TP26RiantR8snn__mbt16AggregateScaling*,
  int32_t
);

int32_t _M0FP26RiantR8snn__mbt27aggregate__scaling__forward(
  struct _M0TP26RiantR8snn__mbt16AggregateScaling*,
  struct _M0TPB5ArrayGfE*,
  float
);

struct _M0TP26RiantR8snn__mbt16AggregateScaling* _M0MP26RiantR8snn__mbt16AggregateScaling26with__plasticity__interval(
  struct _M0TP26RiantR8snn__mbt16AggregateScaling*,
  int32_t
);

struct _M0TP26RiantR8snn__mbt16AggregateScaling* _M0MP26RiantR8snn__mbt16AggregateScaling3new(
  int32_t,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE*,
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter*
);

struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0MP26RiantR8snn__mbt25AggregateScalingParameter15uniform_2einner(
  int32_t,
  float,
  float,
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0MP26RiantR8snn__mbt25AggregateScalingParameter11new_2einner(
  float,
  float,
  float,
  struct _M0TPB5ArrayGfE*,
  float,
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

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  int32_t,
  int32_t,
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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  int32_t,
  int32_t,
  float
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

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t,
  float
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE*
);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

struct _M0TP26RiantR8snn__mbt13SynapseTarget** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE*
);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

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

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

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
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_14 =
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
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[124]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 123, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 97, 103, 103, 114, 101, 103, 97, 
    116, 101, 95, 115, 99, 97, 108, 105, 110, 103, 95, 98, 108, 97, 99, 
    107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 
    107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[122]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 121, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 97, 103, 103, 114, 101, 103, 97, 
    116, 101, 95, 115, 99, 97, 108, 105, 110, 103, 95, 98, 108, 97, 99, 
    107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 
    69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_33 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[43]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 42, 105, 110, 
    100, 101, 120, 32, 111, 117, 116, 32, 111, 102, 32, 98, 111, 117, 
    110, 100, 115, 58, 32, 116, 104, 101, 32, 108, 101, 110, 32, 105, 
    115, 32, 102, 114, 111, 109, 32, 48, 32, 116, 111, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_31 =
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
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 115, 116, 97, 114, 116, 34, 44, 34, 
    102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_17 =
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
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 98, 
    117, 116, 32, 116, 104, 101, 32, 105, 110, 100, 101, 120, 32, 105, 
    115, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1259$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1259
  };

uint32_t const moonbit_layout_table_data[94] =
  {
    sizeof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247)
    / 4, 1,
    offsetof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247, $1)
    / 4
    * 2,
    sizeof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252)
    / 4, 1,
    offsetof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt16AggregateScaling) / 4, 6,
    offsetof(struct _M0TP26RiantR8snn__mbt16AggregateScaling, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AggregateScaling, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AggregateScaling, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AggregateScaling, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AggregateScaling, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AggregateScaling, $6) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter) / 4, 
    1,
    offsetof(struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter, $3)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
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
    sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $4) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2682
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1280,
  moonbit_string_t _M0L8filenameS1249,
  int32_t _M0L5indexS1251
) {
  struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247* _closure_2719;
  struct _M0TWEu* _M0L13handle__startS1247;
  struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252* _closure_2720;
  struct _M0TWssbEu* _M0L14handle__resultS1252;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1259;
  void* _M0L11_2atry__errS1274;
  struct moonbit_result_0 _tmp_2722;
  int32_t _handle__error__result_2723;
  int32_t _M0L6_2atmpS2670;
  void* _M0L3errS1275;
  moonbit_string_t _M0L4nameS1277;
  struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1278;
  moonbit_string_t _M0L7_2anameS1279;
  int32_t _M0L6_2acntS2707;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1249);
  _closure_2719
  = (struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247*)moonbit_malloc(sizeof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247));
  Moonbit_object_header(_closure_2719)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2719->code
  = &_M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1247;
  _closure_2719->$0 = _M0L5indexS1251;
  _closure_2719->$1 = _M0L8filenameS1249;
  _M0L13handle__startS1247 = (struct _M0TWEu*)_closure_2719;
  moonbit_incref_cycle_free(_M0L8filenameS1249);
  _closure_2720
  = (struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252*)moonbit_malloc(sizeof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252));
  Moonbit_object_header(_closure_2720)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2720->code
  = &_M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1252;
  _closure_2720->$0 = _M0L5indexS1251;
  _closure_2720->$1 = _M0L8filenameS1249;
  _M0L14handle__resultS1252 = (struct _M0TWssbEu*)_closure_2720;
  _M0L17error__to__stringS1259
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1259$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2722
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1280, _M0L8filenameS1249, _M0L5indexS1251, _M0L13handle__startS1247, _M0L14handle__resultS1252, _M0L17error__to__stringS1259);
  if (_tmp_2722.tag) {
    int32_t const _M0L5_2aokS2679 = _tmp_2722.data.ok;
    _handle__error__result_2723 = _M0L5_2aokS2679;
  } else {
    void* const _M0L6_2aerrS2680 = _tmp_2722.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
    moonbit_decref_cycle_free(_M0L13handle__startS1247);
    _M0L11_2atry__errS1274 = _M0L6_2aerrS2680;
    goto join_1273;
  }
  if (_handle__error__result_2723) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
    moonbit_decref_cycle_free(_M0L13handle__startS1247);
    _M0L6_2atmpS2670 = 1;
  } else {
    struct moonbit_result_0 _tmp_2724;
    int32_t _handle__error__result_2725;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2724
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1280, _M0L8filenameS1249, _M0L5indexS1251, _M0L13handle__startS1247, _M0L14handle__resultS1252, _M0L17error__to__stringS1259);
    if (_tmp_2724.tag) {
      int32_t const _M0L5_2aokS2677 = _tmp_2724.data.ok;
      _handle__error__result_2725 = _M0L5_2aokS2677;
    } else {
      void* const _M0L6_2aerrS2678 = _tmp_2724.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
      moonbit_decref_cycle_free(_M0L13handle__startS1247);
      _M0L11_2atry__errS1274 = _M0L6_2aerrS2678;
      goto join_1273;
    }
    if (_handle__error__result_2725) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
      moonbit_decref_cycle_free(_M0L13handle__startS1247);
      _M0L6_2atmpS2670 = 1;
    } else {
      struct moonbit_result_0 _tmp_2726;
      int32_t _handle__error__result_2727;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2726
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1280, _M0L8filenameS1249, _M0L5indexS1251, _M0L13handle__startS1247, _M0L14handle__resultS1252, _M0L17error__to__stringS1259);
      if (_tmp_2726.tag) {
        int32_t const _M0L5_2aokS2675 = _tmp_2726.data.ok;
        _handle__error__result_2727 = _M0L5_2aokS2675;
      } else {
        void* const _M0L6_2aerrS2676 = _tmp_2726.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
        moonbit_decref_cycle_free(_M0L13handle__startS1247);
        _M0L11_2atry__errS1274 = _M0L6_2aerrS2676;
        goto join_1273;
      }
      if (_handle__error__result_2727) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
        moonbit_decref_cycle_free(_M0L13handle__startS1247);
        _M0L6_2atmpS2670 = 1;
      } else {
        struct moonbit_result_0 _tmp_2728;
        int32_t _handle__error__result_2729;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2728
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1280, _M0L8filenameS1249, _M0L5indexS1251, _M0L13handle__startS1247, _M0L14handle__resultS1252, _M0L17error__to__stringS1259);
        if (_tmp_2728.tag) {
          int32_t const _M0L5_2aokS2673 = _tmp_2728.data.ok;
          _handle__error__result_2729 = _M0L5_2aokS2673;
        } else {
          void* const _M0L6_2aerrS2674 = _tmp_2728.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
          moonbit_decref_cycle_free(_M0L13handle__startS1247);
          _M0L11_2atry__errS1274 = _M0L6_2aerrS2674;
          goto join_1273;
        }
        if (_handle__error__result_2729) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
          moonbit_decref_cycle_free(_M0L13handle__startS1247);
          _M0L6_2atmpS2670 = 1;
        } else {
          struct moonbit_result_0 _tmp_2730;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2730
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1280, _M0L8filenameS1249, _M0L5indexS1251, _M0L13handle__startS1247, _M0L14handle__resultS1252, _M0L17error__to__stringS1259);
          moonbit_decref_cycle_free(_M0L13handle__startS1247);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1259);
          if (_tmp_2730.tag) {
            int32_t const _M0L5_2aokS2671 = _tmp_2730.data.ok;
            _M0L6_2atmpS2670 = _M0L5_2aokS2671;
          } else {
            void* const _M0L6_2aerrS2672 = _tmp_2730.data.err;
            _M0L11_2atry__errS1274 = _M0L6_2aerrS2672;
            goto join_1273;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2670) {
    void* _M0L137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2681 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2681)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2681)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1274
    = _M0L137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2681;
    goto join_1273;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1252);
  }
  goto joinlet_2721;
  join_1273:;
  _M0L3errS1275 = _M0L11_2atry__errS1274;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1278
  = (struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1275;
  _M0L7_2anameS1279 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1278->$0;
  _M0L6_2acntS2707
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1278));
  if (_M0L6_2acntS2707 > 1) {
    int32_t _M0L11_2anew__cntS2708 = _M0L6_2acntS2707 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1278), _M0L11_2anew__cntS2708);
    moonbit_incref_cycle_free(_M0L7_2anameS1279);
  } else if (_M0L6_2acntS2707 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1278);
  }
  _M0L4nameS1277 = _M0L7_2anameS1279;
  goto join_1276;
  goto joinlet_2731;
  join_1276:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1252(_M0L14handle__resultS1252, _M0L4nameS1277, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1252);
  moonbit_decref_cycle_free(_M0L4nameS1277);
  joinlet_2731:;
  joinlet_2721:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1259(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2669,
  void* _M0L3errS1260
) {
  void* _M0L1eS1262;
  moonbit_string_t _M0L1eS1264;
  moonbit_string_t _result_2734;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1260)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1265 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1260;
      moonbit_string_t _M0L4_2aeS1266 = _M0L10_2aFailureS1265->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1266);
      _M0L1eS1264 = _M0L4_2aeS1266;
      goto join_1263;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1267 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1260;
      moonbit_string_t _M0L4_2aeS1268 = _M0L15_2aInspectErrorS1267->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1268);
      _M0L1eS1264 = _M0L4_2aeS1268;
      goto join_1263;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1269 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1260;
      moonbit_string_t _M0L4_2aeS1270 = _M0L16_2aSnapshotErrorS1269->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1270);
      _M0L1eS1264 = _M0L4_2aeS1270;
      goto join_1263;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1271 =
        (struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1260;
      moonbit_string_t _M0L4_2aeS1272 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1271->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1272);
      _M0L1eS1264 = _M0L4_2aeS1272;
      goto join_1263;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1260);
      _M0L1eS1262 = _M0L3errS1260;
      goto join_1261;
      break;
    }
  }
  join_1263:;
  return _M0L1eS1264;
  join_1261:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _result_2734 = _M0FP15Error10to__string(_M0L1eS1262);
  moonbit_decref_cycle_free(_M0L1eS1262);
  return _result_2734;
}

int32_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1252(
  struct _M0TWssbEu* _M0L6_2aenvS2666,
  moonbit_string_t _M0L10__testnameS1253,
  moonbit_string_t _M0L7messageS1254,
  int32_t _M0L7skippedS1255
) {
  struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252* _M0L14_2acasted__envS2667;
  moonbit_string_t _M0L8filenameS1249;
  int32_t _M0L5indexS1251;
  moonbit_string_t _M0L10file__nameS1256;
  moonbit_string_t _M0L7messageS1257;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1258;
  moonbit_string_t _M0L6_2atmpS2668;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2667
  = (struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1252*)_M0L6_2aenvS2666;
  _M0L8filenameS1249 = _M0L14_2acasted__envS2667->$1;
  _M0L5indexS1251 = _M0L14_2acasted__envS2667->$0;
  if (!_M0L7skippedS1255 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1256
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1249, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1257
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1254, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1258
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1258, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1258, _M0L10file__nameS1256);
  moonbit_decref_cycle_free(_M0L10file__nameS1256);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1258, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1258, _M0L5indexS1251);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1258, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1258, _M0L7messageS1257);
  moonbit_decref_cycle_free(_M0L7messageS1257);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1258, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2668
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1258);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1258);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2668);
  moonbit_decref_cycle_free(_M0L6_2atmpS2668);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1247(
  struct _M0TWEu* _M0L6_2aenvS2663
) {
  struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247* _M0L14_2acasted__envS2664;
  moonbit_string_t _M0L8filenameS1249;
  int32_t _M0L5indexS1251;
  moonbit_string_t _M0L10file__nameS1248;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1250;
  moonbit_string_t _M0L6_2atmpS2665;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2664
  = (struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2faggregate__scaling__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1247*)_M0L6_2aenvS2663;
  _M0L8filenameS1249 = _M0L14_2acasted__envS2664->$1;
  _M0L5indexS1251 = _M0L14_2acasted__envS2664->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1248
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1249, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1250
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1250, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1250, _M0L10file__nameS1248);
  moonbit_decref_cycle_free(_M0L10file__nameS1248);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1250, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1250, _M0L5indexS1251);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1250, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2665
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1250);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1250);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2665);
  moonbit_decref_cycle_free(_M0L6_2atmpS2665);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1217;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1224;
  struct _M0TUsiE** _M0L6_2atmpS2662;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1231;
  moonbit_string_t* _M0L9cli__argsS1232;
  moonbit_string_t _M0L6_2atmpS2661;
  moonbit_string_t _M0L6_2atmpS2660;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1233;
  int32_t _M0L7_2abindS1234;
  moonbit_string_t* _M0L7_2abindS1235;
  int32_t _M0L6_2acntS2709;
  int32_t _M0L2__S1236;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1217 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1224 = 0;
  _M0L6_2atmpS2662 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1231
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1231)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1231->$0 = _M0L6_2atmpS2662;
  _M0L16file__and__indexS1231->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1232
  = _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1232)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2661 = (moonbit_string_t)_M0L9cli__argsS1232[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2661);
  moonbit_decref_cycle_free(_M0L9cli__argsS1232);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2660
  = _M0MP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2661);
  moonbit_decref_cycle_free(_M0L6_2atmpS2661);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1233
  = _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1224(_M0L51moonbit__test__driver__internal__split__mbt__stringS1224, _M0L6_2atmpS2660, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2660);
  _M0L7_2abindS1234 = _M0L10test__argsS1233->$1;
  _M0L7_2abindS1235 = _M0L10test__argsS1233->$0;
  _M0L6_2acntS2709
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1233));
  if (_M0L6_2acntS2709 > 1) {
    int32_t _M0L11_2anew__cntS2710 = _M0L6_2acntS2709 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1233), _M0L11_2anew__cntS2710);
    moonbit_incref_cycle_free(_M0L7_2abindS1235);
  } else if (_M0L6_2acntS2709 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1233);
  }
  _M0L2__S1236 = 0;
  while (1) {
    if (_M0L2__S1236 < _M0L7_2abindS1234) {
      moonbit_string_t _M0L3argS1237 =
        (moonbit_string_t)_M0L7_2abindS1235[_M0L2__S1236];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1238;
      moonbit_string_t _M0L4fileS1239;
      moonbit_string_t _M0L5rangeS1240;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1241;
      moonbit_string_t _M0L6_2atmpS2658;
      int32_t _M0L5startS1242;
      moonbit_string_t _M0L6_2atmpS2657;
      int32_t _M0L3endS1243;
      int32_t _M0L1iS1244;
      int32_t _M0L6_2atmpS2659;
      moonbit_incref_cycle_free(_M0L3argS1237);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1238
      = _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1224(_M0L51moonbit__test__driver__internal__split__mbt__stringS1224, _M0L3argS1237, 58);
      moonbit_decref_cycle_free(_M0L3argS1237);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1239
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1238, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1240
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1238, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1238);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1241
      = _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1224(_M0L51moonbit__test__driver__internal__split__mbt__stringS1224, _M0L5rangeS1240, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1240);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2658
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1241, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1242
      = _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1217(_M0L45moonbit__test__driver__internal__parse__int__S1217, _M0L6_2atmpS2658);
      moonbit_decref_cycle_free(_M0L6_2atmpS2658);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2657
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1241, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1241);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1243
      = _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1217(_M0L45moonbit__test__driver__internal__parse__int__S1217, _M0L6_2atmpS2657);
      moonbit_decref_cycle_free(_M0L6_2atmpS2657);
      _M0L1iS1244 = _M0L5startS1242;
      while (1) {
        if (_M0L1iS1244 < _M0L3endS1243) {
          struct _M0TUsiE* _M0L8_2atupleS2655;
          int32_t _M0L6_2atmpS2656;
          moonbit_incref_cycle_free(_M0L4fileS1239);
          _M0L8_2atupleS2655
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2655)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2655->$0 = _M0L4fileS1239;
          _M0L8_2atupleS2655->$1 = _M0L1iS1244;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1231, _M0L8_2atupleS2655);
          _M0L6_2atmpS2656 = _M0L1iS1244 + 1;
          _M0L1iS1244 = _M0L6_2atmpS2656;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1239);
        }
        break;
      }
      _M0L6_2atmpS2659 = _M0L2__S1236 + 1;
      _M0L2__S1236 = _M0L6_2atmpS2659;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1235);
    }
    break;
  }
  return _M0L16file__and__indexS1231;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1224(
  int32_t _M0L6_2aenvS2636,
  moonbit_string_t _M0L1sS1225,
  int32_t _M0L3sepS1226
) {
  moonbit_string_t* _M0L6_2atmpS2654;
  struct _M0TPB5ArrayGsE* _M0L3resS1227;
  struct _M0TPB8MutLocalGiE* _M0L1iS1228;
  struct _M0TPB8MutLocalGiE* _M0L5startS1229;
  int32_t _M0L3valS2649;
  int32_t _M0L6_2atmpS2650;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2654 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1227
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1227)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1227->$0 = _M0L6_2atmpS2654;
  _M0L3resS1227->$1 = 0;
  _M0L1iS1228
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1228)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1228->$0 = 0;
  _M0L5startS1229
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1229)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1229->$0 = 0;
  while (1) {
    int32_t _M0L3valS2637 = _M0L1iS1228->$0;
    int32_t _M0L6_2atmpS2638 = Moonbit_array_length(_M0L1sS1225);
    if (_M0L3valS2637 < _M0L6_2atmpS2638) {
      int32_t _M0L3valS2641 = _M0L1iS1228->$0;
      int32_t _M0L6_2atmpS2640;
      int32_t _M0L6_2atmpS2639;
      int32_t _M0L3valS2648;
      int32_t _M0L6_2atmpS2647;
      if (
        _M0L3valS2641 < 0
        || _M0L3valS2641 >= Moonbit_array_length(_M0L1sS1225)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2640 = _M0L1sS1225[_M0L3valS2641];
      _M0L6_2atmpS2639 = _M0L6_2atmpS2640;
      if (_M0L6_2atmpS2639 == _M0L3sepS1226) {
        int32_t _M0L3valS2643 = _M0L5startS1229->$0;
        int32_t _M0L3valS2644 = _M0L1iS1228->$0;
        moonbit_string_t _M0L6_2atmpS2642;
        int32_t _M0L3valS2646;
        int32_t _M0L6_2atmpS2645;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2642
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1225, _M0L3valS2643, _M0L3valS2644);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1227, _M0L6_2atmpS2642);
        _M0L3valS2646 = _M0L1iS1228->$0;
        _M0L6_2atmpS2645 = _M0L3valS2646 + 1;
        _M0L5startS1229->$0 = _M0L6_2atmpS2645;
      }
      _M0L3valS2648 = _M0L1iS1228->$0;
      _M0L6_2atmpS2647 = _M0L3valS2648 + 1;
      _M0L1iS1228->$0 = _M0L6_2atmpS2647;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1228);
    }
    break;
  }
  _M0L3valS2649 = _M0L5startS1229->$0;
  _M0L6_2atmpS2650 = Moonbit_array_length(_M0L1sS1225);
  if (_M0L3valS2649 < _M0L6_2atmpS2650) {
    int32_t _M0L3valS2652 = _M0L5startS1229->$0;
    int32_t _M0L6_2atmpS2653;
    moonbit_string_t _M0L6_2atmpS2651;
    moonbit_decref_cycle_free(_M0L5startS1229);
    _M0L6_2atmpS2653 = Moonbit_array_length(_M0L1sS1225);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2651
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1225, _M0L3valS2652, _M0L6_2atmpS2653);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1227, _M0L6_2atmpS2651);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1229);
  }
  return _M0L3resS1227;
}

int32_t _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1217(
  int32_t _M0L6_2aenvS2629,
  moonbit_string_t _M0L1sS1218
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1219;
  int32_t _M0L3lenS1220;
  int32_t _M0L7_2abindS1221;
  int32_t _M0L1iS1222;
  int32_t _result_2739;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1219
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1219->$0 = 0;
  _M0L3lenS1220 = Moonbit_array_length(_M0L1sS1218);
  _M0L7_2abindS1221 = 0;
  _M0L1iS1222 = _M0L7_2abindS1221;
  while (1) {
    if (_M0L1iS1222 < _M0L3lenS1220) {
      int32_t _M0L3valS2634 = _M0L3resS1219->$0;
      int32_t _M0L6_2atmpS2631 = _M0L3valS2634 * 10;
      int32_t _M0L6_2atmpS2633;
      int32_t _M0L6_2atmpS2632;
      int32_t _M0L6_2atmpS2630;
      int32_t _M0L6_2atmpS2635;
      if (
        _M0L1iS1222 < 0 || _M0L1iS1222 >= Moonbit_array_length(_M0L1sS1218)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2633 = _M0L1sS1218[_M0L1iS1222];
      _M0L6_2atmpS2632 = _M0L6_2atmpS2633 - 48;
      _M0L6_2atmpS2630 = _M0L6_2atmpS2631 + _M0L6_2atmpS2632;
      _M0L3resS1219->$0 = _M0L6_2atmpS2630;
      _M0L6_2atmpS2635 = _M0L1iS1222 + 1;
      _M0L1iS1222 = _M0L6_2atmpS2635;
      continue;
    }
    break;
  }
  _result_2739 = _M0L3resS1219->$0;
  moonbit_decref_cycle_free(_M0L3resS1219);
  return _result_2739;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1216
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1216);
  return _M0L4selfS1216;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1186,
  moonbit_string_t _M0L12_2adiscard__S1187,
  int32_t _M0L12_2adiscard__S1188,
  struct _M0TWEu* _M0L12_2adiscard__S1189,
  struct _M0TWssbEu* _M0L12_2adiscard__S1190,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1191
) {
  struct moonbit_result_0 _result_2740;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _result_2740.tag = 1;
  _result_2740.data.ok = 0;
  return _result_2740;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1192,
  moonbit_string_t _M0L12_2adiscard__S1193,
  int32_t _M0L12_2adiscard__S1194,
  struct _M0TWEu* _M0L12_2adiscard__S1195,
  struct _M0TWssbEu* _M0L12_2adiscard__S1196,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1197
) {
  struct moonbit_result_0 _result_2741;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _result_2741.tag = 1;
  _result_2741.data.ok = 0;
  return _result_2741;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1198,
  moonbit_string_t _M0L12_2adiscard__S1199,
  int32_t _M0L12_2adiscard__S1200,
  struct _M0TWEu* _M0L12_2adiscard__S1201,
  struct _M0TWssbEu* _M0L12_2adiscard__S1202,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1203
) {
  struct moonbit_result_0 _result_2742;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _result_2742.tag = 1;
  _result_2742.data.ok = 0;
  return _result_2742;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1204,
  moonbit_string_t _M0L12_2adiscard__S1205,
  int32_t _M0L12_2adiscard__S1206,
  struct _M0TWEu* _M0L12_2adiscard__S1207,
  struct _M0TWssbEu* _M0L12_2adiscard__S1208,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1209
) {
  struct moonbit_result_0 _result_2743;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _result_2743.tag = 1;
  _result_2743.data.ok = 0;
  return _result_2743;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1210,
  moonbit_string_t _M0L12_2adiscard__S1211,
  int32_t _M0L12_2adiscard__S1212,
  struct _M0TWEu* _M0L12_2adiscard__S1213,
  struct _M0TWssbEu* _M0L12_2adiscard__S1214,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1215
) {
  struct moonbit_result_0 _result_2744;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _result_2744.tag = 1;
  _result_2744.data.ok = 0;
  return _result_2744;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1185
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt30aggregate__scaling__plasticity(
  struct _M0TP26RiantR8snn__mbt16AggregateScaling* _M0L1cS1122,
  int32_t _M0L11step__countS1123
) {
  int32_t _M0L27plasticity__interval__stepsS2547;
  int32_t _M0L27plasticity__interval__stepsS2549;
  int32_t _M0L6_2atmpS2548;
  int32_t _M0L1nS1124;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS2628;
  int32_t _M0L10n__targetsS1125;
  struct _M0TPB8MutLocalGiE* _M0L1iS1126;
  struct _M0TPB8MutLocalGiE* _M0L1tS1128;
  #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L27plasticity__interval__stepsS2547 = _M0L1cS1122->$8;
  if (_M0L27plasticity__interval__stepsS2547 <= 0) {
    return 0;
  }
  _M0L27plasticity__interval__stepsS2549 = _M0L1cS1122->$8;
  _M0L6_2atmpS2548
  = _M0L11step__countS1123 % _M0L27plasticity__interval__stepsS2549;
  if (_M0L6_2atmpS2548 != 0) {
    return 0;
  }
  _M0L1nS1124 = _M0L1cS1122->$1;
  _M0L7targetsS2628 = _M0L1cS1122->$2;
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L10n__targetsS1125
  = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS2628);
  _M0L1iS1126
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1126)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1126->$0 = 0;
  while (1) {
    int32_t _M0L3valS2550 = _M0L1iS1126->$0;
    if (_M0L3valS2550 < _M0L1nS1124) {
      struct _M0TPB5ArrayGfE* _M0L2wtS2551 = _M0L1cS1122->$3;
      int32_t _M0L3valS2552 = _M0L1iS1126->$0;
      int32_t _M0L3valS2554;
      int32_t _M0L6_2atmpS2553;
      #line 289 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0MPC15array5Array3setGfE(_M0L2wtS2551, _M0L3valS2552, 0x0p+0f);
      _M0L3valS2554 = _M0L1iS1126->$0;
      _M0L6_2atmpS2553 = _M0L3valS2554 + 1;
      _M0L1iS1126->$0 = _M0L6_2atmpS2553;
      continue;
    }
    break;
  }
  _M0L1tS1128
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1tS1128)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1tS1128->$0 = 0;
  while (1) {
    int32_t _M0L3valS2555 = _M0L1tS1128->$0;
    if (_M0L3valS2555 < _M0L10n__targetsS1125) {
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS2575 =
        _M0L1cS1122->$2;
      int32_t _M0L3valS2576 = _M0L1tS1128->$0;
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L2tgS1129;
      struct _M0TPB5ArrayGfE* _M0L4valsS1130;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS1131;
      int32_t _M0L6_2acntS2711;
      int32_t _M0L3valS2574;
      int32_t _M0L6_2atmpS2573;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L2tgS1129
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS2575, _M0L3valS2576);
      _M0L4valsS1130 = _M0L2tgS1129->$1;
      _M0L6rowptrS1131 = _M0L2tgS1129->$2;
      _M0L6_2acntS2711
      = Moonbit_rc_count(Moonbit_object_header(_M0L2tgS1129));
      if (_M0L6_2acntS2711 > 1) {
        int32_t _M0L11_2anew__cntS2712 = _M0L6_2acntS2711 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L2tgS1129), _M0L11_2anew__cntS2712);
        moonbit_incref_cycle_free(_M0L6rowptrS1131);
        moonbit_incref_cycle_free(_M0L4valsS1130);
      } else if (_M0L6_2acntS2711 == 1) {
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        moonbit_free(_M0L2tgS1129);
      }
      _M0L1iS1126->$0 = 0;
      while (1) {
        int32_t _M0L3valS2556 = _M0L1iS1126->$0;
        if (_M0L3valS2556 < _M0L1nS1124) {
          int32_t _M0L3valS2572 = _M0L1iS1126->$0;
          int32_t _M0L5startS1132;
          int32_t _M0L3valS2571;
          int32_t _M0L6_2atmpS2570;
          int32_t _M0L3endS1133;
          struct _M0TPB8MutLocalGiE* _M0L1jS1134;
          int32_t _M0L3valS2569;
          int32_t _M0L6_2atmpS2568;
          #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
          _M0L5startS1132
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1131, _M0L3valS2572);
          _M0L3valS2571 = _M0L1iS1126->$0;
          _M0L6_2atmpS2570 = _M0L3valS2571 + 1;
          #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
          _M0L3endS1133
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1131, _M0L6_2atmpS2570);
          _M0L1jS1134
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1jS1134)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1jS1134->$0 = _M0L5startS1132;
          while (1) {
            int32_t _M0L3valS2557 = _M0L1jS1134->$0;
            if (_M0L3valS2557 < _M0L3endS1133) {
              struct _M0TPB5ArrayGfE* _M0L2wtS2558 = _M0L1cS1122->$3;
              int32_t _M0L3valS2559 = _M0L1iS1126->$0;
              struct _M0TPB5ArrayGfE* _M0L2wtS2564 = _M0L1cS1122->$3;
              int32_t _M0L3valS2565 = _M0L1iS1126->$0;
              float _M0L6_2atmpS2561;
              int32_t _M0L3valS2563;
              float _M0L6_2atmpS2562;
              float _M0L6_2atmpS2560;
              int32_t _M0L3valS2567;
              int32_t _M0L6_2atmpS2566;
              #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
              _M0L6_2atmpS2561
              = _M0MPC15array5Array2atGfE(_M0L2wtS2564, _M0L3valS2565);
              _M0L3valS2563 = _M0L1jS1134->$0;
              #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
              _M0L6_2atmpS2562
              = _M0MPC15array5Array2atGfE(_M0L4valsS1130, _M0L3valS2563);
              _M0L6_2atmpS2560 = _M0L6_2atmpS2561 + _M0L6_2atmpS2562;
              #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
              _M0MPC15array5Array3setGfE(_M0L2wtS2558, _M0L3valS2559, _M0L6_2atmpS2560);
              _M0L3valS2567 = _M0L1jS1134->$0;
              _M0L6_2atmpS2566 = _M0L3valS2567 + 1;
              _M0L1jS1134->$0 = _M0L6_2atmpS2566;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1jS1134);
            }
            break;
          }
          _M0L3valS2569 = _M0L1iS1126->$0;
          _M0L6_2atmpS2568 = _M0L3valS2569 + 1;
          _M0L1iS1126->$0 = _M0L6_2atmpS2568;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L6rowptrS1131);
          moonbit_decref_cycle_free(_M0L4valsS1130);
        }
        break;
      }
      _M0L3valS2574 = _M0L1tS1128->$0;
      _M0L6_2atmpS2573 = _M0L3valS2574 + 1;
      _M0L1tS1128->$0 = _M0L6_2atmpS2573;
      continue;
    }
    break;
  }
  _M0L1iS1126->$0 = 0;
  while (1) {
    int32_t _M0L3valS2577 = _M0L1iS1126->$0;
    if (_M0L3valS2577 < _M0L1nS1124) {
      struct _M0TPB5ArrayGfE* _M0L2wtS2579 = _M0L1cS1122->$3;
      int32_t _M0L3valS2580 = _M0L1iS1126->$0;
      float _M0L6_2atmpS2578;
      int32_t _M0L3valS2596;
      int32_t _M0L6_2atmpS2595;
      #line 314 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L6_2atmpS2578
      = _M0MPC15array5Array2atGfE(_M0L2wtS2579, _M0L3valS2580);
      if (_M0L6_2atmpS2578 > 0x0p+0f) {
        struct _M0TPB5ArrayGfE* _M0L2muS2581 = _M0L1cS1122->$6;
        int32_t _M0L3valS2582 = _M0L1iS1126->$0;
        struct _M0TPB5ArrayGfE* _M0L9wt__totalS2591 = _M0L1cS1122->$4;
        int32_t _M0L3valS2592 = _M0L1iS1126->$0;
        float _M0L6_2atmpS2588;
        struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0L5paramS2590;
        float _M0L6w__minS2589;
        float _M0L6_2atmpS2584;
        struct _M0TPB5ArrayGfE* _M0L2wtS2586;
        int32_t _M0L3valS2587;
        float _M0L6_2atmpS2585;
        float _M0L6_2atmpS2583;
        #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        _M0L6_2atmpS2588
        = _M0MPC15array5Array2atGfE(_M0L9wt__totalS2591, _M0L3valS2592);
        _M0L5paramS2590 = _M0L1cS1122->$0;
        _M0L6w__minS2589 = _M0L5paramS2590->$4;
        _M0L6_2atmpS2584 = _M0L6_2atmpS2588 - _M0L6w__minS2589;
        _M0L2wtS2586 = _M0L1cS1122->$3;
        _M0L3valS2587 = _M0L1iS1126->$0;
        #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        _M0L6_2atmpS2585
        = _M0MPC15array5Array2atGfE(_M0L2wtS2586, _M0L3valS2587);
        _M0L6_2atmpS2583 = _M0L6_2atmpS2584 / _M0L6_2atmpS2585;
        #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        _M0MPC15array5Array3setGfE(_M0L2muS2581, _M0L3valS2582, _M0L6_2atmpS2583);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2muS2593 = _M0L1cS1122->$6;
        int32_t _M0L3valS2594 = _M0L1iS1126->$0;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        _M0MPC15array5Array3setGfE(_M0L2muS2593, _M0L3valS2594, 0x1p+0f);
      }
      _M0L3valS2596 = _M0L1iS1126->$0;
      _M0L6_2atmpS2595 = _M0L3valS2596 + 1;
      _M0L1iS1126->$0 = _M0L6_2atmpS2595;
      continue;
    }
    break;
  }
  _M0L1tS1128->$0 = 0;
  while (1) {
    int32_t _M0L3valS2597 = _M0L1tS1128->$0;
    if (_M0L3valS2597 < _M0L10n__targetsS1125) {
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS2618 =
        _M0L1cS1122->$2;
      int32_t _M0L3valS2619 = _M0L1tS1128->$0;
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L2tgS1139;
      struct _M0TPB5ArrayGfE* _M0L4valsS1140;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS1141;
      int32_t _M0L6_2acntS2713;
      int32_t _M0L3valS2617;
      int32_t _M0L6_2atmpS2616;
      #line 324 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L2tgS1139
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS2618, _M0L3valS2619);
      _M0L4valsS1140 = _M0L2tgS1139->$1;
      _M0L6rowptrS1141 = _M0L2tgS1139->$2;
      _M0L6_2acntS2713
      = Moonbit_rc_count(Moonbit_object_header(_M0L2tgS1139));
      if (_M0L6_2acntS2713 > 1) {
        int32_t _M0L11_2anew__cntS2714 = _M0L6_2acntS2713 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L2tgS1139), _M0L11_2anew__cntS2714);
        moonbit_incref_cycle_free(_M0L6rowptrS1141);
        moonbit_incref_cycle_free(_M0L4valsS1140);
      } else if (_M0L6_2acntS2713 == 1) {
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        moonbit_free(_M0L2tgS1139);
      }
      _M0L1iS1126->$0 = 0;
      while (1) {
        int32_t _M0L3valS2598 = _M0L1iS1126->$0;
        if (_M0L3valS2598 < _M0L1nS1124) {
          int32_t _M0L3valS2615 = _M0L1iS1126->$0;
          int32_t _M0L5startS1142;
          int32_t _M0L3valS2614;
          int32_t _M0L6_2atmpS2613;
          int32_t _M0L3endS1143;
          struct _M0TPB5ArrayGfE* _M0L2muS2611;
          int32_t _M0L3valS2612;
          float _M0L5mu__iS1144;
          struct _M0TPB8MutLocalGiE* _M0L1jS1145;
          int32_t _M0L3valS2610;
          int32_t _M0L6_2atmpS2609;
          #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
          _M0L5startS1142
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1141, _M0L3valS2615);
          _M0L3valS2614 = _M0L1iS1126->$0;
          _M0L6_2atmpS2613 = _M0L3valS2614 + 1;
          #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
          _M0L3endS1143
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1141, _M0L6_2atmpS2613);
          _M0L2muS2611 = _M0L1cS1122->$6;
          _M0L3valS2612 = _M0L1iS1126->$0;
          #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
          _M0L5mu__iS1144
          = _M0MPC15array5Array2atGfE(_M0L2muS2611, _M0L3valS2612);
          _M0L1jS1145
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1jS1145)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1jS1145->$0 = _M0L5startS1142;
          while (1) {
            int32_t _M0L3valS2599 = _M0L1jS1145->$0;
            if (_M0L3valS2599 < _M0L3endS1143) {
              int32_t _M0L3valS2600 = _M0L1jS1145->$0;
              int32_t _M0L3valS2606 = _M0L1jS1145->$0;
              float _M0L6_2atmpS2605;
              float _M0L6_2atmpS2602;
              struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0L5paramS2604;
              float _M0L6w__minS2603;
              float _M0L6_2atmpS2601;
              int32_t _M0L3valS2608;
              int32_t _M0L6_2atmpS2607;
              #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
              _M0L6_2atmpS2605
              = _M0MPC15array5Array2atGfE(_M0L4valsS1140, _M0L3valS2606);
              _M0L6_2atmpS2602 = _M0L6_2atmpS2605 * _M0L5mu__iS1144;
              _M0L5paramS2604 = _M0L1cS1122->$0;
              _M0L6w__minS2603 = _M0L5paramS2604->$4;
              _M0L6_2atmpS2601 = _M0L6_2atmpS2602 + _M0L6w__minS2603;
              #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
              _M0MPC15array5Array3setGfE(_M0L4valsS1140, _M0L3valS2600, _M0L6_2atmpS2601);
              _M0L3valS2608 = _M0L1jS1145->$0;
              _M0L6_2atmpS2607 = _M0L3valS2608 + 1;
              _M0L1jS1145->$0 = _M0L6_2atmpS2607;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1jS1145);
            }
            break;
          }
          _M0L3valS2610 = _M0L1iS1126->$0;
          _M0L6_2atmpS2609 = _M0L3valS2610 + 1;
          _M0L1iS1126->$0 = _M0L6_2atmpS2609;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L6rowptrS1141);
          moonbit_decref_cycle_free(_M0L4valsS1140);
        }
        break;
      }
      _M0L3valS2617 = _M0L1tS1128->$0;
      _M0L6_2atmpS2616 = _M0L3valS2617 + 1;
      _M0L1tS1128->$0 = _M0L6_2atmpS2616;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1tS1128);
    }
    break;
  }
  _M0L1iS1126->$0 = 0;
  while (1) {
    int32_t _M0L3valS2620 = _M0L1iS1126->$0;
    if (_M0L3valS2620 < _M0L1nS1124) {
      struct _M0TPB5ArrayGfE* _M0L9wt__totalS2621 = _M0L1cS1122->$4;
      int32_t _M0L3valS2622 = _M0L1iS1126->$0;
      struct _M0TPB5ArrayGfE* _M0L2wtS2624 = _M0L1cS1122->$3;
      int32_t _M0L3valS2625 = _M0L1iS1126->$0;
      float _M0L6_2atmpS2623;
      int32_t _M0L3valS2627;
      int32_t _M0L6_2atmpS2626;
      #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L6_2atmpS2623
      = _M0MPC15array5Array2atGfE(_M0L2wtS2624, _M0L3valS2625);
      #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0MPC15array5Array3setGfE(_M0L9wt__totalS2621, _M0L3valS2622, _M0L6_2atmpS2623);
      _M0L3valS2627 = _M0L1iS1126->$0;
      _M0L6_2atmpS2626 = _M0L3valS2627 + 1;
      _M0L1iS1126->$0 = _M0L6_2atmpS2626;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1126);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt27aggregate__scaling__forward(
  struct _M0TP26RiantR8snn__mbt16AggregateScaling* _M0L1cS1107,
  struct _M0TPB5ArrayGfE* _M0L4fireS1116,
  float _M0L2dtS1105
) {
  int32_t _M0L1nS1106;
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0L5paramS2546;
  float _M0L6tau__aS2545;
  float _M0L11tau__a__invS1108;
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0L5paramS2544;
  float _M0L6tau__eS2543;
  float _M0L11tau__e__invS1109;
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0L5paramS2542;
  float _M0L6w__maxS1110;
  struct _M0TPB5ArrayGfE* _M0L1yS1111;
  struct _M0TPB5ArrayGfE* _M0L9wt__totalS1112;
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0L5paramS2541;
  struct _M0TPB5ArrayGfE* _M0L8param__yS1113;
  struct _M0TPB8MutLocalGiE* _M0L1iS1114;
  #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L1nS1106 = _M0L1cS1107->$1;
  _M0L5paramS2546 = _M0L1cS1107->$0;
  _M0L6tau__aS2545 = _M0L5paramS2546->$1;
  _M0L11tau__a__invS1108 = 0x1p+0f / _M0L6tau__aS2545;
  _M0L5paramS2544 = _M0L1cS1107->$0;
  _M0L6tau__eS2543 = _M0L5paramS2544->$2;
  _M0L11tau__e__invS1109 = 0x1p+0f / _M0L6tau__eS2543;
  _M0L5paramS2542 = _M0L1cS1107->$0;
  _M0L6w__maxS1110 = _M0L5paramS2542->$5;
  _M0L1yS1111 = _M0L1cS1107->$5;
  _M0L9wt__totalS1112 = _M0L1cS1107->$4;
  _M0L5paramS2541 = _M0L1cS1107->$0;
  _M0L8param__yS1113 = _M0L5paramS2541->$3;
  _M0L1iS1114
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1114)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1114->$0 = 0;
  while (1) {
    int32_t _M0L3valS2503 = _M0L1iS1114->$0;
    if (_M0L3valS2503 < _M0L1nS1106) {
      int32_t _M0L3valS2504 = _M0L1iS1114->$0;
      int32_t _M0L3valS2510 = _M0L1iS1114->$0;
      float _M0L6_2atmpS2506;
      int32_t _M0L3valS2509;
      float _M0L6_2atmpS2508;
      float _M0L6_2atmpS2507;
      float _M0L6_2atmpS2505;
      int32_t _M0L3valS2512;
      int32_t _M0L6_2atmpS2511;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L6_2atmpS2506
      = _M0MPC15array5Array2atGfE(_M0L1yS1111, _M0L3valS2510);
      _M0L3valS2509 = _M0L1iS1114->$0;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L6_2atmpS2508
      = _M0MPC15array5Array2atGfE(_M0L1yS1111, _M0L3valS2509);
      _M0L6_2atmpS2507 = _M0L6_2atmpS2508 * _M0L11tau__a__invS1108;
      _M0L6_2atmpS2505 = _M0L6_2atmpS2506 - _M0L6_2atmpS2507;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0MPC15array5Array3setGfE(_M0L1yS1111, _M0L3valS2504, _M0L6_2atmpS2505);
      _M0L3valS2512 = _M0L1iS1114->$0;
      _M0L6_2atmpS2511 = _M0L3valS2512 + 1;
      _M0L1iS1114->$0 = _M0L6_2atmpS2511;
      continue;
    }
    break;
  }
  _M0L1iS1114->$0 = 0;
  while (1) {
    int32_t _M0L3valS2513 = _M0L1iS1114->$0;
    if (_M0L3valS2513 < _M0L1nS1106) {
      int32_t _M0L3valS2515 = _M0L1iS1114->$0;
      float _M0L6_2atmpS2514;
      int32_t _M0L3valS2521;
      int32_t _M0L6_2atmpS2520;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L6_2atmpS2514
      = _M0MPC15array5Array2atGfE(_M0L4fireS1116, _M0L3valS2515);
      if (_M0L6_2atmpS2514 >= 0x1p-1f) {
        int32_t _M0L3valS2516 = _M0L1iS1114->$0;
        int32_t _M0L3valS2519 = _M0L1iS1114->$0;
        float _M0L6_2atmpS2518;
        float _M0L6_2atmpS2517;
        #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        _M0L6_2atmpS2518
        = _M0MPC15array5Array2atGfE(_M0L1yS1111, _M0L3valS2519);
        _M0L6_2atmpS2517 = _M0L6_2atmpS2518 + 0x1p+0f;
        #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        _M0MPC15array5Array3setGfE(_M0L1yS1111, _M0L3valS2516, _M0L6_2atmpS2517);
      }
      _M0L3valS2521 = _M0L1iS1114->$0;
      _M0L6_2atmpS2520 = _M0L3valS2521 + 1;
      _M0L1iS1114->$0 = _M0L6_2atmpS2520;
      continue;
    }
    break;
  }
  _M0L1iS1114->$0 = 0;
  while (1) {
    int32_t _M0L3valS2522 = _M0L1iS1114->$0;
    if (_M0L3valS2522 < _M0L1nS1106) {
      int32_t _M0L3valS2536 = _M0L1iS1114->$0;
      float _M0L6_2atmpS2535;
      float _M0L2yiS1118;
      float _M0L6_2atmpS2534;
      float _M0L14one__minus__yYS1119;
      int32_t _M0L3valS2533;
      float _M0L6_2atmpS2532;
      float _M0L6_2atmpS2531;
      float _M0L8wt__termS1120;
      int32_t _M0L3valS2523;
      int32_t _M0L3valS2528;
      float _M0L6_2atmpS2525;
      float _M0L6_2atmpS2527;
      float _M0L6_2atmpS2526;
      float _M0L6_2atmpS2524;
      int32_t _M0L3valS2530;
      int32_t _M0L6_2atmpS2529;
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L6_2atmpS2535
      = _M0MPC15array5Array2atGfE(_M0L8param__yS1113, _M0L3valS2536);
      if (_M0L6_2atmpS2535 > 0x0p+0f) {
        int32_t _M0L3valS2540 = _M0L1iS1114->$0;
        float _M0L6_2atmpS2537;
        int32_t _M0L3valS2539;
        float _M0L6_2atmpS2538;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        _M0L6_2atmpS2537
        = _M0MPC15array5Array2atGfE(_M0L1yS1111, _M0L3valS2540);
        _M0L3valS2539 = _M0L1iS1114->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        _M0L6_2atmpS2538
        = _M0MPC15array5Array2atGfE(_M0L8param__yS1113, _M0L3valS2539);
        _M0L2yiS1118 = _M0L6_2atmpS2537 / _M0L6_2atmpS2538;
      } else {
        _M0L2yiS1118 = 0x0p+0f;
      }
      _M0L6_2atmpS2534 = 0x1p+0f - _M0L2yiS1118;
      if (_M0L6_2atmpS2534 > 0x0p+0f) {
        _M0L14one__minus__yYS1119 = 0x1p+0f - _M0L2yiS1118;
      } else {
        _M0L14one__minus__yYS1119 = 0x0p+0f;
      }
      _M0L3valS2533 = _M0L1iS1114->$0;
      #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L6_2atmpS2532
      = _M0MPC15array5Array2atGfE(_M0L9wt__totalS1112, _M0L3valS2533);
      _M0L6_2atmpS2531 = _M0L6_2atmpS2532 / _M0L6w__maxS1110;
      _M0L8wt__termS1120 = 0x1p+0f - _M0L6_2atmpS2531;
      _M0L3valS2523 = _M0L1iS1114->$0;
      _M0L3valS2528 = _M0L1iS1114->$0;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L6_2atmpS2525
      = _M0MPC15array5Array2atGfE(_M0L9wt__totalS1112, _M0L3valS2528);
      _M0L6_2atmpS2527 = _M0L8wt__termS1120 * _M0L14one__minus__yYS1119;
      _M0L6_2atmpS2526 = _M0L6_2atmpS2527 * _M0L11tau__e__invS1109;
      _M0L6_2atmpS2524 = _M0L6_2atmpS2525 + _M0L6_2atmpS2526;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0MPC15array5Array3setGfE(_M0L9wt__totalS1112, _M0L3valS2523, _M0L6_2atmpS2524);
      _M0L3valS2530 = _M0L1iS1114->$0;
      _M0L6_2atmpS2529 = _M0L3valS2530 + 1;
      _M0L1iS1114->$0 = _M0L6_2atmpS2529;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1114);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt16AggregateScaling* _M0MP26RiantR8snn__mbt16AggregateScaling26with__plasticity__interval(
  struct _M0TP26RiantR8snn__mbt16AggregateScaling* _M0L1cS1103,
  int32_t _M0L15interval__stepsS1104
) {
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0L11_2afield__0S2495;
  int32_t _M0L11_2afield__1S2496;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L11_2afield__2S2497;
  struct _M0TPB5ArrayGfE* _M0L11_2afield__3S2498;
  struct _M0TPB5ArrayGfE* _M0L11_2afield__4S2499;
  struct _M0TPB5ArrayGfE* _M0L11_2afield__5S2500;
  struct _M0TPB5ArrayGfE* _M0L11_2afield__6S2501;
  int32_t _M0L11_2afield__7S2502;
  struct _M0TP26RiantR8snn__mbt16AggregateScaling* _block_2757;
  #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L11_2afield__0S2495 = _M0L1cS1103->$0;
  _M0L11_2afield__1S2496 = _M0L1cS1103->$1;
  _M0L11_2afield__2S2497 = _M0L1cS1103->$2;
  _M0L11_2afield__3S2498 = _M0L1cS1103->$3;
  _M0L11_2afield__4S2499 = _M0L1cS1103->$4;
  _M0L11_2afield__5S2500 = _M0L1cS1103->$5;
  _M0L11_2afield__6S2501 = _M0L1cS1103->$6;
  _M0L11_2afield__7S2502 = _M0L1cS1103->$7;
  moonbit_incref_cycle_free(_M0L11_2afield__0S2495);
  moonbit_incref_cycle_free(_M0L11_2afield__2S2497);
  moonbit_incref_cycle_free(_M0L11_2afield__3S2498);
  moonbit_incref_cycle_free(_M0L11_2afield__4S2499);
  moonbit_incref_cycle_free(_M0L11_2afield__5S2500);
  moonbit_incref_cycle_free(_M0L11_2afield__6S2501);
  _block_2757
  = (struct _M0TP26RiantR8snn__mbt16AggregateScaling*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16AggregateScaling));
  Moonbit_object_header(_block_2757)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2757->$0 = _M0L11_2afield__0S2495;
  _block_2757->$1 = _M0L11_2afield__1S2496;
  _block_2757->$2 = _M0L11_2afield__2S2497;
  _block_2757->$3 = _M0L11_2afield__3S2498;
  _block_2757->$4 = _M0L11_2afield__4S2499;
  _block_2757->$5 = _M0L11_2afield__5S2500;
  _block_2757->$6 = _M0L11_2afield__6S2501;
  _block_2757->$7 = _M0L11_2afield__7S2502;
  _block_2757->$8 = _M0L15interval__stepsS1104;
  return _block_2757;
}

struct _M0TP26RiantR8snn__mbt16AggregateScaling* _M0MP26RiantR8snn__mbt16AggregateScaling3new(
  int32_t _M0L1nS1085,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS1087,
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0L5paramS1102
) {
  struct _M0TPB5ArrayGfE* _M0L9wt__totalS1084;
  struct _M0TPB8MutLocalGiE* _M0L1tS1086;
  struct _M0TPB5ArrayGfE* _M0L2wtS1098;
  struct _M0TPB5ArrayGfE* _M0L1yS1099;
  struct _M0TPB5ArrayGfE* _M0L2muS1100;
  int32_t _M0L15interval__stepsS1101;
  struct _M0TP26RiantR8snn__mbt16AggregateScaling* _block_2761;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L9wt__totalS1084 = _M0MPC15array5Array4makeGfE(_M0L1nS1085, 0x0p+0f);
  _M0L1tS1086
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1tS1086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1tS1086->$0 = 0;
  while (1) {
    int32_t _M0L3valS2475 = _M0L1tS1086->$0;
    int32_t _M0L6_2atmpS2476;
    #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
    _M0L6_2atmpS2476
    = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS1087);
    if (_M0L3valS2475 < _M0L6_2atmpS2476) {
      int32_t _M0L3valS2494 = _M0L1tS1086->$0;
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L2tgS1088;
      struct _M0TPB5ArrayGfE* _M0L4valsS1089;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS1090;
      int32_t _M0L6_2acntS2715;
      struct _M0TPB8MutLocalGiE* _M0L1iS1091;
      int32_t _M0L3valS2493;
      int32_t _M0L6_2atmpS2492;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
      _M0L2tgS1088
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS1087, _M0L3valS2494);
      _M0L4valsS1089 = _M0L2tgS1088->$1;
      _M0L6rowptrS1090 = _M0L2tgS1088->$2;
      _M0L6_2acntS2715
      = Moonbit_rc_count(Moonbit_object_header(_M0L2tgS1088));
      if (_M0L6_2acntS2715 > 1) {
        int32_t _M0L11_2anew__cntS2716 = _M0L6_2acntS2715 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L2tgS1088), _M0L11_2anew__cntS2716);
        moonbit_incref_cycle_free(_M0L6rowptrS1090);
        moonbit_incref_cycle_free(_M0L4valsS1089);
      } else if (_M0L6_2acntS2715 == 1) {
        #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
        moonbit_free(_M0L2tgS1088);
      }
      _M0L1iS1091
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1iS1091)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1iS1091->$0 = 0;
      while (1) {
        int32_t _M0L3valS2477 = _M0L1iS1091->$0;
        if (_M0L3valS2477 < _M0L1nS1085) {
          int32_t _M0L3valS2491 = _M0L1iS1091->$0;
          int32_t _M0L5startS1092;
          int32_t _M0L3valS2490;
          int32_t _M0L6_2atmpS2489;
          int32_t _M0L3endS1093;
          struct _M0TPB8MutLocalGiE* _M0L1jS1094;
          int32_t _M0L3valS2488;
          int32_t _M0L6_2atmpS2487;
          #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
          _M0L5startS1092
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1090, _M0L3valS2491);
          _M0L3valS2490 = _M0L1iS1091->$0;
          _M0L6_2atmpS2489 = _M0L3valS2490 + 1;
          #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
          _M0L3endS1093
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1090, _M0L6_2atmpS2489);
          _M0L1jS1094
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1jS1094)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1jS1094->$0 = _M0L5startS1092;
          while (1) {
            int32_t _M0L3valS2478 = _M0L1jS1094->$0;
            if (_M0L3valS2478 < _M0L3endS1093) {
              int32_t _M0L3valS2479 = _M0L1iS1091->$0;
              int32_t _M0L3valS2484 = _M0L1iS1091->$0;
              float _M0L6_2atmpS2481;
              int32_t _M0L3valS2483;
              float _M0L6_2atmpS2482;
              float _M0L6_2atmpS2480;
              int32_t _M0L3valS2486;
              int32_t _M0L6_2atmpS2485;
              #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
              _M0L6_2atmpS2481
              = _M0MPC15array5Array2atGfE(_M0L9wt__totalS1084, _M0L3valS2484);
              _M0L3valS2483 = _M0L1jS1094->$0;
              #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
              _M0L6_2atmpS2482
              = _M0MPC15array5Array2atGfE(_M0L4valsS1089, _M0L3valS2483);
              _M0L6_2atmpS2480 = _M0L6_2atmpS2481 + _M0L6_2atmpS2482;
              #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
              _M0MPC15array5Array3setGfE(_M0L9wt__totalS1084, _M0L3valS2479, _M0L6_2atmpS2480);
              _M0L3valS2486 = _M0L1jS1094->$0;
              _M0L6_2atmpS2485 = _M0L3valS2486 + 1;
              _M0L1jS1094->$0 = _M0L6_2atmpS2485;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1jS1094);
            }
            break;
          }
          _M0L3valS2488 = _M0L1iS1091->$0;
          _M0L6_2atmpS2487 = _M0L3valS2488 + 1;
          _M0L1iS1091->$0 = _M0L6_2atmpS2487;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1iS1091);
          moonbit_decref_cycle_free(_M0L6rowptrS1090);
          moonbit_decref_cycle_free(_M0L4valsS1089);
        }
        break;
      }
      _M0L3valS2493 = _M0L1tS1086->$0;
      _M0L6_2atmpS2492 = _M0L3valS2493 + 1;
      _M0L1tS1086->$0 = _M0L6_2atmpS2492;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1tS1086);
    }
    break;
  }
  #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L2wtS1098 = _M0MPC15array5Array4makeGfE(_M0L1nS1085, 0x0p+0f);
  #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L1yS1099 = _M0MPC15array5Array4makeGfE(_M0L1nS1085, 0x0p+0f);
  #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L2muS1100 = _M0MPC15array5Array4makeGfE(_M0L1nS1085, 0x0p+0f);
  _M0L15interval__stepsS1101 = 80;
  moonbit_incref_cycle_free(_M0L5paramS1102);
  moonbit_incref_cycle_free(_M0L7targetsS1087);
  _block_2761
  = (struct _M0TP26RiantR8snn__mbt16AggregateScaling*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16AggregateScaling));
  Moonbit_object_header(_block_2761)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2761->$0 = _M0L5paramS1102;
  _block_2761->$1 = _M0L1nS1085;
  _block_2761->$2 = _M0L7targetsS1087;
  _block_2761->$3 = _M0L2wtS1098;
  _block_2761->$4 = _M0L9wt__totalS1084;
  _block_2761->$5 = _M0L1yS1099;
  _block_2761->$6 = _M0L2muS1100;
  _block_2761->$7 = 0;
  _block_2761->$8 = _M0L15interval__stepsS1101;
  return _block_2761;
}

struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0MP26RiantR8snn__mbt25AggregateScalingParameter15uniform_2einner(
  int32_t _M0L1nS1078,
  float _M0L8rate__hzS1076,
  float _M0L3tauS1079,
  float _M0L6tau__aS1080,
  float _M0L6tau__eS1081,
  float _M0L6w__minS1082,
  float _M0L6w__maxS1083
) {
  float _M0L14rate__internalS1075;
  struct _M0TPB5ArrayGfE* _M0L6y__arrS1077;
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _result_2762;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L14rate__internalS1075 = _M0L8rate__hzS1076 * 0x1.999999999999ap-4f;
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _M0L6y__arrS1077
  = _M0MPC15array5Array4makeGfE(_M0L1nS1078, _M0L14rate__internalS1075);
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  _result_2762
  = _M0MP26RiantR8snn__mbt25AggregateScalingParameter11new_2einner(_M0L3tauS1079, _M0L6tau__aS1080, _M0L6tau__eS1081, _M0L6y__arrS1077, _M0L6w__minS1082, _M0L6w__maxS1083);
  moonbit_decref_cycle_free(_M0L6y__arrS1077);
  return _result_2762;
}

struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _M0MP26RiantR8snn__mbt25AggregateScalingParameter11new_2einner(
  float _M0L3tauS1069,
  float _M0L6tau__aS1070,
  float _M0L6tau__eS1071,
  struct _M0TPB5ArrayGfE* _M0L1yS1072,
  float _M0L6w__minS1073,
  float _M0L6w__maxS1074
) {
  struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter* _block_2763;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\aggregate_scaling.mbt"
  moonbit_incref_cycle_free(_M0L1yS1072);
  _block_2763
  = (struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt25AggregateScalingParameter));
  Moonbit_object_header(_block_2763)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 26, 0);
  _block_2763->$0 = _M0L3tauS1069;
  _block_2763->$1 = _M0L6tau__aS1070;
  _block_2763->$2 = _M0L6tau__eS1071;
  _block_2763->$3 = _M0L1yS1072;
  _block_2763->$4 = _M0L6w__minS1073;
  _block_2763->$5 = _M0L6w__maxS1074;
  return _block_2763;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1062,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1063,
  moonbit_string_t _M0L3symS1068,
  float _M0L2muS1064,
  float _M0L5sigmaS1065,
  float _M0L1pS1066,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1067
) {
  int32_t _M0L1nS2473;
  int32_t _M0L1nS2474;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1061;
  float* _M0L6_2atmpS2472;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2463;
  float* _M0L6_2atmpS2471;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2464;
  float* _M0L6_2atmpS2470;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2465;
  int32_t* _M0L6_2atmpS2469;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2466;
  float* _M0L6_2atmpS2468;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2467;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2764;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2473 = _M0L3preS1062->$2;
  _M0L1nS2474 = _M0L4postS1063->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1061
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2473, _M0L1nS2474, _M0L2muS1064, _M0L5sigmaS1065, _M0L1pS1066, _M0L3rngS1067);
  _M0L6_2atmpS2472 = moonbit_empty_float_array;
  _M0L6_2atmpS2463
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2463)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _M0L6_2atmpS2463->$0 = _M0L6_2atmpS2472;
  _M0L6_2atmpS2463->$1 = 0;
  _M0L6_2atmpS2471 = moonbit_empty_float_array;
  _M0L6_2atmpS2464
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2464)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _M0L6_2atmpS2464->$0 = _M0L6_2atmpS2471;
  _M0L6_2atmpS2464->$1 = 0;
  _M0L6_2atmpS2470 = moonbit_empty_float_array;
  _M0L6_2atmpS2465
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2465)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _M0L6_2atmpS2465->$0 = _M0L6_2atmpS2470;
  _M0L6_2atmpS2465->$1 = 0;
  _M0L6_2atmpS2469 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2466
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2466)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
  _M0L6_2atmpS2466->$0 = _M0L6_2atmpS2469;
  _M0L6_2atmpS2466->$1 = 0;
  _M0L6_2atmpS2468 = moonbit_empty_float_array;
  _M0L6_2atmpS2467
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2467)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _M0L6_2atmpS2467->$0 = _M0L6_2atmpS2468;
  _M0L6_2atmpS2467->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1062);
  moonbit_incref_cycle_free(_M0L4postS1063);
  moonbit_incref_cycle_free(_M0L3symS1068);
  _block_2764
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2764)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 35, 0);
  _block_2764->$0 = _M0L3preS1062;
  _block_2764->$1 = _M0L4postS1063;
  _block_2764->$2 = _M0L3symS1068;
  _block_2764->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2764->$4 = _M0L6matrixS1061;
  _block_2764->$5 = _M0L6_2atmpS2463;
  _block_2764->$6 = _M0L6_2atmpS2464;
  _block_2764->$7 = _M0L6_2atmpS2465;
  _block_2764->$8 = _M0L6_2atmpS2466;
  _block_2764->$9 = _M0L6_2atmpS2467;
  return _block_2764;
}

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1059,
  int32_t _M0L3preS1056,
  int32_t _M0L4postS1058,
  float _M0L1wS1060
) {
  int32_t _M0L8pre__idxS1055;
  int32_t _M0L9post__idxS1057;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2462;
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L8pre__idxS1055 = _M0L3preS1056 - 1;
  _M0L9post__idxS1057 = _M0L4postS1058 - 1;
  _M0L6matrixS2462 = _M0L1cS1059->$4;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(_M0L6matrixS2462, _M0L8pre__idxS1055, _M0L9post__idxS1057, _M0L1wS1060);
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1053;
  float _M0L2glS1054;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2765;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1053 = -0x1p+0f;
  _M0L2glS1054 = -0x1p+0f;
  _block_2765
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2765)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2765->$0 = _M0L1cS1053;
  _block_2765->$1 = _M0L2glS1054;
  _block_2765->$2 = 0x1.ep+3f;
  _block_2765->$3 = -0x1.9p+5f;
  _block_2765->$4 = -0x1.ep+5f;
  _block_2765->$5 = -0x1.18p+6f;
  _block_2765->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2765->$7 = 0x1p+1f;
  _block_2765->$8 = 0x0p+0f;
  _block_2765->$9 = 0x0p+0f;
  _block_2765->$10 = 0x0p+0f;
  return _block_2765;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1027,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1029,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1032
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1026;
  float _M0L2vtS2460;
  float _M0L2vrS2461;
  float _M0L6spreadS1028;
  int32_t _M0L7_2abindS1030;
  int32_t _M0L1kS1031;
  struct _M0TPB5ArrayGfE* _M0L1wS1034;
  struct _M0TPB5ArrayGbE* _M0L4fireS1035;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1036;
  struct _M0TPB5ArrayGfE* _M0L1iS1037;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1038;
  struct _M0TPB5ArrayGfE* _M0L2geS1039;
  struct _M0TPB5ArrayGfE* _M0L2giS1040;
  struct _M0TPB5ArrayGfE* _M0L2heS1041;
  struct _M0TPB5ArrayGfE* _M0L2hiS1042;
  struct _M0TPB5ArrayGfE* _M0L3gluS1043;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1044;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1045;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1046;
  float _M0L4e__eS1047;
  float _M0L4e__iS1048;
  float _M0L3treS1049;
  float _M0L3tdeS1050;
  float _M0L3triS1051;
  float _M0L3tdiS1052;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2459;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2767;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1026 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  _M0L2vtS2460 = _M0L5paramS1029->$3;
  _M0L2vrS2461 = _M0L5paramS1029->$4;
  _M0L6spreadS1028 = _M0L2vtS2460 - _M0L2vrS2461;
  _M0L7_2abindS1030 = 0;
  _M0L1kS1031 = _M0L7_2abindS1030;
  while (1) {
    if (_M0L1kS1031 < _M0L1nS1027) {
      float _M0L2vrS2455 = _M0L5paramS1029->$4;
      float _M0L6_2atmpS2457;
      float _M0L6_2atmpS2456;
      float _M0L6_2atmpS2454;
      int32_t _M0L6_2atmpS2458;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2457 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1032);
      _M0L6_2atmpS2456 = _M0L6_2atmpS2457 * _M0L6spreadS1028;
      _M0L6_2atmpS2454 = _M0L2vrS2455 + _M0L6_2atmpS2456;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1026, _M0L1kS1031, _M0L6_2atmpS2454);
      _M0L6_2atmpS2458 = _M0L1kS1031 + 1;
      _M0L1kS1031 = _M0L6_2atmpS2458;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1034 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1035 = _M0MPC15array5Array4makeGbE(_M0L1nS1027, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1036 = _M0MPC15array5Array4makeGiE(_M0L1nS1027, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1037 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1038 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1039 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1040 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1041 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1042 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1043 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1044 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1045 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1046 = _M0MPC15array5Array4makeGfE(_M0L1nS1027, 0x1p+0f);
  _M0L4e__eS1047 = 0x0p+0f;
  _M0L4e__iS1048 = -0x1.2cp+6f;
  _M0L3treS1049 = 0x1p+0f;
  _M0L3tdeS1050 = 0x1.8p+2f;
  _M0L3triS1051 = 0x1p-1f;
  _M0L3tdiS1052 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2459 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1029);
  _block_2767
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2767)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 47, 0);
  _block_2767->$0 = _M0L5paramS1029;
  _block_2767->$1 = _M0L6_2atmpS2459;
  _block_2767->$2 = _M0L1nS1027;
  _block_2767->$3 = _M0L1vS1026;
  _block_2767->$4 = _M0L1wS1034;
  _block_2767->$5 = _M0L4fireS1035;
  _block_2767->$6 = _M0L4tabsS1036;
  _block_2767->$7 = _M0L1iS1037;
  _block_2767->$8 = _M0L9syn__currS1038;
  _block_2767->$9 = _M0L2geS1039;
  _block_2767->$10 = _M0L2giS1040;
  _block_2767->$11 = _M0L2heS1041;
  _block_2767->$12 = _M0L2hiS1042;
  _block_2767->$13 = _M0L3gluS1043;
  _block_2767->$14 = _M0L4gabaS1044;
  _block_2767->$15 = _M0L7gsyn__eS1045;
  _block_2767->$16 = _M0L7gsyn__iS1046;
  _block_2767->$17 = _M0L4e__eS1047;
  _block_2767->$18 = _M0L4e__iS1048;
  _block_2767->$19 = _M0L3treS1049;
  _block_2767->$20 = _M0L3tdeS1050;
  _block_2767->$21 = _M0L3triS1051;
  _block_2767->$22 = _M0L3tdiS1052;
  return _block_2767;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2768;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2768
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2768)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2768->$0 = 0x1p+1f;
  return _block_2768;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1022
) {
  int32_t _M0L1nS1021;
  int32_t _M0L7_2abindS1023;
  int32_t _M0L1iS1024;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1021 = _M0L1pS1022->$2;
  _M0L7_2abindS1023 = 0;
  _M0L1iS1024 = _M0L7_2abindS1023;
  while (1) {
    if (_M0L1iS1024 < _M0L1nS1021) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2431 = _M0L1pS1022->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2452 = _M0L1pS1022->$9;
      float _M0L6_2atmpS2447;
      struct _M0TPB5ArrayGfE* _M0L1vS2451;
      float _M0L6_2atmpS2449;
      float _M0L4e__eS2450;
      float _M0L6_2atmpS2448;
      float _M0L6_2atmpS2444;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2446;
      float _M0L6_2atmpS2445;
      float _M0L6_2atmpS2433;
      struct _M0TPB5ArrayGfE* _M0L2giS2443;
      float _M0L6_2atmpS2438;
      struct _M0TPB5ArrayGfE* _M0L1vS2442;
      float _M0L6_2atmpS2440;
      float _M0L4e__iS2441;
      float _M0L6_2atmpS2439;
      float _M0L6_2atmpS2435;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2437;
      float _M0L6_2atmpS2436;
      float _M0L6_2atmpS2434;
      float _M0L6_2atmpS2432;
      int32_t _M0L6_2atmpS2453;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2447 = _M0MPC15array5Array2atGfE(_M0L2geS2452, _M0L1iS1024);
      _M0L1vS2451 = _M0L1pS1022->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2449 = _M0MPC15array5Array2atGfE(_M0L1vS2451, _M0L1iS1024);
      _M0L4e__eS2450 = _M0L1pS1022->$17;
      _M0L6_2atmpS2448 = _M0L6_2atmpS2449 - _M0L4e__eS2450;
      _M0L6_2atmpS2444 = _M0L6_2atmpS2447 * _M0L6_2atmpS2448;
      _M0L7gsyn__eS2446 = _M0L1pS1022->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2445
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2446, _M0L1iS1024);
      _M0L6_2atmpS2433 = _M0L6_2atmpS2444 * _M0L6_2atmpS2445;
      _M0L2giS2443 = _M0L1pS1022->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2438 = _M0MPC15array5Array2atGfE(_M0L2giS2443, _M0L1iS1024);
      _M0L1vS2442 = _M0L1pS1022->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2440 = _M0MPC15array5Array2atGfE(_M0L1vS2442, _M0L1iS1024);
      _M0L4e__iS2441 = _M0L1pS1022->$18;
      _M0L6_2atmpS2439 = _M0L6_2atmpS2440 - _M0L4e__iS2441;
      _M0L6_2atmpS2435 = _M0L6_2atmpS2438 * _M0L6_2atmpS2439;
      _M0L7gsyn__iS2437 = _M0L1pS1022->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2436
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2437, _M0L1iS1024);
      _M0L6_2atmpS2434 = _M0L6_2atmpS2435 * _M0L6_2atmpS2436;
      _M0L6_2atmpS2432 = _M0L6_2atmpS2433 + _M0L6_2atmpS2434;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2431, _M0L1iS1024, _M0L6_2atmpS2432);
      _M0L6_2atmpS2453 = _M0L1iS1024 + 1;
      _M0L1iS1024 = _M0L6_2atmpS2453;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1013,
  float _M0L2dtS1016
) {
  int32_t _M0L1nS1012;
  int32_t _M0L7_2abindS1014;
  int32_t _M0L1iS1015;
  int32_t _M0L7_2abindS1018;
  int32_t _M0L1iS1019;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1012 = _M0L1pS1013->$2;
  _M0L7_2abindS1014 = 0;
  _M0L1iS1015 = _M0L7_2abindS1014;
  while (1) {
    if (_M0L1iS1015 < _M0L1nS1012) {
      struct _M0TPB5ArrayGfE* _M0L2heS2369 = _M0L1pS1013->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2374 = _M0L1pS1013->$11;
      float _M0L6_2atmpS2371;
      struct _M0TPB5ArrayGfE* _M0L3gluS2373;
      float _M0L6_2atmpS2372;
      float _M0L6_2atmpS2370;
      struct _M0TPB5ArrayGfE* _M0L2hiS2375;
      struct _M0TPB5ArrayGfE* _M0L2hiS2380;
      float _M0L6_2atmpS2377;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2379;
      float _M0L6_2atmpS2378;
      float _M0L6_2atmpS2376;
      struct _M0TPB5ArrayGfE* _M0L2geS2381;
      struct _M0TPB5ArrayGfE* _M0L2geS2393;
      float _M0L6_2atmpS2383;
      struct _M0TPB5ArrayGfE* _M0L2geS2392;
      float _M0L6_2atmpS2391;
      float _M0L6_2atmpS2389;
      float _M0L3tdeS2390;
      float _M0L6_2atmpS2386;
      struct _M0TPB5ArrayGfE* _M0L2heS2388;
      float _M0L6_2atmpS2387;
      float _M0L6_2atmpS2385;
      float _M0L6_2atmpS2384;
      float _M0L6_2atmpS2382;
      struct _M0TPB5ArrayGfE* _M0L2heS2394;
      struct _M0TPB5ArrayGfE* _M0L2heS2403;
      float _M0L6_2atmpS2396;
      struct _M0TPB5ArrayGfE* _M0L2heS2402;
      float _M0L6_2atmpS2401;
      float _M0L6_2atmpS2399;
      float _M0L3treS2400;
      float _M0L6_2atmpS2398;
      float _M0L6_2atmpS2397;
      float _M0L6_2atmpS2395;
      struct _M0TPB5ArrayGfE* _M0L2giS2404;
      struct _M0TPB5ArrayGfE* _M0L2giS2416;
      float _M0L6_2atmpS2406;
      struct _M0TPB5ArrayGfE* _M0L2giS2415;
      float _M0L6_2atmpS2414;
      float _M0L6_2atmpS2412;
      float _M0L3tdiS2413;
      float _M0L6_2atmpS2409;
      struct _M0TPB5ArrayGfE* _M0L2hiS2411;
      float _M0L6_2atmpS2410;
      float _M0L6_2atmpS2408;
      float _M0L6_2atmpS2407;
      float _M0L6_2atmpS2405;
      struct _M0TPB5ArrayGfE* _M0L2hiS2417;
      struct _M0TPB5ArrayGfE* _M0L2hiS2426;
      float _M0L6_2atmpS2419;
      struct _M0TPB5ArrayGfE* _M0L2hiS2425;
      float _M0L6_2atmpS2424;
      float _M0L6_2atmpS2422;
      float _M0L3triS2423;
      float _M0L6_2atmpS2421;
      float _M0L6_2atmpS2420;
      float _M0L6_2atmpS2418;
      int32_t _M0L6_2atmpS2427;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2371 = _M0MPC15array5Array2atGfE(_M0L2heS2374, _M0L1iS1015);
      _M0L3gluS2373 = _M0L1pS1013->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2372
      = _M0MPC15array5Array2atGfE(_M0L3gluS2373, _M0L1iS1015);
      _M0L6_2atmpS2370 = _M0L6_2atmpS2371 + _M0L6_2atmpS2372;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2369, _M0L1iS1015, _M0L6_2atmpS2370);
      _M0L2hiS2375 = _M0L1pS1013->$12;
      _M0L2hiS2380 = _M0L1pS1013->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2377 = _M0MPC15array5Array2atGfE(_M0L2hiS2380, _M0L1iS1015);
      _M0L4gabaS2379 = _M0L1pS1013->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2378
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2379, _M0L1iS1015);
      _M0L6_2atmpS2376 = _M0L6_2atmpS2377 + _M0L6_2atmpS2378;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2375, _M0L1iS1015, _M0L6_2atmpS2376);
      _M0L2geS2381 = _M0L1pS1013->$9;
      _M0L2geS2393 = _M0L1pS1013->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2383 = _M0MPC15array5Array2atGfE(_M0L2geS2393, _M0L1iS1015);
      _M0L2geS2392 = _M0L1pS1013->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2391 = _M0MPC15array5Array2atGfE(_M0L2geS2392, _M0L1iS1015);
      _M0L6_2atmpS2389 = -_M0L6_2atmpS2391;
      _M0L3tdeS2390 = _M0L1pS1013->$20;
      _M0L6_2atmpS2386 = _M0L6_2atmpS2389 / _M0L3tdeS2390;
      _M0L2heS2388 = _M0L1pS1013->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2387 = _M0MPC15array5Array2atGfE(_M0L2heS2388, _M0L1iS1015);
      _M0L6_2atmpS2385 = _M0L6_2atmpS2386 + _M0L6_2atmpS2387;
      _M0L6_2atmpS2384 = _M0L2dtS1016 * _M0L6_2atmpS2385;
      _M0L6_2atmpS2382 = _M0L6_2atmpS2383 + _M0L6_2atmpS2384;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2381, _M0L1iS1015, _M0L6_2atmpS2382);
      _M0L2heS2394 = _M0L1pS1013->$11;
      _M0L2heS2403 = _M0L1pS1013->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2396 = _M0MPC15array5Array2atGfE(_M0L2heS2403, _M0L1iS1015);
      _M0L2heS2402 = _M0L1pS1013->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2401 = _M0MPC15array5Array2atGfE(_M0L2heS2402, _M0L1iS1015);
      _M0L6_2atmpS2399 = -_M0L6_2atmpS2401;
      _M0L3treS2400 = _M0L1pS1013->$19;
      _M0L6_2atmpS2398 = _M0L6_2atmpS2399 / _M0L3treS2400;
      _M0L6_2atmpS2397 = _M0L2dtS1016 * _M0L6_2atmpS2398;
      _M0L6_2atmpS2395 = _M0L6_2atmpS2396 + _M0L6_2atmpS2397;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2394, _M0L1iS1015, _M0L6_2atmpS2395);
      _M0L2giS2404 = _M0L1pS1013->$10;
      _M0L2giS2416 = _M0L1pS1013->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2406 = _M0MPC15array5Array2atGfE(_M0L2giS2416, _M0L1iS1015);
      _M0L2giS2415 = _M0L1pS1013->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2414 = _M0MPC15array5Array2atGfE(_M0L2giS2415, _M0L1iS1015);
      _M0L6_2atmpS2412 = -_M0L6_2atmpS2414;
      _M0L3tdiS2413 = _M0L1pS1013->$22;
      _M0L6_2atmpS2409 = _M0L6_2atmpS2412 / _M0L3tdiS2413;
      _M0L2hiS2411 = _M0L1pS1013->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2410 = _M0MPC15array5Array2atGfE(_M0L2hiS2411, _M0L1iS1015);
      _M0L6_2atmpS2408 = _M0L6_2atmpS2409 + _M0L6_2atmpS2410;
      _M0L6_2atmpS2407 = _M0L2dtS1016 * _M0L6_2atmpS2408;
      _M0L6_2atmpS2405 = _M0L6_2atmpS2406 + _M0L6_2atmpS2407;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2404, _M0L1iS1015, _M0L6_2atmpS2405);
      _M0L2hiS2417 = _M0L1pS1013->$12;
      _M0L2hiS2426 = _M0L1pS1013->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2419 = _M0MPC15array5Array2atGfE(_M0L2hiS2426, _M0L1iS1015);
      _M0L2hiS2425 = _M0L1pS1013->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2424 = _M0MPC15array5Array2atGfE(_M0L2hiS2425, _M0L1iS1015);
      _M0L6_2atmpS2422 = -_M0L6_2atmpS2424;
      _M0L3triS2423 = _M0L1pS1013->$21;
      _M0L6_2atmpS2421 = _M0L6_2atmpS2422 / _M0L3triS2423;
      _M0L6_2atmpS2420 = _M0L2dtS1016 * _M0L6_2atmpS2421;
      _M0L6_2atmpS2418 = _M0L6_2atmpS2419 + _M0L6_2atmpS2420;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2417, _M0L1iS1015, _M0L6_2atmpS2418);
      _M0L6_2atmpS2427 = _M0L1iS1015 + 1;
      _M0L1iS1015 = _M0L6_2atmpS2427;
      continue;
    }
    break;
  }
  _M0L7_2abindS1018 = 0;
  _M0L1iS1019 = _M0L7_2abindS1018;
  while (1) {
    if (_M0L1iS1019 < _M0L1nS1012) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2428 = _M0L1pS1013->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2429;
      int32_t _M0L6_2atmpS2430;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2428, _M0L1iS1019, 0x0p+0f);
      _M0L4gabaS2429 = _M0L1pS1013->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2429, _M0L1iS1019, 0x0p+0f);
      _M0L6_2atmpS2430 = _M0L1iS1019 + 1;
      _M0L1iS1019 = _M0L6_2atmpS2430;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS998,
  float _M0L2dtS1007
) {
  int32_t _M0L1nS997;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S999;
  float _M0L2tmS1000;
  float _M0L2elS1001;
  float _M0L1rS1002;
  float _M0L2vtS1003;
  float _M0L2vrS1004;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2368;
  float _M0L11tabs__constS1005;
  float _M0L6_2atmpS2367;
  int32_t _M0L11tabs__stepsS1006;
  int32_t _M0L7_2abindS1008;
  int32_t _M0L1iS1009;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS997 = _M0L1pS998->$2;
  _M0L3p__S999 = _M0L1pS998->$0;
  _M0L2tmS1000 = _M0L3p__S999->$2;
  _M0L2elS1001 = _M0L3p__S999->$5;
  _M0L1rS1002 = _M0L3p__S999->$6;
  _M0L2vtS1003 = _M0L3p__S999->$3;
  _M0L2vrS1004 = _M0L3p__S999->$4;
  _M0L5spikeS2368 = _M0L1pS998->$1;
  _M0L11tabs__constS1005 = _M0L5spikeS2368->$0;
  _M0L6_2atmpS2367 = _M0L11tabs__constS1005 / _M0L2dtS1007;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1006 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2367);
  _M0L7_2abindS1008 = 0;
  _M0L1iS1009 = _M0L7_2abindS1008;
  while (1) {
    if (_M0L1iS1009 < _M0L1nS997) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2327 = _M0L1pS998->$6;
      int32_t _M0L6_2atmpS2326;
      struct _M0TPB5ArrayGfE* _M0L1vS2333;
      struct _M0TPB5ArrayGfE* _M0L1vS2354;
      float _M0L6_2atmpS2335;
      float _M0L6_2atmpS2337;
      struct _M0TPB5ArrayGfE* _M0L1vS2353;
      float _M0L6_2atmpS2352;
      float _M0L6_2atmpS2351;
      float _M0L6_2atmpS2343;
      struct _M0TPB5ArrayGfE* _M0L1wS2350;
      float _M0L6_2atmpS2349;
      float _M0L6_2atmpS2346;
      struct _M0TPB5ArrayGfE* _M0L1iS2348;
      float _M0L6_2atmpS2347;
      float _M0L6_2atmpS2345;
      float _M0L6_2atmpS2344;
      float _M0L6_2atmpS2339;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2342;
      float _M0L6_2atmpS2341;
      float _M0L6_2atmpS2340;
      float _M0L6_2atmpS2338;
      float _M0L6_2atmpS2336;
      float _M0L6_2atmpS2334;
      struct _M0TPB5ArrayGbE* _M0L4fireS2355;
      struct _M0TPB5ArrayGfE* _M0L1vS2358;
      float _M0L6_2atmpS2357;
      int32_t _M0L6_2atmpS2356;
      struct _M0TPB5ArrayGfE* _M0L1vS2359;
      struct _M0TPB5ArrayGbE* _M0L4fireS2361;
      float _M0L6_2atmpS2360;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2363;
      struct _M0TPB5ArrayGbE* _M0L4fireS2365;
      int32_t _M0L6_2atmpS2364;
      int32_t _M0L6_2atmpS2325;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2326
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2327, _M0L1iS1009);
      if (_M0L6_2atmpS2326 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2328 = _M0L1pS998->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2329;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2332;
        int32_t _M0L6_2atmpS2331;
        int32_t _M0L6_2atmpS2330;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2328, _M0L1iS1009, 0);
        _M0L4tabsS2329 = _M0L1pS998->$6;
        _M0L4tabsS2332 = _M0L1pS998->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2331
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2332, _M0L1iS1009);
        _M0L6_2atmpS2330 = _M0L6_2atmpS2331 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2329, _M0L1iS1009, _M0L6_2atmpS2330);
        goto join_1010;
      }
      _M0L1vS2333 = _M0L1pS998->$3;
      _M0L1vS2354 = _M0L1pS998->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2335 = _M0MPC15array5Array2atGfE(_M0L1vS2354, _M0L1iS1009);
      _M0L6_2atmpS2337 = _M0L2dtS1007 / _M0L2tmS1000;
      _M0L1vS2353 = _M0L1pS998->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2352 = _M0MPC15array5Array2atGfE(_M0L1vS2353, _M0L1iS1009);
      _M0L6_2atmpS2351 = _M0L6_2atmpS2352 - _M0L2elS1001;
      _M0L6_2atmpS2343 = -_M0L6_2atmpS2351;
      _M0L1wS2350 = _M0L1pS998->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2349 = _M0MPC15array5Array2atGfE(_M0L1wS2350, _M0L1iS1009);
      _M0L6_2atmpS2346 = -_M0L6_2atmpS2349;
      _M0L1iS2348 = _M0L1pS998->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2347 = _M0MPC15array5Array2atGfE(_M0L1iS2348, _M0L1iS1009);
      _M0L6_2atmpS2345 = _M0L6_2atmpS2346 + _M0L6_2atmpS2347;
      _M0L6_2atmpS2344 = _M0L1rS1002 * _M0L6_2atmpS2345;
      _M0L6_2atmpS2339 = _M0L6_2atmpS2343 + _M0L6_2atmpS2344;
      _M0L9syn__currS2342 = _M0L1pS998->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2341
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2342, _M0L1iS1009);
      _M0L6_2atmpS2340 = _M0L1rS1002 * _M0L6_2atmpS2341;
      _M0L6_2atmpS2338 = _M0L6_2atmpS2339 - _M0L6_2atmpS2340;
      _M0L6_2atmpS2336 = _M0L6_2atmpS2337 * _M0L6_2atmpS2338;
      _M0L6_2atmpS2334 = _M0L6_2atmpS2335 + _M0L6_2atmpS2336;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2333, _M0L1iS1009, _M0L6_2atmpS2334);
      _M0L4fireS2355 = _M0L1pS998->$5;
      _M0L1vS2358 = _M0L1pS998->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2357 = _M0MPC15array5Array2atGfE(_M0L1vS2358, _M0L1iS1009);
      _M0L6_2atmpS2356 = _M0L6_2atmpS2357 > _M0L2vtS1003;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2355, _M0L1iS1009, _M0L6_2atmpS2356);
      _M0L1vS2359 = _M0L1pS998->$3;
      _M0L4fireS2361 = _M0L1pS998->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2361, _M0L1iS1009)) {
        _M0L6_2atmpS2360 = _M0L2vrS1004;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2362 = _M0L1pS998->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2360
        = _M0MPC15array5Array2atGfE(_M0L1vS2362, _M0L1iS1009);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2359, _M0L1iS1009, _M0L6_2atmpS2360);
      _M0L4tabsS2363 = _M0L1pS998->$6;
      _M0L4fireS2365 = _M0L1pS998->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2365, _M0L1iS1009)) {
        _M0L6_2atmpS2364 = _M0L11tabs__stepsS1006;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2366 = _M0L1pS998->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2364
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2366, _M0L1iS1009);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2363, _M0L1iS1009, _M0L6_2atmpS2364);
      goto join_1010;
      goto joinlet_2773;
      join_1010:;
      _M0L6_2atmpS2325 = _M0L1iS1009 + 1;
      _M0L1iS1009 = _M0L6_2atmpS2325;
      continue;
      joinlet_2773:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS984,
  int32_t _M0L1iS983,
  int32_t _M0L1jS989,
  float _M0L1vS990
) {
  int32_t _if__result_2774;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2324;
  int32_t _M0L5startS985;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2322;
  int32_t _M0L6_2atmpS2323;
  int32_t _M0L3endS986;
  struct _M0TPB8MutLocalGbE* _M0L5foundS987;
  int32_t _M0L1kS988;
  int32_t _M0L3valS2314;
  #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  if (_M0L1iS983 < 0) {
    _if__result_2774 = 1;
  } else {
    int32_t _M0L4rowsS2309 = _M0L1mS984->$0;
    _if__result_2774 = _M0L1iS983 >= _M0L4rowsS2309;
  }
  if (_if__result_2774) {
    return 0;
  }
  _M0L6rowptrS2324 = _M0L1mS984->$2;
  #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5startS985 = _M0MPC15array5Array2atGiE(_M0L6rowptrS2324, _M0L1iS983);
  _M0L6rowptrS2322 = _M0L1mS984->$2;
  _M0L6_2atmpS2323 = _M0L1iS983 + 1;
  #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L3endS986
  = _M0MPC15array5Array2atGiE(_M0L6rowptrS2322, _M0L6_2atmpS2323);
  _M0L5foundS987
  = (struct _M0TPB8MutLocalGbE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGbE));
  Moonbit_object_header(_M0L5foundS987)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5foundS987->$0 = 0;
  _M0L1kS988 = _M0L5startS985;
  while (1) {
    if (_M0L1kS988 < _M0L3endS986) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS2311 = _M0L1mS984->$3;
      int32_t _M0L6_2atmpS2310;
      int32_t _M0L6_2atmpS2313;
      #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2310
      = _M0MPC15array5Array2atGiE(_M0L6colptrS2311, _M0L1kS988);
      if (_M0L6_2atmpS2310 == _M0L1jS989) {
        struct _M0TPB5ArrayGfE* _M0L4valsS2312 = _M0L1mS984->$4;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGfE(_M0L4valsS2312, _M0L1kS988, _M0L1vS990);
        _M0L5foundS987->$0 = 1;
        break;
      }
      _M0L6_2atmpS2313 = _M0L1kS988 + 1;
      _M0L1kS988 = _M0L6_2atmpS2313;
      continue;
    }
    break;
  }
  _M0L3valS2314 = _M0L5foundS987->$0;
  moonbit_decref_cycle_free(_M0L5foundS987);
  if (!_M0L3valS2314) {
    struct _M0TPB5ArrayGiE* _M0L6colptrS2315 = _M0L1mS984->$3;
    struct _M0TPB5ArrayGfE* _M0L4valsS2316;
    int32_t _M0L7n__rowsS992;
    int32_t _M0L7_2abindS993;
    int32_t _M0L7_2abindS994;
    int32_t _M0L1rS995;
    #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGiE(_M0L6colptrS2315, _M0L3endS986, _M0L1jS989);
    _M0L4valsS2316 = _M0L1mS984->$4;
    #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGfE(_M0L4valsS2316, _M0L3endS986, _M0L1vS990);
    _M0L7n__rowsS992 = _M0L1mS984->$0;
    _M0L7_2abindS993 = _M0L1iS983 + 1;
    _M0L7_2abindS994 = _M0L7n__rowsS992 + 1;
    _M0L1rS995 = _M0L7_2abindS993;
    while (1) {
      if (_M0L1rS995 < _M0L7_2abindS994) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2317 = _M0L1mS984->$2;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2320 = _M0L1mS984->$2;
        int32_t _M0L6_2atmpS2319;
        int32_t _M0L6_2atmpS2318;
        int32_t _M0L6_2atmpS2321;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L6_2atmpS2319
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2320, _M0L1rS995);
        _M0L6_2atmpS2318 = _M0L6_2atmpS2319 + 1;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGiE(_M0L6rowptrS2317, _M0L1rS995, _M0L6_2atmpS2318);
        _M0L6_2atmpS2321 = _M0L1rS995 + 1;
        _M0L1rS995 = _M0L6_2atmpS2321;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS977,
  int32_t _M0L4colsS978,
  float _M0L2muS979,
  float _M0L5sigmaS980,
  float _M0L1pS981,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS982
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS977, _M0L4colsS978, _M0L2muS979, _M0L5sigmaS980, _M0L1pS981, 0, _M0L3rngS982);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS891,
  int32_t _M0L4colsS895,
  float _M0L2muS901,
  float _M0L5sigmaS902,
  float _M0L1pS914,
  int32_t _M0L4ruleS908,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS904
) {
  float* _M0L6_2atmpS2308;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2307;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS890;
  int32_t _M0L7_2abindS892;
  int32_t _M0L1iS893;
  int32_t _M0L6_2atmpS2306;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS967;
  int32_t* _M0L6_2atmpS2305;
  struct _M0TPB5ArrayGiE* _M0L6colptrS968;
  float* _M0L6_2atmpS2304;
  struct _M0TPB5ArrayGfE* _M0L4valsS969;
  int32_t _M0L7_2abindS970;
  int32_t _M0L1iS971;
  int32_t _M0L6_2atmpS2303;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2796;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2308 = moonbit_empty_float_array;
  _M0L6_2atmpS2307
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2307)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _M0L6_2atmpS2307->$0 = _M0L6_2atmpS2308;
  _M0L6_2atmpS2307->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS890
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS891, _M0L6_2atmpS2307);
  _M0L7_2abindS892 = 0;
  _M0L1iS893 = _M0L7_2abindS892;
  while (1) {
    if (_M0L1iS893 < _M0L4rowsS891) {
      struct _M0TPB5ArrayGfE* _M0L3rowS894;
      int32_t _M0L7_2abindS896;
      int32_t _M0L1jS897;
      int32_t _M0L6_2atmpS2259;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS894 = _M0MPC15array5Array4makeGfE(_M0L4colsS895, 0x0p+0f);
      _M0L7_2abindS896 = 0;
      _M0L1jS897 = _M0L7_2abindS896;
      while (1) {
        if (_M0L1jS897 < _M0L4colsS895) {
          double _M0L2z1S899;
          struct _M0TUddE* _M0L7_2abindS903;
          double _M0L5_2az1S905;
          float _M0L6_2atmpS2257;
          float _M0L6_2atmpS2256;
          float _M0L1wS900;
          int32_t _M0L6_2atmpS2258;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS903
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS904);
          _M0L5_2az1S905 = _M0L7_2abindS903->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS903);
          _M0L2z1S899 = _M0L5_2az1S905;
          goto join_898;
          goto joinlet_2779;
          join_898:;
          _M0L6_2atmpS2257 = (float)_M0L2z1S899;
          _M0L6_2atmpS2256 = _M0L5sigmaS902 * _M0L6_2atmpS2257;
          _M0L1wS900 = _M0L2muS901 + _M0L6_2atmpS2256;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS894, _M0L1jS897, _M0L1wS900);
          joinlet_2779:;
          _M0L6_2atmpS2258 = _M0L1jS897 + 1;
          _M0L1jS897 = _M0L6_2atmpS2258;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS890, _M0L1iS893, _M0L3rowS894);
      _M0L6_2atmpS2259 = _M0L1iS893 + 1;
      _M0L1iS893 = _M0L6_2atmpS2259;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS908) {
    case 0: {
      int32_t _M0L7_2abindS909 = 0;
      int32_t _M0L1iS910 = _M0L7_2abindS909;
      while (1) {
        if (_M0L1iS910 < _M0L4rowsS891) {
          int32_t _M0L7_2abindS911 = 0;
          int32_t _M0L1jS912 = _M0L7_2abindS911;
          int32_t _M0L6_2atmpS2262;
          while (1) {
            if (_M0L1jS912 < _M0L4colsS895) {
              float _M0L1uS913;
              int32_t _M0L6_2atmpS2261;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS913 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS904);
              if (_M0L1uS913 >= _M0L1pS914) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2260;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2260
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS890, _M0L1iS910);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2260, _M0L1jS912, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2260);
              }
              _M0L6_2atmpS2261 = _M0L1jS912 + 1;
              _M0L1jS912 = _M0L6_2atmpS2261;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2262 = _M0L1iS910 + 1;
          _M0L1iS910 = _M0L6_2atmpS2262;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2280 = (float)_M0L4rowsS891;
      float _M0L6_2atmpS2279 = _M0L6_2atmpS2280 * _M0L1pS914;
      int32_t _M0L7n__keepS917;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS917 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2279);
      if (_M0L7n__keepS917 > 0 && _M0L7n__keepS917 <= _M0L4rowsS891) {
        int32_t _M0L7_2abindS918 = 0;
        int32_t _M0L1jS919 = _M0L7_2abindS918;
        while (1) {
          if (_M0L1jS919 < _M0L4colsS895) {
            int32_t* _M0L6_2atmpS2274 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS920 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS921;
            int32_t _M0L1kS922;
            int32_t _M0L7n__dropS924;
            int32_t _M0L7_2abindS925;
            int32_t _M0L1kS926;
            int32_t _M0L7_2abindS932;
            int32_t _M0L1kS933;
            int32_t _M0L6_2atmpS2275;
            Moonbit_object_header(_M0L8pre__idxS920)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
            _M0L8pre__idxS920->$0 = _M0L6_2atmpS2274;
            _M0L8pre__idxS920->$1 = 0;
            _M0L7_2abindS921 = 0;
            _M0L1kS922 = _M0L7_2abindS921;
            while (1) {
              if (_M0L1kS922 < _M0L4rowsS891) {
                int32_t _M0L6_2atmpS2263;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS920, _M0L1kS922);
                _M0L6_2atmpS2263 = _M0L1kS922 + 1;
                _M0L1kS922 = _M0L6_2atmpS2263;
                continue;
              }
              break;
            }
            _M0L7n__dropS924 = _M0L4rowsS891 - _M0L7n__keepS917;
            _M0L7_2abindS925 = 0;
            _M0L1kS926 = _M0L7_2abindS925;
            while (1) {
              if (_M0L1kS926 < _M0L7n__dropS924) {
                float _M0L1uS927;
                float _M0L6_2atmpS2267;
                float _M0L6_2atmpS2269;
                float _M0L6_2atmpS2268;
                float _M0L6_2atmpS2266;
                int32_t _M0L6_2atmpS2265;
                int32_t _M0L6r__idxS928;
                int32_t _M0L10r__clampedS929;
                int32_t _M0L3tmpS930;
                int32_t _M0L6_2atmpS2264;
                int32_t _M0L6_2atmpS2270;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS927 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS904);
                _M0L6_2atmpS2267 = (float)_M0L4rowsS891;
                _M0L6_2atmpS2269 = (float)_M0L1kS926;
                _M0L6_2atmpS2268 = _M0L6_2atmpS2269 * _M0L1uS927;
                _M0L6_2atmpS2266 = _M0L6_2atmpS2267 - _M0L6_2atmpS2268;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2265
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2266);
                _M0L6r__idxS928 = _M0L1kS926 + _M0L6_2atmpS2265;
                if (_M0L6r__idxS928 >= _M0L4rowsS891) {
                  _M0L10r__clampedS929 = _M0L4rowsS891 - 1;
                } else {
                  _M0L10r__clampedS929 = _M0L6r__idxS928;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS930
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS920, _M0L1kS926);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2264
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS920, _M0L10r__clampedS929);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS920, _M0L1kS926, _M0L6_2atmpS2264);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS920, _M0L10r__clampedS929, _M0L3tmpS930);
                _M0L6_2atmpS2270 = _M0L1kS926 + 1;
                _M0L1kS926 = _M0L6_2atmpS2270;
                continue;
              }
              break;
            }
            _M0L7_2abindS932 = 0;
            _M0L1kS933 = _M0L7_2abindS932;
            while (1) {
              if (_M0L1kS933 < _M0L7n__dropS924) {
                int32_t _M0L6_2atmpS2272;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2271;
                int32_t _M0L6_2atmpS2273;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2272
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS920, _M0L1kS933);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2271
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS890, _M0L6_2atmpS2272);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2271, _M0L1jS919, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2271);
                _M0L6_2atmpS2273 = _M0L1kS933 + 1;
                _M0L1kS933 = _M0L6_2atmpS2273;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS920);
              }
              break;
            }
            _M0L6_2atmpS2275 = _M0L1jS919 + 1;
            _M0L1jS919 = _M0L6_2atmpS2275;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS917 == 0) {
        int32_t _M0L7_2abindS936 = 0;
        int32_t _M0L1iS937 = _M0L7_2abindS936;
        while (1) {
          if (_M0L1iS937 < _M0L4rowsS891) {
            int32_t _M0L7_2abindS938 = 0;
            int32_t _M0L1jS939 = _M0L7_2abindS938;
            int32_t _M0L6_2atmpS2278;
            while (1) {
              if (_M0L1jS939 < _M0L4colsS895) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2276;
                int32_t _M0L6_2atmpS2277;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2276
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS890, _M0L1iS937);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2276, _M0L1jS939, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2276);
                _M0L6_2atmpS2277 = _M0L1jS939 + 1;
                _M0L1jS939 = _M0L6_2atmpS2277;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2278 = _M0L1iS937 + 1;
            _M0L1iS937 = _M0L6_2atmpS2278;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2298 = (float)_M0L4colsS895;
      float _M0L6_2atmpS2297 = _M0L6_2atmpS2298 * _M0L1pS914;
      int32_t _M0L7n__keepS942;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS942 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2297);
      if (_M0L7n__keepS942 > 0 && _M0L7n__keepS942 <= _M0L4colsS895) {
        int32_t _M0L7_2abindS943 = 0;
        int32_t _M0L1iS944 = _M0L7_2abindS943;
        while (1) {
          if (_M0L1iS944 < _M0L4rowsS891) {
            int32_t* _M0L6_2atmpS2292 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS945 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS946;
            int32_t _M0L1kS947;
            int32_t _M0L7n__dropS949;
            int32_t _M0L7_2abindS950;
            int32_t _M0L1kS951;
            int32_t _M0L7_2abindS957;
            int32_t _M0L1kS958;
            int32_t _M0L6_2atmpS2293;
            Moonbit_object_header(_M0L9post__idxS945)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
            _M0L9post__idxS945->$0 = _M0L6_2atmpS2292;
            _M0L9post__idxS945->$1 = 0;
            _M0L7_2abindS946 = 0;
            _M0L1kS947 = _M0L7_2abindS946;
            while (1) {
              if (_M0L1kS947 < _M0L4colsS895) {
                int32_t _M0L6_2atmpS2281;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS945, _M0L1kS947);
                _M0L6_2atmpS2281 = _M0L1kS947 + 1;
                _M0L1kS947 = _M0L6_2atmpS2281;
                continue;
              }
              break;
            }
            _M0L7n__dropS949 = _M0L4colsS895 - _M0L7n__keepS942;
            _M0L7_2abindS950 = 0;
            _M0L1kS951 = _M0L7_2abindS950;
            while (1) {
              if (_M0L1kS951 < _M0L7n__dropS949) {
                float _M0L1uS952;
                float _M0L6_2atmpS2285;
                float _M0L6_2atmpS2287;
                float _M0L6_2atmpS2286;
                float _M0L6_2atmpS2284;
                int32_t _M0L6_2atmpS2283;
                int32_t _M0L6r__idxS953;
                int32_t _M0L10r__clampedS954;
                int32_t _M0L3tmpS955;
                int32_t _M0L6_2atmpS2282;
                int32_t _M0L6_2atmpS2288;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS952 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS904);
                _M0L6_2atmpS2285 = (float)_M0L4colsS895;
                _M0L6_2atmpS2287 = (float)_M0L1kS951;
                _M0L6_2atmpS2286 = _M0L6_2atmpS2287 * _M0L1uS952;
                _M0L6_2atmpS2284 = _M0L6_2atmpS2285 - _M0L6_2atmpS2286;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2283
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2284);
                _M0L6r__idxS953 = _M0L1kS951 + _M0L6_2atmpS2283;
                if (_M0L6r__idxS953 >= _M0L4colsS895) {
                  _M0L10r__clampedS954 = _M0L4colsS895 - 1;
                } else {
                  _M0L10r__clampedS954 = _M0L6r__idxS953;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS955
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS945, _M0L1kS951);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2282
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS945, _M0L10r__clampedS954);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS945, _M0L1kS951, _M0L6_2atmpS2282);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS945, _M0L10r__clampedS954, _M0L3tmpS955);
                _M0L6_2atmpS2288 = _M0L1kS951 + 1;
                _M0L1kS951 = _M0L6_2atmpS2288;
                continue;
              }
              break;
            }
            _M0L7_2abindS957 = 0;
            _M0L1kS958 = _M0L7_2abindS957;
            while (1) {
              if (_M0L1kS958 < _M0L7n__dropS949) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2289;
                int32_t _M0L6_2atmpS2290;
                int32_t _M0L6_2atmpS2291;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2289
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS890, _M0L1iS944);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2290
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS945, _M0L1kS958);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2289, _M0L6_2atmpS2290, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2289);
                _M0L6_2atmpS2291 = _M0L1kS958 + 1;
                _M0L1kS958 = _M0L6_2atmpS2291;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS945);
              }
              break;
            }
            _M0L6_2atmpS2293 = _M0L1iS944 + 1;
            _M0L1iS944 = _M0L6_2atmpS2293;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS942 == 0) {
        int32_t _M0L7_2abindS961 = 0;
        int32_t _M0L1iS962 = _M0L7_2abindS961;
        while (1) {
          if (_M0L1iS962 < _M0L4rowsS891) {
            int32_t _M0L7_2abindS963 = 0;
            int32_t _M0L1jS964 = _M0L7_2abindS963;
            int32_t _M0L6_2atmpS2296;
            while (1) {
              if (_M0L1jS964 < _M0L4colsS895) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2294;
                int32_t _M0L6_2atmpS2295;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2294
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS890, _M0L1iS962);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2294, _M0L1jS964, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2294);
                _M0L6_2atmpS2295 = _M0L1jS964 + 1;
                _M0L1jS964 = _M0L6_2atmpS2295;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2296 = _M0L1iS962 + 1;
            _M0L1iS962 = _M0L6_2atmpS2296;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2306 = _M0L4rowsS891 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS967 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2306, 0);
  _M0L6_2atmpS2305 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS968
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS968)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
  _M0L6colptrS968->$0 = _M0L6_2atmpS2305;
  _M0L6colptrS968->$1 = 0;
  _M0L6_2atmpS2304 = moonbit_empty_float_array;
  _M0L4valsS969
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS969)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _M0L4valsS969->$0 = _M0L6_2atmpS2304;
  _M0L4valsS969->$1 = 0;
  _M0L7_2abindS970 = 0;
  _M0L1iS971 = _M0L7_2abindS970;
  while (1) {
    if (_M0L1iS971 < _M0L4rowsS891) {
      int32_t _M0L6_2atmpS2299;
      int32_t _M0L7_2abindS972;
      int32_t _M0L1jS973;
      int32_t _M0L6_2atmpS2302;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2299 = _M0MPC15array5Array6lengthGfE(_M0L4valsS969);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS967, _M0L1iS971, _M0L6_2atmpS2299);
      _M0L7_2abindS972 = 0;
      _M0L1jS973 = _M0L7_2abindS972;
      while (1) {
        if (_M0L1jS973 < _M0L4colsS895) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2300;
          float _M0L1vS974;
          int32_t _M0L6_2atmpS2301;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2300
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS890, _M0L1iS971);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS974
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2300, _M0L1jS973);
          moonbit_decref_cycle_free(_M0L6_2atmpS2300);
          if (_M0L1vS974 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS968, _M0L1jS973);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS969, _M0L1vS974);
          }
          _M0L6_2atmpS2301 = _M0L1jS973 + 1;
          _M0L1jS973 = _M0L6_2atmpS2301;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2302 = _M0L1iS971 + 1;
      _M0L1iS971 = _M0L6_2atmpS2302;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS890);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2303 = _M0MPC15array5Array6lengthGfE(_M0L4valsS969);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS967, _M0L4rowsS891, _M0L6_2atmpS2303);
  _block_2796
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2796)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 65, 0);
  _block_2796->$0 = _M0L4rowsS891;
  _block_2796->$1 = _M0L4colsS895;
  _block_2796->$2 = _M0L6rowptrS967;
  _block_2796->$3 = _M0L6colptrS968;
  _block_2796->$4 = _M0L4valsS969;
  return _block_2796;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS889
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS2255;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS2255 = _M0L1mS889->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS2255);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS887
) {
  struct _M0TUmmmmE* _M0L1sS886;
  uint64_t _M0L6_2atmpS2254;
  struct _M0TUmmmmE* _M0L1tS888;
  uint64_t _M0L6_2atmpS2250;
  uint64_t _M0L6_2atmpS2251;
  uint64_t _M0L6_2atmpS2252;
  uint64_t _M0L6_2atmpS2253;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2797;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS886 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS887);
  _M0L6_2atmpS2254 = _M0L1sS886->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS888 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2254);
  _M0L6_2atmpS2250 = _M0L1sS886->$0;
  _M0L6_2atmpS2251 = _M0L1sS886->$1;
  _M0L6_2atmpS2252 = _M0L1sS886->$2;
  moonbit_decref_cycle_free(_M0L1sS886);
  _M0L6_2atmpS2253 = _M0L1tS888->$0;
  moonbit_decref_cycle_free(_M0L1tS888);
  _block_2797
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2797)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2797->$0 = _M0L6_2atmpS2250;
  _block_2797->$1 = _M0L6_2atmpS2251;
  _block_2797->$2 = _M0L6_2atmpS2252;
  _block_2797->$3 = _M0L6_2atmpS2253;
  return _block_2797;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS878) {
  uint64_t _M0L2s1S877;
  uint64_t _M0L2z1S879;
  uint64_t _M0L2s2S880;
  uint64_t _M0L2z2S881;
  uint64_t _M0L2s3S882;
  uint64_t _M0L2z3S883;
  uint64_t _M0L2s4S884;
  uint64_t _M0L2z4S885;
  struct _M0TUmmmmE* _block_2798;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S877 = _M0L4seedS878 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S879 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S877);
  _M0L2s2S880 = _M0L2s1S877 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S881 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S880);
  _M0L2s3S882 = _M0L2s2S880 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S883 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S882);
  _M0L2s4S884 = _M0L2s3S882 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S885 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S884);
  _block_2798 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2798)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2798->$0 = _M0L2z1S879;
  _block_2798->$1 = _M0L2z2S881;
  _block_2798->$2 = _M0L2z3S883;
  _block_2798->$3 = _M0L2z4S885;
  return _block_2798;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS875) {
  uint64_t _M0L6_2atmpS2249;
  uint64_t _M0L6_2atmpS2248;
  uint64_t _M0L1zS874;
  uint64_t _M0L6_2atmpS2247;
  uint64_t _M0L6_2atmpS2246;
  uint64_t _M0L1zS876;
  uint64_t _M0L6_2atmpS2245;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2249 = _M0L1zS875 >> 30;
  _M0L6_2atmpS2248 = _M0L1zS875 ^ _M0L6_2atmpS2249;
  _M0L1zS874 = _M0L6_2atmpS2248 * 13787848793156543929ull;
  _M0L6_2atmpS2247 = _M0L1zS874 >> 27;
  _M0L6_2atmpS2246 = _M0L1zS874 ^ _M0L6_2atmpS2247;
  _M0L1zS876 = _M0L6_2atmpS2246 * 10723151780598845931ull;
  _M0L6_2atmpS2245 = _M0L1zS876 >> 31;
  return _M0L1zS876 ^ _M0L6_2atmpS2245;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS869
) {
  double _M0L2u1S868;
  double _M0L8u1__safeS870;
  double _M0L2u2S871;
  double _M0L6_2atmpS2244;
  double _M0L6_2atmpS2243;
  double _M0L1rS872;
  double _M0L5thetaS873;
  double _M0L6_2atmpS2242;
  double _M0L6_2atmpS2239;
  double _M0L6_2atmpS2241;
  double _M0L6_2atmpS2240;
  struct _M0TUddE* _block_2799;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S868 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS869);
  if (_M0L2u1S868 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS870 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS870 = _M0L2u1S868;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S871 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS869);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2244 = _M0FPC14math2ln(_M0L8u1__safeS870);
  _M0L6_2atmpS2243 = -0x1p+1 * _M0L6_2atmpS2244;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS872 = sqrt(_M0L6_2atmpS2243);
  _M0L5thetaS873 = 0x1.921fb54442d18p+2 * _M0L2u2S871;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2242 = _M0FPC14math3cos(_M0L5thetaS873);
  _M0L6_2atmpS2239 = _M0L1rS872 * _M0L6_2atmpS2242;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2241 = _M0FPC14math3sin(_M0L5thetaS873);
  _M0L6_2atmpS2240 = _M0L1rS872 * _M0L6_2atmpS2241;
  _block_2799 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2799)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2799->$0 = _M0L6_2atmpS2239;
  _block_2799->$1 = _M0L6_2atmpS2240;
  return _block_2799;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS866
) {
  uint64_t _M0L1uS865;
  uint64_t _M0L4bitsS867;
  double _M0L6_2atmpS2238;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS865 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS866);
  _M0L4bitsS867 = _M0L1uS865 >> 11;
  _M0L6_2atmpS2238 = (double)_M0L4bitsS867;
  return _M0L6_2atmpS2238 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS863
) {
  uint32_t _M0L1uS862;
  uint32_t _M0L4bitsS864;
  double _M0L6_2atmpS2237;
  double _M0L6_2atmpS2236;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS862 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS863);
  _M0L4bitsS864 = _M0L1uS862 >> 8;
  _M0L6_2atmpS2237 = (double)_M0L4bitsS864;
  _M0L6_2atmpS2236 = _M0L6_2atmpS2237 * 0x1p-24;
  return (float)_M0L6_2atmpS2236;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS861
) {
  uint64_t _M0L1uS860;
  uint64_t _M0L6_2atmpS2235;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS860 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS861);
  _M0L6_2atmpS2235 = _M0L1uS860 >> 32;
  return (uint32_t)_M0L6_2atmpS2235;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS853
) {
  uint64_t _M0L2s0S852;
  uint64_t _M0L2s1S854;
  uint64_t _M0L2s2S855;
  uint64_t _M0L2s3S856;
  uint64_t _M0L3tmpS857;
  uint64_t _M0L6_2atmpS2234;
  uint64_t _M0L3resS858;
  uint64_t _M0L1tS859;
  uint64_t _M0L6_2atmpS2224;
  uint64_t _M0L6_2atmpS2225;
  uint64_t _M0L2s2S2227;
  uint64_t _M0L6_2atmpS2226;
  uint64_t _M0L2s3S2229;
  uint64_t _M0L6_2atmpS2228;
  uint64_t _M0L2s2S2231;
  uint64_t _M0L6_2atmpS2230;
  uint64_t _M0L2s3S2233;
  uint64_t _M0L6_2atmpS2232;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S852 = _M0L1rS853->$0;
  _M0L2s1S854 = _M0L1rS853->$1;
  _M0L2s2S855 = _M0L1rS853->$2;
  _M0L2s3S856 = _M0L1rS853->$3;
  _M0L3tmpS857 = _M0L2s0S852 + _M0L2s3S856;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2234 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS857, 23);
  _M0L3resS858 = _M0L6_2atmpS2234 + _M0L2s0S852;
  _M0L1tS859 = _M0L2s1S854 << 17;
  _M0L6_2atmpS2224 = _M0L2s2S855 ^ _M0L2s0S852;
  _M0L1rS853->$2 = _M0L6_2atmpS2224;
  _M0L6_2atmpS2225 = _M0L2s3S856 ^ _M0L2s1S854;
  _M0L1rS853->$3 = _M0L6_2atmpS2225;
  _M0L2s2S2227 = _M0L1rS853->$2;
  _M0L6_2atmpS2226 = _M0L2s1S854 ^ _M0L2s2S2227;
  _M0L1rS853->$1 = _M0L6_2atmpS2226;
  _M0L2s3S2229 = _M0L1rS853->$3;
  _M0L6_2atmpS2228 = _M0L2s0S852 ^ _M0L2s3S2229;
  _M0L1rS853->$0 = _M0L6_2atmpS2228;
  _M0L2s2S2231 = _M0L1rS853->$2;
  _M0L6_2atmpS2230 = _M0L2s2S2231 ^ _M0L1tS859;
  _M0L1rS853->$2 = _M0L6_2atmpS2230;
  _M0L2s3S2233 = _M0L1rS853->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2232 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2233, 45);
  _M0L1rS853->$3 = _M0L6_2atmpS2232;
  return _M0L3resS858;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS850, int32_t _M0L1kS851) {
  uint64_t _M0L6_2atmpS2221;
  int32_t _M0L6_2atmpS2223;
  uint64_t _M0L6_2atmpS2222;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2221 = _M0L1xS850 << (_M0L1kS851 & 63);
  _M0L6_2atmpS2223 = 64 - _M0L1kS851;
  _M0L6_2atmpS2222 = _M0L1xS850 >> (_M0L6_2atmpS2223 & 63);
  return _M0L6_2atmpS2221 | _M0L6_2atmpS2222;
}

double _M0FPC14math2ln(double _M0L1xS836) {
  struct _M0TUdiE* _M0L7_2abindS837;
  double _M0L5_2af1S838;
  int32_t _M0L5_2akiS839;
  double _M0L1fS841;
  double _M0L1kS842;
  double _M0L6_2atmpS2214;
  double _M0L1sS843;
  double _M0L2s2S844;
  double _M0L2s4S845;
  double _M0L6_2atmpS2213;
  double _M0L6_2atmpS2212;
  double _M0L6_2atmpS2211;
  double _M0L6_2atmpS2210;
  double _M0L6_2atmpS2209;
  double _M0L6_2atmpS2208;
  double _M0L2t1S846;
  double _M0L6_2atmpS2207;
  double _M0L6_2atmpS2206;
  double _M0L6_2atmpS2205;
  double _M0L6_2atmpS2204;
  double _M0L2t2S847;
  double _M0L1rS848;
  double _M0L6_2atmpS2203;
  double _M0L4hfsqS849;
  double _M0L6_2atmpS2196;
  double _M0L6_2atmpS2202;
  double _M0L6_2atmpS2200;
  double _M0L6_2atmpS2201;
  double _M0L6_2atmpS2199;
  double _M0L6_2atmpS2198;
  double _M0L6_2atmpS2197;
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
    double _M0L6_2atmpS2218 = _M0L5_2af1S838 * 0x1p+1;
    double _M0L6_2atmpS2215 = _M0L6_2atmpS2218 - 0x1p+0;
    int32_t _M0L6_2atmpS2217 = _M0L5_2akiS839 - 1;
    double _M0L6_2atmpS2216 = (double)_M0L6_2atmpS2217;
    _M0L1fS841 = _M0L6_2atmpS2215;
    _M0L1kS842 = _M0L6_2atmpS2216;
    goto join_840;
  } else {
    double _M0L6_2atmpS2219 = _M0L5_2af1S838 - 0x1p+0;
    double _M0L6_2atmpS2220 = (double)_M0L5_2akiS839;
    _M0L1fS841 = _M0L6_2atmpS2219;
    _M0L1kS842 = _M0L6_2atmpS2220;
    goto join_840;
  }
  join_840:;
  _M0L6_2atmpS2214 = 0x1p+1 + _M0L1fS841;
  _M0L1sS843 = _M0L1fS841 / _M0L6_2atmpS2214;
  _M0L2s2S844 = _M0L1sS843 * _M0L1sS843;
  _M0L2s4S845 = _M0L2s2S844 * _M0L2s2S844;
  _M0L6_2atmpS2213 = _M0L2s4S845 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2212 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2213;
  _M0L6_2atmpS2211 = _M0L2s4S845 * _M0L6_2atmpS2212;
  _M0L6_2atmpS2210 = 0x1.2492494229359p-2 + _M0L6_2atmpS2211;
  _M0L6_2atmpS2209 = _M0L2s4S845 * _M0L6_2atmpS2210;
  _M0L6_2atmpS2208 = 0x1.5555555555593p-1 + _M0L6_2atmpS2209;
  _M0L2t1S846 = _M0L2s2S844 * _M0L6_2atmpS2208;
  _M0L6_2atmpS2207 = _M0L2s4S845 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2206 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2207;
  _M0L6_2atmpS2205 = _M0L2s4S845 * _M0L6_2atmpS2206;
  _M0L6_2atmpS2204 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2205;
  _M0L2t2S847 = _M0L2s4S845 * _M0L6_2atmpS2204;
  _M0L1rS848 = _M0L2t1S846 + _M0L2t2S847;
  _M0L6_2atmpS2203 = 0x1p-1 * _M0L1fS841;
  _M0L4hfsqS849 = _M0L6_2atmpS2203 * _M0L1fS841;
  _M0L6_2atmpS2196 = _M0L1kS842 * 0x1.62e42feep-1;
  _M0L6_2atmpS2202 = _M0L4hfsqS849 + _M0L1rS848;
  _M0L6_2atmpS2200 = _M0L1sS843 * _M0L6_2atmpS2202;
  _M0L6_2atmpS2201 = _M0L1kS842 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2199 = _M0L6_2atmpS2200 + _M0L6_2atmpS2201;
  _M0L6_2atmpS2198 = _M0L4hfsqS849 - _M0L6_2atmpS2199;
  _M0L6_2atmpS2197 = _M0L6_2atmpS2198 - _M0L1fS841;
  return _M0L6_2atmpS2196 - _M0L6_2atmpS2197;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS829) {
  struct _M0TUdiE* _M0L7_2abindS830;
  double _M0L10_2anorm__fS831;
  int32_t _M0L6_2aexpS832;
  uint64_t _M0L1uS833;
  uint64_t _M0L6_2atmpS2195;
  uint64_t _M0L6_2atmpS2194;
  int32_t _M0L6_2atmpS2193;
  int32_t _M0L6_2atmpS2192;
  int32_t _M0L3expS834;
  uint64_t _M0L6_2atmpS2191;
  uint64_t _M0L6_2atmpS2190;
  uint64_t _M0L6_2atmpS2189;
  double _M0L4fracS835;
  struct _M0TUdiE* _block_2802;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS829 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS829)
    || _M0MPC16double6Double7is__nan(_M0L1fS829)
  ) {
    struct _M0TUdiE* _block_2801 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2801)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2801->$0 = _M0L1fS829;
    _block_2801->$1 = 0;
    return _block_2801;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS830 = _M0FPC14math9normalize(_M0L1fS829);
  _M0L10_2anorm__fS831 = _M0L7_2abindS830->$0;
  _M0L6_2aexpS832 = _M0L7_2abindS830->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS830);
  _M0L1uS833 = *(int64_t*)&_M0L10_2anorm__fS831;
  _M0L6_2atmpS2195 = _M0L1uS833 >> 52;
  _M0L6_2atmpS2194 = _M0L6_2atmpS2195 & 2047ull;
  _M0L6_2atmpS2193 = (int32_t)_M0L6_2atmpS2194;
  _M0L6_2atmpS2192 = _M0L6_2aexpS832 + _M0L6_2atmpS2193;
  _M0L3expS834 = _M0L6_2atmpS2192 - 1022;
  _M0L6_2atmpS2191 = ~9218868437227405312ull;
  _M0L6_2atmpS2190 = _M0L1uS833 & _M0L6_2atmpS2191;
  _M0L6_2atmpS2189 = _M0L6_2atmpS2190 | 4602678819172646912ull;
  _M0L4fracS835 = *(double*)&_M0L6_2atmpS2189;
  _block_2802 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2802)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2802->$0 = _M0L4fracS835;
  _block_2802->$1 = _M0L3expS834;
  return _block_2802;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS828) {
  double _M0L6_2atmpS2186;
  struct _M0TUdiE* _block_2804;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2186 = fabs(_M0L1fS828);
  if (_M0L6_2atmpS2186 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2188 = (double)4503599627370496ll;
    double _M0L6_2atmpS2187 = _M0L1fS828 * _M0L6_2atmpS2188;
    struct _M0TUdiE* _block_2803 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2803)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2803->$0 = _M0L6_2atmpS2187;
    _block_2803->$1 = -52;
    return _block_2803;
  }
  _block_2804 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2804)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2804->$0 = _M0L1fS828;
  _block_2804->$1 = 0;
  return _block_2804;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS827) {
  double _M0L6_2atmpS2185;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2185 = (double)_M0L4selfS827;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2185);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS826) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS826 != _M0L4selfS826) {
    return 0;
  } else if (_M0L4selfS826 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS826 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS826;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS807,
  float _M0L4elemS809
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS806;
  int32_t _M0L1iS808;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS806 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS807);
  _M0L1iS808 = 0;
  while (1) {
    if (_M0L1iS808 < _M0L3lenS807) {
      float* _M0L3bufS2177 = _M0L3arrS806->$0;
      int32_t _M0L6_2atmpS2178;
      _M0L3bufS2177[_M0L1iS808] = _M0L4elemS809;
      _M0L6_2atmpS2178 = _M0L1iS808 + 1;
      _M0L1iS808 = _M0L6_2atmpS2178;
      continue;
    }
    break;
  }
  return _M0L3arrS806;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS812,
  int32_t _M0L4elemS814
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS811;
  int32_t _M0L1iS813;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS811 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS812);
  _M0L1iS813 = 0;
  while (1) {
    if (_M0L1iS813 < _M0L3lenS812) {
      uint8_t* _M0L3bufS2179 = _M0L3arrS811->$0;
      int32_t _M0L6_2atmpS2180;
      _M0L3bufS2179[_M0L1iS813] = _M0L4elemS814;
      _M0L6_2atmpS2180 = _M0L1iS813 + 1;
      _M0L1iS813 = _M0L6_2atmpS2180;
      continue;
    }
    break;
  }
  return _M0L3arrS811;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS817,
  int32_t _M0L4elemS819
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS816;
  int32_t _M0L1iS818;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS816 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS817);
  _M0L1iS818 = 0;
  while (1) {
    if (_M0L1iS818 < _M0L3lenS817) {
      int32_t* _M0L3bufS2181 = _M0L3arrS816->$0;
      int32_t _M0L6_2atmpS2182;
      _M0L3bufS2181[_M0L1iS818] = _M0L4elemS819;
      _M0L6_2atmpS2182 = _M0L1iS818 + 1;
      _M0L1iS818 = _M0L6_2atmpS2182;
      continue;
    }
    break;
  }
  return _M0L3arrS816;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS822,
  struct _M0TPB5ArrayGfE* _M0L4elemS824
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS821;
  int32_t _M0L1iS823;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS821
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS822);
  _M0L1iS823 = 0;
  while (1) {
    if (_M0L1iS823 < _M0L3lenS822) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2183 = _M0L3arrS821->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2683 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2183[_M0L1iS823];
      int32_t _M0L6_2atmpS2184;
      moonbit_incref_cycle_free(_M0L4elemS824);
      if (_M0L6_2aoldS2683) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2683);
      }
      _M0L3bufS2183[_M0L1iS823] = _M0L4elemS824;
      _M0L6_2atmpS2184 = _M0L1iS823 + 1;
      _M0L1iS823 = _M0L6_2atmpS2184;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS824);
    }
    break;
  }
  return _M0L3arrS821;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS791,
  int32_t _M0L5indexS792,
  float _M0L5valueS793
) {
  int32_t _M0L3lenS790;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS790 = _M0L4selfS791->$1;
  if (_M0L5indexS792 >= 0 && _M0L5indexS792 < _M0L3lenS790) {
    float* _M0L6_2atmpS2173;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2173 = _M0MPC15array5Array6bufferGfE(_M0L4selfS791);
    _M0L6_2atmpS2173[_M0L5indexS792] = _M0L5valueS793;
    moonbit_decref_cycle_free(_M0L6_2atmpS2173);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS795,
  int32_t _M0L5indexS796,
  int32_t _M0L5valueS797
) {
  int32_t _M0L3lenS794;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS794 = _M0L4selfS795->$1;
  if (_M0L5indexS796 >= 0 && _M0L5indexS796 < _M0L3lenS794) {
    uint8_t* _M0L6_2atmpS2174;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2174 = _M0MPC15array5Array6bufferGbE(_M0L4selfS795);
    _M0L6_2atmpS2174[_M0L5indexS796] = _M0L5valueS797;
    moonbit_decref_cycle_free(_M0L6_2atmpS2174);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS799,
  int32_t _M0L5indexS800,
  int32_t _M0L5valueS801
) {
  int32_t _M0L3lenS798;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS798 = _M0L4selfS799->$1;
  if (_M0L5indexS800 >= 0 && _M0L5indexS800 < _M0L3lenS798) {
    int32_t* _M0L6_2atmpS2175;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2175 = _M0MPC15array5Array6bufferGiE(_M0L4selfS799);
    _M0L6_2atmpS2175[_M0L5indexS800] = _M0L5valueS801;
    moonbit_decref_cycle_free(_M0L6_2atmpS2175);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS803,
  int32_t _M0L5indexS804,
  struct _M0TPB5ArrayGfE* _M0L5valueS805
) {
  int32_t _M0L3lenS802;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS802 = _M0L4selfS803->$1;
  if (_M0L5indexS804 >= 0 && _M0L5indexS804 < _M0L3lenS802) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2176;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2684;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2176
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS803);
    _M0L6_2aoldS2684
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2176[_M0L5indexS804];
    if (_M0L6_2aoldS2684) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2684);
    }
    _M0L6_2atmpS2176[_M0L5indexS804] = _M0L5valueS805;
    moonbit_decref_cycle_free(_M0L6_2atmpS2176);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS805);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS781,
  int32_t _M0L5indexS780,
  int32_t _M0L5valueS783
) {
  int32_t _if__result_2809;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS780 >= 0) {
    int32_t _M0L3lenS2143 = _M0L4selfS781->$1;
    _if__result_2809 = _M0L5indexS780 <= _M0L3lenS2143;
  } else {
    _if__result_2809 = 0;
  }
  if (_if__result_2809) {
    int32_t _M0L3lenS2144 = _M0L4selfS781->$1;
    int32_t* _M0L6_2atmpS2146;
    int32_t _M0L6_2atmpS2145;
    int32_t* _M0L6_2atmpS2149;
    int32_t _M0L6_2atmpS2150;
    int32_t* _M0L6_2atmpS2151;
    int32_t _M0L3lenS2153;
    int32_t _M0L6_2atmpS2152;
    int32_t _M0L6lengthS782;
    int32_t* _M0L3bufS2154;
    int32_t _M0L6_2atmpS2155;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2146 = _M0MPC15array5Array6bufferGiE(_M0L4selfS781);
    _M0L6_2atmpS2145 = Moonbit_array_length(_M0L6_2atmpS2146);
    moonbit_decref_cycle_free(_M0L6_2atmpS2146);
    if (_M0L3lenS2144 == _M0L6_2atmpS2145) {
      int32_t _M0L3lenS2148 = _M0L4selfS781->$1;
      int32_t _M0L6_2atmpS2147 = _M0L3lenS2148 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGiE(_M0L4selfS781, _M0L6_2atmpS2147);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2149 = _M0MPC15array5Array6bufferGiE(_M0L4selfS781);
    _M0L6_2atmpS2150 = _M0L5indexS780 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2151 = _M0MPC15array5Array6bufferGiE(_M0L4selfS781);
    _M0L3lenS2153 = _M0L4selfS781->$1;
    _M0L6_2atmpS2152 = _M0L3lenS2153 - _M0L5indexS780;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L6_2atmpS2149, _M0L6_2atmpS2150, _M0L6_2atmpS2151, _M0L5indexS780, _M0L6_2atmpS2152);
    moonbit_decref_cycle_free(_M0L6_2atmpS2149);
    moonbit_decref_cycle_free(_M0L6_2atmpS2151);
    _M0L6lengthS782 = _M0L4selfS781->$1;
    _M0L3bufS2154 = _M0L4selfS781->$0;
    _M0L3bufS2154[_M0L5indexS780] = _M0L5valueS783;
    _M0L6_2atmpS2155 = _M0L6lengthS782 + 1;
    _M0L4selfS781->$1 = _M0L6_2atmpS2155;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS784;
    int32_t _M0L3lenS2157;
    moonbit_string_t _M0L6_2atmpS2156;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS784
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS784, (moonbit_string_t)moonbit_string_literal_9.data);
    _M0L3lenS2157 = _M0L4selfS781->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS784, _M0L3lenS2157);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS784, (moonbit_string_t)moonbit_string_literal_10.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS784, _M0L5indexS780);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2156
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS784);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS784);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS2156);
    moonbit_decref_cycle_free(_M0L6_2atmpS2156);
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS786,
  int32_t _M0L5indexS785,
  float _M0L5valueS788
) {
  int32_t _if__result_2810;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS785 >= 0) {
    int32_t _M0L3lenS2158 = _M0L4selfS786->$1;
    _if__result_2810 = _M0L5indexS785 <= _M0L3lenS2158;
  } else {
    _if__result_2810 = 0;
  }
  if (_if__result_2810) {
    int32_t _M0L3lenS2159 = _M0L4selfS786->$1;
    float* _M0L6_2atmpS2161;
    int32_t _M0L6_2atmpS2160;
    float* _M0L6_2atmpS2164;
    int32_t _M0L6_2atmpS2165;
    float* _M0L6_2atmpS2166;
    int32_t _M0L3lenS2168;
    int32_t _M0L6_2atmpS2167;
    int32_t _M0L6lengthS787;
    float* _M0L3bufS2169;
    int32_t _M0L6_2atmpS2170;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2161 = _M0MPC15array5Array6bufferGfE(_M0L4selfS786);
    _M0L6_2atmpS2160 = Moonbit_array_length(_M0L6_2atmpS2161);
    moonbit_decref_cycle_free(_M0L6_2atmpS2161);
    if (_M0L3lenS2159 == _M0L6_2atmpS2160) {
      int32_t _M0L3lenS2163 = _M0L4selfS786->$1;
      int32_t _M0L6_2atmpS2162 = _M0L3lenS2163 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGfE(_M0L4selfS786, _M0L6_2atmpS2162);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2164 = _M0MPC15array5Array6bufferGfE(_M0L4selfS786);
    _M0L6_2atmpS2165 = _M0L5indexS785 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2166 = _M0MPC15array5Array6bufferGfE(_M0L4selfS786);
    _M0L3lenS2168 = _M0L4selfS786->$1;
    _M0L6_2atmpS2167 = _M0L3lenS2168 - _M0L5indexS785;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L6_2atmpS2164, _M0L6_2atmpS2165, _M0L6_2atmpS2166, _M0L5indexS785, _M0L6_2atmpS2167);
    moonbit_decref_cycle_free(_M0L6_2atmpS2164);
    moonbit_decref_cycle_free(_M0L6_2atmpS2166);
    _M0L6lengthS787 = _M0L4selfS786->$1;
    _M0L3bufS2169 = _M0L4selfS786->$0;
    _M0L3bufS2169[_M0L5indexS785] = _M0L5valueS788;
    _M0L6_2atmpS2170 = _M0L6lengthS787 + 1;
    _M0L4selfS786->$1 = _M0L6_2atmpS2170;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS789;
    int32_t _M0L3lenS2172;
    moonbit_string_t _M0L6_2atmpS2171;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS789
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS789, (moonbit_string_t)moonbit_string_literal_9.data);
    _M0L3lenS2172 = _M0L4selfS786->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS789, _M0L3lenS2172);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS789, (moonbit_string_t)moonbit_string_literal_10.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS789, _M0L5indexS785);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2171
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS789);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS789);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS2171);
    moonbit_decref_cycle_free(_M0L6_2atmpS2171);
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
    float* _M0L6_2atmpS2137;
    float _result_2811;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2137 = _M0MPC15array5Array6bufferGfE(_M0L4selfS763);
    _result_2811 = (float)_M0L6_2atmpS2137[_M0L5indexS764];
    moonbit_decref_cycle_free(_M0L6_2atmpS2137);
    return _result_2811;
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
    uint8_t* _M0L6_2atmpS2138;
    int32_t _result_2812;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2138 = _M0MPC15array5Array6bufferGbE(_M0L4selfS766);
    _result_2812 = (int32_t)_M0L6_2atmpS2138[_M0L5indexS767];
    moonbit_decref_cycle_free(_M0L6_2atmpS2138);
    return _result_2812;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L4selfS769,
  int32_t _M0L5indexS770
) {
  int32_t _M0L3lenS768;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS768 = _M0L4selfS769->$1;
  if (_M0L5indexS770 >= 0 && _M0L5indexS770 < _M0L3lenS768) {
    struct _M0TP26RiantR8snn__mbt13SynapseTarget** _M0L6_2atmpS2139;
    struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2685;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2139
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt13SynapseTargetE(_M0L4selfS769);
    _M0L6_2atmpS2685
    = (struct _M0TP26RiantR8snn__mbt13SynapseTarget*)_M0L6_2atmpS2139[
        _M0L5indexS770
      ];
    if (_M0L6_2atmpS2685) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2685);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2139);
    return _M0L6_2atmpS2685;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS772,
  int32_t _M0L5indexS773
) {
  int32_t _M0L3lenS771;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS771 = _M0L4selfS772->$1;
  if (_M0L5indexS773 >= 0 && _M0L5indexS773 < _M0L3lenS771) {
    int32_t* _M0L6_2atmpS2140;
    int32_t _result_2813;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2140 = _M0MPC15array5Array6bufferGiE(_M0L4selfS772);
    _result_2813 = (int32_t)_M0L6_2atmpS2140[_M0L5indexS773];
    moonbit_decref_cycle_free(_M0L6_2atmpS2140);
    return _result_2813;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS775,
  int32_t _M0L5indexS776
) {
  int32_t _M0L3lenS774;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS774 = _M0L4selfS775->$1;
  if (_M0L5indexS776 >= 0 && _M0L5indexS776 < _M0L3lenS774) {
    moonbit_string_t* _M0L6_2atmpS2141;
    moonbit_string_t _M0L6_2atmpS2686;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2141 = _M0MPC15array5Array6bufferGsE(_M0L4selfS775);
    _M0L6_2atmpS2686 = (moonbit_string_t)_M0L6_2atmpS2141[_M0L5indexS776];
    moonbit_incref_cycle_free(_M0L6_2atmpS2686);
    moonbit_decref_cycle_free(_M0L6_2atmpS2141);
    return _M0L6_2atmpS2686;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS778,
  int32_t _M0L5indexS779
) {
  int32_t _M0L3lenS777;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS777 = _M0L4selfS778->$1;
  if (_M0L5indexS779 >= 0 && _M0L5indexS779 < _M0L3lenS777) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2142;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2687;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2142
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS778);
    _M0L6_2atmpS2687
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2142[_M0L5indexS779];
    if (_M0L6_2atmpS2687) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2687);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2142);
    return _M0L6_2atmpS2687;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS761) {
  moonbit_string_t _M0L6_2atmpS2136;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2136 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS761);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2136);
  moonbit_decref_cycle_free(_M0L6_2atmpS2136);
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
  uint64_t _M0L6_2atmpS2135;
  uint64_t _M0L6_2atmpS2134;
  int32_t _M0L8ieeeSignS747;
  uint64_t _M0L12ieeeMantissaS748;
  uint64_t _M0L6_2atmpS2133;
  uint64_t _M0L6_2atmpS2132;
  int32_t _M0L12ieeeExponentS749;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS750;
  struct _M0TPB17FloatingDecimal64* _M0L1vS751;
  moonbit_string_t _result_2815;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS743 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L3valS743 >= -0x1p+53 && _M0L3valS743 <= 0x1p+53) {
    if (_M0L3valS743 >= -0x1p+31 && _M0L3valS743 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS744;
      double _M0L6_2atmpS2121;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double7to__int(_M0L3valS743);
      _M0L6_2atmpS2121 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2121 == _M0L3valS743) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS744, 10);
      }
    } else {
      int64_t _M0L1iS745;
      double _M0L6_2atmpS2122;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS745 = _M0MPC16double6Double9to__int64(_M0L3valS743);
      _M0L6_2atmpS2122 = (double)_M0L1iS745;
      if (_M0L6_2atmpS2122 == _M0L3valS743) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS745, 10);
      }
    }
  }
  _M0L4bitsS746 = *(int64_t*)&_M0L3valS743;
  _M0L6_2atmpS2135 = _M0L4bitsS746 >> 63;
  _M0L6_2atmpS2134 = _M0L6_2atmpS2135 & 1ull;
  _M0L8ieeeSignS747 = _M0L6_2atmpS2134 != 0ull;
  _M0L12ieeeMantissaS748 = _M0L4bitsS746 & 4503599627370495ull;
  _M0L6_2atmpS2133 = _M0L4bitsS746 >> 52;
  _M0L6_2atmpS2132 = _M0L6_2atmpS2133 & 2047ull;
  _M0L12ieeeExponentS749 = (int32_t)_M0L6_2atmpS2132;
  if (
    _M0L12ieeeExponentS749 == 2047
    || _M0L12ieeeExponentS749 == 0 && _M0L12ieeeMantissaS748 == 0ull
  ) {
    int32_t _M0L6_2atmpS2123 = _M0L12ieeeExponentS749 != 0;
    int32_t _M0L6_2atmpS2124 = _M0L12ieeeMantissaS748 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS747, _M0L6_2atmpS2123, _M0L6_2atmpS2124);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS750
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS748, _M0L12ieeeExponentS749);
  if (_M0L7_2abindS750 == 0) {
    uint32_t _M0L6_2atmpS2125;
    if (_M0L7_2abindS750) {
      moonbit_decref_cycle_free(_M0L7_2abindS750);
    }
    _M0L6_2atmpS2125 = *(uint32_t*)&_M0L12ieeeExponentS749;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS751 = _M0FPB3d2d(_M0L12ieeeMantissaS748, _M0L6_2atmpS2125);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS752 = _M0L7_2abindS750;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS753 = _M0L7_2aSomeS752;
    struct _M0TPB17FloatingDecimal64* _M0L1xS754 = _M0L4_2afS753;
    while (1) {
      uint64_t _M0L8mantissaS2131 = _M0L1xS754->$0;
      uint64_t _M0L1qS755 = _M0L8mantissaS2131 / 10ull;
      uint64_t _M0L8mantissaS2129 = _M0L1xS754->$0;
      uint64_t _M0L6_2atmpS2130 = 10ull * _M0L1qS755;
      uint64_t _M0L1rS756 = _M0L8mantissaS2129 - _M0L6_2atmpS2130;
      int32_t _M0L8exponentS2128;
      int32_t _M0L6_2atmpS2127;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2126;
      if (_M0L1rS756 != 0ull) {
        _M0L1vS751 = _M0L1xS754;
        break;
      }
      _M0L8exponentS2128 = _M0L1xS754->$1;
      moonbit_decref_cycle_free(_M0L1xS754);
      _M0L6_2atmpS2127 = _M0L8exponentS2128 + 1;
      _M0L6_2atmpS2126
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2126)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2126->$0 = _M0L1qS755;
      _M0L6_2atmpS2126->$1 = _M0L6_2atmpS2127;
      _M0L1xS754 = _M0L6_2atmpS2126;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2815 = _M0FPB9to__chars(_M0L1vS751, _M0L8ieeeSignS747);
  moonbit_decref_cycle_free(_M0L1vS751);
  return _result_2815;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS738,
  int32_t _M0L12ieeeExponentS740
) {
  uint64_t _M0L2m2S737;
  int32_t _M0L6_2atmpS2120;
  int32_t _M0L2e2S739;
  int32_t _M0L6_2atmpS2119;
  uint64_t _M0L6_2atmpS2118;
  uint64_t _M0L4maskS741;
  uint64_t _M0L8fractionS742;
  int32_t _M0L6_2atmpS2117;
  uint64_t _M0L6_2atmpS2116;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2115;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S737 = 4503599627370496ull | _M0L12ieeeMantissaS738;
  _M0L6_2atmpS2120 = _M0L12ieeeExponentS740 - 1023;
  _M0L2e2S739 = _M0L6_2atmpS2120 - 52;
  if (_M0L2e2S739 > 0) {
    return 0;
  }
  if (_M0L2e2S739 < -52) {
    return 0;
  }
  _M0L6_2atmpS2119 = -_M0L2e2S739;
  _M0L6_2atmpS2118 = 1ull << (_M0L6_2atmpS2119 & 63);
  _M0L4maskS741 = _M0L6_2atmpS2118 - 1ull;
  _M0L8fractionS742 = _M0L2m2S737 & _M0L4maskS741;
  if (_M0L8fractionS742 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2117 = -_M0L2e2S739;
  _M0L6_2atmpS2116 = _M0L2m2S737 >> (_M0L6_2atmpS2117 & 63);
  _M0L6_2atmpS2115
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2115)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2115->$0 = _M0L6_2atmpS2116;
  _M0L6_2atmpS2115->$1 = 0;
  return _M0L6_2atmpS2115;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS705,
  int32_t _M0L4signS703
) {
  moonbit_bytes_t _M0L6resultS701;
  int32_t _M0Lm5indexS702;
  uint64_t _M0L6outputS704;
  int32_t _M0L7olengthS706;
  int32_t _M0L8exponentS2114;
  int32_t _M0L6_2atmpS2113;
  int32_t _M0Lm3expS707;
  int32_t _M0L6_2atmpS2112;
  int32_t _M0L6_2atmpS2110;
  int32_t _M0L18scientificNotationS708;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS701 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS702 = 0;
  if (_M0L4signS703) {
    int32_t _M0L6_2atmpS1984 = _M0Lm5indexS702;
    int32_t _M0L6_2atmpS1985;
    if (
      _M0L6_2atmpS1984 < 0
      || _M0L6_2atmpS1984 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1984] = 45;
    _M0L6_2atmpS1985 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS1985 + 1;
  }
  _M0L6outputS704 = _M0L1vS705->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS706 = _M0FPB17decimal__length17(_M0L6outputS704);
  _M0L8exponentS2114 = _M0L1vS705->$1;
  _M0L6_2atmpS2113 = _M0L8exponentS2114 + _M0L7olengthS706;
  _M0Lm3expS707 = _M0L6_2atmpS2113 - 1;
  _M0L6_2atmpS2112 = _M0Lm3expS707;
  if (_M0L6_2atmpS2112 >= -6) {
    int32_t _M0L6_2atmpS2111 = _M0Lm3expS707;
    _M0L6_2atmpS2110 = _M0L6_2atmpS2111 < 21;
  } else {
    _M0L6_2atmpS2110 = 0;
  }
  _M0L18scientificNotationS708 = !_M0L6_2atmpS2110;
  if (_M0L18scientificNotationS708) {
    int32_t _M0L7_2abindS709 = _M0L7olengthS706 - 1;
    uint64_t _M0L6outputS710;
    int32_t _M0L1iS711 = 0;
    uint64_t _M0L6outputS712 = _M0L6outputS704;
    int32_t _M0L6_2atmpS1986;
    int32_t _M0L6_2atmpS1990;
    int32_t _M0L6_2atmpS1989;
    int32_t _M0L6_2atmpS1988;
    int32_t _M0L6_2atmpS1987;
    int32_t _M0L6_2atmpS1994;
    int32_t _M0L6_2atmpS1995;
    int32_t _M0L6_2atmpS1996;
    int32_t _M0L6_2atmpS1997;
    int32_t _M0L6_2atmpS1998;
    int32_t _M0L6_2atmpS2004;
    int32_t _M0L6_2atmpS2037;
    moonbit_string_t _result_2817;
    while (1) {
      if (_M0L1iS711 < _M0L7_2abindS709) {
        uint64_t _M0L1cS713 = _M0L6outputS712 % 10ull;
        int32_t _M0L6_2atmpS2043 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2042 = _M0L6_2atmpS2043 + _M0L7olengthS706;
        int32_t _M0L6_2atmpS2038 = _M0L6_2atmpS2042 - _M0L1iS711;
        int32_t _M0L6_2atmpS2041 = (int32_t)_M0L1cS713;
        int32_t _M0L6_2atmpS2040 = 48 + _M0L6_2atmpS2041;
        int32_t _M0L6_2atmpS2039 = _M0L6_2atmpS2040 & 0xff;
        int32_t _M0L6_2atmpS2044;
        uint64_t _M0L6_2atmpS2045;
        if (
          _M0L6_2atmpS2038 < 0
          || _M0L6_2atmpS2038 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2038] = _M0L6_2atmpS2039;
        _M0L6_2atmpS2044 = _M0L1iS711 + 1;
        _M0L6_2atmpS2045 = _M0L6outputS712 / 10ull;
        _M0L1iS711 = _M0L6_2atmpS2044;
        _M0L6outputS712 = _M0L6_2atmpS2045;
        continue;
      } else {
        _M0L6outputS710 = _M0L6outputS712;
      }
      break;
    }
    _M0L6_2atmpS1986 = _M0Lm5indexS702;
    _M0L6_2atmpS1990 = (int32_t)_M0L6outputS710;
    _M0L6_2atmpS1989 = _M0L6_2atmpS1990 % 10;
    _M0L6_2atmpS1988 = 48 + _M0L6_2atmpS1989;
    _M0L6_2atmpS1987 = _M0L6_2atmpS1988 & 0xff;
    if (
      _M0L6_2atmpS1986 < 0
      || _M0L6_2atmpS1986 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1986] = _M0L6_2atmpS1987;
    if (_M0L7olengthS706 > 1) {
      int32_t _M0L6_2atmpS1992 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1991 = _M0L6_2atmpS1992 + 1;
      if (
        _M0L6_2atmpS1991 < 0
        || _M0L6_2atmpS1991 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1991] = 46;
    } else {
      int32_t _M0L6_2atmpS1993 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1993 - 1;
    }
    _M0L6_2atmpS1994 = _M0Lm5indexS702;
    _M0L6_2atmpS1995 = _M0L7olengthS706 + 1;
    _M0Lm5indexS702 = _M0L6_2atmpS1994 + _M0L6_2atmpS1995;
    _M0L6_2atmpS1996 = _M0Lm5indexS702;
    if (
      _M0L6_2atmpS1996 < 0
      || _M0L6_2atmpS1996 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1996] = 101;
    _M0L6_2atmpS1997 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS1997 + 1;
    _M0L6_2atmpS1998 = _M0Lm3expS707;
    if (_M0L6_2atmpS1998 < 0) {
      int32_t _M0L6_2atmpS1999 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2000;
      int32_t _M0L6_2atmpS2001;
      if (
        _M0L6_2atmpS1999 < 0
        || _M0L6_2atmpS1999 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1999] = 45;
      _M0L6_2atmpS2000 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2000 + 1;
      _M0L6_2atmpS2001 = _M0Lm3expS707;
      _M0Lm3expS707 = -_M0L6_2atmpS2001;
    } else {
      int32_t _M0L6_2atmpS2002 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2003;
      if (
        _M0L6_2atmpS2002 < 0
        || _M0L6_2atmpS2002 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2002] = 43;
      _M0L6_2atmpS2003 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2003 + 1;
    }
    _M0L6_2atmpS2004 = _M0Lm3expS707;
    if (_M0L6_2atmpS2004 >= 100) {
      int32_t _M0L6_2atmpS2020 = _M0Lm3expS707;
      int32_t _M0L1aS715 = _M0L6_2atmpS2020 / 100;
      int32_t _M0L6_2atmpS2019 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS2018 = _M0L6_2atmpS2019 / 10;
      int32_t _M0L1bS716 = _M0L6_2atmpS2018 % 10;
      int32_t _M0L6_2atmpS2017 = _M0Lm3expS707;
      int32_t _M0L1cS717 = _M0L6_2atmpS2017 % 10;
      int32_t _M0L6_2atmpS2005 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2007 = 48 + _M0L1aS715;
      int32_t _M0L6_2atmpS2006 = _M0L6_2atmpS2007 & 0xff;
      int32_t _M0L6_2atmpS2011;
      int32_t _M0L6_2atmpS2008;
      int32_t _M0L6_2atmpS2010;
      int32_t _M0L6_2atmpS2009;
      int32_t _M0L6_2atmpS2015;
      int32_t _M0L6_2atmpS2012;
      int32_t _M0L6_2atmpS2014;
      int32_t _M0L6_2atmpS2013;
      int32_t _M0L6_2atmpS2016;
      if (
        _M0L6_2atmpS2005 < 0
        || _M0L6_2atmpS2005 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2005] = _M0L6_2atmpS2006;
      _M0L6_2atmpS2011 = _M0Lm5indexS702;
      _M0L6_2atmpS2008 = _M0L6_2atmpS2011 + 1;
      _M0L6_2atmpS2010 = 48 + _M0L1bS716;
      _M0L6_2atmpS2009 = _M0L6_2atmpS2010 & 0xff;
      if (
        _M0L6_2atmpS2008 < 0
        || _M0L6_2atmpS2008 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2008] = _M0L6_2atmpS2009;
      _M0L6_2atmpS2015 = _M0Lm5indexS702;
      _M0L6_2atmpS2012 = _M0L6_2atmpS2015 + 2;
      _M0L6_2atmpS2014 = 48 + _M0L1cS717;
      _M0L6_2atmpS2013 = _M0L6_2atmpS2014 & 0xff;
      if (
        _M0L6_2atmpS2012 < 0
        || _M0L6_2atmpS2012 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2012] = _M0L6_2atmpS2013;
      _M0L6_2atmpS2016 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2016 + 3;
    } else {
      int32_t _M0L6_2atmpS2021 = _M0Lm3expS707;
      if (_M0L6_2atmpS2021 >= 10) {
        int32_t _M0L6_2atmpS2031 = _M0Lm3expS707;
        int32_t _M0L1aS718 = _M0L6_2atmpS2031 / 10;
        int32_t _M0L6_2atmpS2030 = _M0Lm3expS707;
        int32_t _M0L1bS719 = _M0L6_2atmpS2030 % 10;
        int32_t _M0L6_2atmpS2022 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2024 = 48 + _M0L1aS718;
        int32_t _M0L6_2atmpS2023 = _M0L6_2atmpS2024 & 0xff;
        int32_t _M0L6_2atmpS2028;
        int32_t _M0L6_2atmpS2025;
        int32_t _M0L6_2atmpS2027;
        int32_t _M0L6_2atmpS2026;
        int32_t _M0L6_2atmpS2029;
        if (
          _M0L6_2atmpS2022 < 0
          || _M0L6_2atmpS2022 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2022] = _M0L6_2atmpS2023;
        _M0L6_2atmpS2028 = _M0Lm5indexS702;
        _M0L6_2atmpS2025 = _M0L6_2atmpS2028 + 1;
        _M0L6_2atmpS2027 = 48 + _M0L1bS719;
        _M0L6_2atmpS2026 = _M0L6_2atmpS2027 & 0xff;
        if (
          _M0L6_2atmpS2025 < 0
          || _M0L6_2atmpS2025 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2025] = _M0L6_2atmpS2026;
        _M0L6_2atmpS2029 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2029 + 2;
      } else {
        int32_t _M0L6_2atmpS2032 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2035 = _M0Lm3expS707;
        int32_t _M0L6_2atmpS2034 = 48 + _M0L6_2atmpS2035;
        int32_t _M0L6_2atmpS2033 = _M0L6_2atmpS2034 & 0xff;
        int32_t _M0L6_2atmpS2036;
        if (
          _M0L6_2atmpS2032 < 0
          || _M0L6_2atmpS2032 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2032] = _M0L6_2atmpS2033;
        _M0L6_2atmpS2036 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2036 + 1;
      }
    }
    _M0L6_2atmpS2037 = _M0Lm5indexS702;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2817
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS2037);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_2817;
  } else {
    int32_t _M0L6_2atmpS2046 = _M0Lm3expS707;
    int32_t _M0L6_2atmpS2109;
    moonbit_string_t _result_2823;
    if (_M0L6_2atmpS2046 < 0) {
      int32_t _M0L6_2atmpS2047 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2049;
      int32_t _M0L6_2atmpS2048;
      int32_t _M0L6_2atmpS2050;
      int32_t _M0L1iS720;
      int32_t _M0L6_2atmpS2065;
      int32_t _M0L6_2atmpS2067;
      int32_t _M0L6_2atmpS2066;
      int32_t _M0L7currentS722;
      int32_t _M0L1iS723;
      uint64_t _M0L6outputS724;
      if (
        _M0L6_2atmpS2047 < 0
        || _M0L6_2atmpS2047 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2047] = 48;
      _M0L6_2atmpS2049 = _M0Lm5indexS702;
      _M0L6_2atmpS2048 = _M0L6_2atmpS2049 + 1;
      if (
        _M0L6_2atmpS2048 < 0
        || _M0L6_2atmpS2048 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2048] = 46;
      _M0L6_2atmpS2050 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2050 + 2;
      _M0L1iS720 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2051 = _M0Lm3expS707;
        if (_M0L1iS720 > _M0L6_2atmpS2051) {
          int32_t _M0L6_2atmpS2054 = _M0Lm5indexS702;
          int32_t _M0L6_2atmpS2053 = _M0L6_2atmpS2054 - _M0L1iS720;
          int32_t _M0L6_2atmpS2052 = _M0L6_2atmpS2053 - 1;
          int32_t _M0L6_2atmpS2055;
          if (
            _M0L6_2atmpS2052 < 0
            || _M0L6_2atmpS2052 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS2052] = 48;
          _M0L6_2atmpS2055 = _M0L1iS720 - 1;
          _M0L1iS720 = _M0L6_2atmpS2055;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2065 = _M0Lm5indexS702;
      _M0L6_2atmpS2067 = _M0Lm3expS707;
      _M0L6_2atmpS2066 = -1 - _M0L6_2atmpS2067;
      _M0L7currentS722 = _M0L6_2atmpS2065 + _M0L6_2atmpS2066;
      _M0L1iS723 = 0;
      _M0L6outputS724 = _M0L6outputS704;
      while (1) {
        if (_M0L1iS723 < _M0L7olengthS706) {
          int32_t _M0L6_2atmpS2062 = _M0L7currentS722 + _M0L7olengthS706;
          int32_t _M0L6_2atmpS2061 = _M0L6_2atmpS2062 - _M0L1iS723;
          int32_t _M0L6_2atmpS2056 = _M0L6_2atmpS2061 - 1;
          uint64_t _M0L6_2atmpS2060 = _M0L6outputS724 % 10ull;
          int32_t _M0L6_2atmpS2059 = (int32_t)_M0L6_2atmpS2060;
          int32_t _M0L6_2atmpS2058 = 48 + _M0L6_2atmpS2059;
          int32_t _M0L6_2atmpS2057 = _M0L6_2atmpS2058 & 0xff;
          int32_t _M0L6_2atmpS2063;
          uint64_t _M0L6_2atmpS2064;
          if (
            _M0L6_2atmpS2056 < 0
            || _M0L6_2atmpS2056 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS2056] = _M0L6_2atmpS2057;
          _M0L6_2atmpS2063 = _M0L1iS723 + 1;
          _M0L6_2atmpS2064 = _M0L6outputS724 / 10ull;
          _M0L1iS723 = _M0L6_2atmpS2063;
          _M0L6outputS724 = _M0L6_2atmpS2064;
          continue;
        }
        break;
      }
      _M0Lm5indexS702 = _M0L7currentS722 + _M0L7olengthS706;
    } else {
      int32_t _M0L6_2atmpS2069 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS2068 = _M0L6_2atmpS2069 + 1;
      if (_M0L6_2atmpS2068 >= _M0L7olengthS706) {
        int32_t _M0L1iS726 = 0;
        uint64_t _M0L6outputS727 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2080;
        int32_t _M0L6_2atmpS2085;
        int32_t _M0L7_2abindS729;
        int32_t _M0L1iS730;
        int32_t _M0L6_2atmpS2086;
        int32_t _M0L6_2atmpS2089;
        int32_t _M0L6_2atmpS2088;
        int32_t _M0L6_2atmpS2087;
        while (1) {
          if (_M0L1iS726 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2077 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2076 = _M0L6_2atmpS2077 + _M0L7olengthS706;
            int32_t _M0L6_2atmpS2075 = _M0L6_2atmpS2076 - _M0L1iS726;
            int32_t _M0L6_2atmpS2070 = _M0L6_2atmpS2075 - 1;
            uint64_t _M0L6_2atmpS2074 = _M0L6outputS727 % 10ull;
            int32_t _M0L6_2atmpS2073 = (int32_t)_M0L6_2atmpS2074;
            int32_t _M0L6_2atmpS2072 = 48 + _M0L6_2atmpS2073;
            int32_t _M0L6_2atmpS2071 = _M0L6_2atmpS2072 & 0xff;
            int32_t _M0L6_2atmpS2078;
            uint64_t _M0L6_2atmpS2079;
            if (
              _M0L6_2atmpS2070 < 0
              || _M0L6_2atmpS2070 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2070] = _M0L6_2atmpS2071;
            _M0L6_2atmpS2078 = _M0L1iS726 + 1;
            _M0L6_2atmpS2079 = _M0L6outputS727 / 10ull;
            _M0L1iS726 = _M0L6_2atmpS2078;
            _M0L6outputS727 = _M0L6_2atmpS2079;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2080 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2080 + _M0L7olengthS706;
        _M0L6_2atmpS2085 = _M0Lm3expS707;
        _M0L7_2abindS729 = _M0L6_2atmpS2085 + 1;
        _M0L1iS730 = _M0L7olengthS706;
        while (1) {
          if (_M0L1iS730 < _M0L7_2abindS729) {
            int32_t _M0L6_2atmpS2083 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2082 = _M0L6_2atmpS2083 + _M0L1iS730;
            int32_t _M0L6_2atmpS2081 = _M0L6_2atmpS2082 - _M0L7olengthS706;
            int32_t _M0L6_2atmpS2084;
            if (
              _M0L6_2atmpS2081 < 0
              || _M0L6_2atmpS2081 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2081] = 48;
            _M0L6_2atmpS2084 = _M0L1iS730 + 1;
            _M0L1iS730 = _M0L6_2atmpS2084;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2086 = _M0Lm5indexS702;
        _M0L6_2atmpS2089 = _M0Lm3expS707;
        _M0L6_2atmpS2088 = _M0L6_2atmpS2089 + 1;
        _M0L6_2atmpS2087 = _M0L6_2atmpS2088 - _M0L7olengthS706;
        _M0Lm5indexS702 = _M0L6_2atmpS2086 + _M0L6_2atmpS2087;
      } else {
        int32_t _M0L6_2atmpS2106 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2105 = _M0L6_2atmpS2106 + 1;
        int32_t _M0L1iS732 = 0;
        int32_t _M0L7currentS733 = _M0L6_2atmpS2105;
        uint64_t _M0L6outputS734 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2107;
        int32_t _M0L6_2atmpS2108;
        while (1) {
          if (_M0L1iS732 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2101 = _M0L7olengthS706 - _M0L1iS732;
            int32_t _M0L6_2atmpS2099 = _M0L6_2atmpS2101 - 1;
            int32_t _M0L6_2atmpS2100 = _M0Lm3expS707;
            int32_t _M0L7currentS735;
            int32_t _M0L6_2atmpS2096;
            int32_t _M0L6_2atmpS2095;
            int32_t _M0L6_2atmpS2090;
            uint64_t _M0L6_2atmpS2094;
            int32_t _M0L6_2atmpS2093;
            int32_t _M0L6_2atmpS2092;
            int32_t _M0L6_2atmpS2091;
            int32_t _M0L6_2atmpS2097;
            uint64_t _M0L6_2atmpS2098;
            if (_M0L6_2atmpS2099 == _M0L6_2atmpS2100) {
              int32_t _M0L6_2atmpS2104 = _M0L7currentS733 + _M0L7olengthS706;
              int32_t _M0L6_2atmpS2103 = _M0L6_2atmpS2104 - _M0L1iS732;
              int32_t _M0L6_2atmpS2102 = _M0L6_2atmpS2103 - 1;
              if (
                _M0L6_2atmpS2102 < 0
                || _M0L6_2atmpS2102 >= Moonbit_array_length(_M0L6resultS701)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS701[_M0L6_2atmpS2102] = 46;
              _M0L7currentS735 = _M0L7currentS733 - 1;
            } else {
              _M0L7currentS735 = _M0L7currentS733;
            }
            _M0L6_2atmpS2096 = _M0L7currentS735 + _M0L7olengthS706;
            _M0L6_2atmpS2095 = _M0L6_2atmpS2096 - _M0L1iS732;
            _M0L6_2atmpS2090 = _M0L6_2atmpS2095 - 1;
            _M0L6_2atmpS2094 = _M0L6outputS734 % 10ull;
            _M0L6_2atmpS2093 = (int32_t)_M0L6_2atmpS2094;
            _M0L6_2atmpS2092 = 48 + _M0L6_2atmpS2093;
            _M0L6_2atmpS2091 = _M0L6_2atmpS2092 & 0xff;
            if (
              _M0L6_2atmpS2090 < 0
              || _M0L6_2atmpS2090 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2090] = _M0L6_2atmpS2091;
            _M0L6_2atmpS2097 = _M0L1iS732 + 1;
            _M0L6_2atmpS2098 = _M0L6outputS734 / 10ull;
            _M0L1iS732 = _M0L6_2atmpS2097;
            _M0L7currentS733 = _M0L7currentS735;
            _M0L6outputS734 = _M0L6_2atmpS2098;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2107 = _M0Lm5indexS702;
        _M0L6_2atmpS2108 = _M0L7olengthS706 + 1;
        _M0Lm5indexS702 = _M0L6_2atmpS2107 + _M0L6_2atmpS2108;
      }
    }
    _M0L6_2atmpS2109 = _M0Lm5indexS702;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2823
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS2109);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_2823;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS647,
  uint32_t _M0L12ieeeExponentS646
) {
  int32_t _M0Lm2e2S644;
  uint64_t _M0Lm2m2S645;
  uint64_t _M0L6_2atmpS1983;
  uint64_t _M0L6_2atmpS1982;
  int32_t _M0L4evenS648;
  uint64_t _M0L6_2atmpS1981;
  uint64_t _M0L2mvS649;
  int32_t _M0L7mmShiftS650;
  uint64_t _M0Lm2vrS651;
  uint64_t _M0Lm2vpS652;
  uint64_t _M0Lm2vmS653;
  int32_t _M0Lm3e10S654;
  int32_t _M0Lm17vmIsTrailingZerosS655;
  int32_t _M0Lm17vrIsTrailingZerosS656;
  int32_t _M0L6_2atmpS1883;
  int32_t _M0Lm7removedS675;
  int32_t _M0Lm16lastRemovedDigitS676;
  uint64_t _M0Lm6outputS677;
  int32_t _M0L6_2atmpS1979;
  int32_t _M0L6_2atmpS1980;
  int32_t _M0L3expS700;
  uint64_t _M0L6_2atmpS1978;
  struct _M0TPB17FloatingDecimal64* _block_2829;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S644 = 0;
  _M0Lm2m2S645 = 0ull;
  if (_M0L12ieeeExponentS646 == 0u) {
    _M0Lm2e2S644 = -1076;
    _M0Lm2m2S645 = _M0L12ieeeMantissaS647;
  } else {
    int32_t _M0L6_2atmpS1882 = *(int32_t*)&_M0L12ieeeExponentS646;
    int32_t _M0L6_2atmpS1881 = _M0L6_2atmpS1882 - 1023;
    int32_t _M0L6_2atmpS1880 = _M0L6_2atmpS1881 - 52;
    _M0Lm2e2S644 = _M0L6_2atmpS1880 - 2;
    _M0Lm2m2S645 = 4503599627370496ull | _M0L12ieeeMantissaS647;
  }
  _M0L6_2atmpS1983 = _M0Lm2m2S645;
  _M0L6_2atmpS1982 = _M0L6_2atmpS1983 & 1ull;
  _M0L4evenS648 = _M0L6_2atmpS1982 == 0ull;
  _M0L6_2atmpS1981 = _M0Lm2m2S645;
  _M0L2mvS649 = 4ull * _M0L6_2atmpS1981;
  _M0L7mmShiftS650
  = _M0L12ieeeMantissaS647 != 0ull || _M0L12ieeeExponentS646 <= 1u;
  _M0Lm2vrS651 = 0ull;
  _M0Lm2vpS652 = 0ull;
  _M0Lm2vmS653 = 0ull;
  _M0Lm3e10S654 = 0;
  _M0Lm17vmIsTrailingZerosS655 = 0;
  _M0Lm17vrIsTrailingZerosS656 = 0;
  _M0L6_2atmpS1883 = _M0Lm2e2S644;
  if (_M0L6_2atmpS1883 >= 0) {
    int32_t _M0L6_2atmpS1905 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1901;
    int32_t _M0L6_2atmpS1904;
    int32_t _M0L6_2atmpS1903;
    int32_t _M0L6_2atmpS1902;
    int32_t _M0L1qS657;
    int32_t _M0L6_2atmpS1900;
    int32_t _M0L6_2atmpS1899;
    int32_t _M0L1kS658;
    int32_t _M0L6_2atmpS1898;
    int32_t _M0L6_2atmpS1897;
    int32_t _M0L6_2atmpS1896;
    int32_t _M0L1iS659;
    struct _M0TPB8Pow5Pair _M0L4pow5S660;
    uint64_t _M0L6_2atmpS1895;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS661;
    uint64_t _M0L8_2avrOutS662;
    uint64_t _M0L8_2avpOutS663;
    uint64_t _M0L8_2avmOutS664;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1901 = _M0FPB9log10Pow2(_M0L6_2atmpS1905);
    _M0L6_2atmpS1904 = _M0Lm2e2S644;
    _M0L6_2atmpS1903 = _M0L6_2atmpS1904 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1902 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1903);
    _M0L1qS657 = _M0L6_2atmpS1901 - _M0L6_2atmpS1902;
    _M0Lm3e10S654 = _M0L1qS657;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1900 = _M0FPB8pow5bits(_M0L1qS657);
    _M0L6_2atmpS1899 = 125 + _M0L6_2atmpS1900;
    _M0L1kS658 = _M0L6_2atmpS1899 - 1;
    _M0L6_2atmpS1898 = _M0Lm2e2S644;
    _M0L6_2atmpS1897 = -_M0L6_2atmpS1898;
    _M0L6_2atmpS1896 = _M0L6_2atmpS1897 + _M0L1qS657;
    _M0L1iS659 = _M0L6_2atmpS1896 + _M0L1kS658;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S660 = _M0FPB22double__computeInvPow5(_M0L1qS657);
    _M0L6_2atmpS1895 = _M0Lm2m2S645;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS661
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1895, _M0L4pow5S660, _M0L1iS659, _M0L7mmShiftS650);
    _M0L8_2avrOutS662 = _M0L7_2abindS661.$0;
    _M0L8_2avpOutS663 = _M0L7_2abindS661.$1;
    _M0L8_2avmOutS664 = _M0L7_2abindS661.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS662;
    _M0Lm2vpS652 = _M0L8_2avpOutS663;
    _M0Lm2vmS653 = _M0L8_2avmOutS664;
    if (_M0L1qS657 <= 21) {
      int32_t _M0L6_2atmpS1891 = (int32_t)_M0L2mvS649;
      uint64_t _M0L6_2atmpS1894 = _M0L2mvS649 / 5ull;
      int32_t _M0L6_2atmpS1893 = (int32_t)_M0L6_2atmpS1894;
      int32_t _M0L6_2atmpS1892 = 5 * _M0L6_2atmpS1893;
      int32_t _M0L6mvMod5S665 = _M0L6_2atmpS1891 - _M0L6_2atmpS1892;
      if (_M0L6mvMod5S665 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS656
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS649, _M0L1qS657);
      } else if (_M0L4evenS648) {
        uint64_t _M0L6_2atmpS1885 = _M0L2mvS649 - 1ull;
        uint64_t _M0L6_2atmpS1886;
        uint64_t _M0L6_2atmpS1884;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1886 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS650);
        _M0L6_2atmpS1884 = _M0L6_2atmpS1885 - _M0L6_2atmpS1886;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1884, _M0L1qS657);
      } else {
        uint64_t _M0L6_2atmpS1887 = _M0Lm2vpS652;
        uint64_t _M0L6_2atmpS1890 = _M0L2mvS649 + 2ull;
        int32_t _M0L6_2atmpS1889;
        uint64_t _M0L6_2atmpS1888;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1889
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1890, _M0L1qS657);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1888 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1889);
        _M0Lm2vpS652 = _M0L6_2atmpS1887 - _M0L6_2atmpS1888;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1919 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1918 = -_M0L6_2atmpS1919;
    int32_t _M0L6_2atmpS1913;
    int32_t _M0L6_2atmpS1917;
    int32_t _M0L6_2atmpS1916;
    int32_t _M0L6_2atmpS1915;
    int32_t _M0L6_2atmpS1914;
    int32_t _M0L1qS666;
    int32_t _M0L6_2atmpS1906;
    int32_t _M0L6_2atmpS1912;
    int32_t _M0L6_2atmpS1911;
    int32_t _M0L1iS667;
    int32_t _M0L6_2atmpS1910;
    int32_t _M0L1kS668;
    int32_t _M0L1jS669;
    struct _M0TPB8Pow5Pair _M0L4pow5S670;
    uint64_t _M0L6_2atmpS1909;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS671;
    uint64_t _M0L8_2avrOutS672;
    uint64_t _M0L8_2avpOutS673;
    uint64_t _M0L8_2avmOutS674;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1913 = _M0FPB9log10Pow5(_M0L6_2atmpS1918);
    _M0L6_2atmpS1917 = _M0Lm2e2S644;
    _M0L6_2atmpS1916 = -_M0L6_2atmpS1917;
    _M0L6_2atmpS1915 = _M0L6_2atmpS1916 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1914 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1915);
    _M0L1qS666 = _M0L6_2atmpS1913 - _M0L6_2atmpS1914;
    _M0L6_2atmpS1906 = _M0Lm2e2S644;
    _M0Lm3e10S654 = _M0L1qS666 + _M0L6_2atmpS1906;
    _M0L6_2atmpS1912 = _M0Lm2e2S644;
    _M0L6_2atmpS1911 = -_M0L6_2atmpS1912;
    _M0L1iS667 = _M0L6_2atmpS1911 - _M0L1qS666;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1910 = _M0FPB8pow5bits(_M0L1iS667);
    _M0L1kS668 = _M0L6_2atmpS1910 - 125;
    _M0L1jS669 = _M0L1qS666 - _M0L1kS668;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S670 = _M0FPB19double__computePow5(_M0L1iS667);
    _M0L6_2atmpS1909 = _M0Lm2m2S645;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS671
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1909, _M0L4pow5S670, _M0L1jS669, _M0L7mmShiftS650);
    _M0L8_2avrOutS672 = _M0L7_2abindS671.$0;
    _M0L8_2avpOutS673 = _M0L7_2abindS671.$1;
    _M0L8_2avmOutS674 = _M0L7_2abindS671.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS672;
    _M0Lm2vpS652 = _M0L8_2avpOutS673;
    _M0Lm2vmS653 = _M0L8_2avmOutS674;
    if (_M0L1qS666 <= 1) {
      _M0Lm17vrIsTrailingZerosS656 = 1;
      if (_M0L4evenS648) {
        int32_t _M0L6_2atmpS1907;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1907 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS650);
        _M0Lm17vmIsTrailingZerosS655 = _M0L6_2atmpS1907 == 1;
      } else {
        uint64_t _M0L6_2atmpS1908 = _M0Lm2vpS652;
        _M0Lm2vpS652 = _M0L6_2atmpS1908 - 1ull;
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
    int32_t _if__result_2826;
    uint64_t _M0L6_2atmpS1949;
    uint64_t _M0L6_2atmpS1955;
    uint64_t _M0L6_2atmpS1956;
    int32_t _if__result_2827;
    int32_t _M0L6_2atmpS1952;
    int64_t _M0L6_2atmpS1951;
    uint64_t _M0L6_2atmpS1950;
    while (1) {
      uint64_t _M0L6_2atmpS1932 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S678 = _M0L6_2atmpS1932 / 10ull;
      uint64_t _M0L6_2atmpS1931 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S679 = _M0L6_2atmpS1931 / 10ull;
      uint64_t _M0L6_2atmpS1930;
      int32_t _M0L6_2atmpS1927;
      int32_t _M0L6_2atmpS1929;
      int32_t _M0L6_2atmpS1928;
      int32_t _M0L7vmMod10S681;
      uint64_t _M0L6_2atmpS1926;
      uint64_t _M0L7vrDiv10S682;
      uint64_t _M0L6_2atmpS1925;
      int32_t _M0L6_2atmpS1922;
      int32_t _M0L6_2atmpS1924;
      int32_t _M0L6_2atmpS1923;
      int32_t _M0L7vrMod10S683;
      int32_t _M0L6_2atmpS1921;
      if (_M0L7vpDiv10S678 <= _M0L7vmDiv10S679) {
        break;
      }
      _M0L6_2atmpS1930 = _M0Lm2vmS653;
      _M0L6_2atmpS1927 = (int32_t)_M0L6_2atmpS1930;
      _M0L6_2atmpS1929 = (int32_t)_M0L7vmDiv10S679;
      _M0L6_2atmpS1928 = 10 * _M0L6_2atmpS1929;
      _M0L7vmMod10S681 = _M0L6_2atmpS1927 - _M0L6_2atmpS1928;
      _M0L6_2atmpS1926 = _M0Lm2vrS651;
      _M0L7vrDiv10S682 = _M0L6_2atmpS1926 / 10ull;
      _M0L6_2atmpS1925 = _M0Lm2vrS651;
      _M0L6_2atmpS1922 = (int32_t)_M0L6_2atmpS1925;
      _M0L6_2atmpS1924 = (int32_t)_M0L7vrDiv10S682;
      _M0L6_2atmpS1923 = 10 * _M0L6_2atmpS1924;
      _M0L7vrMod10S683 = _M0L6_2atmpS1922 - _M0L6_2atmpS1923;
      _M0Lm17vmIsTrailingZerosS655
      = _M0Lm17vmIsTrailingZerosS655 && _M0L7vmMod10S681 == 0;
      if (_M0Lm17vrIsTrailingZerosS656) {
        int32_t _M0L6_2atmpS1920 = _M0Lm16lastRemovedDigitS676;
        _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS1920 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS656 = 0;
      }
      _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S683;
      _M0Lm2vrS651 = _M0L7vrDiv10S682;
      _M0Lm2vpS652 = _M0L7vpDiv10S678;
      _M0Lm2vmS653 = _M0L7vmDiv10S679;
      _M0L6_2atmpS1921 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1921 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS655) {
      while (1) {
        uint64_t _M0L6_2atmpS1945 = _M0Lm2vmS653;
        uint64_t _M0L7vmDiv10S684 = _M0L6_2atmpS1945 / 10ull;
        uint64_t _M0L6_2atmpS1944 = _M0Lm2vmS653;
        int32_t _M0L6_2atmpS1941 = (int32_t)_M0L6_2atmpS1944;
        int32_t _M0L6_2atmpS1943 = (int32_t)_M0L7vmDiv10S684;
        int32_t _M0L6_2atmpS1942 = 10 * _M0L6_2atmpS1943;
        int32_t _M0L7vmMod10S685 = _M0L6_2atmpS1941 - _M0L6_2atmpS1942;
        uint64_t _M0L6_2atmpS1940;
        uint64_t _M0L7vpDiv10S687;
        uint64_t _M0L6_2atmpS1939;
        uint64_t _M0L7vrDiv10S688;
        uint64_t _M0L6_2atmpS1938;
        int32_t _M0L6_2atmpS1935;
        int32_t _M0L6_2atmpS1937;
        int32_t _M0L6_2atmpS1936;
        int32_t _M0L7vrMod10S689;
        int32_t _M0L6_2atmpS1934;
        if (_M0L7vmMod10S685 != 0) {
          break;
        }
        _M0L6_2atmpS1940 = _M0Lm2vpS652;
        _M0L7vpDiv10S687 = _M0L6_2atmpS1940 / 10ull;
        _M0L6_2atmpS1939 = _M0Lm2vrS651;
        _M0L7vrDiv10S688 = _M0L6_2atmpS1939 / 10ull;
        _M0L6_2atmpS1938 = _M0Lm2vrS651;
        _M0L6_2atmpS1935 = (int32_t)_M0L6_2atmpS1938;
        _M0L6_2atmpS1937 = (int32_t)_M0L7vrDiv10S688;
        _M0L6_2atmpS1936 = 10 * _M0L6_2atmpS1937;
        _M0L7vrMod10S689 = _M0L6_2atmpS1935 - _M0L6_2atmpS1936;
        if (_M0Lm17vrIsTrailingZerosS656) {
          int32_t _M0L6_2atmpS1933 = _M0Lm16lastRemovedDigitS676;
          _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS1933 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS656 = 0;
        }
        _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S689;
        _M0Lm2vrS651 = _M0L7vrDiv10S688;
        _M0Lm2vpS652 = _M0L7vpDiv10S687;
        _M0Lm2vmS653 = _M0L7vmDiv10S684;
        _M0L6_2atmpS1934 = _M0Lm7removedS675;
        _M0Lm7removedS675 = _M0L6_2atmpS1934 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS656) {
      int32_t _M0L6_2atmpS1948 = _M0Lm16lastRemovedDigitS676;
      if (_M0L6_2atmpS1948 == 5) {
        uint64_t _M0L6_2atmpS1947 = _M0Lm2vrS651;
        uint64_t _M0L6_2atmpS1946 = _M0L6_2atmpS1947 % 2ull;
        _if__result_2826 = _M0L6_2atmpS1946 == 0ull;
      } else {
        _if__result_2826 = 0;
      }
    } else {
      _if__result_2826 = 0;
    }
    if (_if__result_2826) {
      _M0Lm16lastRemovedDigitS676 = 4;
    }
    _M0L6_2atmpS1949 = _M0Lm2vrS651;
    _M0L6_2atmpS1955 = _M0Lm2vrS651;
    _M0L6_2atmpS1956 = _M0Lm2vmS653;
    if (_M0L6_2atmpS1955 == _M0L6_2atmpS1956) {
      if (!_M0L4evenS648) {
        _if__result_2827 = 1;
      } else {
        int32_t _M0L6_2atmpS1954 = _M0Lm17vmIsTrailingZerosS655;
        _if__result_2827 = !_M0L6_2atmpS1954;
      }
    } else {
      _if__result_2827 = 0;
    }
    if (_if__result_2827) {
      _M0L6_2atmpS1952 = 1;
    } else {
      int32_t _M0L6_2atmpS1953 = _M0Lm16lastRemovedDigitS676;
      _M0L6_2atmpS1952 = _M0L6_2atmpS1953 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1951 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1952);
    _M0L6_2atmpS1950 = *(uint64_t*)&_M0L6_2atmpS1951;
    _M0Lm6outputS677 = _M0L6_2atmpS1949 + _M0L6_2atmpS1950;
  } else {
    int32_t _M0Lm7roundUpS690 = 0;
    uint64_t _M0L6_2atmpS1977 = _M0Lm2vpS652;
    uint64_t _M0L8vpDiv100S691 = _M0L6_2atmpS1977 / 100ull;
    uint64_t _M0L6_2atmpS1976 = _M0Lm2vmS653;
    uint64_t _M0L8vmDiv100S692 = _M0L6_2atmpS1976 / 100ull;
    uint64_t _M0L6_2atmpS1971;
    uint64_t _M0L6_2atmpS1974;
    uint64_t _M0L6_2atmpS1975;
    int32_t _M0L6_2atmpS1973;
    uint64_t _M0L6_2atmpS1972;
    if (_M0L8vpDiv100S691 > _M0L8vmDiv100S692) {
      uint64_t _M0L6_2atmpS1962 = _M0Lm2vrS651;
      uint64_t _M0L8vrDiv100S693 = _M0L6_2atmpS1962 / 100ull;
      uint64_t _M0L6_2atmpS1961 = _M0Lm2vrS651;
      int32_t _M0L6_2atmpS1958 = (int32_t)_M0L6_2atmpS1961;
      int32_t _M0L6_2atmpS1960 = (int32_t)_M0L8vrDiv100S693;
      int32_t _M0L6_2atmpS1959 = 100 * _M0L6_2atmpS1960;
      int32_t _M0L8vrMod100S694 = _M0L6_2atmpS1958 - _M0L6_2atmpS1959;
      int32_t _M0L6_2atmpS1957;
      _M0Lm7roundUpS690 = _M0L8vrMod100S694 >= 50;
      _M0Lm2vrS651 = _M0L8vrDiv100S693;
      _M0Lm2vpS652 = _M0L8vpDiv100S691;
      _M0Lm2vmS653 = _M0L8vmDiv100S692;
      _M0L6_2atmpS1957 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1957 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1970 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S695 = _M0L6_2atmpS1970 / 10ull;
      uint64_t _M0L6_2atmpS1969 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S696 = _M0L6_2atmpS1969 / 10ull;
      uint64_t _M0L6_2atmpS1968;
      uint64_t _M0L7vrDiv10S698;
      uint64_t _M0L6_2atmpS1967;
      int32_t _M0L6_2atmpS1964;
      int32_t _M0L6_2atmpS1966;
      int32_t _M0L6_2atmpS1965;
      int32_t _M0L7vrMod10S699;
      int32_t _M0L6_2atmpS1963;
      if (_M0L7vpDiv10S695 <= _M0L7vmDiv10S696) {
        break;
      }
      _M0L6_2atmpS1968 = _M0Lm2vrS651;
      _M0L7vrDiv10S698 = _M0L6_2atmpS1968 / 10ull;
      _M0L6_2atmpS1967 = _M0Lm2vrS651;
      _M0L6_2atmpS1964 = (int32_t)_M0L6_2atmpS1967;
      _M0L6_2atmpS1966 = (int32_t)_M0L7vrDiv10S698;
      _M0L6_2atmpS1965 = 10 * _M0L6_2atmpS1966;
      _M0L7vrMod10S699 = _M0L6_2atmpS1964 - _M0L6_2atmpS1965;
      _M0Lm7roundUpS690 = _M0L7vrMod10S699 >= 5;
      _M0Lm2vrS651 = _M0L7vrDiv10S698;
      _M0Lm2vpS652 = _M0L7vpDiv10S695;
      _M0Lm2vmS653 = _M0L7vmDiv10S696;
      _M0L6_2atmpS1963 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1963 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1971 = _M0Lm2vrS651;
    _M0L6_2atmpS1974 = _M0Lm2vrS651;
    _M0L6_2atmpS1975 = _M0Lm2vmS653;
    _M0L6_2atmpS1973
    = _M0L6_2atmpS1974 == _M0L6_2atmpS1975 || _M0Lm7roundUpS690;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1972 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1973);
    _M0Lm6outputS677 = _M0L6_2atmpS1971 + _M0L6_2atmpS1972;
  }
  _M0L6_2atmpS1979 = _M0Lm3e10S654;
  _M0L6_2atmpS1980 = _M0Lm7removedS675;
  _M0L3expS700 = _M0L6_2atmpS1979 + _M0L6_2atmpS1980;
  _M0L6_2atmpS1978 = _M0Lm6outputS677;
  _block_2829
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2829)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2829->$0 = _M0L6_2atmpS1978;
  _block_2829->$1 = _M0L3expS700;
  return _block_2829;
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
  int32_t _M0L6_2atmpS1879;
  int32_t _M0L6_2atmpS1878;
  int32_t _M0L4baseS622;
  int32_t _M0L5base2S624;
  int32_t _M0L6offsetS625;
  int32_t _M0L6_2atmpS1877;
  uint64_t _M0L4mul0S626;
  int32_t _M0L6_2atmpS1876;
  int32_t _M0L6_2atmpS1875;
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
  int32_t _M0L6_2atmpS1873;
  int32_t _M0L6_2atmpS1874;
  int32_t _M0L5deltaS637;
  uint64_t _M0L6_2atmpS1872;
  uint64_t _M0L6_2atmpS1864;
  int32_t _M0L6_2atmpS1871;
  uint32_t _M0L6_2atmpS1868;
  int32_t _M0L6_2atmpS1870;
  int32_t _M0L6_2atmpS1869;
  uint32_t _M0L6_2atmpS1867;
  uint32_t _M0L6_2atmpS1866;
  uint64_t _M0L6_2atmpS1865;
  uint64_t _M0L1aS638;
  uint64_t _M0L6_2atmpS1863;
  uint64_t _M0L1bS639;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1879 = _M0L1iS623 + 26;
  _M0L6_2atmpS1878 = _M0L6_2atmpS1879 - 1;
  _M0L4baseS622 = _M0L6_2atmpS1878 / 26;
  _M0L5base2S624 = _M0L4baseS622 * 26;
  _M0L6offsetS625 = _M0L5base2S624 - _M0L1iS623;
  _M0L6_2atmpS1877 = _M0L4baseS622 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1877);
  _M0L6_2atmpS1876 = _M0L4baseS622 * 2;
  _M0L6_2atmpS1875 = _M0L6_2atmpS1876 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1875);
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
    uint64_t _M0L6_2atmpS1862 = _M0Lm5high1S636;
    _M0Lm5high1S636 = _M0L6_2atmpS1862 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1873 = _M0FPB8pow5bits(_M0L5base2S624);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1874 = _M0FPB8pow5bits(_M0L1iS623);
  _M0L5deltaS637 = _M0L6_2atmpS1873 - _M0L6_2atmpS1874;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1872
  = _M0FPB13shiftright128(_M0L7_2alow0S633, _M0L3sumS635, _M0L5deltaS637);
  _M0L6_2atmpS1864 = _M0L6_2atmpS1872 + 1ull;
  _M0L6_2atmpS1871 = _M0L1iS623 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1868
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1871);
  _M0L6_2atmpS1870 = _M0L1iS623 % 16;
  _M0L6_2atmpS1869 = _M0L6_2atmpS1870 << 1;
  _M0L6_2atmpS1867 = _M0L6_2atmpS1868 >> (_M0L6_2atmpS1869 & 31);
  _M0L6_2atmpS1866 = _M0L6_2atmpS1867 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1865 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1866);
  _M0L1aS638 = _M0L6_2atmpS1864 + _M0L6_2atmpS1865;
  _M0L6_2atmpS1863 = _M0Lm5high1S636;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS639
  = _M0FPB13shiftright128(_M0L3sumS635, _M0L6_2atmpS1863, _M0L5deltaS637);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS638, .$1 = _M0L1bS639};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS605) {
  int32_t _M0L4baseS604;
  int32_t _M0L5base2S606;
  int32_t _M0L6offsetS607;
  int32_t _M0L6_2atmpS1861;
  uint64_t _M0L4mul0S608;
  int32_t _M0L6_2atmpS1860;
  int32_t _M0L6_2atmpS1859;
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
  int32_t _M0L6_2atmpS1857;
  int32_t _M0L6_2atmpS1858;
  int32_t _M0L5deltaS619;
  uint64_t _M0L6_2atmpS1849;
  int32_t _M0L6_2atmpS1856;
  uint32_t _M0L6_2atmpS1853;
  int32_t _M0L6_2atmpS1855;
  int32_t _M0L6_2atmpS1854;
  uint32_t _M0L6_2atmpS1852;
  uint32_t _M0L6_2atmpS1851;
  uint64_t _M0L6_2atmpS1850;
  uint64_t _M0L1aS620;
  uint64_t _M0L6_2atmpS1848;
  uint64_t _M0L1bS621;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS604 = _M0L1iS605 / 26;
  _M0L5base2S606 = _M0L4baseS604 * 26;
  _M0L6offsetS607 = _M0L1iS605 - _M0L5base2S606;
  _M0L6_2atmpS1861 = _M0L4baseS604 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1861);
  _M0L6_2atmpS1860 = _M0L4baseS604 * 2;
  _M0L6_2atmpS1859 = _M0L6_2atmpS1860 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1859);
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
    uint64_t _M0L6_2atmpS1847 = _M0Lm5high1S618;
    _M0Lm5high1S618 = _M0L6_2atmpS1847 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1857 = _M0FPB8pow5bits(_M0L1iS605);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1858 = _M0FPB8pow5bits(_M0L5base2S606);
  _M0L5deltaS619 = _M0L6_2atmpS1857 - _M0L6_2atmpS1858;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1849
  = _M0FPB13shiftright128(_M0L7_2alow0S615, _M0L3sumS617, _M0L5deltaS619);
  _M0L6_2atmpS1856 = _M0L1iS605 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1853
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1856);
  _M0L6_2atmpS1855 = _M0L1iS605 % 16;
  _M0L6_2atmpS1854 = _M0L6_2atmpS1855 << 1;
  _M0L6_2atmpS1852 = _M0L6_2atmpS1853 >> (_M0L6_2atmpS1854 & 31);
  _M0L6_2atmpS1851 = _M0L6_2atmpS1852 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1850 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1851);
  _M0L1aS620 = _M0L6_2atmpS1849 + _M0L6_2atmpS1850;
  _M0L6_2atmpS1848 = _M0Lm5high1S618;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS621
  = _M0FPB13shiftright128(_M0L3sumS617, _M0L6_2atmpS1848, _M0L5deltaS619);
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
  uint64_t _M0L6_2atmpS1846;
  uint64_t _M0L2hiS586;
  uint64_t _M0L3lo2S587;
  uint64_t _M0L6_2atmpS1844;
  uint64_t _M0L6_2atmpS1845;
  uint64_t _M0L4mid2S588;
  uint64_t _M0L6_2atmpS1843;
  uint64_t _M0L3hi2S589;
  int32_t _M0L6_2atmpS1842;
  int32_t _M0L6_2atmpS1841;
  uint64_t _M0L2vpS590;
  uint64_t _M0Lm2vmS592;
  int32_t _M0L6_2atmpS1840;
  int32_t _M0L6_2atmpS1839;
  uint64_t _M0L2vrS603;
  uint64_t _M0L6_2atmpS1838;
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
    _M0L6_2atmpS1846 = 1ull;
  } else {
    _M0L6_2atmpS1846 = 0ull;
  }
  _M0L2hiS586 = _M0L6_2ahi2S584 + _M0L6_2atmpS1846;
  _M0L3lo2S587 = _M0L5_2aloS580 + _M0L7_2amul0S574;
  _M0L6_2atmpS1844 = _M0L3midS585 + _M0L7_2amul1S576;
  if (_M0L3lo2S587 < _M0L5_2aloS580) {
    _M0L6_2atmpS1845 = 1ull;
  } else {
    _M0L6_2atmpS1845 = 0ull;
  }
  _M0L4mid2S588 = _M0L6_2atmpS1844 + _M0L6_2atmpS1845;
  if (_M0L4mid2S588 < _M0L3midS585) {
    _M0L6_2atmpS1843 = 1ull;
  } else {
    _M0L6_2atmpS1843 = 0ull;
  }
  _M0L3hi2S589 = _M0L2hiS586 + _M0L6_2atmpS1843;
  _M0L6_2atmpS1842 = _M0L1jS591 - 64;
  _M0L6_2atmpS1841 = _M0L6_2atmpS1842 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS590
  = _M0FPB13shiftright128(_M0L4mid2S588, _M0L3hi2S589, _M0L6_2atmpS1841);
  _M0Lm2vmS592 = 0ull;
  if (_M0L7mmShiftS593) {
    uint64_t _M0L3lo3S594 = _M0L5_2aloS580 - _M0L7_2amul0S574;
    uint64_t _M0L6_2atmpS1828 = _M0L3midS585 - _M0L7_2amul1S576;
    uint64_t _M0L6_2atmpS1829;
    uint64_t _M0L4mid3S595;
    uint64_t _M0L6_2atmpS1827;
    uint64_t _M0L3hi3S596;
    int32_t _M0L6_2atmpS1826;
    int32_t _M0L6_2atmpS1825;
    if (_M0L5_2aloS580 < _M0L3lo3S594) {
      _M0L6_2atmpS1829 = 1ull;
    } else {
      _M0L6_2atmpS1829 = 0ull;
    }
    _M0L4mid3S595 = _M0L6_2atmpS1828 - _M0L6_2atmpS1829;
    if (_M0L3midS585 < _M0L4mid3S595) {
      _M0L6_2atmpS1827 = 1ull;
    } else {
      _M0L6_2atmpS1827 = 0ull;
    }
    _M0L3hi3S596 = _M0L2hiS586 - _M0L6_2atmpS1827;
    _M0L6_2atmpS1826 = _M0L1jS591 - 64;
    _M0L6_2atmpS1825 = _M0L6_2atmpS1826 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid3S595, _M0L3hi3S596, _M0L6_2atmpS1825);
  } else {
    uint64_t _M0L3lo3S597 = _M0L5_2aloS580 + _M0L5_2aloS580;
    uint64_t _M0L6_2atmpS1836 = _M0L3midS585 + _M0L3midS585;
    uint64_t _M0L6_2atmpS1837;
    uint64_t _M0L4mid3S598;
    uint64_t _M0L6_2atmpS1834;
    uint64_t _M0L6_2atmpS1835;
    uint64_t _M0L3hi3S599;
    uint64_t _M0L3lo4S600;
    uint64_t _M0L6_2atmpS1832;
    uint64_t _M0L6_2atmpS1833;
    uint64_t _M0L4mid4S601;
    uint64_t _M0L6_2atmpS1831;
    uint64_t _M0L3hi4S602;
    int32_t _M0L6_2atmpS1830;
    if (_M0L3lo3S597 < _M0L5_2aloS580) {
      _M0L6_2atmpS1837 = 1ull;
    } else {
      _M0L6_2atmpS1837 = 0ull;
    }
    _M0L4mid3S598 = _M0L6_2atmpS1836 + _M0L6_2atmpS1837;
    _M0L6_2atmpS1834 = _M0L2hiS586 + _M0L2hiS586;
    if (_M0L4mid3S598 < _M0L3midS585) {
      _M0L6_2atmpS1835 = 1ull;
    } else {
      _M0L6_2atmpS1835 = 0ull;
    }
    _M0L3hi3S599 = _M0L6_2atmpS1834 + _M0L6_2atmpS1835;
    _M0L3lo4S600 = _M0L3lo3S597 - _M0L7_2amul0S574;
    _M0L6_2atmpS1832 = _M0L4mid3S598 - _M0L7_2amul1S576;
    if (_M0L3lo3S597 < _M0L3lo4S600) {
      _M0L6_2atmpS1833 = 1ull;
    } else {
      _M0L6_2atmpS1833 = 0ull;
    }
    _M0L4mid4S601 = _M0L6_2atmpS1832 - _M0L6_2atmpS1833;
    if (_M0L4mid3S598 < _M0L4mid4S601) {
      _M0L6_2atmpS1831 = 1ull;
    } else {
      _M0L6_2atmpS1831 = 0ull;
    }
    _M0L3hi4S602 = _M0L3hi3S599 - _M0L6_2atmpS1831;
    _M0L6_2atmpS1830 = _M0L1jS591 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid4S601, _M0L3hi4S602, _M0L6_2atmpS1830);
  }
  _M0L6_2atmpS1840 = _M0L1jS591 - 64;
  _M0L6_2atmpS1839 = _M0L6_2atmpS1840 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS603
  = _M0FPB13shiftright128(_M0L3midS585, _M0L2hiS586, _M0L6_2atmpS1839);
  _M0L6_2atmpS1838 = _M0Lm2vmS592;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS603,
                                                .$1 = _M0L2vpS590,
                                                .$2 = _M0L6_2atmpS1838};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS572,
  int32_t _M0L1pS573
) {
  uint64_t _M0L6_2atmpS1824;
  uint64_t _M0L6_2atmpS1823;
  uint64_t _M0L6_2atmpS1822;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1824 = 1ull << (_M0L1pS573 & 63);
  _M0L6_2atmpS1823 = _M0L6_2atmpS1824 - 1ull;
  _M0L6_2atmpS1822 = _M0L5valueS572 & _M0L6_2atmpS1823;
  return _M0L6_2atmpS1822 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS570,
  int32_t _M0L1pS571
) {
  int32_t _M0L6_2atmpS1821;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1821 = _M0FPB10pow5Factor(_M0L5valueS570);
  return _M0L6_2atmpS1821 >= _M0L1pS571;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS565) {
  uint64_t _M0L6_2atmpS1812;
  uint64_t _M0L6_2atmpS1813;
  uint64_t _M0L6_2atmpS1814;
  uint64_t _M0L6_2atmpS1815;
  uint64_t _M0L6_2atmpS1820;
  int32_t _M0L5countS566;
  uint64_t _M0L1vS567;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1812 = _M0L5valueS565 % 5ull;
  if (_M0L6_2atmpS1812 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1813 = _M0L5valueS565 % 25ull;
  if (_M0L6_2atmpS1813 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1814 = _M0L5valueS565 % 125ull;
  if (_M0L6_2atmpS1814 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1815 = _M0L5valueS565 % 625ull;
  if (_M0L6_2atmpS1815 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1820 = _M0L5valueS565 / 625ull;
  _M0L5countS566 = 4;
  _M0L1vS567 = _M0L6_2atmpS1820;
  while (1) {
    if (_M0L1vS567 > 0ull) {
      uint64_t _M0L6_2atmpS1816 = _M0L1vS567 % 5ull;
      int32_t _M0L6_2atmpS1817;
      uint64_t _M0L6_2atmpS1818;
      if (_M0L6_2atmpS1816 != 0ull) {
        return _M0L5countS566;
      }
      _M0L6_2atmpS1817 = _M0L5countS566 + 1;
      _M0L6_2atmpS1818 = _M0L1vS567 / 5ull;
      _M0L5countS566 = _M0L6_2atmpS1817;
      _M0L1vS567 = _M0L6_2atmpS1818;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS569;
      moonbit_string_t _M0L6_2atmpS1819;
      int32_t _result_2831;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS569
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS569, (moonbit_string_t)moonbit_string_literal_12.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS569, _M0L5valueS565);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1819
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS569);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS569);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2831 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1819);
      moonbit_decref_cycle_free(_M0L6_2atmpS1819);
      return _result_2831;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS564,
  uint64_t _M0L2hiS562,
  int32_t _M0L4distS563
) {
  int32_t _M0L6_2atmpS1811;
  uint64_t _M0L6_2atmpS1809;
  uint64_t _M0L6_2atmpS1810;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1811 = 64 - _M0L4distS563;
  _M0L6_2atmpS1809 = _M0L2hiS562 << (_M0L6_2atmpS1811 & 63);
  _M0L6_2atmpS1810 = _M0L2loS564 >> (_M0L4distS563 & 63);
  return _M0L6_2atmpS1809 | _M0L6_2atmpS1810;
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
  uint64_t _M0L6_2atmpS1807;
  uint64_t _M0L6_2atmpS1808;
  uint64_t _M0L1yS558;
  uint64_t _M0L6_2atmpS1805;
  uint64_t _M0L6_2atmpS1806;
  uint64_t _M0L1zS559;
  uint64_t _M0L6_2atmpS1803;
  uint64_t _M0L6_2atmpS1804;
  uint64_t _M0L6_2atmpS1801;
  uint64_t _M0L6_2atmpS1802;
  uint64_t _M0L1wS560;
  uint64_t _M0L2loS561;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS551 = _M0L1aS552 & 4294967295ull;
  _M0L3aHiS553 = _M0L1aS552 >> 32;
  _M0L3bLoS554 = _M0L1bS555 & 4294967295ull;
  _M0L3bHiS556 = _M0L1bS555 >> 32;
  _M0L1xS557 = _M0L3aLoS551 * _M0L3bLoS554;
  _M0L6_2atmpS1807 = _M0L3aHiS553 * _M0L3bLoS554;
  _M0L6_2atmpS1808 = _M0L1xS557 >> 32;
  _M0L1yS558 = _M0L6_2atmpS1807 + _M0L6_2atmpS1808;
  _M0L6_2atmpS1805 = _M0L3aLoS551 * _M0L3bHiS556;
  _M0L6_2atmpS1806 = _M0L1yS558 & 4294967295ull;
  _M0L1zS559 = _M0L6_2atmpS1805 + _M0L6_2atmpS1806;
  _M0L6_2atmpS1803 = _M0L3aHiS553 * _M0L3bHiS556;
  _M0L6_2atmpS1804 = _M0L1yS558 >> 32;
  _M0L6_2atmpS1801 = _M0L6_2atmpS1803 + _M0L6_2atmpS1804;
  _M0L6_2atmpS1802 = _M0L1zS559 >> 32;
  _M0L1wS560 = _M0L6_2atmpS1801 + _M0L6_2atmpS1802;
  _M0L2loS561 = _M0L1aS552 * _M0L1bS555;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS561, .$1 = _M0L1wS560};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS549,
  int32_t _M0L4fromS546,
  int32_t _M0L2toS545
) {
  int32_t _M0L3lenS544;
  int32_t _M0L6_2atmpS1800;
  uint16_t* _M0L6bufferS547;
  int32_t _M0L1iS548;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS544 = _M0L2toS545 - _M0L4fromS546;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1800 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS547
  = (uint16_t*)moonbit_make_string(_M0L3lenS544, _M0L6_2atmpS1800);
  _M0L1iS548 = 0;
  while (1) {
    if (_M0L1iS548 < _M0L3lenS544) {
      int32_t _M0L6_2atmpS1798 = _M0L4fromS546 + _M0L1iS548;
      int32_t _M0L6_2atmpS1797;
      int32_t _M0L6_2atmpS1796;
      int32_t _M0L6_2atmpS1799;
      if (
        _M0L6_2atmpS1798 < 0
        || _M0L6_2atmpS1798 >= Moonbit_array_length(_M0L5bytesS549)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1797 = (int32_t)_M0L5bytesS549[_M0L6_2atmpS1798];
      _M0L6_2atmpS1796 = (uint16_t)_M0L6_2atmpS1797;
      if (
        _M0L1iS548 < 0 || _M0L1iS548 >= Moonbit_array_length(_M0L6bufferS547)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS547[_M0L1iS548] = _M0L6_2atmpS1796;
      _M0L6_2atmpS1799 = _M0L1iS548 + 1;
      _M0L1iS548 = _M0L6_2atmpS1799;
      continue;
    }
    break;
  }
  return _M0L6bufferS547;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS543) {
  int32_t _M0L6_2atmpS1795;
  uint32_t _M0L6_2atmpS1794;
  uint32_t _M0L6_2atmpS1793;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1795 = _M0L1eS543 * 78913;
  _M0L6_2atmpS1794 = *(uint32_t*)&_M0L6_2atmpS1795;
  _M0L6_2atmpS1793 = _M0L6_2atmpS1794 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1793;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS1792;
  uint32_t _M0L6_2atmpS1791;
  uint32_t _M0L6_2atmpS1790;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1792 = _M0L1eS542 * 732923;
  _M0L6_2atmpS1791 = *(uint32_t*)&_M0L6_2atmpS1792;
  _M0L6_2atmpS1790 = _M0L6_2atmpS1791 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1790;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS540,
  int32_t _M0L8exponentS541,
  int32_t _M0L8mantissaS538
) {
  moonbit_string_t _M0L1sS539;
  moonbit_string_t _result_2834;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS538) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  if (_M0L4signS540) {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_14.data;
  } else {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS541) {
    moonbit_string_t _result_2833;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2833
    = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_15.data);
    moonbit_decref_cycle_free(_M0L1sS539);
    return _result_2833;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2834
  = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_16.data);
  moonbit_decref_cycle_free(_M0L1sS539);
  return _result_2834;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS537) {
  int32_t _M0L6_2atmpS1789;
  uint32_t _M0L6_2atmpS1788;
  uint32_t _M0L6_2atmpS1787;
  int32_t _M0L6_2atmpS1786;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1789 = _M0L1eS537 * 1217359;
  _M0L6_2atmpS1788 = *(uint32_t*)&_M0L6_2atmpS1789;
  _M0L6_2atmpS1787 = _M0L6_2atmpS1788 >> 19;
  _M0L6_2atmpS1786 = *(int32_t*)&_M0L6_2atmpS1787;
  return _M0L6_2atmpS1786 + 1;
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
  float* _M0L6_2atmpS1782;
  struct _M0TPB5ArrayGfE* _block_2835;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1782 = (float*)moonbit_make_float_array_raw(_M0L3lenS531);
  _block_2835
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2835)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _block_2835->$0 = _M0L6_2atmpS1782;
  _block_2835->$1 = _M0L3lenS531;
  return _block_2835;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS532
) {
  uint8_t* _M0L6_2atmpS1783;
  struct _M0TPB5ArrayGbE* _block_2836;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1783 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS532);
  _block_2836
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2836)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 70, 0);
  _block_2836->$0 = _M0L6_2atmpS1783;
  _block_2836->$1 = _M0L3lenS532;
  return _block_2836;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS533
) {
  int32_t* _M0L6_2atmpS1784;
  struct _M0TPB5ArrayGiE* _block_2837;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1784 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS533);
  _block_2837
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2837)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
  _block_2837->$0 = _M0L6_2atmpS1784;
  _block_2837->$1 = _M0L3lenS533;
  return _block_2837;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS534
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1785;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2838;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1785
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS534, 0);
  _block_2838
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2838)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 73, 0);
  _block_2838->$0 = _M0L6_2atmpS1785;
  _block_2838->$1 = _M0L3lenS534;
  return _block_2838;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint64_t* _M0L6_2atmpS1780;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1780 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS1780)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1780[_M0L5indexS528];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS529,
  int32_t _M0L5indexS530
) {
  uint32_t* _M0L6_2atmpS1781;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1781 = _M0L4selfS529;
  if (
    _M0L5indexS530 < 0
    || _M0L5indexS530 >= Moonbit_array_length(_M0L6_2atmpS1781)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1781[_M0L5indexS530];
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

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS524) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS524;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS512,
  moonbit_string_t _M0L5valueS514
) {
  int32_t _M0L3lenS1752;
  moonbit_string_t* _M0L6_2atmpS1754;
  int32_t _M0L6_2atmpS1753;
  int32_t _M0L6lengthS513;
  moonbit_string_t* _M0L3bufS1757;
  moonbit_string_t _M0L6_2aoldS2688;
  int32_t _M0L6_2atmpS1758;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1752 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1754 = _M0MPC15array5Array6bufferGsE(_M0L4selfS512);
  _M0L6_2atmpS1753 = Moonbit_array_length(_M0L6_2atmpS1754);
  moonbit_decref_cycle_free(_M0L6_2atmpS1754);
  if (_M0L3lenS1752 == _M0L6_2atmpS1753) {
    int32_t _M0L3lenS1756 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS1755 = _M0L3lenS1756 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS512, _M0L6_2atmpS1755);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS1757 = _M0L4selfS512->$0;
  _M0L6_2aoldS2688 = (moonbit_string_t)_M0L3bufS1757[_M0L6lengthS513];
  moonbit_decref_cycle_free(_M0L6_2aoldS2688);
  _M0L3bufS1757[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS1758 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS1758;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS515,
  struct _M0TUsiE* _M0L5valueS517
) {
  int32_t _M0L3lenS1759;
  struct _M0TUsiE** _M0L6_2atmpS1761;
  int32_t _M0L6_2atmpS1760;
  int32_t _M0L6lengthS516;
  struct _M0TUsiE** _M0L3bufS1764;
  struct _M0TUsiE* _M0L6_2aoldS2689;
  int32_t _M0L6_2atmpS1765;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1759 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1761 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS515);
  _M0L6_2atmpS1760 = Moonbit_array_length(_M0L6_2atmpS1761);
  moonbit_decref_cycle_free(_M0L6_2atmpS1761);
  if (_M0L3lenS1759 == _M0L6_2atmpS1760) {
    int32_t _M0L3lenS1763 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS1762 = _M0L3lenS1763 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS515, _M0L6_2atmpS1762);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS1764 = _M0L4selfS515->$0;
  _M0L6_2aoldS2689 = (struct _M0TUsiE*)_M0L3bufS1764[_M0L6lengthS516];
  if (_M0L6_2aoldS2689) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2689);
  }
  _M0L3bufS1764[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS1765 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS1765;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS518,
  int32_t _M0L5valueS520
) {
  int32_t _M0L3lenS1766;
  int32_t* _M0L6_2atmpS1768;
  int32_t _M0L6_2atmpS1767;
  int32_t _M0L6lengthS519;
  int32_t* _M0L3bufS1771;
  int32_t _M0L6_2atmpS1772;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1766 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1768 = _M0MPC15array5Array6bufferGiE(_M0L4selfS518);
  _M0L6_2atmpS1767 = Moonbit_array_length(_M0L6_2atmpS1768);
  moonbit_decref_cycle_free(_M0L6_2atmpS1768);
  if (_M0L3lenS1766 == _M0L6_2atmpS1767) {
    int32_t _M0L3lenS1770 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS1769 = _M0L3lenS1770 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS518, _M0L6_2atmpS1769);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS1771 = _M0L4selfS518->$0;
  _M0L3bufS1771[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS1772 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS1772;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS521,
  float _M0L5valueS523
) {
  int32_t _M0L3lenS1773;
  float* _M0L6_2atmpS1775;
  int32_t _M0L6_2atmpS1774;
  int32_t _M0L6lengthS522;
  float* _M0L3bufS1778;
  int32_t _M0L6_2atmpS1779;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1773 = _M0L4selfS521->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1775 = _M0MPC15array5Array6bufferGfE(_M0L4selfS521);
  _M0L6_2atmpS1774 = Moonbit_array_length(_M0L6_2atmpS1775);
  moonbit_decref_cycle_free(_M0L6_2atmpS1775);
  if (_M0L3lenS1773 == _M0L6_2atmpS1774) {
    int32_t _M0L3lenS1777 = _M0L4selfS521->$1;
    int32_t _M0L6_2atmpS1776 = _M0L3lenS1777 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS521, _M0L6_2atmpS1776);
  }
  _M0L6lengthS522 = _M0L4selfS521->$1;
  _M0L3bufS1778 = _M0L4selfS521->$0;
  _M0L3bufS1778[_M0L6lengthS522] = _M0L5valueS523;
  _M0L6_2atmpS1779 = _M0L6lengthS522 + 1;
  _M0L4selfS521->$1 = _M0L6_2atmpS1779;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS497,
  int32_t _M0L8requiredS499
) {
  int32_t _M0L8old__capS496;
  int32_t _M0L3lenS1748;
  int32_t _M0L8new__capS498;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS496 = _M0MPC15array5Array8capacityGsE(_M0L4selfS497);
  _M0L3lenS1748 = _M0L4selfS497->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS498
  = _M0FPB23array__growth__capacity(_M0L8old__capS496, _M0L3lenS1748, _M0L8requiredS499);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS497, _M0L8new__capS498);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS501,
  int32_t _M0L8requiredS503
) {
  int32_t _M0L8old__capS500;
  int32_t _M0L3lenS1749;
  int32_t _M0L8new__capS502;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS500 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS501);
  _M0L3lenS1749 = _M0L4selfS501->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS502
  = _M0FPB23array__growth__capacity(_M0L8old__capS500, _M0L3lenS1749, _M0L8requiredS503);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS501, _M0L8new__capS502);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS505,
  int32_t _M0L8requiredS507
) {
  int32_t _M0L8old__capS504;
  int32_t _M0L3lenS1750;
  int32_t _M0L8new__capS506;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS504 = _M0MPC15array5Array8capacityGiE(_M0L4selfS505);
  _M0L3lenS1750 = _M0L4selfS505->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS506
  = _M0FPB23array__growth__capacity(_M0L8old__capS504, _M0L3lenS1750, _M0L8requiredS507);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS505, _M0L8new__capS506);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS509,
  int32_t _M0L8requiredS511
) {
  int32_t _M0L8old__capS508;
  int32_t _M0L3lenS1751;
  int32_t _M0L8new__capS510;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS508 = _M0MPC15array5Array8capacityGfE(_M0L4selfS509);
  _M0L3lenS1751 = _M0L4selfS509->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS510
  = _M0FPB23array__growth__capacity(_M0L8old__capS508, _M0L3lenS1751, _M0L8requiredS511);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS509, _M0L8new__capS510);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS473,
  int32_t _M0L13new__capacityS476
) {
  moonbit_string_t* _M0L8old__bufS472;
  int32_t _M0L3lenS474;
  int32_t _M0L9copy__lenS475;
  moonbit_string_t* _M0L8new__bufS477;
  moonbit_string_t* _M0L6_2aoldS2690;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS472 = _M0L4selfS473->$0;
  _M0L3lenS474 = _M0L4selfS473->$1;
  if (_M0L3lenS474 < _M0L13new__capacityS476) {
    _M0L9copy__lenS475 = _M0L3lenS474;
  } else {
    _M0L9copy__lenS475 = _M0L13new__capacityS476;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS472);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS477
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS472, _M0L13new__capacityS476, _M0L9copy__lenS475, 0, 0);
  _M0L6_2aoldS2690 = _M0L4selfS473->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2690);
  _M0L4selfS473->$0 = _M0L8new__bufS477;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS479,
  int32_t _M0L13new__capacityS482
) {
  struct _M0TUsiE** _M0L8old__bufS478;
  int32_t _M0L3lenS480;
  int32_t _M0L9copy__lenS481;
  struct _M0TUsiE** _M0L8new__bufS483;
  struct _M0TUsiE** _M0L6_2aoldS2691;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS478 = _M0L4selfS479->$0;
  _M0L3lenS480 = _M0L4selfS479->$1;
  if (_M0L3lenS480 < _M0L13new__capacityS482) {
    _M0L9copy__lenS481 = _M0L3lenS480;
  } else {
    _M0L9copy__lenS481 = _M0L13new__capacityS482;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS478);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS483
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS478, _M0L13new__capacityS482, _M0L9copy__lenS481, 0, 0);
  _M0L6_2aoldS2691 = _M0L4selfS479->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2691);
  _M0L4selfS479->$0 = _M0L8new__bufS483;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS485,
  int32_t _M0L13new__capacityS488
) {
  int32_t* _M0L8old__bufS484;
  int32_t _M0L3lenS486;
  int32_t _M0L9copy__lenS487;
  int32_t* _M0L8new__bufS489;
  int32_t* _M0L6_2aoldS2692;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS484 = _M0L4selfS485->$0;
  _M0L3lenS486 = _M0L4selfS485->$1;
  if (_M0L3lenS486 < _M0L13new__capacityS488) {
    _M0L9copy__lenS487 = _M0L3lenS486;
  } else {
    _M0L9copy__lenS487 = _M0L13new__capacityS488;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS484);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS489
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS484, _M0L13new__capacityS488, _M0L9copy__lenS487, 0, 0);
  _M0L6_2aoldS2692 = _M0L4selfS485->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2692);
  _M0L4selfS485->$0 = _M0L8new__bufS489;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS491,
  int32_t _M0L13new__capacityS494
) {
  float* _M0L8old__bufS490;
  int32_t _M0L3lenS492;
  int32_t _M0L9copy__lenS493;
  float* _M0L8new__bufS495;
  float* _M0L6_2aoldS2693;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS490 = _M0L4selfS491->$0;
  _M0L3lenS492 = _M0L4selfS491->$1;
  if (_M0L3lenS492 < _M0L13new__capacityS494) {
    _M0L9copy__lenS493 = _M0L3lenS492;
  } else {
    _M0L9copy__lenS493 = _M0L13new__capacityS494;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS490);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS495
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS490, _M0L13new__capacityS494, _M0L9copy__lenS493, 0, 0);
  _M0L6_2aoldS2693 = _M0L4selfS491->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2693);
  _M0L4selfS491->$0 = _M0L8new__bufS495;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS468
) {
  moonbit_string_t* _M0L6_2atmpS1744;
  int32_t _result_2839;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1744 = _M0MPC15array5Array6bufferGsE(_M0L4selfS468);
  _result_2839 = Moonbit_array_length(_M0L6_2atmpS1744);
  moonbit_decref_cycle_free(_M0L6_2atmpS1744);
  return _result_2839;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS469
) {
  struct _M0TUsiE** _M0L6_2atmpS1745;
  int32_t _result_2840;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1745 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS469);
  _result_2840 = Moonbit_array_length(_M0L6_2atmpS1745);
  moonbit_decref_cycle_free(_M0L6_2atmpS1745);
  return _result_2840;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS470
) {
  int32_t* _M0L6_2atmpS1746;
  int32_t _result_2841;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1746 = _M0MPC15array5Array6bufferGiE(_M0L4selfS470);
  _result_2841 = Moonbit_array_length(_M0L6_2atmpS1746);
  moonbit_decref_cycle_free(_M0L6_2atmpS1746);
  return _result_2841;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS471
) {
  float* _M0L6_2atmpS1747;
  int32_t _result_2842;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1747 = _M0MPC15array5Array6bufferGfE(_M0L4selfS471);
  _result_2842 = Moonbit_array_length(_M0L6_2atmpS1747);
  moonbit_decref_cycle_free(_M0L6_2atmpS1747);
  return _result_2842;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS464,
  int32_t _M0L3lenS462,
  int32_t _M0L8requiredS461
) {
  int32_t _M0L5startS463;
  int32_t _M0L5spaceS465;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS461 < _M0L3lenS462) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_17.data);
  }
  if (_M0L7currentS464 == 0) {
    _M0L5startS463 = 8;
  } else {
    _M0L5startS463 = _M0L7currentS464;
  }
  _M0L5spaceS465 = _M0L5startS463;
  while (1) {
    if (_M0L5spaceS465 < _M0L8requiredS461) {
      int32_t _M0L4nextS466 = _M0L5spaceS465 * 2;
      if (_M0L4nextS466 <= _M0L5spaceS465) {
        return _M0L8requiredS461;
      }
      _M0L5spaceS465 = _M0L4nextS466;
      continue;
    } else {
      return _M0L5spaceS465;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS459) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS459->$1;
}

int32_t _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L4selfS460
) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS460->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS2694;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2694 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2694);
  return _M0L8_2afieldS2694;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS453) {
  uint8_t* _M0L8_2afieldS2695;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2695 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2695);
  return _M0L8_2afieldS2695;
}

struct _M0TP26RiantR8snn__mbt13SynapseTarget** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L4selfS454
) {
  struct _M0TP26RiantR8snn__mbt13SynapseTarget** _M0L8_2afieldS2696;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2696 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2696);
  return _M0L8_2afieldS2696;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS455) {
  int32_t* _M0L8_2afieldS2697;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2697 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2697);
  return _M0L8_2afieldS2697;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS456
) {
  moonbit_string_t* _M0L8_2afieldS2698;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2698 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2698);
  return _M0L8_2afieldS2698;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS457
) {
  struct _M0TUsiE** _M0L8_2afieldS2699;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2699 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2699);
  return _M0L8_2afieldS2699;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS458
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2700;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2700 = _M0L4selfS458->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2700);
  return _M0L8_2afieldS2700;
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
  int32_t _M0L3endS1742;
  int32_t _M0L5startS1743;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1741;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1734;
  int32_t _M0L6_2atmpS1733;
  int32_t _if__result_2844;
  uint16_t* _M0L4dataS1735;
  int32_t _M0L3lenS1736;
  moonbit_string_t _M0L6_2atmpS1737;
  int32_t _M0L6_2atmpS1738;
  int32_t _M0L3lenS1740;
  int32_t _M0L6_2atmpS1739;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1742 = _M0L3strS448.$2;
  _M0L5startS1743 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1742 - _M0L5startS1743;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1741 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1741 + _M0L8str__lenS447;
  _M0L4dataS1734 = _M0L4selfS450->$0;
  _M0L6_2atmpS1733 = Moonbit_array_length(_M0L4dataS1734);
  if (_M0L8requiredS449 > _M0L6_2atmpS1733) {
    _if__result_2844 = 1;
  } else {
    int32_t _M0L3lenS1732 = _M0L4selfS450->$1;
    _if__result_2844 = _M0L8requiredS449 < _M0L3lenS1732;
  }
  if (_if__result_2844) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1735 = _M0L4selfS450->$0;
  _M0L3lenS1736 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1735);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1737 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1738 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1735, _M0L3lenS1736, _M0L6_2atmpS1737, _M0L6_2atmpS1738, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1735);
  moonbit_decref_cycle_free(_M0L6_2atmpS1737);
  _M0L3lenS1740 = _M0L4selfS450->$1;
  _M0L6_2atmpS1739 = _M0L3lenS1740 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1739;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2845;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1731;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1730;
  moonbit_string_t _result_2846;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1729 = Moonbit_array_length(_M0L3strS444);
    _if__result_2845 = _M0L3endS443 == _M0L6_2atmpS1729;
  } else {
    _if__result_2845 = 0;
  }
  if (_if__result_2845) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1731 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1731, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1730 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2846
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1730, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1730);
  return _result_2846;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2847;
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
      int32_t _M0L6_2atmpS1728 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2847 = _M0L6_2atmpS1728 <= _M0L3lenS436;
    } else {
      _if__result_2847 = 0;
    }
  } else {
    _if__result_2847 = 0;
  }
  if (_if__result_2847) {
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
  int32_t _M0L6_2atmpS1727;
  int32_t _M0L6_2atmpS1726;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1725;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1727 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1726 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1727;
  _M0L2e1S422 = _M0L6_2atmpS1726 - 1;
  _M0L6_2atmpS1725 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1725 - 1;
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
        int32_t _M0L6_2atmpS1722 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1721 = (int32_t)_M0L6_2atmpS1722;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1721;
        uint32_t _M0L6_2atmpS1717 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1716;
        int32_t _M0L6_2atmpS1718;
        uint32_t _M0L6_2atmpS1720;
        int32_t _M0L6_2atmpS1719;
        int32_t _M0L6_2atmpS1723;
        int32_t _M0L6_2atmpS1724;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1716 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1717);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1716;
        _M0L6_2atmpS1718 = _M0L1jS433 + 1;
        _M0L6_2atmpS1720 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1719 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1720);
        if (
          _M0L6_2atmpS1718 < 0
          || _M0L6_2atmpS1718 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1718] = _M0L6_2atmpS1719;
        _M0L6_2atmpS1723 = _M0L1iS432 + 1;
        _M0L6_2atmpS1724 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1723;
        _M0L1jS433 = _M0L6_2atmpS1724;
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
  int32_t _M0L6_2atmpS1715;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1715 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1715 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1714 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1714;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1711;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1711 = 1;
      } else {
        _M0L6_2atmpS1711 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1711;
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
      int32_t _M0L6_2atmpS1712;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1712 = 1;
      } else {
        _M0L6_2atmpS1712 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1712;
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
      int32_t _M0L6_2atmpS1713;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1713 = 1;
      } else {
        _M0L6_2atmpS1713 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1713;
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
  int32_t _M0L6_2atmpS1710;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1710 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1710;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1687 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1687;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1686 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1685 = 48 + _M0L6_2atmpS1686;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1685;
      int32_t _M0L6_2atmpS1684 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1683 = 48 + _M0L6_2atmpS1684;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1683;
      int32_t _M0L6_2atmpS1682 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1681 = 48 + _M0L6_2atmpS1682;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1681;
      int32_t _M0L6_2atmpS1680 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1679 = 48 + _M0L6_2atmpS1680;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1679;
      int32_t _M0L6_2atmpS1671 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1670 = _M0L6_2atmpS1671 - 4;
      int32_t _M0L6_2atmpS1673;
      int32_t _M0L6_2atmpS1672;
      int32_t _M0L6_2atmpS1675;
      int32_t _M0L6_2atmpS1674;
      int32_t _M0L6_2atmpS1677;
      int32_t _M0L6_2atmpS1676;
      int32_t _M0L6_2atmpS1678;
      _M0L6bufferS381[_M0L6_2atmpS1670] = _M0L6d1__hiS377;
      _M0L6_2atmpS1673 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1672 = _M0L6_2atmpS1673 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1672] = _M0L6d1__loS378;
      _M0L6_2atmpS1675 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1674 = _M0L6_2atmpS1675 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1674] = _M0L6d2__hiS379;
      _M0L6_2atmpS1677 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1676 = _M0L6_2atmpS1677 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1676] = _M0L6d2__loS380;
      _M0L6_2atmpS1678 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1678;
      continue;
    } else {
      int32_t _M0L6_2atmpS1709 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1709;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1696 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1695 = 48 + _M0L6_2atmpS1696;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1695;
          int32_t _M0L6_2atmpS1694 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1693 = 48 + _M0L6_2atmpS1694;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1693;
          int32_t _M0L6_2atmpS1689 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1688 = _M0L6_2atmpS1689 - 2;
          int32_t _M0L6_2atmpS1691;
          int32_t _M0L6_2atmpS1690;
          int32_t _M0L6_2atmpS1692;
          _M0L6bufferS381[_M0L6_2atmpS1688] = _M0L5d__hiS388;
          _M0L6_2atmpS1691 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1690 = _M0L6_2atmpS1691 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1690] = _M0L5d__loS389;
          _M0L6_2atmpS1692 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1692;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1704 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1703 = 48 + _M0L6_2atmpS1704;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1703;
          int32_t _M0L6_2atmpS1702 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1701 = 48 + _M0L6_2atmpS1702;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1701;
          int32_t _M0L6_2atmpS1698 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1697 = _M0L6_2atmpS1698 - 2;
          int32_t _M0L6_2atmpS1700;
          int32_t _M0L6_2atmpS1699;
          _M0L6bufferS381[_M0L6_2atmpS1697] = _M0L5d__hiS391;
          _M0L6_2atmpS1700 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1699 = _M0L6_2atmpS1700 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1699] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1708 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1705 = _M0L6_2atmpS1708 - 1;
          int32_t _M0L6_2atmpS1707 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1706 = (uint16_t)_M0L6_2atmpS1707;
          _M0L6bufferS381[_M0L6_2atmpS1705] = _M0L6_2atmpS1706;
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
  int32_t _M0L6_2atmpS1655;
  int32_t _M0L6_2atmpS1654;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1655 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1654 = _M0L5radixS355 & _M0L6_2atmpS1655;
  if (_M0L6_2atmpS1654 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1662;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1662 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1662;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1661 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1661;
        int32_t _M0L6_2atmpS1658 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1656 = _M0L6_2atmpS1658 - 1;
        int32_t _M0L6_2atmpS1657 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1659;
        uint64_t _M0L6_2atmpS1660;
        _M0L6bufferS361[_M0L6_2atmpS1656] = _M0L6_2atmpS1657;
        _M0L6_2atmpS1659 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1660 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1659;
        _M0L1nS359 = _M0L6_2atmpS1660;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1669 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1669;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1668 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1667 = _M0L1nS367 - _M0L6_2atmpS1668;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1667;
        int32_t _M0L6_2atmpS1665 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1663 = _M0L6_2atmpS1665 - 1;
        int32_t _M0L6_2atmpS1664 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1666;
        _M0L6bufferS361[_M0L6_2atmpS1663] = _M0L6_2atmpS1664;
        _M0L6_2atmpS1666 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1666;
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
  int32_t _M0L6_2atmpS1653;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1653 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1653;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1650 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1650;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1644 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1642 = _M0L6_2atmpS1644 - 2;
      int32_t _M0L6_2atmpS1643 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1647;
      int32_t _M0L6_2atmpS1645;
      int32_t _M0L6_2atmpS1646;
      int32_t _M0L6_2atmpS1648;
      uint64_t _M0L6_2atmpS1649;
      _M0L6bufferS348[_M0L6_2atmpS1642] = _M0L6_2atmpS1643;
      _M0L6_2atmpS1647 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1645 = _M0L6_2atmpS1647 - 1;
      _M0L6_2atmpS1646
      = ((moonbit_string_t)moonbit_string_literal_19.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1645] = _M0L6_2atmpS1646;
      _M0L6_2atmpS1648 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1649 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1648;
      _M0L1nS344 = _M0L6_2atmpS1649;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1652 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1652;
      int32_t _M0L6_2atmpS1651 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1651;
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
      uint64_t _M0L6_2atmpS1640 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1641 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1640;
      _M0L5countS341 = _M0L6_2atmpS1641;
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
    int32_t _M0L6_2atmpS1639;
    int32_t _M0L6_2atmpS1638;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1639 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1638 = _M0L6_2atmpS1639 / 4;
    return _M0L6_2atmpS1638 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1637 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1637;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1634;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1634 = 1;
      } else {
        _M0L6_2atmpS1634 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1634;
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
      int32_t _M0L6_2atmpS1635;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1635 = 1;
      } else {
        _M0L6_2atmpS1635 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1635;
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
      int32_t _M0L6_2atmpS1636;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1636 = 1;
      } else {
        _M0L6_2atmpS1636 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1636;
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
      uint32_t _M0L6_2atmpS1632 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1633 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1632;
      _M0L5countS315 = _M0L6_2atmpS1633;
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
    int32_t _M0L6_2atmpS1631;
    int32_t _M0L6_2atmpS1630;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1631 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1630 = _M0L6_2atmpS1631 / 4;
    return _M0L6_2atmpS1630 + 1;
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
  int32_t _M0L6_2atmpS1629;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1629 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1629;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1606 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1606;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1605 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1604 = 48 + _M0L6_2atmpS1605;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1604;
      int32_t _M0L6_2atmpS1603 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1602 = 48 + _M0L6_2atmpS1603;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1602;
      int32_t _M0L6_2atmpS1601 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1600 = 48 + _M0L6_2atmpS1601;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1600;
      int32_t _M0L6_2atmpS1599 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1598 = 48 + _M0L6_2atmpS1599;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1598;
      int32_t _M0L6_2atmpS1590 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1589 = _M0L6_2atmpS1590 - 4;
      int32_t _M0L6_2atmpS1592;
      int32_t _M0L6_2atmpS1591;
      int32_t _M0L6_2atmpS1594;
      int32_t _M0L6_2atmpS1593;
      int32_t _M0L6_2atmpS1596;
      int32_t _M0L6_2atmpS1595;
      int32_t _M0L6_2atmpS1597;
      _M0L6bufferS294[_M0L6_2atmpS1589] = _M0L6d1__hiS290;
      _M0L6_2atmpS1592 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1591 = _M0L6_2atmpS1592 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1591] = _M0L6d1__loS291;
      _M0L6_2atmpS1594 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1593 = _M0L6_2atmpS1594 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1593] = _M0L6d2__hiS292;
      _M0L6_2atmpS1596 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1595 = _M0L6_2atmpS1596 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1595] = _M0L6d2__loS293;
      _M0L6_2atmpS1597 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1597;
      continue;
    } else {
      int32_t _M0L6_2atmpS1628 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1628;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1615 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1614 = 48 + _M0L6_2atmpS1615;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1614;
          int32_t _M0L6_2atmpS1613 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1612 = 48 + _M0L6_2atmpS1613;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1612;
          int32_t _M0L6_2atmpS1608 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1607 = _M0L6_2atmpS1608 - 2;
          int32_t _M0L6_2atmpS1610;
          int32_t _M0L6_2atmpS1609;
          int32_t _M0L6_2atmpS1611;
          _M0L6bufferS294[_M0L6_2atmpS1607] = _M0L5d__hiS301;
          _M0L6_2atmpS1610 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1609 = _M0L6_2atmpS1610 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1609] = _M0L5d__loS302;
          _M0L6_2atmpS1611 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1611;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1623 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1622 = 48 + _M0L6_2atmpS1623;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1622;
          int32_t _M0L6_2atmpS1621 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1620 = 48 + _M0L6_2atmpS1621;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1620;
          int32_t _M0L6_2atmpS1617 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1616 = _M0L6_2atmpS1617 - 2;
          int32_t _M0L6_2atmpS1619;
          int32_t _M0L6_2atmpS1618;
          _M0L6bufferS294[_M0L6_2atmpS1616] = _M0L5d__hiS304;
          _M0L6_2atmpS1619 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1618 = _M0L6_2atmpS1619 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1618] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1627 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1624 = _M0L6_2atmpS1627 - 1;
          int32_t _M0L6_2atmpS1626 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1625 = (uint16_t)_M0L6_2atmpS1626;
          _M0L6bufferS294[_M0L6_2atmpS1624] = _M0L6_2atmpS1625;
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
  int32_t _M0L6_2atmpS1574;
  int32_t _M0L6_2atmpS1573;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1574 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1573 = _M0L5radixS268 & _M0L6_2atmpS1574;
  if (_M0L6_2atmpS1573 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1581;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1581 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1581;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1580 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1580;
        int32_t _M0L6_2atmpS1577 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1575 = _M0L6_2atmpS1577 - 1;
        int32_t _M0L6_2atmpS1576 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1578;
        uint32_t _M0L6_2atmpS1579;
        _M0L6bufferS274[_M0L6_2atmpS1575] = _M0L6_2atmpS1576;
        _M0L6_2atmpS1578 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1579 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1578;
        _M0L1nS272 = _M0L6_2atmpS1579;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1588 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1588;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1587 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1586 = _M0L1nS280 - _M0L6_2atmpS1587;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1586;
        int32_t _M0L6_2atmpS1584 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1582 = _M0L6_2atmpS1584 - 1;
        int32_t _M0L6_2atmpS1583 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1585;
        _M0L6bufferS274[_M0L6_2atmpS1582] = _M0L6_2atmpS1583;
        _M0L6_2atmpS1585 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1585;
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
  int32_t _M0L6_2atmpS1572;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1572 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1572;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1569 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1569;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1563 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1561 = _M0L6_2atmpS1563 - 2;
      int32_t _M0L6_2atmpS1562 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1566;
      int32_t _M0L6_2atmpS1564;
      int32_t _M0L6_2atmpS1565;
      int32_t _M0L6_2atmpS1567;
      uint32_t _M0L6_2atmpS1568;
      _M0L6bufferS261[_M0L6_2atmpS1561] = _M0L6_2atmpS1562;
      _M0L6_2atmpS1566 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1564 = _M0L6_2atmpS1566 - 1;
      _M0L6_2atmpS1565
      = ((moonbit_string_t)moonbit_string_literal_19.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1564] = _M0L6_2atmpS1565;
      _M0L6_2atmpS1567 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1568 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1567;
      _M0L1nS257 = _M0L6_2atmpS1568;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1571 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1571;
      int32_t _M0L6_2atmpS1570 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1570;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1560;
  moonbit_string_t _result_2861;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1560
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1560);
  if (_M0L6_2atmpS1560.$1) {
    moonbit_decref(_M0L6_2atmpS1560.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2861 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2861;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1557;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1557 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1557);
  moonbit_decref_cycle_free(_M0L6_2atmpS1557);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1558;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1558 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1558);
  moonbit_decref_cycle_free(_M0L6_2atmpS1558);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1559;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1559 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1559);
  moonbit_decref_cycle_free(_M0L6_2atmpS1559);
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
  moonbit_string_t _M0L8_2afieldS2701;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2701 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2701);
  return _M0L8_2afieldS2701;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1556;
  int64_t _M0L6_2atmpS1555;
  struct _M0TPC16string10StringView _M0L6_2atmpS1554;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1556 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1555 = (int64_t)_M0L6_2atmpS1556;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1554
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1555);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1554);
  moonbit_decref_cycle_free(_M0L6_2atmpS1554.$0);
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
  int32_t _M0L6_2atmpS1538;
  int32_t _if__result_2862;
  int32_t _M0L6_2atmpS1546;
  int32_t _if__result_2863;
  int32_t _M0L6_2atmpS1548;
  int32_t _M0L6_2atmpS1549;
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
  _M0L6_2atmpS1538 = _M0Lm2loS236;
  if (_M0L6_2atmpS1538 > 0) {
    int32_t _M0L6_2atmpS1537 = _M0Lm2loS236;
    if (_M0L6_2atmpS1537 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1536 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1535 = _M0L4selfS235[_M0L6_2atmpS1536];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1535)) {
        int32_t _M0L6_2atmpS1534 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1533 = _M0L6_2atmpS1534 - 1;
        int32_t _M0L6_2atmpS1532 = _M0L4selfS235[_M0L6_2atmpS1533];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2862
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1532);
      } else {
        _if__result_2862 = 0;
      }
    } else {
      _if__result_2862 = 0;
    }
  } else {
    _if__result_2862 = 0;
  }
  if (_if__result_2862) {
    int32_t _M0L6_2atmpS1539 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1539 + 1;
  }
  _M0L6_2atmpS1546 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1546 > 0) {
    int32_t _M0L6_2atmpS1545 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1545 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1544 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1543 = _M0L4selfS235[_M0L6_2atmpS1544];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1543)) {
        int32_t _M0L6_2atmpS1542 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1541 = _M0L6_2atmpS1542 - 1;
        int32_t _M0L6_2atmpS1540 = _M0L4selfS235[_M0L6_2atmpS1541];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2863
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1540);
      } else {
        _if__result_2863 = 0;
      }
    } else {
      _if__result_2863 = 0;
    }
  } else {
    _if__result_2863 = 0;
  }
  if (_if__result_2863) {
    int32_t _M0L6_2atmpS1547 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1547 - 1;
  }
  _M0L6_2atmpS1548 = _M0Lm2loS236;
  _M0L6_2atmpS1549 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1548 >= _M0L6_2atmpS1549) {
    int32_t _M0L6_2atmpS1550 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1551 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1550,
                                                 .$2 = _M0L6_2atmpS1551};
  } else {
    int32_t _M0L6_2atmpS1552 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1553 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1552,
                                                 .$2 = _M0L6_2atmpS1553};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1531;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1531
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1531);
  if (_M0L6_2atmpS1531.$1) {
    moonbit_decref(_M0L6_2atmpS1531.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1530;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1530
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1530);
  if (_M0L6_2atmpS1530.$1) {
    moonbit_decref(_M0L6_2atmpS1530.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1529;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1529 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1529;
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
  int32_t _M0L6_2atmpS1528;
  struct _M0TPC16string10StringView _M0L6_2atmpS1526;
  struct _M0TPB6Logger _M0L6_2atmpS1527;
  moonbit_string_t _result_2864;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1528 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1526
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1528
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1527
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1526, _M0L6_2atmpS1527, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1526.$0);
  if (_M0L6_2atmpS1527.$1) {
    moonbit_decref(_M0L6_2atmpS1527.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2864 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2864;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1524;
  int32_t _M0L5startS1525;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1524 = _M0L4selfS218.$2;
  _M0L5startS1525 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1524 - _M0L5startS1525;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 83, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1521;
    int32_t _M0L5startS1523;
    int32_t _M0L6_2atmpS1522;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1505;
    int32_t _M0L6_2atmpS1506;
    int32_t _M0L6_2atmpS1507;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1521 = _M0L4selfS218.$0;
    _M0L5startS1523 = _M0L4selfS218.$1;
    _M0L6_2atmpS1522 = _M0L5startS1523 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1521[_M0L6_2atmpS1522];
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
        int32_t _M0L6_2atmpS1508;
        int32_t _M0L6_2atmpS1509;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1508 = _M0L1iS220 + 1;
        _M0L6_2atmpS1509 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1508;
        _M0L3segS221 = _M0L6_2atmpS1509;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1510;
        int32_t _M0L6_2atmpS1511;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1510 = _M0L1iS220 + 1;
        _M0L6_2atmpS1511 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1510;
        _M0L3segS221 = _M0L6_2atmpS1511;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1512;
        int32_t _M0L6_2atmpS1513;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1512 = _M0L1iS220 + 1;
        _M0L6_2atmpS1513 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1512;
        _M0L3segS221 = _M0L6_2atmpS1513;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1514;
        int32_t _M0L6_2atmpS1515;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1514 = _M0L1iS220 + 1;
        _M0L6_2atmpS1515 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1514;
        _M0L3segS221 = _M0L6_2atmpS1515;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1517;
          moonbit_string_t _M0L6_2atmpS1516;
          int32_t _M0L6_2atmpS1518;
          int32_t _M0L6_2atmpS1519;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
          _M0L6_2atmpS1517 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1516 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1517);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1516);
          moonbit_decref_cycle_free(_M0L6_2atmpS1516);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1518 = _M0L1iS220 + 1;
          _M0L6_2atmpS1519 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1518;
          _M0L3segS221 = _M0L6_2atmpS1519;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1520 = _M0L1iS220 + 1;
          int32_t _tmp_2867 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1520;
          _M0L3segS221 = _tmp_2867;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2866;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1505 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1505);
    _M0L6_2atmpS1506 = _M0L1iS220 + 1;
    _M0L6_2atmpS1507 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1506;
    _M0L3segS221 = _M0L6_2atmpS1507;
    continue;
    joinlet_2866:;
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
    int64_t _M0L6_2atmpS1504 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1503;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1503
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1504);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1503);
    moonbit_decref_cycle_free(_M0L6_2atmpS1503.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1501;
  int32_t _M0L5startS1502;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1479;
  int32_t _if__result_2868;
  int32_t _M0L6_2atmpS1489;
  int32_t _if__result_2869;
  int32_t _M0L6_2atmpS1491;
  int32_t _M0L6_2atmpS1492;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1501 = _M0L4selfS201.$2;
  _M0L5startS1502 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1501 - _M0L5startS1502;
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
  _M0L6_2atmpS1479 = _M0Lm2loS202;
  if (_M0L6_2atmpS1479 > 0) {
    int32_t _M0L6_2atmpS1478 = _M0Lm2loS202;
    if (_M0L6_2atmpS1478 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1477 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1476 = _M0L4baseS209 + _M0L6_2atmpS1477;
      int32_t _M0L6_2atmpS1475 = _M0L3strS208[_M0L6_2atmpS1476];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1475)) {
        int32_t _M0L6_2atmpS1474 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1473 = _M0L4baseS209 + _M0L6_2atmpS1474;
        int32_t _M0L6_2atmpS1472 = _M0L6_2atmpS1473 - 1;
        int32_t _M0L6_2atmpS1471 = _M0L3strS208[_M0L6_2atmpS1472];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2868
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1471);
      } else {
        _if__result_2868 = 0;
      }
    } else {
      _if__result_2868 = 0;
    }
  } else {
    _if__result_2868 = 0;
  }
  if (_if__result_2868) {
    int32_t _M0L6_2atmpS1480 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1480 + 1;
  }
  _M0L6_2atmpS1489 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1489 > 0) {
    int32_t _M0L6_2atmpS1488 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1488 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1487 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1486 = _M0L4baseS209 + _M0L6_2atmpS1487;
      int32_t _M0L6_2atmpS1485 = _M0L3strS208[_M0L6_2atmpS1486];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1485)) {
        int32_t _M0L6_2atmpS1484 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1483 = _M0L4baseS209 + _M0L6_2atmpS1484;
        int32_t _M0L6_2atmpS1482 = _M0L6_2atmpS1483 - 1;
        int32_t _M0L6_2atmpS1481 = _M0L3strS208[_M0L6_2atmpS1482];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2869
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1481);
      } else {
        _if__result_2869 = 0;
      }
    } else {
      _if__result_2869 = 0;
    }
  } else {
    _if__result_2869 = 0;
  }
  if (_if__result_2869) {
    int32_t _M0L6_2atmpS1490 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1490 - 1;
  }
  _M0L6_2atmpS1491 = _M0Lm2loS202;
  _M0L6_2atmpS1492 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1491 >= _M0L6_2atmpS1492) {
    int32_t _M0L6_2atmpS1496 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1493 = _M0L4baseS209 + _M0L6_2atmpS1496;
    int32_t _M0L6_2atmpS1495 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1494 = _M0L4baseS209 + _M0L6_2atmpS1495;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1493,
                                                 .$2 = _M0L6_2atmpS1494};
  } else {
    int32_t _M0L6_2atmpS1500 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1497 = _M0L4baseS209 + _M0L6_2atmpS1500;
    int32_t _M0L6_2atmpS1499 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1498 = _M0L4baseS209 + _M0L6_2atmpS1499;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1497,
                                                 .$2 = _M0L6_2atmpS1498};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1468;
  int32_t _M0L6_2atmpS1467;
  int32_t _M0L6_2atmpS1470;
  int32_t _M0L6_2atmpS1469;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1466;
  moonbit_string_t _result_2870;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1468 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1467
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1468);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1467);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1470 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1469
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1470);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1469);
  _M0L6_2atmpS1466 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2870 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1466);
  moonbit_decref_cycle_free(_M0L6_2atmpS1466);
  return _result_2870;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1463;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1463 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1463);
  } else {
    int32_t _M0L6_2atmpS1465;
    int32_t _M0L6_2atmpS1464;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1465 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1464 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1465, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1464);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1461;
  int32_t _M0L6_2atmpS1462;
  int32_t _M0L6_2atmpS1460;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1461 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1462 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1460 = _M0L6_2atmpS1461 - _M0L6_2atmpS1462;
  return _M0L6_2atmpS1460 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1458;
  int32_t _M0L6_2atmpS1459;
  int32_t _M0L6_2atmpS1457;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1458 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1459 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1457 = _M0L6_2atmpS1458 % _M0L6_2atmpS1459;
  return _M0L6_2atmpS1457 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1455;
  int32_t _M0L6_2atmpS1456;
  int32_t _M0L6_2atmpS1454;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1455 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1456 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1454 = _M0L6_2atmpS1455 / _M0L6_2atmpS1456;
  return _M0L6_2atmpS1454 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1452;
  int32_t _M0L6_2atmpS1453;
  int32_t _M0L6_2atmpS1451;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1452 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1453 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1451 = _M0L6_2atmpS1452 + _M0L6_2atmpS1453;
  return _M0L6_2atmpS1451 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1450;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1450 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1450;
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
  int32_t _M0L3lenS1449;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1444;
  int32_t _M0L6_2atmpS1443;
  int32_t _if__result_2871;
  uint16_t* _M0L4dataS1445;
  int32_t _M0L3lenS1446;
  int32_t _M0L3lenS1448;
  int32_t _M0L6_2atmpS1447;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1449 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1449 + _M0L8str__lenS182;
  _M0L4dataS1444 = _M0L4selfS185->$0;
  _M0L6_2atmpS1443 = Moonbit_array_length(_M0L4dataS1444);
  if (_M0L8requiredS184 > _M0L6_2atmpS1443) {
    _if__result_2871 = 1;
  } else {
    int32_t _M0L3lenS1442 = _M0L4selfS185->$1;
    _if__result_2871 = _M0L8requiredS184 < _M0L3lenS1442;
  }
  if (_if__result_2871) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1445 = _M0L4selfS185->$0;
  _M0L3lenS1446 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1445);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1445, _M0L3lenS1446, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1445);
  _M0L3lenS1448 = _M0L4selfS185->$1;
  _M0L6_2atmpS1447 = _M0L3lenS1448 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1447;
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
      int32_t _M0L6_2atmpS1439 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1440;
      int32_t _M0L6_2atmpS1441;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1439;
      _M0L6_2atmpS1440 = _M0L1iS176 + 1;
      _M0L6_2atmpS1441 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1440;
      _M0L1jS177 = _M0L6_2atmpS1441;
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
    int32_t _M0L3lenS1410 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1412 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1411 = Moonbit_array_length(_M0L4dataS1412);
    uint16_t* _M0L4dataS1415;
    int32_t _M0L3lenS1416;
    int32_t _M0L6_2atmpS1417;
    int32_t _M0L3lenS1419;
    int32_t _M0L6_2atmpS1418;
    if (_M0L3lenS1410 >= _M0L6_2atmpS1411) {
      int32_t _M0L3lenS1414 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1413 = _M0L3lenS1414 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1413);
    }
    _M0L4dataS1415 = _M0L4selfS171->$0;
    _M0L3lenS1416 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1415);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1417 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1416 < 0
      || _M0L3lenS1416 >= Moonbit_array_length(_M0L4dataS1415)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1415[_M0L3lenS1416] = _M0L6_2atmpS1417;
    moonbit_decref_cycle_free(_M0L4dataS1415);
    _M0L3lenS1419 = _M0L4selfS171->$1;
    _M0L6_2atmpS1418 = _M0L3lenS1419 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1418;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1423 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1421 = Moonbit_array_length(_M0L4dataS1423);
    int32_t _M0L3lenS1422 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1420 = _M0L6_2atmpS1421 - _M0L3lenS1422;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1426;
    int32_t _M0L3lenS1427;
    uint32_t _M0L6_2atmpS1430;
    uint32_t _M0L6_2atmpS1429;
    int32_t _M0L6_2atmpS1428;
    uint16_t* _M0L4dataS1431;
    int32_t _M0L3lenS1436;
    int32_t _M0L6_2atmpS1432;
    uint32_t _M0L6_2atmpS1435;
    uint32_t _M0L6_2atmpS1434;
    int32_t _M0L6_2atmpS1433;
    int32_t _M0L3lenS1438;
    int32_t _M0L6_2atmpS1437;
    if (_M0L6_2atmpS1420 < 2) {
      int32_t _M0L3lenS1425 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1424 = _M0L3lenS1425 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1424);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1426 = _M0L4selfS171->$0;
    _M0L3lenS1427 = _M0L4selfS171->$1;
    _M0L6_2atmpS1430 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1429 = 55296u + _M0L6_2atmpS1430;
    moonbit_incref_cycle_free(_M0L4dataS1426);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1428 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1429);
    if (
      _M0L3lenS1427 < 0
      || _M0L3lenS1427 >= Moonbit_array_length(_M0L4dataS1426)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1426[_M0L3lenS1427] = _M0L6_2atmpS1428;
    moonbit_decref_cycle_free(_M0L4dataS1426);
    _M0L4dataS1431 = _M0L4selfS171->$0;
    _M0L3lenS1436 = _M0L4selfS171->$1;
    _M0L6_2atmpS1432 = _M0L3lenS1436 + 1;
    _M0L6_2atmpS1435 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1434 = 56320u + _M0L6_2atmpS1435;
    moonbit_incref_cycle_free(_M0L4dataS1431);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1433 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1434);
    if (
      _M0L6_2atmpS1432 < 0
      || _M0L6_2atmpS1432 >= Moonbit_array_length(_M0L4dataS1431)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1431[_M0L6_2atmpS1432] = _M0L6_2atmpS1433;
    moonbit_decref_cycle_free(_M0L4dataS1431);
    _M0L3lenS1438 = _M0L4selfS171->$1;
    _M0L6_2atmpS1437 = _M0L3lenS1438 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1437;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_25.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1409;
  int32_t _M0L6_2atmpS1407;
  int32_t _M0L3lenS1408;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1404;
  int32_t _M0L6_2atmpS1405;
  int32_t _M0L3lenS1406;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2702;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1409 = _M0L4selfS166->$0;
  _M0L6_2atmpS1407 = Moonbit_array_length(_M0L4dataS1409);
  _M0L3lenS1408 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1407, _M0L3lenS1408, _M0L8requiredS167);
  _M0L4dataS1404 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1404);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1405 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1406 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1404, _M0L13new__capacityS165, _M0L6_2atmpS1405, _M0L3lenS1406, 0, 0);
  _M0L6_2aoldS2702 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2702);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
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
  int32_t _M0L6_2atmpS1403;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1403 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1403;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1402;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1402 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1402;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1393;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1393 = _M0L4selfS155->$1;
  if (_M0L3lenS1393 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1394 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1396 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1395 = Moonbit_array_length(_M0L4dataS1396);
    if (_M0L3lenS1394 == _M0L6_2atmpS1395) {
      uint16_t* _M0L4dataS1397 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1397);
      return _M0L4dataS1397;
    } else {
      uint16_t* _M0L4dataS1398 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1399 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1400;
      int32_t _M0L3lenS1401;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1398);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1400 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1401 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1398, _M0L3lenS1399, _M0L6_2atmpS1400, _M0L3lenS1401, 0, 0);
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
  int32_t _if__result_2874;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1389 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1390 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1389 <= _M0L6_2atmpS1390) {
            int32_t _M0L6_2atmpS1388 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2874 = _M0L6_2atmpS1388 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2874 = 0;
          }
        } else {
          _if__result_2874 = 0;
        }
      } else {
        _if__result_2874 = 0;
      }
    } else {
      _if__result_2874 = 0;
    }
  } else {
    _if__result_2874 = 0;
  }
  if (_if__result_2874) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1392;
    moonbit_string_t _M0L6_2atmpS1391;
    uint16_t* _result_2875;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_31.data);
    _M0L6_2atmpS1392 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1392);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1391
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2875 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1391);
    moonbit_decref_cycle_free(_M0L6_2atmpS1391);
    return _result_2875;
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
  struct _M0TPB13StringBuilder* _block_2876;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1387 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1387 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2876
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2876)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 88, 0);
  _block_2876->$0 = _M0L4dataS140;
  _block_2876->$1 = 0;
  return _block_2876;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1386;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1386 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1386;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2877;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1367 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1368;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1368
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS1367 <= _M0L6_2atmpS1368) {
            int32_t _M0L6_2atmpS1366 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2877 = _M0L6_2atmpS1366 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2877 = 0;
          }
        } else {
          _if__result_2877 = 0;
        }
      } else {
        _if__result_2877 = 0;
      }
    } else {
      _if__result_2877 = 0;
    }
  } else {
    _if__result_2877 = 0;
  }
  if (_if__result_2877) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1370;
    moonbit_string_t _M0L6_2atmpS1369;
    moonbit_string_t* _result_2878;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1370 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1370);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1369
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2878
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1369);
    moonbit_decref_cycle_free(_M0L6_2atmpS1369);
    return _result_2878;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2879;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1372 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1373;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1373
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS1372 <= _M0L6_2atmpS1373) {
            int32_t _M0L6_2atmpS1371 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2879 = _M0L6_2atmpS1371 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2879 = 0;
          }
        } else {
          _if__result_2879 = 0;
        }
      } else {
        _if__result_2879 = 0;
      }
    } else {
      _if__result_2879 = 0;
    }
  } else {
    _if__result_2879 = 0;
  }
  if (_if__result_2879) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1375;
    moonbit_string_t _M0L6_2atmpS1374;
    struct _M0TUsiE** _result_2880;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1375 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1375);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1374
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2880
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1374);
    moonbit_decref_cycle_free(_M0L6_2atmpS1374);
    return _result_2880;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2881;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1377 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1378;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1378
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
          if (_M0L6_2atmpS1377 <= _M0L6_2atmpS1378) {
            int32_t _M0L6_2atmpS1376 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2881 = _M0L6_2atmpS1376 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2881 = 0;
          }
        } else {
          _if__result_2881 = 0;
        }
      } else {
        _if__result_2881 = 0;
      }
    } else {
      _if__result_2881 = 0;
    }
  } else {
    _if__result_2881 = 0;
  }
  if (_if__result_2881) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1380;
    moonbit_string_t _M0L6_2atmpS1379;
    int32_t* _result_2882;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1380 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1380);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1379
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2882
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1379);
    moonbit_decref_cycle_free(_M0L6_2atmpS1379);
    return _result_2882;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2883;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1382 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1383;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1383
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS1382 <= _M0L6_2atmpS1383) {
            int32_t _M0L6_2atmpS1381 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2883 = _M0L6_2atmpS1381 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2883 = 0;
          }
        } else {
          _if__result_2883 = 0;
        }
      } else {
        _if__result_2883 = 0;
      }
    } else {
      _if__result_2883 = 0;
    }
  } else {
    _if__result_2883 = 0;
  }
  if (_if__result_2883) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1385;
    moonbit_string_t _M0L6_2atmpS1384;
    float* _result_2884;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1385 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1385);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1384
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2884
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1384);
    moonbit_decref_cycle_free(_M0L6_2atmpS1384);
    return _result_2884;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1363;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1363
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1363);
  if (_M0L6_2atmpS1363.$1) {
    moonbit_decref(_M0L6_2atmpS1363.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1364;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1364
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1364);
  if (_M0L6_2atmpS1364.$1) {
    moonbit_decref(_M0L6_2atmpS1364.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1365;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1365
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1365);
  if (_M0L6_2atmpS1365.$1) {
    moonbit_decref(_M0L6_2atmpS1365.$1);
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  int32_t* _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS96);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  float* _M0L3srcS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L3lenS72
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS70);
  moonbit_incref_cycle_free(_M0L3dstS68);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS68, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L3lenS72, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  moonbit_string_t* _M0L3srcS75,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS78,
  int32_t _M0L11dst__offsetS79,
  struct _M0TUsiE** _M0L3srcS80,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L3lenS82
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS80);
  moonbit_incref_cycle_free(_M0L3dstS78);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS78, _M0L11dst__offsetS79, _M0L3srcS80, _M0L11src__offsetS81, _M0L3lenS82);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS18,
  int32_t _M0L11dst__offsetS20,
  int32_t* _M0L3srcS19,
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
        int32_t _M0L6_2atmpS1318 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1320 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1319;
        int32_t _M0L6_2atmpS1321;
        if (
          _M0L6_2atmpS1320 < 0
          || _M0L6_2atmpS1320 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1319 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1320];
        if (
          _M0L6_2atmpS1318 < 0
          || _M0L6_2atmpS1318 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1318] = _M0L6_2atmpS1319;
        _M0L6_2atmpS1321 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1321;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1326 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1326;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1322 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1324 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1323;
        int32_t _M0L6_2atmpS1325;
        if (
          _M0L6_2atmpS1324 < 0
          || _M0L6_2atmpS1324 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1323 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1324];
        if (
          _M0L6_2atmpS1322 < 0
          || _M0L6_2atmpS1322 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1322] = _M0L6_2atmpS1323;
        _M0L6_2atmpS1325 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1325;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS27,
  int32_t _M0L11dst__offsetS29,
  float* _M0L3srcS28,
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
        int32_t _M0L6_2atmpS1327 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1329 = _M0L11src__offsetS30 + _M0L1iS31;
        float _M0L6_2atmpS1328;
        int32_t _M0L6_2atmpS1330;
        if (
          _M0L6_2atmpS1329 < 0
          || _M0L6_2atmpS1329 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1328 = (float)_M0L3srcS28[_M0L6_2atmpS1329];
        if (
          _M0L6_2atmpS1327 < 0
          || _M0L6_2atmpS1327 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1327] = _M0L6_2atmpS1328;
        _M0L6_2atmpS1330 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1330;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1335 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1335;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1331 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1333 = _M0L11src__offsetS30 + _M0L1iS34;
        float _M0L6_2atmpS1332;
        int32_t _M0L6_2atmpS1334;
        if (
          _M0L6_2atmpS1333 < 0
          || _M0L6_2atmpS1333 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1332 = (float)_M0L3srcS28[_M0L6_2atmpS1333];
        if (
          _M0L6_2atmpS1331 < 0
          || _M0L6_2atmpS1331 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1331] = _M0L6_2atmpS1332;
        _M0L6_2atmpS1334 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1334;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  uint16_t* _M0L3srcS37,
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
        int32_t _M0L6_2atmpS1336 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1338 = _M0L11src__offsetS39 + _M0L1iS40;
        int32_t _M0L6_2atmpS1337;
        int32_t _M0L6_2atmpS1339;
        if (
          _M0L6_2atmpS1338 < 0
          || _M0L6_2atmpS1338 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1337 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1338];
        if (
          _M0L6_2atmpS1336 < 0
          || _M0L6_2atmpS1336 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1336] = _M0L6_2atmpS1337;
        _M0L6_2atmpS1339 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1339;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1344 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1344;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1340 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1342 = _M0L11src__offsetS39 + _M0L1iS43;
        int32_t _M0L6_2atmpS1341;
        int32_t _M0L6_2atmpS1343;
        if (
          _M0L6_2atmpS1342 < 0
          || _M0L6_2atmpS1342 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1341 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1342];
        if (
          _M0L6_2atmpS1340 < 0
          || _M0L6_2atmpS1340 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1340] = _M0L6_2atmpS1341;
        _M0L6_2atmpS1343 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1343;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  moonbit_string_t* _M0L3srcS46,
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
        int32_t _M0L6_2atmpS1345 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1347 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS1346;
        moonbit_string_t _M0L6_2aoldS2703;
        int32_t _M0L6_2atmpS1348;
        if (
          _M0L6_2atmpS1347 < 0
          || _M0L6_2atmpS1347 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1346 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1347];
        if (
          _M0L6_2atmpS1345 < 0
          || _M0L6_2atmpS1345 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2703 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1345];
        moonbit_incref_cycle_free(_M0L6_2atmpS1346);
        moonbit_decref_cycle_free(_M0L6_2aoldS2703);
        _M0L3dstS45[_M0L6_2atmpS1345] = _M0L6_2atmpS1346;
        _M0L6_2atmpS1348 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1348;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1353 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1353;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1349 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1351 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1350;
        moonbit_string_t _M0L6_2aoldS2704;
        int32_t _M0L6_2atmpS1352;
        if (
          _M0L6_2atmpS1351 < 0
          || _M0L6_2atmpS1351 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1350 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1351];
        if (
          _M0L6_2atmpS1349 < 0
          || _M0L6_2atmpS1349 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2704 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1349];
        moonbit_incref_cycle_free(_M0L6_2atmpS1350);
        moonbit_decref_cycle_free(_M0L6_2aoldS2704);
        _M0L3dstS45[_M0L6_2atmpS1349] = _M0L6_2atmpS1350;
        _M0L6_2atmpS1352 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1352;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS54,
  int32_t _M0L11dst__offsetS56,
  struct _M0TUsiE** _M0L3srcS55,
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
        int32_t _M0L6_2atmpS1354 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1356 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS1355;
        struct _M0TUsiE* _M0L6_2aoldS2705;
        int32_t _M0L6_2atmpS1357;
        if (
          _M0L6_2atmpS1356 < 0
          || _M0L6_2atmpS1356 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1355 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1356];
        if (
          _M0L6_2atmpS1354 < 0
          || _M0L6_2atmpS1354 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2705 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1354];
        if (_M0L6_2atmpS1355) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1355);
        }
        if (_M0L6_2aoldS2705) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2705);
        }
        _M0L3dstS54[_M0L6_2atmpS1354] = _M0L6_2atmpS1355;
        _M0L6_2atmpS1357 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1357;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1362 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1362;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1358 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1360 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1359;
        struct _M0TUsiE* _M0L6_2aoldS2706;
        int32_t _M0L6_2atmpS1361;
        if (
          _M0L6_2atmpS1360 < 0
          || _M0L6_2atmpS1360 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1359 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1360];
        if (
          _M0L6_2atmpS1358 < 0
          || _M0L6_2atmpS1358 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2706 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1358];
        if (_M0L6_2atmpS1359) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1359);
        }
        if (_M0L6_2aoldS2706) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2706);
        }
        _M0L3dstS54[_M0L6_2atmpS1358] = _M0L6_2atmpS1359;
        _M0L6_2atmpS1361 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1361;
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS16) {
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_32.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_33.data);
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS5) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS7
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1288) {
  switch (Moonbit_object_tag(_M0L4_2aeS1288)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_34.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1288);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1313,
  struct _M0TPB4Show _M0L8_2aparamS1312
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1311 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1313;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1311, _M0L8_2aparamS1312);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1310,
  struct _M0TPB4Show _M0L8_2aparamS1309
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1308 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1310;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1308, _M0L8_2aparamS1309);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1307,
  int32_t _M0L8_2aparamS1306
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1305 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1307;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1305, _M0L8_2aparamS1306);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1304,
  struct _M0TPC16string10StringView _M0L8_2aparamS1303
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1302 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1304;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1302, _M0L8_2aparamS1303);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1301,
  moonbit_string_t _M0L8_2aparamS1298,
  int32_t _M0L8_2aparamS1299,
  int32_t _M0L8_2aparamS1300
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1297 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1301;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1297, _M0L8_2aparamS1298, _M0L8_2aparamS1299, _M0L8_2aparamS1300);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1296,
  moonbit_string_t _M0L8_2aparamS1295
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1294 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1296;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1294, _M0L8_2aparamS1295);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2895 = 9218868437227405311ll;
  int64_t _tmp_2896;
  int64_t _tmp_2897;
  int64_t _tmp_2898;
  int64_t _tmp_2899;
  _M0FPB18double__max__value = *(double*)&_tmp_2895;
  _tmp_2896 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2896;
  _tmp_2897 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2897;
  _tmp_2898 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2898;
  _tmp_2899 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2899;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1317;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1281;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1282;
  int32_t _M0L7_2abindS1283;
  struct _M0TUsiE** _M0L7_2abindS1284;
  int32_t _M0L6_2acntS2717;
  int32_t _M0L2__S1285;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1317
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1281
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1281)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 91, 0);
  _M0L12async__testsS1281->$0 = _M0L6_2atmpS1317;
  _M0L12async__testsS1281->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1282
  = _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1283 = _M0L7_2abindS1282->$1;
  _M0L7_2abindS1284 = _M0L7_2abindS1282->$0;
  _M0L6_2acntS2717
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1282));
  if (_M0L6_2acntS2717 > 1) {
    int32_t _M0L11_2anew__cntS2718 = _M0L6_2acntS2717 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1282), _M0L11_2anew__cntS2718);
    moonbit_incref_cycle_free(_M0L7_2abindS1284);
  } else if (_M0L6_2acntS2717 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1282);
  }
  _M0L2__S1285 = 0;
  while (1) {
    if (_M0L2__S1285 < _M0L7_2abindS1283) {
      struct _M0TUsiE* _M0L3argS1286 =
        (struct _M0TUsiE*)_M0L7_2abindS1284[_M0L2__S1285];
      moonbit_string_t _M0L6_2atmpS1314 = _M0L3argS1286->$0;
      int32_t _M0L6_2atmpS1315 = _M0L3argS1286->$1;
      int32_t _M0L6_2atmpS1316;
      moonbit_incref_cycle_free(_M0L6_2atmpS1314);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1281, _M0L6_2atmpS1314, _M0L6_2atmpS1315);
      moonbit_decref_cycle_free(_M0L6_2atmpS1314);
      _M0L6_2atmpS1316 = _M0L2__S1285 + 1;
      _M0L2__S1285 = _M0L6_2atmpS1316;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1284);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\aggregate_scaling\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34aggregate__scaling__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1281);
  moonbit_decref_cycle_free(_M0L12async__testsS1281);
  moonbit_flush_cycles();
  return 0;
}