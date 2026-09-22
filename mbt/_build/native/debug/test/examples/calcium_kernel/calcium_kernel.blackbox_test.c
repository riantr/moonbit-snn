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
struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt14STDPMexicanHat;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TWEu;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TWRPC15error5ErrorEu;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB6Logger {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt12STDPGerstner {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  
};

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
};

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TPB8MutLocalGfE {
  float $0;
  
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

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err {
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

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt14STDPMexicanHat {
  float $0;
  float $1;
  float $2;
  float $3;
  
};

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGsE {
  moonbit_string_t* $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok {
  int32_t $0;
  
};

struct _M0DTPC16option6OptionGfE4Some {
  float $0;
  
};

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
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

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TWRPC15error5ErrorEu {
  int32_t(* code)(struct _M0TWRPC15error5ErrorEu*, void*);
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS930(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS923(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS918(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS895(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S888(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

float _M0FP26RiantR8snn__mbt26stdp__weight__decorrelated(
  struct _M0TP26RiantR8snn__mbt12STDPGerstner*
);

int32_t _M0FP26RiantR8snn__mbt32stdp__mexican__hat__plot_2einner(
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat*,
  float,
  int32_t,
  int32_t
);

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float);

int32_t _M0FP26RiantR8snn__mbt26stdp__kernel__plot_2einner(
  struct _M0TP26RiantR8snn__mbt12STDPGerstner*,
  float,
  int32_t,
  int32_t
);

moonbit_string_t _M0FP26RiantR8snn__mbt27format__axis__label__kernel(float);

float _M0FP26RiantR8snn__mbt16gerstner__kernel(
  float,
  float,
  float,
  float,
  float
);

#define _M0FP26RiantR8snn__mbt4expf expf

moonbit_string_t _M0FP26RiantR8snn__mbt19row__int__set__char(
  moonbit_string_t,
  int32_t,
  int32_t
);

int32_t _M0MPC15float5Float7is__nan(float);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

float _M0MPC15float5Float5round(float);

float _M0MPC15float5Float5floor(float);

float _M0MPC15float5Float5trunc(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGsE* _M0MPC15array5Array4makeGsE(
  int32_t,
  moonbit_string_t
);

int32_t _M0MPC15array5Array3setGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t,
  moonbit_string_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

struct _M0TPB5ArrayGsE* _M0MPC15array5Array20unsafe__make__uninitGsE(int32_t);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE*,
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

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(moonbit_string_t);

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder*,
  struct _M0TPC16string10StringView
);

moonbit_string_t _M0MPC16string6String4make(int32_t, int32_t);

#define _M0FPB20unsafe__make__string moonbit_unsafe_make_string

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

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float*,
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

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float*,
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

moonbit_string_t _M0FPC15abort5abortGsE(moonbit_string_t);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t
);

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t
);

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
} const moonbit_string_literal_31 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_29 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_45 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_28 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 83, 84, 
    68, 80, 77, 101, 120, 105, 99, 97, 110, 72, 97, 116, 32, 107, 101, 
    114, 110, 101, 108, 58, 32, 65, 32, 42, 32, 40, 49, 45, 120, 41, 
    32, 42, 32, 101, 120, 112, 40, 45, 120, 47, 115, 113, 114, 116, 40, 
    50, 41, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[121]; 
} const moonbit_string_literal_43 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 120, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 97, 108, 99, 105, 117, 109, 
    95, 107, 101, 114, 110, 101, 108, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 32, 8594, 
    32, 43, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_32 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 93, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_41 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 32, 124, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[15]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 14, 105, 110, 
    118, 97, 108, 105, 100, 32, 108, 101, 110, 103, 116, 104, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_30 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 32, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_42 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_44 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 97, 108, 99, 105, 117, 109, 
    95, 107, 101, 114, 110, 101, 108, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 
    111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[33]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 32, 45, 45, 
    45, 45, 45, 32, 69, 78, 68, 32, 77, 79, 79, 78, 32, 84, 69, 83, 84, 
    32, 82, 69, 83, 85, 76, 84, 32, 45, 45, 45, 45, 45, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_36 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[30]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 29, 120, 32, 
    61, 32, 40, 108, 110, 40, 116, 112, 114, 101, 47, 116, 112, 111, 
    115, 116, 41, 41, 94, 50, 32, 8712, 32, 91, 48, 44, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_24 =
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
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[36]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 35, 83, 84, 
    68, 80, 32, 107, 101, 114, 110, 101, 108, 32, 40, 71, 101, 114, 115, 
    116, 110, 101, 114, 32, 49, 57, 57, 54, 41, 58, 32, 916, 87, 40, 
    916, 116, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 32, 109, 115, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 916, 116, 
    32, 61, 32, 45, 0
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS930$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS930
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[39] =
  {
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918, $1)
    / 4
    * 2,
    sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923)
    / 4, 1,
    offsetof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2003
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS951,
  moonbit_string_t _M0L8filenameS920,
  int32_t _M0L5indexS922
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918* _closure_2027;
  struct _M0TWEu* _M0L13handle__startS918;
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923* _closure_2028;
  struct _M0TWssbEu* _M0L14handle__resultS923;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS930;
  void* _M0L11_2atry__errS945;
  struct moonbit_result_0 _tmp_2030;
  int32_t _handle__error__result_2031;
  int32_t _M0L6_2atmpS1991;
  void* _M0L3errS946;
  moonbit_string_t _M0L4nameS948;
  struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS949;
  moonbit_string_t _M0L7_2anameS950;
  int32_t _M0L6_2acntS2021;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS920);
  _closure_2027
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918));
  Moonbit_object_header(_closure_2027)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2027->code
  = &_M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS918;
  _closure_2027->$0 = _M0L5indexS922;
  _closure_2027->$1 = _M0L8filenameS920;
  _M0L13handle__startS918 = (struct _M0TWEu*)_closure_2027;
  moonbit_incref_cycle_free(_M0L8filenameS920);
  _closure_2028
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923*)moonbit_malloc(sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923));
  Moonbit_object_header(_closure_2028)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2028->code
  = &_M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS923;
  _closure_2028->$0 = _M0L5indexS922;
  _closure_2028->$1 = _M0L8filenameS920;
  _M0L14handle__resultS923 = (struct _M0TWssbEu*)_closure_2028;
  _M0L17error__to__stringS930
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS930$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2030
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS951, _M0L8filenameS920, _M0L5indexS922, _M0L13handle__startS918, _M0L14handle__resultS923, _M0L17error__to__stringS930);
  if (_tmp_2030.tag) {
    int32_t const _M0L5_2aokS2000 = _tmp_2030.data.ok;
    _handle__error__result_2031 = _M0L5_2aokS2000;
  } else {
    void* const _M0L6_2aerrS2001 = _tmp_2030.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS930);
    moonbit_decref_cycle_free(_M0L13handle__startS918);
    _M0L11_2atry__errS945 = _M0L6_2aerrS2001;
    goto join_944;
  }
  if (_handle__error__result_2031) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS930);
    moonbit_decref_cycle_free(_M0L13handle__startS918);
    _M0L6_2atmpS1991 = 1;
  } else {
    struct moonbit_result_0 _tmp_2032;
    int32_t _handle__error__result_2033;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2032
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS951, _M0L8filenameS920, _M0L5indexS922, _M0L13handle__startS918, _M0L14handle__resultS923, _M0L17error__to__stringS930);
    if (_tmp_2032.tag) {
      int32_t const _M0L5_2aokS1998 = _tmp_2032.data.ok;
      _handle__error__result_2033 = _M0L5_2aokS1998;
    } else {
      void* const _M0L6_2aerrS1999 = _tmp_2032.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS930);
      moonbit_decref_cycle_free(_M0L13handle__startS918);
      _M0L11_2atry__errS945 = _M0L6_2aerrS1999;
      goto join_944;
    }
    if (_handle__error__result_2033) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS930);
      moonbit_decref_cycle_free(_M0L13handle__startS918);
      _M0L6_2atmpS1991 = 1;
    } else {
      struct moonbit_result_0 _tmp_2034;
      int32_t _handle__error__result_2035;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2034
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS951, _M0L8filenameS920, _M0L5indexS922, _M0L13handle__startS918, _M0L14handle__resultS923, _M0L17error__to__stringS930);
      if (_tmp_2034.tag) {
        int32_t const _M0L5_2aokS1996 = _tmp_2034.data.ok;
        _handle__error__result_2035 = _M0L5_2aokS1996;
      } else {
        void* const _M0L6_2aerrS1997 = _tmp_2034.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS930);
        moonbit_decref_cycle_free(_M0L13handle__startS918);
        _M0L11_2atry__errS945 = _M0L6_2aerrS1997;
        goto join_944;
      }
      if (_handle__error__result_2035) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS930);
        moonbit_decref_cycle_free(_M0L13handle__startS918);
        _M0L6_2atmpS1991 = 1;
      } else {
        struct moonbit_result_0 _tmp_2036;
        int32_t _handle__error__result_2037;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2036
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS951, _M0L8filenameS920, _M0L5indexS922, _M0L13handle__startS918, _M0L14handle__resultS923, _M0L17error__to__stringS930);
        if (_tmp_2036.tag) {
          int32_t const _M0L5_2aokS1994 = _tmp_2036.data.ok;
          _handle__error__result_2037 = _M0L5_2aokS1994;
        } else {
          void* const _M0L6_2aerrS1995 = _tmp_2036.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS930);
          moonbit_decref_cycle_free(_M0L13handle__startS918);
          _M0L11_2atry__errS945 = _M0L6_2aerrS1995;
          goto join_944;
        }
        if (_handle__error__result_2037) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS930);
          moonbit_decref_cycle_free(_M0L13handle__startS918);
          _M0L6_2atmpS1991 = 1;
        } else {
          struct moonbit_result_0 _tmp_2038;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2038
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS951, _M0L8filenameS920, _M0L5indexS922, _M0L13handle__startS918, _M0L14handle__resultS923, _M0L17error__to__stringS930);
          moonbit_decref_cycle_free(_M0L13handle__startS918);
          moonbit_decref_cycle_free(_M0L17error__to__stringS930);
          if (_tmp_2038.tag) {
            int32_t const _M0L5_2aokS1992 = _tmp_2038.data.ok;
            _M0L6_2atmpS1991 = _M0L5_2aokS1992;
          } else {
            void* const _M0L6_2aerrS1993 = _tmp_2038.data.err;
            _M0L11_2atry__errS945 = _M0L6_2aerrS1993;
            goto join_944;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1991) {
    void* _M0L134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2002 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2002)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2002)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS945
    = _M0L134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2002;
    goto join_944;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS923);
  }
  goto joinlet_2029;
  join_944:;
  _M0L3errS946 = _M0L11_2atry__errS945;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS949
  = (struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS946;
  _M0L7_2anameS950 = _M0L36_2aMoonBitTestDriverInternalSkipTestS949->$0;
  _M0L6_2acntS2021
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS949));
  if (_M0L6_2acntS2021 > 1) {
    int32_t _M0L11_2anew__cntS2022 = _M0L6_2acntS2021 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS949), _M0L11_2anew__cntS2022);
    moonbit_incref_cycle_free(_M0L7_2anameS950);
  } else if (_M0L6_2acntS2021 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS949);
  }
  _M0L4nameS948 = _M0L7_2anameS950;
  goto join_947;
  goto joinlet_2039;
  join_947:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS923(_M0L14handle__resultS923, _M0L4nameS948, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS923);
  moonbit_decref_cycle_free(_M0L4nameS948);
  joinlet_2039:;
  joinlet_2029:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS930(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1990,
  void* _M0L3errS931
) {
  void* _M0L1eS933;
  moonbit_string_t _M0L1eS935;
  moonbit_string_t _result_2042;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS931)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS936 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS931;
      moonbit_string_t _M0L4_2aeS937 = _M0L10_2aFailureS936->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS937);
      _M0L1eS935 = _M0L4_2aeS937;
      goto join_934;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS938 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS931;
      moonbit_string_t _M0L4_2aeS939 = _M0L15_2aInspectErrorS938->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS939);
      _M0L1eS935 = _M0L4_2aeS939;
      goto join_934;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS940 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS931;
      moonbit_string_t _M0L4_2aeS941 = _M0L16_2aSnapshotErrorS940->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS941);
      _M0L1eS935 = _M0L4_2aeS941;
      goto join_934;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS942 =
        (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS931;
      moonbit_string_t _M0L4_2aeS943 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS942->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS943);
      _M0L1eS935 = _M0L4_2aeS943;
      goto join_934;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS931);
      _M0L1eS933 = _M0L3errS931;
      goto join_932;
      break;
    }
  }
  join_934:;
  return _M0L1eS935;
  join_932:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_2042 = _M0FP15Error10to__string(_M0L1eS933);
  moonbit_decref_cycle_free(_M0L1eS933);
  return _result_2042;
}

int32_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS923(
  struct _M0TWssbEu* _M0L6_2aenvS1987,
  moonbit_string_t _M0L10__testnameS924,
  moonbit_string_t _M0L7messageS925,
  int32_t _M0L7skippedS926
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923* _M0L14_2acasted__envS1988;
  moonbit_string_t _M0L8filenameS920;
  int32_t _M0L5indexS922;
  moonbit_string_t _M0L10file__nameS927;
  moonbit_string_t _M0L7messageS928;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS929;
  moonbit_string_t _M0L6_2atmpS1989;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1988
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c923*)_M0L6_2aenvS1987;
  _M0L8filenameS920 = _M0L14_2acasted__envS1988->$1;
  _M0L5indexS922 = _M0L14_2acasted__envS1988->$0;
  if (!_M0L7skippedS926 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS927
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS920, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS928
  = _M0MPC16string6String14escape_2einner(_M0L7messageS925, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS929
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS929, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS929, _M0L10file__nameS927);
  moonbit_decref_cycle_free(_M0L10file__nameS927);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS929, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS929, _M0L5indexS922);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS929, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS929, _M0L7messageS928);
  moonbit_decref_cycle_free(_M0L7messageS928);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS929, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1989
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS929);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS929);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1989);
  moonbit_decref_cycle_free(_M0L6_2atmpS1989);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS918(
  struct _M0TWEu* _M0L6_2aenvS1984
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918* _M0L14_2acasted__envS1985;
  moonbit_string_t _M0L8filenameS920;
  int32_t _M0L5indexS922;
  moonbit_string_t _M0L10file__nameS919;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS921;
  moonbit_string_t _M0L6_2atmpS1986;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1985
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fcalcium__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c918*)_M0L6_2aenvS1984;
  _M0L8filenameS920 = _M0L14_2acasted__envS1985->$1;
  _M0L5indexS922 = _M0L14_2acasted__envS1985->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS919
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS920, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS921
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS921, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS921, _M0L10file__nameS919);
  moonbit_decref_cycle_free(_M0L10file__nameS919);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS921, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS921, _M0L5indexS922);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS921, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1986
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS921);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS921);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1986);
  moonbit_decref_cycle_free(_M0L6_2atmpS1986);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S888;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS895;
  struct _M0TUsiE** _M0L6_2atmpS1983;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS902;
  moonbit_string_t* _M0L9cli__argsS903;
  moonbit_string_t _M0L6_2atmpS1982;
  moonbit_string_t _M0L6_2atmpS1981;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS904;
  int32_t _M0L7_2abindS905;
  moonbit_string_t* _M0L7_2abindS906;
  int32_t _M0L6_2acntS2023;
  int32_t _M0L2__S907;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S888 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS895 = 0;
  _M0L6_2atmpS1983 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS902
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS902)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS902->$0 = _M0L6_2atmpS1983;
  _M0L16file__and__indexS902->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS903
  = _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS903)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1982 = (moonbit_string_t)_M0L9cli__argsS903[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1982);
  moonbit_decref_cycle_free(_M0L9cli__argsS903);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1981
  = _M0MP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1982);
  moonbit_decref_cycle_free(_M0L6_2atmpS1982);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS904
  = _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS895(_M0L51moonbit__test__driver__internal__split__mbt__stringS895, _M0L6_2atmpS1981, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1981);
  _M0L7_2abindS905 = _M0L10test__argsS904->$1;
  _M0L7_2abindS906 = _M0L10test__argsS904->$0;
  _M0L6_2acntS2023
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS904));
  if (_M0L6_2acntS2023 > 1) {
    int32_t _M0L11_2anew__cntS2024 = _M0L6_2acntS2023 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS904), _M0L11_2anew__cntS2024);
    moonbit_incref_cycle_free(_M0L7_2abindS906);
  } else if (_M0L6_2acntS2023 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS904);
  }
  _M0L2__S907 = 0;
  while (1) {
    if (_M0L2__S907 < _M0L7_2abindS905) {
      moonbit_string_t _M0L3argS908 =
        (moonbit_string_t)_M0L7_2abindS906[_M0L2__S907];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS909;
      moonbit_string_t _M0L4fileS910;
      moonbit_string_t _M0L5rangeS911;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS912;
      moonbit_string_t _M0L6_2atmpS1979;
      int32_t _M0L5startS913;
      moonbit_string_t _M0L6_2atmpS1978;
      int32_t _M0L3endS914;
      int32_t _M0L1iS915;
      int32_t _M0L6_2atmpS1980;
      moonbit_incref_cycle_free(_M0L3argS908);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS909
      = _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS895(_M0L51moonbit__test__driver__internal__split__mbt__stringS895, _M0L3argS908, 58);
      moonbit_decref_cycle_free(_M0L3argS908);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS910
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS909, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS911
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS909, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS909);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS912
      = _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS895(_M0L51moonbit__test__driver__internal__split__mbt__stringS895, _M0L5rangeS911, 45);
      moonbit_decref_cycle_free(_M0L5rangeS911);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1979
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS912, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS913
      = _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S888(_M0L45moonbit__test__driver__internal__parse__int__S888, _M0L6_2atmpS1979);
      moonbit_decref_cycle_free(_M0L6_2atmpS1979);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1978
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS912, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS912);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS914
      = _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S888(_M0L45moonbit__test__driver__internal__parse__int__S888, _M0L6_2atmpS1978);
      moonbit_decref_cycle_free(_M0L6_2atmpS1978);
      _M0L1iS915 = _M0L5startS913;
      while (1) {
        if (_M0L1iS915 < _M0L3endS914) {
          struct _M0TUsiE* _M0L8_2atupleS1976;
          int32_t _M0L6_2atmpS1977;
          moonbit_incref_cycle_free(_M0L4fileS910);
          _M0L8_2atupleS1976
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1976)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1976->$0 = _M0L4fileS910;
          _M0L8_2atupleS1976->$1 = _M0L1iS915;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS902, _M0L8_2atupleS1976);
          _M0L6_2atmpS1977 = _M0L1iS915 + 1;
          _M0L1iS915 = _M0L6_2atmpS1977;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS910);
        }
        break;
      }
      _M0L6_2atmpS1980 = _M0L2__S907 + 1;
      _M0L2__S907 = _M0L6_2atmpS1980;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS906);
    }
    break;
  }
  return _M0L16file__and__indexS902;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS895(
  int32_t _M0L6_2aenvS1957,
  moonbit_string_t _M0L1sS896,
  int32_t _M0L3sepS897
) {
  moonbit_string_t* _M0L6_2atmpS1975;
  struct _M0TPB5ArrayGsE* _M0L3resS898;
  struct _M0TPB8MutLocalGiE* _M0L1iS899;
  struct _M0TPB8MutLocalGiE* _M0L5startS900;
  int32_t _M0L3valS1970;
  int32_t _M0L6_2atmpS1971;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1975 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS898
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS898)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS898->$0 = _M0L6_2atmpS1975;
  _M0L3resS898->$1 = 0;
  _M0L1iS899
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS899)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS899->$0 = 0;
  _M0L5startS900
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS900)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS900->$0 = 0;
  while (1) {
    int32_t _M0L3valS1958 = _M0L1iS899->$0;
    int32_t _M0L6_2atmpS1959 = Moonbit_array_length(_M0L1sS896);
    if (_M0L3valS1958 < _M0L6_2atmpS1959) {
      int32_t _M0L3valS1962 = _M0L1iS899->$0;
      int32_t _M0L6_2atmpS1961;
      int32_t _M0L6_2atmpS1960;
      int32_t _M0L3valS1969;
      int32_t _M0L6_2atmpS1968;
      if (
        _M0L3valS1962 < 0
        || _M0L3valS1962 >= Moonbit_array_length(_M0L1sS896)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1961 = _M0L1sS896[_M0L3valS1962];
      _M0L6_2atmpS1960 = _M0L6_2atmpS1961;
      if (_M0L6_2atmpS1960 == _M0L3sepS897) {
        int32_t _M0L3valS1964 = _M0L5startS900->$0;
        int32_t _M0L3valS1965 = _M0L1iS899->$0;
        moonbit_string_t _M0L6_2atmpS1963;
        int32_t _M0L3valS1967;
        int32_t _M0L6_2atmpS1966;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1963
        = _M0MPC16string6String17unsafe__substring(_M0L1sS896, _M0L3valS1964, _M0L3valS1965);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS898, _M0L6_2atmpS1963);
        _M0L3valS1967 = _M0L1iS899->$0;
        _M0L6_2atmpS1966 = _M0L3valS1967 + 1;
        _M0L5startS900->$0 = _M0L6_2atmpS1966;
      }
      _M0L3valS1969 = _M0L1iS899->$0;
      _M0L6_2atmpS1968 = _M0L3valS1969 + 1;
      _M0L1iS899->$0 = _M0L6_2atmpS1968;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS899);
    }
    break;
  }
  _M0L3valS1970 = _M0L5startS900->$0;
  _M0L6_2atmpS1971 = Moonbit_array_length(_M0L1sS896);
  if (_M0L3valS1970 < _M0L6_2atmpS1971) {
    int32_t _M0L3valS1973 = _M0L5startS900->$0;
    int32_t _M0L6_2atmpS1974;
    moonbit_string_t _M0L6_2atmpS1972;
    moonbit_decref_cycle_free(_M0L5startS900);
    _M0L6_2atmpS1974 = Moonbit_array_length(_M0L1sS896);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1972
    = _M0MPC16string6String17unsafe__substring(_M0L1sS896, _M0L3valS1973, _M0L6_2atmpS1974);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS898, _M0L6_2atmpS1972);
  } else {
    moonbit_decref_cycle_free(_M0L5startS900);
  }
  return _M0L3resS898;
}

int32_t _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S888(
  int32_t _M0L6_2aenvS1950,
  moonbit_string_t _M0L1sS889
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS890;
  int32_t _M0L3lenS891;
  int32_t _M0L7_2abindS892;
  int32_t _M0L1iS893;
  int32_t _result_2047;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS890
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS890)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS890->$0 = 0;
  _M0L3lenS891 = Moonbit_array_length(_M0L1sS889);
  _M0L7_2abindS892 = 0;
  _M0L1iS893 = _M0L7_2abindS892;
  while (1) {
    if (_M0L1iS893 < _M0L3lenS891) {
      int32_t _M0L3valS1955 = _M0L3resS890->$0;
      int32_t _M0L6_2atmpS1952 = _M0L3valS1955 * 10;
      int32_t _M0L6_2atmpS1954;
      int32_t _M0L6_2atmpS1953;
      int32_t _M0L6_2atmpS1951;
      int32_t _M0L6_2atmpS1956;
      if (_M0L1iS893 < 0 || _M0L1iS893 >= Moonbit_array_length(_M0L1sS889)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1954 = _M0L1sS889[_M0L1iS893];
      _M0L6_2atmpS1953 = _M0L6_2atmpS1954 - 48;
      _M0L6_2atmpS1951 = _M0L6_2atmpS1952 + _M0L6_2atmpS1953;
      _M0L3resS890->$0 = _M0L6_2atmpS1951;
      _M0L6_2atmpS1956 = _M0L1iS893 + 1;
      _M0L1iS893 = _M0L6_2atmpS1956;
      continue;
    }
    break;
  }
  _result_2047 = _M0L3resS890->$0;
  moonbit_decref_cycle_free(_M0L3resS890);
  return _result_2047;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS887
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS887);
  return _M0L4selfS887;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S857,
  moonbit_string_t _M0L12_2adiscard__S858,
  int32_t _M0L12_2adiscard__S859,
  struct _M0TWEu* _M0L12_2adiscard__S860,
  struct _M0TWssbEu* _M0L12_2adiscard__S861,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S862
) {
  struct moonbit_result_0 _result_2048;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_2048.tag = 1;
  _result_2048.data.ok = 0;
  return _result_2048;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S863,
  moonbit_string_t _M0L12_2adiscard__S864,
  int32_t _M0L12_2adiscard__S865,
  struct _M0TWEu* _M0L12_2adiscard__S866,
  struct _M0TWssbEu* _M0L12_2adiscard__S867,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S868
) {
  struct moonbit_result_0 _result_2049;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_2049.tag = 1;
  _result_2049.data.ok = 0;
  return _result_2049;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S869,
  moonbit_string_t _M0L12_2adiscard__S870,
  int32_t _M0L12_2adiscard__S871,
  struct _M0TWEu* _M0L12_2adiscard__S872,
  struct _M0TWssbEu* _M0L12_2adiscard__S873,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S874
) {
  struct moonbit_result_0 _result_2050;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_2050.tag = 1;
  _result_2050.data.ok = 0;
  return _result_2050;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S875,
  moonbit_string_t _M0L12_2adiscard__S876,
  int32_t _M0L12_2adiscard__S877,
  struct _M0TWEu* _M0L12_2adiscard__S878,
  struct _M0TWssbEu* _M0L12_2adiscard__S879,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S880
) {
  struct moonbit_result_0 _result_2051;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_2051.tag = 1;
  _result_2051.data.ok = 0;
  return _result_2051;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S881,
  moonbit_string_t _M0L12_2adiscard__S882,
  int32_t _M0L12_2adiscard__S883,
  struct _M0TWEu* _M0L12_2adiscard__S884,
  struct _M0TWssbEu* _M0L12_2adiscard__S885,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S886
) {
  struct moonbit_result_0 _result_2052;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_2052.tag = 1;
  _result_2052.data.ok = 0;
  return _result_2052;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S856
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

float _M0FP26RiantR8snn__mbt26stdp__weight__decorrelated(
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS852
) {
  float _M0L6a__preS1948;
  float _M0L8tau__preS1949;
  float _M0L6_2atmpS1944;
  float _M0L7a__postS1946;
  float _M0L9tau__postS1947;
  float _M0L6_2atmpS1945;
  #line 891 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6a__preS1948 = _M0L5paramS852->$0;
  _M0L8tau__preS1949 = _M0L5paramS852->$2;
  _M0L6_2atmpS1944 = _M0L6a__preS1948 * _M0L8tau__preS1949;
  _M0L7a__postS1946 = _M0L5paramS852->$1;
  _M0L9tau__postS1947 = _M0L5paramS852->$3;
  _M0L6_2atmpS1945 = _M0L7a__postS1946 * _M0L9tau__postS1947;
  return _M0L6_2atmpS1944 - _M0L6_2atmpS1945;
}

int32_t _M0FP26RiantR8snn__mbt32stdp__mexican__hat__plot_2einner(
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS818,
  float _M0L6x__maxS816,
  int32_t _M0L5widthS810,
  int32_t _M0L6heightS822
) {
  int32_t _M0L7n__colsS809;
  struct _M0TPB8MutLocalGfE* _M0L2mnS811;
  struct _M0TPB8MutLocalGfE* _M0L2mxS812;
  float* _M0L6_2atmpS1943;
  struct _M0TPB5ArrayGfE* _M0L4valsS813;
  struct _M0TPB8MutLocalGiE* _M0L1kS814;
  float _M0L3valS1902;
  float _M0L3valS1903;
  float _M0L6_2atmpS1901;
  float _M0L3valS1941;
  float _M0L3valS1942;
  float _M0L6vrangeS820;
  struct _M0TPB5ArrayGsE* _M0L6canvasS821;
  moonbit_string_t _M0L3padS823;
  moonbit_string_t _M0L9row__initS824;
  int32_t _M0L7_2abindS825;
  int32_t _M0L1rS826;
  struct _M0TPB8MutLocalGiE* _M0L1cS828;
  float _M0L3valS1940;
  moonbit_string_t _M0L10max__labelS837;
  float _M0L3valS1938;
  float _M0L3valS1939;
  float _M0L6_2atmpS1937;
  float _M0L6_2atmpS1936;
  moonbit_string_t _M0L10mid__labelS838;
  float _M0L3valS1935;
  moonbit_string_t _M0L10min__labelS839;
  int32_t _M0L1aS841;
  int32_t _M0L1bS842;
  int32_t _M0L1cS843;
  int32_t _M0L1mS844;
  int32_t _M0L12label__widthS840;
  int32_t _M0L7_2abindS845;
  int32_t _M0L1rS846;
  int32_t _M0L6_2atmpS1934;
  moonbit_string_t _M0L8pad__strS851;
  moonbit_string_t _M0L6_2atmpS1932;
  moonbit_string_t _M0L6_2atmpS1933;
  moonbit_string_t _M0L6_2atmpS1931;
  moonbit_string_t _M0L6_2atmpS1930;
  #line 706 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0L5widthS810 > 1) {
    _M0L7n__colsS809 = _M0L5widthS810;
  } else {
    _M0L7n__colsS809 = 1;
  }
  _M0L2mnS811
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mnS811)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mnS811->$0 = 0x0p+0f;
  _M0L2mxS812
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mxS812)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mxS812->$0 = 0x0p+0f;
  _M0L6_2atmpS1943 = moonbit_empty_float_array;
  _M0L4valsS813
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS813)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS813->$0 = _M0L6_2atmpS1943;
  _M0L4valsS813->$1 = 0;
  _M0L1kS814
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS814)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS814->$0 = 0;
  while (1) {
    int32_t _M0L3valS1888 = _M0L1kS814->$0;
    if (_M0L3valS1888 < _M0L7n__colsS809) {
      int32_t _M0L3valS1900 = _M0L1kS814->$0;
      float _M0L6_2atmpS1899 = (float)_M0L3valS1900;
      float _M0L6_2atmpS1896 = _M0L6_2atmpS1899 * _M0L6x__maxS816;
      int32_t _M0L6_2atmpS1898 = _M0L7n__colsS809 - 1;
      float _M0L6_2atmpS1897 = (float)_M0L6_2atmpS1898;
      float _M0L1xS815 = _M0L6_2atmpS1896 / _M0L6_2atmpS1897;
      float _M0L1aS1894 = _M0L5paramS818->$0;
      float _M0L6_2atmpS1895;
      float _M0L1mS817;
      int32_t _M0L3valS1889;
      int32_t _M0L3valS1893;
      int32_t _M0L6_2atmpS1892;
      #line 720 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1895
      = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS815);
      _M0L1mS817 = _M0L1aS1894 * _M0L6_2atmpS1895;
      #line 721 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array4pushGfE(_M0L4valsS813, _M0L1mS817);
      _M0L3valS1889 = _M0L1kS814->$0;
      if (_M0L3valS1889 == 0) {
        _M0L2mnS811->$0 = _M0L1mS817;
        _M0L2mxS812->$0 = _M0L1mS817;
      } else {
        float _M0L3valS1890 = _M0L2mnS811->$0;
        float _M0L3valS1891;
        if (_M0L1mS817 < _M0L3valS1890) {
          _M0L2mnS811->$0 = _M0L1mS817;
        }
        _M0L3valS1891 = _M0L2mxS812->$0;
        if (_M0L1mS817 > _M0L3valS1891) {
          _M0L2mxS812->$0 = _M0L1mS817;
        }
      }
      _M0L3valS1893 = _M0L1kS814->$0;
      _M0L6_2atmpS1892 = _M0L3valS1893 + 1;
      _M0L1kS814->$0 = _M0L6_2atmpS1892;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS814);
    }
    break;
  }
  _M0L3valS1902 = _M0L2mxS812->$0;
  _M0L3valS1903 = _M0L2mnS811->$0;
  _M0L6_2atmpS1901 = _M0L3valS1902 - _M0L3valS1903;
  if (_M0L6_2atmpS1901 < 0x1.12e0be826d695p-30f) {
    float _M0L3valS1905 = _M0L2mnS811->$0;
    float _M0L6_2atmpS1904 = _M0L3valS1905 + 0x1.12e0be826d695p-30f;
    _M0L2mxS812->$0 = _M0L6_2atmpS1904;
  }
  _M0L3valS1941 = _M0L2mxS812->$0;
  _M0L3valS1942 = _M0L2mnS811->$0;
  _M0L6vrangeS820 = _M0L3valS1941 - _M0L3valS1942;
  #line 733 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6canvasS821
  = _M0MPC15array5Array4makeGsE(_M0L6heightS822, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 734 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3padS823 = _M0MPC16string6String4make(_M0L7n__colsS809, 32);
  _M0L9row__initS824 = (moonbit_string_t)moonbit_string_literal_9.data;
  _M0L7_2abindS825 = 0;
  _M0L1rS826 = _M0L7_2abindS825;
  while (1) {
    if (_M0L1rS826 < _M0L6heightS822) {
      moonbit_string_t _M0L6_2atmpS1906;
      int32_t _M0L6_2atmpS1907;
      #line 737 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1906 = moonbit_add_string(_M0L9row__initS824, _M0L3padS823);
      #line 737 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS821, _M0L1rS826, _M0L6_2atmpS1906);
      _M0L6_2atmpS1907 = _M0L1rS826 + 1;
      _M0L1rS826 = _M0L6_2atmpS1907;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3padS823);
    }
    break;
  }
  _M0L1cS828
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1cS828)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1cS828->$0 = 0;
  while (1) {
    int32_t _M0L3valS1908 = _M0L1cS828->$0;
    if (_M0L3valS1908 < _M0L7n__colsS809) {
      int32_t _M0L3valS1920 = _M0L1cS828->$0;
      float _M0L1wS829;
      float _M0L3valS1919;
      float _M0L6_2atmpS1918;
      float _M0L10normalizedS830;
      float _M0L6_2atmpS1915;
      float _M0L6_2atmpS1917;
      float _M0L6_2atmpS1916;
      float _M0L6_2atmpS1914;
      int32_t _M0L14row__from__topS831;
      int32_t _M0L1rS832;
      int32_t _M0L2chS833;
      moonbit_string_t _M0L8row__strS834;
      int32_t _M0L3valS1912;
      int32_t _M0L6_2atmpS1913;
      int32_t _M0L6_2atmpS1911;
      moonbit_string_t _M0L8new__rowS835;
      int32_t _M0L3valS1910;
      int32_t _M0L6_2atmpS1909;
      #line 741 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L1wS829 = _M0MPC15array5Array2atGfE(_M0L4valsS813, _M0L3valS1920);
      _M0L3valS1919 = _M0L2mnS811->$0;
      _M0L6_2atmpS1918 = _M0L1wS829 - _M0L3valS1919;
      _M0L10normalizedS830 = _M0L6_2atmpS1918 / _M0L6vrangeS820;
      _M0L6_2atmpS1915 = (float)_M0L6heightS822;
      _M0L6_2atmpS1917 = (float)1;
      _M0L6_2atmpS1916 = _M0L6_2atmpS1917 * _M0L10normalizedS830;
      _M0L6_2atmpS1914 = _M0L6_2atmpS1915 - _M0L6_2atmpS1916;
      #line 743 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L14row__from__topS831
      = _M0MPC15float5Float7to__int(_M0L6_2atmpS1914);
      if (_M0L14row__from__topS831 < 0) {
        _M0L1rS832 = 0;
      } else if (_M0L14row__from__topS831 >= _M0L6heightS822) {
        _M0L1rS832 = _M0L6heightS822 - 1;
      } else {
        _M0L1rS832 = _M0L14row__from__topS831;
      }
      if (_M0L1wS829 >= 0x0p+0f) {
        _M0L2chS833 = 42;
      } else {
        _M0L2chS833 = 46;
      }
      #line 752 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8row__strS834
      = _M0MPC15array5Array2atGsE(_M0L6canvasS821, _M0L1rS832);
      _M0L3valS1912 = _M0L1cS828->$0;
      _M0L6_2atmpS1913 = Moonbit_array_length(_M0L9row__initS824);
      _M0L6_2atmpS1911 = _M0L3valS1912 + _M0L6_2atmpS1913;
      #line 753 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8new__rowS835
      = _M0FP26RiantR8snn__mbt19row__int__set__char(_M0L8row__strS834, _M0L6_2atmpS1911, _M0L2chS833);
      moonbit_decref_cycle_free(_M0L8row__strS834);
      #line 754 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS821, _M0L1rS832, _M0L8new__rowS835);
      _M0L3valS1910 = _M0L1cS828->$0;
      _M0L6_2atmpS1909 = _M0L3valS1910 + 1;
      _M0L1cS828->$0 = _M0L6_2atmpS1909;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1cS828);
      moonbit_decref_cycle_free(_M0L9row__initS824);
      moonbit_decref_cycle_free(_M0L4valsS813);
    }
    break;
  }
  #line 757 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_10.data);
  _M0L3valS1940 = _M0L2mxS812->$0;
  #line 758 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10max__labelS837
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L3valS1940);
  _M0L3valS1938 = _M0L2mxS812->$0;
  moonbit_decref_cycle_free(_M0L2mxS812);
  _M0L3valS1939 = _M0L2mnS811->$0;
  _M0L6_2atmpS1937 = _M0L3valS1938 + _M0L3valS1939;
  _M0L6_2atmpS1936 = _M0L6_2atmpS1937 / 0x1p+1f;
  #line 759 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10mid__labelS838
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L6_2atmpS1936);
  _M0L3valS1935 = _M0L2mnS811->$0;
  moonbit_decref_cycle_free(_M0L2mnS811);
  #line 760 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10min__labelS839
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L3valS1935);
  _M0L1aS841 = Moonbit_array_length(_M0L10max__labelS837);
  _M0L1bS842 = Moonbit_array_length(_M0L10mid__labelS838);
  _M0L1cS843 = Moonbit_array_length(_M0L10min__labelS839);
  if (_M0L1aS841 > _M0L1bS842) {
    _M0L1mS844 = _M0L1aS841;
  } else {
    _M0L1mS844 = _M0L1bS842;
  }
  if (_M0L1mS844 > _M0L1cS843) {
    _M0L12label__widthS840 = _M0L1mS844;
  } else {
    _M0L12label__widthS840 = _M0L1cS843;
  }
  _M0L7_2abindS845 = 0;
  _M0L1rS846 = _M0L7_2abindS845;
  while (1) {
    if (_M0L1rS846 < _M0L6heightS822) {
      moonbit_string_t _M0L5labelS847;
      int32_t _M0L6_2atmpS1925;
      int32_t _M0L10pad__countS848;
      moonbit_string_t _M0L6paddedS849;
      moonbit_string_t _M0L6_2atmpS1924;
      moonbit_string_t _M0L6_2atmpS1922;
      moonbit_string_t _M0L6_2atmpS1923;
      moonbit_string_t _M0L6_2atmpS1921;
      int32_t _M0L6_2atmpS1929;
      if (_M0L1rS846 == 0) {
        moonbit_incref_cycle_free(_M0L10max__labelS837);
        _M0L5labelS847 = _M0L10max__labelS837;
      } else {
        int32_t _M0L6_2atmpS1927 = _M0L6heightS822 - 1;
        if (_M0L1rS846 == _M0L6_2atmpS1927) {
          moonbit_incref_cycle_free(_M0L10min__labelS839);
          _M0L5labelS847 = _M0L10min__labelS839;
        } else {
          int32_t _M0L6_2atmpS1928 = _M0L6heightS822 / 2;
          if (_M0L1rS846 == _M0L6_2atmpS1928) {
            moonbit_incref_cycle_free(_M0L10mid__labelS838);
            _M0L5labelS847 = _M0L10mid__labelS838;
          } else {
            _M0L5labelS847 = (moonbit_string_t)moonbit_string_literal_0.data;
          }
        }
      }
      _M0L6_2atmpS1925 = Moonbit_array_length(_M0L5labelS847);
      if (_M0L12label__widthS840 > _M0L6_2atmpS1925) {
        int32_t _M0L6_2atmpS1926 = Moonbit_array_length(_M0L5labelS847);
        _M0L10pad__countS848 = _M0L12label__widthS840 - _M0L6_2atmpS1926;
      } else {
        _M0L10pad__countS848 = 0;
      }
      #line 783 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6paddedS849 = _M0MPC16string6String4make(_M0L10pad__countS848, 32);
      #line 784 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1924 = moonbit_add_string(_M0L6paddedS849, _M0L5labelS847);
      moonbit_decref_cycle_free(_M0L5labelS847);
      moonbit_decref_cycle_free(_M0L6paddedS849);
      #line 784 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1922
      = moonbit_add_string(_M0L6_2atmpS1924, (moonbit_string_t)moonbit_string_literal_11.data);
      moonbit_decref_cycle_free(_M0L6_2atmpS1924);
      #line 784 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1923
      = _M0MPC15array5Array2atGsE(_M0L6canvasS821, _M0L1rS846);
      #line 784 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1921
      = moonbit_add_string(_M0L6_2atmpS1922, _M0L6_2atmpS1923);
      moonbit_decref_cycle_free(_M0L6_2atmpS1923);
      moonbit_decref_cycle_free(_M0L6_2atmpS1922);
      #line 784 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0FPB7printlnGsE(_M0L6_2atmpS1921);
      moonbit_decref_cycle_free(_M0L6_2atmpS1921);
      _M0L6_2atmpS1929 = _M0L1rS846 + 1;
      _M0L1rS846 = _M0L6_2atmpS1929;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L10min__labelS839);
      moonbit_decref_cycle_free(_M0L10mid__labelS838);
      moonbit_decref_cycle_free(_M0L10max__labelS837);
      moonbit_decref_cycle_free(_M0L6canvasS821);
    }
    break;
  }
  _M0L6_2atmpS1934 = _M0L12label__widthS840 + 3;
  #line 786 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L8pad__strS851 = _M0MPC16string6String4make(_M0L6_2atmpS1934, 32);
  #line 787 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1932
  = moonbit_add_string(_M0L8pad__strS851, (moonbit_string_t)moonbit_string_literal_12.data);
  moonbit_decref_cycle_free(_M0L8pad__strS851);
  #line 787 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1933 = _M0IPC15float5FloatPB4Show10to__string(_M0L6x__maxS816);
  #line 787 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1931 = moonbit_add_string(_M0L6_2atmpS1932, _M0L6_2atmpS1933);
  moonbit_decref_cycle_free(_M0L6_2atmpS1933);
  moonbit_decref_cycle_free(_M0L6_2atmpS1932);
  #line 787 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1930
  = moonbit_add_string(_M0L6_2atmpS1931, (moonbit_string_t)moonbit_string_literal_13.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS1931);
  #line 787 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1930);
  moonbit_decref_cycle_free(_M0L6_2atmpS1930);
  return 0;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS806) {
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 428 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS806)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS1887 = -_M0L1xS806;
    float _M0L3argS807 = _M0L6_2atmpS1887 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS1885 = 0x1p+0f - _M0L1xS806;
    float _M0L6_2atmpS1886;
    float _M0L1vS808;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS1886 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS807);
    _M0L1vS808 = _M0L6_2atmpS1885 * _M0L6_2atmpS1886;
    #line 433 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS808)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS808;
    }
  }
}

