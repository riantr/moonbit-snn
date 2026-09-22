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
struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE;

struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples21chain__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt7Monitor;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TP26RiantR8snn__mbt5Model;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples21chain__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB4Show;

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTPC15error5Error122RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TPB8MutLocalGbE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

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

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** $0;
  int32_t $1;
  
};

struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples21chain__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
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

struct _M0TP26RiantR8snn__mbt7Monitor {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  moonbit_string_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  int32_t $4;
  int32_t $5;
  int32_t $6;
  
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

struct _M0TP26RiantR8snn__mbt5Model {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* $0;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* $1;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* $2;
  
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

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples21chain__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error122RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0TPB8MutLocalGbE {
  int32_t $0;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE {
  struct _M0TP26RiantR8snn__mbt2IF** $0;
  int32_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1129(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1122(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1117(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1094(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1087(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples21chain__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  int32_t,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse3new(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t
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

int32_t _M0FP26RiantR8snn__mbt8sim__for(
  struct _M0TP26RiantR8snn__mbt5Model*,
  float
);

int32_t _M0FP26RiantR8snn__mbt11step__model(
  struct _M0TP26RiantR8snn__mbt5Model*,
  struct _M0TP26RiantR8snn__mbt4Time*
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

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model*
);

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor*,
  float
);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor6new__v(
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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  int32_t,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(
  int32_t,
  int32_t
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

float _M0FP26RiantR8snn__mbt9get__time(struct _M0TP26RiantR8snn__mbt4Time*);

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new();

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

int32_t _M0MP26RiantR8snn__mbt7Monitor13dump__summary(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

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

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*,
  int32_t
);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*
);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

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
} const moonbit_string_literal_32 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_30 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_46 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_29 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 109, 
    101, 97, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[110]; 
} const moonbit_string_literal_45 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 109, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 104, 97, 105, 110, 95, 98, 108, 
    97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 
    114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 
    115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 93, 32, 
    40, 101, 109, 112, 116, 121, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 109, 
    105, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_33 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 109, 
    97, 120, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_42 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 77, 111, 
    110, 105, 116, 111, 114, 91, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_31 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[43]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 42, 105, 110, 
    100, 101, 120, 32, 111, 117, 116, 32, 111, 102, 32, 98, 111, 117, 
    110, 100, 115, 58, 32, 116, 104, 101, 32, 108, 101, 110, 32, 105, 
    115, 32, 102, 114, 111, 109, 32, 48, 32, 116, 111, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_44 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_40 =
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
} const moonbit_string_literal_37 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[112]; 
} const moonbit_string_literal_43 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 111, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 104, 97, 105, 110, 95, 98, 108, 
    97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_26 =
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
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 98, 
    117, 116, 32, 116, 104, 101, 32, 105, 110, 100, 101, 120, 32, 105, 
    115, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 93, 32, 
    110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_41 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1129$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1129
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[90] =
  {
    sizeof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117)
    / 4, 1,
    offsetof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117, $1)
    / 4
    * 2,
    sizeof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122)
    / 4, 1,
    offsetof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2508
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1150,
  moonbit_string_t _M0L8filenameS1119,
  int32_t _M0L5indexS1121
) {
  struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117* _closure_2539;
  struct _M0TWEu* _M0L13handle__startS1117;
  struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122* _closure_2540;
  struct _M0TWssbEu* _M0L14handle__resultS1122;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1129;
  void* _M0L11_2atry__errS1144;
  struct moonbit_result_0 _tmp_2542;
  int32_t _handle__error__result_2543;
  int32_t _M0L6_2atmpS2496;
  void* _M0L3errS1145;
  moonbit_string_t _M0L4nameS1147;
  struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1148;
  moonbit_string_t _M0L7_2anameS1149;
  int32_t _M0L6_2acntS2533;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1119);
  _closure_2539
  = (struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117*)moonbit_malloc(sizeof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117));
  Moonbit_object_header(_closure_2539)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2539->code
  = &_M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1117;
  _closure_2539->$0 = _M0L5indexS1121;
  _closure_2539->$1 = _M0L8filenameS1119;
  _M0L13handle__startS1117 = (struct _M0TWEu*)_closure_2539;
  moonbit_incref_cycle_free(_M0L8filenameS1119);
  _closure_2540
  = (struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122*)moonbit_malloc(sizeof(struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122));
  Moonbit_object_header(_closure_2540)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2540->code
  = &_M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1122;
  _closure_2540->$0 = _M0L5indexS1121;
  _closure_2540->$1 = _M0L8filenameS1119;
  _M0L14handle__resultS1122 = (struct _M0TWssbEu*)_closure_2540;
  _M0L17error__to__stringS1129
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1129$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2542
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1150, _M0L8filenameS1119, _M0L5indexS1121, _M0L13handle__startS1117, _M0L14handle__resultS1122, _M0L17error__to__stringS1129);
  if (_tmp_2542.tag) {
    int32_t const _M0L5_2aokS2505 = _tmp_2542.data.ok;
    _handle__error__result_2543 = _M0L5_2aokS2505;
  } else {
    void* const _M0L6_2aerrS2506 = _tmp_2542.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
    moonbit_decref_cycle_free(_M0L13handle__startS1117);
    _M0L11_2atry__errS1144 = _M0L6_2aerrS2506;
    goto join_1143;
  }
  if (_handle__error__result_2543) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
    moonbit_decref_cycle_free(_M0L13handle__startS1117);
    _M0L6_2atmpS2496 = 1;
  } else {
    struct moonbit_result_0 _tmp_2544;
    int32_t _handle__error__result_2545;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2544
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1150, _M0L8filenameS1119, _M0L5indexS1121, _M0L13handle__startS1117, _M0L14handle__resultS1122, _M0L17error__to__stringS1129);
    if (_tmp_2544.tag) {
      int32_t const _M0L5_2aokS2503 = _tmp_2544.data.ok;
      _handle__error__result_2545 = _M0L5_2aokS2503;
    } else {
      void* const _M0L6_2aerrS2504 = _tmp_2544.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
      moonbit_decref_cycle_free(_M0L13handle__startS1117);
      _M0L11_2atry__errS1144 = _M0L6_2aerrS2504;
      goto join_1143;
    }
    if (_handle__error__result_2545) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
      moonbit_decref_cycle_free(_M0L13handle__startS1117);
      _M0L6_2atmpS2496 = 1;
    } else {
      struct moonbit_result_0 _tmp_2546;
      int32_t _handle__error__result_2547;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2546
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1150, _M0L8filenameS1119, _M0L5indexS1121, _M0L13handle__startS1117, _M0L14handle__resultS1122, _M0L17error__to__stringS1129);
      if (_tmp_2546.tag) {
        int32_t const _M0L5_2aokS2501 = _tmp_2546.data.ok;
        _handle__error__result_2547 = _M0L5_2aokS2501;
      } else {
        void* const _M0L6_2aerrS2502 = _tmp_2546.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
        moonbit_decref_cycle_free(_M0L13handle__startS1117);
        _M0L11_2atry__errS1144 = _M0L6_2aerrS2502;
        goto join_1143;
      }
      if (_handle__error__result_2547) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
        moonbit_decref_cycle_free(_M0L13handle__startS1117);
        _M0L6_2atmpS2496 = 1;
      } else {
        struct moonbit_result_0 _tmp_2548;
        int32_t _handle__error__result_2549;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2548
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1150, _M0L8filenameS1119, _M0L5indexS1121, _M0L13handle__startS1117, _M0L14handle__resultS1122, _M0L17error__to__stringS1129);
        if (_tmp_2548.tag) {
          int32_t const _M0L5_2aokS2499 = _tmp_2548.data.ok;
          _handle__error__result_2549 = _M0L5_2aokS2499;
        } else {
          void* const _M0L6_2aerrS2500 = _tmp_2548.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
          moonbit_decref_cycle_free(_M0L13handle__startS1117);
          _M0L11_2atry__errS1144 = _M0L6_2aerrS2500;
          goto join_1143;
        }
        if (_handle__error__result_2549) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
          moonbit_decref_cycle_free(_M0L13handle__startS1117);
          _M0L6_2atmpS2496 = 1;
        } else {
          struct moonbit_result_0 _tmp_2550;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2550
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1150, _M0L8filenameS1119, _M0L5indexS1121, _M0L13handle__startS1117, _M0L14handle__resultS1122, _M0L17error__to__stringS1129);
          moonbit_decref_cycle_free(_M0L13handle__startS1117);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1129);
          if (_tmp_2550.tag) {
            int32_t const _M0L5_2aokS2497 = _tmp_2550.data.ok;
            _M0L6_2atmpS2496 = _M0L5_2aokS2497;
          } else {
            void* const _M0L6_2aerrS2498 = _tmp_2550.data.err;
            _M0L11_2atry__errS1144 = _M0L6_2aerrS2498;
            goto join_1143;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2496) {
    void* _M0L124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2507 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2507)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2507)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1144
    = _M0L124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2507;
    goto join_1143;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1122);
  }
  goto joinlet_2541;
  join_1143:;
  _M0L3errS1145 = _M0L11_2atry__errS1144;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1148
  = (struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1145;
  _M0L7_2anameS1149 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1148->$0;
  _M0L6_2acntS2533
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1148));
  if (_M0L6_2acntS2533 > 1) {
    int32_t _M0L11_2anew__cntS2534 = _M0L6_2acntS2533 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1148), _M0L11_2anew__cntS2534);
    moonbit_incref_cycle_free(_M0L7_2anameS1149);
  } else if (_M0L6_2acntS2533 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1148);
  }
  _M0L4nameS1147 = _M0L7_2anameS1149;
  goto join_1146;
  goto joinlet_2551;
  join_1146:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1122(_M0L14handle__resultS1122, _M0L4nameS1147, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1122);
  moonbit_decref_cycle_free(_M0L4nameS1147);
  joinlet_2551:;
  joinlet_2541:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1129(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2495,
  void* _M0L3errS1130
) {
  void* _M0L1eS1132;
  moonbit_string_t _M0L1eS1134;
  moonbit_string_t _result_2554;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1130)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1135 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1130;
      moonbit_string_t _M0L4_2aeS1136 = _M0L10_2aFailureS1135->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1136);
      _M0L1eS1134 = _M0L4_2aeS1136;
      goto join_1133;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1137 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1130;
      moonbit_string_t _M0L4_2aeS1138 = _M0L15_2aInspectErrorS1137->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1138);
      _M0L1eS1134 = _M0L4_2aeS1138;
      goto join_1133;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1139 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1130;
      moonbit_string_t _M0L4_2aeS1140 = _M0L16_2aSnapshotErrorS1139->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1140);
      _M0L1eS1134 = _M0L4_2aeS1140;
      goto join_1133;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error122RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1141 =
        (struct _M0DTPC15error5Error122RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1130;
      moonbit_string_t _M0L4_2aeS1142 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1141->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1142);
      _M0L1eS1134 = _M0L4_2aeS1142;
      goto join_1133;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1130);
      _M0L1eS1132 = _M0L3errS1130;
      goto join_1131;
      break;
    }
  }
  join_1133:;
  return _M0L1eS1134;
  join_1131:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _result_2554 = _M0FP15Error10to__string(_M0L1eS1132);
  moonbit_decref_cycle_free(_M0L1eS1132);
  return _result_2554;
}

int32_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1122(
  struct _M0TWssbEu* _M0L6_2aenvS2492,
  moonbit_string_t _M0L10__testnameS1123,
  moonbit_string_t _M0L7messageS1124,
  int32_t _M0L7skippedS1125
) {
  struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122* _M0L14_2acasted__envS2493;
  moonbit_string_t _M0L8filenameS1119;
  int32_t _M0L5indexS1121;
  moonbit_string_t _M0L10file__nameS1126;
  moonbit_string_t _M0L7messageS1127;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1128;
  moonbit_string_t _M0L6_2atmpS2494;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2493
  = (struct _M0R126_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1122*)_M0L6_2aenvS2492;
  _M0L8filenameS1119 = _M0L14_2acasted__envS2493->$1;
  _M0L5indexS1121 = _M0L14_2acasted__envS2493->$0;
  if (!_M0L7skippedS1125 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1126
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1119, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1127
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1124, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1128
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1128, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1128, _M0L10file__nameS1126);
  moonbit_decref_cycle_free(_M0L10file__nameS1126);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1128, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1128, _M0L5indexS1121);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1128, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1128, _M0L7messageS1127);
  moonbit_decref_cycle_free(_M0L7messageS1127);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1128, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2494
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1128);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1128);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2494);
  moonbit_decref_cycle_free(_M0L6_2atmpS2494);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1117(
  struct _M0TWEu* _M0L6_2aenvS2489
) {
  struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117* _M0L14_2acasted__envS2490;
  moonbit_string_t _M0L8filenameS1119;
  int32_t _M0L5indexS1121;
  moonbit_string_t _M0L10file__nameS1118;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1120;
  moonbit_string_t _M0L6_2atmpS2491;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2490
  = (struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fchain__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1117*)_M0L6_2aenvS2489;
  _M0L8filenameS1119 = _M0L14_2acasted__envS2490->$1;
  _M0L5indexS1121 = _M0L14_2acasted__envS2490->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1118
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1119, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1120
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1120, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1120, _M0L10file__nameS1118);
  moonbit_decref_cycle_free(_M0L10file__nameS1118);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1120, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1120, _M0L5indexS1121);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1120, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2491
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1120);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1120);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2491);
  moonbit_decref_cycle_free(_M0L6_2atmpS2491);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1087;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1094;
  struct _M0TUsiE** _M0L6_2atmpS2488;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1101;
  moonbit_string_t* _M0L9cli__argsS1102;
  moonbit_string_t _M0L6_2atmpS2487;
  moonbit_string_t _M0L6_2atmpS2486;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1103;
  int32_t _M0L7_2abindS1104;
  moonbit_string_t* _M0L7_2abindS1105;
  int32_t _M0L6_2acntS2535;
  int32_t _M0L2__S1106;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1087 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1094 = 0;
  _M0L6_2atmpS2488 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1101
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1101)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1101->$0 = _M0L6_2atmpS2488;
  _M0L16file__and__indexS1101->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1102
  = _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1102)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2487 = (moonbit_string_t)_M0L9cli__argsS1102[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2487);
  moonbit_decref_cycle_free(_M0L9cli__argsS1102);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2486
  = _M0MP46RiantR8snn__mbt8examples21chain__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2487);
  moonbit_decref_cycle_free(_M0L6_2atmpS2487);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1103
  = _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1094(_M0L51moonbit__test__driver__internal__split__mbt__stringS1094, _M0L6_2atmpS2486, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2486);
  _M0L7_2abindS1104 = _M0L10test__argsS1103->$1;
  _M0L7_2abindS1105 = _M0L10test__argsS1103->$0;
  _M0L6_2acntS2535
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1103));
  if (_M0L6_2acntS2535 > 1) {
    int32_t _M0L11_2anew__cntS2536 = _M0L6_2acntS2535 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1103), _M0L11_2anew__cntS2536);
    moonbit_incref_cycle_free(_M0L7_2abindS1105);
  } else if (_M0L6_2acntS2535 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1103);
  }
  _M0L2__S1106 = 0;
  while (1) {
    if (_M0L2__S1106 < _M0L7_2abindS1104) {
      moonbit_string_t _M0L3argS1107 =
        (moonbit_string_t)_M0L7_2abindS1105[_M0L2__S1106];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1108;
      moonbit_string_t _M0L4fileS1109;
      moonbit_string_t _M0L5rangeS1110;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1111;
      moonbit_string_t _M0L6_2atmpS2484;
      int32_t _M0L5startS1112;
      moonbit_string_t _M0L6_2atmpS2483;
      int32_t _M0L3endS1113;
      int32_t _M0L1iS1114;
      int32_t _M0L6_2atmpS2485;
      moonbit_incref_cycle_free(_M0L3argS1107);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1108
      = _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1094(_M0L51moonbit__test__driver__internal__split__mbt__stringS1094, _M0L3argS1107, 58);
      moonbit_decref_cycle_free(_M0L3argS1107);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1109
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1108, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1110
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1108, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1108);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1111
      = _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1094(_M0L51moonbit__test__driver__internal__split__mbt__stringS1094, _M0L5rangeS1110, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1110);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2484
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1111, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1112
      = _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1087(_M0L45moonbit__test__driver__internal__parse__int__S1087, _M0L6_2atmpS2484);
      moonbit_decref_cycle_free(_M0L6_2atmpS2484);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2483
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1111, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1111);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1113
      = _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1087(_M0L45moonbit__test__driver__internal__parse__int__S1087, _M0L6_2atmpS2483);
      moonbit_decref_cycle_free(_M0L6_2atmpS2483);
      _M0L1iS1114 = _M0L5startS1112;
      while (1) {
        if (_M0L1iS1114 < _M0L3endS1113) {
          struct _M0TUsiE* _M0L8_2atupleS2481;
          int32_t _M0L6_2atmpS2482;
          moonbit_incref_cycle_free(_M0L4fileS1109);
          _M0L8_2atupleS2481
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2481)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2481->$0 = _M0L4fileS1109;
          _M0L8_2atupleS2481->$1 = _M0L1iS1114;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1101, _M0L8_2atupleS2481);
          _M0L6_2atmpS2482 = _M0L1iS1114 + 1;
          _M0L1iS1114 = _M0L6_2atmpS2482;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1109);
        }
        break;
      }
      _M0L6_2atmpS2485 = _M0L2__S1106 + 1;
      _M0L2__S1106 = _M0L6_2atmpS2485;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1105);
    }
    break;
  }
  return _M0L16file__and__indexS1101;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1094(
  int32_t _M0L6_2aenvS2462,
  moonbit_string_t _M0L1sS1095,
  int32_t _M0L3sepS1096
) {
  moonbit_string_t* _M0L6_2atmpS2480;
  struct _M0TPB5ArrayGsE* _M0L3resS1097;
  struct _M0TPB8MutLocalGiE* _M0L1iS1098;
  struct _M0TPB8MutLocalGiE* _M0L5startS1099;
  int32_t _M0L3valS2475;
  int32_t _M0L6_2atmpS2476;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2480 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1097
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1097)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1097->$0 = _M0L6_2atmpS2480;
  _M0L3resS1097->$1 = 0;
  _M0L1iS1098
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1098)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1098->$0 = 0;
  _M0L5startS1099
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1099)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1099->$0 = 0;
  while (1) {
    int32_t _M0L3valS2463 = _M0L1iS1098->$0;
    int32_t _M0L6_2atmpS2464 = Moonbit_array_length(_M0L1sS1095);
    if (_M0L3valS2463 < _M0L6_2atmpS2464) {
      int32_t _M0L3valS2467 = _M0L1iS1098->$0;
      int32_t _M0L6_2atmpS2466;
      int32_t _M0L6_2atmpS2465;
      int32_t _M0L3valS2474;
      int32_t _M0L6_2atmpS2473;
      if (
        _M0L3valS2467 < 0
        || _M0L3valS2467 >= Moonbit_array_length(_M0L1sS1095)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2466 = _M0L1sS1095[_M0L3valS2467];
      _M0L6_2atmpS2465 = _M0L6_2atmpS2466;
      if (_M0L6_2atmpS2465 == _M0L3sepS1096) {
        int32_t _M0L3valS2469 = _M0L5startS1099->$0;
        int32_t _M0L3valS2470 = _M0L1iS1098->$0;
        moonbit_string_t _M0L6_2atmpS2468;
        int32_t _M0L3valS2472;
        int32_t _M0L6_2atmpS2471;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2468
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1095, _M0L3valS2469, _M0L3valS2470);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1097, _M0L6_2atmpS2468);
        _M0L3valS2472 = _M0L1iS1098->$0;
        _M0L6_2atmpS2471 = _M0L3valS2472 + 1;
        _M0L5startS1099->$0 = _M0L6_2atmpS2471;
      }
      _M0L3valS2474 = _M0L1iS1098->$0;
      _M0L6_2atmpS2473 = _M0L3valS2474 + 1;
      _M0L1iS1098->$0 = _M0L6_2atmpS2473;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1098);
    }
    break;
  }
  _M0L3valS2475 = _M0L5startS1099->$0;
  _M0L6_2atmpS2476 = Moonbit_array_length(_M0L1sS1095);
  if (_M0L3valS2475 < _M0L6_2atmpS2476) {
    int32_t _M0L3valS2478 = _M0L5startS1099->$0;
    int32_t _M0L6_2atmpS2479;
    moonbit_string_t _M0L6_2atmpS2477;
    moonbit_decref_cycle_free(_M0L5startS1099);
    _M0L6_2atmpS2479 = Moonbit_array_length(_M0L1sS1095);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2477
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1095, _M0L3valS2478, _M0L6_2atmpS2479);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1097, _M0L6_2atmpS2477);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1099);
  }
  return _M0L3resS1097;
}