int32_t _M0FP26RiantR8snn__mbt26stdp__kernel__plot_2einner(
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS772,
  float _M0L6t__maxS770,
  int32_t _M0L5widthS764,
  int32_t _M0L6heightS776
) {
  int32_t _M0L7n__colsS763;
  struct _M0TPB8MutLocalGfE* _M0L2mnS765;
  struct _M0TPB8MutLocalGfE* _M0L2mxS766;
  float* _M0L6_2atmpS1884;
  struct _M0TPB5ArrayGfE* _M0L4valsS767;
  struct _M0TPB8MutLocalGiE* _M0L1iS768;
  float _M0L3valS1840;
  float _M0L3valS1841;
  float _M0L6_2atmpS1839;
  float _M0L3valS1882;
  float _M0L3valS1883;
  float _M0L6vrangeS774;
  struct _M0TPB5ArrayGsE* _M0L6canvasS775;
  moonbit_string_t _M0L3padS777;
  moonbit_string_t _M0L9row__initS778;
  int32_t _M0L7_2abindS779;
  int32_t _M0L1rS780;
  struct _M0TPB8MutLocalGiE* _M0L1cS782;
  float _M0L3valS1881;
  moonbit_string_t _M0L10max__labelS791;
  float _M0L3valS1879;
  float _M0L3valS1880;
  float _M0L6_2atmpS1878;
  float _M0L6_2atmpS1877;
  moonbit_string_t _M0L10mid__labelS792;
  float _M0L3valS1876;
  moonbit_string_t _M0L10min__labelS793;
  int32_t _M0L1aS795;
  int32_t _M0L1bS796;
  int32_t _M0L1cS797;
  int32_t _M0L1mS798;
  int32_t _M0L12label__widthS794;
  int32_t _M0L7_2abindS799;
  int32_t _M0L1rS800;
  int32_t _M0L6_2atmpS1875;
  moonbit_string_t _M0L8pad__strS805;
  moonbit_string_t _M0L6_2atmpS1873;
  moonbit_string_t _M0L6_2atmpS1874;
  moonbit_string_t _M0L6_2atmpS1872;
  moonbit_string_t _M0L6_2atmpS1870;
  moonbit_string_t _M0L6_2atmpS1871;
  moonbit_string_t _M0L6_2atmpS1869;
  moonbit_string_t _M0L6_2atmpS1868;
  #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0L5widthS764 > 1) {
    _M0L7n__colsS763 = _M0L5widthS764;
  } else {
    _M0L7n__colsS763 = 1;
  }
  _M0L2mnS765
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mnS765)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mnS765->$0 = 0x0p+0f;
  _M0L2mxS766
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mxS766)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mxS766->$0 = 0x0p+0f;
  _M0L6_2atmpS1884 = moonbit_empty_float_array;
  _M0L4valsS767
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS767)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS767->$0 = _M0L6_2atmpS1884;
  _M0L4valsS767->$1 = 0;
  _M0L1iS768
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS768)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS768->$0 = 0;
  while (1) {
    int32_t _M0L3valS1821 = _M0L1iS768->$0;
    if (_M0L3valS1821 < _M0L7n__colsS763) {
      float _M0L6_2atmpS1831 = -_M0L6t__maxS770;
      int32_t _M0L3valS1838 = _M0L1iS768->$0;
      float _M0L6_2atmpS1836 = (float)_M0L3valS1838;
      float _M0L6_2atmpS1837 = 0x1p+1f * _M0L6t__maxS770;
      float _M0L6_2atmpS1833 = _M0L6_2atmpS1836 * _M0L6_2atmpS1837;
      int32_t _M0L6_2atmpS1835 = _M0L7n__colsS763 - 1;
      float _M0L6_2atmpS1834 = (float)_M0L6_2atmpS1835;
      float _M0L6_2atmpS1832 = _M0L6_2atmpS1833 / _M0L6_2atmpS1834;
      float _M0L2dtS769 = _M0L6_2atmpS1831 + _M0L6_2atmpS1832;
      float _M0L8tau__preS1827 = _M0L5paramS772->$2;
      float _M0L9tau__postS1828 = _M0L5paramS772->$3;
      float _M0L6a__preS1829 = _M0L5paramS772->$0;
      float _M0L7a__postS1830 = _M0L5paramS772->$1;
      float _M0L1wS771;
      int32_t _M0L3valS1822;
      int32_t _M0L3valS1826;
      int32_t _M0L6_2atmpS1825;
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L1wS771
      = _M0FP26RiantR8snn__mbt16gerstner__kernel(_M0L2dtS769, _M0L8tau__preS1827, _M0L9tau__postS1828, _M0L6a__preS1829, _M0L7a__postS1830);
      #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array4pushGfE(_M0L4valsS767, _M0L1wS771);
      _M0L3valS1822 = _M0L1iS768->$0;
      if (_M0L3valS1822 == 0) {
        _M0L2mnS765->$0 = _M0L1wS771;
        _M0L2mxS766->$0 = _M0L1wS771;
      } else {
        float _M0L3valS1823 = _M0L2mnS765->$0;
        float _M0L3valS1824;
        if (_M0L1wS771 < _M0L3valS1823) {
          _M0L2mnS765->$0 = _M0L1wS771;
        }
        _M0L3valS1824 = _M0L2mxS766->$0;
        if (_M0L1wS771 > _M0L3valS1824) {
          _M0L2mxS766->$0 = _M0L1wS771;
        }
      }
      _M0L3valS1826 = _M0L1iS768->$0;
      _M0L6_2atmpS1825 = _M0L3valS1826 + 1;
      _M0L1iS768->$0 = _M0L6_2atmpS1825;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS768);
    }
    break;
  }
  _M0L3valS1840 = _M0L2mxS766->$0;
  _M0L3valS1841 = _M0L2mnS765->$0;
  _M0L6_2atmpS1839 = _M0L3valS1840 - _M0L3valS1841;
  if (_M0L6_2atmpS1839 < 0x1.12e0be826d695p-30f) {
    float _M0L3valS1843 = _M0L2mnS765->$0;
    float _M0L6_2atmpS1842 = _M0L3valS1843 + 0x1.12e0be826d695p-30f;
    _M0L2mxS766->$0 = _M0L6_2atmpS1842;
  }
  _M0L3valS1882 = _M0L2mxS766->$0;
  _M0L3valS1883 = _M0L2mnS765->$0;
  _M0L6vrangeS774 = _M0L3valS1882 - _M0L3valS1883;
  #line 311 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6canvasS775
  = _M0MPC15array5Array4makeGsE(_M0L6heightS776, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3padS777 = _M0MPC16string6String4make(_M0L7n__colsS763, 32);
  _M0L9row__initS778 = (moonbit_string_t)moonbit_string_literal_9.data;
  _M0L7_2abindS779 = 0;
  _M0L1rS780 = _M0L7_2abindS779;
  while (1) {
    if (_M0L1rS780 < _M0L6heightS776) {
      moonbit_string_t _M0L6_2atmpS1844;
      int32_t _M0L6_2atmpS1845;
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1844 = moonbit_add_string(_M0L9row__initS778, _M0L3padS777);
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS775, _M0L1rS780, _M0L6_2atmpS1844);
      _M0L6_2atmpS1845 = _M0L1rS780 + 1;
      _M0L1rS780 = _M0L6_2atmpS1845;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3padS777);
    }
    break;
  }
  _M0L1cS782
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1cS782)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1cS782->$0 = 0;
  while (1) {
    int32_t _M0L3valS1846 = _M0L1cS782->$0;
    if (_M0L3valS1846 < _M0L7n__colsS763) {
      int32_t _M0L3valS1858 = _M0L1cS782->$0;
      float _M0L1wS783;
      float _M0L3valS1857;
      float _M0L6_2atmpS1856;
      float _M0L10normalizedS784;
      float _M0L6_2atmpS1853;
      float _M0L6_2atmpS1855;
      float _M0L6_2atmpS1854;
      float _M0L6_2atmpS1852;
      int32_t _M0L14row__from__topS785;
      int32_t _M0L1rS786;
      int32_t _M0L2chS787;
      moonbit_string_t _M0L8row__strS788;
      int32_t _M0L3valS1850;
      int32_t _M0L6_2atmpS1851;
      int32_t _M0L6_2atmpS1849;
      moonbit_string_t _M0L8new__rowS789;
      int32_t _M0L3valS1848;
      int32_t _M0L6_2atmpS1847;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L1wS783 = _M0MPC15array5Array2atGfE(_M0L4valsS767, _M0L3valS1858);
      _M0L3valS1857 = _M0L2mnS765->$0;
      _M0L6_2atmpS1856 = _M0L1wS783 - _M0L3valS1857;
      _M0L10normalizedS784 = _M0L6_2atmpS1856 / _M0L6vrangeS774;
      _M0L6_2atmpS1853 = (float)_M0L6heightS776;
      _M0L6_2atmpS1855 = (float)1;
      _M0L6_2atmpS1854 = _M0L6_2atmpS1855 * _M0L10normalizedS784;
      _M0L6_2atmpS1852 = _M0L6_2atmpS1853 - _M0L6_2atmpS1854;
      #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L14row__from__topS785
      = _M0MPC15float5Float7to__int(_M0L6_2atmpS1852);
      if (_M0L14row__from__topS785 < 0) {
        _M0L1rS786 = 0;
      } else if (_M0L14row__from__topS785 >= _M0L6heightS776) {
        _M0L1rS786 = _M0L6heightS776 - 1;
      } else {
        _M0L1rS786 = _M0L14row__from__topS785;
      }
      if (_M0L1wS783 >= 0x0p+0f) {
        _M0L2chS787 = 42;
      } else {
        _M0L2chS787 = 46;
      }
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8row__strS788
      = _M0MPC15array5Array2atGsE(_M0L6canvasS775, _M0L1rS786);
      _M0L3valS1850 = _M0L1cS782->$0;
      _M0L6_2atmpS1851 = Moonbit_array_length(_M0L9row__initS778);
      _M0L6_2atmpS1849 = _M0L3valS1850 + _M0L6_2atmpS1851;
      #line 332 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8new__rowS789
      = _M0FP26RiantR8snn__mbt19row__int__set__char(_M0L8row__strS788, _M0L6_2atmpS1849, _M0L2chS787);
      moonbit_decref_cycle_free(_M0L8row__strS788);
      #line 333 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS775, _M0L1rS786, _M0L8new__rowS789);
      _M0L3valS1848 = _M0L1cS782->$0;
      _M0L6_2atmpS1847 = _M0L3valS1848 + 1;
      _M0L1cS782->$0 = _M0L6_2atmpS1847;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1cS782);
      moonbit_decref_cycle_free(_M0L9row__initS778);
      moonbit_decref_cycle_free(_M0L4valsS767);
    }
    break;
  }
  #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_14.data);
  _M0L3valS1881 = _M0L2mxS766->$0;
  #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10max__labelS791
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L3valS1881);
  _M0L3valS1879 = _M0L2mxS766->$0;
  moonbit_decref_cycle_free(_M0L2mxS766);
  _M0L3valS1880 = _M0L2mnS765->$0;
  _M0L6_2atmpS1878 = _M0L3valS1879 + _M0L3valS1880;
  _M0L6_2atmpS1877 = _M0L6_2atmpS1878 / 0x1p+1f;
  #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10mid__labelS792
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L6_2atmpS1877);
  _M0L3valS1876 = _M0L2mnS765->$0;
  moonbit_decref_cycle_free(_M0L2mnS765);
  #line 340 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10min__labelS793
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L3valS1876);
  _M0L1aS795 = Moonbit_array_length(_M0L10max__labelS791);
  _M0L1bS796 = Moonbit_array_length(_M0L10mid__labelS792);
  _M0L1cS797 = Moonbit_array_length(_M0L10min__labelS793);
  if (_M0L1aS795 > _M0L1bS796) {
    _M0L1mS798 = _M0L1aS795;
  } else {
    _M0L1mS798 = _M0L1bS796;
  }
  if (_M0L1mS798 > _M0L1cS797) {
    _M0L12label__widthS794 = _M0L1mS798;
  } else {
    _M0L12label__widthS794 = _M0L1cS797;
  }
  _M0L7_2abindS799 = 0;
  _M0L1rS800 = _M0L7_2abindS799;
  while (1) {
    if (_M0L1rS800 < _M0L6heightS776) {
      moonbit_string_t _M0L5labelS801;
      int32_t _M0L6_2atmpS1863;
      int32_t _M0L10pad__countS802;
      moonbit_string_t _M0L6paddedS803;
      moonbit_string_t _M0L6_2atmpS1862;
      moonbit_string_t _M0L6_2atmpS1860;
      moonbit_string_t _M0L6_2atmpS1861;
      moonbit_string_t _M0L6_2atmpS1859;
      int32_t _M0L6_2atmpS1867;
      if (_M0L1rS800 == 0) {
        moonbit_incref_cycle_free(_M0L10max__labelS791);
        _M0L5labelS801 = _M0L10max__labelS791;
      } else {
        int32_t _M0L6_2atmpS1865 = _M0L6heightS776 - 1;
        if (_M0L1rS800 == _M0L6_2atmpS1865) {
          moonbit_incref_cycle_free(_M0L10min__labelS793);
          _M0L5labelS801 = _M0L10min__labelS793;
        } else {
          int32_t _M0L6_2atmpS1866 = _M0L6heightS776 / 2;
          if (_M0L1rS800 == _M0L6_2atmpS1866) {
            moonbit_incref_cycle_free(_M0L10mid__labelS792);
            _M0L5labelS801 = _M0L10mid__labelS792;
          } else {
            _M0L5labelS801 = (moonbit_string_t)moonbit_string_literal_0.data;
          }
        }
      }
      _M0L6_2atmpS1863 = Moonbit_array_length(_M0L5labelS801);
      if (_M0L12label__widthS794 > _M0L6_2atmpS1863) {
        int32_t _M0L6_2atmpS1864 = Moonbit_array_length(_M0L5labelS801);
        _M0L10pad__countS802 = _M0L12label__widthS794 - _M0L6_2atmpS1864;
      } else {
        _M0L10pad__countS802 = 0;
      }
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6paddedS803 = _M0MPC16string6String4make(_M0L10pad__countS802, 32);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1862 = moonbit_add_string(_M0L6paddedS803, _M0L5labelS801);
      moonbit_decref_cycle_free(_M0L5labelS801);
      moonbit_decref_cycle_free(_M0L6paddedS803);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1860
      = moonbit_add_string(_M0L6_2atmpS1862, (moonbit_string_t)moonbit_string_literal_11.data);
      moonbit_decref_cycle_free(_M0L6_2atmpS1862);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1861
      = _M0MPC15array5Array2atGsE(_M0L6canvasS775, _M0L1rS800);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1859
      = moonbit_add_string(_M0L6_2atmpS1860, _M0L6_2atmpS1861);
      moonbit_decref_cycle_free(_M0L6_2atmpS1861);
      moonbit_decref_cycle_free(_M0L6_2atmpS1860);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0FPB7printlnGsE(_M0L6_2atmpS1859);
      moonbit_decref_cycle_free(_M0L6_2atmpS1859);
      _M0L6_2atmpS1867 = _M0L1rS800 + 1;
      _M0L1rS800 = _M0L6_2atmpS1867;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L10min__labelS793);
      moonbit_decref_cycle_free(_M0L10mid__labelS792);
      moonbit_decref_cycle_free(_M0L10max__labelS791);
      moonbit_decref_cycle_free(_M0L6canvasS775);
    }
    break;
  }
  _M0L6_2atmpS1875 = _M0L12label__widthS794 + 3;
  #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L8pad__strS805 = _M0MPC16string6String4make(_M0L6_2atmpS1875, 32);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1873
  = moonbit_add_string(_M0L8pad__strS805, (moonbit_string_t)moonbit_string_literal_15.data);
  moonbit_decref_cycle_free(_M0L8pad__strS805);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1874 = _M0IPC15float5FloatPB4Show10to__string(_M0L6t__maxS770);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1872 = moonbit_add_string(_M0L6_2atmpS1873, _M0L6_2atmpS1874);
  moonbit_decref_cycle_free(_M0L6_2atmpS1874);
  moonbit_decref_cycle_free(_M0L6_2atmpS1873);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1870
  = moonbit_add_string(_M0L6_2atmpS1872, (moonbit_string_t)moonbit_string_literal_16.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS1872);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1871 = _M0IPC15float5FloatPB4Show10to__string(_M0L6t__maxS770);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1869 = moonbit_add_string(_M0L6_2atmpS1870, _M0L6_2atmpS1871);
  moonbit_decref_cycle_free(_M0L6_2atmpS1871);
  moonbit_decref_cycle_free(_M0L6_2atmpS1870);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1868
  = moonbit_add_string(_M0L6_2atmpS1869, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS1869);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1868);
  moonbit_decref_cycle_free(_M0L6_2atmpS1868);
  return 0;
}

moonbit_string_t _M0FP26RiantR8snn__mbt27format__axis__label__kernel(
  float _M0L1vS760
) {
  float _M0L6scaledS759;
  float _M0L6_2atmpS1820;
  int32_t _M0L7roundedS761;
  float _M0L6_2atmpS1819;
  float _M0L12scaled__backS762;
  #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6scaledS759 = _M0L1vS760 * 0x1.388p+13f;
  #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1820 = _M0MPC15float5Float5round(_M0L6scaledS759);
  #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7roundedS761 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1820);
  _M0L6_2atmpS1819 = (float)_M0L7roundedS761;
  _M0L12scaled__backS762 = _M0L6_2atmpS1819 / 0x1.388p+13f;
  #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  return _M0IPC15float5FloatPB4Show10to__string(_M0L12scaled__backS762);
}

float _M0FP26RiantR8snn__mbt16gerstner__kernel(
  float _M0L2dtS752,
  float _M0L8tau__preS754,
  float _M0L9tau__postS757,
  float _M0L6a__preS755,
  float _M0L7a__postS758
) {
  #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0L2dtS752 > 0x0p+0f) {
    float _M0L6_2atmpS1816 = -_M0L2dtS752;
    float _M0L3argS753 = _M0L6_2atmpS1816 / _M0L8tau__preS754;
    float _M0L6_2atmpS1815;
    #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS1815 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS753);
    return _M0L6a__preS755 * _M0L6_2atmpS1815;
  } else if (_M0L2dtS752 < 0x0p+0f) {
    float _M0L3argS756 = _M0L2dtS752 / _M0L9tau__postS757;
    float _M0L6_2atmpS1817 = -_M0L7a__postS758;
    float _M0L6_2atmpS1818;
    #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS1818 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS756);
    return _M0L6_2atmpS1817 * _M0L6_2atmpS1818;
  } else {
    return 0x0p+0f;
  }
}

moonbit_string_t _M0FP26RiantR8snn__mbt19row__int__set__char(
  moonbit_string_t _M0L1sS742,
  int32_t _M0L3posS745,
  int32_t _M0L2chS749
) {
  int32_t _M0L6_2atmpS1814;
  struct _M0TPB13StringBuilder* _M0L2sbS741;
  int32_t _M0L3lenS743;
  int32_t _M0L11prefix__endS744;
  int32_t _M0L6_2atmpS1806;
  moonbit_string_t _result_2063;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1814 = Moonbit_array_length(_M0L1sS742);
  #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L2sbS741
  = _M0MPB13StringBuilder21StringBuilder_2einner(_M0L6_2atmpS1814);
  _M0L3lenS743 = Moonbit_array_length(_M0L1sS742);
  if (_M0L3posS745 < _M0L3lenS743) {
    _M0L11prefix__endS744 = _M0L3posS745;
  } else {
    _M0L11prefix__endS744 = _M0L3lenS743;
  }
  if (_M0L11prefix__endS744 > 0) {
    moonbit_string_t _M0L6prefixS746;
    struct _M0TPB8MutLocalGiE* _M0L1kS747;
    #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6prefixS746 = _M0MPC16string6String4make(_M0L11prefix__endS744, 32);
    _M0L1kS747
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS747)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS747->$0 = 0;
    while (1) {
      int32_t _M0L3valS1800 = _M0L1kS747->$0;
      if (_M0L3valS1800 < _M0L11prefix__endS744) {
        int32_t _M0L3valS1803 = _M0L1kS747->$0;
        int32_t _M0L6_2atmpS1802;
        int32_t _M0L6_2atmpS1801;
        int32_t _M0L3valS1805;
        int32_t _M0L6_2atmpS1804;
        if (
          _M0L3valS1803 < 0
          || _M0L3valS1803 >= Moonbit_array_length(_M0L1sS742)
        ) {
          #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1802 = _M0L1sS742[_M0L3valS1803];
        #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0L6_2atmpS1801
        = _M0MPC16uint166UInt1616unsafe__to__char(_M0L6_2atmpS1802);
        #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS741, _M0L6_2atmpS1801);
        _M0L3valS1805 = _M0L1kS747->$0;
        _M0L6_2atmpS1804 = _M0L3valS1805 + 1;
        _M0L1kS747->$0 = _M0L6_2atmpS1804;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS747);
      }
      break;
    }
    moonbit_decref_cycle_free(_M0L6prefixS746);
  }
  #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS741, _M0L2chS749);
  _M0L6_2atmpS1806 = _M0L3posS745 + 1;
  if (_M0L6_2atmpS1806 < _M0L3lenS743) {
    int32_t _M0L6_2atmpS1813 = _M0L3posS745 + 1;
    struct _M0TPB8MutLocalGiE* _M0L1kS750 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS750)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS750->$0 = _M0L6_2atmpS1813;
    while (1) {
      int32_t _M0L3valS1807 = _M0L1kS750->$0;
      if (_M0L3valS1807 < _M0L3lenS743) {
        int32_t _M0L3valS1810 = _M0L1kS750->$0;
        int32_t _M0L6_2atmpS1809;
        int32_t _M0L6_2atmpS1808;
        int32_t _M0L3valS1812;
        int32_t _M0L6_2atmpS1811;
        if (
          _M0L3valS1810 < 0
          || _M0L3valS1810 >= Moonbit_array_length(_M0L1sS742)
        ) {
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1809 = _M0L1sS742[_M0L3valS1810];
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0L6_2atmpS1808
        = _M0MPC16uint166UInt1616unsafe__to__char(_M0L6_2atmpS1809);
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS741, _M0L6_2atmpS1808);
        _M0L3valS1812 = _M0L1kS750->$0;
        _M0L6_2atmpS1811 = _M0L3valS1812 + 1;
        _M0L1kS750->$0 = _M0L6_2atmpS1811;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS750);
      }
      break;
    }
  }
  #line 380 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _result_2063 = _M0MPB13StringBuilder10to__string(_M0L2sbS741);
  moonbit_decref_cycle_free(_M0L2sbS741);
  return _result_2063;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS740) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS740 != _M0L4selfS740;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS739) {
  double _M0L6_2atmpS1799;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1799 = (double)_M0L4selfS739;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1799);
}

float _M0MPC15float5Float5round(float _M0L4selfS738) {
  float _M0L6_2atmpS1798;
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L6_2atmpS1798 = _M0L4selfS738 + 0x1p-1f;
  #line 145 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  return _M0MPC15float5Float5floor(_M0L6_2atmpS1798);
}

float _M0MPC15float5Float5floor(float _M0L4selfS737) {
  float _M0L7truncedS736;
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L7truncedS736 = _M0MPC15float5Float5trunc(_M0L4selfS737);
  if (_M0L4selfS737 < _M0L7truncedS736) {
    return _M0L7truncedS736 - 0x1p+0f;
  } else {
    return _M0L7truncedS736;
  }
}

float _M0MPC15float5Float5trunc(float _M0L4selfS732) {
  uint32_t _M0L3u32S731;
  uint32_t _M0L6_2atmpS1797;
  uint32_t _M0L6_2atmpS1796;
  int32_t _M0L11biased__expS733;
  int32_t _M0L6_2atmpS1795;
  int32_t _M0L11mask__shiftS734;
  uint32_t _tmp_2064;
  int32_t _M0L6_2atmpS1794;
  int32_t _M0L6_2atmpS1793;
  uint32_t _M0L11trunc__maskS735;
  uint32_t _M0L6_2atmpS1792;
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L3u32S731 = *(int32_t*)&_M0L4selfS732;
  _M0L6_2atmpS1797 = _M0L3u32S731 >> 23;
  _M0L6_2atmpS1796 = _M0L6_2atmpS1797 & 255u;
  _M0L11biased__expS733 = *(int32_t*)&_M0L6_2atmpS1796;
  if (_M0L11biased__expS733 < 127) {
    uint32_t _M0L6_2atmpS1791 = _M0L3u32S731 & 2147483648u;
    return *(float*)&_M0L6_2atmpS1791;
  } else if (_M0L11biased__expS733 >= 150) {
    return _M0L4selfS732;
  }
  _M0L6_2atmpS1795 = _M0L11biased__expS733 - 127;
  _M0L11mask__shiftS734 = _M0L6_2atmpS1795 + 8;
  _tmp_2064 = 2147483648u;
  _M0L6_2atmpS1794 = *(int32_t*)&_tmp_2064;
  _M0L6_2atmpS1793 = _M0L6_2atmpS1794 >> (_M0L11mask__shiftS734 & 31);
  _M0L11trunc__maskS735 = *(uint32_t*)&_M0L6_2atmpS1793;
  _M0L6_2atmpS1792 = _M0L3u32S731 & _M0L11trunc__maskS735;
  return *(float*)&_M0L6_2atmpS1792;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS730) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS730 != _M0L4selfS730) {
    return 0;
  } else if (_M0L4selfS730 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS730 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS730;
  }
}

struct _M0TPB5ArrayGsE* _M0MPC15array5Array4makeGsE(
  int32_t _M0L3lenS726,
  moonbit_string_t _M0L4elemS728
) {
  struct _M0TPB5ArrayGsE* _M0L3arrS725;
  int32_t _M0L1iS727;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS725 = _M0MPC15array5Array20unsafe__make__uninitGsE(_M0L3lenS726);
  _M0L1iS727 = 0;
  while (1) {
    if (_M0L1iS727 < _M0L3lenS726) {
      moonbit_string_t* _M0L3bufS1789 = _M0L3arrS725->$0;
      moonbit_string_t _M0L6_2aoldS2004 =
        (moonbit_string_t)_M0L3bufS1789[_M0L1iS727];
      int32_t _M0L6_2atmpS1790;
      moonbit_incref_cycle_free(_M0L4elemS728);
      moonbit_decref_cycle_free(_M0L6_2aoldS2004);
      _M0L3bufS1789[_M0L1iS727] = _M0L4elemS728;
      _M0L6_2atmpS1790 = _M0L1iS727 + 1;
      _M0L1iS727 = _M0L6_2atmpS1790;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS728);
    }
    break;
  }
  return _M0L3arrS725;
}