int32_t _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1087(
  int32_t _M0L6_2aenvS2455,
  moonbit_string_t _M0L1sS1088
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1089;
  int32_t _M0L3lenS1090;
  int32_t _M0L7_2abindS1091;
  int32_t _M0L1iS1092;
  int32_t _result_2559;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1089
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1089)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1089->$0 = 0;
  _M0L3lenS1090 = Moonbit_array_length(_M0L1sS1088);
  _M0L7_2abindS1091 = 0;
  _M0L1iS1092 = _M0L7_2abindS1091;
  while (1) {
    if (_M0L1iS1092 < _M0L3lenS1090) {
      int32_t _M0L3valS2460 = _M0L3resS1089->$0;
      int32_t _M0L6_2atmpS2457 = _M0L3valS2460 * 10;
      int32_t _M0L6_2atmpS2459;
      int32_t _M0L6_2atmpS2458;
      int32_t _M0L6_2atmpS2456;
      int32_t _M0L6_2atmpS2461;
      if (
        _M0L1iS1092 < 0 || _M0L1iS1092 >= Moonbit_array_length(_M0L1sS1088)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2459 = _M0L1sS1088[_M0L1iS1092];
      _M0L6_2atmpS2458 = _M0L6_2atmpS2459 - 48;
      _M0L6_2atmpS2456 = _M0L6_2atmpS2457 + _M0L6_2atmpS2458;
      _M0L3resS1089->$0 = _M0L6_2atmpS2456;
      _M0L6_2atmpS2461 = _M0L1iS1092 + 1;
      _M0L1iS1092 = _M0L6_2atmpS2461;
      continue;
    }
    break;
  }
  _result_2559 = _M0L3resS1089->$0;
  moonbit_decref_cycle_free(_M0L3resS1089);
  return _result_2559;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples21chain__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1086
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1086);
  return _M0L4selfS1086;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1056,
  moonbit_string_t _M0L12_2adiscard__S1057,
  int32_t _M0L12_2adiscard__S1058,
  struct _M0TWEu* _M0L12_2adiscard__S1059,
  struct _M0TWssbEu* _M0L12_2adiscard__S1060,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1061
) {
  struct moonbit_result_0 _result_2560;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _result_2560.tag = 1;
  _result_2560.data.ok = 0;
  return _result_2560;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1062,
  moonbit_string_t _M0L12_2adiscard__S1063,
  int32_t _M0L12_2adiscard__S1064,
  struct _M0TWEu* _M0L12_2adiscard__S1065,
  struct _M0TWssbEu* _M0L12_2adiscard__S1066,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1067
) {
  struct moonbit_result_0 _result_2561;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _result_2561.tag = 1;
  _result_2561.data.ok = 0;
  return _result_2561;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1068,
  moonbit_string_t _M0L12_2adiscard__S1069,
  int32_t _M0L12_2adiscard__S1070,
  struct _M0TWEu* _M0L12_2adiscard__S1071,
  struct _M0TWssbEu* _M0L12_2adiscard__S1072,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1073
) {
  struct moonbit_result_0 _result_2562;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _result_2562.tag = 1;
  _result_2562.data.ok = 0;
  return _result_2562;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1074,
  moonbit_string_t _M0L12_2adiscard__S1075,
  int32_t _M0L12_2adiscard__S1076,
  struct _M0TWEu* _M0L12_2adiscard__S1077,
  struct _M0TWssbEu* _M0L12_2adiscard__S1078,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1079
) {
  struct moonbit_result_0 _result_2563;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _result_2563.tag = 1;
  _result_2563.data.ok = 0;
  return _result_2563;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples21chain__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1080,
  moonbit_string_t _M0L12_2adiscard__S1081,
  int32_t _M0L12_2adiscard__S1082,
  struct _M0TWEu* _M0L12_2adiscard__S1083,
  struct _M0TWssbEu* _M0L12_2adiscard__S1084,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1085
) {
  struct moonbit_result_0 _result_2564;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _result_2564.tag = 1;
  _result_2564.data.ok = 0;
  return _result_2564;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1055
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1047,
  int32_t _M0L3preS1044,
  int32_t _M0L4postS1046,
  float _M0L1wS1048
) {
  int32_t _M0L8pre__idxS1043;
  int32_t _M0L9post__idxS1045;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2454;
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L8pre__idxS1043 = _M0L3preS1044 - 1;
  _M0L9post__idxS1045 = _M0L4postS1046 - 1;
  _M0L6matrixS2454 = _M0L1cS1047->$4;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(_M0L6matrixS2454, _M0L8pre__idxS1043, _M0L9post__idxS1045, _M0L1wS1048);
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1040,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1041,
  moonbit_string_t _M0L3symS1042
) {
  int32_t _M0L1nS2452;
  int32_t _M0L1nS2453;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1039;
  float* _M0L6_2atmpS2451;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2442;
  float* _M0L6_2atmpS2450;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2443;
  float* _M0L6_2atmpS2449;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2444;
  int32_t* _M0L6_2atmpS2448;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2445;
  float* _M0L6_2atmpS2447;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2446;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2565;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2452 = _M0L3preS1040->$2;
  _M0L1nS2453 = _M0L4postS1041->$2;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1039
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(_M0L1nS2452, _M0L1nS2453);
  _M0L6_2atmpS2451 = moonbit_empty_float_array;
  _M0L6_2atmpS2442
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2442)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2442->$0 = _M0L6_2atmpS2451;
  _M0L6_2atmpS2442->$1 = 0;
  _M0L6_2atmpS2450 = moonbit_empty_float_array;
  _M0L6_2atmpS2443
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2443)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2443->$0 = _M0L6_2atmpS2450;
  _M0L6_2atmpS2443->$1 = 0;
  _M0L6_2atmpS2449 = moonbit_empty_float_array;
  _M0L6_2atmpS2444
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2444)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2444->$0 = _M0L6_2atmpS2449;
  _M0L6_2atmpS2444->$1 = 0;
  _M0L6_2atmpS2448 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2445
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2445)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2445->$0 = _M0L6_2atmpS2448;
  _M0L6_2atmpS2445->$1 = 0;
  _M0L6_2atmpS2447 = moonbit_empty_float_array;
  _M0L6_2atmpS2446
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2446)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2446->$0 = _M0L6_2atmpS2447;
  _M0L6_2atmpS2446->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1040);
  moonbit_incref_cycle_free(_M0L4postS1041);
  moonbit_incref_cycle_free(_M0L3symS1042);
  _block_2565
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2565)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2565->$0 = _M0L3preS1040;
  _block_2565->$1 = _M0L4postS1041;
  _block_2565->$2 = _M0L3symS1042;
  _block_2565->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2565->$4 = _M0L6matrixS1039;
  _block_2565->$5 = _M0L6_2atmpS2442;
  _block_2565->$6 = _M0L6_2atmpS2443;
  _block_2565->$7 = _M0L6_2atmpS2444;
  _block_2565->$8 = _M0L6_2atmpS2445;
  _block_2565->$9 = _M0L6_2atmpS2446;
  return _block_2565;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1037;
  float _M0L2glS1038;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2566;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1037 = -0x1p+0f;
  _M0L2glS1038 = -0x1p+0f;
  _block_2566
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2566)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2566->$0 = _M0L1cS1037;
  _block_2566->$1 = _M0L2glS1038;
  _block_2566->$2 = 0x1.ep+3f;
  _block_2566->$3 = -0x1.9p+5f;
  _block_2566->$4 = -0x1.ep+5f;
  _block_2566->$5 = -0x1.18p+6f;
  _block_2566->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2566->$7 = 0x1p+1f;
  _block_2566->$8 = 0x0p+0f;
  _block_2566->$9 = 0x0p+0f;
  _block_2566->$10 = 0x0p+0f;
  return _block_2566;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1011,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1013,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1016
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1010;
  float _M0L2vtS2440;
  float _M0L2vrS2441;
  float _M0L6spreadS1012;
  int32_t _M0L7_2abindS1014;
  int32_t _M0L1kS1015;
  struct _M0TPB5ArrayGfE* _M0L1wS1018;
  struct _M0TPB5ArrayGbE* _M0L4fireS1019;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1020;
  struct _M0TPB5ArrayGfE* _M0L1iS1021;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1022;
  struct _M0TPB5ArrayGfE* _M0L2geS1023;
  struct _M0TPB5ArrayGfE* _M0L2giS1024;
  struct _M0TPB5ArrayGfE* _M0L2heS1025;
  struct _M0TPB5ArrayGfE* _M0L2hiS1026;
  struct _M0TPB5ArrayGfE* _M0L3gluS1027;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1028;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1029;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1030;
  float _M0L4e__eS1031;
  float _M0L4e__iS1032;
  float _M0L3treS1033;
  float _M0L3tdeS1034;
  float _M0L3triS1035;
  float _M0L3tdiS1036;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2439;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2568;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1010 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  _M0L2vtS2440 = _M0L5paramS1013->$3;
  _M0L2vrS2441 = _M0L5paramS1013->$4;
  _M0L6spreadS1012 = _M0L2vtS2440 - _M0L2vrS2441;
  _M0L7_2abindS1014 = 0;
  _M0L1kS1015 = _M0L7_2abindS1014;
  while (1) {
    if (_M0L1kS1015 < _M0L1nS1011) {
      float _M0L2vrS2435 = _M0L5paramS1013->$4;
      float _M0L6_2atmpS2437;
      float _M0L6_2atmpS2436;
      float _M0L6_2atmpS2434;
      int32_t _M0L6_2atmpS2438;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2437 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1016);
      _M0L6_2atmpS2436 = _M0L6_2atmpS2437 * _M0L6spreadS1012;
      _M0L6_2atmpS2434 = _M0L2vrS2435 + _M0L6_2atmpS2436;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1010, _M0L1kS1015, _M0L6_2atmpS2434);
      _M0L6_2atmpS2438 = _M0L1kS1015 + 1;
      _M0L1kS1015 = _M0L6_2atmpS2438;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1018 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1019 = _M0MPC15array5Array4makeGbE(_M0L1nS1011, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1020 = _M0MPC15array5Array4makeGiE(_M0L1nS1011, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1021 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1022 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1023 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1024 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1025 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1026 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1027 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1028 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1029 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1030 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x1p+0f);
  _M0L4e__eS1031 = 0x0p+0f;
  _M0L4e__iS1032 = -0x1.2cp+6f;
  _M0L3treS1033 = 0x1p+0f;
  _M0L3tdeS1034 = 0x1.8p+2f;
  _M0L3triS1035 = 0x1p-1f;
  _M0L3tdiS1036 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2439 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1013);
  _block_2568
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2568)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2568->$0 = _M0L5paramS1013;
  _block_2568->$1 = _M0L6_2atmpS2439;
  _block_2568->$2 = _M0L1nS1011;
  _block_2568->$3 = _M0L1vS1010;
  _block_2568->$4 = _M0L1wS1018;
  _block_2568->$5 = _M0L4fireS1019;
  _block_2568->$6 = _M0L4tabsS1020;
  _block_2568->$7 = _M0L1iS1021;
  _block_2568->$8 = _M0L9syn__currS1022;
  _block_2568->$9 = _M0L2geS1023;
  _block_2568->$10 = _M0L2giS1024;
  _block_2568->$11 = _M0L2heS1025;
  _block_2568->$12 = _M0L2hiS1026;
  _block_2568->$13 = _M0L3gluS1027;
  _block_2568->$14 = _M0L4gabaS1028;
  _block_2568->$15 = _M0L7gsyn__eS1029;
  _block_2568->$16 = _M0L7gsyn__iS1030;
  _block_2568->$17 = _M0L4e__eS1031;
  _block_2568->$18 = _M0L4e__iS1032;
  _block_2568->$19 = _M0L3treS1033;
  _block_2568->$20 = _M0L3tdeS1034;
  _block_2568->$21 = _M0L3triS1035;
  _block_2568->$22 = _M0L3tdiS1036;
  return _block_2568;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2569;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2569
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2569)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2569->$0 = 0x1p+1f;
  return _block_2569;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt8sim__for(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS1006,
  float _M0L8durationS1005
) {
  float _M0L2dtS1002;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS1003;
  float _M0L6_2atmpS2433;
  int32_t _M0L5stepsS1004;
  int32_t _M0L7_2abindS1007;
  int32_t _M0L2__S1008;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L2dtS1002 = 0x1p-3f;
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L4timeS1003 = _M0MP26RiantR8snn__mbt4Time3new();
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt7set__dt(_M0L4timeS1003, _M0L2dtS1002);
  _M0L6_2atmpS2433 = _M0L8durationS1005 / _M0L2dtS1002;
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L5stepsS1004 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2433);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12record__zero(_M0L5modelS1006);
  _M0L7_2abindS1007 = 0;
  _M0L2__S1008 = _M0L7_2abindS1007;
  while (1) {
    if (_M0L2__S1008 < _M0L5stepsS1004) {
      int32_t _M0L6_2atmpS2432;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11step__model(_M0L5modelS1006, _M0L4timeS1003);
      _M0L6_2atmpS2432 = _M0L2__S1008 + 1;
      _M0L2__S1008 = _M0L6_2atmpS2432;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4timeS1003);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11step__model(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS977,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS975
) {
  float _M0L6t__nowS974;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS976;
  int32_t _M0L7_2abindS978;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS979;
  int32_t _M0L2__S980;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS983;
  int32_t _M0L7_2abindS984;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS985;
  int32_t _M0L2__S986;
  float _M0L2dtS989;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L7_2abindS990;
  int32_t _M0L7_2abindS991;
  struct _M0TP26RiantR8snn__mbt2IF** _M0L7_2abindS992;
  int32_t _M0L2__S993;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS996;
  int32_t _M0L7_2abindS997;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS998;
  int32_t _M0L2__S999;
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6t__nowS974 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS975);
  _M0L7_2abindS976 = _M0L5modelS977->$1;
  _M0L7_2abindS978 = _M0L7_2abindS976->$1;
  _M0L7_2abindS979 = _M0L7_2abindS976->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS979);
  _M0L2__S980 = 0;
  while (1) {
    if (_M0L2__S980 < _M0L7_2abindS978) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS981 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS979[
          _M0L2__S980
        ];
      int32_t _M0L6_2atmpS2426;
      moonbit_incref_cycle_free(_M0L1cS981);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS981, _M0L6t__nowS974);
      moonbit_decref_cycle_free(_M0L1cS981);
      _M0L6_2atmpS2426 = _M0L2__S980 + 1;
      _M0L2__S980 = _M0L6_2atmpS2426;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS979);
    }
    break;
  }
  _M0L7_2abindS983 = _M0L5modelS977->$1;
  _M0L7_2abindS984 = _M0L7_2abindS983->$1;
  _M0L7_2abindS985 = _M0L7_2abindS983->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS985);
  _M0L2__S986 = 0;
  while (1) {
    if (_M0L2__S986 < _M0L7_2abindS984) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS987 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS985[
          _M0L2__S986
        ];
      int32_t _M0L6_2atmpS2427;
      moonbit_incref_cycle_free(_M0L1cS987);
      #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS987, _M0L6t__nowS974);
      moonbit_decref_cycle_free(_M0L1cS987);
      _M0L6_2atmpS2427 = _M0L2__S986 + 1;
      _M0L2__S986 = _M0L6_2atmpS2427;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS985);
    }
    break;
  }
  _M0L2dtS989 = _M0L4timeS975->$2;
  _M0L7_2abindS990 = _M0L5modelS977->$0;
  _M0L7_2abindS991 = _M0L7_2abindS990->$1;
  _M0L7_2abindS992 = _M0L7_2abindS990->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS992);
  _M0L2__S993 = 0;
  while (1) {
    if (_M0L2__S993 < _M0L7_2abindS991) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS994 =
        (struct _M0TP26RiantR8snn__mbt2IF*)_M0L7_2abindS992[_M0L2__S993];
      int32_t _M0L6_2atmpS2428;
      moonbit_incref_cycle_free(_M0L1pS994);
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L1pS994, _M0L2dtS989);
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1pS994);
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L1pS994, _M0L2dtS989);
      moonbit_decref_cycle_free(_M0L1pS994);
      _M0L6_2atmpS2428 = _M0L2__S993 + 1;
      _M0L2__S993 = _M0L6_2atmpS2428;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS992);
    }
    break;
  }
  _M0L7_2abindS996 = _M0L5modelS977->$2;
  _M0L7_2abindS997 = _M0L7_2abindS996->$1;
  _M0L7_2abindS998 = _M0L7_2abindS996->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS998);
  _M0L2__S999 = 0;
  while (1) {
    if (_M0L2__S999 < _M0L7_2abindS997) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1000 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS998[_M0L2__S999];
      struct _M0TPB5ArrayGfE* _M0L1tS2430 = _M0L4timeS975->$0;
      float _M0L6_2atmpS2429;
      int32_t _M0L6_2atmpS2431;
      moonbit_incref_cycle_free(_M0L1mS1000);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0L6_2atmpS2429 = _M0MPC15array5Array2atGfE(_M0L1tS2430, 0);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS1000, _M0L6_2atmpS2429);
      moonbit_decref_cycle_free(_M0L1mS1000);
      _M0L6_2atmpS2431 = _M0L2__S999 + 1;
      _M0L2__S999 = _M0L6_2atmpS2431;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS998);
    }
    break;
  }
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS975, _M0L2dtS989);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS950,
  float _M0L6t__nowS961
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS2425;
  int32_t _M0L6_2atmpS2424;
  int32_t _M0L10use__delayS949;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2423;
  int32_t _M0L6_2atmpS2422;
  int32_t _M0L8use__rhoS951;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS2425 = _M0L1cS950->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2424 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS2425);
  _M0L10use__delayS949 = _M0L6_2atmpS2424 > 0;
  _M0L3rhoS2423 = _M0L1cS950->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2422 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2423);
  _M0L8use__rhoS951 = _M0L6_2atmpS2422 > 0;
  if (_M0L10use__delayS949) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2385 = _M0L1cS950->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS2384 = _M0L3preS2385->$5;
    int32_t _M0L6n__preS952;
    struct _M0TPB8MutLocalGiE* _M0L1jS953;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS952 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2384);
    _M0L1jS953
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS953)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS953->$0 = 0;
    while (1) {
      int32_t _M0L3valS2353 = _M0L1jS953->$0;
      if (_M0L3valS2353 < _M0L6n__preS952) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2356 = _M0L1cS950->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS2354 = _M0L3preS2356->$5;
        int32_t _M0L3valS2355 = _M0L1jS953->$0;
        int32_t _M0L3valS2383;
        int32_t _M0L6_2atmpS2382;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS2354, _M0L3valS2355)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2381 =
            _M0L1cS950->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2379 = _M0L6matrixS2381->$2;
          int32_t _M0L3valS2380 = _M0L1jS953->$0;
          int32_t _M0L5startS954;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2378;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2375;
          int32_t _M0L3valS2377;
          int32_t _M0L6_2atmpS2376;
          int32_t _M0L3endS955;
          struct _M0TPB8MutLocalGiE* _M0L1sS956;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS954
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2379, _M0L3valS2380);
          _M0L6matrixS2378 = _M0L1cS950->$4;
          _M0L6rowptrS2375 = _M0L6matrixS2378->$2;
          _M0L3valS2377 = _M0L1jS953->$0;
          _M0L6_2atmpS2376 = _M0L3valS2377 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS955
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2375, _M0L6_2atmpS2376);
          _M0L1sS956
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS956)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS956->$0 = _M0L5startS954;
          while (1) {
            int32_t _M0L3valS2357 = _M0L1sS956->$0;
            if (_M0L3valS2357 < _M0L3endS955) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2374 =
                _M0L1cS950->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS2372 = _M0L6matrixS2374->$3;
              int32_t _M0L3valS2373 = _M0L1sS956->$0;
              int32_t _M0L9post__idxS957;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2371;
              struct _M0TPB5ArrayGfE* _M0L4valsS2369;
              int32_t _M0L3valS2370;
              float _M0L1wS958;
              struct _M0TPB5ArrayGfE* _M0L6delaysS2367;
              int32_t _M0L3valS2368;
              float _M0L1dS959;
              float _M0L9w__scaledS960;
              int32_t _M0L3valS2363;
              int32_t _M0L6_2atmpS2362;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS957
              = _M0MPC15array5Array2atGiE(_M0L6colptrS2372, _M0L3valS2373);
              _M0L6matrixS2371 = _M0L1cS950->$4;
              _M0L4valsS2369 = _M0L6matrixS2371->$4;
              _M0L3valS2370 = _M0L1sS956->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS958
              = _M0MPC15array5Array2atGfE(_M0L4valsS2369, _M0L3valS2370);
              _M0L6delaysS2367 = _M0L1cS950->$5;
              _M0L3valS2368 = _M0L1sS956->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS959
              = _M0MPC15array5Array2atGfE(_M0L6delaysS2367, _M0L3valS2368);
              if (_M0L8use__rhoS951) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS2365 = _M0L1cS950->$6;
                int32_t _M0L3valS2366 = _M0L1sS956->$0;
                float _M0L6_2atmpS2364;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2364
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2365, _M0L3valS2366);
                _M0L9w__scaledS960 = _M0L1wS958 * _M0L6_2atmpS2364;
              } else {
                _M0L9w__scaledS960 = _M0L1wS958;
              }
              if (_M0L1dS959 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS950, _M0L9post__idxS957, _M0L9w__scaledS960);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS2358 =
                  _M0L1cS950->$7;
                float _M0L6_2atmpS2359 = _M0L6t__nowS961 + _M0L1dS959;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS2360;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2361;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS2358, _M0L6_2atmpS2359);
                _M0L14pending__postsS2360 = _M0L1cS950->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS2360, _M0L9post__idxS957);
                _M0L16pending__weightsS2361 = _M0L1cS950->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS2361, _M0L9w__scaledS960);
              }
              _M0L3valS2363 = _M0L1sS956->$0;
              _M0L6_2atmpS2362 = _M0L3valS2363 + 1;
              _M0L1sS956->$0 = _M0L6_2atmpS2362;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS956);
            }
            break;
          }
        }
        _M0L3valS2383 = _M0L1jS953->$0;
        _M0L6_2atmpS2382 = _M0L3valS2383 + 1;
        _M0L1jS953->$0 = _M0L6_2atmpS2382;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS953);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2419 = _M0L1cS950->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS964;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS2419 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS2419)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS2419, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2419) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2420 = _M0L1cS950->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2509 = _M0L4postS2420->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS2509);
      _M0L6targetS964 = _M0L8_2afieldS2509;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2421 = _M0L1cS950->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2510 = _M0L4postS2421->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS2510);
      _M0L6targetS964 = _M0L8_2afieldS2510;
    }
    if (_M0L8use__rhoS951) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2415 = _M0L1cS950->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2414 = _M0L3preS2415->$5;
      int32_t _M0L6n__preS965;
      struct _M0TPB8MutLocalGiE* _M0L1jS966;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS965 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2414);
      _M0L1jS966
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS966)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS966->$0 = 0;
      while (1) {
        int32_t _M0L3valS2386 = _M0L1jS966->$0;
        if (_M0L3valS2386 < _M0L6n__preS965) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2389 = _M0L1cS950->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS2387 = _M0L3preS2389->$5;
          int32_t _M0L3valS2388 = _M0L1jS966->$0;
          int32_t _M0L3valS2413;
          int32_t _M0L6_2atmpS2412;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS2387, _M0L3valS2388)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2411 =
              _M0L1cS950->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2409 = _M0L6matrixS2411->$2;
            int32_t _M0L3valS2410 = _M0L1jS966->$0;
            int32_t _M0L5startS967;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2408;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2405;
            int32_t _M0L3valS2407;
            int32_t _M0L6_2atmpS2406;
            int32_t _M0L3endS968;
            struct _M0TPB8MutLocalGiE* _M0L1sS969;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS967
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2409, _M0L3valS2410);
            _M0L6matrixS2408 = _M0L1cS950->$4;
            _M0L6rowptrS2405 = _M0L6matrixS2408->$2;
            _M0L3valS2407 = _M0L1jS966->$0;
            _M0L6_2atmpS2406 = _M0L3valS2407 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS968
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2405, _M0L6_2atmpS2406);
            _M0L1sS969
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS969)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS969->$0 = _M0L5startS967;
            while (1) {
              int32_t _M0L3valS2390 = _M0L1sS969->$0;
              if (_M0L3valS2390 < _M0L3endS968) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2404 =
                  _M0L1cS950->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS2402 =
                  _M0L6matrixS2404->$3;
                int32_t _M0L3valS2403 = _M0L1sS969->$0;
                int32_t _M0L9post__idxS970;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2401;
                struct _M0TPB5ArrayGfE* _M0L4valsS2399;
                int32_t _M0L3valS2400;
                float _M0L6_2atmpS2395;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2397;
                int32_t _M0L3valS2398;
                float _M0L6_2atmpS2396;
                float _M0L9w__scaledS971;
                float _M0L6_2atmpS2392;
                float _M0L6_2atmpS2391;
                int32_t _M0L3valS2394;
                int32_t _M0L6_2atmpS2393;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS970
                = _M0MPC15array5Array2atGiE(_M0L6colptrS2402, _M0L3valS2403);
                _M0L6matrixS2401 = _M0L1cS950->$4;
                _M0L4valsS2399 = _M0L6matrixS2401->$4;
                _M0L3valS2400 = _M0L1sS969->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2395
                = _M0MPC15array5Array2atGfE(_M0L4valsS2399, _M0L3valS2400);
                _M0L3rhoS2397 = _M0L1cS950->$6;
                _M0L3valS2398 = _M0L1sS969->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2396
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2397, _M0L3valS2398);
                _M0L9w__scaledS971 = _M0L6_2atmpS2395 * _M0L6_2atmpS2396;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2392
                = _M0MPC15array5Array2atGfE(_M0L6targetS964, _M0L9post__idxS970);
                _M0L6_2atmpS2391 = _M0L6_2atmpS2392 + _M0L9w__scaledS971;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS964, _M0L9post__idxS970, _M0L6_2atmpS2391);
                _M0L3valS2394 = _M0L1sS969->$0;
                _M0L6_2atmpS2393 = _M0L3valS2394 + 1;
                _M0L1sS969->$0 = _M0L6_2atmpS2393;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS969);
              }
              break;
            }
          }
          _M0L3valS2413 = _M0L1jS966->$0;
          _M0L6_2atmpS2412 = _M0L3valS2413 + 1;
          _M0L1jS966->$0 = _M0L6_2atmpS2412;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS966);
          moonbit_decref_cycle_free(_M0L6targetS964);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2416 =
        _M0L1cS950->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2418 = _M0L1cS950->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2417 = _M0L3preS2418->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2416, _M0L4fireS2417, _M0L6targetS964);
      moonbit_decref_cycle_free(_M0L6targetS964);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS942,
  float _M0L6t__nowS945
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS2352;
  int32_t _M0L1nS941;
  struct _M0TPB8MutLocalGiE* _M0L4keptS943;
  struct _M0TPB8MutLocalGiE* _M0L1kS944;
  int32_t _M0L3valS2351;
  int32_t _M0L6_2atmpS2350;
  struct _M0TPB8MutLocalGiE* _M0L4dropS947;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS2352 = _M0L1cS942->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS941 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS2352);
  if (_M0L1nS941 == 0) {
    return 0;
  }
  _M0L4keptS943
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS943)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS943->$0 = 0;
  _M0L1kS944
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS944)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS944->$0 = 0;
  while (1) {
    int32_t _M0L3valS2313 = _M0L1kS944->$0;
    if (_M0L3valS2313 < _M0L1nS941) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS2315 = _M0L1cS942->$7;
      int32_t _M0L3valS2316 = _M0L1kS944->$0;
      float _M0L6_2atmpS2314;
      int32_t _M0L3valS2343;
      int32_t _M0L6_2atmpS2342;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2314
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS2315, _M0L3valS2316);
      if (_M0L6_2atmpS2314 <= _M0L6t__nowS945) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS2321 = _M0L1cS942->$8;
        int32_t _M0L3valS2322 = _M0L1kS944->$0;
        int32_t _M0L6_2atmpS2317;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2319;
        int32_t _M0L3valS2320;
        float _M0L6_2atmpS2318;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS2317
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS2321, _M0L3valS2322);
        _M0L16pending__weightsS2319 = _M0L1cS942->$9;
        _M0L3valS2320 = _M0L1kS944->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS2318
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS2319, _M0L3valS2320);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS942, _M0L6_2atmpS2317, _M0L6_2atmpS2318);
      } else {
        int32_t _M0L3valS2323 = _M0L4keptS943->$0;
        int32_t _M0L3valS2324 = _M0L1kS944->$0;
        int32_t _M0L3valS2341;
        int32_t _M0L6_2atmpS2340;
        if (_M0L3valS2323 != _M0L3valS2324) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS2325 = _M0L1cS942->$7;
          int32_t _M0L3valS2326 = _M0L4keptS943->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS2328 = _M0L1cS942->$7;
          int32_t _M0L3valS2329 = _M0L1kS944->$0;
          float _M0L6_2atmpS2327;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS2330;
          int32_t _M0L3valS2331;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS2333;
          int32_t _M0L3valS2334;
          int32_t _M0L6_2atmpS2332;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2335;
          int32_t _M0L3valS2336;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2338;
          int32_t _M0L3valS2339;
          float _M0L6_2atmpS2337;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2327
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS2328, _M0L3valS2329);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS2325, _M0L3valS2326, _M0L6_2atmpS2327);
          _M0L14pending__postsS2330 = _M0L1cS942->$8;
          _M0L3valS2331 = _M0L4keptS943->$0;
          _M0L14pending__postsS2333 = _M0L1cS942->$8;
          _M0L3valS2334 = _M0L1kS944->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2332
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS2333, _M0L3valS2334);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS2330, _M0L3valS2331, _M0L6_2atmpS2332);
          _M0L16pending__weightsS2335 = _M0L1cS942->$9;
          _M0L3valS2336 = _M0L4keptS943->$0;
          _M0L16pending__weightsS2338 = _M0L1cS942->$9;
          _M0L3valS2339 = _M0L1kS944->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2337
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS2338, _M0L3valS2339);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS2335, _M0L3valS2336, _M0L6_2atmpS2337);
        }
        _M0L3valS2341 = _M0L4keptS943->$0;
        _M0L6_2atmpS2340 = _M0L3valS2341 + 1;
        _M0L4keptS943->$0 = _M0L6_2atmpS2340;
      }
      _M0L3valS2343 = _M0L1kS944->$0;
      _M0L6_2atmpS2342 = _M0L3valS2343 + 1;
      _M0L1kS944->$0 = _M0L6_2atmpS2342;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS944);
    }
    break;
  }
  _M0L3valS2351 = _M0L4keptS943->$0;
  moonbit_decref_cycle_free(_M0L4keptS943);
  _M0L6_2atmpS2350 = _M0L1nS941 - _M0L3valS2351;
  _M0L4dropS947
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS947)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS947->$0 = _M0L6_2atmpS2350;
  while (1) {
    int32_t _M0L3valS2344 = _M0L4dropS947->$0;
    if (_M0L3valS2344 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS2345 = _M0L1cS942->$7;
      void* _M0L6_2atmpS2512;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS2346;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2347;
      void* _M0L6_2atmpS2511;
      int32_t _M0L3valS2349;
      int32_t _M0L6_2atmpS2348;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2512
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS2345);
      moonbit_decref_cycle_free(_M0L6_2atmpS2512);
      _M0L14pending__postsS2346 = _M0L1cS942->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS2346);
      _M0L16pending__weightsS2347 = _M0L1cS942->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2511
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS2347);
      moonbit_decref_cycle_free(_M0L6_2atmpS2511);
      _M0L3valS2349 = _M0L4dropS947->$0;
      _M0L6_2atmpS2348 = _M0L3valS2349 - 1;
      _M0L4dropS947->$0 = _M0L6_2atmpS2348;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS947);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS938,
  int32_t _M0L9post__idxS939,
  float _M0L1wS940
) {
  moonbit_string_t _M0L3symS2300;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS2300 = _M0L1cS938->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS2300 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2300)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2300, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2300) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2306 = _M0L1cS938->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS2301 = _M0L4postS2306->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2305 = _M0L1cS938->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS2304 = _M0L4postS2305->$13;
    float _M0L6_2atmpS2303;
    float _M0L6_2atmpS2302;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS2303
    = _M0MPC15array5Array2atGfE(_M0L3gluS2304, _M0L9post__idxS939);
    _M0L6_2atmpS2302 = _M0L6_2atmpS2303 + _M0L1wS940;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS2301, _M0L9post__idxS939, _M0L6_2atmpS2302);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2312 = _M0L1cS938->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS2307 = _M0L4postS2312->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2311 = _M0L1cS938->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS2310 = _M0L4postS2311->$14;
    float _M0L6_2atmpS2309;
    float _M0L6_2atmpS2308;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS2309
    = _M0MPC15array5Array2atGfE(_M0L4gabaS2310, _M0L9post__idxS939);
    _M0L6_2atmpS2308 = _M0L6_2atmpS2309 + _M0L1wS940;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS2307, _M0L9post__idxS939, _M0L6_2atmpS2308);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS932
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS931;
  int32_t _M0L7_2abindS933;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS934;
  int32_t _M0L2__S935;
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS931 = _M0L5modelS932->$2;
  _M0L7_2abindS933 = _M0L7_2abindS931->$1;
  _M0L7_2abindS934 = _M0L7_2abindS931->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS934);
  _M0L2__S935 = 0;
  while (1) {
    if (_M0L2__S935 < _M0L7_2abindS933) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS936 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS934[_M0L2__S935];
      int32_t _M0L6_2atmpS2299;
      moonbit_incref_cycle_free(_M0L1mS936);
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS936, 0x0p+0f);
      moonbit_decref_cycle_free(_M0L1mS936);
      _M0L6_2atmpS2299 = _M0L2__S935 + 1;
      _M0L2__S935 = _M0L6_2atmpS2299;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS934);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS928,
  float _M0L1tS930
) {
  int32_t _M0L11step__countS2285;
  int32_t _M0L6_2atmpS2284;
  int32_t _M0L11step__countS2287;
  int32_t _M0L9rec__stepS2288;
  int32_t _M0L6_2atmpS2286;
  moonbit_string_t _M0L3symS2291;
  float _M0L1vS929;
  struct _M0TPB5ArrayGfE* _M0L4dataS2289;
  struct _M0TPB5ArrayGfE* _M0L5timesS2290;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS2285 = _M0L1mS928->$6;
  _M0L6_2atmpS2284 = _M0L11step__countS2285 + 1;
  _M0L1mS928->$6 = _M0L6_2atmpS2284;
  _M0L11step__countS2287 = _M0L1mS928->$6;
  _M0L9rec__stepS2288 = _M0L1mS928->$5;
  _M0L6_2atmpS2286 = _M0L11step__countS2287 % _M0L9rec__stepS2288;
  if (_M0L6_2atmpS2286 != 0) {
    return 0;
  }
  _M0L3symS2291 = _M0L1mS928->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS2291 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS2291)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS2291, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS2291) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS2294 = _M0L1mS928->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS2292 = _M0L3popS2294->$3;
    int32_t _M0L6neuronS2293 = _M0L1mS928->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS929 = _M0MPC15array5Array2atGfE(_M0L1vS2292, _M0L6neuronS2293);
  } else {
    moonbit_string_t _M0L3symS2295 = _M0L1mS928->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS2295 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS2295)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS2295, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS2295) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS2298 = _M0L1mS928->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2296 = _M0L3popS2298->$5;
      int32_t _M0L6neuronS2297 = _M0L1mS928->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2296, _M0L6neuronS2297)) {
        _M0L1vS929 = 0x1p+0f;
      } else {
        _M0L1vS929 = 0x0p+0f;
      }
    } else {
      _M0L1vS929 = 0x0p+0f;
    }
  }
  _M0L4dataS2289 = _M0L1mS928->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS2289, _M0L1vS929);
  _M0L5timesS2290 = _M0L1mS928->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS2290, _M0L1tS930);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor6new__v(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS926,
  int32_t _M0L6neuronS927
) {
  float* _M0L6_2atmpS2283;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2280;
  float* _M0L6_2atmpS2282;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2281;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_2582;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS2283 = moonbit_empty_float_array;
  _M0L6_2atmpS2280
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2280)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2280->$0 = _M0L6_2atmpS2283;
  _M0L6_2atmpS2280->$1 = 0;
  _M0L6_2atmpS2282 = moonbit_empty_float_array;
  _M0L6_2atmpS2281
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2281)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2281->$0 = _M0L6_2atmpS2282;
  _M0L6_2atmpS2281->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS926);
  _block_2582
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_2582)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2582->$0 = _M0L3popS926;
  _block_2582->$1 = (moonbit_string_t)moonbit_string_literal_10.data;
  _block_2582->$2 = _M0L6_2atmpS2280;
  _block_2582->$3 = _M0L6_2atmpS2281;
  _block_2582->$4 = _M0L6neuronS927;
  _block_2582->$5 = 1;
  _block_2582->$6 = 0;
  return _block_2582;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS922
) {
  int32_t _M0L1nS921;
  int32_t _M0L7_2abindS923;
  int32_t _M0L1iS924;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS921 = _M0L1pS922->$2;
  _M0L7_2abindS923 = 0;
  _M0L1iS924 = _M0L7_2abindS923;
  while (1) {
    if (_M0L1iS924 < _M0L1nS921) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2257 = _M0L1pS922->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2278 = _M0L1pS922->$9;
      float _M0L6_2atmpS2273;
      struct _M0TPB5ArrayGfE* _M0L1vS2277;
      float _M0L6_2atmpS2275;
      float _M0L4e__eS2276;
      float _M0L6_2atmpS2274;
      float _M0L6_2atmpS2270;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2272;
      float _M0L6_2atmpS2271;
      float _M0L6_2atmpS2259;
      struct _M0TPB5ArrayGfE* _M0L2giS2269;
      float _M0L6_2atmpS2264;
      struct _M0TPB5ArrayGfE* _M0L1vS2268;
      float _M0L6_2atmpS2266;
      float _M0L4e__iS2267;
      float _M0L6_2atmpS2265;
      float _M0L6_2atmpS2261;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2263;
      float _M0L6_2atmpS2262;
      float _M0L6_2atmpS2260;
      float _M0L6_2atmpS2258;
      int32_t _M0L6_2atmpS2279;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2273 = _M0MPC15array5Array2atGfE(_M0L2geS2278, _M0L1iS924);
      _M0L1vS2277 = _M0L1pS922->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2275 = _M0MPC15array5Array2atGfE(_M0L1vS2277, _M0L1iS924);
      _M0L4e__eS2276 = _M0L1pS922->$17;
      _M0L6_2atmpS2274 = _M0L6_2atmpS2275 - _M0L4e__eS2276;
      _M0L6_2atmpS2270 = _M0L6_2atmpS2273 * _M0L6_2atmpS2274;
      _M0L7gsyn__eS2272 = _M0L1pS922->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2271
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2272, _M0L1iS924);
      _M0L6_2atmpS2259 = _M0L6_2atmpS2270 * _M0L6_2atmpS2271;
      _M0L2giS2269 = _M0L1pS922->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2264 = _M0MPC15array5Array2atGfE(_M0L2giS2269, _M0L1iS924);
      _M0L1vS2268 = _M0L1pS922->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2266 = _M0MPC15array5Array2atGfE(_M0L1vS2268, _M0L1iS924);
      _M0L4e__iS2267 = _M0L1pS922->$18;
      _M0L6_2atmpS2265 = _M0L6_2atmpS2266 - _M0L4e__iS2267;
      _M0L6_2atmpS2261 = _M0L6_2atmpS2264 * _M0L6_2atmpS2265;
      _M0L7gsyn__iS2263 = _M0L1pS922->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2262
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2263, _M0L1iS924);
      _M0L6_2atmpS2260 = _M0L6_2atmpS2261 * _M0L6_2atmpS2262;
      _M0L6_2atmpS2258 = _M0L6_2atmpS2259 + _M0L6_2atmpS2260;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2257, _M0L1iS924, _M0L6_2atmpS2258);
      _M0L6_2atmpS2279 = _M0L1iS924 + 1;
      _M0L1iS924 = _M0L6_2atmpS2279;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS913,
  float _M0L2dtS916
) {
  int32_t _M0L1nS912;
  int32_t _M0L7_2abindS914;
  int32_t _M0L1iS915;
  int32_t _M0L7_2abindS918;
  int32_t _M0L1iS919;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS912 = _M0L1pS913->$2;
  _M0L7_2abindS914 = 0;
  _M0L1iS915 = _M0L7_2abindS914;
  while (1) {
    if (_M0L1iS915 < _M0L1nS912) {
      struct _M0TPB5ArrayGfE* _M0L2heS2195 = _M0L1pS913->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2200 = _M0L1pS913->$11;
      float _M0L6_2atmpS2197;
      struct _M0TPB5ArrayGfE* _M0L3gluS2199;
      float _M0L6_2atmpS2198;
      float _M0L6_2atmpS2196;
      struct _M0TPB5ArrayGfE* _M0L2hiS2201;
      struct _M0TPB5ArrayGfE* _M0L2hiS2206;
      float _M0L6_2atmpS2203;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2205;
      float _M0L6_2atmpS2204;
      float _M0L6_2atmpS2202;
      struct _M0TPB5ArrayGfE* _M0L2geS2207;
      struct _M0TPB5ArrayGfE* _M0L2geS2219;
      float _M0L6_2atmpS2209;
      struct _M0TPB5ArrayGfE* _M0L2geS2218;
      float _M0L6_2atmpS2217;
      float _M0L6_2atmpS2215;
      float _M0L3tdeS2216;
      float _M0L6_2atmpS2212;
      struct _M0TPB5ArrayGfE* _M0L2heS2214;
      float _M0L6_2atmpS2213;
      float _M0L6_2atmpS2211;
      float _M0L6_2atmpS2210;
      float _M0L6_2atmpS2208;
      struct _M0TPB5ArrayGfE* _M0L2heS2220;
      struct _M0TPB5ArrayGfE* _M0L2heS2229;
      float _M0L6_2atmpS2222;
      struct _M0TPB5ArrayGfE* _M0L2heS2228;
      float _M0L6_2atmpS2227;
      float _M0L6_2atmpS2225;
      float _M0L3treS2226;
      float _M0L6_2atmpS2224;
      float _M0L6_2atmpS2223;
      float _M0L6_2atmpS2221;
      struct _M0TPB5ArrayGfE* _M0L2giS2230;
      struct _M0TPB5ArrayGfE* _M0L2giS2242;
      float _M0L6_2atmpS2232;
      struct _M0TPB5ArrayGfE* _M0L2giS2241;
      float _M0L6_2atmpS2240;
      float _M0L6_2atmpS2238;
      float _M0L3tdiS2239;
      float _M0L6_2atmpS2235;
      struct _M0TPB5ArrayGfE* _M0L2hiS2237;
      float _M0L6_2atmpS2236;
      float _M0L6_2atmpS2234;
      float _M0L6_2atmpS2233;
      float _M0L6_2atmpS2231;
      struct _M0TPB5ArrayGfE* _M0L2hiS2243;
      struct _M0TPB5ArrayGfE* _M0L2hiS2252;
      float _M0L6_2atmpS2245;
      struct _M0TPB5ArrayGfE* _M0L2hiS2251;
      float _M0L6_2atmpS2250;
      float _M0L6_2atmpS2248;
      float _M0L3triS2249;
      float _M0L6_2atmpS2247;
      float _M0L6_2atmpS2246;
      float _M0L6_2atmpS2244;
      int32_t _M0L6_2atmpS2253;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2197 = _M0MPC15array5Array2atGfE(_M0L2heS2200, _M0L1iS915);
      _M0L3gluS2199 = _M0L1pS913->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2198 = _M0MPC15array5Array2atGfE(_M0L3gluS2199, _M0L1iS915);
      _M0L6_2atmpS2196 = _M0L6_2atmpS2197 + _M0L6_2atmpS2198;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2195, _M0L1iS915, _M0L6_2atmpS2196);
      _M0L2hiS2201 = _M0L1pS913->$12;
      _M0L2hiS2206 = _M0L1pS913->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2203 = _M0MPC15array5Array2atGfE(_M0L2hiS2206, _M0L1iS915);
      _M0L4gabaS2205 = _M0L1pS913->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2204
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2205, _M0L1iS915);
      _M0L6_2atmpS2202 = _M0L6_2atmpS2203 + _M0L6_2atmpS2204;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2201, _M0L1iS915, _M0L6_2atmpS2202);
      _M0L2geS2207 = _M0L1pS913->$9;
      _M0L2geS2219 = _M0L1pS913->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2209 = _M0MPC15array5Array2atGfE(_M0L2geS2219, _M0L1iS915);
      _M0L2geS2218 = _M0L1pS913->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2217 = _M0MPC15array5Array2atGfE(_M0L2geS2218, _M0L1iS915);
      _M0L6_2atmpS2215 = -_M0L6_2atmpS2217;
      _M0L3tdeS2216 = _M0L1pS913->$20;
      _M0L6_2atmpS2212 = _M0L6_2atmpS2215 / _M0L3tdeS2216;
      _M0L2heS2214 = _M0L1pS913->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2213 = _M0MPC15array5Array2atGfE(_M0L2heS2214, _M0L1iS915);
      _M0L6_2atmpS2211 = _M0L6_2atmpS2212 + _M0L6_2atmpS2213;
      _M0L6_2atmpS2210 = _M0L2dtS916 * _M0L6_2atmpS2211;
      _M0L6_2atmpS2208 = _M0L6_2atmpS2209 + _M0L6_2atmpS2210;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2207, _M0L1iS915, _M0L6_2atmpS2208);
      _M0L2heS2220 = _M0L1pS913->$11;
      _M0L2heS2229 = _M0L1pS913->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2222 = _M0MPC15array5Array2atGfE(_M0L2heS2229, _M0L1iS915);
      _M0L2heS2228 = _M0L1pS913->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2227 = _M0MPC15array5Array2atGfE(_M0L2heS2228, _M0L1iS915);
      _M0L6_2atmpS2225 = -_M0L6_2atmpS2227;
      _M0L3treS2226 = _M0L1pS913->$19;
      _M0L6_2atmpS2224 = _M0L6_2atmpS2225 / _M0L3treS2226;
      _M0L6_2atmpS2223 = _M0L2dtS916 * _M0L6_2atmpS2224;
      _M0L6_2atmpS2221 = _M0L6_2atmpS2222 + _M0L6_2atmpS2223;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2220, _M0L1iS915, _M0L6_2atmpS2221);
      _M0L2giS2230 = _M0L1pS913->$10;
      _M0L2giS2242 = _M0L1pS913->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2232 = _M0MPC15array5Array2atGfE(_M0L2giS2242, _M0L1iS915);
      _M0L2giS2241 = _M0L1pS913->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2240 = _M0MPC15array5Array2atGfE(_M0L2giS2241, _M0L1iS915);
      _M0L6_2atmpS2238 = -_M0L6_2atmpS2240;
      _M0L3tdiS2239 = _M0L1pS913->$22;
      _M0L6_2atmpS2235 = _M0L6_2atmpS2238 / _M0L3tdiS2239;
      _M0L2hiS2237 = _M0L1pS913->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2236 = _M0MPC15array5Array2atGfE(_M0L2hiS2237, _M0L1iS915);
      _M0L6_2atmpS2234 = _M0L6_2atmpS2235 + _M0L6_2atmpS2236;
      _M0L6_2atmpS2233 = _M0L2dtS916 * _M0L6_2atmpS2234;
      _M0L6_2atmpS2231 = _M0L6_2atmpS2232 + _M0L6_2atmpS2233;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2230, _M0L1iS915, _M0L6_2atmpS2231);
      _M0L2hiS2243 = _M0L1pS913->$12;
      _M0L2hiS2252 = _M0L1pS913->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2245 = _M0MPC15array5Array2atGfE(_M0L2hiS2252, _M0L1iS915);
      _M0L2hiS2251 = _M0L1pS913->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2250 = _M0MPC15array5Array2atGfE(_M0L2hiS2251, _M0L1iS915);
      _M0L6_2atmpS2248 = -_M0L6_2atmpS2250;
      _M0L3triS2249 = _M0L1pS913->$21;
      _M0L6_2atmpS2247 = _M0L6_2atmpS2248 / _M0L3triS2249;
      _M0L6_2atmpS2246 = _M0L2dtS916 * _M0L6_2atmpS2247;
      _M0L6_2atmpS2244 = _M0L6_2atmpS2245 + _M0L6_2atmpS2246;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2243, _M0L1iS915, _M0L6_2atmpS2244);
      _M0L6_2atmpS2253 = _M0L1iS915 + 1;
      _M0L1iS915 = _M0L6_2atmpS2253;
      continue;
    }
    break;
  }
  _M0L7_2abindS918 = 0;
  _M0L1iS919 = _M0L7_2abindS918;
  while (1) {
    if (_M0L1iS919 < _M0L1nS912) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2254 = _M0L1pS913->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2255;
      int32_t _M0L6_2atmpS2256;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2254, _M0L1iS919, 0x0p+0f);
      _M0L4gabaS2255 = _M0L1pS913->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2255, _M0L1iS919, 0x0p+0f);
      _M0L6_2atmpS2256 = _M0L1iS919 + 1;
      _M0L1iS919 = _M0L6_2atmpS2256;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS898,
  float _M0L2dtS907
) {
  int32_t _M0L1nS897;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S899;
  float _M0L2tmS900;
  float _M0L2elS901;
  float _M0L1rS902;
  float _M0L2vtS903;
  float _M0L2vrS904;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2194;
  float _M0L11tabs__constS905;
  float _M0L6_2atmpS2193;
  int32_t _M0L11tabs__stepsS906;
  int32_t _M0L7_2abindS908;
  int32_t _M0L1iS909;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS897 = _M0L1pS898->$2;
  _M0L3p__S899 = _M0L1pS898->$0;
  _M0L2tmS900 = _M0L3p__S899->$2;
  _M0L2elS901 = _M0L3p__S899->$5;
  _M0L1rS902 = _M0L3p__S899->$6;
  _M0L2vtS903 = _M0L3p__S899->$3;
  _M0L2vrS904 = _M0L3p__S899->$4;
  _M0L5spikeS2194 = _M0L1pS898->$1;
  _M0L11tabs__constS905 = _M0L5spikeS2194->$0;
  _M0L6_2atmpS2193 = _M0L11tabs__constS905 / _M0L2dtS907;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS906 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2193);
  _M0L7_2abindS908 = 0;
  _M0L1iS909 = _M0L7_2abindS908;
  while (1) {
    if (_M0L1iS909 < _M0L1nS897) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2153 = _M0L1pS898->$6;
      int32_t _M0L6_2atmpS2152;
      struct _M0TPB5ArrayGfE* _M0L1vS2159;
      struct _M0TPB5ArrayGfE* _M0L1vS2180;
      float _M0L6_2atmpS2161;
      float _M0L6_2atmpS2163;
      struct _M0TPB5ArrayGfE* _M0L1vS2179;
      float _M0L6_2atmpS2178;
      float _M0L6_2atmpS2177;
      float _M0L6_2atmpS2169;
      struct _M0TPB5ArrayGfE* _M0L1wS2176;
      float _M0L6_2atmpS2175;
      float _M0L6_2atmpS2172;
      struct _M0TPB5ArrayGfE* _M0L1iS2174;
      float _M0L6_2atmpS2173;
      float _M0L6_2atmpS2171;
      float _M0L6_2atmpS2170;
      float _M0L6_2atmpS2165;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2168;
      float _M0L6_2atmpS2167;
      float _M0L6_2atmpS2166;
      float _M0L6_2atmpS2164;
      float _M0L6_2atmpS2162;
      float _M0L6_2atmpS2160;
      struct _M0TPB5ArrayGbE* _M0L4fireS2181;
      struct _M0TPB5ArrayGfE* _M0L1vS2184;
      float _M0L6_2atmpS2183;
      int32_t _M0L6_2atmpS2182;
      struct _M0TPB5ArrayGfE* _M0L1vS2185;
      struct _M0TPB5ArrayGbE* _M0L4fireS2187;
      float _M0L6_2atmpS2186;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2189;
      struct _M0TPB5ArrayGbE* _M0L4fireS2191;
      int32_t _M0L6_2atmpS2190;
      int32_t _M0L6_2atmpS2151;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2152
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2153, _M0L1iS909);
      if (_M0L6_2atmpS2152 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2154 = _M0L1pS898->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2155;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2158;
        int32_t _M0L6_2atmpS2157;
        int32_t _M0L6_2atmpS2156;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2154, _M0L1iS909, 0);
        _M0L4tabsS2155 = _M0L1pS898->$6;
        _M0L4tabsS2158 = _M0L1pS898->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2157
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2158, _M0L1iS909);
        _M0L6_2atmpS2156 = _M0L6_2atmpS2157 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2155, _M0L1iS909, _M0L6_2atmpS2156);
        goto join_910;
      }
      _M0L1vS2159 = _M0L1pS898->$3;
      _M0L1vS2180 = _M0L1pS898->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2161 = _M0MPC15array5Array2atGfE(_M0L1vS2180, _M0L1iS909);
      _M0L6_2atmpS2163 = _M0L2dtS907 / _M0L2tmS900;
      _M0L1vS2179 = _M0L1pS898->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2178 = _M0MPC15array5Array2atGfE(_M0L1vS2179, _M0L1iS909);
      _M0L6_2atmpS2177 = _M0L6_2atmpS2178 - _M0L2elS901;
      _M0L6_2atmpS2169 = -_M0L6_2atmpS2177;
      _M0L1wS2176 = _M0L1pS898->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2175 = _M0MPC15array5Array2atGfE(_M0L1wS2176, _M0L1iS909);
      _M0L6_2atmpS2172 = -_M0L6_2atmpS2175;
      _M0L1iS2174 = _M0L1pS898->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2173 = _M0MPC15array5Array2atGfE(_M0L1iS2174, _M0L1iS909);
      _M0L6_2atmpS2171 = _M0L6_2atmpS2172 + _M0L6_2atmpS2173;
      _M0L6_2atmpS2170 = _M0L1rS902 * _M0L6_2atmpS2171;
      _M0L6_2atmpS2165 = _M0L6_2atmpS2169 + _M0L6_2atmpS2170;
      _M0L9syn__currS2168 = _M0L1pS898->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2167
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2168, _M0L1iS909);
      _M0L6_2atmpS2166 = _M0L1rS902 * _M0L6_2atmpS2167;
      _M0L6_2atmpS2164 = _M0L6_2atmpS2165 - _M0L6_2atmpS2166;
      _M0L6_2atmpS2162 = _M0L6_2atmpS2163 * _M0L6_2atmpS2164;
      _M0L6_2atmpS2160 = _M0L6_2atmpS2161 + _M0L6_2atmpS2162;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2159, _M0L1iS909, _M0L6_2atmpS2160);
      _M0L4fireS2181 = _M0L1pS898->$5;
      _M0L1vS2184 = _M0L1pS898->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2183 = _M0MPC15array5Array2atGfE(_M0L1vS2184, _M0L1iS909);
      _M0L6_2atmpS2182 = _M0L6_2atmpS2183 > _M0L2vtS903;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2181, _M0L1iS909, _M0L6_2atmpS2182);
      _M0L1vS2185 = _M0L1pS898->$3;
      _M0L4fireS2187 = _M0L1pS898->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2187, _M0L1iS909)) {
        _M0L6_2atmpS2186 = _M0L2vrS904;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2188 = _M0L1pS898->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2186 = _M0MPC15array5Array2atGfE(_M0L1vS2188, _M0L1iS909);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2185, _M0L1iS909, _M0L6_2atmpS2186);
      _M0L4tabsS2189 = _M0L1pS898->$6;
      _M0L4fireS2191 = _M0L1pS898->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2191, _M0L1iS909)) {
        _M0L6_2atmpS2190 = _M0L11tabs__stepsS906;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2192 = _M0L1pS898->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2190
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2192, _M0L1iS909);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2189, _M0L1iS909, _M0L6_2atmpS2190);
      goto join_910;
      goto joinlet_2587;
      join_910:;
      _M0L6_2atmpS2151 = _M0L1iS909 + 1;
      _M0L1iS909 = _M0L6_2atmpS2151;
      continue;
      joinlet_2587:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS885,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS888,
  struct _M0TPB5ArrayGfE* _M0L7post__gS894
) {
  int32_t _M0L4rowsS884;
  int32_t _M0L7_2abindS886;
  int32_t _M0L1iS887;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS884 = _M0L1mS885->$0;
  _M0L7_2abindS886 = 0;
  _M0L1iS887 = _M0L7_2abindS886;
  while (1) {
    if (_M0L1iS887 < _M0L4rowsS884) {
      int32_t _M0L6_2atmpS2150;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS888, _M0L1iS887)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2149 = _M0L1mS885->$2;
        int32_t _M0L5startS889;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2147;
        int32_t _M0L6_2atmpS2148;
        int32_t _M0L3endS890;
        int32_t _M0L1kS891;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS889
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2149, _M0L1iS887);
        _M0L6rowptrS2147 = _M0L1mS885->$2;
        _M0L6_2atmpS2148 = _M0L1iS887 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS890
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2147, _M0L6_2atmpS2148);
        _M0L1kS891 = _M0L5startS889;
        while (1) {
          if (_M0L1kS891 < _M0L3endS890) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS2145 = _M0L1mS885->$3;
            int32_t _M0L9post__idxS892;
            struct _M0TPB5ArrayGfE* _M0L4valsS2144;
            float _M0L1wS893;
            float _M0L6_2atmpS2143;
            float _M0L6_2atmpS2142;
            int32_t _M0L6_2atmpS2146;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS892
            = _M0MPC15array5Array2atGiE(_M0L6colptrS2145, _M0L1kS891);
            _M0L4valsS2144 = _M0L1mS885->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS893
            = _M0MPC15array5Array2atGfE(_M0L4valsS2144, _M0L1kS891);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS2143
            = _M0MPC15array5Array2atGfE(_M0L7post__gS894, _M0L9post__idxS892);
            _M0L6_2atmpS2142 = _M0L6_2atmpS2143 + _M0L1wS893;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS894, _M0L9post__idxS892, _M0L6_2atmpS2142);
            _M0L6_2atmpS2146 = _M0L1kS891 + 1;
            _M0L1kS891 = _M0L6_2atmpS2146;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2150 = _M0L1iS887 + 1;
      _M0L1iS887 = _M0L6_2atmpS2150;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS871,
  int32_t _M0L1iS870,
  int32_t _M0L1jS876,
  float _M0L1vS877
) {
  int32_t _if__result_2590;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2141;
  int32_t _M0L5startS872;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2139;
  int32_t _M0L6_2atmpS2140;
  int32_t _M0L3endS873;
  struct _M0TPB8MutLocalGbE* _M0L5foundS874;
  int32_t _M0L1kS875;
  int32_t _M0L3valS2131;
  #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  if (_M0L1iS870 < 0) {
    _if__result_2590 = 1;
  } else {
    int32_t _M0L4rowsS2126 = _M0L1mS871->$0;
    _if__result_2590 = _M0L1iS870 >= _M0L4rowsS2126;
  }
  if (_if__result_2590) {
    return 0;
  }
  _M0L6rowptrS2141 = _M0L1mS871->$2;
  #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5startS872 = _M0MPC15array5Array2atGiE(_M0L6rowptrS2141, _M0L1iS870);
  _M0L6rowptrS2139 = _M0L1mS871->$2;
  _M0L6_2atmpS2140 = _M0L1iS870 + 1;
  #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L3endS873
  = _M0MPC15array5Array2atGiE(_M0L6rowptrS2139, _M0L6_2atmpS2140);
  _M0L5foundS874
  = (struct _M0TPB8MutLocalGbE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGbE));
  Moonbit_object_header(_M0L5foundS874)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5foundS874->$0 = 0;
  _M0L1kS875 = _M0L5startS872;
  while (1) {
    if (_M0L1kS875 < _M0L3endS873) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS2128 = _M0L1mS871->$3;
      int32_t _M0L6_2atmpS2127;
      int32_t _M0L6_2atmpS2130;
      #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2127
      = _M0MPC15array5Array2atGiE(_M0L6colptrS2128, _M0L1kS875);
      if (_M0L6_2atmpS2127 == _M0L1jS876) {
        struct _M0TPB5ArrayGfE* _M0L4valsS2129 = _M0L1mS871->$4;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGfE(_M0L4valsS2129, _M0L1kS875, _M0L1vS877);
        _M0L5foundS874->$0 = 1;
        break;
      }
      _M0L6_2atmpS2130 = _M0L1kS875 + 1;
      _M0L1kS875 = _M0L6_2atmpS2130;
      continue;
    }
    break;
  }
  _M0L3valS2131 = _M0L5foundS874->$0;
  moonbit_decref_cycle_free(_M0L5foundS874);
  if (!_M0L3valS2131) {
    struct _M0TPB5ArrayGiE* _M0L6colptrS2132 = _M0L1mS871->$3;
    struct _M0TPB5ArrayGfE* _M0L4valsS2133;
    int32_t _M0L7n__rowsS879;
    int32_t _M0L7_2abindS880;
    int32_t _M0L7_2abindS881;
    int32_t _M0L1rS882;
    #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGiE(_M0L6colptrS2132, _M0L3endS873, _M0L1jS876);
    _M0L4valsS2133 = _M0L1mS871->$4;
    #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGfE(_M0L4valsS2133, _M0L3endS873, _M0L1vS877);
    _M0L7n__rowsS879 = _M0L1mS871->$0;
    _M0L7_2abindS880 = _M0L1iS870 + 1;
    _M0L7_2abindS881 = _M0L7n__rowsS879 + 1;
    _M0L1rS882 = _M0L7_2abindS880;
    while (1) {
      if (_M0L1rS882 < _M0L7_2abindS881) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2134 = _M0L1mS871->$2;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2137 = _M0L1mS871->$2;
        int32_t _M0L6_2atmpS2136;
        int32_t _M0L6_2atmpS2135;
        int32_t _M0L6_2atmpS2138;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L6_2atmpS2136
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2137, _M0L1rS882);
        _M0L6_2atmpS2135 = _M0L6_2atmpS2136 + 1;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGiE(_M0L6rowptrS2134, _M0L1rS882, _M0L6_2atmpS2135);
        _M0L6_2atmpS2138 = _M0L1rS882 + 1;
        _M0L1rS882 = _M0L6_2atmpS2138;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(
  int32_t _M0L4rowsS868,
  int32_t _M0L4colsS869
) {
  int32_t _M0L6_2atmpS2125;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS867;
  int32_t* _M0L6_2atmpS2124;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2121;
  float* _M0L6_2atmpS2123;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2122;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2593;
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2125 = _M0L4rowsS868 + 1;
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS867 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2125, 0);
  _M0L6_2atmpS2124 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2121
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2121)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2121->$0 = _M0L6_2atmpS2124;
  _M0L6_2atmpS2121->$1 = 0;
  _M0L6_2atmpS2123 = moonbit_empty_float_array;
  _M0L6_2atmpS2122
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2122)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2122->$0 = _M0L6_2atmpS2123;
  _M0L6_2atmpS2122->$1 = 0;
  _block_2593
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2593)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_2593->$0 = _M0L4rowsS868;
  _block_2593->$1 = _M0L4colsS869;
  _block_2593->$2 = _M0L6rowptrS867;
  _block_2593->$3 = _M0L6_2atmpS2121;
  _block_2593->$4 = _M0L6_2atmpS2122;
  return _block_2593;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS865
) {
  struct _M0TUmmmmE* _M0L1sS864;
  uint64_t _M0L6_2atmpS2120;
  struct _M0TUmmmmE* _M0L1tS866;
  uint64_t _M0L6_2atmpS2116;
  uint64_t _M0L6_2atmpS2117;
  uint64_t _M0L6_2atmpS2118;
  uint64_t _M0L6_2atmpS2119;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2594;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS864 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS865);
  _M0L6_2atmpS2120 = _M0L1sS864->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS866 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2120);
  _M0L6_2atmpS2116 = _M0L1sS864->$0;
  _M0L6_2atmpS2117 = _M0L1sS864->$1;
  _M0L6_2atmpS2118 = _M0L1sS864->$2;
  moonbit_decref_cycle_free(_M0L1sS864);
  _M0L6_2atmpS2119 = _M0L1tS866->$0;
  moonbit_decref_cycle_free(_M0L1tS866);
  _block_2594
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2594)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2594->$0 = _M0L6_2atmpS2116;
  _block_2594->$1 = _M0L6_2atmpS2117;
  _block_2594->$2 = _M0L6_2atmpS2118;
  _block_2594->$3 = _M0L6_2atmpS2119;
  return _block_2594;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS856) {
  uint64_t _M0L2s1S855;
  uint64_t _M0L2z1S857;
  uint64_t _M0L2s2S858;
  uint64_t _M0L2z2S859;
  uint64_t _M0L2s3S860;
  uint64_t _M0L2z3S861;
  uint64_t _M0L2s4S862;
  uint64_t _M0L2z4S863;
  struct _M0TUmmmmE* _block_2595;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S855 = _M0L4seedS856 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S857 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S855);
  _M0L2s2S858 = _M0L2s1S855 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S859 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S858);
  _M0L2s3S860 = _M0L2s2S858 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S861 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S860);
  _M0L2s4S862 = _M0L2s3S860 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S863 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S862);
  _block_2595 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2595)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2595->$0 = _M0L2z1S857;
  _block_2595->$1 = _M0L2z2S859;
  _block_2595->$2 = _M0L2z3S861;
  _block_2595->$3 = _M0L2z4S863;
  return _block_2595;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS853) {
  uint64_t _M0L6_2atmpS2115;
  uint64_t _M0L6_2atmpS2114;
  uint64_t _M0L1zS852;
  uint64_t _M0L6_2atmpS2113;
  uint64_t _M0L6_2atmpS2112;
  uint64_t _M0L1zS854;
  uint64_t _M0L6_2atmpS2111;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2115 = _M0L1zS853 >> 30;
  _M0L6_2atmpS2114 = _M0L1zS853 ^ _M0L6_2atmpS2115;
  _M0L1zS852 = _M0L6_2atmpS2114 * 13787848793156543929ull;
  _M0L6_2atmpS2113 = _M0L1zS852 >> 27;
  _M0L6_2atmpS2112 = _M0L1zS852 ^ _M0L6_2atmpS2113;
  _M0L1zS854 = _M0L6_2atmpS2112 * 10723151780598845931ull;
  _M0L6_2atmpS2111 = _M0L1zS854 >> 31;
  return _M0L1zS854 ^ _M0L6_2atmpS2111;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS850,
  float _M0L2dtS851
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2103;
  struct _M0TPB5ArrayGfE* _M0L1tS2106;
  float _M0L6_2atmpS2105;
  float _M0L6_2atmpS2104;
  struct _M0TPB5ArrayGiE* _M0L2ttS2107;
  struct _M0TPB5ArrayGiE* _M0L2ttS2110;
  int32_t _M0L6_2atmpS2109;
  int32_t _M0L6_2atmpS2108;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2103 = _M0L1tS850->$0;
  _M0L1tS2106 = _M0L1tS850->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2105 = _M0MPC15array5Array2atGfE(_M0L1tS2106, 0);
  _M0L6_2atmpS2104 = _M0L6_2atmpS2105 + _M0L2dtS851;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS2103, 0, _M0L6_2atmpS2104);
  _M0L2ttS2107 = _M0L1tS850->$1;
  _M0L2ttS2110 = _M0L1tS850->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2109 = _M0MPC15array5Array2atGiE(_M0L2ttS2110, 0);
  _M0L6_2atmpS2108 = _M0L6_2atmpS2109 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS2107, 0, _M0L6_2atmpS2108);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS848,
  float _M0L1vS849
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS848->$2 = _M0L1vS849;
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS847
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2102;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2102 = _M0L1tS847->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS2102, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS2101;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2098;
  int32_t* _M0L6_2atmpS2100;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2099;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2596;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2101 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2101[0] = 0x0p+0f;
  _M0L6_2atmpS2098
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2098)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2098->$0 = _M0L6_2atmpS2101;
  _M0L6_2atmpS2098->$1 = 1;
  _M0L6_2atmpS2100 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS2100[0] = 0;
  _M0L6_2atmpS2099
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2099)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2099->$0 = _M0L6_2atmpS2100;
  _M0L6_2atmpS2099->$1 = 1;
  _block_2596
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2596)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 65, 0);
  _block_2596->$0 = _M0L6_2atmpS2098;
  _block_2596->$1 = _M0L6_2atmpS2099;
  _block_2596->$2 = 0x1p-3f;
  return _block_2596;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS845
) {
  uint32_t _M0L1uS844;
  uint32_t _M0L4bitsS846;
  double _M0L6_2atmpS2097;
  double _M0L6_2atmpS2096;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS844 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS845);
  _M0L4bitsS846 = _M0L1uS844 >> 8;
  _M0L6_2atmpS2097 = (double)_M0L4bitsS846;
  _M0L6_2atmpS2096 = _M0L6_2atmpS2097 * 0x1p-24;
  return (float)_M0L6_2atmpS2096;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS843
) {
  uint64_t _M0L1uS842;
  uint64_t _M0L6_2atmpS2095;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS842 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS843);
  _M0L6_2atmpS2095 = _M0L1uS842 >> 32;
  return (uint32_t)_M0L6_2atmpS2095;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS835
) {
  uint64_t _M0L2s0S834;
  uint64_t _M0L2s1S836;
  uint64_t _M0L2s2S837;
  uint64_t _M0L2s3S838;
  uint64_t _M0L3tmpS839;
  uint64_t _M0L6_2atmpS2094;
  uint64_t _M0L3resS840;
  uint64_t _M0L1tS841;
  uint64_t _M0L6_2atmpS2084;
  uint64_t _M0L6_2atmpS2085;
  uint64_t _M0L2s2S2087;
  uint64_t _M0L6_2atmpS2086;
  uint64_t _M0L2s3S2089;
  uint64_t _M0L6_2atmpS2088;
  uint64_t _M0L2s2S2091;
  uint64_t _M0L6_2atmpS2090;
  uint64_t _M0L2s3S2093;
  uint64_t _M0L6_2atmpS2092;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S834 = _M0L1rS835->$0;
  _M0L2s1S836 = _M0L1rS835->$1;
  _M0L2s2S837 = _M0L1rS835->$2;
  _M0L2s3S838 = _M0L1rS835->$3;
  _M0L3tmpS839 = _M0L2s0S834 + _M0L2s3S838;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2094 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS839, 23);
  _M0L3resS840 = _M0L6_2atmpS2094 + _M0L2s0S834;
  _M0L1tS841 = _M0L2s1S836 << 17;
  _M0L6_2atmpS2084 = _M0L2s2S837 ^ _M0L2s0S834;
  _M0L1rS835->$2 = _M0L6_2atmpS2084;
  _M0L6_2atmpS2085 = _M0L2s3S838 ^ _M0L2s1S836;
  _M0L1rS835->$3 = _M0L6_2atmpS2085;
  _M0L2s2S2087 = _M0L1rS835->$2;
  _M0L6_2atmpS2086 = _M0L2s1S836 ^ _M0L2s2S2087;
  _M0L1rS835->$1 = _M0L6_2atmpS2086;
  _M0L2s3S2089 = _M0L1rS835->$3;
  _M0L6_2atmpS2088 = _M0L2s0S834 ^ _M0L2s3S2089;
  _M0L1rS835->$0 = _M0L6_2atmpS2088;
  _M0L2s2S2091 = _M0L1rS835->$2;
  _M0L6_2atmpS2090 = _M0L2s2S2091 ^ _M0L1tS841;
  _M0L1rS835->$2 = _M0L6_2atmpS2090;
  _M0L2s3S2093 = _M0L1rS835->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2092 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2093, 45);
  _M0L1rS835->$3 = _M0L6_2atmpS2092;
  return _M0L3resS840;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS832, int32_t _M0L1kS833) {
  uint64_t _M0L6_2atmpS2081;
  int32_t _M0L6_2atmpS2083;
  uint64_t _M0L6_2atmpS2082;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2081 = _M0L1xS832 << (_M0L1kS833 & 63);
  _M0L6_2atmpS2083 = 64 - _M0L1kS833;
  _M0L6_2atmpS2082 = _M0L1xS832 >> (_M0L6_2atmpS2083 & 63);
  return _M0L6_2atmpS2081 | _M0L6_2atmpS2082;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13dump__summary(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS821
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS2080;
  int32_t _M0L1nS820;
  struct _M0TPB5ArrayGfE* _M0L4dataS2079;
  float _M0L6_2atmpS2078;
  struct _M0TPB8MutLocalGfE* _M0L2mnS823;
  struct _M0TPB5ArrayGfE* _M0L4dataS2077;
  float _M0L6_2atmpS2076;
  struct _M0TPB8MutLocalGfE* _M0L2mxS824;
  struct _M0TPB8MutLocalGfE* _M0L3sumS825;
  int32_t _M0L7_2abindS826;
  int32_t _M0L1iS827;
  float _M0L3valS2074;
  float _M0L6_2atmpS2075;
  float _M0L4meanS830;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS831;
  moonbit_string_t _M0L3symS2073;
  moonbit_string_t _M0L6_2atmpS2071;
  moonbit_string_t _M0L6_2atmpS2072;
  moonbit_string_t _M0L6_2atmpS2070;
  moonbit_string_t _M0L6_2atmpS2067;
  float _M0L3valS2069;
  moonbit_string_t _M0L6_2atmpS2068;
  moonbit_string_t _M0L6_2atmpS2066;
  moonbit_string_t _M0L6_2atmpS2063;
  float _M0L3valS2065;
  moonbit_string_t _M0L6_2atmpS2064;
  moonbit_string_t _M0L6_2atmpS2062;
  moonbit_string_t _M0L6_2atmpS2060;
  moonbit_string_t _M0L6_2atmpS2061;
  moonbit_string_t _M0L6_2atmpS2059;
  #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS2080 = _M0L1mS821->$2;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS820 = _M0MPC15array5Array6lengthGfE(_M0L4dataS2080);
  if (_M0L1nS820 == 0) {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS822;
    moonbit_string_t _M0L3symS2052;
    moonbit_string_t _M0L6_2atmpS2051;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L18_2astring__builderS822
    = _M0MPB13StringBuilder21StringBuilder_2einner(17);
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS822, (moonbit_string_t)moonbit_string_literal_12.data);
    _M0L3symS2052 = _M0L1mS821->$1;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS822, _M0L3symS2052);
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS822, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS2051
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS822);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS822);
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0FPB7printlnGsE(_M0L6_2atmpS2051);
    moonbit_decref_cycle_free(_M0L6_2atmpS2051);
    return 0;
  }
  _M0L4dataS2079 = _M0L1mS821->$2;
  #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2078 = _M0MPC15array5Array2atGfE(_M0L4dataS2079, 0);
  _M0L2mnS823
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mnS823)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mnS823->$0 = _M0L6_2atmpS2078;
  _M0L4dataS2077 = _M0L1mS821->$2;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2076 = _M0MPC15array5Array2atGfE(_M0L4dataS2077, 0);
  _M0L2mxS824
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mxS824)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mxS824->$0 = _M0L6_2atmpS2076;
  _M0L3sumS825
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L3sumS825)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3sumS825->$0 = 0x0p+0f;
  _M0L7_2abindS826 = 0;
  _M0L1iS827 = _M0L7_2abindS826;
  while (1) {
    if (_M0L1iS827 < _M0L1nS820) {
      struct _M0TPB5ArrayGfE* _M0L4dataS2057 = _M0L1mS821->$2;
      float _M0L1vS828;
      float _M0L3valS2053;
      float _M0L3valS2054;
      float _M0L3valS2056;
      float _M0L6_2atmpS2055;
      int32_t _M0L6_2atmpS2058;
      #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L1vS828 = _M0MPC15array5Array2atGfE(_M0L4dataS2057, _M0L1iS827);
      _M0L3valS2053 = _M0L2mnS823->$0;
      if (_M0L1vS828 < _M0L3valS2053) {
        _M0L2mnS823->$0 = _M0L1vS828;
      }
      _M0L3valS2054 = _M0L2mxS824->$0;
      if (_M0L1vS828 > _M0L3valS2054) {
        _M0L2mxS824->$0 = _M0L1vS828;
      }
      _M0L3valS2056 = _M0L3sumS825->$0;
      _M0L6_2atmpS2055 = _M0L3valS2056 + _M0L1vS828;
      _M0L3sumS825->$0 = _M0L6_2atmpS2055;
      _M0L6_2atmpS2058 = _M0L1iS827 + 1;
      _M0L1iS827 = _M0L6_2atmpS2058;
      continue;
    }
    break;
  }
  _M0L3valS2074 = _M0L3sumS825->$0;
  moonbit_decref_cycle_free(_M0L3sumS825);
  _M0L6_2atmpS2075 = (float)_M0L1nS820;
  _M0L4meanS830 = _M0L3valS2074 / _M0L6_2atmpS2075;
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L18_2astring__builderS831
  = _M0MPB13StringBuilder21StringBuilder_2einner(12);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS831, (moonbit_string_t)moonbit_string_literal_12.data);
  _M0L3symS2073 = _M0L1mS821->$1;
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS831, _M0L3symS2073);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS831, (moonbit_string_t)moonbit_string_literal_14.data);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2071
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS831);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS831);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2072 = _M0MPC13int3Int18to__string_2einner(_M0L1nS820, 10);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2070 = moonbit_add_string(_M0L6_2atmpS2071, _M0L6_2atmpS2072);
  moonbit_decref_cycle_free(_M0L6_2atmpS2072);
  moonbit_decref_cycle_free(_M0L6_2atmpS2071);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2067
  = moonbit_add_string(_M0L6_2atmpS2070, (moonbit_string_t)moonbit_string_literal_15.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2070);
  _M0L3valS2069 = _M0L2mnS823->$0;
  moonbit_decref_cycle_free(_M0L2mnS823);
  #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2068 = _M0IPC15float5FloatPB4Show10to__string(_M0L3valS2069);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2066 = moonbit_add_string(_M0L6_2atmpS2067, _M0L6_2atmpS2068);
  moonbit_decref_cycle_free(_M0L6_2atmpS2068);
  moonbit_decref_cycle_free(_M0L6_2atmpS2067);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2063
  = moonbit_add_string(_M0L6_2atmpS2066, (moonbit_string_t)moonbit_string_literal_16.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2066);
  _M0L3valS2065 = _M0L2mxS824->$0;
  moonbit_decref_cycle_free(_M0L2mxS824);
  #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2064 = _M0IPC15float5FloatPB4Show10to__string(_M0L3valS2065);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2062 = moonbit_add_string(_M0L6_2atmpS2063, _M0L6_2atmpS2064);
  moonbit_decref_cycle_free(_M0L6_2atmpS2064);
  moonbit_decref_cycle_free(_M0L6_2atmpS2063);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2060
  = moonbit_add_string(_M0L6_2atmpS2062, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2062);
  #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2061 = _M0IPC15float5FloatPB4Show10to__string(_M0L4meanS830);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2059 = moonbit_add_string(_M0L6_2atmpS2060, _M0L6_2atmpS2061);
  moonbit_decref_cycle_free(_M0L6_2atmpS2061);
  moonbit_decref_cycle_free(_M0L6_2atmpS2060);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2059);
  moonbit_decref_cycle_free(_M0L6_2atmpS2059);
  return 0;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS819) {
  double _M0L6_2atmpS2050;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2050 = (double)_M0L4selfS819;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2050);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS818) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS818 != _M0L4selfS818) {
    return 0;
  } else if (_M0L4selfS818 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS818 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS818;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS804,
  float _M0L4elemS806
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS803;
  int32_t _M0L1iS805;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS803 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS804);
  _M0L1iS805 = 0;
  while (1) {
    if (_M0L1iS805 < _M0L3lenS804) {
      float* _M0L3bufS2044 = _M0L3arrS803->$0;
      int32_t _M0L6_2atmpS2045;
      _M0L3bufS2044[_M0L1iS805] = _M0L4elemS806;
      _M0L6_2atmpS2045 = _M0L1iS805 + 1;
      _M0L1iS805 = _M0L6_2atmpS2045;
      continue;
    }
    break;
  }
  return _M0L3arrS803;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS809,
  int32_t _M0L4elemS811
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS808;
  int32_t _M0L1iS810;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS808 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS809);
  _M0L1iS810 = 0;
  while (1) {
    if (_M0L1iS810 < _M0L3lenS809) {
      uint8_t* _M0L3bufS2046 = _M0L3arrS808->$0;
      int32_t _M0L6_2atmpS2047;
      _M0L3bufS2046[_M0L1iS810] = _M0L4elemS811;
      _M0L6_2atmpS2047 = _M0L1iS810 + 1;
      _M0L1iS810 = _M0L6_2atmpS2047;
      continue;
    }
    break;
  }
  return _M0L3arrS808;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS814,
  int32_t _M0L4elemS816
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS813;
  int32_t _M0L1iS815;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS813 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS814);
  _M0L1iS815 = 0;
  while (1) {
    if (_M0L1iS815 < _M0L3lenS814) {
      int32_t* _M0L3bufS2048 = _M0L3arrS813->$0;
      int32_t _M0L6_2atmpS2049;
      _M0L3bufS2048[_M0L1iS815] = _M0L4elemS816;
      _M0L6_2atmpS2049 = _M0L1iS815 + 1;
      _M0L1iS815 = _M0L6_2atmpS2049;
      continue;
    }
    break;
  }
  return _M0L3arrS813;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS792,
  int32_t _M0L5indexS793,
  float _M0L5valueS794
) {
  int32_t _M0L3lenS791;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS791 = _M0L4selfS792->$1;
  if (_M0L5indexS793 >= 0 && _M0L5indexS793 < _M0L3lenS791) {
    float* _M0L6_2atmpS2041;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2041 = _M0MPC15array5Array6bufferGfE(_M0L4selfS792);
    _M0L6_2atmpS2041[_M0L5indexS793] = _M0L5valueS794;
    moonbit_decref_cycle_free(_M0L6_2atmpS2041);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS796,
  int32_t _M0L5indexS797,
  int32_t _M0L5valueS798
) {
  int32_t _M0L3lenS795;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS795 = _M0L4selfS796->$1;
  if (_M0L5indexS797 >= 0 && _M0L5indexS797 < _M0L3lenS795) {
    int32_t* _M0L6_2atmpS2042;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2042 = _M0MPC15array5Array6bufferGiE(_M0L4selfS796);
    _M0L6_2atmpS2042[_M0L5indexS797] = _M0L5valueS798;
    moonbit_decref_cycle_free(_M0L6_2atmpS2042);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS800,
  int32_t _M0L5indexS801,
  int32_t _M0L5valueS802
) {
  int32_t _M0L3lenS799;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS799 = _M0L4selfS800->$1;
  if (_M0L5indexS801 >= 0 && _M0L5indexS801 < _M0L3lenS799) {
    uint8_t* _M0L6_2atmpS2043;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2043 = _M0MPC15array5Array6bufferGbE(_M0L4selfS800);
    _M0L6_2atmpS2043[_M0L5indexS801] = _M0L5valueS802;
    moonbit_decref_cycle_free(_M0L6_2atmpS2043);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS782,
  int32_t _M0L5indexS781,
  int32_t _M0L5valueS784
) {
  int32_t _if__result_2601;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS781 >= 0) {
    int32_t _M0L3lenS2011 = _M0L4selfS782->$1;
    _if__result_2601 = _M0L5indexS781 <= _M0L3lenS2011;
  } else {
    _if__result_2601 = 0;
  }
  if (_if__result_2601) {
    int32_t _M0L3lenS2012 = _M0L4selfS782->$1;
    int32_t* _M0L6_2atmpS2014;
    int32_t _M0L6_2atmpS2013;
    int32_t* _M0L6_2atmpS2017;
    int32_t _M0L6_2atmpS2018;
    int32_t* _M0L6_2atmpS2019;
    int32_t _M0L3lenS2021;
    int32_t _M0L6_2atmpS2020;
    int32_t _M0L6lengthS783;
    int32_t* _M0L3bufS2022;
    int32_t _M0L6_2atmpS2023;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2014 = _M0MPC15array5Array6bufferGiE(_M0L4selfS782);
    _M0L6_2atmpS2013 = Moonbit_array_length(_M0L6_2atmpS2014);
    moonbit_decref_cycle_free(_M0L6_2atmpS2014);
    if (_M0L3lenS2012 == _M0L6_2atmpS2013) {
      int32_t _M0L3lenS2016 = _M0L4selfS782->$1;
      int32_t _M0L6_2atmpS2015 = _M0L3lenS2016 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGiE(_M0L4selfS782, _M0L6_2atmpS2015);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2017 = _M0MPC15array5Array6bufferGiE(_M0L4selfS782);
    _M0L6_2atmpS2018 = _M0L5indexS781 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2019 = _M0MPC15array5Array6bufferGiE(_M0L4selfS782);
    _M0L3lenS2021 = _M0L4selfS782->$1;
    _M0L6_2atmpS2020 = _M0L3lenS2021 - _M0L5indexS781;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L6_2atmpS2017, _M0L6_2atmpS2018, _M0L6_2atmpS2019, _M0L5indexS781, _M0L6_2atmpS2020);
    moonbit_decref_cycle_free(_M0L6_2atmpS2017);
    moonbit_decref_cycle_free(_M0L6_2atmpS2019);
    _M0L6lengthS783 = _M0L4selfS782->$1;
    _M0L3bufS2022 = _M0L4selfS782->$0;
    _M0L3bufS2022[_M0L5indexS781] = _M0L5valueS784;
    _M0L6_2atmpS2023 = _M0L6lengthS783 + 1;
    _M0L4selfS782->$1 = _M0L6_2atmpS2023;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS785;
    int32_t _M0L3lenS2025;
    moonbit_string_t _M0L6_2atmpS2024;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS785
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS785, (moonbit_string_t)moonbit_string_literal_18.data);
    _M0L3lenS2025 = _M0L4selfS782->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS785, _M0L3lenS2025);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS785, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS785, _M0L5indexS781);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2024
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS785);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS785);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS2024);
    moonbit_decref_cycle_free(_M0L6_2atmpS2024);
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS787,
  int32_t _M0L5indexS786,
  float _M0L5valueS789
) {
  int32_t _if__result_2602;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS786 >= 0) {
    int32_t _M0L3lenS2026 = _M0L4selfS787->$1;
    _if__result_2602 = _M0L5indexS786 <= _M0L3lenS2026;
  } else {
    _if__result_2602 = 0;
  }
  if (_if__result_2602) {
    int32_t _M0L3lenS2027 = _M0L4selfS787->$1;
    float* _M0L6_2atmpS2029;
    int32_t _M0L6_2atmpS2028;
    float* _M0L6_2atmpS2032;
    int32_t _M0L6_2atmpS2033;
    float* _M0L6_2atmpS2034;
    int32_t _M0L3lenS2036;
    int32_t _M0L6_2atmpS2035;
    int32_t _M0L6lengthS788;
    float* _M0L3bufS2037;
    int32_t _M0L6_2atmpS2038;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2029 = _M0MPC15array5Array6bufferGfE(_M0L4selfS787);
    _M0L6_2atmpS2028 = Moonbit_array_length(_M0L6_2atmpS2029);
    moonbit_decref_cycle_free(_M0L6_2atmpS2029);
    if (_M0L3lenS2027 == _M0L6_2atmpS2028) {
      int32_t _M0L3lenS2031 = _M0L4selfS787->$1;
      int32_t _M0L6_2atmpS2030 = _M0L3lenS2031 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGfE(_M0L4selfS787, _M0L6_2atmpS2030);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2032 = _M0MPC15array5Array6bufferGfE(_M0L4selfS787);
    _M0L6_2atmpS2033 = _M0L5indexS786 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2034 = _M0MPC15array5Array6bufferGfE(_M0L4selfS787);
    _M0L3lenS2036 = _M0L4selfS787->$1;
    _M0L6_2atmpS2035 = _M0L3lenS2036 - _M0L5indexS786;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L6_2atmpS2032, _M0L6_2atmpS2033, _M0L6_2atmpS2034, _M0L5indexS786, _M0L6_2atmpS2035);
    moonbit_decref_cycle_free(_M0L6_2atmpS2032);
    moonbit_decref_cycle_free(_M0L6_2atmpS2034);
    _M0L6lengthS788 = _M0L4selfS787->$1;
    _M0L3bufS2037 = _M0L4selfS787->$0;
    _M0L3bufS2037[_M0L5indexS786] = _M0L5valueS789;
    _M0L6_2atmpS2038 = _M0L6lengthS788 + 1;
    _M0L4selfS787->$1 = _M0L6_2atmpS2038;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS790;
    int32_t _M0L3lenS2040;
    moonbit_string_t _M0L6_2atmpS2039;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS790
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS790, (moonbit_string_t)moonbit_string_literal_18.data);
    _M0L3lenS2040 = _M0L4selfS787->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS790, _M0L3lenS2040);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS790, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS790, _M0L5indexS786);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2039
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS790);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS790);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS2039);
    moonbit_decref_cycle_free(_M0L6_2atmpS2039);
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS774) {
  int32_t _M0L3lenS773;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS773 = _M0L4selfS774->$1;
  if (_M0L3lenS773 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS775 = _M0L3lenS773 - 1;
    float* _M0L3bufS2009 = _M0L4selfS774->$0;
    float _M0L1vS776 = (float)_M0L3bufS2009[_M0L5indexS775];
    void* _block_2603;
    _M0L4selfS774->$1 = _M0L5indexS775;
    _block_2603
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_2603)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_2603)->$0 = _M0L1vS776;
    return _block_2603;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS778) {
  int32_t _M0L3lenS777;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS777 = _M0L4selfS778->$1;
  if (_M0L3lenS777 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS779 = _M0L3lenS777 - 1;
    int32_t* _M0L3bufS2010 = _M0L4selfS778->$0;
    int32_t _M0L1vS780 = (int32_t)_M0L3bufS2010[_M0L5indexS779];
    _M0L4selfS778->$1 = _M0L5indexS779;
    return (int64_t)_M0L1vS780;
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS759,
  int32_t _M0L5indexS760
) {
  int32_t _M0L3lenS758;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS758 = _M0L4selfS759->$1;
  if (_M0L5indexS760 >= 0 && _M0L5indexS760 < _M0L3lenS758) {
    float* _M0L6_2atmpS2004;
    float _result_2604;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2004 = _M0MPC15array5Array6bufferGfE(_M0L4selfS759);
    _result_2604 = (float)_M0L6_2atmpS2004[_M0L5indexS760];
    moonbit_decref_cycle_free(_M0L6_2atmpS2004);
    return _result_2604;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS762,
  int32_t _M0L5indexS763
) {
  int32_t _M0L3lenS761;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS761 = _M0L4selfS762->$1;
  if (_M0L5indexS763 >= 0 && _M0L5indexS763 < _M0L3lenS761) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS2005;
    struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS2513;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2005
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(_M0L4selfS762);
    _M0L6_2atmpS2513
    = (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L6_2atmpS2005[
        _M0L5indexS763
      ];
    if (_M0L6_2atmpS2513) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2513);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2005);
    return _M0L6_2atmpS2513;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS765,
  int32_t _M0L5indexS766
) {
  int32_t _M0L3lenS764;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS764 = _M0L4selfS765->$1;
  if (_M0L5indexS766 >= 0 && _M0L5indexS766 < _M0L3lenS764) {
    moonbit_string_t* _M0L6_2atmpS2006;
    moonbit_string_t _M0L6_2atmpS2514;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2006 = _M0MPC15array5Array6bufferGsE(_M0L4selfS765);
    _M0L6_2atmpS2514 = (moonbit_string_t)_M0L6_2atmpS2006[_M0L5indexS766];
    moonbit_incref_cycle_free(_M0L6_2atmpS2514);
    moonbit_decref_cycle_free(_M0L6_2atmpS2006);
    return _M0L6_2atmpS2514;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS768,
  int32_t _M0L5indexS769
) {
  int32_t _M0L3lenS767;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS767 = _M0L4selfS768->$1;
  if (_M0L5indexS769 >= 0 && _M0L5indexS769 < _M0L3lenS767) {
    int32_t* _M0L6_2atmpS2007;
    int32_t _result_2605;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2007 = _M0MPC15array5Array6bufferGiE(_M0L4selfS768);
    _result_2605 = (int32_t)_M0L6_2atmpS2007[_M0L5indexS769];
    moonbit_decref_cycle_free(_M0L6_2atmpS2007);
    return _result_2605;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS771,
  int32_t _M0L5indexS772
) {
  int32_t _M0L3lenS770;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS770 = _M0L4selfS771->$1;
  if (_M0L5indexS772 >= 0 && _M0L5indexS772 < _M0L3lenS770) {
    uint8_t* _M0L6_2atmpS2008;
    int32_t _result_2606;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2008 = _M0MPC15array5Array6bufferGbE(_M0L4selfS771);
    _result_2606 = (int32_t)_M0L6_2atmpS2008[_M0L5indexS772];
    moonbit_decref_cycle_free(_M0L6_2atmpS2008);
    return _result_2606;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS757) {
  moonbit_string_t _M0L6_2atmpS2003;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2003 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS757);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2003);
  moonbit_decref_cycle_free(_M0L6_2atmpS2003);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS756) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS756);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS741) {
  uint64_t _M0L4bitsS744;
  uint64_t _M0L6_2atmpS2002;
  uint64_t _M0L6_2atmpS2001;
  int32_t _M0L8ieeeSignS745;
  uint64_t _M0L12ieeeMantissaS746;
  uint64_t _M0L6_2atmpS2000;
  uint64_t _M0L6_2atmpS1999;
  int32_t _M0L12ieeeExponentS747;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS748;
  struct _M0TPB17FloatingDecimal64* _M0L1vS749;
  moonbit_string_t _result_2608;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS741 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
  }
  if (_M0L3valS741 >= -0x1p+53 && _M0L3valS741 <= 0x1p+53) {
    if (_M0L3valS741 >= -0x1p+31 && _M0L3valS741 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS742;
      double _M0L6_2atmpS1988;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS742 = _M0MPC16double6Double7to__int(_M0L3valS741);
      _M0L6_2atmpS1988 = (double)_M0L1iS742;
      if (_M0L6_2atmpS1988 == _M0L3valS741) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS742, 10);
      }
    } else {
      int64_t _M0L1iS743;
      double _M0L6_2atmpS1989;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS743 = _M0MPC16double6Double9to__int64(_M0L3valS741);
      _M0L6_2atmpS1989 = (double)_M0L1iS743;
      if (_M0L6_2atmpS1989 == _M0L3valS741) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS743, 10);
      }
    }
  }
  _M0L4bitsS744 = *(int64_t*)&_M0L3valS741;
  _M0L6_2atmpS2002 = _M0L4bitsS744 >> 63;
  _M0L6_2atmpS2001 = _M0L6_2atmpS2002 & 1ull;
  _M0L8ieeeSignS745 = _M0L6_2atmpS2001 != 0ull;
  _M0L12ieeeMantissaS746 = _M0L4bitsS744 & 4503599627370495ull;
  _M0L6_2atmpS2000 = _M0L4bitsS744 >> 52;
  _M0L6_2atmpS1999 = _M0L6_2atmpS2000 & 2047ull;
  _M0L12ieeeExponentS747 = (int32_t)_M0L6_2atmpS1999;
  if (
    _M0L12ieeeExponentS747 == 2047
    || _M0L12ieeeExponentS747 == 0 && _M0L12ieeeMantissaS746 == 0ull
  ) {
    int32_t _M0L6_2atmpS1990 = _M0L12ieeeExponentS747 != 0;
    int32_t _M0L6_2atmpS1991 = _M0L12ieeeMantissaS746 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS745, _M0L6_2atmpS1990, _M0L6_2atmpS1991);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS748
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS746, _M0L12ieeeExponentS747);
  if (_M0L7_2abindS748 == 0) {
    uint32_t _M0L6_2atmpS1992;
    if (_M0L7_2abindS748) {
      moonbit_decref_cycle_free(_M0L7_2abindS748);
    }
    _M0L6_2atmpS1992 = *(uint32_t*)&_M0L12ieeeExponentS747;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS749 = _M0FPB3d2d(_M0L12ieeeMantissaS746, _M0L6_2atmpS1992);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS750 = _M0L7_2abindS748;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS751 = _M0L7_2aSomeS750;
    struct _M0TPB17FloatingDecimal64* _M0L1xS752 = _M0L4_2afS751;
    while (1) {
      uint64_t _M0L8mantissaS1998 = _M0L1xS752->$0;
      uint64_t _M0L1qS753 = _M0L8mantissaS1998 / 10ull;
      uint64_t _M0L8mantissaS1996 = _M0L1xS752->$0;
      uint64_t _M0L6_2atmpS1997 = 10ull * _M0L1qS753;
      uint64_t _M0L1rS754 = _M0L8mantissaS1996 - _M0L6_2atmpS1997;
      int32_t _M0L8exponentS1995;
      int32_t _M0L6_2atmpS1994;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1993;
      if (_M0L1rS754 != 0ull) {
        _M0L1vS749 = _M0L1xS752;
        break;
      }
      _M0L8exponentS1995 = _M0L1xS752->$1;
      moonbit_decref_cycle_free(_M0L1xS752);
      _M0L6_2atmpS1994 = _M0L8exponentS1995 + 1;
      _M0L6_2atmpS1993
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1993)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1993->$0 = _M0L1qS753;
      _M0L6_2atmpS1993->$1 = _M0L6_2atmpS1994;
      _M0L1xS752 = _M0L6_2atmpS1993;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2608 = _M0FPB9to__chars(_M0L1vS749, _M0L8ieeeSignS745);
  moonbit_decref_cycle_free(_M0L1vS749);
  return _result_2608;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS736,
  int32_t _M0L12ieeeExponentS738
) {
  uint64_t _M0L2m2S735;
  int32_t _M0L6_2atmpS1987;
  int32_t _M0L2e2S737;
  int32_t _M0L6_2atmpS1986;
  uint64_t _M0L6_2atmpS1985;
  uint64_t _M0L4maskS739;
  uint64_t _M0L8fractionS740;
  int32_t _M0L6_2atmpS1984;
  uint64_t _M0L6_2atmpS1983;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1982;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S735 = 4503599627370496ull | _M0L12ieeeMantissaS736;
  _M0L6_2atmpS1987 = _M0L12ieeeExponentS738 - 1023;
  _M0L2e2S737 = _M0L6_2atmpS1987 - 52;
  if (_M0L2e2S737 > 0) {
    return 0;
  }
  if (_M0L2e2S737 < -52) {
    return 0;
  }
  _M0L6_2atmpS1986 = -_M0L2e2S737;
  _M0L6_2atmpS1985 = 1ull << (_M0L6_2atmpS1986 & 63);
  _M0L4maskS739 = _M0L6_2atmpS1985 - 1ull;
  _M0L8fractionS740 = _M0L2m2S735 & _M0L4maskS739;
  if (_M0L8fractionS740 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1984 = -_M0L2e2S737;
  _M0L6_2atmpS1983 = _M0L2m2S735 >> (_M0L6_2atmpS1984 & 63);
  _M0L6_2atmpS1982
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1982)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1982->$0 = _M0L6_2atmpS1983;
  _M0L6_2atmpS1982->$1 = 0;
  return _M0L6_2atmpS1982;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS703,
  int32_t _M0L4signS701
) {
  moonbit_bytes_t _M0L6resultS699;
  int32_t _M0Lm5indexS700;
  uint64_t _M0L6outputS702;
  int32_t _M0L7olengthS704;
  int32_t _M0L8exponentS1981;
  int32_t _M0L6_2atmpS1980;
  int32_t _M0Lm3expS705;
  int32_t _M0L6_2atmpS1979;
  int32_t _M0L6_2atmpS1977;
  int32_t _M0L18scientificNotationS706;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS699 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS700 = 0;
  if (_M0L4signS701) {
    int32_t _M0L6_2atmpS1851 = _M0Lm5indexS700;
    int32_t _M0L6_2atmpS1852;
    if (
      _M0L6_2atmpS1851 < 0
      || _M0L6_2atmpS1851 >= Moonbit_array_length(_M0L6resultS699)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS699[_M0L6_2atmpS1851] = 45;
    _M0L6_2atmpS1852 = _M0Lm5indexS700;
    _M0Lm5indexS700 = _M0L6_2atmpS1852 + 1;
  }
  _M0L6outputS702 = _M0L1vS703->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS704 = _M0FPB17decimal__length17(_M0L6outputS702);
  _M0L8exponentS1981 = _M0L1vS703->$1;
  _M0L6_2atmpS1980 = _M0L8exponentS1981 + _M0L7olengthS704;
  _M0Lm3expS705 = _M0L6_2atmpS1980 - 1;
  _M0L6_2atmpS1979 = _M0Lm3expS705;
  if (_M0L6_2atmpS1979 >= -6) {
    int32_t _M0L6_2atmpS1978 = _M0Lm3expS705;
    _M0L6_2atmpS1977 = _M0L6_2atmpS1978 < 21;
  } else {
    _M0L6_2atmpS1977 = 0;
  }
  _M0L18scientificNotationS706 = !_M0L6_2atmpS1977;
  if (_M0L18scientificNotationS706) {
    int32_t _M0L7_2abindS707 = _M0L7olengthS704 - 1;
    uint64_t _M0L6outputS708;
    int32_t _M0L1iS709 = 0;
    uint64_t _M0L6outputS710 = _M0L6outputS702;
    int32_t _M0L6_2atmpS1853;
    int32_t _M0L6_2atmpS1857;
    int32_t _M0L6_2atmpS1856;
    int32_t _M0L6_2atmpS1855;
    int32_t _M0L6_2atmpS1854;
    int32_t _M0L6_2atmpS1861;
    int32_t _M0L6_2atmpS1862;
    int32_t _M0L6_2atmpS1863;
    int32_t _M0L6_2atmpS1864;
    int32_t _M0L6_2atmpS1865;
    int32_t _M0L6_2atmpS1871;
    int32_t _M0L6_2atmpS1904;
    moonbit_string_t _result_2610;
    while (1) {
      if (_M0L1iS709 < _M0L7_2abindS707) {
        uint64_t _M0L1cS711 = _M0L6outputS710 % 10ull;
        int32_t _M0L6_2atmpS1910 = _M0Lm5indexS700;
        int32_t _M0L6_2atmpS1909 = _M0L6_2atmpS1910 + _M0L7olengthS704;
        int32_t _M0L6_2atmpS1905 = _M0L6_2atmpS1909 - _M0L1iS709;
        int32_t _M0L6_2atmpS1908 = (int32_t)_M0L1cS711;
        int32_t _M0L6_2atmpS1907 = 48 + _M0L6_2atmpS1908;
        int32_t _M0L6_2atmpS1906 = _M0L6_2atmpS1907 & 0xff;
        int32_t _M0L6_2atmpS1911;
        uint64_t _M0L6_2atmpS1912;
        if (
          _M0L6_2atmpS1905 < 0
          || _M0L6_2atmpS1905 >= Moonbit_array_length(_M0L6resultS699)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS699[_M0L6_2atmpS1905] = _M0L6_2atmpS1906;
        _M0L6_2atmpS1911 = _M0L1iS709 + 1;
        _M0L6_2atmpS1912 = _M0L6outputS710 / 10ull;
        _M0L1iS709 = _M0L6_2atmpS1911;
        _M0L6outputS710 = _M0L6_2atmpS1912;
        continue;
      } else {
        _M0L6outputS708 = _M0L6outputS710;
      }
      break;
    }
    _M0L6_2atmpS1853 = _M0Lm5indexS700;
    _M0L6_2atmpS1857 = (int32_t)_M0L6outputS708;
    _M0L6_2atmpS1856 = _M0L6_2atmpS1857 % 10;
    _M0L6_2atmpS1855 = 48 + _M0L6_2atmpS1856;
    _M0L6_2atmpS1854 = _M0L6_2atmpS1855 & 0xff;
    if (
      _M0L6_2atmpS1853 < 0
      || _M0L6_2atmpS1853 >= Moonbit_array_length(_M0L6resultS699)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS699[_M0L6_2atmpS1853] = _M0L6_2atmpS1854;
    if (_M0L7olengthS704 > 1) {
      int32_t _M0L6_2atmpS1859 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1858 = _M0L6_2atmpS1859 + 1;
      if (
        _M0L6_2atmpS1858 < 0
        || _M0L6_2atmpS1858 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1858] = 46;
    } else {
      int32_t _M0L6_2atmpS1860 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1860 - 1;
    }
    _M0L6_2atmpS1861 = _M0Lm5indexS700;
    _M0L6_2atmpS1862 = _M0L7olengthS704 + 1;
    _M0Lm5indexS700 = _M0L6_2atmpS1861 + _M0L6_2atmpS1862;
    _M0L6_2atmpS1863 = _M0Lm5indexS700;
    if (
      _M0L6_2atmpS1863 < 0
      || _M0L6_2atmpS1863 >= Moonbit_array_length(_M0L6resultS699)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS699[_M0L6_2atmpS1863] = 101;
    _M0L6_2atmpS1864 = _M0Lm5indexS700;
    _M0Lm5indexS700 = _M0L6_2atmpS1864 + 1;
    _M0L6_2atmpS1865 = _M0Lm3expS705;
    if (_M0L6_2atmpS1865 < 0) {
      int32_t _M0L6_2atmpS1866 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1867;
      int32_t _M0L6_2atmpS1868;
      if (
        _M0L6_2atmpS1866 < 0
        || _M0L6_2atmpS1866 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1866] = 45;
      _M0L6_2atmpS1867 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1867 + 1;
      _M0L6_2atmpS1868 = _M0Lm3expS705;
      _M0Lm3expS705 = -_M0L6_2atmpS1868;
    } else {
      int32_t _M0L6_2atmpS1869 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1870;
      if (
        _M0L6_2atmpS1869 < 0
        || _M0L6_2atmpS1869 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1869] = 43;
      _M0L6_2atmpS1870 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1870 + 1;
    }
    _M0L6_2atmpS1871 = _M0Lm3expS705;
    if (_M0L6_2atmpS1871 >= 100) {
      int32_t _M0L6_2atmpS1887 = _M0Lm3expS705;
      int32_t _M0L1aS713 = _M0L6_2atmpS1887 / 100;
      int32_t _M0L6_2atmpS1886 = _M0Lm3expS705;
      int32_t _M0L6_2atmpS1885 = _M0L6_2atmpS1886 / 10;
      int32_t _M0L1bS714 = _M0L6_2atmpS1885 % 10;
      int32_t _M0L6_2atmpS1884 = _M0Lm3expS705;
      int32_t _M0L1cS715 = _M0L6_2atmpS1884 % 10;
      int32_t _M0L6_2atmpS1872 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1874 = 48 + _M0L1aS713;
      int32_t _M0L6_2atmpS1873 = _M0L6_2atmpS1874 & 0xff;
      int32_t _M0L6_2atmpS1878;
      int32_t _M0L6_2atmpS1875;
      int32_t _M0L6_2atmpS1877;
      int32_t _M0L6_2atmpS1876;
      int32_t _M0L6_2atmpS1882;
      int32_t _M0L6_2atmpS1879;
      int32_t _M0L6_2atmpS1881;
      int32_t _M0L6_2atmpS1880;
      int32_t _M0L6_2atmpS1883;
      if (
        _M0L6_2atmpS1872 < 0
        || _M0L6_2atmpS1872 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1872] = _M0L6_2atmpS1873;
      _M0L6_2atmpS1878 = _M0Lm5indexS700;
      _M0L6_2atmpS1875 = _M0L6_2atmpS1878 + 1;
      _M0L6_2atmpS1877 = 48 + _M0L1bS714;
      _M0L6_2atmpS1876 = _M0L6_2atmpS1877 & 0xff;
      if (
        _M0L6_2atmpS1875 < 0
        || _M0L6_2atmpS1875 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1875] = _M0L6_2atmpS1876;
      _M0L6_2atmpS1882 = _M0Lm5indexS700;
      _M0L6_2atmpS1879 = _M0L6_2atmpS1882 + 2;
      _M0L6_2atmpS1881 = 48 + _M0L1cS715;
      _M0L6_2atmpS1880 = _M0L6_2atmpS1881 & 0xff;
      if (
        _M0L6_2atmpS1879 < 0
        || _M0L6_2atmpS1879 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1879] = _M0L6_2atmpS1880;
      _M0L6_2atmpS1883 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1883 + 3;
    } else {
      int32_t _M0L6_2atmpS1888 = _M0Lm3expS705;
      if (_M0L6_2atmpS1888 >= 10) {
        int32_t _M0L6_2atmpS1898 = _M0Lm3expS705;
        int32_t _M0L1aS716 = _M0L6_2atmpS1898 / 10;
        int32_t _M0L6_2atmpS1897 = _M0Lm3expS705;
        int32_t _M0L1bS717 = _M0L6_2atmpS1897 % 10;
        int32_t _M0L6_2atmpS1889 = _M0Lm5indexS700;
        int32_t _M0L6_2atmpS1891 = 48 + _M0L1aS716;
        int32_t _M0L6_2atmpS1890 = _M0L6_2atmpS1891 & 0xff;
        int32_t _M0L6_2atmpS1895;
        int32_t _M0L6_2atmpS1892;
        int32_t _M0L6_2atmpS1894;
        int32_t _M0L6_2atmpS1893;
        int32_t _M0L6_2atmpS1896;
        if (
          _M0L6_2atmpS1889 < 0
          || _M0L6_2atmpS1889 >= Moonbit_array_length(_M0L6resultS699)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS699[_M0L6_2atmpS1889] = _M0L6_2atmpS1890;
        _M0L6_2atmpS1895 = _M0Lm5indexS700;
        _M0L6_2atmpS1892 = _M0L6_2atmpS1895 + 1;
        _M0L6_2atmpS1894 = 48 + _M0L1bS717;
        _M0L6_2atmpS1893 = _M0L6_2atmpS1894 & 0xff;
        if (
          _M0L6_2atmpS1892 < 0
          || _M0L6_2atmpS1892 >= Moonbit_array_length(_M0L6resultS699)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS699[_M0L6_2atmpS1892] = _M0L6_2atmpS1893;
        _M0L6_2atmpS1896 = _M0Lm5indexS700;
        _M0Lm5indexS700 = _M0L6_2atmpS1896 + 2;
      } else {
        int32_t _M0L6_2atmpS1899 = _M0Lm5indexS700;
        int32_t _M0L6_2atmpS1902 = _M0Lm3expS705;
        int32_t _M0L6_2atmpS1901 = 48 + _M0L6_2atmpS1902;
        int32_t _M0L6_2atmpS1900 = _M0L6_2atmpS1901 & 0xff;
        int32_t _M0L6_2atmpS1903;
        if (
          _M0L6_2atmpS1899 < 0
          || _M0L6_2atmpS1899 >= Moonbit_array_length(_M0L6resultS699)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS699[_M0L6_2atmpS1899] = _M0L6_2atmpS1900;
        _M0L6_2atmpS1903 = _M0Lm5indexS700;
        _M0Lm5indexS700 = _M0L6_2atmpS1903 + 1;
      }
    }
    _M0L6_2atmpS1904 = _M0Lm5indexS700;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2610
    = _M0FPB19string__from__bytes(_M0L6resultS699, 0, _M0L6_2atmpS1904);
    moonbit_decref_cycle_free(_M0L6resultS699);
    return _result_2610;
  } else {
    int32_t _M0L6_2atmpS1913 = _M0Lm3expS705;
    int32_t _M0L6_2atmpS1976;
    moonbit_string_t _result_2616;
    if (_M0L6_2atmpS1913 < 0) {
      int32_t _M0L6_2atmpS1914 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1916;
      int32_t _M0L6_2atmpS1915;
      int32_t _M0L6_2atmpS1917;
      int32_t _M0L1iS718;
      int32_t _M0L6_2atmpS1932;
      int32_t _M0L6_2atmpS1934;
      int32_t _M0L6_2atmpS1933;
      int32_t _M0L7currentS720;
      int32_t _M0L1iS721;
      uint64_t _M0L6outputS722;
      if (
        _M0L6_2atmpS1914 < 0
        || _M0L6_2atmpS1914 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1914] = 48;
      _M0L6_2atmpS1916 = _M0Lm5indexS700;
      _M0L6_2atmpS1915 = _M0L6_2atmpS1916 + 1;
      if (
        _M0L6_2atmpS1915 < 0
        || _M0L6_2atmpS1915 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1915] = 46;
      _M0L6_2atmpS1917 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1917 + 2;
      _M0L1iS718 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1918 = _M0Lm3expS705;
        if (_M0L1iS718 > _M0L6_2atmpS1918) {
          int32_t _M0L6_2atmpS1921 = _M0Lm5indexS700;
          int32_t _M0L6_2atmpS1920 = _M0L6_2atmpS1921 - _M0L1iS718;
          int32_t _M0L6_2atmpS1919 = _M0L6_2atmpS1920 - 1;
          int32_t _M0L6_2atmpS1922;
          if (
            _M0L6_2atmpS1919 < 0
            || _M0L6_2atmpS1919 >= Moonbit_array_length(_M0L6resultS699)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS699[_M0L6_2atmpS1919] = 48;
          _M0L6_2atmpS1922 = _M0L1iS718 - 1;
          _M0L1iS718 = _M0L6_2atmpS1922;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1932 = _M0Lm5indexS700;
      _M0L6_2atmpS1934 = _M0Lm3expS705;
      _M0L6_2atmpS1933 = -1 - _M0L6_2atmpS1934;
      _M0L7currentS720 = _M0L6_2atmpS1932 + _M0L6_2atmpS1933;
      _M0L1iS721 = 0;
      _M0L6outputS722 = _M0L6outputS702;
      while (1) {
        if (_M0L1iS721 < _M0L7olengthS704) {
          int32_t _M0L6_2atmpS1929 = _M0L7currentS720 + _M0L7olengthS704;
          int32_t _M0L6_2atmpS1928 = _M0L6_2atmpS1929 - _M0L1iS721;
          int32_t _M0L6_2atmpS1923 = _M0L6_2atmpS1928 - 1;
          uint64_t _M0L6_2atmpS1927 = _M0L6outputS722 % 10ull;
          int32_t _M0L6_2atmpS1926 = (int32_t)_M0L6_2atmpS1927;
          int32_t _M0L6_2atmpS1925 = 48 + _M0L6_2atmpS1926;
          int32_t _M0L6_2atmpS1924 = _M0L6_2atmpS1925 & 0xff;
          int32_t _M0L6_2atmpS1930;
          uint64_t _M0L6_2atmpS1931;
          if (
            _M0L6_2atmpS1923 < 0
            || _M0L6_2atmpS1923 >= Moonbit_array_length(_M0L6resultS699)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS699[_M0L6_2atmpS1923] = _M0L6_2atmpS1924;
          _M0L6_2atmpS1930 = _M0L1iS721 + 1;
          _M0L6_2atmpS1931 = _M0L6outputS722 / 10ull;
          _M0L1iS721 = _M0L6_2atmpS1930;
          _M0L6outputS722 = _M0L6_2atmpS1931;
          continue;
        }
        break;
      }
      _M0Lm5indexS700 = _M0L7currentS720 + _M0L7olengthS704;
    } else {
      int32_t _M0L6_2atmpS1936 = _M0Lm3expS705;
      int32_t _M0L6_2atmpS1935 = _M0L6_2atmpS1936 + 1;
      if (_M0L6_2atmpS1935 >= _M0L7olengthS704) {
        int32_t _M0L1iS724 = 0;
        uint64_t _M0L6outputS725 = _M0L6outputS702;
        int32_t _M0L6_2atmpS1947;
        int32_t _M0L6_2atmpS1952;
        int32_t _M0L7_2abindS727;
        int32_t _M0L1iS728;
        int32_t _M0L6_2atmpS1953;
        int32_t _M0L6_2atmpS1956;
        int32_t _M0L6_2atmpS1955;
        int32_t _M0L6_2atmpS1954;
        while (1) {
          if (_M0L1iS724 < _M0L7olengthS704) {
            int32_t _M0L6_2atmpS1944 = _M0Lm5indexS700;
            int32_t _M0L6_2atmpS1943 = _M0L6_2atmpS1944 + _M0L7olengthS704;
            int32_t _M0L6_2atmpS1942 = _M0L6_2atmpS1943 - _M0L1iS724;
            int32_t _M0L6_2atmpS1937 = _M0L6_2atmpS1942 - 1;
            uint64_t _M0L6_2atmpS1941 = _M0L6outputS725 % 10ull;
            int32_t _M0L6_2atmpS1940 = (int32_t)_M0L6_2atmpS1941;
            int32_t _M0L6_2atmpS1939 = 48 + _M0L6_2atmpS1940;
            int32_t _M0L6_2atmpS1938 = _M0L6_2atmpS1939 & 0xff;
            int32_t _M0L6_2atmpS1945;
            uint64_t _M0L6_2atmpS1946;
            if (
              _M0L6_2atmpS1937 < 0
              || _M0L6_2atmpS1937 >= Moonbit_array_length(_M0L6resultS699)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS699[_M0L6_2atmpS1937] = _M0L6_2atmpS1938;
            _M0L6_2atmpS1945 = _M0L1iS724 + 1;
            _M0L6_2atmpS1946 = _M0L6outputS725 / 10ull;
            _M0L1iS724 = _M0L6_2atmpS1945;
            _M0L6outputS725 = _M0L6_2atmpS1946;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1947 = _M0Lm5indexS700;
        _M0Lm5indexS700 = _M0L6_2atmpS1947 + _M0L7olengthS704;
        _M0L6_2atmpS1952 = _M0Lm3expS705;
        _M0L7_2abindS727 = _M0L6_2atmpS1952 + 1;
        _M0L1iS728 = _M0L7olengthS704;
        while (1) {
          if (_M0L1iS728 < _M0L7_2abindS727) {
            int32_t _M0L6_2atmpS1950 = _M0Lm5indexS700;
            int32_t _M0L6_2atmpS1949 = _M0L6_2atmpS1950 + _M0L1iS728;
            int32_t _M0L6_2atmpS1948 = _M0L6_2atmpS1949 - _M0L7olengthS704;
            int32_t _M0L6_2atmpS1951;
            if (
              _M0L6_2atmpS1948 < 0
              || _M0L6_2atmpS1948 >= Moonbit_array_length(_M0L6resultS699)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS699[_M0L6_2atmpS1948] = 48;
            _M0L6_2atmpS1951 = _M0L1iS728 + 1;
            _M0L1iS728 = _M0L6_2atmpS1951;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1953 = _M0Lm5indexS700;
        _M0L6_2atmpS1956 = _M0Lm3expS705;
        _M0L6_2atmpS1955 = _M0L6_2atmpS1956 + 1;
        _M0L6_2atmpS1954 = _M0L6_2atmpS1955 - _M0L7olengthS704;
        _M0Lm5indexS700 = _M0L6_2atmpS1953 + _M0L6_2atmpS1954;
      } else {
        int32_t _M0L6_2atmpS1973 = _M0Lm5indexS700;
        int32_t _M0L6_2atmpS1972 = _M0L6_2atmpS1973 + 1;
        int32_t _M0L1iS730 = 0;
        int32_t _M0L7currentS731 = _M0L6_2atmpS1972;
        uint64_t _M0L6outputS732 = _M0L6outputS702;
        int32_t _M0L6_2atmpS1974;
        int32_t _M0L6_2atmpS1975;
        while (1) {
          if (_M0L1iS730 < _M0L7olengthS704) {
            int32_t _M0L6_2atmpS1968 = _M0L7olengthS704 - _M0L1iS730;
            int32_t _M0L6_2atmpS1966 = _M0L6_2atmpS1968 - 1;
            int32_t _M0L6_2atmpS1967 = _M0Lm3expS705;
            int32_t _M0L7currentS733;
            int32_t _M0L6_2atmpS1963;
            int32_t _M0L6_2atmpS1962;
            int32_t _M0L6_2atmpS1957;
            uint64_t _M0L6_2atmpS1961;
            int32_t _M0L6_2atmpS1960;
            int32_t _M0L6_2atmpS1959;
            int32_t _M0L6_2atmpS1958;
            int32_t _M0L6_2atmpS1964;
            uint64_t _M0L6_2atmpS1965;
            if (_M0L6_2atmpS1966 == _M0L6_2atmpS1967) {
              int32_t _M0L6_2atmpS1971 = _M0L7currentS731 + _M0L7olengthS704;
              int32_t _M0L6_2atmpS1970 = _M0L6_2atmpS1971 - _M0L1iS730;
              int32_t _M0L6_2atmpS1969 = _M0L6_2atmpS1970 - 1;
              if (
                _M0L6_2atmpS1969 < 0
                || _M0L6_2atmpS1969 >= Moonbit_array_length(_M0L6resultS699)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS699[_M0L6_2atmpS1969] = 46;
              _M0L7currentS733 = _M0L7currentS731 - 1;
            } else {
              _M0L7currentS733 = _M0L7currentS731;
            }
            _M0L6_2atmpS1963 = _M0L7currentS733 + _M0L7olengthS704;
            _M0L6_2atmpS1962 = _M0L6_2atmpS1963 - _M0L1iS730;
            _M0L6_2atmpS1957 = _M0L6_2atmpS1962 - 1;
            _M0L6_2atmpS1961 = _M0L6outputS732 % 10ull;
            _M0L6_2atmpS1960 = (int32_t)_M0L6_2atmpS1961;
            _M0L6_2atmpS1959 = 48 + _M0L6_2atmpS1960;
            _M0L6_2atmpS1958 = _M0L6_2atmpS1959 & 0xff;
            if (
              _M0L6_2atmpS1957 < 0
              || _M0L6_2atmpS1957 >= Moonbit_array_length(_M0L6resultS699)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS699[_M0L6_2atmpS1957] = _M0L6_2atmpS1958;
            _M0L6_2atmpS1964 = _M0L1iS730 + 1;
            _M0L6_2atmpS1965 = _M0L6outputS732 / 10ull;
            _M0L1iS730 = _M0L6_2atmpS1964;
            _M0L7currentS731 = _M0L7currentS733;
            _M0L6outputS732 = _M0L6_2atmpS1965;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1974 = _M0Lm5indexS700;
        _M0L6_2atmpS1975 = _M0L7olengthS704 + 1;
        _M0Lm5indexS700 = _M0L6_2atmpS1974 + _M0L6_2atmpS1975;
      }
    }
    _M0L6_2atmpS1976 = _M0Lm5indexS700;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2616
    = _M0FPB19string__from__bytes(_M0L6resultS699, 0, _M0L6_2atmpS1976);
    moonbit_decref_cycle_free(_M0L6resultS699);
    return _result_2616;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS645,
  uint32_t _M0L12ieeeExponentS644
) {
  int32_t _M0Lm2e2S642;
  uint64_t _M0Lm2m2S643;
  uint64_t _M0L6_2atmpS1850;
  uint64_t _M0L6_2atmpS1849;
  int32_t _M0L4evenS646;
  uint64_t _M0L6_2atmpS1848;
  uint64_t _M0L2mvS647;
  int32_t _M0L7mmShiftS648;
  uint64_t _M0Lm2vrS649;
  uint64_t _M0Lm2vpS650;
  uint64_t _M0Lm2vmS651;
  int32_t _M0Lm3e10S652;
  int32_t _M0Lm17vmIsTrailingZerosS653;
  int32_t _M0Lm17vrIsTrailingZerosS654;
  int32_t _M0L6_2atmpS1750;
  int32_t _M0Lm7removedS673;
  int32_t _M0Lm16lastRemovedDigitS674;
  uint64_t _M0Lm6outputS675;
  int32_t _M0L6_2atmpS1846;
  int32_t _M0L6_2atmpS1847;
  int32_t _M0L3expS698;
  uint64_t _M0L6_2atmpS1845;
  struct _M0TPB17FloatingDecimal64* _block_2622;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S642 = 0;
  _M0Lm2m2S643 = 0ull;
  if (_M0L12ieeeExponentS644 == 0u) {
    _M0Lm2e2S642 = -1076;
    _M0Lm2m2S643 = _M0L12ieeeMantissaS645;
  } else {
    int32_t _M0L6_2atmpS1749 = *(int32_t*)&_M0L12ieeeExponentS644;
    int32_t _M0L6_2atmpS1748 = _M0L6_2atmpS1749 - 1023;
    int32_t _M0L6_2atmpS1747 = _M0L6_2atmpS1748 - 52;
    _M0Lm2e2S642 = _M0L6_2atmpS1747 - 2;
    _M0Lm2m2S643 = 4503599627370496ull | _M0L12ieeeMantissaS645;
  }
  _M0L6_2atmpS1850 = _M0Lm2m2S643;
  _M0L6_2atmpS1849 = _M0L6_2atmpS1850 & 1ull;
  _M0L4evenS646 = _M0L6_2atmpS1849 == 0ull;
  _M0L6_2atmpS1848 = _M0Lm2m2S643;
  _M0L2mvS647 = 4ull * _M0L6_2atmpS1848;
  _M0L7mmShiftS648
  = _M0L12ieeeMantissaS645 != 0ull || _M0L12ieeeExponentS644 <= 1u;
  _M0Lm2vrS649 = 0ull;
  _M0Lm2vpS650 = 0ull;
  _M0Lm2vmS651 = 0ull;
  _M0Lm3e10S652 = 0;
  _M0Lm17vmIsTrailingZerosS653 = 0;
  _M0Lm17vrIsTrailingZerosS654 = 0;
  _M0L6_2atmpS1750 = _M0Lm2e2S642;
  if (_M0L6_2atmpS1750 >= 0) {
    int32_t _M0L6_2atmpS1772 = _M0Lm2e2S642;
    int32_t _M0L6_2atmpS1768;
    int32_t _M0L6_2atmpS1771;
    int32_t _M0L6_2atmpS1770;
    int32_t _M0L6_2atmpS1769;
    int32_t _M0L1qS655;
    int32_t _M0L6_2atmpS1767;
    int32_t _M0L6_2atmpS1766;
    int32_t _M0L1kS656;
    int32_t _M0L6_2atmpS1765;
    int32_t _M0L6_2atmpS1764;
    int32_t _M0L6_2atmpS1763;
    int32_t _M0L1iS657;
    struct _M0TPB8Pow5Pair _M0L4pow5S658;
    uint64_t _M0L6_2atmpS1762;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS659;
    uint64_t _M0L8_2avrOutS660;
    uint64_t _M0L8_2avpOutS661;
    uint64_t _M0L8_2avmOutS662;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1768 = _M0FPB9log10Pow2(_M0L6_2atmpS1772);
    _M0L6_2atmpS1771 = _M0Lm2e2S642;
    _M0L6_2atmpS1770 = _M0L6_2atmpS1771 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1769 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1770);
    _M0L1qS655 = _M0L6_2atmpS1768 - _M0L6_2atmpS1769;
    _M0Lm3e10S652 = _M0L1qS655;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1767 = _M0FPB8pow5bits(_M0L1qS655);
    _M0L6_2atmpS1766 = 125 + _M0L6_2atmpS1767;
    _M0L1kS656 = _M0L6_2atmpS1766 - 1;
    _M0L6_2atmpS1765 = _M0Lm2e2S642;
    _M0L6_2atmpS1764 = -_M0L6_2atmpS1765;
    _M0L6_2atmpS1763 = _M0L6_2atmpS1764 + _M0L1qS655;
    _M0L1iS657 = _M0L6_2atmpS1763 + _M0L1kS656;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S658 = _M0FPB22double__computeInvPow5(_M0L1qS655);
    _M0L6_2atmpS1762 = _M0Lm2m2S643;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS659
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1762, _M0L4pow5S658, _M0L1iS657, _M0L7mmShiftS648);
    _M0L8_2avrOutS660 = _M0L7_2abindS659.$0;
    _M0L8_2avpOutS661 = _M0L7_2abindS659.$1;
    _M0L8_2avmOutS662 = _M0L7_2abindS659.$2;
    _M0Lm2vrS649 = _M0L8_2avrOutS660;
    _M0Lm2vpS650 = _M0L8_2avpOutS661;
    _M0Lm2vmS651 = _M0L8_2avmOutS662;
    if (_M0L1qS655 <= 21) {
      int32_t _M0L6_2atmpS1758 = (int32_t)_M0L2mvS647;
      uint64_t _M0L6_2atmpS1761 = _M0L2mvS647 / 5ull;
      int32_t _M0L6_2atmpS1760 = (int32_t)_M0L6_2atmpS1761;
      int32_t _M0L6_2atmpS1759 = 5 * _M0L6_2atmpS1760;
      int32_t _M0L6mvMod5S663 = _M0L6_2atmpS1758 - _M0L6_2atmpS1759;
      if (_M0L6mvMod5S663 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS654
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS647, _M0L1qS655);
      } else if (_M0L4evenS646) {
        uint64_t _M0L6_2atmpS1752 = _M0L2mvS647 - 1ull;
        uint64_t _M0L6_2atmpS1753;
        uint64_t _M0L6_2atmpS1751;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1753 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS648);
        _M0L6_2atmpS1751 = _M0L6_2atmpS1752 - _M0L6_2atmpS1753;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS653
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1751, _M0L1qS655);
      } else {
        uint64_t _M0L6_2atmpS1754 = _M0Lm2vpS650;
        uint64_t _M0L6_2atmpS1757 = _M0L2mvS647 + 2ull;
        int32_t _M0L6_2atmpS1756;
        uint64_t _M0L6_2atmpS1755;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1756
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1757, _M0L1qS655);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1755 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1756);
        _M0Lm2vpS650 = _M0L6_2atmpS1754 - _M0L6_2atmpS1755;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1786 = _M0Lm2e2S642;
    int32_t _M0L6_2atmpS1785 = -_M0L6_2atmpS1786;
    int32_t _M0L6_2atmpS1780;
    int32_t _M0L6_2atmpS1784;
    int32_t _M0L6_2atmpS1783;
    int32_t _M0L6_2atmpS1782;
    int32_t _M0L6_2atmpS1781;
    int32_t _M0L1qS664;
    int32_t _M0L6_2atmpS1773;
    int32_t _M0L6_2atmpS1779;
    int32_t _M0L6_2atmpS1778;
    int32_t _M0L1iS665;
    int32_t _M0L6_2atmpS1777;
    int32_t _M0L1kS666;
    int32_t _M0L1jS667;
    struct _M0TPB8Pow5Pair _M0L4pow5S668;
    uint64_t _M0L6_2atmpS1776;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS669;
    uint64_t _M0L8_2avrOutS670;
    uint64_t _M0L8_2avpOutS671;
    uint64_t _M0L8_2avmOutS672;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1780 = _M0FPB9log10Pow5(_M0L6_2atmpS1785);
    _M0L6_2atmpS1784 = _M0Lm2e2S642;
    _M0L6_2atmpS1783 = -_M0L6_2atmpS1784;
    _M0L6_2atmpS1782 = _M0L6_2atmpS1783 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1781 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1782);
    _M0L1qS664 = _M0L6_2atmpS1780 - _M0L6_2atmpS1781;
    _M0L6_2atmpS1773 = _M0Lm2e2S642;
    _M0Lm3e10S652 = _M0L1qS664 + _M0L6_2atmpS1773;
    _M0L6_2atmpS1779 = _M0Lm2e2S642;
    _M0L6_2atmpS1778 = -_M0L6_2atmpS1779;
    _M0L1iS665 = _M0L6_2atmpS1778 - _M0L1qS664;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1777 = _M0FPB8pow5bits(_M0L1iS665);
    _M0L1kS666 = _M0L6_2atmpS1777 - 125;
    _M0L1jS667 = _M0L1qS664 - _M0L1kS666;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S668 = _M0FPB19double__computePow5(_M0L1iS665);
    _M0L6_2atmpS1776 = _M0Lm2m2S643;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS669
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1776, _M0L4pow5S668, _M0L1jS667, _M0L7mmShiftS648);
    _M0L8_2avrOutS670 = _M0L7_2abindS669.$0;
    _M0L8_2avpOutS671 = _M0L7_2abindS669.$1;
    _M0L8_2avmOutS672 = _M0L7_2abindS669.$2;
    _M0Lm2vrS649 = _M0L8_2avrOutS670;
    _M0Lm2vpS650 = _M0L8_2avpOutS671;
    _M0Lm2vmS651 = _M0L8_2avmOutS672;
    if (_M0L1qS664 <= 1) {
      _M0Lm17vrIsTrailingZerosS654 = 1;
      if (_M0L4evenS646) {
        int32_t _M0L6_2atmpS1774;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1774 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS648);
        _M0Lm17vmIsTrailingZerosS653 = _M0L6_2atmpS1774 == 1;
      } else {
        uint64_t _M0L6_2atmpS1775 = _M0Lm2vpS650;
        _M0Lm2vpS650 = _M0L6_2atmpS1775 - 1ull;
      }
    } else if (_M0L1qS664 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS654
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS647, _M0L1qS664);
    }
  }
  _M0Lm7removedS673 = 0;
  _M0Lm16lastRemovedDigitS674 = 0;
  _M0Lm6outputS675 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS653 || _M0Lm17vrIsTrailingZerosS654) {
    int32_t _if__result_2619;
    uint64_t _M0L6_2atmpS1816;
    uint64_t _M0L6_2atmpS1822;
    uint64_t _M0L6_2atmpS1823;
    int32_t _if__result_2620;
    int32_t _M0L6_2atmpS1819;
    int64_t _M0L6_2atmpS1818;
    uint64_t _M0L6_2atmpS1817;
    while (1) {
      uint64_t _M0L6_2atmpS1799 = _M0Lm2vpS650;
      uint64_t _M0L7vpDiv10S676 = _M0L6_2atmpS1799 / 10ull;
      uint64_t _M0L6_2atmpS1798 = _M0Lm2vmS651;
      uint64_t _M0L7vmDiv10S677 = _M0L6_2atmpS1798 / 10ull;
      uint64_t _M0L6_2atmpS1797;
      int32_t _M0L6_2atmpS1794;
      int32_t _M0L6_2atmpS1796;
      int32_t _M0L6_2atmpS1795;
      int32_t _M0L7vmMod10S679;
      uint64_t _M0L6_2atmpS1793;
      uint64_t _M0L7vrDiv10S680;
      uint64_t _M0L6_2atmpS1792;
      int32_t _M0L6_2atmpS1789;
      int32_t _M0L6_2atmpS1791;
      int32_t _M0L6_2atmpS1790;
      int32_t _M0L7vrMod10S681;
      int32_t _M0L6_2atmpS1788;
      if (_M0L7vpDiv10S676 <= _M0L7vmDiv10S677) {
        break;
      }
      _M0L6_2atmpS1797 = _M0Lm2vmS651;
      _M0L6_2atmpS1794 = (int32_t)_M0L6_2atmpS1797;
      _M0L6_2atmpS1796 = (int32_t)_M0L7vmDiv10S677;
      _M0L6_2atmpS1795 = 10 * _M0L6_2atmpS1796;
      _M0L7vmMod10S679 = _M0L6_2atmpS1794 - _M0L6_2atmpS1795;
      _M0L6_2atmpS1793 = _M0Lm2vrS649;
      _M0L7vrDiv10S680 = _M0L6_2atmpS1793 / 10ull;
      _M0L6_2atmpS1792 = _M0Lm2vrS649;
      _M0L6_2atmpS1789 = (int32_t)_M0L6_2atmpS1792;
      _M0L6_2atmpS1791 = (int32_t)_M0L7vrDiv10S680;
      _M0L6_2atmpS1790 = 10 * _M0L6_2atmpS1791;
      _M0L7vrMod10S681 = _M0L6_2atmpS1789 - _M0L6_2atmpS1790;
      _M0Lm17vmIsTrailingZerosS653
      = _M0Lm17vmIsTrailingZerosS653 && _M0L7vmMod10S679 == 0;
      if (_M0Lm17vrIsTrailingZerosS654) {
        int32_t _M0L6_2atmpS1787 = _M0Lm16lastRemovedDigitS674;
        _M0Lm17vrIsTrailingZerosS654 = _M0L6_2atmpS1787 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS654 = 0;
      }
      _M0Lm16lastRemovedDigitS674 = _M0L7vrMod10S681;
      _M0Lm2vrS649 = _M0L7vrDiv10S680;
      _M0Lm2vpS650 = _M0L7vpDiv10S676;
      _M0Lm2vmS651 = _M0L7vmDiv10S677;
      _M0L6_2atmpS1788 = _M0Lm7removedS673;
      _M0Lm7removedS673 = _M0L6_2atmpS1788 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS653) {
      while (1) {
        uint64_t _M0L6_2atmpS1812 = _M0Lm2vmS651;
        uint64_t _M0L7vmDiv10S682 = _M0L6_2atmpS1812 / 10ull;
        uint64_t _M0L6_2atmpS1811 = _M0Lm2vmS651;
        int32_t _M0L6_2atmpS1808 = (int32_t)_M0L6_2atmpS1811;
        int32_t _M0L6_2atmpS1810 = (int32_t)_M0L7vmDiv10S682;
        int32_t _M0L6_2atmpS1809 = 10 * _M0L6_2atmpS1810;
        int32_t _M0L7vmMod10S683 = _M0L6_2atmpS1808 - _M0L6_2atmpS1809;
        uint64_t _M0L6_2atmpS1807;
        uint64_t _M0L7vpDiv10S685;
        uint64_t _M0L6_2atmpS1806;
        uint64_t _M0L7vrDiv10S686;
        uint64_t _M0L6_2atmpS1805;
        int32_t _M0L6_2atmpS1802;
        int32_t _M0L6_2atmpS1804;
        int32_t _M0L6_2atmpS1803;
        int32_t _M0L7vrMod10S687;
        int32_t _M0L6_2atmpS1801;
        if (_M0L7vmMod10S683 != 0) {
          break;
        }
        _M0L6_2atmpS1807 = _M0Lm2vpS650;
        _M0L7vpDiv10S685 = _M0L6_2atmpS1807 / 10ull;
        _M0L6_2atmpS1806 = _M0Lm2vrS649;
        _M0L7vrDiv10S686 = _M0L6_2atmpS1806 / 10ull;
        _M0L6_2atmpS1805 = _M0Lm2vrS649;
        _M0L6_2atmpS1802 = (int32_t)_M0L6_2atmpS1805;
        _M0L6_2atmpS1804 = (int32_t)_M0L7vrDiv10S686;
        _M0L6_2atmpS1803 = 10 * _M0L6_2atmpS1804;
        _M0L7vrMod10S687 = _M0L6_2atmpS1802 - _M0L6_2atmpS1803;
        if (_M0Lm17vrIsTrailingZerosS654) {
          int32_t _M0L6_2atmpS1800 = _M0Lm16lastRemovedDigitS674;
          _M0Lm17vrIsTrailingZerosS654 = _M0L6_2atmpS1800 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS654 = 0;
        }
        _M0Lm16lastRemovedDigitS674 = _M0L7vrMod10S687;
        _M0Lm2vrS649 = _M0L7vrDiv10S686;
        _M0Lm2vpS650 = _M0L7vpDiv10S685;
        _M0Lm2vmS651 = _M0L7vmDiv10S682;
        _M0L6_2atmpS1801 = _M0Lm7removedS673;
        _M0Lm7removedS673 = _M0L6_2atmpS1801 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS654) {
      int32_t _M0L6_2atmpS1815 = _M0Lm16lastRemovedDigitS674;
      if (_M0L6_2atmpS1815 == 5) {
        uint64_t _M0L6_2atmpS1814 = _M0Lm2vrS649;
        uint64_t _M0L6_2atmpS1813 = _M0L6_2atmpS1814 % 2ull;
        _if__result_2619 = _M0L6_2atmpS1813 == 0ull;
      } else {
        _if__result_2619 = 0;
      }
    } else {
      _if__result_2619 = 0;
    }
    if (_if__result_2619) {
      _M0Lm16lastRemovedDigitS674 = 4;
    }
    _M0L6_2atmpS1816 = _M0Lm2vrS649;
    _M0L6_2atmpS1822 = _M0Lm2vrS649;
    _M0L6_2atmpS1823 = _M0Lm2vmS651;
    if (_M0L6_2atmpS1822 == _M0L6_2atmpS1823) {
      if (!_M0L4evenS646) {
        _if__result_2620 = 1;
      } else {
        int32_t _M0L6_2atmpS1821 = _M0Lm17vmIsTrailingZerosS653;
        _if__result_2620 = !_M0L6_2atmpS1821;
      }
    } else {
      _if__result_2620 = 0;
    }
    if (_if__result_2620) {
      _M0L6_2atmpS1819 = 1;
    } else {
      int32_t _M0L6_2atmpS1820 = _M0Lm16lastRemovedDigitS674;
      _M0L6_2atmpS1819 = _M0L6_2atmpS1820 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1818 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1819);
    _M0L6_2atmpS1817 = *(uint64_t*)&_M0L6_2atmpS1818;
    _M0Lm6outputS675 = _M0L6_2atmpS1816 + _M0L6_2atmpS1817;
  } else {
    int32_t _M0Lm7roundUpS688 = 0;
    uint64_t _M0L6_2atmpS1844 = _M0Lm2vpS650;
    uint64_t _M0L8vpDiv100S689 = _M0L6_2atmpS1844 / 100ull;
    uint64_t _M0L6_2atmpS1843 = _M0Lm2vmS651;
    uint64_t _M0L8vmDiv100S690 = _M0L6_2atmpS1843 / 100ull;
    uint64_t _M0L6_2atmpS1838;
    uint64_t _M0L6_2atmpS1841;
    uint64_t _M0L6_2atmpS1842;
    int32_t _M0L6_2atmpS1840;
    uint64_t _M0L6_2atmpS1839;
    if (_M0L8vpDiv100S689 > _M0L8vmDiv100S690) {
      uint64_t _M0L6_2atmpS1829 = _M0Lm2vrS649;
      uint64_t _M0L8vrDiv100S691 = _M0L6_2atmpS1829 / 100ull;
      uint64_t _M0L6_2atmpS1828 = _M0Lm2vrS649;
      int32_t _M0L6_2atmpS1825 = (int32_t)_M0L6_2atmpS1828;
      int32_t _M0L6_2atmpS1827 = (int32_t)_M0L8vrDiv100S691;
      int32_t _M0L6_2atmpS1826 = 100 * _M0L6_2atmpS1827;
      int32_t _M0L8vrMod100S692 = _M0L6_2atmpS1825 - _M0L6_2atmpS1826;
      int32_t _M0L6_2atmpS1824;
      _M0Lm7roundUpS688 = _M0L8vrMod100S692 >= 50;
      _M0Lm2vrS649 = _M0L8vrDiv100S691;
      _M0Lm2vpS650 = _M0L8vpDiv100S689;
      _M0Lm2vmS651 = _M0L8vmDiv100S690;
      _M0L6_2atmpS1824 = _M0Lm7removedS673;
      _M0Lm7removedS673 = _M0L6_2atmpS1824 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1837 = _M0Lm2vpS650;
      uint64_t _M0L7vpDiv10S693 = _M0L6_2atmpS1837 / 10ull;
      uint64_t _M0L6_2atmpS1836 = _M0Lm2vmS651;
      uint64_t _M0L7vmDiv10S694 = _M0L6_2atmpS1836 / 10ull;
      uint64_t _M0L6_2atmpS1835;
      uint64_t _M0L7vrDiv10S696;
      uint64_t _M0L6_2atmpS1834;
      int32_t _M0L6_2atmpS1831;
      int32_t _M0L6_2atmpS1833;
      int32_t _M0L6_2atmpS1832;
      int32_t _M0L7vrMod10S697;
      int32_t _M0L6_2atmpS1830;
      if (_M0L7vpDiv10S693 <= _M0L7vmDiv10S694) {
        break;
      }
      _M0L6_2atmpS1835 = _M0Lm2vrS649;
      _M0L7vrDiv10S696 = _M0L6_2atmpS1835 / 10ull;
      _M0L6_2atmpS1834 = _M0Lm2vrS649;
      _M0L6_2atmpS1831 = (int32_t)_M0L6_2atmpS1834;
      _M0L6_2atmpS1833 = (int32_t)_M0L7vrDiv10S696;
      _M0L6_2atmpS1832 = 10 * _M0L6_2atmpS1833;
      _M0L7vrMod10S697 = _M0L6_2atmpS1831 - _M0L6_2atmpS1832;
      _M0Lm7roundUpS688 = _M0L7vrMod10S697 >= 5;
      _M0Lm2vrS649 = _M0L7vrDiv10S696;
      _M0Lm2vpS650 = _M0L7vpDiv10S693;
      _M0Lm2vmS651 = _M0L7vmDiv10S694;
      _M0L6_2atmpS1830 = _M0Lm7removedS673;
      _M0Lm7removedS673 = _M0L6_2atmpS1830 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1838 = _M0Lm2vrS649;
    _M0L6_2atmpS1841 = _M0Lm2vrS649;
    _M0L6_2atmpS1842 = _M0Lm2vmS651;
    _M0L6_2atmpS1840
    = _M0L6_2atmpS1841 == _M0L6_2atmpS1842 || _M0Lm7roundUpS688;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1839 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1840);
    _M0Lm6outputS675 = _M0L6_2atmpS1838 + _M0L6_2atmpS1839;
  }
  _M0L6_2atmpS1846 = _M0Lm3e10S652;
  _M0L6_2atmpS1847 = _M0Lm7removedS673;
  _M0L3expS698 = _M0L6_2atmpS1846 + _M0L6_2atmpS1847;
  _M0L6_2atmpS1845 = _M0Lm6outputS675;
  _block_2622
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2622)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2622->$0 = _M0L6_2atmpS1845;
  _block_2622->$1 = _M0L3expS698;
  return _block_2622;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS641) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS641) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS640) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS640) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS639) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS639) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS638) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS638 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS638 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS638 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS638 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS638 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS638 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS638 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS638 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS638 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS638 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS638 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS638 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS638 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS638 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS638 >= 100ull) {
    return 3;
  }
  if (_M0L1vS638 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS621) {
  int32_t _M0L6_2atmpS1746;
  int32_t _M0L6_2atmpS1745;
  int32_t _M0L4baseS620;
  int32_t _M0L5base2S622;
  int32_t _M0L6offsetS623;
  int32_t _M0L6_2atmpS1744;
  uint64_t _M0L4mul0S624;
  int32_t _M0L6_2atmpS1743;
  int32_t _M0L6_2atmpS1742;
  uint64_t _M0L4mul1S625;
  uint64_t _M0L1mS626;
  struct _M0TPB7Umul128 _M0L7_2abindS627;
  uint64_t _M0L7_2alow1S628;
  uint64_t _M0L8_2ahigh1S629;
  struct _M0TPB7Umul128 _M0L7_2abindS630;
  uint64_t _M0L7_2alow0S631;
  uint64_t _M0L8_2ahigh0S632;
  uint64_t _M0L3sumS633;
  uint64_t _M0Lm5high1S634;
  int32_t _M0L6_2atmpS1740;
  int32_t _M0L6_2atmpS1741;
  int32_t _M0L5deltaS635;
  uint64_t _M0L6_2atmpS1739;
  uint64_t _M0L6_2atmpS1731;
  int32_t _M0L6_2atmpS1738;
  uint32_t _M0L6_2atmpS1735;
  int32_t _M0L6_2atmpS1737;
  int32_t _M0L6_2atmpS1736;
  uint32_t _M0L6_2atmpS1734;
  uint32_t _M0L6_2atmpS1733;
  uint64_t _M0L6_2atmpS1732;
  uint64_t _M0L1aS636;
  uint64_t _M0L6_2atmpS1730;
  uint64_t _M0L1bS637;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1746 = _M0L1iS621 + 26;
  _M0L6_2atmpS1745 = _M0L6_2atmpS1746 - 1;
  _M0L4baseS620 = _M0L6_2atmpS1745 / 26;
  _M0L5base2S622 = _M0L4baseS620 * 26;
  _M0L6offsetS623 = _M0L5base2S622 - _M0L1iS621;
  _M0L6_2atmpS1744 = _M0L4baseS620 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S624
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1744);
  _M0L6_2atmpS1743 = _M0L4baseS620 * 2;
  _M0L6_2atmpS1742 = _M0L6_2atmpS1743 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S625
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1742);
  if (_M0L6offsetS623 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S624, .$1 = _M0L4mul1S625};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS623);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS627 = _M0FPB7umul128(_M0L1mS626, _M0L4mul1S625);
  _M0L7_2alow1S628 = _M0L7_2abindS627.$0;
  _M0L8_2ahigh1S629 = _M0L7_2abindS627.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS630 = _M0FPB7umul128(_M0L1mS626, _M0L4mul0S624);
  _M0L7_2alow0S631 = _M0L7_2abindS630.$0;
  _M0L8_2ahigh0S632 = _M0L7_2abindS630.$1;
  _M0L3sumS633 = _M0L8_2ahigh0S632 + _M0L7_2alow1S628;
  _M0Lm5high1S634 = _M0L8_2ahigh1S629;
  if (_M0L3sumS633 < _M0L8_2ahigh0S632) {
    uint64_t _M0L6_2atmpS1729 = _M0Lm5high1S634;
    _M0Lm5high1S634 = _M0L6_2atmpS1729 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1740 = _M0FPB8pow5bits(_M0L5base2S622);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1741 = _M0FPB8pow5bits(_M0L1iS621);
  _M0L5deltaS635 = _M0L6_2atmpS1740 - _M0L6_2atmpS1741;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1739
  = _M0FPB13shiftright128(_M0L7_2alow0S631, _M0L3sumS633, _M0L5deltaS635);
  _M0L6_2atmpS1731 = _M0L6_2atmpS1739 + 1ull;
  _M0L6_2atmpS1738 = _M0L1iS621 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1735
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1738);
  _M0L6_2atmpS1737 = _M0L1iS621 % 16;
  _M0L6_2atmpS1736 = _M0L6_2atmpS1737 << 1;
  _M0L6_2atmpS1734 = _M0L6_2atmpS1735 >> (_M0L6_2atmpS1736 & 31);
  _M0L6_2atmpS1733 = _M0L6_2atmpS1734 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1732 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1733);
  _M0L1aS636 = _M0L6_2atmpS1731 + _M0L6_2atmpS1732;
  _M0L6_2atmpS1730 = _M0Lm5high1S634;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS637
  = _M0FPB13shiftright128(_M0L3sumS633, _M0L6_2atmpS1730, _M0L5deltaS635);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS636, .$1 = _M0L1bS637};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS603) {
  int32_t _M0L4baseS602;
  int32_t _M0L5base2S604;
  int32_t _M0L6offsetS605;
  int32_t _M0L6_2atmpS1728;
  uint64_t _M0L4mul0S606;
  int32_t _M0L6_2atmpS1727;
  int32_t _M0L6_2atmpS1726;
  uint64_t _M0L4mul1S607;
  uint64_t _M0L1mS608;
  struct _M0TPB7Umul128 _M0L7_2abindS609;
  uint64_t _M0L7_2alow1S610;
  uint64_t _M0L8_2ahigh1S611;
  struct _M0TPB7Umul128 _M0L7_2abindS612;
  uint64_t _M0L7_2alow0S613;
  uint64_t _M0L8_2ahigh0S614;
  uint64_t _M0L3sumS615;
  uint64_t _M0Lm5high1S616;
  int32_t _M0L6_2atmpS1724;
  int32_t _M0L6_2atmpS1725;
  int32_t _M0L5deltaS617;
  uint64_t _M0L6_2atmpS1716;
  int32_t _M0L6_2atmpS1723;
  uint32_t _M0L6_2atmpS1720;
  int32_t _M0L6_2atmpS1722;
  int32_t _M0L6_2atmpS1721;
  uint32_t _M0L6_2atmpS1719;
  uint32_t _M0L6_2atmpS1718;
  uint64_t _M0L6_2atmpS1717;
  uint64_t _M0L1aS618;
  uint64_t _M0L6_2atmpS1715;
  uint64_t _M0L1bS619;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS602 = _M0L1iS603 / 26;
  _M0L5base2S604 = _M0L4baseS602 * 26;
  _M0L6offsetS605 = _M0L1iS603 - _M0L5base2S604;
  _M0L6_2atmpS1728 = _M0L4baseS602 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S606
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1728);
  _M0L6_2atmpS1727 = _M0L4baseS602 * 2;
  _M0L6_2atmpS1726 = _M0L6_2atmpS1727 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S607
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1726);
  if (_M0L6offsetS605 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S606, .$1 = _M0L4mul1S607};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS605);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS609 = _M0FPB7umul128(_M0L1mS608, _M0L4mul1S607);
  _M0L7_2alow1S610 = _M0L7_2abindS609.$0;
  _M0L8_2ahigh1S611 = _M0L7_2abindS609.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS612 = _M0FPB7umul128(_M0L1mS608, _M0L4mul0S606);
  _M0L7_2alow0S613 = _M0L7_2abindS612.$0;
  _M0L8_2ahigh0S614 = _M0L7_2abindS612.$1;
  _M0L3sumS615 = _M0L8_2ahigh0S614 + _M0L7_2alow1S610;
  _M0Lm5high1S616 = _M0L8_2ahigh1S611;
  if (_M0L3sumS615 < _M0L8_2ahigh0S614) {
    uint64_t _M0L6_2atmpS1714 = _M0Lm5high1S616;
    _M0Lm5high1S616 = _M0L6_2atmpS1714 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1724 = _M0FPB8pow5bits(_M0L1iS603);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1725 = _M0FPB8pow5bits(_M0L5base2S604);
  _M0L5deltaS617 = _M0L6_2atmpS1724 - _M0L6_2atmpS1725;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1716
  = _M0FPB13shiftright128(_M0L7_2alow0S613, _M0L3sumS615, _M0L5deltaS617);
  _M0L6_2atmpS1723 = _M0L1iS603 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1720
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1723);
  _M0L6_2atmpS1722 = _M0L1iS603 % 16;
  _M0L6_2atmpS1721 = _M0L6_2atmpS1722 << 1;
  _M0L6_2atmpS1719 = _M0L6_2atmpS1720 >> (_M0L6_2atmpS1721 & 31);
  _M0L6_2atmpS1718 = _M0L6_2atmpS1719 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1717 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1718);
  _M0L1aS618 = _M0L6_2atmpS1716 + _M0L6_2atmpS1717;
  _M0L6_2atmpS1715 = _M0Lm5high1S616;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS619
  = _M0FPB13shiftright128(_M0L3sumS615, _M0L6_2atmpS1715, _M0L5deltaS617);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS618, .$1 = _M0L1bS619};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS576,
  struct _M0TPB8Pow5Pair _M0L3mulS573,
  int32_t _M0L1jS589,
  int32_t _M0L7mmShiftS591
) {
  uint64_t _M0L7_2amul0S572;
  uint64_t _M0L7_2amul1S574;
  uint64_t _M0L1mS575;
  struct _M0TPB7Umul128 _M0L7_2abindS577;
  uint64_t _M0L5_2aloS578;
  uint64_t _M0L6_2atmpS579;
  struct _M0TPB7Umul128 _M0L7_2abindS580;
  uint64_t _M0L6_2alo2S581;
  uint64_t _M0L6_2ahi2S582;
  uint64_t _M0L3midS583;
  uint64_t _M0L6_2atmpS1713;
  uint64_t _M0L2hiS584;
  uint64_t _M0L3lo2S585;
  uint64_t _M0L6_2atmpS1711;
  uint64_t _M0L6_2atmpS1712;
  uint64_t _M0L4mid2S586;
  uint64_t _M0L6_2atmpS1710;
  uint64_t _M0L3hi2S587;
  int32_t _M0L6_2atmpS1709;
  int32_t _M0L6_2atmpS1708;
  uint64_t _M0L2vpS588;
  uint64_t _M0Lm2vmS590;
  int32_t _M0L6_2atmpS1707;
  int32_t _M0L6_2atmpS1706;
  uint64_t _M0L2vrS601;
  uint64_t _M0L6_2atmpS1705;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S572 = _M0L3mulS573.$0;
  _M0L7_2amul1S574 = _M0L3mulS573.$1;
  _M0L1mS575 = _M0L1mS576 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS577 = _M0FPB7umul128(_M0L1mS575, _M0L7_2amul0S572);
  _M0L5_2aloS578 = _M0L7_2abindS577.$0;
  _M0L6_2atmpS579 = _M0L7_2abindS577.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS580 = _M0FPB7umul128(_M0L1mS575, _M0L7_2amul1S574);
  _M0L6_2alo2S581 = _M0L7_2abindS580.$0;
  _M0L6_2ahi2S582 = _M0L7_2abindS580.$1;
  _M0L3midS583 = _M0L6_2atmpS579 + _M0L6_2alo2S581;
  if (_M0L3midS583 < _M0L6_2atmpS579) {
    _M0L6_2atmpS1713 = 1ull;
  } else {
    _M0L6_2atmpS1713 = 0ull;
  }
  _M0L2hiS584 = _M0L6_2ahi2S582 + _M0L6_2atmpS1713;
  _M0L3lo2S585 = _M0L5_2aloS578 + _M0L7_2amul0S572;
  _M0L6_2atmpS1711 = _M0L3midS583 + _M0L7_2amul1S574;
  if (_M0L3lo2S585 < _M0L5_2aloS578) {
    _M0L6_2atmpS1712 = 1ull;
  } else {
    _M0L6_2atmpS1712 = 0ull;
  }
  _M0L4mid2S586 = _M0L6_2atmpS1711 + _M0L6_2atmpS1712;
  if (_M0L4mid2S586 < _M0L3midS583) {
    _M0L6_2atmpS1710 = 1ull;
  } else {
    _M0L6_2atmpS1710 = 0ull;
  }
  _M0L3hi2S587 = _M0L2hiS584 + _M0L6_2atmpS1710;
  _M0L6_2atmpS1709 = _M0L1jS589 - 64;
  _M0L6_2atmpS1708 = _M0L6_2atmpS1709 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS588
  = _M0FPB13shiftright128(_M0L4mid2S586, _M0L3hi2S587, _M0L6_2atmpS1708);
  _M0Lm2vmS590 = 0ull;
  if (_M0L7mmShiftS591) {
    uint64_t _M0L3lo3S592 = _M0L5_2aloS578 - _M0L7_2amul0S572;
    uint64_t _M0L6_2atmpS1695 = _M0L3midS583 - _M0L7_2amul1S574;
    uint64_t _M0L6_2atmpS1696;
    uint64_t _M0L4mid3S593;
    uint64_t _M0L6_2atmpS1694;
    uint64_t _M0L3hi3S594;
    int32_t _M0L6_2atmpS1693;
    int32_t _M0L6_2atmpS1692;
    if (_M0L5_2aloS578 < _M0L3lo3S592) {
      _M0L6_2atmpS1696 = 1ull;
    } else {
      _M0L6_2atmpS1696 = 0ull;
    }
    _M0L4mid3S593 = _M0L6_2atmpS1695 - _M0L6_2atmpS1696;
    if (_M0L3midS583 < _M0L4mid3S593) {
      _M0L6_2atmpS1694 = 1ull;
    } else {
      _M0L6_2atmpS1694 = 0ull;
    }
    _M0L3hi3S594 = _M0L2hiS584 - _M0L6_2atmpS1694;
    _M0L6_2atmpS1693 = _M0L1jS589 - 64;
    _M0L6_2atmpS1692 = _M0L6_2atmpS1693 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS590
    = _M0FPB13shiftright128(_M0L4mid3S593, _M0L3hi3S594, _M0L6_2atmpS1692);
  } else {
    uint64_t _M0L3lo3S595 = _M0L5_2aloS578 + _M0L5_2aloS578;
    uint64_t _M0L6_2atmpS1703 = _M0L3midS583 + _M0L3midS583;
    uint64_t _M0L6_2atmpS1704;
    uint64_t _M0L4mid3S596;
    uint64_t _M0L6_2atmpS1701;
    uint64_t _M0L6_2atmpS1702;
    uint64_t _M0L3hi3S597;
    uint64_t _M0L3lo4S598;
    uint64_t _M0L6_2atmpS1699;
    uint64_t _M0L6_2atmpS1700;
    uint64_t _M0L4mid4S599;
    uint64_t _M0L6_2atmpS1698;
    uint64_t _M0L3hi4S600;
    int32_t _M0L6_2atmpS1697;
    if (_M0L3lo3S595 < _M0L5_2aloS578) {
      _M0L6_2atmpS1704 = 1ull;
    } else {
      _M0L6_2atmpS1704 = 0ull;
    }
    _M0L4mid3S596 = _M0L6_2atmpS1703 + _M0L6_2atmpS1704;
    _M0L6_2atmpS1701 = _M0L2hiS584 + _M0L2hiS584;
    if (_M0L4mid3S596 < _M0L3midS583) {
      _M0L6_2atmpS1702 = 1ull;
    } else {
      _M0L6_2atmpS1702 = 0ull;
    }
    _M0L3hi3S597 = _M0L6_2atmpS1701 + _M0L6_2atmpS1702;
    _M0L3lo4S598 = _M0L3lo3S595 - _M0L7_2amul0S572;
    _M0L6_2atmpS1699 = _M0L4mid3S596 - _M0L7_2amul1S574;
    if (_M0L3lo3S595 < _M0L3lo4S598) {
      _M0L6_2atmpS1700 = 1ull;
    } else {
      _M0L6_2atmpS1700 = 0ull;
    }
    _M0L4mid4S599 = _M0L6_2atmpS1699 - _M0L6_2atmpS1700;
    if (_M0L4mid3S596 < _M0L4mid4S599) {
      _M0L6_2atmpS1698 = 1ull;
    } else {
      _M0L6_2atmpS1698 = 0ull;
    }
    _M0L3hi4S600 = _M0L3hi3S597 - _M0L6_2atmpS1698;
    _M0L6_2atmpS1697 = _M0L1jS589 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS590
    = _M0FPB13shiftright128(_M0L4mid4S599, _M0L3hi4S600, _M0L6_2atmpS1697);
  }
  _M0L6_2atmpS1707 = _M0L1jS589 - 64;
  _M0L6_2atmpS1706 = _M0L6_2atmpS1707 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS601
  = _M0FPB13shiftright128(_M0L3midS583, _M0L2hiS584, _M0L6_2atmpS1706);
  _M0L6_2atmpS1705 = _M0Lm2vmS590;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS601,
                                                .$1 = _M0L2vpS588,
                                                .$2 = _M0L6_2atmpS1705};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS570,
  int32_t _M0L1pS571
) {
  uint64_t _M0L6_2atmpS1691;
  uint64_t _M0L6_2atmpS1690;
  uint64_t _M0L6_2atmpS1689;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1691 = 1ull << (_M0L1pS571 & 63);
  _M0L6_2atmpS1690 = _M0L6_2atmpS1691 - 1ull;
  _M0L6_2atmpS1689 = _M0L5valueS570 & _M0L6_2atmpS1690;
  return _M0L6_2atmpS1689 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS568,
  int32_t _M0L1pS569
) {
  int32_t _M0L6_2atmpS1688;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1688 = _M0FPB10pow5Factor(_M0L5valueS568);
  return _M0L6_2atmpS1688 >= _M0L1pS569;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS563) {
  uint64_t _M0L6_2atmpS1679;
  uint64_t _M0L6_2atmpS1680;
  uint64_t _M0L6_2atmpS1681;
  uint64_t _M0L6_2atmpS1682;
  uint64_t _M0L6_2atmpS1687;
  int32_t _M0L5countS564;
  uint64_t _M0L1vS565;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1679 = _M0L5valueS563 % 5ull;
  if (_M0L6_2atmpS1679 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1680 = _M0L5valueS563 % 25ull;
  if (_M0L6_2atmpS1680 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1681 = _M0L5valueS563 % 125ull;
  if (_M0L6_2atmpS1681 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1682 = _M0L5valueS563 % 625ull;
  if (_M0L6_2atmpS1682 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1687 = _M0L5valueS563 / 625ull;
  _M0L5countS564 = 4;
  _M0L1vS565 = _M0L6_2atmpS1687;
  while (1) {
    if (_M0L1vS565 > 0ull) {
      uint64_t _M0L6_2atmpS1683 = _M0L1vS565 % 5ull;
      int32_t _M0L6_2atmpS1684;
      uint64_t _M0L6_2atmpS1685;
      if (_M0L6_2atmpS1683 != 0ull) {
        return _M0L5countS564;
      }
      _M0L6_2atmpS1684 = _M0L5countS564 + 1;
      _M0L6_2atmpS1685 = _M0L1vS565 / 5ull;
      _M0L5countS564 = _M0L6_2atmpS1684;
      _M0L1vS565 = _M0L6_2atmpS1685;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS567;
      moonbit_string_t _M0L6_2atmpS1686;
      int32_t _result_2624;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS567
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS567, (moonbit_string_t)moonbit_string_literal_21.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS567, _M0L5valueS563);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1686
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS567);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS567);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2624 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1686);
      moonbit_decref_cycle_free(_M0L6_2atmpS1686);
      return _result_2624;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS562,
  uint64_t _M0L2hiS560,
  int32_t _M0L4distS561
) {
  int32_t _M0L6_2atmpS1678;
  uint64_t _M0L6_2atmpS1676;
  uint64_t _M0L6_2atmpS1677;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1678 = 64 - _M0L4distS561;
  _M0L6_2atmpS1676 = _M0L2hiS560 << (_M0L6_2atmpS1678 & 63);
  _M0L6_2atmpS1677 = _M0L2loS562 >> (_M0L4distS561 & 63);
  return _M0L6_2atmpS1676 | _M0L6_2atmpS1677;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS550,
  uint64_t _M0L1bS553
) {
  uint64_t _M0L3aLoS549;
  uint64_t _M0L3aHiS551;
  uint64_t _M0L3bLoS552;
  uint64_t _M0L3bHiS554;
  uint64_t _M0L1xS555;
  uint64_t _M0L6_2atmpS1674;
  uint64_t _M0L6_2atmpS1675;
  uint64_t _M0L1yS556;
  uint64_t _M0L6_2atmpS1672;
  uint64_t _M0L6_2atmpS1673;
  uint64_t _M0L1zS557;
  uint64_t _M0L6_2atmpS1670;
  uint64_t _M0L6_2atmpS1671;
  uint64_t _M0L6_2atmpS1668;
  uint64_t _M0L6_2atmpS1669;
  uint64_t _M0L1wS558;
  uint64_t _M0L2loS559;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS549 = _M0L1aS550 & 4294967295ull;
  _M0L3aHiS551 = _M0L1aS550 >> 32;
  _M0L3bLoS552 = _M0L1bS553 & 4294967295ull;
  _M0L3bHiS554 = _M0L1bS553 >> 32;
  _M0L1xS555 = _M0L3aLoS549 * _M0L3bLoS552;
  _M0L6_2atmpS1674 = _M0L3aHiS551 * _M0L3bLoS552;
  _M0L6_2atmpS1675 = _M0L1xS555 >> 32;
  _M0L1yS556 = _M0L6_2atmpS1674 + _M0L6_2atmpS1675;
  _M0L6_2atmpS1672 = _M0L3aLoS549 * _M0L3bHiS554;
  _M0L6_2atmpS1673 = _M0L1yS556 & 4294967295ull;
  _M0L1zS557 = _M0L6_2atmpS1672 + _M0L6_2atmpS1673;
  _M0L6_2atmpS1670 = _M0L3aHiS551 * _M0L3bHiS554;
  _M0L6_2atmpS1671 = _M0L1yS556 >> 32;
  _M0L6_2atmpS1668 = _M0L6_2atmpS1670 + _M0L6_2atmpS1671;
  _M0L6_2atmpS1669 = _M0L1zS557 >> 32;
  _M0L1wS558 = _M0L6_2atmpS1668 + _M0L6_2atmpS1669;
  _M0L2loS559 = _M0L1aS550 * _M0L1bS553;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS559, .$1 = _M0L1wS558};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS547,
  int32_t _M0L4fromS544,
  int32_t _M0L2toS543
) {
  int32_t _M0L3lenS542;
  int32_t _M0L6_2atmpS1667;
  uint16_t* _M0L6bufferS545;
  int32_t _M0L1iS546;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS542 = _M0L2toS543 - _M0L4fromS544;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1667 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS545
  = (uint16_t*)moonbit_make_string(_M0L3lenS542, _M0L6_2atmpS1667);
  _M0L1iS546 = 0;
  while (1) {
    if (_M0L1iS546 < _M0L3lenS542) {
      int32_t _M0L6_2atmpS1665 = _M0L4fromS544 + _M0L1iS546;
      int32_t _M0L6_2atmpS1664;
      int32_t _M0L6_2atmpS1663;
      int32_t _M0L6_2atmpS1666;
      if (
        _M0L6_2atmpS1665 < 0
        || _M0L6_2atmpS1665 >= Moonbit_array_length(_M0L5bytesS547)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1664 = (int32_t)_M0L5bytesS547[_M0L6_2atmpS1665];
      _M0L6_2atmpS1663 = (uint16_t)_M0L6_2atmpS1664;
      if (
        _M0L1iS546 < 0 || _M0L1iS546 >= Moonbit_array_length(_M0L6bufferS545)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS545[_M0L1iS546] = _M0L6_2atmpS1663;
      _M0L6_2atmpS1666 = _M0L1iS546 + 1;
      _M0L1iS546 = _M0L6_2atmpS1666;
      continue;
    }
    break;
  }
  return _M0L6bufferS545;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS541) {
  int32_t _M0L6_2atmpS1662;
  uint32_t _M0L6_2atmpS1661;
  uint32_t _M0L6_2atmpS1660;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1662 = _M0L1eS541 * 78913;
  _M0L6_2atmpS1661 = *(uint32_t*)&_M0L6_2atmpS1662;
  _M0L6_2atmpS1660 = _M0L6_2atmpS1661 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1660;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS540) {
  int32_t _M0L6_2atmpS1659;
  uint32_t _M0L6_2atmpS1658;
  uint32_t _M0L6_2atmpS1657;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1659 = _M0L1eS540 * 732923;
  _M0L6_2atmpS1658 = *(uint32_t*)&_M0L6_2atmpS1659;
  _M0L6_2atmpS1657 = _M0L6_2atmpS1658 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1657;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS538,
  int32_t _M0L8exponentS539,
  int32_t _M0L8mantissaS536
) {
  moonbit_string_t _M0L1sS537;
  moonbit_string_t _result_2627;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS536) {
    return (moonbit_string_t)moonbit_string_literal_22.data;
  }
  if (_M0L4signS538) {
    _M0L1sS537 = (moonbit_string_t)moonbit_string_literal_23.data;
  } else {
    _M0L1sS537 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS539) {
    moonbit_string_t _result_2626;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2626
    = moonbit_add_string(_M0L1sS537, (moonbit_string_t)moonbit_string_literal_24.data);
    moonbit_decref_cycle_free(_M0L1sS537);
    return _result_2626;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2627
  = moonbit_add_string(_M0L1sS537, (moonbit_string_t)moonbit_string_literal_25.data);
  moonbit_decref_cycle_free(_M0L1sS537);
  return _result_2627;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS535) {
  int32_t _M0L6_2atmpS1656;
  uint32_t _M0L6_2atmpS1655;
  uint32_t _M0L6_2atmpS1654;
  int32_t _M0L6_2atmpS1653;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1656 = _M0L1eS535 * 1217359;
  _M0L6_2atmpS1655 = *(uint32_t*)&_M0L6_2atmpS1656;
  _M0L6_2atmpS1654 = _M0L6_2atmpS1655 >> 19;
  _M0L6_2atmpS1653 = *(int32_t*)&_M0L6_2atmpS1654;
  return _M0L6_2atmpS1653 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS534) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS534 != _M0L4selfS534) {
    return 0;
  } else if (_M0L4selfS534 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS534 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS534;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS533) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS533 != _M0L4selfS533) {
    return 0ll;
  } else if (_M0L4selfS533 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS533 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS533;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS530
) {
  float* _M0L6_2atmpS1650;
  struct _M0TPB5ArrayGfE* _block_2628;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1650 = (float*)moonbit_make_float_array_raw(_M0L3lenS530);
  _block_2628
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2628)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2628->$0 = _M0L6_2atmpS1650;
  _block_2628->$1 = _M0L3lenS530;
  return _block_2628;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS531
) {
  uint8_t* _M0L6_2atmpS1651;
  struct _M0TPB5ArrayGbE* _block_2629;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1651 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS531);
  _block_2629
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2629)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _block_2629->$0 = _M0L6_2atmpS1651;
  _block_2629->$1 = _M0L3lenS531;
  return _block_2629;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS532
) {
  int32_t* _M0L6_2atmpS1652;
  struct _M0TPB5ArrayGiE* _block_2630;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1652 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS532);
  _block_2630
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2630)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2630->$0 = _M0L6_2atmpS1652;
  _block_2630->$1 = _M0L3lenS532;
  return _block_2630;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS526,
  int32_t _M0L5indexS527
) {
  uint64_t* _M0L6_2atmpS1648;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1648 = _M0L4selfS526;
  if (
    _M0L5indexS527 < 0
    || _M0L5indexS527 >= Moonbit_array_length(_M0L6_2atmpS1648)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1648[_M0L5indexS527];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS528,
  int32_t _M0L5indexS529
) {
  uint32_t* _M0L6_2atmpS1649;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1649 = _M0L4selfS528;
  if (
    _M0L5indexS529 < 0
    || _M0L5indexS529 >= Moonbit_array_length(_M0L6_2atmpS1649)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1649[_M0L5indexS529];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS525
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS525, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS524) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS524, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS523) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS523;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS511,
  moonbit_string_t _M0L5valueS513
) {
  int32_t _M0L3lenS1620;
  moonbit_string_t* _M0L6_2atmpS1622;
  int32_t _M0L6_2atmpS1621;
  int32_t _M0L6lengthS512;
  moonbit_string_t* _M0L3bufS1625;
  moonbit_string_t _M0L6_2aoldS2515;
  int32_t _M0L6_2atmpS1626;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1620 = _M0L4selfS511->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1622 = _M0MPC15array5Array6bufferGsE(_M0L4selfS511);
  _M0L6_2atmpS1621 = Moonbit_array_length(_M0L6_2atmpS1622);
  moonbit_decref_cycle_free(_M0L6_2atmpS1622);
  if (_M0L3lenS1620 == _M0L6_2atmpS1621) {
    int32_t _M0L3lenS1624 = _M0L4selfS511->$1;
    int32_t _M0L6_2atmpS1623 = _M0L3lenS1624 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS511, _M0L6_2atmpS1623);
  }
  _M0L6lengthS512 = _M0L4selfS511->$1;
  _M0L3bufS1625 = _M0L4selfS511->$0;
  _M0L6_2aoldS2515 = (moonbit_string_t)_M0L3bufS1625[_M0L6lengthS512];
  moonbit_decref_cycle_free(_M0L6_2aoldS2515);
  _M0L3bufS1625[_M0L6lengthS512] = _M0L5valueS513;
  _M0L6_2atmpS1626 = _M0L6lengthS512 + 1;
  _M0L4selfS511->$1 = _M0L6_2atmpS1626;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS514,
  struct _M0TUsiE* _M0L5valueS516
) {
  int32_t _M0L3lenS1627;
  struct _M0TUsiE** _M0L6_2atmpS1629;
  int32_t _M0L6_2atmpS1628;
  int32_t _M0L6lengthS515;
  struct _M0TUsiE** _M0L3bufS1632;
  struct _M0TUsiE* _M0L6_2aoldS2516;
  int32_t _M0L6_2atmpS1633;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1627 = _M0L4selfS514->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1629 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS514);
  _M0L6_2atmpS1628 = Moonbit_array_length(_M0L6_2atmpS1629);
  moonbit_decref_cycle_free(_M0L6_2atmpS1629);
  if (_M0L3lenS1627 == _M0L6_2atmpS1628) {
    int32_t _M0L3lenS1631 = _M0L4selfS514->$1;
    int32_t _M0L6_2atmpS1630 = _M0L3lenS1631 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS514, _M0L6_2atmpS1630);
  }
  _M0L6lengthS515 = _M0L4selfS514->$1;
  _M0L3bufS1632 = _M0L4selfS514->$0;
  _M0L6_2aoldS2516 = (struct _M0TUsiE*)_M0L3bufS1632[_M0L6lengthS515];
  if (_M0L6_2aoldS2516) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2516);
  }
  _M0L3bufS1632[_M0L6lengthS515] = _M0L5valueS516;
  _M0L6_2atmpS1633 = _M0L6lengthS515 + 1;
  _M0L4selfS514->$1 = _M0L6_2atmpS1633;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS517,
  float _M0L5valueS519
) {
  int32_t _M0L3lenS1634;
  float* _M0L6_2atmpS1636;
  int32_t _M0L6_2atmpS1635;
  int32_t _M0L6lengthS518;
  float* _M0L3bufS1639;
  int32_t _M0L6_2atmpS1640;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1634 = _M0L4selfS517->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1636 = _M0MPC15array5Array6bufferGfE(_M0L4selfS517);
  _M0L6_2atmpS1635 = Moonbit_array_length(_M0L6_2atmpS1636);
  moonbit_decref_cycle_free(_M0L6_2atmpS1636);
  if (_M0L3lenS1634 == _M0L6_2atmpS1635) {
    int32_t _M0L3lenS1638 = _M0L4selfS517->$1;
    int32_t _M0L6_2atmpS1637 = _M0L3lenS1638 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS517, _M0L6_2atmpS1637);
  }
  _M0L6lengthS518 = _M0L4selfS517->$1;
  _M0L3bufS1639 = _M0L4selfS517->$0;
  _M0L3bufS1639[_M0L6lengthS518] = _M0L5valueS519;
  _M0L6_2atmpS1640 = _M0L6lengthS518 + 1;
  _M0L4selfS517->$1 = _M0L6_2atmpS1640;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS520,
  int32_t _M0L5valueS522
) {
  int32_t _M0L3lenS1641;
  int32_t* _M0L6_2atmpS1643;
  int32_t _M0L6_2atmpS1642;
  int32_t _M0L6lengthS521;
  int32_t* _M0L3bufS1646;
  int32_t _M0L6_2atmpS1647;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1641 = _M0L4selfS520->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1643 = _M0MPC15array5Array6bufferGiE(_M0L4selfS520);
  _M0L6_2atmpS1642 = Moonbit_array_length(_M0L6_2atmpS1643);
  moonbit_decref_cycle_free(_M0L6_2atmpS1643);
  if (_M0L3lenS1641 == _M0L6_2atmpS1642) {
    int32_t _M0L3lenS1645 = _M0L4selfS520->$1;
    int32_t _M0L6_2atmpS1644 = _M0L3lenS1645 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS520, _M0L6_2atmpS1644);
  }
  _M0L6lengthS521 = _M0L4selfS520->$1;
  _M0L3bufS1646 = _M0L4selfS520->$0;
  _M0L3bufS1646[_M0L6lengthS521] = _M0L5valueS522;
  _M0L6_2atmpS1647 = _M0L6lengthS521 + 1;
  _M0L4selfS520->$1 = _M0L6_2atmpS1647;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS496,
  int32_t _M0L8requiredS498
) {
  int32_t _M0L8old__capS495;
  int32_t _M0L3lenS1616;
  int32_t _M0L8new__capS497;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS495 = _M0MPC15array5Array8capacityGsE(_M0L4selfS496);
  _M0L3lenS1616 = _M0L4selfS496->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS497
  = _M0FPB23array__growth__capacity(_M0L8old__capS495, _M0L3lenS1616, _M0L8requiredS498);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS496, _M0L8new__capS497);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS500,
  int32_t _M0L8requiredS502
) {
  int32_t _M0L8old__capS499;
  int32_t _M0L3lenS1617;
  int32_t _M0L8new__capS501;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS499 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS500);
  _M0L3lenS1617 = _M0L4selfS500->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS501
  = _M0FPB23array__growth__capacity(_M0L8old__capS499, _M0L3lenS1617, _M0L8requiredS502);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS500, _M0L8new__capS501);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS504,
  int32_t _M0L8requiredS506
) {
  int32_t _M0L8old__capS503;
  int32_t _M0L3lenS1618;
  int32_t _M0L8new__capS505;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS503 = _M0MPC15array5Array8capacityGiE(_M0L4selfS504);
  _M0L3lenS1618 = _M0L4selfS504->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS505
  = _M0FPB23array__growth__capacity(_M0L8old__capS503, _M0L3lenS1618, _M0L8requiredS506);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS504, _M0L8new__capS505);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS508,
  int32_t _M0L8requiredS510
) {
  int32_t _M0L8old__capS507;
  int32_t _M0L3lenS1619;
  int32_t _M0L8new__capS509;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS507 = _M0MPC15array5Array8capacityGfE(_M0L4selfS508);
  _M0L3lenS1619 = _M0L4selfS508->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS509
  = _M0FPB23array__growth__capacity(_M0L8old__capS507, _M0L3lenS1619, _M0L8requiredS510);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS508, _M0L8new__capS509);
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
  moonbit_string_t* _M0L6_2aoldS2517;
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
  _M0L6_2aoldS2517 = _M0L4selfS472->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2517);
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
  struct _M0TUsiE** _M0L6_2aoldS2518;
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
  _M0L6_2aoldS2518 = _M0L4selfS478->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2518);
  _M0L4selfS478->$0 = _M0L8new__bufS482;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS484,
  int32_t _M0L13new__capacityS487
) {
  int32_t* _M0L8old__bufS483;
  int32_t _M0L3lenS485;
  int32_t _M0L9copy__lenS486;
  int32_t* _M0L8new__bufS488;
  int32_t* _M0L6_2aoldS2519;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS483, _M0L13new__capacityS487, _M0L9copy__lenS486, 0, 0);
  _M0L6_2aoldS2519 = _M0L4selfS484->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2519);
  _M0L4selfS484->$0 = _M0L8new__bufS488;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS490,
  int32_t _M0L13new__capacityS493
) {
  float* _M0L8old__bufS489;
  int32_t _M0L3lenS491;
  int32_t _M0L9copy__lenS492;
  float* _M0L8new__bufS494;
  float* _M0L6_2aoldS2520;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS489, _M0L13new__capacityS493, _M0L9copy__lenS492, 0, 0);
  _M0L6_2aoldS2520 = _M0L4selfS490->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2520);
  _M0L4selfS490->$0 = _M0L8new__bufS494;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS467
) {
  moonbit_string_t* _M0L6_2atmpS1612;
  int32_t _result_2631;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1612 = _M0MPC15array5Array6bufferGsE(_M0L4selfS467);
  _result_2631 = Moonbit_array_length(_M0L6_2atmpS1612);
  moonbit_decref_cycle_free(_M0L6_2atmpS1612);
  return _result_2631;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS468
) {
  struct _M0TUsiE** _M0L6_2atmpS1613;
  int32_t _result_2632;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1613 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS468);
  _result_2632 = Moonbit_array_length(_M0L6_2atmpS1613);
  moonbit_decref_cycle_free(_M0L6_2atmpS1613);
  return _result_2632;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS469
) {
  int32_t* _M0L6_2atmpS1614;
  int32_t _result_2633;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1614 = _M0MPC15array5Array6bufferGiE(_M0L4selfS469);
  _result_2633 = Moonbit_array_length(_M0L6_2atmpS1614);
  moonbit_decref_cycle_free(_M0L6_2atmpS1614);
  return _result_2633;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS470
) {
  float* _M0L6_2atmpS1615;
  int32_t _result_2634;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1615 = _M0MPC15array5Array6bufferGfE(_M0L4selfS470);
  _result_2634 = Moonbit_array_length(_M0L6_2atmpS1615);
  moonbit_decref_cycle_free(_M0L6_2atmpS1615);
  return _result_2634;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
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

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS458) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS458->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS459) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS459->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS2521;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2521 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2521);
  return _M0L8_2afieldS2521;
}

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS453
) {
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L8_2afieldS2522;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2522 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2522);
  return _M0L8_2afieldS2522;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS454
) {
  moonbit_string_t* _M0L8_2afieldS2523;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2523 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2523);
  return _M0L8_2afieldS2523;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS455
) {
  struct _M0TUsiE** _M0L8_2afieldS2524;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2524 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2524);
  return _M0L8_2afieldS2524;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS456) {
  int32_t* _M0L8_2afieldS2525;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2525 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2525);
  return _M0L8_2afieldS2525;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS457) {
  uint8_t* _M0L8_2afieldS2526;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2526 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2526);
  return _M0L8_2afieldS2526;
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
  int32_t _M0L3endS1610;
  int32_t _M0L5startS1611;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1609;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1602;
  int32_t _M0L6_2atmpS1601;
  int32_t _if__result_2636;
  uint16_t* _M0L4dataS1603;
  int32_t _M0L3lenS1604;
  moonbit_string_t _M0L6_2atmpS1605;
  int32_t _M0L6_2atmpS1606;
  int32_t _M0L3lenS1608;
  int32_t _M0L6_2atmpS1607;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1610 = _M0L3strS448.$2;
  _M0L5startS1611 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1610 - _M0L5startS1611;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1609 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1609 + _M0L8str__lenS447;
  _M0L4dataS1602 = _M0L4selfS450->$0;
  _M0L6_2atmpS1601 = Moonbit_array_length(_M0L4dataS1602);
  if (_M0L8requiredS449 > _M0L6_2atmpS1601) {
    _if__result_2636 = 1;
  } else {
    int32_t _M0L3lenS1600 = _M0L4selfS450->$1;
    _if__result_2636 = _M0L8requiredS449 < _M0L3lenS1600;
  }
  if (_if__result_2636) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1603 = _M0L4selfS450->$0;
  _M0L3lenS1604 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1603);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1605 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1606 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1603, _M0L3lenS1604, _M0L6_2atmpS1605, _M0L6_2atmpS1606, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1603);
  moonbit_decref_cycle_free(_M0L6_2atmpS1605);
  _M0L3lenS1608 = _M0L4selfS450->$1;
  _M0L6_2atmpS1607 = _M0L3lenS1608 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1607;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2637;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1599;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1598;
  moonbit_string_t _result_2638;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1597 = Moonbit_array_length(_M0L3strS444);
    _if__result_2637 = _M0L3endS443 == _M0L6_2atmpS1597;
  } else {
    _if__result_2637 = 0;
  }
  if (_if__result_2637) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1599 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1599, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1598 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2638
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1598, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1598);
  return _result_2638;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2639;
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
      int32_t _M0L6_2atmpS1596 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2639 = _M0L6_2atmpS1596 <= _M0L3lenS436;
    } else {
      _if__result_2639 = 0;
    }
  } else {
    _if__result_2639 = 0;
  }
  if (_if__result_2639) {
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
  int32_t _M0L6_2atmpS1595;
  int32_t _M0L6_2atmpS1594;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1593;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1595 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1594 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1595;
  _M0L2e1S422 = _M0L6_2atmpS1594 - 1;
  _M0L6_2atmpS1593 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1593 - 1;
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
        int32_t _M0L6_2atmpS1590 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1589 = (int32_t)_M0L6_2atmpS1590;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1589;
        uint32_t _M0L6_2atmpS1585 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1584;
        int32_t _M0L6_2atmpS1586;
        uint32_t _M0L6_2atmpS1588;
        int32_t _M0L6_2atmpS1587;
        int32_t _M0L6_2atmpS1591;
        int32_t _M0L6_2atmpS1592;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1584 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1585);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1584;
        _M0L6_2atmpS1586 = _M0L1jS433 + 1;
        _M0L6_2atmpS1588 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1587 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1588);
        if (
          _M0L6_2atmpS1586 < 0
          || _M0L6_2atmpS1586 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1586] = _M0L6_2atmpS1587;
        _M0L6_2atmpS1591 = _M0L1iS432 + 1;
        _M0L6_2atmpS1592 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1591;
        _M0L1jS433 = _M0L6_2atmpS1592;
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
  int32_t _M0L6_2atmpS1583;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1583 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1583 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1582 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1582;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1579;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1579 = 1;
      } else {
        _M0L6_2atmpS1579 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1579;
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
      int32_t _M0L6_2atmpS1580;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1580 = 1;
      } else {
        _M0L6_2atmpS1580 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1580;
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
      int32_t _M0L6_2atmpS1581;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1581 = 1;
      } else {
        _M0L6_2atmpS1581 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1581;
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
  int32_t _M0L6_2atmpS1578;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1578 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1578;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1555 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1555;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1554 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1553 = 48 + _M0L6_2atmpS1554;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1553;
      int32_t _M0L6_2atmpS1552 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1551 = 48 + _M0L6_2atmpS1552;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1551;
      int32_t _M0L6_2atmpS1550 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1549 = 48 + _M0L6_2atmpS1550;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1549;
      int32_t _M0L6_2atmpS1548 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1547 = 48 + _M0L6_2atmpS1548;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1547;
      int32_t _M0L6_2atmpS1539 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1538 = _M0L6_2atmpS1539 - 4;
      int32_t _M0L6_2atmpS1541;
      int32_t _M0L6_2atmpS1540;
      int32_t _M0L6_2atmpS1543;
      int32_t _M0L6_2atmpS1542;
      int32_t _M0L6_2atmpS1545;
      int32_t _M0L6_2atmpS1544;
      int32_t _M0L6_2atmpS1546;
      _M0L6bufferS381[_M0L6_2atmpS1538] = _M0L6d1__hiS377;
      _M0L6_2atmpS1541 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1540 = _M0L6_2atmpS1541 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1540] = _M0L6d1__loS378;
      _M0L6_2atmpS1543 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1542 = _M0L6_2atmpS1543 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1542] = _M0L6d2__hiS379;
      _M0L6_2atmpS1545 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1544 = _M0L6_2atmpS1545 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1544] = _M0L6d2__loS380;
      _M0L6_2atmpS1546 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1546;
      continue;
    } else {
      int32_t _M0L6_2atmpS1577 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1577;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1564 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1563 = 48 + _M0L6_2atmpS1564;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1563;
          int32_t _M0L6_2atmpS1562 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1561 = 48 + _M0L6_2atmpS1562;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1561;
          int32_t _M0L6_2atmpS1557 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1556 = _M0L6_2atmpS1557 - 2;
          int32_t _M0L6_2atmpS1559;
          int32_t _M0L6_2atmpS1558;
          int32_t _M0L6_2atmpS1560;
          _M0L6bufferS381[_M0L6_2atmpS1556] = _M0L5d__hiS388;
          _M0L6_2atmpS1559 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1558 = _M0L6_2atmpS1559 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1558] = _M0L5d__loS389;
          _M0L6_2atmpS1560 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1560;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1572 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1571 = 48 + _M0L6_2atmpS1572;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1571;
          int32_t _M0L6_2atmpS1570 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1569 = 48 + _M0L6_2atmpS1570;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1569;
          int32_t _M0L6_2atmpS1566 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1565 = _M0L6_2atmpS1566 - 2;
          int32_t _M0L6_2atmpS1568;
          int32_t _M0L6_2atmpS1567;
          _M0L6bufferS381[_M0L6_2atmpS1565] = _M0L5d__hiS391;
          _M0L6_2atmpS1568 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1567 = _M0L6_2atmpS1568 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1567] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1576 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1573 = _M0L6_2atmpS1576 - 1;
          int32_t _M0L6_2atmpS1575 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1574 = (uint16_t)_M0L6_2atmpS1575;
          _M0L6bufferS381[_M0L6_2atmpS1573] = _M0L6_2atmpS1574;
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
  int32_t _M0L6_2atmpS1523;
  int32_t _M0L6_2atmpS1522;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1523 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1522 = _M0L5radixS355 & _M0L6_2atmpS1523;
  if (_M0L6_2atmpS1522 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1530;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1530 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1530;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1529 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1529;
        int32_t _M0L6_2atmpS1526 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1524 = _M0L6_2atmpS1526 - 1;
        int32_t _M0L6_2atmpS1525 =
          ((moonbit_string_t)moonbit_string_literal_28.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1527;
        uint64_t _M0L6_2atmpS1528;
        _M0L6bufferS361[_M0L6_2atmpS1524] = _M0L6_2atmpS1525;
        _M0L6_2atmpS1527 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1528 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1527;
        _M0L1nS359 = _M0L6_2atmpS1528;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1537 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1537;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1536 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1535 = _M0L1nS367 - _M0L6_2atmpS1536;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1535;
        int32_t _M0L6_2atmpS1533 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1531 = _M0L6_2atmpS1533 - 1;
        int32_t _M0L6_2atmpS1532 =
          ((moonbit_string_t)moonbit_string_literal_28.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1534;
        _M0L6bufferS361[_M0L6_2atmpS1531] = _M0L6_2atmpS1532;
        _M0L6_2atmpS1534 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1534;
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
  int32_t _M0L6_2atmpS1521;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1521 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1521;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1518 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1518;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1512 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1510 = _M0L6_2atmpS1512 - 2;
      int32_t _M0L6_2atmpS1511 =
        ((moonbit_string_t)moonbit_string_literal_28.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1515;
      int32_t _M0L6_2atmpS1513;
      int32_t _M0L6_2atmpS1514;
      int32_t _M0L6_2atmpS1516;
      uint64_t _M0L6_2atmpS1517;
      _M0L6bufferS348[_M0L6_2atmpS1510] = _M0L6_2atmpS1511;
      _M0L6_2atmpS1515 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1513 = _M0L6_2atmpS1515 - 1;
      _M0L6_2atmpS1514
      = ((moonbit_string_t)moonbit_string_literal_28.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1513] = _M0L6_2atmpS1514;
      _M0L6_2atmpS1516 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1517 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1516;
      _M0L1nS344 = _M0L6_2atmpS1517;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1520 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1520;
      int32_t _M0L6_2atmpS1519 =
        ((moonbit_string_t)moonbit_string_literal_28.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1519;
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
      uint64_t _M0L6_2atmpS1508 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1509 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1508;
      _M0L5countS341 = _M0L6_2atmpS1509;
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
    int32_t _M0L6_2atmpS1507;
    int32_t _M0L6_2atmpS1506;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1507 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1506 = _M0L6_2atmpS1507 / 4;
    return _M0L6_2atmpS1506 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1505 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1505;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1502;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1502 = 1;
      } else {
        _M0L6_2atmpS1502 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1502;
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
      int32_t _M0L6_2atmpS1503;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1503 = 1;
      } else {
        _M0L6_2atmpS1503 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1503;
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
      int32_t _M0L6_2atmpS1504;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1504 = 1;
      } else {
        _M0L6_2atmpS1504 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1504;
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
      uint32_t _M0L6_2atmpS1500 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1501 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1500;
      _M0L5countS315 = _M0L6_2atmpS1501;
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
    int32_t _M0L6_2atmpS1499;
    int32_t _M0L6_2atmpS1498;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1499 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1498 = _M0L6_2atmpS1499 / 4;
    return _M0L6_2atmpS1498 + 1;
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
  int32_t _M0L6_2atmpS1497;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1497 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1497;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1474 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1474;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1473 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1472 = 48 + _M0L6_2atmpS1473;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1472;
      int32_t _M0L6_2atmpS1471 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1470 = 48 + _M0L6_2atmpS1471;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1470;
      int32_t _M0L6_2atmpS1469 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1468 = 48 + _M0L6_2atmpS1469;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1468;
      int32_t _M0L6_2atmpS1467 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1466 = 48 + _M0L6_2atmpS1467;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1466;
      int32_t _M0L6_2atmpS1458 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1457 = _M0L6_2atmpS1458 - 4;
      int32_t _M0L6_2atmpS1460;
      int32_t _M0L6_2atmpS1459;
      int32_t _M0L6_2atmpS1462;
      int32_t _M0L6_2atmpS1461;
      int32_t _M0L6_2atmpS1464;
      int32_t _M0L6_2atmpS1463;
      int32_t _M0L6_2atmpS1465;
      _M0L6bufferS294[_M0L6_2atmpS1457] = _M0L6d1__hiS290;
      _M0L6_2atmpS1460 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1459 = _M0L6_2atmpS1460 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1459] = _M0L6d1__loS291;
      _M0L6_2atmpS1462 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1461 = _M0L6_2atmpS1462 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1461] = _M0L6d2__hiS292;
      _M0L6_2atmpS1464 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1463 = _M0L6_2atmpS1464 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1463] = _M0L6d2__loS293;
      _M0L6_2atmpS1465 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1465;
      continue;
    } else {
      int32_t _M0L6_2atmpS1496 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1496;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1483 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1482 = 48 + _M0L6_2atmpS1483;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1482;
          int32_t _M0L6_2atmpS1481 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1480 = 48 + _M0L6_2atmpS1481;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1480;
          int32_t _M0L6_2atmpS1476 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1475 = _M0L6_2atmpS1476 - 2;
          int32_t _M0L6_2atmpS1478;
          int32_t _M0L6_2atmpS1477;
          int32_t _M0L6_2atmpS1479;
          _M0L6bufferS294[_M0L6_2atmpS1475] = _M0L5d__hiS301;
          _M0L6_2atmpS1478 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1477 = _M0L6_2atmpS1478 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1477] = _M0L5d__loS302;
          _M0L6_2atmpS1479 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1479;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1491 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1490 = 48 + _M0L6_2atmpS1491;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1490;
          int32_t _M0L6_2atmpS1489 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1488 = 48 + _M0L6_2atmpS1489;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1488;
          int32_t _M0L6_2atmpS1485 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1484 = _M0L6_2atmpS1485 - 2;
          int32_t _M0L6_2atmpS1487;
          int32_t _M0L6_2atmpS1486;
          _M0L6bufferS294[_M0L6_2atmpS1484] = _M0L5d__hiS304;
          _M0L6_2atmpS1487 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1486 = _M0L6_2atmpS1487 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1486] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1495 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1492 = _M0L6_2atmpS1495 - 1;
          int32_t _M0L6_2atmpS1494 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1493 = (uint16_t)_M0L6_2atmpS1494;
          _M0L6bufferS294[_M0L6_2atmpS1492] = _M0L6_2atmpS1493;
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
  int32_t _M0L6_2atmpS1442;
  int32_t _M0L6_2atmpS1441;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1442 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1441 = _M0L5radixS268 & _M0L6_2atmpS1442;
  if (_M0L6_2atmpS1441 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1449;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1449 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1449;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1448 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1448;
        int32_t _M0L6_2atmpS1445 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1443 = _M0L6_2atmpS1445 - 1;
        int32_t _M0L6_2atmpS1444 =
          ((moonbit_string_t)moonbit_string_literal_28.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1446;
        uint32_t _M0L6_2atmpS1447;
        _M0L6bufferS274[_M0L6_2atmpS1443] = _M0L6_2atmpS1444;
        _M0L6_2atmpS1446 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1447 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1446;
        _M0L1nS272 = _M0L6_2atmpS1447;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1456 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1456;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1455 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1454 = _M0L1nS280 - _M0L6_2atmpS1455;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1454;
        int32_t _M0L6_2atmpS1452 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1450 = _M0L6_2atmpS1452 - 1;
        int32_t _M0L6_2atmpS1451 =
          ((moonbit_string_t)moonbit_string_literal_28.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1453;
        _M0L6bufferS274[_M0L6_2atmpS1450] = _M0L6_2atmpS1451;
        _M0L6_2atmpS1453 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1453;
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
  int32_t _M0L6_2atmpS1440;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1440 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1440;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1437 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1437;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1431 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1429 = _M0L6_2atmpS1431 - 2;
      int32_t _M0L6_2atmpS1430 =
        ((moonbit_string_t)moonbit_string_literal_28.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1434;
      int32_t _M0L6_2atmpS1432;
      int32_t _M0L6_2atmpS1433;
      int32_t _M0L6_2atmpS1435;
      uint32_t _M0L6_2atmpS1436;
      _M0L6bufferS261[_M0L6_2atmpS1429] = _M0L6_2atmpS1430;
      _M0L6_2atmpS1434 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1432 = _M0L6_2atmpS1434 - 1;
      _M0L6_2atmpS1433
      = ((moonbit_string_t)moonbit_string_literal_28.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1432] = _M0L6_2atmpS1433;
      _M0L6_2atmpS1435 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1436 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1435;
      _M0L1nS257 = _M0L6_2atmpS1436;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1439 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1439;
      int32_t _M0L6_2atmpS1438 =
        ((moonbit_string_t)moonbit_string_literal_28.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1438;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1428;
  moonbit_string_t _result_2653;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1428
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1428);
  if (_M0L6_2atmpS1428.$1) {
    moonbit_decref(_M0L6_2atmpS1428.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2653 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2653;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1425;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1425 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1425);
  moonbit_decref_cycle_free(_M0L6_2atmpS1425);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1426;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1426 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1426);
  moonbit_decref_cycle_free(_M0L6_2atmpS1426);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1427;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1427 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1427);
  moonbit_decref_cycle_free(_M0L6_2atmpS1427);
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
  moonbit_string_t _M0L8_2afieldS2527;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2527 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2527);
  return _M0L8_2afieldS2527;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1424;
  int64_t _M0L6_2atmpS1423;
  struct _M0TPC16string10StringView _M0L6_2atmpS1422;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1424 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1423 = (int64_t)_M0L6_2atmpS1424;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1422
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1423);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1422);
  moonbit_decref_cycle_free(_M0L6_2atmpS1422.$0);
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
  int32_t _M0L6_2atmpS1406;
  int32_t _if__result_2654;
  int32_t _M0L6_2atmpS1414;
  int32_t _if__result_2655;
  int32_t _M0L6_2atmpS1416;
  int32_t _M0L6_2atmpS1417;
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
  _M0L6_2atmpS1406 = _M0Lm2loS236;
  if (_M0L6_2atmpS1406 > 0) {
    int32_t _M0L6_2atmpS1405 = _M0Lm2loS236;
    if (_M0L6_2atmpS1405 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1404 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1403 = _M0L4selfS235[_M0L6_2atmpS1404];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1403)) {
        int32_t _M0L6_2atmpS1402 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1401 = _M0L6_2atmpS1402 - 1;
        int32_t _M0L6_2atmpS1400 = _M0L4selfS235[_M0L6_2atmpS1401];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2654
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1400);
      } else {
        _if__result_2654 = 0;
      }
    } else {
      _if__result_2654 = 0;
    }
  } else {
    _if__result_2654 = 0;
  }
  if (_if__result_2654) {
    int32_t _M0L6_2atmpS1407 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1407 + 1;
  }
  _M0L6_2atmpS1414 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1414 > 0) {
    int32_t _M0L6_2atmpS1413 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1413 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1412 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1411 = _M0L4selfS235[_M0L6_2atmpS1412];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1411)) {
        int32_t _M0L6_2atmpS1410 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1409 = _M0L6_2atmpS1410 - 1;
        int32_t _M0L6_2atmpS1408 = _M0L4selfS235[_M0L6_2atmpS1409];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2655
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1408);
      } else {
        _if__result_2655 = 0;
      }
    } else {
      _if__result_2655 = 0;
    }
  } else {
    _if__result_2655 = 0;
  }
  if (_if__result_2655) {
    int32_t _M0L6_2atmpS1415 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1415 - 1;
  }
  _M0L6_2atmpS1416 = _M0Lm2loS236;
  _M0L6_2atmpS1417 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1416 >= _M0L6_2atmpS1417) {
    int32_t _M0L6_2atmpS1418 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1419 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1418,
                                                 .$2 = _M0L6_2atmpS1419};
  } else {
    int32_t _M0L6_2atmpS1420 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1421 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1420,
                                                 .$2 = _M0L6_2atmpS1421};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1399;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1399
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1399);
  if (_M0L6_2atmpS1399.$1) {
    moonbit_decref(_M0L6_2atmpS1399.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1398;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1398
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1398);
  if (_M0L6_2atmpS1398.$1) {
    moonbit_decref(_M0L6_2atmpS1398.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1397;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1397 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1397;
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
  int32_t _M0L6_2atmpS1396;
  struct _M0TPC16string10StringView _M0L6_2atmpS1394;
  struct _M0TPB6Logger _M0L6_2atmpS1395;
  moonbit_string_t _result_2656;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1396 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1394
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1396
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1395
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1394, _M0L6_2atmpS1395, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1394.$0);
  if (_M0L6_2atmpS1395.$1) {
    moonbit_decref(_M0L6_2atmpS1395.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2656 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2656;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1392;
  int32_t _M0L5startS1393;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1392 = _M0L4selfS218.$2;
  _M0L5startS1393 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1392 - _M0L5startS1393;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 79, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1389;
    int32_t _M0L5startS1391;
    int32_t _M0L6_2atmpS1390;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1373;
    int32_t _M0L6_2atmpS1374;
    int32_t _M0L6_2atmpS1375;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1389 = _M0L4selfS218.$0;
    _M0L5startS1391 = _M0L4selfS218.$1;
    _M0L6_2atmpS1390 = _M0L5startS1391 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1389[_M0L6_2atmpS1390];
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
        int32_t _M0L6_2atmpS1376;
        int32_t _M0L6_2atmpS1377;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_29.data);
        _M0L6_2atmpS1376 = _M0L1iS220 + 1;
        _M0L6_2atmpS1377 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1376;
        _M0L3segS221 = _M0L6_2atmpS1377;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1378;
        int32_t _M0L6_2atmpS1379;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_30.data);
        _M0L6_2atmpS1378 = _M0L1iS220 + 1;
        _M0L6_2atmpS1379 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1378;
        _M0L3segS221 = _M0L6_2atmpS1379;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1380;
        int32_t _M0L6_2atmpS1381;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_31.data);
        _M0L6_2atmpS1380 = _M0L1iS220 + 1;
        _M0L6_2atmpS1381 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1380;
        _M0L3segS221 = _M0L6_2atmpS1381;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1382;
        int32_t _M0L6_2atmpS1383;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_32.data);
        _M0L6_2atmpS1382 = _M0L1iS220 + 1;
        _M0L6_2atmpS1383 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1382;
        _M0L3segS221 = _M0L6_2atmpS1383;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1385;
          moonbit_string_t _M0L6_2atmpS1384;
          int32_t _M0L6_2atmpS1386;
          int32_t _M0L6_2atmpS1387;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_33.data);
          _M0L6_2atmpS1385 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1384 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1385);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1384);
          moonbit_decref_cycle_free(_M0L6_2atmpS1384);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1386 = _M0L1iS220 + 1;
          _M0L6_2atmpS1387 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1386;
          _M0L3segS221 = _M0L6_2atmpS1387;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1388 = _M0L1iS220 + 1;
          int32_t _tmp_2659 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1388;
          _M0L3segS221 = _tmp_2659;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2658;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1373 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1373);
    _M0L6_2atmpS1374 = _M0L1iS220 + 1;
    _M0L6_2atmpS1375 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1374;
    _M0L3segS221 = _M0L6_2atmpS1375;
    continue;
    joinlet_2658:;
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
    int64_t _M0L6_2atmpS1372 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1371;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1371
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1372);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1371);
    moonbit_decref_cycle_free(_M0L6_2atmpS1371.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1369;
  int32_t _M0L5startS1370;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1347;
  int32_t _if__result_2660;
  int32_t _M0L6_2atmpS1357;
  int32_t _if__result_2661;
  int32_t _M0L6_2atmpS1359;
  int32_t _M0L6_2atmpS1360;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1369 = _M0L4selfS201.$2;
  _M0L5startS1370 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1369 - _M0L5startS1370;
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
  _M0L6_2atmpS1347 = _M0Lm2loS202;
  if (_M0L6_2atmpS1347 > 0) {
    int32_t _M0L6_2atmpS1346 = _M0Lm2loS202;
    if (_M0L6_2atmpS1346 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1345 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1344 = _M0L4baseS209 + _M0L6_2atmpS1345;
      int32_t _M0L6_2atmpS1343 = _M0L3strS208[_M0L6_2atmpS1344];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1343)) {
        int32_t _M0L6_2atmpS1342 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1341 = _M0L4baseS209 + _M0L6_2atmpS1342;
        int32_t _M0L6_2atmpS1340 = _M0L6_2atmpS1341 - 1;
        int32_t _M0L6_2atmpS1339 = _M0L3strS208[_M0L6_2atmpS1340];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2660
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1339);
      } else {
        _if__result_2660 = 0;
      }
    } else {
      _if__result_2660 = 0;
    }
  } else {
    _if__result_2660 = 0;
  }
  if (_if__result_2660) {
    int32_t _M0L6_2atmpS1348 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1348 + 1;
  }
  _M0L6_2atmpS1357 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1357 > 0) {
    int32_t _M0L6_2atmpS1356 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1356 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1355 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1354 = _M0L4baseS209 + _M0L6_2atmpS1355;
      int32_t _M0L6_2atmpS1353 = _M0L3strS208[_M0L6_2atmpS1354];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1353)) {
        int32_t _M0L6_2atmpS1352 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1351 = _M0L4baseS209 + _M0L6_2atmpS1352;
        int32_t _M0L6_2atmpS1350 = _M0L6_2atmpS1351 - 1;
        int32_t _M0L6_2atmpS1349 = _M0L3strS208[_M0L6_2atmpS1350];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2661
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1349);
      } else {
        _if__result_2661 = 0;
      }
    } else {
      _if__result_2661 = 0;
    }
  } else {
    _if__result_2661 = 0;
  }
  if (_if__result_2661) {
    int32_t _M0L6_2atmpS1358 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1358 - 1;
  }
  _M0L6_2atmpS1359 = _M0Lm2loS202;
  _M0L6_2atmpS1360 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1359 >= _M0L6_2atmpS1360) {
    int32_t _M0L6_2atmpS1364 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1361 = _M0L4baseS209 + _M0L6_2atmpS1364;
    int32_t _M0L6_2atmpS1363 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1362 = _M0L4baseS209 + _M0L6_2atmpS1363;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1361,
                                                 .$2 = _M0L6_2atmpS1362};
  } else {
    int32_t _M0L6_2atmpS1368 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1365 = _M0L4baseS209 + _M0L6_2atmpS1368;
    int32_t _M0L6_2atmpS1367 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1366 = _M0L4baseS209 + _M0L6_2atmpS1367;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1365,
                                                 .$2 = _M0L6_2atmpS1366};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1336;
  int32_t _M0L6_2atmpS1335;
  int32_t _M0L6_2atmpS1338;
  int32_t _M0L6_2atmpS1337;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1334;
  moonbit_string_t _result_2662;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1336 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1335
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1336);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1335);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1338 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1337
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1338);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1337);
  _M0L6_2atmpS1334 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2662 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1334);
  moonbit_decref_cycle_free(_M0L6_2atmpS1334);
  return _result_2662;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1331;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1331 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1331);
  } else {
    int32_t _M0L6_2atmpS1333;
    int32_t _M0L6_2atmpS1332;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1333 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1332 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1333, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1332);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1329;
  int32_t _M0L6_2atmpS1330;
  int32_t _M0L6_2atmpS1328;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1329 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1330 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1328 = _M0L6_2atmpS1329 - _M0L6_2atmpS1330;
  return _M0L6_2atmpS1328 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1326;
  int32_t _M0L6_2atmpS1327;
  int32_t _M0L6_2atmpS1325;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1326 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1327 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1325 = _M0L6_2atmpS1326 % _M0L6_2atmpS1327;
  return _M0L6_2atmpS1325 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1323;
  int32_t _M0L6_2atmpS1324;
  int32_t _M0L6_2atmpS1322;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1323 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1324 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1322 = _M0L6_2atmpS1323 / _M0L6_2atmpS1324;
  return _M0L6_2atmpS1322 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1320;
  int32_t _M0L6_2atmpS1321;
  int32_t _M0L6_2atmpS1319;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1320 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1321 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1319 = _M0L6_2atmpS1320 + _M0L6_2atmpS1321;
  return _M0L6_2atmpS1319 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1318;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1318 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1318;
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
  int32_t _M0L3lenS1317;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1312;
  int32_t _M0L6_2atmpS1311;
  int32_t _if__result_2663;
  uint16_t* _M0L4dataS1313;
  int32_t _M0L3lenS1314;
  int32_t _M0L3lenS1316;
  int32_t _M0L6_2atmpS1315;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1317 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1317 + _M0L8str__lenS182;
  _M0L4dataS1312 = _M0L4selfS185->$0;
  _M0L6_2atmpS1311 = Moonbit_array_length(_M0L4dataS1312);
  if (_M0L8requiredS184 > _M0L6_2atmpS1311) {
    _if__result_2663 = 1;
  } else {
    int32_t _M0L3lenS1310 = _M0L4selfS185->$1;
    _if__result_2663 = _M0L8requiredS184 < _M0L3lenS1310;
  }
  if (_if__result_2663) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1313 = _M0L4selfS185->$0;
  _M0L3lenS1314 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1313);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1313, _M0L3lenS1314, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1313);
  _M0L3lenS1316 = _M0L4selfS185->$1;
  _M0L6_2atmpS1315 = _M0L3lenS1316 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1315;
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
      int32_t _M0L6_2atmpS1307 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1308;
      int32_t _M0L6_2atmpS1309;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1307;
      _M0L6_2atmpS1308 = _M0L1iS176 + 1;
      _M0L6_2atmpS1309 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1308;
      _M0L1jS177 = _M0L6_2atmpS1309;
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
    int32_t _M0L3lenS1278 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1280 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1279 = Moonbit_array_length(_M0L4dataS1280);
    uint16_t* _M0L4dataS1283;
    int32_t _M0L3lenS1284;
    int32_t _M0L6_2atmpS1285;
    int32_t _M0L3lenS1287;
    int32_t _M0L6_2atmpS1286;
    if (_M0L3lenS1278 >= _M0L6_2atmpS1279) {
      int32_t _M0L3lenS1282 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1281 = _M0L3lenS1282 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1281);
    }
    _M0L4dataS1283 = _M0L4selfS171->$0;
    _M0L3lenS1284 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1283);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1285 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1284 < 0
      || _M0L3lenS1284 >= Moonbit_array_length(_M0L4dataS1283)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1283[_M0L3lenS1284] = _M0L6_2atmpS1285;
    moonbit_decref_cycle_free(_M0L4dataS1283);
    _M0L3lenS1287 = _M0L4selfS171->$1;
    _M0L6_2atmpS1286 = _M0L3lenS1287 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1286;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1291 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1289 = Moonbit_array_length(_M0L4dataS1291);
    int32_t _M0L3lenS1290 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1288 = _M0L6_2atmpS1289 - _M0L3lenS1290;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1294;
    int32_t _M0L3lenS1295;
    uint32_t _M0L6_2atmpS1298;
    uint32_t _M0L6_2atmpS1297;
    int32_t _M0L6_2atmpS1296;
    uint16_t* _M0L4dataS1299;
    int32_t _M0L3lenS1304;
    int32_t _M0L6_2atmpS1300;
    uint32_t _M0L6_2atmpS1303;
    uint32_t _M0L6_2atmpS1302;
    int32_t _M0L6_2atmpS1301;
    int32_t _M0L3lenS1306;
    int32_t _M0L6_2atmpS1305;
    if (_M0L6_2atmpS1288 < 2) {
      int32_t _M0L3lenS1293 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1292 = _M0L3lenS1293 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1292);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1294 = _M0L4selfS171->$0;
    _M0L3lenS1295 = _M0L4selfS171->$1;
    _M0L6_2atmpS1298 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1297 = 55296u + _M0L6_2atmpS1298;
    moonbit_incref_cycle_free(_M0L4dataS1294);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1296 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1297);
    if (
      _M0L3lenS1295 < 0
      || _M0L3lenS1295 >= Moonbit_array_length(_M0L4dataS1294)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1294[_M0L3lenS1295] = _M0L6_2atmpS1296;
    moonbit_decref_cycle_free(_M0L4dataS1294);
    _M0L4dataS1299 = _M0L4selfS171->$0;
    _M0L3lenS1304 = _M0L4selfS171->$1;
    _M0L6_2atmpS1300 = _M0L3lenS1304 + 1;
    _M0L6_2atmpS1303 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1302 = 56320u + _M0L6_2atmpS1303;
    moonbit_incref_cycle_free(_M0L4dataS1299);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1301 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1302);
    if (
      _M0L6_2atmpS1300 < 0
      || _M0L6_2atmpS1300 >= Moonbit_array_length(_M0L4dataS1299)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1299[_M0L6_2atmpS1300] = _M0L6_2atmpS1301;
    moonbit_decref_cycle_free(_M0L4dataS1299);
    _M0L3lenS1306 = _M0L4selfS171->$1;
    _M0L6_2atmpS1305 = _M0L3lenS1306 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1305;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_34.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1277;
  int32_t _M0L6_2atmpS1275;
  int32_t _M0L3lenS1276;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1272;
  int32_t _M0L6_2atmpS1273;
  int32_t _M0L3lenS1274;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2528;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1277 = _M0L4selfS166->$0;
  _M0L6_2atmpS1275 = Moonbit_array_length(_M0L4dataS1277);
  _M0L3lenS1276 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1275, _M0L3lenS1276, _M0L8requiredS167);
  _M0L4dataS1272 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1272);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1273 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1274 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1272, _M0L13new__capacityS165, _M0L6_2atmpS1273, _M0L3lenS1274, 0, 0);
  _M0L6_2aoldS2528 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2528);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_35.data);
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
  int32_t _M0L6_2atmpS1271;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1271 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1271;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1270;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1270 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1270;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1261;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1261 = _M0L4selfS155->$1;
  if (_M0L3lenS1261 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1262 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1264 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1263 = Moonbit_array_length(_M0L4dataS1264);
    if (_M0L3lenS1262 == _M0L6_2atmpS1263) {
      uint16_t* _M0L4dataS1265 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1265);
      return _M0L4dataS1265;
    } else {
      uint16_t* _M0L4dataS1266 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1267 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1268;
      int32_t _M0L3lenS1269;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1266);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1268 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1269 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1266, _M0L3lenS1267, _M0L6_2atmpS1268, _M0L3lenS1269, 0, 0);
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
  int32_t _if__result_2666;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1257 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1258 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1257 <= _M0L6_2atmpS1258) {
            int32_t _M0L6_2atmpS1256 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2666 = _M0L6_2atmpS1256 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2666 = 0;
          }
        } else {
          _if__result_2666 = 0;
        }
      } else {
        _if__result_2666 = 0;
      }
    } else {
      _if__result_2666 = 0;
    }
  } else {
    _if__result_2666 = 0;
  }
  if (_if__result_2666) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1260;
    moonbit_string_t _M0L6_2atmpS1259;
    uint16_t* _result_2667;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_40.data);
    _M0L6_2atmpS1260 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1260);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1259
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2667 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1259);
    moonbit_decref_cycle_free(_M0L6_2atmpS1259);
    return _result_2667;
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
  struct _M0TPB13StringBuilder* _block_2668;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1255 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1255 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2668
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2668)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 84, 0);
  _block_2668->$0 = _M0L4dataS140;
  _block_2668->$1 = 0;
  return _block_2668;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1254;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1254 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1254;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2669;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1235 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1236;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1236
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS1235 <= _M0L6_2atmpS1236) {
            int32_t _M0L6_2atmpS1234 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2669 = _M0L6_2atmpS1234 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2669 = 0;
          }
        } else {
          _if__result_2669 = 0;
        }
      } else {
        _if__result_2669 = 0;
      }
    } else {
      _if__result_2669 = 0;
    }
  } else {
    _if__result_2669 = 0;
  }
  if (_if__result_2669) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1238;
    moonbit_string_t _M0L6_2atmpS1237;
    moonbit_string_t* _result_2670;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1238 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1238);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1237
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2670
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1237);
    moonbit_decref_cycle_free(_M0L6_2atmpS1237);
    return _result_2670;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2671;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1240 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1241;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1241
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS1240 <= _M0L6_2atmpS1241) {
            int32_t _M0L6_2atmpS1239 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2671 = _M0L6_2atmpS1239 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2671 = 0;
          }
        } else {
          _if__result_2671 = 0;
        }
      } else {
        _if__result_2671 = 0;
      }
    } else {
      _if__result_2671 = 0;
    }
  } else {
    _if__result_2671 = 0;
  }
  if (_if__result_2671) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1243;
    moonbit_string_t _M0L6_2atmpS1242;
    struct _M0TUsiE** _result_2672;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1243 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1243);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1242
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2672
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1242);
    moonbit_decref_cycle_free(_M0L6_2atmpS1242);
    return _result_2672;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2673;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1245 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1246;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1246
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
          if (_M0L6_2atmpS1245 <= _M0L6_2atmpS1246) {
            int32_t _M0L6_2atmpS1244 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2673 = _M0L6_2atmpS1244 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2673 = 0;
          }
        } else {
          _if__result_2673 = 0;
        }
      } else {
        _if__result_2673 = 0;
      }
    } else {
      _if__result_2673 = 0;
    }
  } else {
    _if__result_2673 = 0;
  }
  if (_if__result_2673) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1248;
    moonbit_string_t _M0L6_2atmpS1247;
    int32_t* _result_2674;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1248 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1248);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1247
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2674
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1247);
    moonbit_decref_cycle_free(_M0L6_2atmpS1247);
    return _result_2674;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2675;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1250 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1251;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1251
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS1250 <= _M0L6_2atmpS1251) {
            int32_t _M0L6_2atmpS1249 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2675 = _M0L6_2atmpS1249 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2675 = 0;
          }
        } else {
          _if__result_2675 = 0;
        }
      } else {
        _if__result_2675 = 0;
      }
    } else {
      _if__result_2675 = 0;
    }
  } else {
    _if__result_2675 = 0;
  }
  if (_if__result_2675) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1253;
    moonbit_string_t _M0L6_2atmpS1252;
    float* _result_2676;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1253 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1253);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1252
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2676
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1252);
    moonbit_decref_cycle_free(_M0L6_2atmpS1252);
    return _result_2676;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1231;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1231
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1231);
  if (_M0L6_2atmpS1231.$1) {
    moonbit_decref(_M0L6_2atmpS1231.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1232;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1232
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1232);
  if (_M0L6_2atmpS1232.$1) {
    moonbit_decref(_M0L6_2atmpS1232.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1233;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1233
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1233);
  if (_M0L6_2atmpS1233.$1) {
    moonbit_decref(_M0L6_2atmpS1233.$1);
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
        int32_t _M0L6_2atmpS1186 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1188 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1187;
        int32_t _M0L6_2atmpS1189;
        if (
          _M0L6_2atmpS1188 < 0
          || _M0L6_2atmpS1188 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1187 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1188];
        if (
          _M0L6_2atmpS1186 < 0
          || _M0L6_2atmpS1186 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1186] = _M0L6_2atmpS1187;
        _M0L6_2atmpS1189 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1189;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1194 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1194;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1190 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1192 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1191;
        int32_t _M0L6_2atmpS1193;
        if (
          _M0L6_2atmpS1192 < 0
          || _M0L6_2atmpS1192 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1191 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1192];
        if (
          _M0L6_2atmpS1190 < 0
          || _M0L6_2atmpS1190 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1190] = _M0L6_2atmpS1191;
        _M0L6_2atmpS1193 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1193;
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
        int32_t _M0L6_2atmpS1195 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1197 = _M0L11src__offsetS30 + _M0L1iS31;
        float _M0L6_2atmpS1196;
        int32_t _M0L6_2atmpS1198;
        if (
          _M0L6_2atmpS1197 < 0
          || _M0L6_2atmpS1197 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1196 = (float)_M0L3srcS28[_M0L6_2atmpS1197];
        if (
          _M0L6_2atmpS1195 < 0
          || _M0L6_2atmpS1195 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1195] = _M0L6_2atmpS1196;
        _M0L6_2atmpS1198 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1198;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1203 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1203;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1199 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1201 = _M0L11src__offsetS30 + _M0L1iS34;
        float _M0L6_2atmpS1200;
        int32_t _M0L6_2atmpS1202;
        if (
          _M0L6_2atmpS1201 < 0
          || _M0L6_2atmpS1201 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1200 = (float)_M0L3srcS28[_M0L6_2atmpS1201];
        if (
          _M0L6_2atmpS1199 < 0
          || _M0L6_2atmpS1199 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1199] = _M0L6_2atmpS1200;
        _M0L6_2atmpS1202 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1202;
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
        int32_t _M0L6_2atmpS1204 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1206 = _M0L11src__offsetS39 + _M0L1iS40;
        int32_t _M0L6_2atmpS1205;
        int32_t _M0L6_2atmpS1207;
        if (
          _M0L6_2atmpS1206 < 0
          || _M0L6_2atmpS1206 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1205 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1206];
        if (
          _M0L6_2atmpS1204 < 0
          || _M0L6_2atmpS1204 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1204] = _M0L6_2atmpS1205;
        _M0L6_2atmpS1207 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1207;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1212 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1212;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1208 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1210 = _M0L11src__offsetS39 + _M0L1iS43;
        int32_t _M0L6_2atmpS1209;
        int32_t _M0L6_2atmpS1211;
        if (
          _M0L6_2atmpS1210 < 0
          || _M0L6_2atmpS1210 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1209 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1210];
        if (
          _M0L6_2atmpS1208 < 0
          || _M0L6_2atmpS1208 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1208] = _M0L6_2atmpS1209;
        _M0L6_2atmpS1211 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1211;
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
        int32_t _M0L6_2atmpS1213 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1215 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS1214;
        moonbit_string_t _M0L6_2aoldS2529;
        int32_t _M0L6_2atmpS1216;
        if (
          _M0L6_2atmpS1215 < 0
          || _M0L6_2atmpS1215 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1214 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1215];
        if (
          _M0L6_2atmpS1213 < 0
          || _M0L6_2atmpS1213 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2529 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1213];
        moonbit_incref_cycle_free(_M0L6_2atmpS1214);
        moonbit_decref_cycle_free(_M0L6_2aoldS2529);
        _M0L3dstS45[_M0L6_2atmpS1213] = _M0L6_2atmpS1214;
        _M0L6_2atmpS1216 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1216;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1221 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1221;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1217 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1219 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1218;
        moonbit_string_t _M0L6_2aoldS2530;
        int32_t _M0L6_2atmpS1220;
        if (
          _M0L6_2atmpS1219 < 0
          || _M0L6_2atmpS1219 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1218 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1219];
        if (
          _M0L6_2atmpS1217 < 0
          || _M0L6_2atmpS1217 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2530 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1217];
        moonbit_incref_cycle_free(_M0L6_2atmpS1218);
        moonbit_decref_cycle_free(_M0L6_2aoldS2530);
        _M0L3dstS45[_M0L6_2atmpS1217] = _M0L6_2atmpS1218;
        _M0L6_2atmpS1220 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1220;
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
        int32_t _M0L6_2atmpS1222 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1224 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS1223;
        struct _M0TUsiE* _M0L6_2aoldS2531;
        int32_t _M0L6_2atmpS1225;
        if (
          _M0L6_2atmpS1224 < 0
          || _M0L6_2atmpS1224 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1223 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1224];
        if (
          _M0L6_2atmpS1222 < 0
          || _M0L6_2atmpS1222 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2531 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1222];
        if (_M0L6_2atmpS1223) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1223);
        }
        if (_M0L6_2aoldS2531) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2531);
        }
        _M0L3dstS54[_M0L6_2atmpS1222] = _M0L6_2atmpS1223;
        _M0L6_2atmpS1225 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1225;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1230 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1230;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1226 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1228 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1227;
        struct _M0TUsiE* _M0L6_2aoldS2532;
        int32_t _M0L6_2atmpS1229;
        if (
          _M0L6_2atmpS1228 < 0
          || _M0L6_2atmpS1228 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1227 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1228];
        if (
          _M0L6_2atmpS1226 < 0
          || _M0L6_2atmpS1226 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2532 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1226];
        if (_M0L6_2atmpS1227) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1227);
        }
        if (_M0L6_2aoldS2532) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2532);
        }
        _M0L3dstS54[_M0L6_2atmpS1226] = _M0L6_2atmpS1227;
        _M0L6_2atmpS1229 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1229;
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_41.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_42.data);
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1158) {
  switch (Moonbit_object_tag(_M0L4_2aeS1158)) {
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_43.data;
      break;
    }
    
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_44.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_45.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1158);
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_46.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1181,
  struct _M0TPB4Show _M0L8_2aparamS1180
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1179 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1181;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1179, _M0L8_2aparamS1180);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1178,
  struct _M0TPB4Show _M0L8_2aparamS1177
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1176 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1178;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1176, _M0L8_2aparamS1177);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1175,
  int32_t _M0L8_2aparamS1174
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1173 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1175;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1173, _M0L8_2aparamS1174);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1172,
  struct _M0TPC16string10StringView _M0L8_2aparamS1171
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1170 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1172;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1170, _M0L8_2aparamS1171);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1169,
  moonbit_string_t _M0L8_2aparamS1166,
  int32_t _M0L8_2aparamS1167,
  int32_t _M0L8_2aparamS1168
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1165 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1169;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1165, _M0L8_2aparamS1166, _M0L8_2aparamS1167, _M0L8_2aparamS1168);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1164,
  moonbit_string_t _M0L8_2aparamS1163
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1162 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1164;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1162, _M0L8_2aparamS1163);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1185;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1151;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1152;
  int32_t _M0L7_2abindS1153;
  struct _M0TUsiE** _M0L7_2abindS1154;
  int32_t _M0L6_2acntS2537;
  int32_t _M0L2__S1155;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1185
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1151
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1151)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 87, 0);
  _M0L12async__testsS1151->$0 = _M0L6_2atmpS1185;
  _M0L12async__testsS1151->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1152
  = _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1153 = _M0L7_2abindS1152->$1;
  _M0L7_2abindS1154 = _M0L7_2abindS1152->$0;
  _M0L6_2acntS2537
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1152));
  if (_M0L6_2acntS2537 > 1) {
    int32_t _M0L11_2anew__cntS2538 = _M0L6_2acntS2537 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1152), _M0L11_2anew__cntS2538);
    moonbit_incref_cycle_free(_M0L7_2abindS1154);
  } else if (_M0L6_2acntS2537 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1152);
  }
  _M0L2__S1155 = 0;
  while (1) {
    if (_M0L2__S1155 < _M0L7_2abindS1153) {
      struct _M0TUsiE* _M0L3argS1156 =
        (struct _M0TUsiE*)_M0L7_2abindS1154[_M0L2__S1155];
      moonbit_string_t _M0L6_2atmpS1182 = _M0L3argS1156->$0;
      int32_t _M0L6_2atmpS1183 = _M0L3argS1156->$1;
      int32_t _M0L6_2atmpS1184;
      moonbit_incref_cycle_free(_M0L6_2atmpS1182);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples21chain__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1151, _M0L6_2atmpS1182, _M0L6_2atmpS1183);
      moonbit_decref_cycle_free(_M0L6_2atmpS1182);
      _M0L6_2atmpS1184 = _M0L2__S1155 + 1;
      _M0L2__S1155 = _M0L6_2atmpS1184;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1154);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples21chain__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples21chain__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1151);
  moonbit_decref_cycle_free(_M0L12async__testsS1151);
  moonbit_flush_cycles();
  return 0;
}