int32_t _M0MPC15array5Array3setGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS722,
  int32_t _M0L5indexS723,
  moonbit_string_t _M0L5valueS724
) {
  int32_t _M0L3lenS721;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS721 = _M0L4selfS722->$1;
  if (_M0L5indexS723 >= 0 && _M0L5indexS723 < _M0L3lenS721) {
    moonbit_string_t* _M0L6_2atmpS1788;
    moonbit_string_t _M0L6_2aoldS2005;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1788 = _M0MPC15array5Array6bufferGsE(_M0L4selfS722);
    _M0L6_2aoldS2005 = (moonbit_string_t)_M0L6_2atmpS1788[_M0L5indexS723];
    moonbit_decref_cycle_free(_M0L6_2aoldS2005);
    _M0L6_2atmpS1788[_M0L5indexS723] = _M0L5valueS724;
    moonbit_decref_cycle_free(_M0L6_2atmpS1788);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS724);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS716,
  int32_t _M0L5indexS717
) {
  int32_t _M0L3lenS715;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS715 = _M0L4selfS716->$1;
  if (_M0L5indexS717 >= 0 && _M0L5indexS717 < _M0L3lenS715) {
    float* _M0L6_2atmpS1786;
    float _result_2066;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1786 = _M0MPC15array5Array6bufferGfE(_M0L4selfS716);
    _result_2066 = (float)_M0L6_2atmpS1786[_M0L5indexS717];
    moonbit_decref_cycle_free(_M0L6_2atmpS1786);
    return _result_2066;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS719,
  int32_t _M0L5indexS720
) {
  int32_t _M0L3lenS718;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS718 = _M0L4selfS719->$1;
  if (_M0L5indexS720 >= 0 && _M0L5indexS720 < _M0L3lenS718) {
    moonbit_string_t* _M0L6_2atmpS1787;
    moonbit_string_t _M0L6_2atmpS2006;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1787 = _M0MPC15array5Array6bufferGsE(_M0L4selfS719);
    _M0L6_2atmpS2006 = (moonbit_string_t)_M0L6_2atmpS1787[_M0L5indexS720];
    moonbit_incref_cycle_free(_M0L6_2atmpS2006);
    moonbit_decref_cycle_free(_M0L6_2atmpS1787);
    return _M0L6_2atmpS2006;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS714) {
  moonbit_string_t _M0L6_2atmpS1785;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1785 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS714);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1785);
  moonbit_decref_cycle_free(_M0L6_2atmpS1785);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS713) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS713);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS698) {
  uint64_t _M0L4bitsS701;
  uint64_t _M0L6_2atmpS1784;
  uint64_t _M0L6_2atmpS1783;
  int32_t _M0L8ieeeSignS702;
  uint64_t _M0L12ieeeMantissaS703;
  uint64_t _M0L6_2atmpS1782;
  uint64_t _M0L6_2atmpS1781;
  int32_t _M0L12ieeeExponentS704;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS705;
  struct _M0TPB17FloatingDecimal64* _M0L1vS706;
  moonbit_string_t _result_2068;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS698 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  if (_M0L3valS698 >= -0x1p+53 && _M0L3valS698 <= 0x1p+53) {
    if (_M0L3valS698 >= -0x1p+31 && _M0L3valS698 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS699;
      double _M0L6_2atmpS1770;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS699 = _M0MPC16double6Double7to__int(_M0L3valS698);
      _M0L6_2atmpS1770 = (double)_M0L1iS699;
      if (_M0L6_2atmpS1770 == _M0L3valS698) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS699, 10);
      }
    } else {
      int64_t _M0L1iS700;
      double _M0L6_2atmpS1771;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS700 = _M0MPC16double6Double9to__int64(_M0L3valS698);
      _M0L6_2atmpS1771 = (double)_M0L1iS700;
      if (_M0L6_2atmpS1771 == _M0L3valS698) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS700, 10);
      }
    }
  }
  _M0L4bitsS701 = *(int64_t*)&_M0L3valS698;
  _M0L6_2atmpS1784 = _M0L4bitsS701 >> 63;
  _M0L6_2atmpS1783 = _M0L6_2atmpS1784 & 1ull;
  _M0L8ieeeSignS702 = _M0L6_2atmpS1783 != 0ull;
  _M0L12ieeeMantissaS703 = _M0L4bitsS701 & 4503599627370495ull;
  _M0L6_2atmpS1782 = _M0L4bitsS701 >> 52;
  _M0L6_2atmpS1781 = _M0L6_2atmpS1782 & 2047ull;
  _M0L12ieeeExponentS704 = (int32_t)_M0L6_2atmpS1781;
  if (
    _M0L12ieeeExponentS704 == 2047
    || _M0L12ieeeExponentS704 == 0 && _M0L12ieeeMantissaS703 == 0ull
  ) {
    int32_t _M0L6_2atmpS1772 = _M0L12ieeeExponentS704 != 0;
    int32_t _M0L6_2atmpS1773 = _M0L12ieeeMantissaS703 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS702, _M0L6_2atmpS1772, _M0L6_2atmpS1773);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS705
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS703, _M0L12ieeeExponentS704);
  if (_M0L7_2abindS705 == 0) {
    uint32_t _M0L6_2atmpS1774;
    if (_M0L7_2abindS705) {
      moonbit_decref_cycle_free(_M0L7_2abindS705);
    }
    _M0L6_2atmpS1774 = *(uint32_t*)&_M0L12ieeeExponentS704;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS706 = _M0FPB3d2d(_M0L12ieeeMantissaS703, _M0L6_2atmpS1774);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS707 = _M0L7_2abindS705;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS708 = _M0L7_2aSomeS707;
    struct _M0TPB17FloatingDecimal64* _M0L1xS709 = _M0L4_2afS708;
    while (1) {
      uint64_t _M0L8mantissaS1780 = _M0L1xS709->$0;
      uint64_t _M0L1qS710 = _M0L8mantissaS1780 / 10ull;
      uint64_t _M0L8mantissaS1778 = _M0L1xS709->$0;
      uint64_t _M0L6_2atmpS1779 = 10ull * _M0L1qS710;
      uint64_t _M0L1rS711 = _M0L8mantissaS1778 - _M0L6_2atmpS1779;
      int32_t _M0L8exponentS1777;
      int32_t _M0L6_2atmpS1776;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1775;
      if (_M0L1rS711 != 0ull) {
        _M0L1vS706 = _M0L1xS709;
        break;
      }
      _M0L8exponentS1777 = _M0L1xS709->$1;
      moonbit_decref_cycle_free(_M0L1xS709);
      _M0L6_2atmpS1776 = _M0L8exponentS1777 + 1;
      _M0L6_2atmpS1775
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1775)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1775->$0 = _M0L1qS710;
      _M0L6_2atmpS1775->$1 = _M0L6_2atmpS1776;
      _M0L1xS709 = _M0L6_2atmpS1775;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2068 = _M0FPB9to__chars(_M0L1vS706, _M0L8ieeeSignS702);
  moonbit_decref_cycle_free(_M0L1vS706);
  return _result_2068;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS693,
  int32_t _M0L12ieeeExponentS695
) {
  uint64_t _M0L2m2S692;
  int32_t _M0L6_2atmpS1769;
  int32_t _M0L2e2S694;
  int32_t _M0L6_2atmpS1768;
  uint64_t _M0L6_2atmpS1767;
  uint64_t _M0L4maskS696;
  uint64_t _M0L8fractionS697;
  int32_t _M0L6_2atmpS1766;
  uint64_t _M0L6_2atmpS1765;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1764;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S692 = 4503599627370496ull | _M0L12ieeeMantissaS693;
  _M0L6_2atmpS1769 = _M0L12ieeeExponentS695 - 1023;
  _M0L2e2S694 = _M0L6_2atmpS1769 - 52;
  if (_M0L2e2S694 > 0) {
    return 0;
  }
  if (_M0L2e2S694 < -52) {
    return 0;
  }
  _M0L6_2atmpS1768 = -_M0L2e2S694;
  _M0L6_2atmpS1767 = 1ull << (_M0L6_2atmpS1768 & 63);
  _M0L4maskS696 = _M0L6_2atmpS1767 - 1ull;
  _M0L8fractionS697 = _M0L2m2S692 & _M0L4maskS696;
  if (_M0L8fractionS697 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1766 = -_M0L2e2S694;
  _M0L6_2atmpS1765 = _M0L2m2S692 >> (_M0L6_2atmpS1766 & 63);
  _M0L6_2atmpS1764
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1764)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1764->$0 = _M0L6_2atmpS1765;
  _M0L6_2atmpS1764->$1 = 0;
  return _M0L6_2atmpS1764;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS660,
  int32_t _M0L4signS658
) {
  moonbit_bytes_t _M0L6resultS656;
  int32_t _M0Lm5indexS657;
  uint64_t _M0L6outputS659;
  int32_t _M0L7olengthS661;
  int32_t _M0L8exponentS1763;
  int32_t _M0L6_2atmpS1762;
  int32_t _M0Lm3expS662;
  int32_t _M0L6_2atmpS1761;
  int32_t _M0L6_2atmpS1759;
  int32_t _M0L18scientificNotationS663;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS656 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS657 = 0;
  if (_M0L4signS658) {
    int32_t _M0L6_2atmpS1633 = _M0Lm5indexS657;
    int32_t _M0L6_2atmpS1634;
    if (
      _M0L6_2atmpS1633 < 0
      || _M0L6_2atmpS1633 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1633] = 45;
    _M0L6_2atmpS1634 = _M0Lm5indexS657;
    _M0Lm5indexS657 = _M0L6_2atmpS1634 + 1;
  }
  _M0L6outputS659 = _M0L1vS660->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS661 = _M0FPB17decimal__length17(_M0L6outputS659);
  _M0L8exponentS1763 = _M0L1vS660->$1;
  _M0L6_2atmpS1762 = _M0L8exponentS1763 + _M0L7olengthS661;
  _M0Lm3expS662 = _M0L6_2atmpS1762 - 1;
  _M0L6_2atmpS1761 = _M0Lm3expS662;
  if (_M0L6_2atmpS1761 >= -6) {
    int32_t _M0L6_2atmpS1760 = _M0Lm3expS662;
    _M0L6_2atmpS1759 = _M0L6_2atmpS1760 < 21;
  } else {
    _M0L6_2atmpS1759 = 0;
  }
  _M0L18scientificNotationS663 = !_M0L6_2atmpS1759;
  if (_M0L18scientificNotationS663) {
    int32_t _M0L7_2abindS664 = _M0L7olengthS661 - 1;
    uint64_t _M0L6outputS665;
    int32_t _M0L1iS666 = 0;
    uint64_t _M0L6outputS667 = _M0L6outputS659;
    int32_t _M0L6_2atmpS1635;
    int32_t _M0L6_2atmpS1639;
    int32_t _M0L6_2atmpS1638;
    int32_t _M0L6_2atmpS1637;
    int32_t _M0L6_2atmpS1636;
    int32_t _M0L6_2atmpS1643;
    int32_t _M0L6_2atmpS1644;
    int32_t _M0L6_2atmpS1645;
    int32_t _M0L6_2atmpS1646;
    int32_t _M0L6_2atmpS1647;
    int32_t _M0L6_2atmpS1653;
    int32_t _M0L6_2atmpS1686;
    moonbit_string_t _result_2070;
    while (1) {
      if (_M0L1iS666 < _M0L7_2abindS664) {
        uint64_t _M0L1cS668 = _M0L6outputS667 % 10ull;
        int32_t _M0L6_2atmpS1692 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1691 = _M0L6_2atmpS1692 + _M0L7olengthS661;
        int32_t _M0L6_2atmpS1687 = _M0L6_2atmpS1691 - _M0L1iS666;
        int32_t _M0L6_2atmpS1690 = (int32_t)_M0L1cS668;
        int32_t _M0L6_2atmpS1689 = 48 + _M0L6_2atmpS1690;
        int32_t _M0L6_2atmpS1688 = _M0L6_2atmpS1689 & 0xff;
        int32_t _M0L6_2atmpS1693;
        uint64_t _M0L6_2atmpS1694;
        if (
          _M0L6_2atmpS1687 < 0
          || _M0L6_2atmpS1687 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1687] = _M0L6_2atmpS1688;
        _M0L6_2atmpS1693 = _M0L1iS666 + 1;
        _M0L6_2atmpS1694 = _M0L6outputS667 / 10ull;
        _M0L1iS666 = _M0L6_2atmpS1693;
        _M0L6outputS667 = _M0L6_2atmpS1694;
        continue;
      } else {
        _M0L6outputS665 = _M0L6outputS667;
      }
      break;
    }
    _M0L6_2atmpS1635 = _M0Lm5indexS657;
    _M0L6_2atmpS1639 = (int32_t)_M0L6outputS665;
    _M0L6_2atmpS1638 = _M0L6_2atmpS1639 % 10;
    _M0L6_2atmpS1637 = 48 + _M0L6_2atmpS1638;
    _M0L6_2atmpS1636 = _M0L6_2atmpS1637 & 0xff;
    if (
      _M0L6_2atmpS1635 < 0
      || _M0L6_2atmpS1635 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1635] = _M0L6_2atmpS1636;
    if (_M0L7olengthS661 > 1) {
      int32_t _M0L6_2atmpS1641 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1640 = _M0L6_2atmpS1641 + 1;
      if (
        _M0L6_2atmpS1640 < 0
        || _M0L6_2atmpS1640 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1640] = 46;
    } else {
      int32_t _M0L6_2atmpS1642 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1642 - 1;
    }
    _M0L6_2atmpS1643 = _M0Lm5indexS657;
    _M0L6_2atmpS1644 = _M0L7olengthS661 + 1;
    _M0Lm5indexS657 = _M0L6_2atmpS1643 + _M0L6_2atmpS1644;
    _M0L6_2atmpS1645 = _M0Lm5indexS657;
    if (
      _M0L6_2atmpS1645 < 0
      || _M0L6_2atmpS1645 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1645] = 101;
    _M0L6_2atmpS1646 = _M0Lm5indexS657;
    _M0Lm5indexS657 = _M0L6_2atmpS1646 + 1;
    _M0L6_2atmpS1647 = _M0Lm3expS662;
    if (_M0L6_2atmpS1647 < 0) {
      int32_t _M0L6_2atmpS1648 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1649;
      int32_t _M0L6_2atmpS1650;
      if (
        _M0L6_2atmpS1648 < 0
        || _M0L6_2atmpS1648 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1648] = 45;
      _M0L6_2atmpS1649 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1649 + 1;
      _M0L6_2atmpS1650 = _M0Lm3expS662;
      _M0Lm3expS662 = -_M0L6_2atmpS1650;
    } else {
      int32_t _M0L6_2atmpS1651 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1652;
      if (
        _M0L6_2atmpS1651 < 0
        || _M0L6_2atmpS1651 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1651] = 43;
      _M0L6_2atmpS1652 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1652 + 1;
    }
    _M0L6_2atmpS1653 = _M0Lm3expS662;
    if (_M0L6_2atmpS1653 >= 100) {
      int32_t _M0L6_2atmpS1669 = _M0Lm3expS662;
      int32_t _M0L1aS670 = _M0L6_2atmpS1669 / 100;
      int32_t _M0L6_2atmpS1668 = _M0Lm3expS662;
      int32_t _M0L6_2atmpS1667 = _M0L6_2atmpS1668 / 10;
      int32_t _M0L1bS671 = _M0L6_2atmpS1667 % 10;
      int32_t _M0L6_2atmpS1666 = _M0Lm3expS662;
      int32_t _M0L1cS672 = _M0L6_2atmpS1666 % 10;
      int32_t _M0L6_2atmpS1654 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1656 = 48 + _M0L1aS670;
      int32_t _M0L6_2atmpS1655 = _M0L6_2atmpS1656 & 0xff;
      int32_t _M0L6_2atmpS1660;
      int32_t _M0L6_2atmpS1657;
      int32_t _M0L6_2atmpS1659;
      int32_t _M0L6_2atmpS1658;
      int32_t _M0L6_2atmpS1664;
      int32_t _M0L6_2atmpS1661;
      int32_t _M0L6_2atmpS1663;
      int32_t _M0L6_2atmpS1662;
      int32_t _M0L6_2atmpS1665;
      if (
        _M0L6_2atmpS1654 < 0
        || _M0L6_2atmpS1654 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1654] = _M0L6_2atmpS1655;
      _M0L6_2atmpS1660 = _M0Lm5indexS657;
      _M0L6_2atmpS1657 = _M0L6_2atmpS1660 + 1;
      _M0L6_2atmpS1659 = 48 + _M0L1bS671;
      _M0L6_2atmpS1658 = _M0L6_2atmpS1659 & 0xff;
      if (
        _M0L6_2atmpS1657 < 0
        || _M0L6_2atmpS1657 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1657] = _M0L6_2atmpS1658;
      _M0L6_2atmpS1664 = _M0Lm5indexS657;
      _M0L6_2atmpS1661 = _M0L6_2atmpS1664 + 2;
      _M0L6_2atmpS1663 = 48 + _M0L1cS672;
      _M0L6_2atmpS1662 = _M0L6_2atmpS1663 & 0xff;
      if (
        _M0L6_2atmpS1661 < 0
        || _M0L6_2atmpS1661 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1661] = _M0L6_2atmpS1662;
      _M0L6_2atmpS1665 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1665 + 3;
    } else {
      int32_t _M0L6_2atmpS1670 = _M0Lm3expS662;
      if (_M0L6_2atmpS1670 >= 10) {
        int32_t _M0L6_2atmpS1680 = _M0Lm3expS662;
        int32_t _M0L1aS673 = _M0L6_2atmpS1680 / 10;
        int32_t _M0L6_2atmpS1679 = _M0Lm3expS662;
        int32_t _M0L1bS674 = _M0L6_2atmpS1679 % 10;
        int32_t _M0L6_2atmpS1671 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1673 = 48 + _M0L1aS673;
        int32_t _M0L6_2atmpS1672 = _M0L6_2atmpS1673 & 0xff;
        int32_t _M0L6_2atmpS1677;
        int32_t _M0L6_2atmpS1674;
        int32_t _M0L6_2atmpS1676;
        int32_t _M0L6_2atmpS1675;
        int32_t _M0L6_2atmpS1678;
        if (
          _M0L6_2atmpS1671 < 0
          || _M0L6_2atmpS1671 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1671] = _M0L6_2atmpS1672;
        _M0L6_2atmpS1677 = _M0Lm5indexS657;
        _M0L6_2atmpS1674 = _M0L6_2atmpS1677 + 1;
        _M0L6_2atmpS1676 = 48 + _M0L1bS674;
        _M0L6_2atmpS1675 = _M0L6_2atmpS1676 & 0xff;
        if (
          _M0L6_2atmpS1674 < 0
          || _M0L6_2atmpS1674 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1674] = _M0L6_2atmpS1675;
        _M0L6_2atmpS1678 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1678 + 2;
      } else {
        int32_t _M0L6_2atmpS1681 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1684 = _M0Lm3expS662;
        int32_t _M0L6_2atmpS1683 = 48 + _M0L6_2atmpS1684;
        int32_t _M0L6_2atmpS1682 = _M0L6_2atmpS1683 & 0xff;
        int32_t _M0L6_2atmpS1685;
        if (
          _M0L6_2atmpS1681 < 0
          || _M0L6_2atmpS1681 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1681] = _M0L6_2atmpS1682;
        _M0L6_2atmpS1685 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1685 + 1;
      }
    }
    _M0L6_2atmpS1686 = _M0Lm5indexS657;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2070
    = _M0FPB19string__from__bytes(_M0L6resultS656, 0, _M0L6_2atmpS1686);
    moonbit_decref_cycle_free(_M0L6resultS656);
    return _result_2070;
  } else {
    int32_t _M0L6_2atmpS1695 = _M0Lm3expS662;
    int32_t _M0L6_2atmpS1758;
    moonbit_string_t _result_2076;
    if (_M0L6_2atmpS1695 < 0) {
      int32_t _M0L6_2atmpS1696 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1698;
      int32_t _M0L6_2atmpS1697;
      int32_t _M0L6_2atmpS1699;
      int32_t _M0L1iS675;
      int32_t _M0L6_2atmpS1714;
      int32_t _M0L6_2atmpS1716;
      int32_t _M0L6_2atmpS1715;
      int32_t _M0L7currentS677;
      int32_t _M0L1iS678;
      uint64_t _M0L6outputS679;
      if (
        _M0L6_2atmpS1696 < 0
        || _M0L6_2atmpS1696 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1696] = 48;
      _M0L6_2atmpS1698 = _M0Lm5indexS657;
      _M0L6_2atmpS1697 = _M0L6_2atmpS1698 + 1;
      if (
        _M0L6_2atmpS1697 < 0
        || _M0L6_2atmpS1697 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1697] = 46;
      _M0L6_2atmpS1699 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1699 + 2;
      _M0L1iS675 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1700 = _M0Lm3expS662;
        if (_M0L1iS675 > _M0L6_2atmpS1700) {
          int32_t _M0L6_2atmpS1703 = _M0Lm5indexS657;
          int32_t _M0L6_2atmpS1702 = _M0L6_2atmpS1703 - _M0L1iS675;
          int32_t _M0L6_2atmpS1701 = _M0L6_2atmpS1702 - 1;
          int32_t _M0L6_2atmpS1704;
          if (
            _M0L6_2atmpS1701 < 0
            || _M0L6_2atmpS1701 >= Moonbit_array_length(_M0L6resultS656)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS656[_M0L6_2atmpS1701] = 48;
          _M0L6_2atmpS1704 = _M0L1iS675 - 1;
          _M0L1iS675 = _M0L6_2atmpS1704;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1714 = _M0Lm5indexS657;
      _M0L6_2atmpS1716 = _M0Lm3expS662;
      _M0L6_2atmpS1715 = -1 - _M0L6_2atmpS1716;
      _M0L7currentS677 = _M0L6_2atmpS1714 + _M0L6_2atmpS1715;
      _M0L1iS678 = 0;
      _M0L6outputS679 = _M0L6outputS659;
      while (1) {
        if (_M0L1iS678 < _M0L7olengthS661) {
          int32_t _M0L6_2atmpS1711 = _M0L7currentS677 + _M0L7olengthS661;
          int32_t _M0L6_2atmpS1710 = _M0L6_2atmpS1711 - _M0L1iS678;
          int32_t _M0L6_2atmpS1705 = _M0L6_2atmpS1710 - 1;
          uint64_t _M0L6_2atmpS1709 = _M0L6outputS679 % 10ull;
          int32_t _M0L6_2atmpS1708 = (int32_t)_M0L6_2atmpS1709;
          int32_t _M0L6_2atmpS1707 = 48 + _M0L6_2atmpS1708;
          int32_t _M0L6_2atmpS1706 = _M0L6_2atmpS1707 & 0xff;
          int32_t _M0L6_2atmpS1712;
          uint64_t _M0L6_2atmpS1713;
          if (
            _M0L6_2atmpS1705 < 0
            || _M0L6_2atmpS1705 >= Moonbit_array_length(_M0L6resultS656)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS656[_M0L6_2atmpS1705] = _M0L6_2atmpS1706;
          _M0L6_2atmpS1712 = _M0L1iS678 + 1;
          _M0L6_2atmpS1713 = _M0L6outputS679 / 10ull;
          _M0L1iS678 = _M0L6_2atmpS1712;
          _M0L6outputS679 = _M0L6_2atmpS1713;
          continue;
        }
        break;
      }
      _M0Lm5indexS657 = _M0L7currentS677 + _M0L7olengthS661;
    } else {
      int32_t _M0L6_2atmpS1718 = _M0Lm3expS662;
      int32_t _M0L6_2atmpS1717 = _M0L6_2atmpS1718 + 1;
      if (_M0L6_2atmpS1717 >= _M0L7olengthS661) {
        int32_t _M0L1iS681 = 0;
        uint64_t _M0L6outputS682 = _M0L6outputS659;
        int32_t _M0L6_2atmpS1729;
        int32_t _M0L6_2atmpS1734;
        int32_t _M0L7_2abindS684;
        int32_t _M0L1iS685;
        int32_t _M0L6_2atmpS1735;
        int32_t _M0L6_2atmpS1738;
        int32_t _M0L6_2atmpS1737;
        int32_t _M0L6_2atmpS1736;
        while (1) {
          if (_M0L1iS681 < _M0L7olengthS661) {
            int32_t _M0L6_2atmpS1726 = _M0Lm5indexS657;
            int32_t _M0L6_2atmpS1725 = _M0L6_2atmpS1726 + _M0L7olengthS661;
            int32_t _M0L6_2atmpS1724 = _M0L6_2atmpS1725 - _M0L1iS681;
            int32_t _M0L6_2atmpS1719 = _M0L6_2atmpS1724 - 1;
            uint64_t _M0L6_2atmpS1723 = _M0L6outputS682 % 10ull;
            int32_t _M0L6_2atmpS1722 = (int32_t)_M0L6_2atmpS1723;
            int32_t _M0L6_2atmpS1721 = 48 + _M0L6_2atmpS1722;
            int32_t _M0L6_2atmpS1720 = _M0L6_2atmpS1721 & 0xff;
            int32_t _M0L6_2atmpS1727;
            uint64_t _M0L6_2atmpS1728;
            if (
              _M0L6_2atmpS1719 < 0
              || _M0L6_2atmpS1719 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1719] = _M0L6_2atmpS1720;
            _M0L6_2atmpS1727 = _M0L1iS681 + 1;
            _M0L6_2atmpS1728 = _M0L6outputS682 / 10ull;
            _M0L1iS681 = _M0L6_2atmpS1727;
            _M0L6outputS682 = _M0L6_2atmpS1728;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1729 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1729 + _M0L7olengthS661;
        _M0L6_2atmpS1734 = _M0Lm3expS662;
        _M0L7_2abindS684 = _M0L6_2atmpS1734 + 1;
        _M0L1iS685 = _M0L7olengthS661;
        while (1) {
          if (_M0L1iS685 < _M0L7_2abindS684) {
            int32_t _M0L6_2atmpS1732 = _M0Lm5indexS657;
            int32_t _M0L6_2atmpS1731 = _M0L6_2atmpS1732 + _M0L1iS685;
            int32_t _M0L6_2atmpS1730 = _M0L6_2atmpS1731 - _M0L7olengthS661;
            int32_t _M0L6_2atmpS1733;
            if (
              _M0L6_2atmpS1730 < 0
              || _M0L6_2atmpS1730 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1730] = 48;
            _M0L6_2atmpS1733 = _M0L1iS685 + 1;
            _M0L1iS685 = _M0L6_2atmpS1733;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1735 = _M0Lm5indexS657;
        _M0L6_2atmpS1738 = _M0Lm3expS662;
        _M0L6_2atmpS1737 = _M0L6_2atmpS1738 + 1;
        _M0L6_2atmpS1736 = _M0L6_2atmpS1737 - _M0L7olengthS661;
        _M0Lm5indexS657 = _M0L6_2atmpS1735 + _M0L6_2atmpS1736;
      } else {
        int32_t _M0L6_2atmpS1755 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1754 = _M0L6_2atmpS1755 + 1;
        int32_t _M0L1iS687 = 0;
        int32_t _M0L7currentS688 = _M0L6_2atmpS1754;
        uint64_t _M0L6outputS689 = _M0L6outputS659;
        int32_t _M0L6_2atmpS1756;
        int32_t _M0L6_2atmpS1757;
        while (1) {
          if (_M0L1iS687 < _M0L7olengthS661) {
            int32_t _M0L6_2atmpS1750 = _M0L7olengthS661 - _M0L1iS687;
            int32_t _M0L6_2atmpS1748 = _M0L6_2atmpS1750 - 1;
            int32_t _M0L6_2atmpS1749 = _M0Lm3expS662;
            int32_t _M0L7currentS690;
            int32_t _M0L6_2atmpS1745;
            int32_t _M0L6_2atmpS1744;
            int32_t _M0L6_2atmpS1739;
            uint64_t _M0L6_2atmpS1743;
            int32_t _M0L6_2atmpS1742;
            int32_t _M0L6_2atmpS1741;
            int32_t _M0L6_2atmpS1740;
            int32_t _M0L6_2atmpS1746;
            uint64_t _M0L6_2atmpS1747;
            if (_M0L6_2atmpS1748 == _M0L6_2atmpS1749) {
              int32_t _M0L6_2atmpS1753 = _M0L7currentS688 + _M0L7olengthS661;
              int32_t _M0L6_2atmpS1752 = _M0L6_2atmpS1753 - _M0L1iS687;
              int32_t _M0L6_2atmpS1751 = _M0L6_2atmpS1752 - 1;
              if (
                _M0L6_2atmpS1751 < 0
                || _M0L6_2atmpS1751 >= Moonbit_array_length(_M0L6resultS656)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS656[_M0L6_2atmpS1751] = 46;
              _M0L7currentS690 = _M0L7currentS688 - 1;
            } else {
              _M0L7currentS690 = _M0L7currentS688;
            }
            _M0L6_2atmpS1745 = _M0L7currentS690 + _M0L7olengthS661;
            _M0L6_2atmpS1744 = _M0L6_2atmpS1745 - _M0L1iS687;
            _M0L6_2atmpS1739 = _M0L6_2atmpS1744 - 1;
            _M0L6_2atmpS1743 = _M0L6outputS689 % 10ull;
            _M0L6_2atmpS1742 = (int32_t)_M0L6_2atmpS1743;
            _M0L6_2atmpS1741 = 48 + _M0L6_2atmpS1742;
            _M0L6_2atmpS1740 = _M0L6_2atmpS1741 & 0xff;
            if (
              _M0L6_2atmpS1739 < 0
              || _M0L6_2atmpS1739 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1739] = _M0L6_2atmpS1740;
            _M0L6_2atmpS1746 = _M0L1iS687 + 1;
            _M0L6_2atmpS1747 = _M0L6outputS689 / 10ull;
            _M0L1iS687 = _M0L6_2atmpS1746;
            _M0L7currentS688 = _M0L7currentS690;
            _M0L6outputS689 = _M0L6_2atmpS1747;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1756 = _M0Lm5indexS657;
        _M0L6_2atmpS1757 = _M0L7olengthS661 + 1;
        _M0Lm5indexS657 = _M0L6_2atmpS1756 + _M0L6_2atmpS1757;
      }
    }
    _M0L6_2atmpS1758 = _M0Lm5indexS657;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2076
    = _M0FPB19string__from__bytes(_M0L6resultS656, 0, _M0L6_2atmpS1758);
    moonbit_decref_cycle_free(_M0L6resultS656);
    return _result_2076;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS602,
  uint32_t _M0L12ieeeExponentS601
) {
  int32_t _M0Lm2e2S599;
  uint64_t _M0Lm2m2S600;
  uint64_t _M0L6_2atmpS1632;
  uint64_t _M0L6_2atmpS1631;
  int32_t _M0L4evenS603;
  uint64_t _M0L6_2atmpS1630;
  uint64_t _M0L2mvS604;
  int32_t _M0L7mmShiftS605;
  uint64_t _M0Lm2vrS606;
  uint64_t _M0Lm2vpS607;
  uint64_t _M0Lm2vmS608;
  int32_t _M0Lm3e10S609;
  int32_t _M0Lm17vmIsTrailingZerosS610;
  int32_t _M0Lm17vrIsTrailingZerosS611;
  int32_t _M0L6_2atmpS1532;
  int32_t _M0Lm7removedS630;
  int32_t _M0Lm16lastRemovedDigitS631;
  uint64_t _M0Lm6outputS632;
  int32_t _M0L6_2atmpS1628;
  int32_t _M0L6_2atmpS1629;
  int32_t _M0L3expS655;
  uint64_t _M0L6_2atmpS1627;
  struct _M0TPB17FloatingDecimal64* _block_2082;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S599 = 0;
  _M0Lm2m2S600 = 0ull;
  if (_M0L12ieeeExponentS601 == 0u) {
    _M0Lm2e2S599 = -1076;
    _M0Lm2m2S600 = _M0L12ieeeMantissaS602;
  } else {
    int32_t _M0L6_2atmpS1531 = *(int32_t*)&_M0L12ieeeExponentS601;
    int32_t _M0L6_2atmpS1530 = _M0L6_2atmpS1531 - 1023;
    int32_t _M0L6_2atmpS1529 = _M0L6_2atmpS1530 - 52;
    _M0Lm2e2S599 = _M0L6_2atmpS1529 - 2;
    _M0Lm2m2S600 = 4503599627370496ull | _M0L12ieeeMantissaS602;
  }
  _M0L6_2atmpS1632 = _M0Lm2m2S600;
  _M0L6_2atmpS1631 = _M0L6_2atmpS1632 & 1ull;
  _M0L4evenS603 = _M0L6_2atmpS1631 == 0ull;
  _M0L6_2atmpS1630 = _M0Lm2m2S600;
  _M0L2mvS604 = 4ull * _M0L6_2atmpS1630;
  _M0L7mmShiftS605
  = _M0L12ieeeMantissaS602 != 0ull || _M0L12ieeeExponentS601 <= 1u;
  _M0Lm2vrS606 = 0ull;
  _M0Lm2vpS607 = 0ull;
  _M0Lm2vmS608 = 0ull;
  _M0Lm3e10S609 = 0;
  _M0Lm17vmIsTrailingZerosS610 = 0;
  _M0Lm17vrIsTrailingZerosS611 = 0;
  _M0L6_2atmpS1532 = _M0Lm2e2S599;
  if (_M0L6_2atmpS1532 >= 0) {
    int32_t _M0L6_2atmpS1554 = _M0Lm2e2S599;
    int32_t _M0L6_2atmpS1550;
    int32_t _M0L6_2atmpS1553;
    int32_t _M0L6_2atmpS1552;
    int32_t _M0L6_2atmpS1551;
    int32_t _M0L1qS612;
    int32_t _M0L6_2atmpS1549;
    int32_t _M0L6_2atmpS1548;
    int32_t _M0L1kS613;
    int32_t _M0L6_2atmpS1547;
    int32_t _M0L6_2atmpS1546;
    int32_t _M0L6_2atmpS1545;
    int32_t _M0L1iS614;
    struct _M0TPB8Pow5Pair _M0L4pow5S615;
    uint64_t _M0L6_2atmpS1544;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS616;
    uint64_t _M0L8_2avrOutS617;
    uint64_t _M0L8_2avpOutS618;
    uint64_t _M0L8_2avmOutS619;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1550 = _M0FPB9log10Pow2(_M0L6_2atmpS1554);
    _M0L6_2atmpS1553 = _M0Lm2e2S599;
    _M0L6_2atmpS1552 = _M0L6_2atmpS1553 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1551 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1552);
    _M0L1qS612 = _M0L6_2atmpS1550 - _M0L6_2atmpS1551;
    _M0Lm3e10S609 = _M0L1qS612;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1549 = _M0FPB8pow5bits(_M0L1qS612);
    _M0L6_2atmpS1548 = 125 + _M0L6_2atmpS1549;
    _M0L1kS613 = _M0L6_2atmpS1548 - 1;
    _M0L6_2atmpS1547 = _M0Lm2e2S599;
    _M0L6_2atmpS1546 = -_M0L6_2atmpS1547;
    _M0L6_2atmpS1545 = _M0L6_2atmpS1546 + _M0L1qS612;
    _M0L1iS614 = _M0L6_2atmpS1545 + _M0L1kS613;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S615 = _M0FPB22double__computeInvPow5(_M0L1qS612);
    _M0L6_2atmpS1544 = _M0Lm2m2S600;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS616
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1544, _M0L4pow5S615, _M0L1iS614, _M0L7mmShiftS605);
    _M0L8_2avrOutS617 = _M0L7_2abindS616.$0;
    _M0L8_2avpOutS618 = _M0L7_2abindS616.$1;
    _M0L8_2avmOutS619 = _M0L7_2abindS616.$2;
    _M0Lm2vrS606 = _M0L8_2avrOutS617;
    _M0Lm2vpS607 = _M0L8_2avpOutS618;
    _M0Lm2vmS608 = _M0L8_2avmOutS619;
    if (_M0L1qS612 <= 21) {
      int32_t _M0L6_2atmpS1540 = (int32_t)_M0L2mvS604;
      uint64_t _M0L6_2atmpS1543 = _M0L2mvS604 / 5ull;
      int32_t _M0L6_2atmpS1542 = (int32_t)_M0L6_2atmpS1543;
      int32_t _M0L6_2atmpS1541 = 5 * _M0L6_2atmpS1542;
      int32_t _M0L6mvMod5S620 = _M0L6_2atmpS1540 - _M0L6_2atmpS1541;
      if (_M0L6mvMod5S620 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS611
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS604, _M0L1qS612);
      } else if (_M0L4evenS603) {
        uint64_t _M0L6_2atmpS1534 = _M0L2mvS604 - 1ull;
        uint64_t _M0L6_2atmpS1535;
        uint64_t _M0L6_2atmpS1533;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1535 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS605);
        _M0L6_2atmpS1533 = _M0L6_2atmpS1534 - _M0L6_2atmpS1535;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS610
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1533, _M0L1qS612);
      } else {
        uint64_t _M0L6_2atmpS1536 = _M0Lm2vpS607;
        uint64_t _M0L6_2atmpS1539 = _M0L2mvS604 + 2ull;
        int32_t _M0L6_2atmpS1538;
        uint64_t _M0L6_2atmpS1537;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1538
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1539, _M0L1qS612);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1537 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1538);
        _M0Lm2vpS607 = _M0L6_2atmpS1536 - _M0L6_2atmpS1537;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1568 = _M0Lm2e2S599;
    int32_t _M0L6_2atmpS1567 = -_M0L6_2atmpS1568;
    int32_t _M0L6_2atmpS1562;
    int32_t _M0L6_2atmpS1566;
    int32_t _M0L6_2atmpS1565;
    int32_t _M0L6_2atmpS1564;
    int32_t _M0L6_2atmpS1563;
    int32_t _M0L1qS621;
    int32_t _M0L6_2atmpS1555;
    int32_t _M0L6_2atmpS1561;
    int32_t _M0L6_2atmpS1560;
    int32_t _M0L1iS622;
    int32_t _M0L6_2atmpS1559;
    int32_t _M0L1kS623;
    int32_t _M0L1jS624;
    struct _M0TPB8Pow5Pair _M0L4pow5S625;
    uint64_t _M0L6_2atmpS1558;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS626;
    uint64_t _M0L8_2avrOutS627;
    uint64_t _M0L8_2avpOutS628;
    uint64_t _M0L8_2avmOutS629;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1562 = _M0FPB9log10Pow5(_M0L6_2atmpS1567);
    _M0L6_2atmpS1566 = _M0Lm2e2S599;
    _M0L6_2atmpS1565 = -_M0L6_2atmpS1566;
    _M0L6_2atmpS1564 = _M0L6_2atmpS1565 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1563 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1564);
    _M0L1qS621 = _M0L6_2atmpS1562 - _M0L6_2atmpS1563;
    _M0L6_2atmpS1555 = _M0Lm2e2S599;
    _M0Lm3e10S609 = _M0L1qS621 + _M0L6_2atmpS1555;
    _M0L6_2atmpS1561 = _M0Lm2e2S599;
    _M0L6_2atmpS1560 = -_M0L6_2atmpS1561;
    _M0L1iS622 = _M0L6_2atmpS1560 - _M0L1qS621;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1559 = _M0FPB8pow5bits(_M0L1iS622);
    _M0L1kS623 = _M0L6_2atmpS1559 - 125;
    _M0L1jS624 = _M0L1qS621 - _M0L1kS623;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S625 = _M0FPB19double__computePow5(_M0L1iS622);
    _M0L6_2atmpS1558 = _M0Lm2m2S600;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS626
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1558, _M0L4pow5S625, _M0L1jS624, _M0L7mmShiftS605);
    _M0L8_2avrOutS627 = _M0L7_2abindS626.$0;
    _M0L8_2avpOutS628 = _M0L7_2abindS626.$1;
    _M0L8_2avmOutS629 = _M0L7_2abindS626.$2;
    _M0Lm2vrS606 = _M0L8_2avrOutS627;
    _M0Lm2vpS607 = _M0L8_2avpOutS628;
    _M0Lm2vmS608 = _M0L8_2avmOutS629;
    if (_M0L1qS621 <= 1) {
      _M0Lm17vrIsTrailingZerosS611 = 1;
      if (_M0L4evenS603) {
        int32_t _M0L6_2atmpS1556;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1556 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS605);
        _M0Lm17vmIsTrailingZerosS610 = _M0L6_2atmpS1556 == 1;
      } else {
        uint64_t _M0L6_2atmpS1557 = _M0Lm2vpS607;
        _M0Lm2vpS607 = _M0L6_2atmpS1557 - 1ull;
      }
    } else if (_M0L1qS621 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS611
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS604, _M0L1qS621);
    }
  }
  _M0Lm7removedS630 = 0;
  _M0Lm16lastRemovedDigitS631 = 0;
  _M0Lm6outputS632 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS610 || _M0Lm17vrIsTrailingZerosS611) {
    int32_t _if__result_2079;
    uint64_t _M0L6_2atmpS1598;
    uint64_t _M0L6_2atmpS1604;
    uint64_t _M0L6_2atmpS1605;
    int32_t _if__result_2080;
    int32_t _M0L6_2atmpS1601;
    int64_t _M0L6_2atmpS1600;
    uint64_t _M0L6_2atmpS1599;
    while (1) {
      uint64_t _M0L6_2atmpS1581 = _M0Lm2vpS607;
      uint64_t _M0L7vpDiv10S633 = _M0L6_2atmpS1581 / 10ull;
      uint64_t _M0L6_2atmpS1580 = _M0Lm2vmS608;
      uint64_t _M0L7vmDiv10S634 = _M0L6_2atmpS1580 / 10ull;
      uint64_t _M0L6_2atmpS1579;
      int32_t _M0L6_2atmpS1576;
      int32_t _M0L6_2atmpS1578;
      int32_t _M0L6_2atmpS1577;
      int32_t _M0L7vmMod10S636;
      uint64_t _M0L6_2atmpS1575;
      uint64_t _M0L7vrDiv10S637;
      uint64_t _M0L6_2atmpS1574;
      int32_t _M0L6_2atmpS1571;
      int32_t _M0L6_2atmpS1573;
      int32_t _M0L6_2atmpS1572;
      int32_t _M0L7vrMod10S638;
      int32_t _M0L6_2atmpS1570;
      if (_M0L7vpDiv10S633 <= _M0L7vmDiv10S634) {
        break;
      }
      _M0L6_2atmpS1579 = _M0Lm2vmS608;
      _M0L6_2atmpS1576 = (int32_t)_M0L6_2atmpS1579;
      _M0L6_2atmpS1578 = (int32_t)_M0L7vmDiv10S634;
      _M0L6_2atmpS1577 = 10 * _M0L6_2atmpS1578;
      _M0L7vmMod10S636 = _M0L6_2atmpS1576 - _M0L6_2atmpS1577;
      _M0L6_2atmpS1575 = _M0Lm2vrS606;
      _M0L7vrDiv10S637 = _M0L6_2atmpS1575 / 10ull;
      _M0L6_2atmpS1574 = _M0Lm2vrS606;
      _M0L6_2atmpS1571 = (int32_t)_M0L6_2atmpS1574;
      _M0L6_2atmpS1573 = (int32_t)_M0L7vrDiv10S637;
      _M0L6_2atmpS1572 = 10 * _M0L6_2atmpS1573;
      _M0L7vrMod10S638 = _M0L6_2atmpS1571 - _M0L6_2atmpS1572;
      _M0Lm17vmIsTrailingZerosS610
      = _M0Lm17vmIsTrailingZerosS610 && _M0L7vmMod10S636 == 0;
      if (_M0Lm17vrIsTrailingZerosS611) {
        int32_t _M0L6_2atmpS1569 = _M0Lm16lastRemovedDigitS631;
        _M0Lm17vrIsTrailingZerosS611 = _M0L6_2atmpS1569 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS611 = 0;
      }
      _M0Lm16lastRemovedDigitS631 = _M0L7vrMod10S638;
      _M0Lm2vrS606 = _M0L7vrDiv10S637;
      _M0Lm2vpS607 = _M0L7vpDiv10S633;
      _M0Lm2vmS608 = _M0L7vmDiv10S634;
      _M0L6_2atmpS1570 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1570 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS610) {
      while (1) {
        uint64_t _M0L6_2atmpS1594 = _M0Lm2vmS608;
        uint64_t _M0L7vmDiv10S639 = _M0L6_2atmpS1594 / 10ull;
        uint64_t _M0L6_2atmpS1593 = _M0Lm2vmS608;
        int32_t _M0L6_2atmpS1590 = (int32_t)_M0L6_2atmpS1593;
        int32_t _M0L6_2atmpS1592 = (int32_t)_M0L7vmDiv10S639;
        int32_t _M0L6_2atmpS1591 = 10 * _M0L6_2atmpS1592;
        int32_t _M0L7vmMod10S640 = _M0L6_2atmpS1590 - _M0L6_2atmpS1591;
        uint64_t _M0L6_2atmpS1589;
        uint64_t _M0L7vpDiv10S642;
        uint64_t _M0L6_2atmpS1588;
        uint64_t _M0L7vrDiv10S643;
        uint64_t _M0L6_2atmpS1587;
        int32_t _M0L6_2atmpS1584;
        int32_t _M0L6_2atmpS1586;
        int32_t _M0L6_2atmpS1585;
        int32_t _M0L7vrMod10S644;
        int32_t _M0L6_2atmpS1583;
        if (_M0L7vmMod10S640 != 0) {
          break;
        }
        _M0L6_2atmpS1589 = _M0Lm2vpS607;
        _M0L7vpDiv10S642 = _M0L6_2atmpS1589 / 10ull;
        _M0L6_2atmpS1588 = _M0Lm2vrS606;
        _M0L7vrDiv10S643 = _M0L6_2atmpS1588 / 10ull;
        _M0L6_2atmpS1587 = _M0Lm2vrS606;
        _M0L6_2atmpS1584 = (int32_t)_M0L6_2atmpS1587;
        _M0L6_2atmpS1586 = (int32_t)_M0L7vrDiv10S643;
        _M0L6_2atmpS1585 = 10 * _M0L6_2atmpS1586;
        _M0L7vrMod10S644 = _M0L6_2atmpS1584 - _M0L6_2atmpS1585;
        if (_M0Lm17vrIsTrailingZerosS611) {
          int32_t _M0L6_2atmpS1582 = _M0Lm16lastRemovedDigitS631;
          _M0Lm17vrIsTrailingZerosS611 = _M0L6_2atmpS1582 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS611 = 0;
        }
        _M0Lm16lastRemovedDigitS631 = _M0L7vrMod10S644;
        _M0Lm2vrS606 = _M0L7vrDiv10S643;
        _M0Lm2vpS607 = _M0L7vpDiv10S642;
        _M0Lm2vmS608 = _M0L7vmDiv10S639;
        _M0L6_2atmpS1583 = _M0Lm7removedS630;
        _M0Lm7removedS630 = _M0L6_2atmpS1583 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS611) {
      int32_t _M0L6_2atmpS1597 = _M0Lm16lastRemovedDigitS631;
      if (_M0L6_2atmpS1597 == 5) {
        uint64_t _M0L6_2atmpS1596 = _M0Lm2vrS606;
        uint64_t _M0L6_2atmpS1595 = _M0L6_2atmpS1596 % 2ull;
        _if__result_2079 = _M0L6_2atmpS1595 == 0ull;
      } else {
        _if__result_2079 = 0;
      }
    } else {
      _if__result_2079 = 0;
    }
    if (_if__result_2079) {
      _M0Lm16lastRemovedDigitS631 = 4;
    }
    _M0L6_2atmpS1598 = _M0Lm2vrS606;
    _M0L6_2atmpS1604 = _M0Lm2vrS606;
    _M0L6_2atmpS1605 = _M0Lm2vmS608;
    if (_M0L6_2atmpS1604 == _M0L6_2atmpS1605) {
      if (!_M0L4evenS603) {
        _if__result_2080 = 1;
      } else {
        int32_t _M0L6_2atmpS1603 = _M0Lm17vmIsTrailingZerosS610;
        _if__result_2080 = !_M0L6_2atmpS1603;
      }
    } else {
      _if__result_2080 = 0;
    }
    if (_if__result_2080) {
      _M0L6_2atmpS1601 = 1;
    } else {
      int32_t _M0L6_2atmpS1602 = _M0Lm16lastRemovedDigitS631;
      _M0L6_2atmpS1601 = _M0L6_2atmpS1602 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1600 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1601);
    _M0L6_2atmpS1599 = *(uint64_t*)&_M0L6_2atmpS1600;
    _M0Lm6outputS632 = _M0L6_2atmpS1598 + _M0L6_2atmpS1599;
  } else {
    int32_t _M0Lm7roundUpS645 = 0;
    uint64_t _M0L6_2atmpS1626 = _M0Lm2vpS607;
    uint64_t _M0L8vpDiv100S646 = _M0L6_2atmpS1626 / 100ull;
    uint64_t _M0L6_2atmpS1625 = _M0Lm2vmS608;
    uint64_t _M0L8vmDiv100S647 = _M0L6_2atmpS1625 / 100ull;
    uint64_t _M0L6_2atmpS1620;
    uint64_t _M0L6_2atmpS1623;
    uint64_t _M0L6_2atmpS1624;
    int32_t _M0L6_2atmpS1622;
    uint64_t _M0L6_2atmpS1621;
    if (_M0L8vpDiv100S646 > _M0L8vmDiv100S647) {
      uint64_t _M0L6_2atmpS1611 = _M0Lm2vrS606;
      uint64_t _M0L8vrDiv100S648 = _M0L6_2atmpS1611 / 100ull;
      uint64_t _M0L6_2atmpS1610 = _M0Lm2vrS606;
      int32_t _M0L6_2atmpS1607 = (int32_t)_M0L6_2atmpS1610;
      int32_t _M0L6_2atmpS1609 = (int32_t)_M0L8vrDiv100S648;
      int32_t _M0L6_2atmpS1608 = 100 * _M0L6_2atmpS1609;
      int32_t _M0L8vrMod100S649 = _M0L6_2atmpS1607 - _M0L6_2atmpS1608;
      int32_t _M0L6_2atmpS1606;
      _M0Lm7roundUpS645 = _M0L8vrMod100S649 >= 50;
      _M0Lm2vrS606 = _M0L8vrDiv100S648;
      _M0Lm2vpS607 = _M0L8vpDiv100S646;
      _M0Lm2vmS608 = _M0L8vmDiv100S647;
      _M0L6_2atmpS1606 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1606 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1619 = _M0Lm2vpS607;
      uint64_t _M0L7vpDiv10S650 = _M0L6_2atmpS1619 / 10ull;
      uint64_t _M0L6_2atmpS1618 = _M0Lm2vmS608;
      uint64_t _M0L7vmDiv10S651 = _M0L6_2atmpS1618 / 10ull;
      uint64_t _M0L6_2atmpS1617;
      uint64_t _M0L7vrDiv10S653;
      uint64_t _M0L6_2atmpS1616;
      int32_t _M0L6_2atmpS1613;
      int32_t _M0L6_2atmpS1615;
      int32_t _M0L6_2atmpS1614;
      int32_t _M0L7vrMod10S654;
      int32_t _M0L6_2atmpS1612;
      if (_M0L7vpDiv10S650 <= _M0L7vmDiv10S651) {
        break;
      }
      _M0L6_2atmpS1617 = _M0Lm2vrS606;
      _M0L7vrDiv10S653 = _M0L6_2atmpS1617 / 10ull;
      _M0L6_2atmpS1616 = _M0Lm2vrS606;
      _M0L6_2atmpS1613 = (int32_t)_M0L6_2atmpS1616;
      _M0L6_2atmpS1615 = (int32_t)_M0L7vrDiv10S653;
      _M0L6_2atmpS1614 = 10 * _M0L6_2atmpS1615;
      _M0L7vrMod10S654 = _M0L6_2atmpS1613 - _M0L6_2atmpS1614;
      _M0Lm7roundUpS645 = _M0L7vrMod10S654 >= 5;
      _M0Lm2vrS606 = _M0L7vrDiv10S653;
      _M0Lm2vpS607 = _M0L7vpDiv10S650;
      _M0Lm2vmS608 = _M0L7vmDiv10S651;
      _M0L6_2atmpS1612 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1612 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1620 = _M0Lm2vrS606;
    _M0L6_2atmpS1623 = _M0Lm2vrS606;
    _M0L6_2atmpS1624 = _M0Lm2vmS608;
    _M0L6_2atmpS1622
    = _M0L6_2atmpS1623 == _M0L6_2atmpS1624 || _M0Lm7roundUpS645;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1621 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1622);
    _M0Lm6outputS632 = _M0L6_2atmpS1620 + _M0L6_2atmpS1621;
  }
  _M0L6_2atmpS1628 = _M0Lm3e10S609;
  _M0L6_2atmpS1629 = _M0Lm7removedS630;
  _M0L3expS655 = _M0L6_2atmpS1628 + _M0L6_2atmpS1629;
  _M0L6_2atmpS1627 = _M0Lm6outputS632;
  _block_2082
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2082)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2082->$0 = _M0L6_2atmpS1627;
  _block_2082->$1 = _M0L3expS655;
  return _block_2082;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS598) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS598) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS597) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS597) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS596) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS596) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS595) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS595 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS595 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS595 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS595 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS595 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS595 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS595 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS595 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS595 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS595 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS595 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS595 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS595 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS595 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS595 >= 100ull) {
    return 3;
  }
  if (_M0L1vS595 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS578) {
  int32_t _M0L6_2atmpS1528;
  int32_t _M0L6_2atmpS1527;
  int32_t _M0L4baseS577;
  int32_t _M0L5base2S579;
  int32_t _M0L6offsetS580;
  int32_t _M0L6_2atmpS1526;
  uint64_t _M0L4mul0S581;
  int32_t _M0L6_2atmpS1525;
  int32_t _M0L6_2atmpS1524;
  uint64_t _M0L4mul1S582;
  uint64_t _M0L1mS583;
  struct _M0TPB7Umul128 _M0L7_2abindS584;
  uint64_t _M0L7_2alow1S585;
  uint64_t _M0L8_2ahigh1S586;
  struct _M0TPB7Umul128 _M0L7_2abindS587;
  uint64_t _M0L7_2alow0S588;
  uint64_t _M0L8_2ahigh0S589;
  uint64_t _M0L3sumS590;
  uint64_t _M0Lm5high1S591;
  int32_t _M0L6_2atmpS1522;
  int32_t _M0L6_2atmpS1523;
  int32_t _M0L5deltaS592;
  uint64_t _M0L6_2atmpS1521;
  uint64_t _M0L6_2atmpS1513;
  int32_t _M0L6_2atmpS1520;
  uint32_t _M0L6_2atmpS1517;
  int32_t _M0L6_2atmpS1519;
  int32_t _M0L6_2atmpS1518;
  uint32_t _M0L6_2atmpS1516;
  uint32_t _M0L6_2atmpS1515;
  uint64_t _M0L6_2atmpS1514;
  uint64_t _M0L1aS593;
  uint64_t _M0L6_2atmpS1512;
  uint64_t _M0L1bS594;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1528 = _M0L1iS578 + 26;
  _M0L6_2atmpS1527 = _M0L6_2atmpS1528 - 1;
  _M0L4baseS577 = _M0L6_2atmpS1527 / 26;
  _M0L5base2S579 = _M0L4baseS577 * 26;
  _M0L6offsetS580 = _M0L5base2S579 - _M0L1iS578;
  _M0L6_2atmpS1526 = _M0L4baseS577 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S581
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1526);
  _M0L6_2atmpS1525 = _M0L4baseS577 * 2;
  _M0L6_2atmpS1524 = _M0L6_2atmpS1525 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S582
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1524);
  if (_M0L6offsetS580 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S581, .$1 = _M0L4mul1S582};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS583
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS580);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS584 = _M0FPB7umul128(_M0L1mS583, _M0L4mul1S582);
  _M0L7_2alow1S585 = _M0L7_2abindS584.$0;
  _M0L8_2ahigh1S586 = _M0L7_2abindS584.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS587 = _M0FPB7umul128(_M0L1mS583, _M0L4mul0S581);
  _M0L7_2alow0S588 = _M0L7_2abindS587.$0;
  _M0L8_2ahigh0S589 = _M0L7_2abindS587.$1;
  _M0L3sumS590 = _M0L8_2ahigh0S589 + _M0L7_2alow1S585;
  _M0Lm5high1S591 = _M0L8_2ahigh1S586;
  if (_M0L3sumS590 < _M0L8_2ahigh0S589) {
    uint64_t _M0L6_2atmpS1511 = _M0Lm5high1S591;
    _M0Lm5high1S591 = _M0L6_2atmpS1511 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1522 = _M0FPB8pow5bits(_M0L5base2S579);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1523 = _M0FPB8pow5bits(_M0L1iS578);
  _M0L5deltaS592 = _M0L6_2atmpS1522 - _M0L6_2atmpS1523;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1521
  = _M0FPB13shiftright128(_M0L7_2alow0S588, _M0L3sumS590, _M0L5deltaS592);
  _M0L6_2atmpS1513 = _M0L6_2atmpS1521 + 1ull;
  _M0L6_2atmpS1520 = _M0L1iS578 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1517
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1520);
  _M0L6_2atmpS1519 = _M0L1iS578 % 16;
  _M0L6_2atmpS1518 = _M0L6_2atmpS1519 << 1;
  _M0L6_2atmpS1516 = _M0L6_2atmpS1517 >> (_M0L6_2atmpS1518 & 31);
  _M0L6_2atmpS1515 = _M0L6_2atmpS1516 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1514 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1515);
  _M0L1aS593 = _M0L6_2atmpS1513 + _M0L6_2atmpS1514;
  _M0L6_2atmpS1512 = _M0Lm5high1S591;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS594
  = _M0FPB13shiftright128(_M0L3sumS590, _M0L6_2atmpS1512, _M0L5deltaS592);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS593, .$1 = _M0L1bS594};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS560) {
  int32_t _M0L4baseS559;
  int32_t _M0L5base2S561;
  int32_t _M0L6offsetS562;
  int32_t _M0L6_2atmpS1510;
  uint64_t _M0L4mul0S563;
  int32_t _M0L6_2atmpS1509;
  int32_t _M0L6_2atmpS1508;
  uint64_t _M0L4mul1S564;
  uint64_t _M0L1mS565;
  struct _M0TPB7Umul128 _M0L7_2abindS566;
  uint64_t _M0L7_2alow1S567;
  uint64_t _M0L8_2ahigh1S568;
  struct _M0TPB7Umul128 _M0L7_2abindS569;
  uint64_t _M0L7_2alow0S570;
  uint64_t _M0L8_2ahigh0S571;
  uint64_t _M0L3sumS572;
  uint64_t _M0Lm5high1S573;
  int32_t _M0L6_2atmpS1506;
  int32_t _M0L6_2atmpS1507;
  int32_t _M0L5deltaS574;
  uint64_t _M0L6_2atmpS1498;
  int32_t _M0L6_2atmpS1505;
  uint32_t _M0L6_2atmpS1502;
  int32_t _M0L6_2atmpS1504;
  int32_t _M0L6_2atmpS1503;
  uint32_t _M0L6_2atmpS1501;
  uint32_t _M0L6_2atmpS1500;
  uint64_t _M0L6_2atmpS1499;
  uint64_t _M0L1aS575;
  uint64_t _M0L6_2atmpS1497;
  uint64_t _M0L1bS576;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS559 = _M0L1iS560 / 26;
  _M0L5base2S561 = _M0L4baseS559 * 26;
  _M0L6offsetS562 = _M0L1iS560 - _M0L5base2S561;
  _M0L6_2atmpS1510 = _M0L4baseS559 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S563
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1510);
  _M0L6_2atmpS1509 = _M0L4baseS559 * 2;
  _M0L6_2atmpS1508 = _M0L6_2atmpS1509 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S564
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1508);
  if (_M0L6offsetS562 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S563, .$1 = _M0L4mul1S564};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS565
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS562);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS566 = _M0FPB7umul128(_M0L1mS565, _M0L4mul1S564);
  _M0L7_2alow1S567 = _M0L7_2abindS566.$0;
  _M0L8_2ahigh1S568 = _M0L7_2abindS566.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS569 = _M0FPB7umul128(_M0L1mS565, _M0L4mul0S563);
  _M0L7_2alow0S570 = _M0L7_2abindS569.$0;
  _M0L8_2ahigh0S571 = _M0L7_2abindS569.$1;
  _M0L3sumS572 = _M0L8_2ahigh0S571 + _M0L7_2alow1S567;
  _M0Lm5high1S573 = _M0L8_2ahigh1S568;
  if (_M0L3sumS572 < _M0L8_2ahigh0S571) {
    uint64_t _M0L6_2atmpS1496 = _M0Lm5high1S573;
    _M0Lm5high1S573 = _M0L6_2atmpS1496 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1506 = _M0FPB8pow5bits(_M0L1iS560);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1507 = _M0FPB8pow5bits(_M0L5base2S561);
  _M0L5deltaS574 = _M0L6_2atmpS1506 - _M0L6_2atmpS1507;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1498
  = _M0FPB13shiftright128(_M0L7_2alow0S570, _M0L3sumS572, _M0L5deltaS574);
  _M0L6_2atmpS1505 = _M0L1iS560 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1502
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1505);
  _M0L6_2atmpS1504 = _M0L1iS560 % 16;
  _M0L6_2atmpS1503 = _M0L6_2atmpS1504 << 1;
  _M0L6_2atmpS1501 = _M0L6_2atmpS1502 >> (_M0L6_2atmpS1503 & 31);
  _M0L6_2atmpS1500 = _M0L6_2atmpS1501 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1499 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1500);
  _M0L1aS575 = _M0L6_2atmpS1498 + _M0L6_2atmpS1499;
  _M0L6_2atmpS1497 = _M0Lm5high1S573;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS576
  = _M0FPB13shiftright128(_M0L3sumS572, _M0L6_2atmpS1497, _M0L5deltaS574);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS575, .$1 = _M0L1bS576};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS533,
  struct _M0TPB8Pow5Pair _M0L3mulS530,
  int32_t _M0L1jS546,
  int32_t _M0L7mmShiftS548
) {
  uint64_t _M0L7_2amul0S529;
  uint64_t _M0L7_2amul1S531;
  uint64_t _M0L1mS532;
  struct _M0TPB7Umul128 _M0L7_2abindS534;
  uint64_t _M0L5_2aloS535;
  uint64_t _M0L6_2atmpS536;
  struct _M0TPB7Umul128 _M0L7_2abindS537;
  uint64_t _M0L6_2alo2S538;
  uint64_t _M0L6_2ahi2S539;
  uint64_t _M0L3midS540;
  uint64_t _M0L6_2atmpS1495;
  uint64_t _M0L2hiS541;
  uint64_t _M0L3lo2S542;
  uint64_t _M0L6_2atmpS1493;
  uint64_t _M0L6_2atmpS1494;
  uint64_t _M0L4mid2S543;
  uint64_t _M0L6_2atmpS1492;
  uint64_t _M0L3hi2S544;
  int32_t _M0L6_2atmpS1491;
  int32_t _M0L6_2atmpS1490;
  uint64_t _M0L2vpS545;
  uint64_t _M0Lm2vmS547;
  int32_t _M0L6_2atmpS1489;
  int32_t _M0L6_2atmpS1488;
  uint64_t _M0L2vrS558;
  uint64_t _M0L6_2atmpS1487;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S529 = _M0L3mulS530.$0;
  _M0L7_2amul1S531 = _M0L3mulS530.$1;
  _M0L1mS532 = _M0L1mS533 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS534 = _M0FPB7umul128(_M0L1mS532, _M0L7_2amul0S529);
  _M0L5_2aloS535 = _M0L7_2abindS534.$0;
  _M0L6_2atmpS536 = _M0L7_2abindS534.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS537 = _M0FPB7umul128(_M0L1mS532, _M0L7_2amul1S531);
  _M0L6_2alo2S538 = _M0L7_2abindS537.$0;
  _M0L6_2ahi2S539 = _M0L7_2abindS537.$1;
  _M0L3midS540 = _M0L6_2atmpS536 + _M0L6_2alo2S538;
  if (_M0L3midS540 < _M0L6_2atmpS536) {
    _M0L6_2atmpS1495 = 1ull;
  } else {
    _M0L6_2atmpS1495 = 0ull;
  }
  _M0L2hiS541 = _M0L6_2ahi2S539 + _M0L6_2atmpS1495;
  _M0L3lo2S542 = _M0L5_2aloS535 + _M0L7_2amul0S529;
  _M0L6_2atmpS1493 = _M0L3midS540 + _M0L7_2amul1S531;
  if (_M0L3lo2S542 < _M0L5_2aloS535) {
    _M0L6_2atmpS1494 = 1ull;
  } else {
    _M0L6_2atmpS1494 = 0ull;
  }
  _M0L4mid2S543 = _M0L6_2atmpS1493 + _M0L6_2atmpS1494;
  if (_M0L4mid2S543 < _M0L3midS540) {
    _M0L6_2atmpS1492 = 1ull;
  } else {
    _M0L6_2atmpS1492 = 0ull;
  }
  _M0L3hi2S544 = _M0L2hiS541 + _M0L6_2atmpS1492;
  _M0L6_2atmpS1491 = _M0L1jS546 - 64;
  _M0L6_2atmpS1490 = _M0L6_2atmpS1491 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS545
  = _M0FPB13shiftright128(_M0L4mid2S543, _M0L3hi2S544, _M0L6_2atmpS1490);
  _M0Lm2vmS547 = 0ull;
  if (_M0L7mmShiftS548) {
    uint64_t _M0L3lo3S549 = _M0L5_2aloS535 - _M0L7_2amul0S529;
    uint64_t _M0L6_2atmpS1477 = _M0L3midS540 - _M0L7_2amul1S531;
    uint64_t _M0L6_2atmpS1478;
    uint64_t _M0L4mid3S550;
    uint64_t _M0L6_2atmpS1476;
    uint64_t _M0L3hi3S551;
    int32_t _M0L6_2atmpS1475;
    int32_t _M0L6_2atmpS1474;
    if (_M0L5_2aloS535 < _M0L3lo3S549) {
      _M0L6_2atmpS1478 = 1ull;
    } else {
      _M0L6_2atmpS1478 = 0ull;
    }
    _M0L4mid3S550 = _M0L6_2atmpS1477 - _M0L6_2atmpS1478;
    if (_M0L3midS540 < _M0L4mid3S550) {
      _M0L6_2atmpS1476 = 1ull;
    } else {
      _M0L6_2atmpS1476 = 0ull;
    }
    _M0L3hi3S551 = _M0L2hiS541 - _M0L6_2atmpS1476;
    _M0L6_2atmpS1475 = _M0L1jS546 - 64;
    _M0L6_2atmpS1474 = _M0L6_2atmpS1475 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS547
    = _M0FPB13shiftright128(_M0L4mid3S550, _M0L3hi3S551, _M0L6_2atmpS1474);
  } else {
    uint64_t _M0L3lo3S552 = _M0L5_2aloS535 + _M0L5_2aloS535;
    uint64_t _M0L6_2atmpS1485 = _M0L3midS540 + _M0L3midS540;
    uint64_t _M0L6_2atmpS1486;
    uint64_t _M0L4mid3S553;
    uint64_t _M0L6_2atmpS1483;
    uint64_t _M0L6_2atmpS1484;
    uint64_t _M0L3hi3S554;
    uint64_t _M0L3lo4S555;
    uint64_t _M0L6_2atmpS1481;
    uint64_t _M0L6_2atmpS1482;
    uint64_t _M0L4mid4S556;
    uint64_t _M0L6_2atmpS1480;
    uint64_t _M0L3hi4S557;
    int32_t _M0L6_2atmpS1479;
    if (_M0L3lo3S552 < _M0L5_2aloS535) {
      _M0L6_2atmpS1486 = 1ull;
    } else {
      _M0L6_2atmpS1486 = 0ull;
    }
    _M0L4mid3S553 = _M0L6_2atmpS1485 + _M0L6_2atmpS1486;
    _M0L6_2atmpS1483 = _M0L2hiS541 + _M0L2hiS541;
    if (_M0L4mid3S553 < _M0L3midS540) {
      _M0L6_2atmpS1484 = 1ull;
    } else {
      _M0L6_2atmpS1484 = 0ull;
    }
    _M0L3hi3S554 = _M0L6_2atmpS1483 + _M0L6_2atmpS1484;
    _M0L3lo4S555 = _M0L3lo3S552 - _M0L7_2amul0S529;
    _M0L6_2atmpS1481 = _M0L4mid3S553 - _M0L7_2amul1S531;
    if (_M0L3lo3S552 < _M0L3lo4S555) {
      _M0L6_2atmpS1482 = 1ull;
    } else {
      _M0L6_2atmpS1482 = 0ull;
    }
    _M0L4mid4S556 = _M0L6_2atmpS1481 - _M0L6_2atmpS1482;
    if (_M0L4mid3S553 < _M0L4mid4S556) {
      _M0L6_2atmpS1480 = 1ull;
    } else {
      _M0L6_2atmpS1480 = 0ull;
    }
    _M0L3hi4S557 = _M0L3hi3S554 - _M0L6_2atmpS1480;
    _M0L6_2atmpS1479 = _M0L1jS546 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS547
    = _M0FPB13shiftright128(_M0L4mid4S556, _M0L3hi4S557, _M0L6_2atmpS1479);
  }
  _M0L6_2atmpS1489 = _M0L1jS546 - 64;
  _M0L6_2atmpS1488 = _M0L6_2atmpS1489 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS558
  = _M0FPB13shiftright128(_M0L3midS540, _M0L2hiS541, _M0L6_2atmpS1488);
  _M0L6_2atmpS1487 = _M0Lm2vmS547;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS558,
                                                .$1 = _M0L2vpS545,
                                                .$2 = _M0L6_2atmpS1487};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS527,
  int32_t _M0L1pS528
) {
  uint64_t _M0L6_2atmpS1473;
  uint64_t _M0L6_2atmpS1472;
  uint64_t _M0L6_2atmpS1471;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1473 = 1ull << (_M0L1pS528 & 63);
  _M0L6_2atmpS1472 = _M0L6_2atmpS1473 - 1ull;
  _M0L6_2atmpS1471 = _M0L5valueS527 & _M0L6_2atmpS1472;
  return _M0L6_2atmpS1471 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS525,
  int32_t _M0L1pS526
) {
  int32_t _M0L6_2atmpS1470;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1470 = _M0FPB10pow5Factor(_M0L5valueS525);
  return _M0L6_2atmpS1470 >= _M0L1pS526;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS520) {
  uint64_t _M0L6_2atmpS1461;
  uint64_t _M0L6_2atmpS1462;
  uint64_t _M0L6_2atmpS1463;
  uint64_t _M0L6_2atmpS1464;
  uint64_t _M0L6_2atmpS1469;
  int32_t _M0L5countS521;
  uint64_t _M0L1vS522;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1461 = _M0L5valueS520 % 5ull;
  if (_M0L6_2atmpS1461 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1462 = _M0L5valueS520 % 25ull;
  if (_M0L6_2atmpS1462 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1463 = _M0L5valueS520 % 125ull;
  if (_M0L6_2atmpS1463 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1464 = _M0L5valueS520 % 625ull;
  if (_M0L6_2atmpS1464 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1469 = _M0L5valueS520 / 625ull;
  _M0L5countS521 = 4;
  _M0L1vS522 = _M0L6_2atmpS1469;
  while (1) {
    if (_M0L1vS522 > 0ull) {
      uint64_t _M0L6_2atmpS1465 = _M0L1vS522 % 5ull;
      int32_t _M0L6_2atmpS1466;
      uint64_t _M0L6_2atmpS1467;
      if (_M0L6_2atmpS1465 != 0ull) {
        return _M0L5countS521;
      }
      _M0L6_2atmpS1466 = _M0L5countS521 + 1;
      _M0L6_2atmpS1467 = _M0L1vS522 / 5ull;
      _M0L5countS521 = _M0L6_2atmpS1466;
      _M0L1vS522 = _M0L6_2atmpS1467;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS524;
      moonbit_string_t _M0L6_2atmpS1468;
      int32_t _result_2084;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS524
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS524, (moonbit_string_t)moonbit_string_literal_19.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS524, _M0L5valueS520);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1468
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS524);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS524);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2084 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1468);
      moonbit_decref_cycle_free(_M0L6_2atmpS1468);
      return _result_2084;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS519,
  uint64_t _M0L2hiS517,
  int32_t _M0L4distS518
) {
  int32_t _M0L6_2atmpS1460;
  uint64_t _M0L6_2atmpS1458;
  uint64_t _M0L6_2atmpS1459;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1460 = 64 - _M0L4distS518;
  _M0L6_2atmpS1458 = _M0L2hiS517 << (_M0L6_2atmpS1460 & 63);
  _M0L6_2atmpS1459 = _M0L2loS519 >> (_M0L4distS518 & 63);
  return _M0L6_2atmpS1458 | _M0L6_2atmpS1459;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS507,
  uint64_t _M0L1bS510
) {
  uint64_t _M0L3aLoS506;
  uint64_t _M0L3aHiS508;
  uint64_t _M0L3bLoS509;
  uint64_t _M0L3bHiS511;
  uint64_t _M0L1xS512;
  uint64_t _M0L6_2atmpS1456;
  uint64_t _M0L6_2atmpS1457;
  uint64_t _M0L1yS513;
  uint64_t _M0L6_2atmpS1454;
  uint64_t _M0L6_2atmpS1455;
  uint64_t _M0L1zS514;
  uint64_t _M0L6_2atmpS1452;
  uint64_t _M0L6_2atmpS1453;
  uint64_t _M0L6_2atmpS1450;
  uint64_t _M0L6_2atmpS1451;
  uint64_t _M0L1wS515;
  uint64_t _M0L2loS516;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS506 = _M0L1aS507 & 4294967295ull;
  _M0L3aHiS508 = _M0L1aS507 >> 32;
  _M0L3bLoS509 = _M0L1bS510 & 4294967295ull;
  _M0L3bHiS511 = _M0L1bS510 >> 32;
  _M0L1xS512 = _M0L3aLoS506 * _M0L3bLoS509;
  _M0L6_2atmpS1456 = _M0L3aHiS508 * _M0L3bLoS509;
  _M0L6_2atmpS1457 = _M0L1xS512 >> 32;
  _M0L1yS513 = _M0L6_2atmpS1456 + _M0L6_2atmpS1457;
  _M0L6_2atmpS1454 = _M0L3aLoS506 * _M0L3bHiS511;
  _M0L6_2atmpS1455 = _M0L1yS513 & 4294967295ull;
  _M0L1zS514 = _M0L6_2atmpS1454 + _M0L6_2atmpS1455;
  _M0L6_2atmpS1452 = _M0L3aHiS508 * _M0L3bHiS511;
  _M0L6_2atmpS1453 = _M0L1yS513 >> 32;
  _M0L6_2atmpS1450 = _M0L6_2atmpS1452 + _M0L6_2atmpS1453;
  _M0L6_2atmpS1451 = _M0L1zS514 >> 32;
  _M0L1wS515 = _M0L6_2atmpS1450 + _M0L6_2atmpS1451;
  _M0L2loS516 = _M0L1aS507 * _M0L1bS510;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS516, .$1 = _M0L1wS515};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS504,
  int32_t _M0L4fromS501,
  int32_t _M0L2toS500
) {
  int32_t _M0L3lenS499;
  int32_t _M0L6_2atmpS1449;
  uint16_t* _M0L6bufferS502;
  int32_t _M0L1iS503;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS499 = _M0L2toS500 - _M0L4fromS501;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1449 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS502
  = (uint16_t*)moonbit_make_string(_M0L3lenS499, _M0L6_2atmpS1449);
  _M0L1iS503 = 0;
  while (1) {
    if (_M0L1iS503 < _M0L3lenS499) {
      int32_t _M0L6_2atmpS1447 = _M0L4fromS501 + _M0L1iS503;
      int32_t _M0L6_2atmpS1446;
      int32_t _M0L6_2atmpS1445;
      int32_t _M0L6_2atmpS1448;
      if (
        _M0L6_2atmpS1447 < 0
        || _M0L6_2atmpS1447 >= Moonbit_array_length(_M0L5bytesS504)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1446 = (int32_t)_M0L5bytesS504[_M0L6_2atmpS1447];
      _M0L6_2atmpS1445 = (uint16_t)_M0L6_2atmpS1446;
      if (
        _M0L1iS503 < 0 || _M0L1iS503 >= Moonbit_array_length(_M0L6bufferS502)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS502[_M0L1iS503] = _M0L6_2atmpS1445;
      _M0L6_2atmpS1448 = _M0L1iS503 + 1;
      _M0L1iS503 = _M0L6_2atmpS1448;
      continue;
    }
    break;
  }
  return _M0L6bufferS502;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS498) {
  int32_t _M0L6_2atmpS1444;
  uint32_t _M0L6_2atmpS1443;
  uint32_t _M0L6_2atmpS1442;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1444 = _M0L1eS498 * 78913;
  _M0L6_2atmpS1443 = *(uint32_t*)&_M0L6_2atmpS1444;
  _M0L6_2atmpS1442 = _M0L6_2atmpS1443 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1442;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS497) {
  int32_t _M0L6_2atmpS1441;
  uint32_t _M0L6_2atmpS1440;
  uint32_t _M0L6_2atmpS1439;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1441 = _M0L1eS497 * 732923;
  _M0L6_2atmpS1440 = *(uint32_t*)&_M0L6_2atmpS1441;
  _M0L6_2atmpS1439 = _M0L6_2atmpS1440 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1439;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS495,
  int32_t _M0L8exponentS496,
  int32_t _M0L8mantissaS493
) {
  moonbit_string_t _M0L1sS494;
  moonbit_string_t _result_2087;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS493) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
  }
  if (_M0L4signS495) {
    _M0L1sS494 = (moonbit_string_t)moonbit_string_literal_21.data;
  } else {
    _M0L1sS494 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS496) {
    moonbit_string_t _result_2086;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2086
    = moonbit_add_string(_M0L1sS494, (moonbit_string_t)moonbit_string_literal_22.data);
    moonbit_decref_cycle_free(_M0L1sS494);
    return _result_2086;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2087
  = moonbit_add_string(_M0L1sS494, (moonbit_string_t)moonbit_string_literal_23.data);
  moonbit_decref_cycle_free(_M0L1sS494);
  return _result_2087;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS492) {
  int32_t _M0L6_2atmpS1438;
  uint32_t _M0L6_2atmpS1437;
  uint32_t _M0L6_2atmpS1436;
  int32_t _M0L6_2atmpS1435;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1438 = _M0L1eS492 * 1217359;
  _M0L6_2atmpS1437 = *(uint32_t*)&_M0L6_2atmpS1438;
  _M0L6_2atmpS1436 = _M0L6_2atmpS1437 >> 19;
  _M0L6_2atmpS1435 = *(int32_t*)&_M0L6_2atmpS1436;
  return _M0L6_2atmpS1435 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS491) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS491 != _M0L4selfS491) {
    return 0;
  } else if (_M0L4selfS491 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS491 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS491;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS490) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS490 != _M0L4selfS490) {
    return 0ll;
  } else if (_M0L4selfS490 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS490 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS490;
  }
}

struct _M0TPB5ArrayGsE* _M0MPC15array5Array20unsafe__make__uninitGsE(
  int32_t _M0L3lenS489
) {
  moonbit_string_t* _M0L6_2atmpS1434;
  struct _M0TPB5ArrayGsE* _block_2088;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1434
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L3lenS489, (moonbit_string_t)moonbit_string_literal_0.data);
  _block_2088
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_block_2088)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _block_2088->$0 = _M0L6_2atmpS1434;
  _block_2088->$1 = _M0L3lenS489;
  return _block_2088;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS485,
  int32_t _M0L5indexS486
) {
  uint64_t* _M0L6_2atmpS1432;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1432 = _M0L4selfS485;
  if (
    _M0L5indexS486 < 0
    || _M0L5indexS486 >= Moonbit_array_length(_M0L6_2atmpS1432)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1432[_M0L5indexS486];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS487,
  int32_t _M0L5indexS488
) {
  uint32_t* _M0L6_2atmpS1433;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1433 = _M0L4selfS487;
  if (
    _M0L5indexS488 < 0
    || _M0L5indexS488 >= Moonbit_array_length(_M0L6_2atmpS1433)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1433[_M0L5indexS488];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS484
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS484, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS483) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS483, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS482) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS482;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS473,
  float _M0L5valueS475
) {
  int32_t _M0L3lenS1411;
  float* _M0L6_2atmpS1413;
  int32_t _M0L6_2atmpS1412;
  int32_t _M0L6lengthS474;
  float* _M0L3bufS1416;
  int32_t _M0L6_2atmpS1417;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1411 = _M0L4selfS473->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1413 = _M0MPC15array5Array6bufferGfE(_M0L4selfS473);
  _M0L6_2atmpS1412 = Moonbit_array_length(_M0L6_2atmpS1413);
  moonbit_decref_cycle_free(_M0L6_2atmpS1413);
  if (_M0L3lenS1411 == _M0L6_2atmpS1412) {
    int32_t _M0L3lenS1415 = _M0L4selfS473->$1;
    int32_t _M0L6_2atmpS1414 = _M0L3lenS1415 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS473, _M0L6_2atmpS1414);
  }
  _M0L6lengthS474 = _M0L4selfS473->$1;
  _M0L3bufS1416 = _M0L4selfS473->$0;
  _M0L3bufS1416[_M0L6lengthS474] = _M0L5valueS475;
  _M0L6_2atmpS1417 = _M0L6lengthS474 + 1;
  _M0L4selfS473->$1 = _M0L6_2atmpS1417;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS476,
  moonbit_string_t _M0L5valueS478
) {
  int32_t _M0L3lenS1418;
  moonbit_string_t* _M0L6_2atmpS1420;
  int32_t _M0L6_2atmpS1419;
  int32_t _M0L6lengthS477;
  moonbit_string_t* _M0L3bufS1423;
  moonbit_string_t _M0L6_2aoldS2007;
  int32_t _M0L6_2atmpS1424;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1418 = _M0L4selfS476->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1420 = _M0MPC15array5Array6bufferGsE(_M0L4selfS476);
  _M0L6_2atmpS1419 = Moonbit_array_length(_M0L6_2atmpS1420);
  moonbit_decref_cycle_free(_M0L6_2atmpS1420);
  if (_M0L3lenS1418 == _M0L6_2atmpS1419) {
    int32_t _M0L3lenS1422 = _M0L4selfS476->$1;
    int32_t _M0L6_2atmpS1421 = _M0L3lenS1422 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS476, _M0L6_2atmpS1421);
  }
  _M0L6lengthS477 = _M0L4selfS476->$1;
  _M0L3bufS1423 = _M0L4selfS476->$0;
  _M0L6_2aoldS2007 = (moonbit_string_t)_M0L3bufS1423[_M0L6lengthS477];
  moonbit_decref_cycle_free(_M0L6_2aoldS2007);
  _M0L3bufS1423[_M0L6lengthS477] = _M0L5valueS478;
  _M0L6_2atmpS1424 = _M0L6lengthS477 + 1;
  _M0L4selfS476->$1 = _M0L6_2atmpS1424;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS479,
  struct _M0TUsiE* _M0L5valueS481
) {
  int32_t _M0L3lenS1425;
  struct _M0TUsiE** _M0L6_2atmpS1427;
  int32_t _M0L6_2atmpS1426;
  int32_t _M0L6lengthS480;
  struct _M0TUsiE** _M0L3bufS1430;
  struct _M0TUsiE* _M0L6_2aoldS2008;
  int32_t _M0L6_2atmpS1431;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1425 = _M0L4selfS479->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1427 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS479);
  _M0L6_2atmpS1426 = Moonbit_array_length(_M0L6_2atmpS1427);
  moonbit_decref_cycle_free(_M0L6_2atmpS1427);
  if (_M0L3lenS1425 == _M0L6_2atmpS1426) {
    int32_t _M0L3lenS1429 = _M0L4selfS479->$1;
    int32_t _M0L6_2atmpS1428 = _M0L3lenS1429 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS479, _M0L6_2atmpS1428);
  }
  _M0L6lengthS480 = _M0L4selfS479->$1;
  _M0L3bufS1430 = _M0L4selfS479->$0;
  _M0L6_2aoldS2008 = (struct _M0TUsiE*)_M0L3bufS1430[_M0L6lengthS480];
  if (_M0L6_2aoldS2008) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2008);
  }
  _M0L3bufS1430[_M0L6lengthS480] = _M0L5valueS481;
  _M0L6_2atmpS1431 = _M0L6lengthS480 + 1;
  _M0L4selfS479->$1 = _M0L6_2atmpS1431;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS462,
  int32_t _M0L8requiredS464
) {
  int32_t _M0L8old__capS461;
  int32_t _M0L3lenS1408;
  int32_t _M0L8new__capS463;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS461 = _M0MPC15array5Array8capacityGfE(_M0L4selfS462);
  _M0L3lenS1408 = _M0L4selfS462->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS463
  = _M0FPB23array__growth__capacity(_M0L8old__capS461, _M0L3lenS1408, _M0L8requiredS464);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS462, _M0L8new__capS463);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS466,
  int32_t _M0L8requiredS468
) {
  int32_t _M0L8old__capS465;
  int32_t _M0L3lenS1409;
  int32_t _M0L8new__capS467;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS465 = _M0MPC15array5Array8capacityGsE(_M0L4selfS466);
  _M0L3lenS1409 = _M0L4selfS466->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS467
  = _M0FPB23array__growth__capacity(_M0L8old__capS465, _M0L3lenS1409, _M0L8requiredS468);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS466, _M0L8new__capS467);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS470,
  int32_t _M0L8requiredS472
) {
  int32_t _M0L8old__capS469;
  int32_t _M0L3lenS1410;
  int32_t _M0L8new__capS471;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS469 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS470);
  _M0L3lenS1410 = _M0L4selfS470->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS471
  = _M0FPB23array__growth__capacity(_M0L8old__capS469, _M0L3lenS1410, _M0L8requiredS472);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS470, _M0L8new__capS471);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS444,
  int32_t _M0L13new__capacityS447
) {
  float* _M0L8old__bufS443;
  int32_t _M0L3lenS445;
  int32_t _M0L9copy__lenS446;
  float* _M0L8new__bufS448;
  float* _M0L6_2aoldS2009;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS443 = _M0L4selfS444->$0;
  _M0L3lenS445 = _M0L4selfS444->$1;
  if (_M0L3lenS445 < _M0L13new__capacityS447) {
    _M0L9copy__lenS446 = _M0L3lenS445;
  } else {
    _M0L9copy__lenS446 = _M0L13new__capacityS447;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS443);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS448
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS443, _M0L13new__capacityS447, _M0L9copy__lenS446, 0, 0);
  _M0L6_2aoldS2009 = _M0L4selfS444->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2009);
  _M0L4selfS444->$0 = _M0L8new__bufS448;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS450,
  int32_t _M0L13new__capacityS453
) {
  moonbit_string_t* _M0L8old__bufS449;
  int32_t _M0L3lenS451;
  int32_t _M0L9copy__lenS452;
  moonbit_string_t* _M0L8new__bufS454;
  moonbit_string_t* _M0L6_2aoldS2010;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS449 = _M0L4selfS450->$0;
  _M0L3lenS451 = _M0L4selfS450->$1;
  if (_M0L3lenS451 < _M0L13new__capacityS453) {
    _M0L9copy__lenS452 = _M0L3lenS451;
  } else {
    _M0L9copy__lenS452 = _M0L13new__capacityS453;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS449);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS454
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS449, _M0L13new__capacityS453, _M0L9copy__lenS452, 0, 0);
  _M0L6_2aoldS2010 = _M0L4selfS450->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2010);
  _M0L4selfS450->$0 = _M0L8new__bufS454;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456,
  int32_t _M0L13new__capacityS459
) {
  struct _M0TUsiE** _M0L8old__bufS455;
  int32_t _M0L3lenS457;
  int32_t _M0L9copy__lenS458;
  struct _M0TUsiE** _M0L8new__bufS460;
  struct _M0TUsiE** _M0L6_2aoldS2011;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS455 = _M0L4selfS456->$0;
  _M0L3lenS457 = _M0L4selfS456->$1;
  if (_M0L3lenS457 < _M0L13new__capacityS459) {
    _M0L9copy__lenS458 = _M0L3lenS457;
  } else {
    _M0L9copy__lenS458 = _M0L13new__capacityS459;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS455);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS460
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS455, _M0L13new__capacityS459, _M0L9copy__lenS458, 0, 0);
  _M0L6_2aoldS2011 = _M0L4selfS456->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2011);
  _M0L4selfS456->$0 = _M0L8new__bufS460;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS440
) {
  float* _M0L6_2atmpS1405;
  int32_t _result_2089;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1405 = _M0MPC15array5Array6bufferGfE(_M0L4selfS440);
  _result_2089 = Moonbit_array_length(_M0L6_2atmpS1405);
  moonbit_decref_cycle_free(_M0L6_2atmpS1405);
  return _result_2089;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS441
) {
  moonbit_string_t* _M0L6_2atmpS1406;
  int32_t _result_2090;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1406 = _M0MPC15array5Array6bufferGsE(_M0L4selfS441);
  _result_2090 = Moonbit_array_length(_M0L6_2atmpS1406);
  moonbit_decref_cycle_free(_M0L6_2atmpS1406);
  return _result_2090;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS442
) {
  struct _M0TUsiE** _M0L6_2atmpS1407;
  int32_t _result_2091;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1407 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS442);
  _result_2091 = Moonbit_array_length(_M0L6_2atmpS1407);
  moonbit_decref_cycle_free(_M0L6_2atmpS1407);
  return _result_2091;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS436,
  int32_t _M0L3lenS434,
  int32_t _M0L8requiredS433
) {
  int32_t _M0L5startS435;
  int32_t _M0L5spaceS437;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS433 < _M0L3lenS434) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
  }
  if (_M0L7currentS436 == 0) {
    _M0L5startS435 = 8;
  } else {
    _M0L5startS435 = _M0L7currentS436;
  }
  _M0L5spaceS437 = _M0L5startS435;
  while (1) {
    if (_M0L5spaceS437 < _M0L8requiredS433) {
      int32_t _M0L4nextS438 = _M0L5spaceS437 * 2;
      if (_M0L4nextS438 <= _M0L5spaceS437) {
        return _M0L8requiredS433;
      }
      _M0L5spaceS437 = _M0L4nextS438;
      continue;
    } else {
      return _M0L5spaceS437;
    }
    break;
  }
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS430) {
  float* _M0L8_2afieldS2012;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2012 = _M0L4selfS430->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2012);
  return _M0L8_2afieldS2012;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS431
) {
  moonbit_string_t* _M0L8_2afieldS2013;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2013 = _M0L4selfS431->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2013);
  return _M0L8_2afieldS2013;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS432
) {
  struct _M0TUsiE** _M0L8_2afieldS2014;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2014 = _M0L4selfS432->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2014);
  return _M0L8_2afieldS2014;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS429
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS429);
  return _M0L4selfS429;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS428,
  struct _M0TPC16string10StringView _M0L3strS426
) {
  int32_t _M0L3endS1403;
  int32_t _M0L5startS1404;
  int32_t _M0L8str__lenS425;
  int32_t _M0L3lenS1402;
  int32_t _M0L8requiredS427;
  uint16_t* _M0L4dataS1395;
  int32_t _M0L6_2atmpS1394;
  int32_t _if__result_2093;
  uint16_t* _M0L4dataS1396;
  int32_t _M0L3lenS1397;
  moonbit_string_t _M0L6_2atmpS1398;
  int32_t _M0L6_2atmpS1399;
  int32_t _M0L3lenS1401;
  int32_t _M0L6_2atmpS1400;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1403 = _M0L3strS426.$2;
  _M0L5startS1404 = _M0L3strS426.$1;
  _M0L8str__lenS425 = _M0L3endS1403 - _M0L5startS1404;
  if (_M0L8str__lenS425 == 0) {
    return 0;
  }
  _M0L3lenS1402 = _M0L4selfS428->$1;
  _M0L8requiredS427 = _M0L3lenS1402 + _M0L8str__lenS425;
  _M0L4dataS1395 = _M0L4selfS428->$0;
  _M0L6_2atmpS1394 = Moonbit_array_length(_M0L4dataS1395);
  if (_M0L8requiredS427 > _M0L6_2atmpS1394) {
    _if__result_2093 = 1;
  } else {
    int32_t _M0L3lenS1393 = _M0L4selfS428->$1;
    _if__result_2093 = _M0L8requiredS427 < _M0L3lenS1393;
  }
  if (_if__result_2093) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS428, _M0L8requiredS427);
  }
  _M0L4dataS1396 = _M0L4selfS428->$0;
  _M0L3lenS1397 = _M0L4selfS428->$1;
  moonbit_incref_cycle_free(_M0L4dataS1396);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1398 = _M0MPC16string10StringView4data(_M0L3strS426);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1399 = _M0MPC16string10StringView13start__offset(_M0L3strS426);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1396, _M0L3lenS1397, _M0L6_2atmpS1398, _M0L6_2atmpS1399, _M0L8str__lenS425);
  moonbit_decref_cycle_free(_M0L4dataS1396);
  moonbit_decref_cycle_free(_M0L6_2atmpS1398);
  _M0L3lenS1401 = _M0L4selfS428->$1;
  _M0L6_2atmpS1400 = _M0L3lenS1401 + _M0L8str__lenS425;
  _M0L4selfS428->$1 = _M0L6_2atmpS1400;
  return 0;
}

moonbit_string_t _M0MPC16string6String4make(
  int32_t _M0L6lengthS420,
  int32_t _M0L5valueS421
) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L6lengthS420 >= 0) {
    int32_t _M0L6_2atmpS1390 = _M0L5valueS421;
    if (_M0L6_2atmpS1390 <= 65535) {
      #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      return _M0FPB20unsafe__make__string(_M0L6lengthS420, _M0L5valueS421);
    } else {
      int32_t _M0L6_2atmpS1392 = 2 * _M0L6lengthS420;
      struct _M0TPB13StringBuilder* _M0L3bufS422;
      int32_t _M0L2__S423;
      moonbit_string_t _result_2095;
      #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      _M0L3bufS422
      = _M0MPB13StringBuilder21StringBuilder_2einner(_M0L6_2atmpS1392);
      _M0L2__S423 = 0;
      while (1) {
        if (_M0L2__S423 < _M0L6lengthS420) {
          int32_t _M0L6_2atmpS1391;
          #line 33 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
          _M0IPB13StringBuilderPB6Logger11write__char(_M0L3bufS422, _M0L5valueS421);
          _M0L6_2atmpS1391 = _M0L2__S423 + 1;
          _M0L2__S423 = _M0L6_2atmpS1391;
          continue;
        }
        break;
      }
      #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      _result_2095 = _M0MPB13StringBuilder10to__string(_M0L3bufS422);
      moonbit_decref_cycle_free(_M0L3bufS422);
      return _result_2095;
    }
  } else {
    #line 27 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
    return _M0FPC15abort5abortGsE((moonbit_string_t)moonbit_string_literal_25.data);
  }
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS417,
  int32_t _M0L5startS415,
  int32_t _M0L3endS416
) {
  int32_t _if__result_2096;
  int32_t _M0L3lenS418;
  int32_t _M0L6_2atmpS1389;
  moonbit_bytes_t _M0L5bytesS419;
  moonbit_bytes_t _M0L6_2atmpS1388;
  moonbit_string_t _result_2097;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS415 == 0) {
    int32_t _M0L6_2atmpS1387 = Moonbit_array_length(_M0L3strS417);
    _if__result_2096 = _M0L3endS416 == _M0L6_2atmpS1387;
  } else {
    _if__result_2096 = 0;
  }
  if (_if__result_2096) {
    moonbit_incref_cycle_free(_M0L3strS417);
    return _M0L3strS417;
  }
  _M0L3lenS418 = _M0L3endS416 - _M0L5startS415;
  _M0L6_2atmpS1389 = _M0L3lenS418 * 2;
  _M0L5bytesS419 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1389, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS419, 0, _M0L3strS417, _M0L5startS415, _M0L3lenS418);
  _M0L6_2atmpS1388 = _M0L5bytesS419;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2097
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1388, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1388);
  return _result_2097;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS410,
  int32_t _M0L6offsetS414,
  int64_t _M0L6lengthS412
) {
  int32_t _M0L3lenS409;
  int32_t _M0L6lengthS411;
  int32_t _if__result_2098;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS409 = Moonbit_array_length(_M0L4selfS410);
  if (_M0L6lengthS412 == 4294967296ll) {
    _M0L6lengthS411 = _M0L3lenS409 - _M0L6offsetS414;
  } else {
    int64_t _M0L7_2aSomeS413 = _M0L6lengthS412;
    _M0L6lengthS411 = (int32_t)_M0L7_2aSomeS413;
  }
  if (_M0L6offsetS414 >= 0) {
    if (_M0L6lengthS411 >= 0) {
      int32_t _M0L6_2atmpS1386 = _M0L6offsetS414 + _M0L6lengthS411;
      _if__result_2098 = _M0L6_2atmpS1386 <= _M0L3lenS409;
    } else {
      _if__result_2098 = 0;
    }
  } else {
    _if__result_2098 = 0;
  }
  if (_if__result_2098) {
    moonbit_incref_cycle_free(_M0L4selfS410);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS410, _M0L6offsetS414, _M0L6lengthS411);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS401,
  int32_t _M0L13bytes__offsetS396,
  moonbit_string_t _M0L3strS403,
  int32_t _M0L11str__offsetS399,
  int32_t _M0L6lengthS397
) {
  int32_t _M0L6_2atmpS1385;
  int32_t _M0L6_2atmpS1384;
  int32_t _M0L2e1S395;
  int32_t _M0L6_2atmpS1383;
  int32_t _M0L2e2S398;
  int32_t _M0L4len1S400;
  int32_t _M0L4len2S402;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1385 = _M0L6lengthS397 * 2;
  _M0L6_2atmpS1384 = _M0L13bytes__offsetS396 + _M0L6_2atmpS1385;
  _M0L2e1S395 = _M0L6_2atmpS1384 - 1;
  _M0L6_2atmpS1383 = _M0L11str__offsetS399 + _M0L6lengthS397;
  _M0L2e2S398 = _M0L6_2atmpS1383 - 1;
  _M0L4len1S400 = Moonbit_array_length(_M0L4selfS401);
  _M0L4len2S402 = Moonbit_array_length(_M0L3strS403);
  if (
    _M0L6lengthS397 >= 0
    && _M0L13bytes__offsetS396 >= 0
    && _M0L2e1S395 < _M0L4len1S400
    && _M0L11str__offsetS399 >= 0
    && _M0L2e2S398 < _M0L4len2S402
  ) {
    int32_t _M0L16end__str__offsetS404 =
      _M0L11str__offsetS399 + _M0L6lengthS397;
    int32_t _M0L1iS405 = _M0L11str__offsetS399;
    int32_t _M0L1jS406 = _M0L13bytes__offsetS396;
    while (1) {
      if (_M0L1iS405 < _M0L16end__str__offsetS404) {
        int32_t _M0L6_2atmpS1380 = _M0L3strS403[_M0L1iS405];
        int32_t _M0L6_2atmpS1379 = (int32_t)_M0L6_2atmpS1380;
        uint32_t _M0L1cS407 = *(uint32_t*)&_M0L6_2atmpS1379;
        uint32_t _M0L6_2atmpS1375 = _M0L1cS407 & 255u;
        int32_t _M0L6_2atmpS1374;
        int32_t _M0L6_2atmpS1376;
        uint32_t _M0L6_2atmpS1378;
        int32_t _M0L6_2atmpS1377;
        int32_t _M0L6_2atmpS1381;
        int32_t _M0L6_2atmpS1382;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1374 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1375);
        if (
          _M0L1jS406 < 0 || _M0L1jS406 >= Moonbit_array_length(_M0L4selfS401)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS401[_M0L1jS406] = _M0L6_2atmpS1374;
        _M0L6_2atmpS1376 = _M0L1jS406 + 1;
        _M0L6_2atmpS1378 = _M0L1cS407 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1377 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1378);
        if (
          _M0L6_2atmpS1376 < 0
          || _M0L6_2atmpS1376 >= Moonbit_array_length(_M0L4selfS401)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS401[_M0L6_2atmpS1376] = _M0L6_2atmpS1377;
        _M0L6_2atmpS1381 = _M0L1iS405 + 1;
        _M0L6_2atmpS1382 = _M0L1jS406 + 2;
        _M0L1iS405 = _M0L6_2atmpS1381;
        _M0L1jS406 = _M0L6_2atmpS1382;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS394) {
  int32_t _M0L6_2atmpS1373;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1373 = *(int32_t*)&_M0L4selfS394;
  return _M0L6_2atmpS1373 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS386,
  int32_t _M0L5radixS385
) {
  uint16_t* _M0L6bufferS387;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS385 < 2 || _M0L5radixS385 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  if (_M0L4selfS386 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  switch (_M0L5radixS385) {
    case 10: {
      int32_t _M0L3lenS388;
      uint16_t* _M0L6bufferS389;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS388 = _M0FPB12dec__count64(_M0L4selfS386);
      _M0L6bufferS389 = (uint16_t*)moonbit_make_string(_M0L3lenS388, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS389, _M0L4selfS386, 0, _M0L3lenS388);
      _M0L6bufferS387 = _M0L6bufferS389;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS390;
      uint16_t* _M0L6bufferS391;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS390 = _M0FPB12hex__count64(_M0L4selfS386);
      _M0L6bufferS391 = (uint16_t*)moonbit_make_string(_M0L3lenS390, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS391, _M0L4selfS386, 0, _M0L3lenS390);
      _M0L6bufferS387 = _M0L6bufferS391;
      break;
    }
    default: {
      int32_t _M0L3lenS392;
      uint16_t* _M0L6bufferS393;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS392 = _M0FPB14radix__count64(_M0L4selfS386, _M0L5radixS385);
      _M0L6bufferS393 = (uint16_t*)moonbit_make_string(_M0L3lenS392, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS393, _M0L4selfS386, 0, _M0L3lenS392, _M0L5radixS385);
      _M0L6bufferS387 = _M0L6bufferS393;
      break;
    }
  }
  return _M0L6bufferS387;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS369,
  int32_t _M0L5radixS368
) {
  int32_t _M0L12is__negativeS370;
  uint64_t _M0L3numS371;
  uint16_t* _M0L6bufferS372;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS368 < 2 || _M0L5radixS368 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  if (_M0L4selfS369 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  _M0L12is__negativeS370 = _M0L4selfS369 < 0ll;
  if (_M0L12is__negativeS370) {
    int64_t _M0L6_2atmpS1372 = -_M0L4selfS369;
    _M0L3numS371 = *(uint64_t*)&_M0L6_2atmpS1372;
  } else {
    _M0L3numS371 = *(uint64_t*)&_M0L4selfS369;
  }
  switch (_M0L5radixS368) {
    case 10: {
      int32_t _M0L10digit__lenS373;
      int32_t _M0L6_2atmpS1369;
      int32_t _M0L10total__lenS374;
      uint16_t* _M0L6bufferS375;
      int32_t _M0L12digit__startS376;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS373 = _M0FPB12dec__count64(_M0L3numS371);
      if (_M0L12is__negativeS370) {
        _M0L6_2atmpS1369 = 1;
      } else {
        _M0L6_2atmpS1369 = 0;
      }
      _M0L10total__lenS374 = _M0L10digit__lenS373 + _M0L6_2atmpS1369;
      _M0L6bufferS375
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS374, 0);
      if (_M0L12is__negativeS370) {
        _M0L12digit__startS376 = 1;
      } else {
        _M0L12digit__startS376 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS375, _M0L3numS371, _M0L12digit__startS376, _M0L10total__lenS374);
      _M0L6bufferS372 = _M0L6bufferS375;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS377;
      int32_t _M0L6_2atmpS1370;
      int32_t _M0L10total__lenS378;
      uint16_t* _M0L6bufferS379;
      int32_t _M0L12digit__startS380;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS377 = _M0FPB12hex__count64(_M0L3numS371);
      if (_M0L12is__negativeS370) {
        _M0L6_2atmpS1370 = 1;
      } else {
        _M0L6_2atmpS1370 = 0;
      }
      _M0L10total__lenS378 = _M0L10digit__lenS377 + _M0L6_2atmpS1370;
      _M0L6bufferS379
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS378, 0);
      if (_M0L12is__negativeS370) {
        _M0L12digit__startS380 = 1;
      } else {
        _M0L12digit__startS380 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS379, _M0L3numS371, _M0L12digit__startS380, _M0L10total__lenS378);
      _M0L6bufferS372 = _M0L6bufferS379;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS381;
      int32_t _M0L6_2atmpS1371;
      int32_t _M0L10total__lenS382;
      uint16_t* _M0L6bufferS383;
      int32_t _M0L12digit__startS384;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS381
      = _M0FPB14radix__count64(_M0L3numS371, _M0L5radixS368);
      if (_M0L12is__negativeS370) {
        _M0L6_2atmpS1371 = 1;
      } else {
        _M0L6_2atmpS1371 = 0;
      }
      _M0L10total__lenS382 = _M0L10digit__lenS381 + _M0L6_2atmpS1371;
      _M0L6bufferS383
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS382, 0);
      if (_M0L12is__negativeS370) {
        _M0L12digit__startS384 = 1;
      } else {
        _M0L12digit__startS384 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS383, _M0L3numS371, _M0L12digit__startS384, _M0L10total__lenS382, _M0L5radixS368);
      _M0L6bufferS372 = _M0L6bufferS383;
      break;
    }
  }
  if (_M0L12is__negativeS370) {
    _M0L6bufferS372[0] = 45;
  }
  return _M0L6bufferS372;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS354,
  uint64_t _M0L3numS366,
  int32_t _M0L12digit__startS355,
  int32_t _M0L10total__lenS367
) {
  int32_t _M0L6_2atmpS1368;
  uint64_t _M0L3numS344;
  int32_t _M0L6offsetS345;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1368 = _M0L10total__lenS367 - _M0L12digit__startS355;
  _M0L3numS344 = _M0L3numS366;
  _M0L6offsetS345 = _M0L6_2atmpS1368;
  while (1) {
    if (_M0L3numS344 >= 10000ull) {
      uint64_t _M0L1tS346 = _M0L3numS344 / 10000ull;
      uint64_t _M0L6_2atmpS1345 = _M0L3numS344 % 10000ull;
      int32_t _M0L1rS347 = (int32_t)_M0L6_2atmpS1345;
      int32_t _M0L2d1S348 = _M0L1rS347 / 100;
      int32_t _M0L2d2S349 = _M0L1rS347 % 100;
      int32_t _M0L6_2atmpS1344 = _M0L2d1S348 / 10;
      int32_t _M0L6_2atmpS1343 = 48 + _M0L6_2atmpS1344;
      int32_t _M0L6d1__hiS350 = (uint16_t)_M0L6_2atmpS1343;
      int32_t _M0L6_2atmpS1342 = _M0L2d1S348 % 10;
      int32_t _M0L6_2atmpS1341 = 48 + _M0L6_2atmpS1342;
      int32_t _M0L6d1__loS351 = (uint16_t)_M0L6_2atmpS1341;
      int32_t _M0L6_2atmpS1340 = _M0L2d2S349 / 10;
      int32_t _M0L6_2atmpS1339 = 48 + _M0L6_2atmpS1340;
      int32_t _M0L6d2__hiS352 = (uint16_t)_M0L6_2atmpS1339;
      int32_t _M0L6_2atmpS1338 = _M0L2d2S349 % 10;
      int32_t _M0L6_2atmpS1337 = 48 + _M0L6_2atmpS1338;
      int32_t _M0L6d2__loS353 = (uint16_t)_M0L6_2atmpS1337;
      int32_t _M0L6_2atmpS1329 = _M0L12digit__startS355 + _M0L6offsetS345;
      int32_t _M0L6_2atmpS1328 = _M0L6_2atmpS1329 - 4;
      int32_t _M0L6_2atmpS1331;
      int32_t _M0L6_2atmpS1330;
      int32_t _M0L6_2atmpS1333;
      int32_t _M0L6_2atmpS1332;
      int32_t _M0L6_2atmpS1335;
      int32_t _M0L6_2atmpS1334;
      int32_t _M0L6_2atmpS1336;
      _M0L6bufferS354[_M0L6_2atmpS1328] = _M0L6d1__hiS350;
      _M0L6_2atmpS1331 = _M0L12digit__startS355 + _M0L6offsetS345;
      _M0L6_2atmpS1330 = _M0L6_2atmpS1331 - 3;
      _M0L6bufferS354[_M0L6_2atmpS1330] = _M0L6d1__loS351;
      _M0L6_2atmpS1333 = _M0L12digit__startS355 + _M0L6offsetS345;
      _M0L6_2atmpS1332 = _M0L6_2atmpS1333 - 2;
      _M0L6bufferS354[_M0L6_2atmpS1332] = _M0L6d2__hiS352;
      _M0L6_2atmpS1335 = _M0L12digit__startS355 + _M0L6offsetS345;
      _M0L6_2atmpS1334 = _M0L6_2atmpS1335 - 1;
      _M0L6bufferS354[_M0L6_2atmpS1334] = _M0L6d2__loS353;
      _M0L6_2atmpS1336 = _M0L6offsetS345 - 4;
      _M0L3numS344 = _M0L1tS346;
      _M0L6offsetS345 = _M0L6_2atmpS1336;
      continue;
    } else {
      int32_t _M0L6_2atmpS1367 = (int32_t)_M0L3numS344;
      int32_t _M0L9remainingS357 = _M0L6_2atmpS1367;
      int32_t _M0L6offsetS358 = _M0L6offsetS345;
      while (1) {
        if (_M0L9remainingS357 >= 100) {
          int32_t _M0L1tS359 = _M0L9remainingS357 / 100;
          int32_t _M0L1dS360 = _M0L9remainingS357 % 100;
          int32_t _M0L6_2atmpS1354 = _M0L1dS360 / 10;
          int32_t _M0L6_2atmpS1353 = 48 + _M0L6_2atmpS1354;
          int32_t _M0L5d__hiS361 = (uint16_t)_M0L6_2atmpS1353;
          int32_t _M0L6_2atmpS1352 = _M0L1dS360 % 10;
          int32_t _M0L6_2atmpS1351 = 48 + _M0L6_2atmpS1352;
          int32_t _M0L5d__loS362 = (uint16_t)_M0L6_2atmpS1351;
          int32_t _M0L6_2atmpS1347 = _M0L12digit__startS355 + _M0L6offsetS358;
          int32_t _M0L6_2atmpS1346 = _M0L6_2atmpS1347 - 2;
          int32_t _M0L6_2atmpS1349;
          int32_t _M0L6_2atmpS1348;
          int32_t _M0L6_2atmpS1350;
          _M0L6bufferS354[_M0L6_2atmpS1346] = _M0L5d__hiS361;
          _M0L6_2atmpS1349 = _M0L12digit__startS355 + _M0L6offsetS358;
          _M0L6_2atmpS1348 = _M0L6_2atmpS1349 - 1;
          _M0L6bufferS354[_M0L6_2atmpS1348] = _M0L5d__loS362;
          _M0L6_2atmpS1350 = _M0L6offsetS358 - 2;
          _M0L9remainingS357 = _M0L1tS359;
          _M0L6offsetS358 = _M0L6_2atmpS1350;
          continue;
        } else if (_M0L9remainingS357 >= 10) {
          int32_t _M0L6_2atmpS1362 = _M0L9remainingS357 / 10;
          int32_t _M0L6_2atmpS1361 = 48 + _M0L6_2atmpS1362;
          int32_t _M0L5d__hiS364 = (uint16_t)_M0L6_2atmpS1361;
          int32_t _M0L6_2atmpS1360 = _M0L9remainingS357 % 10;
          int32_t _M0L6_2atmpS1359 = 48 + _M0L6_2atmpS1360;
          int32_t _M0L5d__loS365 = (uint16_t)_M0L6_2atmpS1359;
          int32_t _M0L6_2atmpS1356 = _M0L12digit__startS355 + _M0L6offsetS358;
          int32_t _M0L6_2atmpS1355 = _M0L6_2atmpS1356 - 2;
          int32_t _M0L6_2atmpS1358;
          int32_t _M0L6_2atmpS1357;
          _M0L6bufferS354[_M0L6_2atmpS1355] = _M0L5d__hiS364;
          _M0L6_2atmpS1358 = _M0L12digit__startS355 + _M0L6offsetS358;
          _M0L6_2atmpS1357 = _M0L6_2atmpS1358 - 1;
          _M0L6bufferS354[_M0L6_2atmpS1357] = _M0L5d__loS365;
        } else {
          int32_t _M0L6_2atmpS1366 = _M0L12digit__startS355 + _M0L6offsetS358;
          int32_t _M0L6_2atmpS1363 = _M0L6_2atmpS1366 - 1;
          int32_t _M0L6_2atmpS1365 = 48 + _M0L9remainingS357;
          int32_t _M0L6_2atmpS1364 = (uint16_t)_M0L6_2atmpS1365;
          _M0L6bufferS354[_M0L6_2atmpS1363] = _M0L6_2atmpS1364;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS334,
  uint64_t _M0L3numS338,
  int32_t _M0L12digit__startS335,
  int32_t _M0L10total__lenS337,
  int32_t _M0L5radixS328
) {
  uint64_t _M0L4baseS327;
  int32_t _M0L6_2atmpS1313;
  int32_t _M0L6_2atmpS1312;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS327 = _M0MPC13int3Int10to__uint64(_M0L5radixS328);
  _M0L6_2atmpS1313 = _M0L5radixS328 - 1;
  _M0L6_2atmpS1312 = _M0L5radixS328 & _M0L6_2atmpS1313;
  if (_M0L6_2atmpS1312 == 0) {
    int32_t _M0L5shiftS329;
    uint64_t _M0L4maskS330;
    int32_t _M0L6_2atmpS1320;
    int32_t _M0L6offsetS331;
    uint64_t _M0L1nS332;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS329 = moonbit_ctz32(_M0L5radixS328);
    _M0L4maskS330 = _M0L4baseS327 - 1ull;
    _M0L6_2atmpS1320 = _M0L10total__lenS337 - _M0L12digit__startS335;
    _M0L6offsetS331 = _M0L6_2atmpS1320;
    _M0L1nS332 = _M0L3numS338;
    while (1) {
      if (_M0L1nS332 > 0ull) {
        uint64_t _M0L6_2atmpS1319 = _M0L1nS332 & _M0L4maskS330;
        int32_t _M0L5digitS333 = (int32_t)_M0L6_2atmpS1319;
        int32_t _M0L6_2atmpS1316 = _M0L12digit__startS335 + _M0L6offsetS331;
        int32_t _M0L6_2atmpS1314 = _M0L6_2atmpS1316 - 1;
        int32_t _M0L6_2atmpS1315 =
          ((moonbit_string_t)moonbit_string_literal_27.data)[_M0L5digitS333];
        int32_t _M0L6_2atmpS1317;
        uint64_t _M0L6_2atmpS1318;
        _M0L6bufferS334[_M0L6_2atmpS1314] = _M0L6_2atmpS1315;
        _M0L6_2atmpS1317 = _M0L6offsetS331 - 1;
        _M0L6_2atmpS1318 = _M0L1nS332 >> (_M0L5shiftS329 & 63);
        _M0L6offsetS331 = _M0L6_2atmpS1317;
        _M0L1nS332 = _M0L6_2atmpS1318;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1327 = _M0L10total__lenS337 - _M0L12digit__startS335;
    int32_t _M0L6offsetS339 = _M0L6_2atmpS1327;
    uint64_t _M0L1nS340 = _M0L3numS338;
    while (1) {
      if (_M0L1nS340 > 0ull) {
        uint64_t _M0L1qS341 = _M0L1nS340 / _M0L4baseS327;
        uint64_t _M0L6_2atmpS1326 = _M0L1qS341 * _M0L4baseS327;
        uint64_t _M0L6_2atmpS1325 = _M0L1nS340 - _M0L6_2atmpS1326;
        int32_t _M0L5digitS342 = (int32_t)_M0L6_2atmpS1325;
        int32_t _M0L6_2atmpS1323 = _M0L12digit__startS335 + _M0L6offsetS339;
        int32_t _M0L6_2atmpS1321 = _M0L6_2atmpS1323 - 1;
        int32_t _M0L6_2atmpS1322 =
          ((moonbit_string_t)moonbit_string_literal_27.data)[_M0L5digitS342];
        int32_t _M0L6_2atmpS1324;
        _M0L6bufferS334[_M0L6_2atmpS1321] = _M0L6_2atmpS1322;
        _M0L6_2atmpS1324 = _M0L6offsetS339 - 1;
        _M0L6offsetS339 = _M0L6_2atmpS1324;
        _M0L1nS340 = _M0L1qS341;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS321,
  uint64_t _M0L3numS326,
  int32_t _M0L12digit__startS322,
  int32_t _M0L10total__lenS325
) {
  int32_t _M0L6_2atmpS1311;
  int32_t _M0L6offsetS316;
  uint64_t _M0L1nS317;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1311 = _M0L10total__lenS325 - _M0L12digit__startS322;
  _M0L6offsetS316 = _M0L6_2atmpS1311;
  _M0L1nS317 = _M0L3numS326;
  while (1) {
    if (_M0L6offsetS316 >= 2) {
      uint64_t _M0L6_2atmpS1308 = _M0L1nS317 & 255ull;
      int32_t _M0L9byte__valS318 = (int32_t)_M0L6_2atmpS1308;
      int32_t _M0L2hiS319 = _M0L9byte__valS318 / 16;
      int32_t _M0L2loS320 = _M0L9byte__valS318 % 16;
      int32_t _M0L6_2atmpS1302 = _M0L12digit__startS322 + _M0L6offsetS316;
      int32_t _M0L6_2atmpS1300 = _M0L6_2atmpS1302 - 2;
      int32_t _M0L6_2atmpS1301 =
        ((moonbit_string_t)moonbit_string_literal_27.data)[_M0L2hiS319];
      int32_t _M0L6_2atmpS1305;
      int32_t _M0L6_2atmpS1303;
      int32_t _M0L6_2atmpS1304;
      int32_t _M0L6_2atmpS1306;
      uint64_t _M0L6_2atmpS1307;
      _M0L6bufferS321[_M0L6_2atmpS1300] = _M0L6_2atmpS1301;
      _M0L6_2atmpS1305 = _M0L12digit__startS322 + _M0L6offsetS316;
      _M0L6_2atmpS1303 = _M0L6_2atmpS1305 - 1;
      _M0L6_2atmpS1304
      = ((moonbit_string_t)moonbit_string_literal_27.data)[
        _M0L2loS320
      ];
      _M0L6bufferS321[_M0L6_2atmpS1303] = _M0L6_2atmpS1304;
      _M0L6_2atmpS1306 = _M0L6offsetS316 - 2;
      _M0L6_2atmpS1307 = _M0L1nS317 >> 8;
      _M0L6offsetS316 = _M0L6_2atmpS1306;
      _M0L1nS317 = _M0L6_2atmpS1307;
      continue;
    } else if (_M0L6offsetS316 == 1) {
      uint64_t _M0L6_2atmpS1310 = _M0L1nS317 & 15ull;
      int32_t _M0L6nibbleS324 = (int32_t)_M0L6_2atmpS1310;
      int32_t _M0L6_2atmpS1309 =
        ((moonbit_string_t)moonbit_string_literal_27.data)[_M0L6nibbleS324];
      _M0L6bufferS321[_M0L12digit__startS322] = _M0L6_2atmpS1309;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS310,
  int32_t _M0L5radixS312
) {
  uint64_t _M0L4baseS311;
  uint64_t _M0L3numS313;
  int32_t _M0L5countS314;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS310 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS311 = _M0MPC13int3Int10to__uint64(_M0L5radixS312);
  _M0L3numS313 = _M0L5valueS310;
  _M0L5countS314 = 0;
  while (1) {
    if (_M0L3numS313 > 0ull) {
      uint64_t _M0L6_2atmpS1298 = _M0L3numS313 / _M0L4baseS311;
      int32_t _M0L6_2atmpS1299 = _M0L5countS314 + 1;
      _M0L3numS313 = _M0L6_2atmpS1298;
      _M0L5countS314 = _M0L6_2atmpS1299;
      continue;
    } else {
      return _M0L5countS314;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS308) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS308 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS309;
    int32_t _M0L6_2atmpS1297;
    int32_t _M0L6_2atmpS1296;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS309 = moonbit_clz64(_M0L5valueS308);
    _M0L6_2atmpS1297 = 63 - _M0L14leading__zerosS309;
    _M0L6_2atmpS1296 = _M0L6_2atmpS1297 / 4;
    return _M0L6_2atmpS1296 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS307) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS307 >= 10000000000ull) {
    if (_M0L5valueS307 >= 100000000000000ull) {
      if (_M0L5valueS307 >= 10000000000000000ull) {
        if (_M0L5valueS307 >= 1000000000000000000ull) {
          if (_M0L5valueS307 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS307 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS307 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS307 >= 1000000000000ull) {
      if (_M0L5valueS307 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS307 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS307 >= 100000ull) {
    if (_M0L5valueS307 >= 10000000ull) {
      if (_M0L5valueS307 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS307 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS307 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS307 >= 1000ull) {
    if (_M0L5valueS307 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS307 >= 100ull) {
    return 3;
  } else if (_M0L5valueS307 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS291,
  int32_t _M0L5radixS290
) {
  int32_t _M0L12is__negativeS292;
  uint32_t _M0L3numS293;
  uint16_t* _M0L6bufferS294;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS290 < 2 || _M0L5radixS290 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  if (_M0L4selfS291 == 0) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  _M0L12is__negativeS292 = _M0L4selfS291 < 0;
  if (_M0L12is__negativeS292) {
    int32_t _M0L6_2atmpS1295 = -_M0L4selfS291;
    _M0L3numS293 = *(uint32_t*)&_M0L6_2atmpS1295;
  } else {
    _M0L3numS293 = *(uint32_t*)&_M0L4selfS291;
  }
  switch (_M0L5radixS290) {
    case 10: {
      int32_t _M0L10digit__lenS295;
      int32_t _M0L6_2atmpS1292;
      int32_t _M0L10total__lenS296;
      uint16_t* _M0L6bufferS297;
      int32_t _M0L12digit__startS298;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS295 = _M0FPB12dec__count32(_M0L3numS293);
      if (_M0L12is__negativeS292) {
        _M0L6_2atmpS1292 = 1;
      } else {
        _M0L6_2atmpS1292 = 0;
      }
      _M0L10total__lenS296 = _M0L10digit__lenS295 + _M0L6_2atmpS1292;
      _M0L6bufferS297
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS296, 0);
      if (_M0L12is__negativeS292) {
        _M0L12digit__startS298 = 1;
      } else {
        _M0L12digit__startS298 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS297, _M0L3numS293, _M0L12digit__startS298, _M0L10total__lenS296);
      _M0L6bufferS294 = _M0L6bufferS297;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS299;
      int32_t _M0L6_2atmpS1293;
      int32_t _M0L10total__lenS300;
      uint16_t* _M0L6bufferS301;
      int32_t _M0L12digit__startS302;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS299 = _M0FPB12hex__count32(_M0L3numS293);
      if (_M0L12is__negativeS292) {
        _M0L6_2atmpS1293 = 1;
      } else {
        _M0L6_2atmpS1293 = 0;
      }
      _M0L10total__lenS300 = _M0L10digit__lenS299 + _M0L6_2atmpS1293;
      _M0L6bufferS301
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS300, 0);
      if (_M0L12is__negativeS292) {
        _M0L12digit__startS302 = 1;
      } else {
        _M0L12digit__startS302 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS301, _M0L3numS293, _M0L12digit__startS302, _M0L10total__lenS300);
      _M0L6bufferS294 = _M0L6bufferS301;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS303;
      int32_t _M0L6_2atmpS1294;
      int32_t _M0L10total__lenS304;
      uint16_t* _M0L6bufferS305;
      int32_t _M0L12digit__startS306;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS303
      = _M0FPB14radix__count32(_M0L3numS293, _M0L5radixS290);
      if (_M0L12is__negativeS292) {
        _M0L6_2atmpS1294 = 1;
      } else {
        _M0L6_2atmpS1294 = 0;
      }
      _M0L10total__lenS304 = _M0L10digit__lenS303 + _M0L6_2atmpS1294;
      _M0L6bufferS305
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS304, 0);
      if (_M0L12is__negativeS292) {
        _M0L12digit__startS306 = 1;
      } else {
        _M0L12digit__startS306 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS305, _M0L3numS293, _M0L12digit__startS306, _M0L10total__lenS304, _M0L5radixS290);
      _M0L6bufferS294 = _M0L6bufferS305;
      break;
    }
  }
  if (_M0L12is__negativeS292) {
    _M0L6bufferS294[0] = 45;
  }
  return _M0L6bufferS294;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS284,
  int32_t _M0L5radixS286
) {
  uint32_t _M0L4baseS285;
  uint32_t _M0L3numS287;
  int32_t _M0L5countS288;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS284 == 0u) {
    return 1;
  }
  _M0L4baseS285 = *(uint32_t*)&_M0L5radixS286;
  _M0L3numS287 = _M0L5valueS284;
  _M0L5countS288 = 0;
  while (1) {
    if (_M0L3numS287 > 0u) {
      uint32_t _M0L6_2atmpS1290 = _M0L3numS287 / _M0L4baseS285;
      int32_t _M0L6_2atmpS1291 = _M0L5countS288 + 1;
      _M0L3numS287 = _M0L6_2atmpS1290;
      _M0L5countS288 = _M0L6_2atmpS1291;
      continue;
    } else {
      return _M0L5countS288;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS282) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS282 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS283;
    int32_t _M0L6_2atmpS1289;
    int32_t _M0L6_2atmpS1288;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS283 = moonbit_clz32(_M0L5valueS282);
    _M0L6_2atmpS1289 = 31 - _M0L14leading__zerosS283;
    _M0L6_2atmpS1288 = _M0L6_2atmpS1289 / 4;
    return _M0L6_2atmpS1288 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS281) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS281 >= 100000u) {
    if (_M0L5valueS281 >= 10000000u) {
      if (_M0L5valueS281 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS281 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS281 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS281 >= 1000u) {
    if (_M0L5valueS281 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS281 >= 100u) {
    return 3;
  } else if (_M0L5valueS281 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS267,
  uint32_t _M0L3numS279,
  int32_t _M0L12digit__startS268,
  int32_t _M0L10total__lenS280
) {
  int32_t _M0L6_2atmpS1287;
  uint32_t _M0L3numS257;
  int32_t _M0L6offsetS258;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1287 = _M0L10total__lenS280 - _M0L12digit__startS268;
  _M0L3numS257 = _M0L3numS279;
  _M0L6offsetS258 = _M0L6_2atmpS1287;
  while (1) {
    if (_M0L3numS257 >= 10000u) {
      uint32_t _M0L1tS259 = _M0L3numS257 / 10000u;
      uint32_t _M0L6_2atmpS1264 = _M0L3numS257 % 10000u;
      int32_t _M0L1rS260 = *(int32_t*)&_M0L6_2atmpS1264;
      int32_t _M0L2d1S261 = _M0L1rS260 / 100;
      int32_t _M0L2d2S262 = _M0L1rS260 % 100;
      int32_t _M0L6_2atmpS1263 = _M0L2d1S261 / 10;
      int32_t _M0L6_2atmpS1262 = 48 + _M0L6_2atmpS1263;
      int32_t _M0L6d1__hiS263 = (uint16_t)_M0L6_2atmpS1262;
      int32_t _M0L6_2atmpS1261 = _M0L2d1S261 % 10;
      int32_t _M0L6_2atmpS1260 = 48 + _M0L6_2atmpS1261;
      int32_t _M0L6d1__loS264 = (uint16_t)_M0L6_2atmpS1260;
      int32_t _M0L6_2atmpS1259 = _M0L2d2S262 / 10;
      int32_t _M0L6_2atmpS1258 = 48 + _M0L6_2atmpS1259;
      int32_t _M0L6d2__hiS265 = (uint16_t)_M0L6_2atmpS1258;
      int32_t _M0L6_2atmpS1257 = _M0L2d2S262 % 10;
      int32_t _M0L6_2atmpS1256 = 48 + _M0L6_2atmpS1257;
      int32_t _M0L6d2__loS266 = (uint16_t)_M0L6_2atmpS1256;
      int32_t _M0L6_2atmpS1248 = _M0L12digit__startS268 + _M0L6offsetS258;
      int32_t _M0L6_2atmpS1247 = _M0L6_2atmpS1248 - 4;
      int32_t _M0L6_2atmpS1250;
      int32_t _M0L6_2atmpS1249;
      int32_t _M0L6_2atmpS1252;
      int32_t _M0L6_2atmpS1251;
      int32_t _M0L6_2atmpS1254;
      int32_t _M0L6_2atmpS1253;
      int32_t _M0L6_2atmpS1255;
      _M0L6bufferS267[_M0L6_2atmpS1247] = _M0L6d1__hiS263;
      _M0L6_2atmpS1250 = _M0L12digit__startS268 + _M0L6offsetS258;
      _M0L6_2atmpS1249 = _M0L6_2atmpS1250 - 3;
      _M0L6bufferS267[_M0L6_2atmpS1249] = _M0L6d1__loS264;
      _M0L6_2atmpS1252 = _M0L12digit__startS268 + _M0L6offsetS258;
      _M0L6_2atmpS1251 = _M0L6_2atmpS1252 - 2;
      _M0L6bufferS267[_M0L6_2atmpS1251] = _M0L6d2__hiS265;
      _M0L6_2atmpS1254 = _M0L12digit__startS268 + _M0L6offsetS258;
      _M0L6_2atmpS1253 = _M0L6_2atmpS1254 - 1;
      _M0L6bufferS267[_M0L6_2atmpS1253] = _M0L6d2__loS266;
      _M0L6_2atmpS1255 = _M0L6offsetS258 - 4;
      _M0L3numS257 = _M0L1tS259;
      _M0L6offsetS258 = _M0L6_2atmpS1255;
      continue;
    } else {
      int32_t _M0L6_2atmpS1286 = *(int32_t*)&_M0L3numS257;
      int32_t _M0L9remainingS270 = _M0L6_2atmpS1286;
      int32_t _M0L6offsetS271 = _M0L6offsetS258;
      while (1) {
        if (_M0L9remainingS270 >= 100) {
          int32_t _M0L1tS272 = _M0L9remainingS270 / 100;
          int32_t _M0L1dS273 = _M0L9remainingS270 % 100;
          int32_t _M0L6_2atmpS1273 = _M0L1dS273 / 10;
          int32_t _M0L6_2atmpS1272 = 48 + _M0L6_2atmpS1273;
          int32_t _M0L5d__hiS274 = (uint16_t)_M0L6_2atmpS1272;
          int32_t _M0L6_2atmpS1271 = _M0L1dS273 % 10;
          int32_t _M0L6_2atmpS1270 = 48 + _M0L6_2atmpS1271;
          int32_t _M0L5d__loS275 = (uint16_t)_M0L6_2atmpS1270;
          int32_t _M0L6_2atmpS1266 = _M0L12digit__startS268 + _M0L6offsetS271;
          int32_t _M0L6_2atmpS1265 = _M0L6_2atmpS1266 - 2;
          int32_t _M0L6_2atmpS1268;
          int32_t _M0L6_2atmpS1267;
          int32_t _M0L6_2atmpS1269;
          _M0L6bufferS267[_M0L6_2atmpS1265] = _M0L5d__hiS274;
          _M0L6_2atmpS1268 = _M0L12digit__startS268 + _M0L6offsetS271;
          _M0L6_2atmpS1267 = _M0L6_2atmpS1268 - 1;
          _M0L6bufferS267[_M0L6_2atmpS1267] = _M0L5d__loS275;
          _M0L6_2atmpS1269 = _M0L6offsetS271 - 2;
          _M0L9remainingS270 = _M0L1tS272;
          _M0L6offsetS271 = _M0L6_2atmpS1269;
          continue;
        } else if (_M0L9remainingS270 >= 10) {
          int32_t _M0L6_2atmpS1281 = _M0L9remainingS270 / 10;
          int32_t _M0L6_2atmpS1280 = 48 + _M0L6_2atmpS1281;
          int32_t _M0L5d__hiS277 = (uint16_t)_M0L6_2atmpS1280;
          int32_t _M0L6_2atmpS1279 = _M0L9remainingS270 % 10;
          int32_t _M0L6_2atmpS1278 = 48 + _M0L6_2atmpS1279;
          int32_t _M0L5d__loS278 = (uint16_t)_M0L6_2atmpS1278;
          int32_t _M0L6_2atmpS1275 = _M0L12digit__startS268 + _M0L6offsetS271;
          int32_t _M0L6_2atmpS1274 = _M0L6_2atmpS1275 - 2;
          int32_t _M0L6_2atmpS1277;
          int32_t _M0L6_2atmpS1276;
          _M0L6bufferS267[_M0L6_2atmpS1274] = _M0L5d__hiS277;
          _M0L6_2atmpS1277 = _M0L12digit__startS268 + _M0L6offsetS271;
          _M0L6_2atmpS1276 = _M0L6_2atmpS1277 - 1;
          _M0L6bufferS267[_M0L6_2atmpS1276] = _M0L5d__loS278;
        } else {
          int32_t _M0L6_2atmpS1285 = _M0L12digit__startS268 + _M0L6offsetS271;
          int32_t _M0L6_2atmpS1282 = _M0L6_2atmpS1285 - 1;
          int32_t _M0L6_2atmpS1284 = 48 + _M0L9remainingS270;
          int32_t _M0L6_2atmpS1283 = (uint16_t)_M0L6_2atmpS1284;
          _M0L6bufferS267[_M0L6_2atmpS1282] = _M0L6_2atmpS1283;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS247,
  uint32_t _M0L3numS251,
  int32_t _M0L12digit__startS248,
  int32_t _M0L10total__lenS250,
  int32_t _M0L5radixS241
) {
  uint32_t _M0L4baseS240;
  int32_t _M0L6_2atmpS1232;
  int32_t _M0L6_2atmpS1231;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS240 = *(uint32_t*)&_M0L5radixS241;
  _M0L6_2atmpS1232 = _M0L5radixS241 - 1;
  _M0L6_2atmpS1231 = _M0L5radixS241 & _M0L6_2atmpS1232;
  if (_M0L6_2atmpS1231 == 0) {
    int32_t _M0L5shiftS242;
    uint32_t _M0L4maskS243;
    int32_t _M0L6_2atmpS1239;
    int32_t _M0L6offsetS244;
    uint32_t _M0L1nS245;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS242 = moonbit_ctz32(_M0L5radixS241);
    _M0L4maskS243 = _M0L4baseS240 - 1u;
    _M0L6_2atmpS1239 = _M0L10total__lenS250 - _M0L12digit__startS248;
    _M0L6offsetS244 = _M0L6_2atmpS1239;
    _M0L1nS245 = _M0L3numS251;
    while (1) {
      if (_M0L1nS245 > 0u) {
        uint32_t _M0L6_2atmpS1238 = _M0L1nS245 & _M0L4maskS243;
        int32_t _M0L5digitS246 = *(int32_t*)&_M0L6_2atmpS1238;
        int32_t _M0L6_2atmpS1235 = _M0L12digit__startS248 + _M0L6offsetS244;
        int32_t _M0L6_2atmpS1233 = _M0L6_2atmpS1235 - 1;
        int32_t _M0L6_2atmpS1234 =
          ((moonbit_string_t)moonbit_string_literal_27.data)[_M0L5digitS246];
        int32_t _M0L6_2atmpS1236;
        uint32_t _M0L6_2atmpS1237;
        _M0L6bufferS247[_M0L6_2atmpS1233] = _M0L6_2atmpS1234;
        _M0L6_2atmpS1236 = _M0L6offsetS244 - 1;
        _M0L6_2atmpS1237 = _M0L1nS245 >> (_M0L5shiftS242 & 31);
        _M0L6offsetS244 = _M0L6_2atmpS1236;
        _M0L1nS245 = _M0L6_2atmpS1237;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1246 = _M0L10total__lenS250 - _M0L12digit__startS248;
    int32_t _M0L6offsetS252 = _M0L6_2atmpS1246;
    uint32_t _M0L1nS253 = _M0L3numS251;
    while (1) {
      if (_M0L1nS253 > 0u) {
        uint32_t _M0L1qS254 = _M0L1nS253 / _M0L4baseS240;
        uint32_t _M0L6_2atmpS1245 = _M0L1qS254 * _M0L4baseS240;
        uint32_t _M0L6_2atmpS1244 = _M0L1nS253 - _M0L6_2atmpS1245;
        int32_t _M0L5digitS255 = *(int32_t*)&_M0L6_2atmpS1244;
        int32_t _M0L6_2atmpS1242 = _M0L12digit__startS248 + _M0L6offsetS252;
        int32_t _M0L6_2atmpS1240 = _M0L6_2atmpS1242 - 1;
        int32_t _M0L6_2atmpS1241 =
          ((moonbit_string_t)moonbit_string_literal_27.data)[_M0L5digitS255];
        int32_t _M0L6_2atmpS1243;
        _M0L6bufferS247[_M0L6_2atmpS1240] = _M0L6_2atmpS1241;
        _M0L6_2atmpS1243 = _M0L6offsetS252 - 1;
        _M0L6offsetS252 = _M0L6_2atmpS1243;
        _M0L1nS253 = _M0L1qS254;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS234,
  uint32_t _M0L3numS239,
  int32_t _M0L12digit__startS235,
  int32_t _M0L10total__lenS238
) {
  int32_t _M0L6_2atmpS1230;
  int32_t _M0L6offsetS229;
  uint32_t _M0L1nS230;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1230 = _M0L10total__lenS238 - _M0L12digit__startS235;
  _M0L6offsetS229 = _M0L6_2atmpS1230;
  _M0L1nS230 = _M0L3numS239;
  while (1) {
    if (_M0L6offsetS229 >= 2) {
      uint32_t _M0L6_2atmpS1227 = _M0L1nS230 & 255u;
      int32_t _M0L9byte__valS231 = *(int32_t*)&_M0L6_2atmpS1227;
      int32_t _M0L2hiS232 = _M0L9byte__valS231 / 16;
      int32_t _M0L2loS233 = _M0L9byte__valS231 % 16;
      int32_t _M0L6_2atmpS1221 = _M0L12digit__startS235 + _M0L6offsetS229;
      int32_t _M0L6_2atmpS1219 = _M0L6_2atmpS1221 - 2;
      int32_t _M0L6_2atmpS1220 =
        ((moonbit_string_t)moonbit_string_literal_27.data)[_M0L2hiS232];
      int32_t _M0L6_2atmpS1224;
      int32_t _M0L6_2atmpS1222;
      int32_t _M0L6_2atmpS1223;
      int32_t _M0L6_2atmpS1225;
      uint32_t _M0L6_2atmpS1226;
      _M0L6bufferS234[_M0L6_2atmpS1219] = _M0L6_2atmpS1220;
      _M0L6_2atmpS1224 = _M0L12digit__startS235 + _M0L6offsetS229;
      _M0L6_2atmpS1222 = _M0L6_2atmpS1224 - 1;
      _M0L6_2atmpS1223
      = ((moonbit_string_t)moonbit_string_literal_27.data)[
        _M0L2loS233
      ];
      _M0L6bufferS234[_M0L6_2atmpS1222] = _M0L6_2atmpS1223;
      _M0L6_2atmpS1225 = _M0L6offsetS229 - 2;
      _M0L6_2atmpS1226 = _M0L1nS230 >> 8;
      _M0L6offsetS229 = _M0L6_2atmpS1225;
      _M0L1nS230 = _M0L6_2atmpS1226;
      continue;
    } else if (_M0L6offsetS229 == 1) {
      uint32_t _M0L6_2atmpS1229 = _M0L1nS230 & 15u;
      int32_t _M0L6nibbleS237 = *(int32_t*)&_M0L6_2atmpS1229;
      int32_t _M0L6_2atmpS1228 =
        ((moonbit_string_t)moonbit_string_literal_27.data)[_M0L6nibbleS237];
      _M0L6bufferS234[_M0L12digit__startS235] = _M0L6_2atmpS1228;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS228
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS227;
  struct _M0TPB6Logger _M0L6_2atmpS1218;
  moonbit_string_t _result_2112;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS227 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS227);
  _M0L6_2atmpS1218
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS227
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS228, _M0L6_2atmpS1218);
  if (_M0L6_2atmpS1218.$1) {
    moonbit_decref(_M0L6_2atmpS1218.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2112 = _M0MPB13StringBuilder10to__string(_M0L6loggerS227);
  moonbit_decref_cycle_free(_M0L6loggerS227);
  return _result_2112;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS222,
  struct _M0TPB6Logger _M0L6loggerS221
) {
  moonbit_string_t _M0L6_2atmpS1215;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1215 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS222);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS221.$0->$method_0(_M0L6loggerS221.$1, _M0L6_2atmpS1215);
  moonbit_decref_cycle_free(_M0L6_2atmpS1215);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS224,
  struct _M0TPB6Logger _M0L6loggerS223
) {
  moonbit_string_t _M0L6_2atmpS1216;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1216 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS224);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS223.$0->$method_0(_M0L6loggerS223.$1, _M0L6_2atmpS1216);
  moonbit_decref_cycle_free(_M0L6_2atmpS1216);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS226,
  struct _M0TPB6Logger _M0L6loggerS225
) {
  moonbit_string_t _M0L6_2atmpS1217;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1217 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS226);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS225.$0->$method_0(_M0L6loggerS225.$1, _M0L6_2atmpS1217);
  moonbit_decref_cycle_free(_M0L6_2atmpS1217);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS220
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS220.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS219
) {
  moonbit_string_t _M0L8_2afieldS2015;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2015 = _M0L4selfS219.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2015);
  return _M0L8_2afieldS2015;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS215,
  moonbit_string_t _M0L5valueS216,
  int32_t _M0L5startS217,
  int32_t _M0L3lenS218
) {
  int32_t _M0L6_2atmpS1214;
  int64_t _M0L6_2atmpS1213;
  struct _M0TPC16string10StringView _M0L6_2atmpS1212;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1214 = _M0L5startS217 + _M0L3lenS218;
  _M0L6_2atmpS1213 = (int64_t)_M0L6_2atmpS1214;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1212
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS216, _M0L5startS217, _M0L6_2atmpS1213);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS215, _M0L6_2atmpS1212);
  moonbit_decref_cycle_free(_M0L6_2atmpS1212.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS208,
  int32_t _M0L5startS210,
  int64_t _M0L3endS212
) {
  int32_t _M0L3lenS207;
  int32_t _M0Lm2loS209;
  int32_t _M0Lm2hiS211;
  int32_t _M0L6_2atmpS1196;
  int32_t _if__result_2113;
  int32_t _M0L6_2atmpS1204;
  int32_t _if__result_2114;
  int32_t _M0L6_2atmpS1206;
  int32_t _M0L6_2atmpS1207;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS207 = Moonbit_array_length(_M0L4selfS208);
  if (_M0L5startS210 < 0) {
    _M0Lm2loS209 = 0;
  } else if (_M0L5startS210 > _M0L3lenS207) {
    _M0Lm2loS209 = _M0L3lenS207;
  } else {
    _M0Lm2loS209 = _M0L5startS210;
  }
  if (_M0L3endS212 == 4294967296ll) {
    _M0Lm2hiS211 = _M0L3lenS207;
  } else {
    int64_t _M0L7_2aSomeS213 = _M0L3endS212;
    int32_t _M0L4_2aeS214 = (int32_t)_M0L7_2aSomeS213;
    if (_M0L4_2aeS214 < 0) {
      _M0Lm2hiS211 = 0;
    } else if (_M0L4_2aeS214 > _M0L3lenS207) {
      _M0Lm2hiS211 = _M0L3lenS207;
    } else {
      _M0Lm2hiS211 = _M0L4_2aeS214;
    }
  }
  _M0L6_2atmpS1196 = _M0Lm2loS209;
  if (_M0L6_2atmpS1196 > 0) {
    int32_t _M0L6_2atmpS1195 = _M0Lm2loS209;
    if (_M0L6_2atmpS1195 < _M0L3lenS207) {
      int32_t _M0L6_2atmpS1194 = _M0Lm2loS209;
      int32_t _M0L6_2atmpS1193 = _M0L4selfS208[_M0L6_2atmpS1194];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1193)) {
        int32_t _M0L6_2atmpS1192 = _M0Lm2loS209;
        int32_t _M0L6_2atmpS1191 = _M0L6_2atmpS1192 - 1;
        int32_t _M0L6_2atmpS1190 = _M0L4selfS208[_M0L6_2atmpS1191];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2113
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1190);
      } else {
        _if__result_2113 = 0;
      }
    } else {
      _if__result_2113 = 0;
    }
  } else {
    _if__result_2113 = 0;
  }
  if (_if__result_2113) {
    int32_t _M0L6_2atmpS1197 = _M0Lm2loS209;
    _M0Lm2loS209 = _M0L6_2atmpS1197 + 1;
  }
  _M0L6_2atmpS1204 = _M0Lm2hiS211;
  if (_M0L6_2atmpS1204 > 0) {
    int32_t _M0L6_2atmpS1203 = _M0Lm2hiS211;
    if (_M0L6_2atmpS1203 < _M0L3lenS207) {
      int32_t _M0L6_2atmpS1202 = _M0Lm2hiS211;
      int32_t _M0L6_2atmpS1201 = _M0L4selfS208[_M0L6_2atmpS1202];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1201)) {
        int32_t _M0L6_2atmpS1200 = _M0Lm2hiS211;
        int32_t _M0L6_2atmpS1199 = _M0L6_2atmpS1200 - 1;
        int32_t _M0L6_2atmpS1198 = _M0L4selfS208[_M0L6_2atmpS1199];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2114
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1198);
      } else {
        _if__result_2114 = 0;
      }
    } else {
      _if__result_2114 = 0;
    }
  } else {
    _if__result_2114 = 0;
  }
  if (_if__result_2114) {
    int32_t _M0L6_2atmpS1205 = _M0Lm2hiS211;
    _M0Lm2hiS211 = _M0L6_2atmpS1205 - 1;
  }
  _M0L6_2atmpS1206 = _M0Lm2loS209;
  _M0L6_2atmpS1207 = _M0Lm2hiS211;
  if (_M0L6_2atmpS1206 >= _M0L6_2atmpS1207) {
    int32_t _M0L6_2atmpS1208 = _M0Lm2loS209;
    int32_t _M0L6_2atmpS1209 = _M0Lm2loS209;
    moonbit_incref_cycle_free(_M0L4selfS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS208,
                                                 .$1 = _M0L6_2atmpS1208,
                                                 .$2 = _M0L6_2atmpS1209};
  } else {
    int32_t _M0L6_2atmpS1210 = _M0Lm2loS209;
    int32_t _M0L6_2atmpS1211 = _M0Lm2hiS211;
    moonbit_incref_cycle_free(_M0L4selfS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS208,
                                                 .$1 = _M0L6_2atmpS1210,
                                                 .$2 = _M0L6_2atmpS1211};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS206,
  struct _M0TPB4Show _M0L4showS205
) {
  struct _M0TPB6Logger _M0L6_2atmpS1189;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS206);
  _M0L6_2atmpS1189
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS206
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS205.$0->$method_0(_M0L4showS205.$1, _M0L6_2atmpS1189);
  if (_M0L6_2atmpS1189.$1) {
    moonbit_decref(_M0L6_2atmpS1189.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS204,
  struct _M0TPB4Show _M0L4showS203
) {
  struct _M0TPB6Logger _M0L6_2atmpS1188;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS204);
  _M0L6_2atmpS1188
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS204
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS203.$0->$method_0(_M0L4showS203.$1, _M0L6_2atmpS1188);
  if (_M0L6_2atmpS1188.$1) {
    moonbit_decref(_M0L6_2atmpS1188.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS202) {
  int64_t _M0L6_2atmpS1187;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1187 = (int64_t)_M0L4selfS202;
  return *(uint64_t*)&_M0L6_2atmpS1187;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS200,
  int32_t _M0L5quoteS201
) {
  struct _M0TPB13StringBuilder* _M0L3bufS199;
  int32_t _M0L6_2atmpS1186;
  struct _M0TPC16string10StringView _M0L6_2atmpS1184;
  struct _M0TPB6Logger _M0L6_2atmpS1185;
  moonbit_string_t _result_2115;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS199 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1186 = Moonbit_array_length(_M0L4selfS200);
  moonbit_incref_cycle_free(_M0L4selfS200);
  _M0L6_2atmpS1184
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS200, .$1 = 0, .$2 = _M0L6_2atmpS1186
  };
  moonbit_incref_cycle_free(_M0L3bufS199);
  _M0L6_2atmpS1185
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS199
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1184, _M0L6_2atmpS1185, _M0L5quoteS201);
  moonbit_decref_cycle_free(_M0L6_2atmpS1184.$0);
  if (_M0L6_2atmpS1185.$1) {
    moonbit_decref(_M0L6_2atmpS1185.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2115 = _M0MPB13StringBuilder10to__string(_M0L3bufS199);
  moonbit_decref_cycle_free(_M0L3bufS199);
  return _result_2115;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS191,
  struct _M0TPB6Logger _M0L6loggerS189,
  int32_t _M0L5quoteS188
) {
  int32_t _M0L3endS1182;
  int32_t _M0L5startS1183;
  int32_t _M0L3lenS190;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS192;
  int32_t _M0L1iS193;
  int32_t _M0L3segS194;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS188) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS189.$0->$method_3(_M0L6loggerS189.$1, 34);
  }
  _M0L3endS1182 = _M0L4selfS191.$2;
  _M0L5startS1183 = _M0L4selfS191.$1;
  _M0L3lenS190 = _M0L3endS1182 - _M0L5startS1183;
  moonbit_incref_cycle_free(_M0L4selfS191.$0);
  if (_M0L6loggerS189.$1) {
    moonbit_incref(_M0L6loggerS189.$1);
  }
  _M0L6_2aenvS192
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS192)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 28, 0);
  _M0L6_2aenvS192->$0 = _M0L4selfS191;
  _M0L6_2aenvS192->$1 = _M0L6loggerS189;
  _M0L1iS193 = 0;
  _M0L3segS194 = 0;
  _2afor_195:;
  while (1) {
    moonbit_string_t _M0L3strS1179;
    int32_t _M0L5startS1181;
    int32_t _M0L6_2atmpS1180;
    int32_t _M0L4codeS196;
    int32_t _M0L1cS198;
    int32_t _M0L6_2atmpS1163;
    int32_t _M0L6_2atmpS1164;
    int32_t _M0L6_2atmpS1165;
    if (_M0L1iS193 >= _M0L3lenS190) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
      moonbit_decref_cycle_free(_M0L6_2aenvS192);
      break;
    }
    _M0L3strS1179 = _M0L4selfS191.$0;
    _M0L5startS1181 = _M0L4selfS191.$1;
    _M0L6_2atmpS1180 = _M0L5startS1181 + _M0L1iS193;
    _M0L4codeS196 = _M0L3strS1179[_M0L6_2atmpS1180];
    switch (_M0L4codeS196) {
      case 34: {
        _M0L1cS198 = _M0L4codeS196;
        goto join_197;
        break;
      }
      
      case 92: {
        _M0L1cS198 = _M0L4codeS196;
        goto join_197;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1166;
        int32_t _M0L6_2atmpS1167;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_28.data);
        _M0L6_2atmpS1166 = _M0L1iS193 + 1;
        _M0L6_2atmpS1167 = _M0L1iS193 + 1;
        _M0L1iS193 = _M0L6_2atmpS1166;
        _M0L3segS194 = _M0L6_2atmpS1167;
        goto _2afor_195;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1168;
        int32_t _M0L6_2atmpS1169;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_29.data);
        _M0L6_2atmpS1168 = _M0L1iS193 + 1;
        _M0L6_2atmpS1169 = _M0L1iS193 + 1;
        _M0L1iS193 = _M0L6_2atmpS1168;
        _M0L3segS194 = _M0L6_2atmpS1169;
        goto _2afor_195;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1170;
        int32_t _M0L6_2atmpS1171;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_30.data);
        _M0L6_2atmpS1170 = _M0L1iS193 + 1;
        _M0L6_2atmpS1171 = _M0L1iS193 + 1;
        _M0L1iS193 = _M0L6_2atmpS1170;
        _M0L3segS194 = _M0L6_2atmpS1171;
        goto _2afor_195;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1172;
        int32_t _M0L6_2atmpS1173;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_31.data);
        _M0L6_2atmpS1172 = _M0L1iS193 + 1;
        _M0L6_2atmpS1173 = _M0L1iS193 + 1;
        _M0L1iS193 = _M0L6_2atmpS1172;
        _M0L3segS194 = _M0L6_2atmpS1173;
        goto _2afor_195;
        break;
      }
      default: {
        if (_M0L4codeS196 < 32) {
          int32_t _M0L6_2atmpS1175;
          moonbit_string_t _M0L6_2atmpS1174;
          int32_t _M0L6_2atmpS1176;
          int32_t _M0L6_2atmpS1177;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_32.data);
          _M0L6_2atmpS1175 = _M0L4codeS196 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1174 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1175);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, _M0L6_2atmpS1174);
          moonbit_decref_cycle_free(_M0L6_2atmpS1174);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1176 = _M0L1iS193 + 1;
          _M0L6_2atmpS1177 = _M0L1iS193 + 1;
          _M0L1iS193 = _M0L6_2atmpS1176;
          _M0L3segS194 = _M0L6_2atmpS1177;
          goto _2afor_195;
        } else {
          int32_t _M0L6_2atmpS1178 = _M0L1iS193 + 1;
          int32_t _tmp_2118 = _M0L3segS194;
          _M0L1iS193 = _M0L6_2atmpS1178;
          _M0L3segS194 = _tmp_2118;
          goto _2afor_195;
        }
        break;
      }
    }
    goto joinlet_2117;
    join_197:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS189.$0->$method_3(_M0L6loggerS189.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1163 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS198);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS189.$0->$method_3(_M0L6loggerS189.$1, _M0L6_2atmpS1163);
    _M0L6_2atmpS1164 = _M0L1iS193 + 1;
    _M0L6_2atmpS1165 = _M0L1iS193 + 1;
    _M0L1iS193 = _M0L6_2atmpS1164;
    _M0L3segS194 = _M0L6_2atmpS1165;
    continue;
    joinlet_2117:;
    break;
  }
  if (_M0L5quoteS188) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS189.$0->$method_3(_M0L6loggerS189.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS184,
  int32_t _M0L3segS187,
  int32_t _M0L1iS186
) {
  struct _M0TPB6Logger _M0L6loggerS183;
  struct _M0TPC16string10StringView _M0L4selfS185;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS183 = _M0L6_2aenvS184->$1;
  _M0L4selfS185 = _M0L6_2aenvS184->$0;
  if (_M0L1iS186 > _M0L3segS187) {
    int64_t _M0L6_2atmpS1162 = (int64_t)_M0L1iS186;
    struct _M0TPC16string10StringView _M0L6_2atmpS1161;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1161
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS185, _M0L3segS187, _M0L6_2atmpS1162);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS183.$0->$method_2(_M0L6loggerS183.$1, _M0L6_2atmpS1161);
    moonbit_decref_cycle_free(_M0L6_2atmpS1161.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS174,
  int32_t _M0L5startS176,
  int64_t _M0L3endS178
) {
  int32_t _M0L3endS1159;
  int32_t _M0L5startS1160;
  int32_t _M0L3lenS173;
  int32_t _M0Lm2loS175;
  int32_t _M0Lm2hiS177;
  moonbit_string_t _M0L3strS181;
  int32_t _M0L4baseS182;
  int32_t _M0L6_2atmpS1137;
  int32_t _if__result_2119;
  int32_t _M0L6_2atmpS1147;
  int32_t _if__result_2120;
  int32_t _M0L6_2atmpS1149;
  int32_t _M0L6_2atmpS1150;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1159 = _M0L4selfS174.$2;
  _M0L5startS1160 = _M0L4selfS174.$1;
  _M0L3lenS173 = _M0L3endS1159 - _M0L5startS1160;
  if (_M0L5startS176 < 0) {
    _M0Lm2loS175 = 0;
  } else if (_M0L5startS176 > _M0L3lenS173) {
    _M0Lm2loS175 = _M0L3lenS173;
  } else {
    _M0Lm2loS175 = _M0L5startS176;
  }
  if (_M0L3endS178 == 4294967296ll) {
    _M0Lm2hiS177 = _M0L3lenS173;
  } else {
    int64_t _M0L7_2aSomeS179 = _M0L3endS178;
    int32_t _M0L4_2aeS180 = (int32_t)_M0L7_2aSomeS179;
    if (_M0L4_2aeS180 < 0) {
      _M0Lm2hiS177 = 0;
    } else if (_M0L4_2aeS180 > _M0L3lenS173) {
      _M0Lm2hiS177 = _M0L3lenS173;
    } else {
      _M0Lm2hiS177 = _M0L4_2aeS180;
    }
  }
  _M0L3strS181 = _M0L4selfS174.$0;
  _M0L4baseS182 = _M0L4selfS174.$1;
  _M0L6_2atmpS1137 = _M0Lm2loS175;
  if (_M0L6_2atmpS1137 > 0) {
    int32_t _M0L6_2atmpS1136 = _M0Lm2loS175;
    if (_M0L6_2atmpS1136 < _M0L3lenS173) {
      int32_t _M0L6_2atmpS1135 = _M0Lm2loS175;
      int32_t _M0L6_2atmpS1134 = _M0L4baseS182 + _M0L6_2atmpS1135;
      int32_t _M0L6_2atmpS1133 = _M0L3strS181[_M0L6_2atmpS1134];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1133)) {
        int32_t _M0L6_2atmpS1132 = _M0Lm2loS175;
        int32_t _M0L6_2atmpS1131 = _M0L4baseS182 + _M0L6_2atmpS1132;
        int32_t _M0L6_2atmpS1130 = _M0L6_2atmpS1131 - 1;
        int32_t _M0L6_2atmpS1129 = _M0L3strS181[_M0L6_2atmpS1130];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2119
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1129);
      } else {
        _if__result_2119 = 0;
      }
    } else {
      _if__result_2119 = 0;
    }
  } else {
    _if__result_2119 = 0;
  }
  if (_if__result_2119) {
    int32_t _M0L6_2atmpS1138 = _M0Lm2loS175;
    _M0Lm2loS175 = _M0L6_2atmpS1138 + 1;
  }
  _M0L6_2atmpS1147 = _M0Lm2hiS177;
  if (_M0L6_2atmpS1147 > 0) {
    int32_t _M0L6_2atmpS1146 = _M0Lm2hiS177;
    if (_M0L6_2atmpS1146 < _M0L3lenS173) {
      int32_t _M0L6_2atmpS1145 = _M0Lm2hiS177;
      int32_t _M0L6_2atmpS1144 = _M0L4baseS182 + _M0L6_2atmpS1145;
      int32_t _M0L6_2atmpS1143 = _M0L3strS181[_M0L6_2atmpS1144];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1143)) {
        int32_t _M0L6_2atmpS1142 = _M0Lm2hiS177;
        int32_t _M0L6_2atmpS1141 = _M0L4baseS182 + _M0L6_2atmpS1142;
        int32_t _M0L6_2atmpS1140 = _M0L6_2atmpS1141 - 1;
        int32_t _M0L6_2atmpS1139 = _M0L3strS181[_M0L6_2atmpS1140];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2120
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1139);
      } else {
        _if__result_2120 = 0;
      }
    } else {
      _if__result_2120 = 0;
    }
  } else {
    _if__result_2120 = 0;
  }
  if (_if__result_2120) {
    int32_t _M0L6_2atmpS1148 = _M0Lm2hiS177;
    _M0Lm2hiS177 = _M0L6_2atmpS1148 - 1;
  }
  _M0L6_2atmpS1149 = _M0Lm2loS175;
  _M0L6_2atmpS1150 = _M0Lm2hiS177;
  if (_M0L6_2atmpS1149 >= _M0L6_2atmpS1150) {
    int32_t _M0L6_2atmpS1154 = _M0Lm2loS175;
    int32_t _M0L6_2atmpS1151 = _M0L4baseS182 + _M0L6_2atmpS1154;
    int32_t _M0L6_2atmpS1153 = _M0Lm2loS175;
    int32_t _M0L6_2atmpS1152 = _M0L4baseS182 + _M0L6_2atmpS1153;
    moonbit_incref_cycle_free(_M0L3strS181);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS181,
                                                 .$1 = _M0L6_2atmpS1151,
                                                 .$2 = _M0L6_2atmpS1152};
  } else {
    int32_t _M0L6_2atmpS1158 = _M0Lm2loS175;
    int32_t _M0L6_2atmpS1155 = _M0L4baseS182 + _M0L6_2atmpS1158;
    int32_t _M0L6_2atmpS1157 = _M0Lm2hiS177;
    int32_t _M0L6_2atmpS1156 = _M0L4baseS182 + _M0L6_2atmpS1157;
    moonbit_incref_cycle_free(_M0L3strS181);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS181,
                                                 .$1 = _M0L6_2atmpS1155,
                                                 .$2 = _M0L6_2atmpS1156};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS172) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS171;
  int32_t _M0L6_2atmpS1126;
  int32_t _M0L6_2atmpS1125;
  int32_t _M0L6_2atmpS1128;
  int32_t _M0L6_2atmpS1127;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1124;
  moonbit_string_t _result_2121;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS171 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1126 = _M0IPC14byte4BytePB3Div3div(_M0L1bS172, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1125
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1126);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS171, _M0L6_2atmpS1125);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1128 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS172, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1127
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1128);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS171, _M0L6_2atmpS1127);
  _M0L6_2atmpS1124 = _M0L7_2aselfS171;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2121 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1124);
  moonbit_decref_cycle_free(_M0L6_2atmpS1124);
  return _result_2121;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS170) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS170 < 10) {
    int32_t _M0L6_2atmpS1121;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1121 = _M0IPC14byte4BytePB3Add3add(_M0L1iS170, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1121);
  } else {
    int32_t _M0L6_2atmpS1123;
    int32_t _M0L6_2atmpS1122;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1123 = _M0IPC14byte4BytePB3Add3add(_M0L1iS170, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1122 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1123, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1122);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS168,
  int32_t _M0L4thatS169
) {
  int32_t _M0L6_2atmpS1119;
  int32_t _M0L6_2atmpS1120;
  int32_t _M0L6_2atmpS1118;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1119 = (int32_t)_M0L4selfS168;
  _M0L6_2atmpS1120 = (int32_t)_M0L4thatS169;
  _M0L6_2atmpS1118 = _M0L6_2atmpS1119 - _M0L6_2atmpS1120;
  return _M0L6_2atmpS1118 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS166,
  int32_t _M0L4thatS167
) {
  int32_t _M0L6_2atmpS1116;
  int32_t _M0L6_2atmpS1117;
  int32_t _M0L6_2atmpS1115;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1116 = (int32_t)_M0L4selfS166;
  _M0L6_2atmpS1117 = (int32_t)_M0L4thatS167;
  _M0L6_2atmpS1115 = _M0L6_2atmpS1116 % _M0L6_2atmpS1117;
  return _M0L6_2atmpS1115 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS164,
  int32_t _M0L4thatS165
) {
  int32_t _M0L6_2atmpS1113;
  int32_t _M0L6_2atmpS1114;
  int32_t _M0L6_2atmpS1112;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1113 = (int32_t)_M0L4selfS164;
  _M0L6_2atmpS1114 = (int32_t)_M0L4thatS165;
  _M0L6_2atmpS1112 = _M0L6_2atmpS1113 / _M0L6_2atmpS1114;
  return _M0L6_2atmpS1112 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS162,
  int32_t _M0L4thatS163
) {
  int32_t _M0L6_2atmpS1110;
  int32_t _M0L6_2atmpS1111;
  int32_t _M0L6_2atmpS1109;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1110 = (int32_t)_M0L4selfS162;
  _M0L6_2atmpS1111 = (int32_t)_M0L4thatS163;
  _M0L6_2atmpS1109 = _M0L6_2atmpS1110 + _M0L6_2atmpS1111;
  return _M0L6_2atmpS1109 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS161) {
  int32_t _M0L6_2atmpS1108;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1108 = (int32_t)_M0L4selfS161;
  return _M0L6_2atmpS1108;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS160) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS160 >= 56320 && _M0L4selfS160 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS159) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS159 >= 55296 && _M0L4selfS159 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS158,
  moonbit_string_t _M0L3strS156
) {
  int32_t _M0L8str__lenS155;
  int32_t _M0L3lenS1107;
  int32_t _M0L8requiredS157;
  uint16_t* _M0L4dataS1102;
  int32_t _M0L6_2atmpS1101;
  int32_t _if__result_2122;
  uint16_t* _M0L4dataS1103;
  int32_t _M0L3lenS1104;
  int32_t _M0L3lenS1106;
  int32_t _M0L6_2atmpS1105;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS155 = Moonbit_array_length(_M0L3strS156);
  if (_M0L8str__lenS155 == 0) {
    return 0;
  }
  _M0L3lenS1107 = _M0L4selfS158->$1;
  _M0L8requiredS157 = _M0L3lenS1107 + _M0L8str__lenS155;
  _M0L4dataS1102 = _M0L4selfS158->$0;
  _M0L6_2atmpS1101 = Moonbit_array_length(_M0L4dataS1102);
  if (_M0L8requiredS157 > _M0L6_2atmpS1101) {
    _if__result_2122 = 1;
  } else {
    int32_t _M0L3lenS1100 = _M0L4selfS158->$1;
    _if__result_2122 = _M0L8requiredS157 < _M0L3lenS1100;
  }
  if (_if__result_2122) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS158, _M0L8requiredS157);
  }
  _M0L4dataS1103 = _M0L4selfS158->$0;
  _M0L3lenS1104 = _M0L4selfS158->$1;
  moonbit_incref_cycle_free(_M0L4dataS1103);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1103, _M0L3lenS1104, _M0L3strS156, 0, _M0L8str__lenS155);
  moonbit_decref_cycle_free(_M0L4dataS1103);
  _M0L3lenS1106 = _M0L4selfS158->$1;
  _M0L6_2atmpS1105 = _M0L3lenS1106 + _M0L8str__lenS155;
  _M0L4selfS158->$1 = _M0L6_2atmpS1105;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS151,
  int32_t _M0L11dst__offsetS154,
  moonbit_string_t _M0L3strS152,
  int32_t _M0L11str__offsetS147,
  int32_t _M0L3lenS148
) {
  int32_t _M0L16end__str__offsetS146;
  int32_t _M0L1iS149;
  int32_t _M0L1jS150;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS146 = _M0L11str__offsetS147 + _M0L3lenS148;
  _M0L1iS149 = _M0L11str__offsetS147;
  _M0L1jS150 = _M0L11dst__offsetS154;
  while (1) {
    if (_M0L1iS149 < _M0L16end__str__offsetS146) {
      int32_t _M0L6_2atmpS1097 = _M0L3strS152[_M0L1iS149];
      int32_t _M0L6_2atmpS1098;
      int32_t _M0L6_2atmpS1099;
      _M0L4selfS151[_M0L1jS150] = _M0L6_2atmpS1097;
      _M0L6_2atmpS1098 = _M0L1iS149 + 1;
      _M0L6_2atmpS1099 = _M0L1jS150 + 1;
      _M0L1iS149 = _M0L6_2atmpS1098;
      _M0L1jS150 = _M0L6_2atmpS1099;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS144,
  int32_t _M0L2chS143
) {
  uint32_t _M0L4codeS142;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS142 = _M0MPC14char4Char8to__uint(_M0L2chS143);
  if (_M0L4codeS142 <= 65535u) {
    int32_t _M0L3lenS1068 = _M0L4selfS144->$1;
    uint16_t* _M0L4dataS1070 = _M0L4selfS144->$0;
    int32_t _M0L6_2atmpS1069 = Moonbit_array_length(_M0L4dataS1070);
    uint16_t* _M0L4dataS1073;
    int32_t _M0L3lenS1074;
    int32_t _M0L6_2atmpS1075;
    int32_t _M0L3lenS1077;
    int32_t _M0L6_2atmpS1076;
    if (_M0L3lenS1068 >= _M0L6_2atmpS1069) {
      int32_t _M0L3lenS1072 = _M0L4selfS144->$1;
      int32_t _M0L6_2atmpS1071 = _M0L3lenS1072 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS144, _M0L6_2atmpS1071);
    }
    _M0L4dataS1073 = _M0L4selfS144->$0;
    _M0L3lenS1074 = _M0L4selfS144->$1;
    moonbit_incref_cycle_free(_M0L4dataS1073);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1075 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS142);
    if (
      _M0L3lenS1074 < 0
      || _M0L3lenS1074 >= Moonbit_array_length(_M0L4dataS1073)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1073[_M0L3lenS1074] = _M0L6_2atmpS1075;
    moonbit_decref_cycle_free(_M0L4dataS1073);
    _M0L3lenS1077 = _M0L4selfS144->$1;
    _M0L6_2atmpS1076 = _M0L3lenS1077 + 1;
    _M0L4selfS144->$1 = _M0L6_2atmpS1076;
  } else if (_M0L4codeS142 <= 1114111u) {
    uint16_t* _M0L4dataS1081 = _M0L4selfS144->$0;
    int32_t _M0L6_2atmpS1079 = Moonbit_array_length(_M0L4dataS1081);
    int32_t _M0L3lenS1080 = _M0L4selfS144->$1;
    int32_t _M0L6_2atmpS1078 = _M0L6_2atmpS1079 - _M0L3lenS1080;
    uint32_t _M0L4codeS145;
    uint16_t* _M0L4dataS1084;
    int32_t _M0L3lenS1085;
    uint32_t _M0L6_2atmpS1088;
    uint32_t _M0L6_2atmpS1087;
    int32_t _M0L6_2atmpS1086;
    uint16_t* _M0L4dataS1089;
    int32_t _M0L3lenS1094;
    int32_t _M0L6_2atmpS1090;
    uint32_t _M0L6_2atmpS1093;
    uint32_t _M0L6_2atmpS1092;
    int32_t _M0L6_2atmpS1091;
    int32_t _M0L3lenS1096;
    int32_t _M0L6_2atmpS1095;
    if (_M0L6_2atmpS1078 < 2) {
      int32_t _M0L3lenS1083 = _M0L4selfS144->$1;
      int32_t _M0L6_2atmpS1082 = _M0L3lenS1083 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS144, _M0L6_2atmpS1082);
    }
    _M0L4codeS145 = _M0L4codeS142 - 65536u;
    _M0L4dataS1084 = _M0L4selfS144->$0;
    _M0L3lenS1085 = _M0L4selfS144->$1;
    _M0L6_2atmpS1088 = _M0L4codeS145 >> 10;
    _M0L6_2atmpS1087 = 55296u + _M0L6_2atmpS1088;
    moonbit_incref_cycle_free(_M0L4dataS1084);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1086 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1087);
    if (
      _M0L3lenS1085 < 0
      || _M0L3lenS1085 >= Moonbit_array_length(_M0L4dataS1084)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1084[_M0L3lenS1085] = _M0L6_2atmpS1086;
    moonbit_decref_cycle_free(_M0L4dataS1084);
    _M0L4dataS1089 = _M0L4selfS144->$0;
    _M0L3lenS1094 = _M0L4selfS144->$1;
    _M0L6_2atmpS1090 = _M0L3lenS1094 + 1;
    _M0L6_2atmpS1093 = _M0L4codeS145 & 1023u;
    _M0L6_2atmpS1092 = 56320u + _M0L6_2atmpS1093;
    moonbit_incref_cycle_free(_M0L4dataS1089);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1091 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1092);
    if (
      _M0L6_2atmpS1090 < 0
      || _M0L6_2atmpS1090 >= Moonbit_array_length(_M0L4dataS1089)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1089[_M0L6_2atmpS1090] = _M0L6_2atmpS1091;
    moonbit_decref_cycle_free(_M0L4dataS1089);
    _M0L3lenS1096 = _M0L4selfS144->$1;
    _M0L6_2atmpS1095 = _M0L3lenS1096 + 2;
    _M0L4selfS144->$1 = _M0L6_2atmpS1095;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_33.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS139,
  int32_t _M0L8requiredS140
) {
  uint16_t* _M0L4dataS1067;
  int32_t _M0L6_2atmpS1065;
  int32_t _M0L3lenS1066;
  int32_t _M0L13new__capacityS138;
  uint16_t* _M0L4dataS1062;
  int32_t _M0L6_2atmpS1063;
  int32_t _M0L3lenS1064;
  uint16_t* _M0L9new__dataS141;
  uint16_t* _M0L6_2aoldS2016;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1067 = _M0L4selfS139->$0;
  _M0L6_2atmpS1065 = Moonbit_array_length(_M0L4dataS1067);
  _M0L3lenS1066 = _M0L4selfS139->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS138
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1065, _M0L3lenS1066, _M0L8requiredS140);
  _M0L4dataS1062 = _M0L4selfS139->$0;
  moonbit_incref_cycle_free(_M0L4dataS1062);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1063 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1064 = _M0L4selfS139->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS141
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1062, _M0L13new__capacityS138, _M0L6_2atmpS1063, _M0L3lenS1064, 0, 0);
  _M0L6_2aoldS2016 = _M0L4selfS139->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2016);
  _M0L4selfS139->$0 = _M0L9new__dataS141;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS137,
  int32_t _M0L3lenS133,
  int32_t _M0L8requiredS132
) {
  int32_t _M0L5spaceS134;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS132 < _M0L3lenS133) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_34.data);
  }
  _M0L5spaceS134 = _M0L7currentS137;
  while (1) {
    if (_M0L5spaceS134 < _M0L8requiredS132) {
      int32_t _M0L4nextS135 = _M0L5spaceS134 * 2;
      if (_M0L4nextS135 <= _M0L5spaceS134) {
        return _M0L8requiredS132;
      }
      _M0L5spaceS134 = _M0L4nextS135;
      continue;
    } else {
      return _M0L5spaceS134;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS131) {
  int32_t _M0L6_2atmpS1061;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1061 = *(int32_t*)&_M0L4selfS131;
  return (uint16_t)_M0L6_2atmpS1061;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS130) {
  int32_t _M0L6_2atmpS1060;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1060 = _M0L4selfS130;
  return *(uint32_t*)&_M0L6_2atmpS1060;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS128
) {
  int32_t _M0L3lenS1051;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1051 = _M0L4selfS128->$1;
  if (_M0L3lenS1051 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1052 = _M0L4selfS128->$1;
    uint16_t* _M0L4dataS1054 = _M0L4selfS128->$0;
    int32_t _M0L6_2atmpS1053 = Moonbit_array_length(_M0L4dataS1054);
    if (_M0L3lenS1052 == _M0L6_2atmpS1053) {
      uint16_t* _M0L4dataS1055 = _M0L4selfS128->$0;
      moonbit_incref_cycle_free(_M0L4dataS1055);
      return _M0L4dataS1055;
    } else {
      uint16_t* _M0L4dataS1056 = _M0L4selfS128->$0;
      int32_t _M0L3lenS1057 = _M0L4selfS128->$1;
      int32_t _M0L6_2atmpS1058;
      int32_t _M0L3lenS1059;
      uint16_t* _M0L4dataS129;
      moonbit_incref_cycle_free(_M0L4dataS1056);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1058 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1059 = _M0L4selfS128->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS129
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1056, _M0L3lenS1057, _M0L6_2atmpS1058, _M0L3lenS1059, 0, 0);
      return _M0L4dataS129;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS125,
  int32_t _M0L13allocate__lenS121,
  int32_t _M0L4initS126,
  int32_t _M0L3lenS122,
  int32_t _M0L11src__offsetS123,
  int32_t _M0L11dst__offsetS124
) {
  int32_t _if__result_2125;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS121 >= 0) {
    if (_M0L3lenS122 >= 0) {
      if (_M0L11src__offsetS123 >= 0) {
        if (_M0L11dst__offsetS124 >= 0) {
          int32_t _M0L6_2atmpS1047 = _M0L11src__offsetS123 + _M0L3lenS122;
          int32_t _M0L6_2atmpS1048 = Moonbit_array_length(_M0L3srcS125);
          if (_M0L6_2atmpS1047 <= _M0L6_2atmpS1048) {
            int32_t _M0L6_2atmpS1046 = _M0L11dst__offsetS124 + _M0L3lenS122;
            _if__result_2125 = _M0L6_2atmpS1046 <= _M0L13allocate__lenS121;
          } else {
            _if__result_2125 = 0;
          }
        } else {
          _if__result_2125 = 0;
        }
      } else {
        _if__result_2125 = 0;
      }
    } else {
      _if__result_2125 = 0;
    }
  } else {
    _if__result_2125 = 0;
  }
  if (_if__result_2125) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS125, _M0L13allocate__lenS121, _M0L4initS126, _M0L11src__offsetS123, _M0L11dst__offsetS124, _M0L3lenS122);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS127;
    int32_t _M0L6_2atmpS1050;
    moonbit_string_t _M0L6_2atmpS1049;
    uint16_t* _result_2126;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS127
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L13allocate__lenS121);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11src__offsetS123);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11dst__offsetS124);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L3lenS122);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_39.data);
    _M0L6_2atmpS1050 = Moonbit_array_length(_M0L3srcS125);
    moonbit_decref_cycle_free(_M0L3srcS125);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L6_2atmpS1050);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1049
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS127);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS127);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2126 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1049);
    moonbit_decref_cycle_free(_M0L6_2atmpS1049);
    return _result_2126;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS118,
  int32_t _M0L13allocate__lenS115,
  int32_t _M0L4initS116,
  int32_t _M0L11src__offsetS119,
  int32_t _M0L11dst__offsetS117,
  int32_t _M0L9blit__lenS120
) {
  uint16_t* _M0L3dstS114;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS114
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS115, _M0L4initS116);
  moonbit_incref_cycle_free(_M0L3dstS114);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS114, _M0L11dst__offsetS117, _M0L3srcS118, _M0L11src__offsetS119, _M0L9blit__lenS120, sizeof(uint16_t));
  return _M0L3dstS114;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS112
) {
  int32_t _M0L7initialS111;
  uint16_t* _M0L4dataS113;
  struct _M0TPB13StringBuilder* _block_2127;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS112 < 1) {
    _M0L7initialS111 = 1;
  } else {
    int32_t _M0L6_2atmpS1045 = _M0L10size__hintS112 + 1;
    _M0L7initialS111 = _M0L6_2atmpS1045 / 2;
  }
  _M0L4dataS113 = (uint16_t*)moonbit_make_string(_M0L7initialS111, 0);
  _block_2127
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2127)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
  _block_2127->$0 = _M0L4dataS113;
  _block_2127->$1 = 0;
  return _block_2127;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS110) {
  int32_t _M0L6_2atmpS1044;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1044 = (int32_t)_M0L4selfS110;
  return _M0L6_2atmpS1044;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS96,
  int32_t _M0L13allocate__lenS92,
  int32_t _M0L3lenS93,
  int32_t _M0L11src__offsetS94,
  int32_t _M0L11dst__offsetS95
) {
  int32_t _if__result_2128;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS92 >= 0) {
    if (_M0L3lenS93 >= 0) {
      if (_M0L11src__offsetS94 >= 0) {
        if (_M0L11dst__offsetS95 >= 0) {
          int32_t _M0L6_2atmpS1030 = _M0L11src__offsetS94 + _M0L3lenS93;
          int32_t _M0L6_2atmpS1031;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1031
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS96);
          if (_M0L6_2atmpS1030 <= _M0L6_2atmpS1031) {
            int32_t _M0L6_2atmpS1029 = _M0L11dst__offsetS95 + _M0L3lenS93;
            _if__result_2128 = _M0L6_2atmpS1029 <= _M0L13allocate__lenS92;
          } else {
            _if__result_2128 = 0;
          }
        } else {
          _if__result_2128 = 0;
        }
      } else {
        _if__result_2128 = 0;
      }
    } else {
      _if__result_2128 = 0;
    }
  } else {
    _if__result_2128 = 0;
  }
  if (_if__result_2128) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS96, _M0L13allocate__lenS92, _M0L11src__offsetS94, _M0L11dst__offsetS95, _M0L3lenS93);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS97;
    int32_t _M0L6_2atmpS1033;
    moonbit_string_t _M0L6_2atmpS1032;
    float* _result_2129;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS97
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L13allocate__lenS92);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L11src__offsetS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L11dst__offsetS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L3lenS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1033 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS96);
    moonbit_decref_cycle_free(_M0L3srcS96);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L6_2atmpS1033);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1032
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS97);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS97);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2129
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1032);
    moonbit_decref_cycle_free(_M0L6_2atmpS1032);
    return _result_2129;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS102,
  int32_t _M0L13allocate__lenS98,
  int32_t _M0L3lenS99,
  int32_t _M0L11src__offsetS100,
  int32_t _M0L11dst__offsetS101
) {
  int32_t _if__result_2130;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS98 >= 0) {
    if (_M0L3lenS99 >= 0) {
      if (_M0L11src__offsetS100 >= 0) {
        if (_M0L11dst__offsetS101 >= 0) {
          int32_t _M0L6_2atmpS1035 = _M0L11src__offsetS100 + _M0L3lenS99;
          int32_t _M0L6_2atmpS1036;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1036
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS102);
          if (_M0L6_2atmpS1035 <= _M0L6_2atmpS1036) {
            int32_t _M0L6_2atmpS1034 = _M0L11dst__offsetS101 + _M0L3lenS99;
            _if__result_2130 = _M0L6_2atmpS1034 <= _M0L13allocate__lenS98;
          } else {
            _if__result_2130 = 0;
          }
        } else {
          _if__result_2130 = 0;
        }
      } else {
        _if__result_2130 = 0;
      }
    } else {
      _if__result_2130 = 0;
    }
  } else {
    _if__result_2130 = 0;
  }
  if (_if__result_2130) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS98, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS102, _M0L11src__offsetS100, _M0L11dst__offsetS101, _M0L3lenS99);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS103;
    int32_t _M0L6_2atmpS1038;
    moonbit_string_t _M0L6_2atmpS1037;
    moonbit_string_t* _result_2131;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS103
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L13allocate__lenS98);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L11src__offsetS100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L11dst__offsetS101);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L3lenS99);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1038 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS102);
    moonbit_decref_cycle_free(_M0L3srcS102);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L6_2atmpS1038);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1037
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS103);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS103);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2131
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1037);
    moonbit_decref_cycle_free(_M0L6_2atmpS1037);
    return _result_2131;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS108,
  int32_t _M0L13allocate__lenS104,
  int32_t _M0L3lenS105,
  int32_t _M0L11src__offsetS106,
  int32_t _M0L11dst__offsetS107
) {
  int32_t _if__result_2132;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS104 >= 0) {
    if (_M0L3lenS105 >= 0) {
      if (_M0L11src__offsetS106 >= 0) {
        if (_M0L11dst__offsetS107 >= 0) {
          int32_t _M0L6_2atmpS1040 = _M0L11src__offsetS106 + _M0L3lenS105;
          int32_t _M0L6_2atmpS1041;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1041
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS108);
          if (_M0L6_2atmpS1040 <= _M0L6_2atmpS1041) {
            int32_t _M0L6_2atmpS1039 = _M0L11dst__offsetS107 + _M0L3lenS105;
            _if__result_2132 = _M0L6_2atmpS1039 <= _M0L13allocate__lenS104;
          } else {
            _if__result_2132 = 0;
          }
        } else {
          _if__result_2132 = 0;
        }
      } else {
        _if__result_2132 = 0;
      }
    } else {
      _if__result_2132 = 0;
    }
  } else {
    _if__result_2132 = 0;
  }
  if (_if__result_2132) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS104, 0, _M0L3srcS108, _M0L11src__offsetS106, _M0L11dst__offsetS107, _M0L3lenS105);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS109;
    int32_t _M0L6_2atmpS1043;
    moonbit_string_t _M0L6_2atmpS1042;
    struct _M0TUsiE** _result_2133;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS109
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L13allocate__lenS104);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L11src__offsetS106);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L11dst__offsetS107);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L3lenS105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1043 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS108);
    moonbit_decref_cycle_free(_M0L3srcS108);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L6_2atmpS1043);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1042
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS109);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS109);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2133
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1042);
    moonbit_decref_cycle_free(_M0L6_2atmpS1042);
    return _result_2133;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS87,
  moonbit_string_t _M0L3objS86
) {
  struct _M0TPB6Logger _M0L6_2atmpS1026;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS87);
  _M0L6_2atmpS1026
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS87
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS86, _M0L6_2atmpS1026);
  if (_M0L6_2atmpS1026.$1) {
    moonbit_decref(_M0L6_2atmpS1026.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS89,
  int32_t _M0L3objS88
) {
  struct _M0TPB6Logger _M0L6_2atmpS1027;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS89);
  _M0L6_2atmpS1027
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS89
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS88, _M0L6_2atmpS1027);
  if (_M0L6_2atmpS1027.$1) {
    moonbit_decref(_M0L6_2atmpS1027.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS91,
  uint64_t _M0L3objS90
) {
  struct _M0TPB6Logger _M0L6_2atmpS1028;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS91);
  _M0L6_2atmpS1028
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS91
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS90, _M0L6_2atmpS1028);
  if (_M0L6_2atmpS1028.$1) {
    moonbit_decref(_M0L6_2atmpS1028.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS71,
  int32_t _M0L13allocate__lenS69,
  int32_t _M0L11src__offsetS72,
  int32_t _M0L11dst__offsetS70,
  int32_t _M0L9blit__lenS73
) {
  float* _M0L3dstS68;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS68 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS69);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS68, _M0L11dst__offsetS70, _M0L3srcS71, _M0L11src__offsetS72, _M0L9blit__lenS73);
  moonbit_decref_cycle_free(_M0L3srcS71);
  return _M0L3dstS68;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS77,
  int32_t _M0L13allocate__lenS75,
  int32_t _M0L11src__offsetS78,
  int32_t _M0L11dst__offsetS76,
  int32_t _M0L9blit__lenS79
) {
  moonbit_string_t* _M0L3dstS74;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS74
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS75, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS74, _M0L11dst__offsetS76, _M0L3srcS77, _M0L11src__offsetS78, _M0L9blit__lenS79);
  moonbit_decref_cycle_free(_M0L3srcS77);
  return _M0L3dstS74;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS83,
  int32_t _M0L13allocate__lenS81,
  int32_t _M0L11src__offsetS84,
  int32_t _M0L11dst__offsetS82,
  int32_t _M0L9blit__lenS85
) {
  struct _M0TUsiE** _M0L3dstS80;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS80
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS81, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS80, _M0L11dst__offsetS82, _M0L3srcS83, _M0L11src__offsetS84, _M0L9blit__lenS85);
  moonbit_decref_cycle_free(_M0L3srcS83);
  return _M0L3dstS80;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS53,
  int32_t _M0L11dst__offsetS54,
  float* _M0L3srcS55,
  int32_t _M0L11src__offsetS56,
  int32_t _M0L3lenS57
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS55);
  moonbit_incref_cycle_free(_M0L3dstS53);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS53, _M0L11dst__offsetS54, _M0L3srcS55, _M0L11src__offsetS56, _M0L3lenS57, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS58,
  int32_t _M0L11dst__offsetS59,
  moonbit_string_t* _M0L3srcS60,
  int32_t _M0L11src__offsetS61,
  int32_t _M0L3lenS62
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS60);
  moonbit_incref_cycle_free(_M0L3dstS58);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS58, _M0L11dst__offsetS59, _M0L3srcS60, _M0L11src__offsetS61, _M0L3lenS62);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS63,
  int32_t _M0L11dst__offsetS64,
  struct _M0TUsiE** _M0L3srcS65,
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
        int32_t _M0L6_2atmpS990 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS992 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS991;
        int32_t _M0L6_2atmpS993;
        if (
          _M0L6_2atmpS992 < 0
          || _M0L6_2atmpS992 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS991 = (int32_t)_M0L3srcS18[_M0L6_2atmpS992];
        if (
          _M0L6_2atmpS990 < 0
          || _M0L6_2atmpS990 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS990] = _M0L6_2atmpS991;
        _M0L6_2atmpS993 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS993;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS998 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS998;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS994 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS996 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS995;
        int32_t _M0L6_2atmpS997;
        if (
          _M0L6_2atmpS996 < 0
          || _M0L6_2atmpS996 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS995 = (int32_t)_M0L3srcS18[_M0L6_2atmpS996];
        if (
          _M0L6_2atmpS994 < 0
          || _M0L6_2atmpS994 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS994] = _M0L6_2atmpS995;
        _M0L6_2atmpS997 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS997;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
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
        int32_t _M0L6_2atmpS999 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1001 = _M0L11src__offsetS29 + _M0L1iS30;
        float _M0L6_2atmpS1000;
        int32_t _M0L6_2atmpS1002;
        if (
          _M0L6_2atmpS1001 < 0
          || _M0L6_2atmpS1001 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1000 = (float)_M0L3srcS27[_M0L6_2atmpS1001];
        if (
          _M0L6_2atmpS999 < 0
          || _M0L6_2atmpS999 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS999] = _M0L6_2atmpS1000;
        _M0L6_2atmpS1002 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1002;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1007 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1007;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1003 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1005 = _M0L11src__offsetS29 + _M0L1iS33;
        float _M0L6_2atmpS1004;
        int32_t _M0L6_2atmpS1006;
        if (
          _M0L6_2atmpS1005 < 0
          || _M0L6_2atmpS1005 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1004 = (float)_M0L3srcS27[_M0L6_2atmpS1005];
        if (
          _M0L6_2atmpS1003 < 0
          || _M0L6_2atmpS1003 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1003] = _M0L6_2atmpS1004;
        _M0L6_2atmpS1006 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1006;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS35,
  int32_t _M0L11dst__offsetS37,
  moonbit_string_t* _M0L3srcS36,
  int32_t _M0L11src__offsetS38,
  int32_t _M0L3lenS40
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS35 == _M0L3srcS36 && _M0L11dst__offsetS37 < _M0L11src__offsetS38
  ) {
    int32_t _M0L1iS39 = 0;
    while (1) {
      if (_M0L1iS39 < _M0L3lenS40) {
        int32_t _M0L6_2atmpS1008 = _M0L11dst__offsetS37 + _M0L1iS39;
        int32_t _M0L6_2atmpS1010 = _M0L11src__offsetS38 + _M0L1iS39;
        moonbit_string_t _M0L6_2atmpS1009;
        moonbit_string_t _M0L6_2aoldS2017;
        int32_t _M0L6_2atmpS1011;
        if (
          _M0L6_2atmpS1010 < 0
          || _M0L6_2atmpS1010 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1009 = (moonbit_string_t)_M0L3srcS36[_M0L6_2atmpS1010];
        if (
          _M0L6_2atmpS1008 < 0
          || _M0L6_2atmpS1008 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2017 = (moonbit_string_t)_M0L3dstS35[_M0L6_2atmpS1008];
        moonbit_incref_cycle_free(_M0L6_2atmpS1009);
        moonbit_decref_cycle_free(_M0L6_2aoldS2017);
        _M0L3dstS35[_M0L6_2atmpS1008] = _M0L6_2atmpS1009;
        _M0L6_2atmpS1011 = _M0L1iS39 + 1;
        _M0L1iS39 = _M0L6_2atmpS1011;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1016 = _M0L3lenS40 - 1;
    int32_t _M0L1iS42 = _M0L6_2atmpS1016;
    while (1) {
      if (_M0L1iS42 >= 0) {
        int32_t _M0L6_2atmpS1012 = _M0L11dst__offsetS37 + _M0L1iS42;
        int32_t _M0L6_2atmpS1014 = _M0L11src__offsetS38 + _M0L1iS42;
        moonbit_string_t _M0L6_2atmpS1013;
        moonbit_string_t _M0L6_2aoldS2018;
        int32_t _M0L6_2atmpS1015;
        if (
          _M0L6_2atmpS1014 < 0
          || _M0L6_2atmpS1014 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1013 = (moonbit_string_t)_M0L3srcS36[_M0L6_2atmpS1014];
        if (
          _M0L6_2atmpS1012 < 0
          || _M0L6_2atmpS1012 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2018 = (moonbit_string_t)_M0L3dstS35[_M0L6_2atmpS1012];
        moonbit_incref_cycle_free(_M0L6_2atmpS1013);
        moonbit_decref_cycle_free(_M0L6_2aoldS2018);
        _M0L3dstS35[_M0L6_2atmpS1012] = _M0L6_2atmpS1013;
        _M0L6_2atmpS1015 = _M0L1iS42 - 1;
        _M0L1iS42 = _M0L6_2atmpS1015;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS44,
  int32_t _M0L11dst__offsetS46,
  struct _M0TUsiE** _M0L3srcS45,
  int32_t _M0L11src__offsetS47,
  int32_t _M0L3lenS49
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS44 == _M0L3srcS45 && _M0L11dst__offsetS46 < _M0L11src__offsetS47
  ) {
    int32_t _M0L1iS48 = 0;
    while (1) {
      if (_M0L1iS48 < _M0L3lenS49) {
        int32_t _M0L6_2atmpS1017 = _M0L11dst__offsetS46 + _M0L1iS48;
        int32_t _M0L6_2atmpS1019 = _M0L11src__offsetS47 + _M0L1iS48;
        struct _M0TUsiE* _M0L6_2atmpS1018;
        struct _M0TUsiE* _M0L6_2aoldS2019;
        int32_t _M0L6_2atmpS1020;
        if (
          _M0L6_2atmpS1019 < 0
          || _M0L6_2atmpS1019 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1018 = (struct _M0TUsiE*)_M0L3srcS45[_M0L6_2atmpS1019];
        if (
          _M0L6_2atmpS1017 < 0
          || _M0L6_2atmpS1017 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2019 = (struct _M0TUsiE*)_M0L3dstS44[_M0L6_2atmpS1017];
        if (_M0L6_2atmpS1018) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1018);
        }
        if (_M0L6_2aoldS2019) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2019);
        }
        _M0L3dstS44[_M0L6_2atmpS1017] = _M0L6_2atmpS1018;
        _M0L6_2atmpS1020 = _M0L1iS48 + 1;
        _M0L1iS48 = _M0L6_2atmpS1020;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1025 = _M0L3lenS49 - 1;
    int32_t _M0L1iS51 = _M0L6_2atmpS1025;
    while (1) {
      if (_M0L1iS51 >= 0) {
        int32_t _M0L6_2atmpS1021 = _M0L11dst__offsetS46 + _M0L1iS51;
        int32_t _M0L6_2atmpS1023 = _M0L11src__offsetS47 + _M0L1iS51;
        struct _M0TUsiE* _M0L6_2atmpS1022;
        struct _M0TUsiE* _M0L6_2aoldS2020;
        int32_t _M0L6_2atmpS1024;
        if (
          _M0L6_2atmpS1023 < 0
          || _M0L6_2atmpS1023 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1022 = (struct _M0TUsiE*)_M0L3srcS45[_M0L6_2atmpS1023];
        if (
          _M0L6_2atmpS1021 < 0
          || _M0L6_2atmpS1021 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2020 = (struct _M0TUsiE*)_M0L3dstS44[_M0L6_2atmpS1021];
        if (_M0L6_2atmpS1022) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1022);
        }
        if (_M0L6_2aoldS2020) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2020);
        }
        _M0L3dstS44[_M0L6_2atmpS1021] = _M0L6_2atmpS1022;
        _M0L6_2atmpS1024 = _M0L1iS51 - 1;
        _M0L1iS51 = _M0L6_2atmpS1024;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS14) {
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_40.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_41.data);
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

moonbit_string_t _M0FPC15abort5abortGsE(moonbit_string_t _M0L3msgS1) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS1);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0FPC15abort5abortGuE(moonbit_string_t _M0L3msgS2) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS2);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
  return 0;
}

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t _M0L3msgS3) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS4
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS4);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t _M0L3msgS5
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS959) {
  switch (Moonbit_object_tag(_M0L4_2aeS959)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_42.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS959);
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_43.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_44.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_45.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS985,
  struct _M0TPB4Show _M0L8_2aparamS984
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS983 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS985;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS983, _M0L8_2aparamS984);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS982,
  struct _M0TPB4Show _M0L8_2aparamS981
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS980 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS982;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS980, _M0L8_2aparamS981);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS979,
  int32_t _M0L8_2aparamS978
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS977 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS979;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS977, _M0L8_2aparamS978);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS976,
  struct _M0TPC16string10StringView _M0L8_2aparamS975
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS974 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS976;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS974, _M0L8_2aparamS975);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS973,
  moonbit_string_t _M0L8_2aparamS970,
  int32_t _M0L8_2aparamS971,
  int32_t _M0L8_2aparamS972
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS969 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS973;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS969, _M0L8_2aparamS970, _M0L8_2aparamS971, _M0L8_2aparamS972);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS968,
  moonbit_string_t _M0L8_2aparamS967
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS966 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS968;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS966, _M0L8_2aparamS967);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS989;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS952;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS953;
  int32_t _M0L7_2abindS954;
  struct _M0TUsiE** _M0L7_2abindS955;
  int32_t _M0L6_2acntS2025;
  int32_t _M0L2__S956;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS989
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS952
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS952)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _M0L12async__testsS952->$0 = _M0L6_2atmpS989;
  _M0L12async__testsS952->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS953
  = _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS954 = _M0L7_2abindS953->$1;
  _M0L7_2abindS955 = _M0L7_2abindS953->$0;
  _M0L6_2acntS2025
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS953));
  if (_M0L6_2acntS2025 > 1) {
    int32_t _M0L11_2anew__cntS2026 = _M0L6_2acntS2025 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS953), _M0L11_2anew__cntS2026);
    moonbit_incref_cycle_free(_M0L7_2abindS955);
  } else if (_M0L6_2acntS2025 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS953);
  }
  _M0L2__S956 = 0;
  while (1) {
    if (_M0L2__S956 < _M0L7_2abindS954) {
      struct _M0TUsiE* _M0L3argS957 =
        (struct _M0TUsiE*)_M0L7_2abindS955[_M0L2__S956];
      moonbit_string_t _M0L6_2atmpS986 = _M0L3argS957->$0;
      int32_t _M0L6_2atmpS987 = _M0L3argS957->$1;
      int32_t _M0L6_2atmpS988;
      moonbit_incref_cycle_free(_M0L6_2atmpS986);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS952, _M0L6_2atmpS986, _M0L6_2atmpS987);
      moonbit_decref_cycle_free(_M0L6_2atmpS986);
      _M0L6_2atmpS988 = _M0L2__S956 + 1;
      _M0L2__S956 = _M0L6_2atmpS988;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS955);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\calcium_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31calcium__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS952);
  moonbit_decref_cycle_free(_M0L12async__testsS952);
  moonbit_flush_cycles();
  return 0;
}