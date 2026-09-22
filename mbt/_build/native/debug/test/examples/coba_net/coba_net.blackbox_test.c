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
struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25coba__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958;

struct _M0TWRPC15error5ErrorEs;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TUdiE;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

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

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25coba__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TUddE;

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953 {
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25coba__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958 {
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

struct _M0TPB13StringBuilder {
  uint16_t* $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGfE {
  float* $0;
  int32_t $1;
  
};

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TP26RiantR8snn__mbt4Time {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGiE* $1;
  float $2;
  
};

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25coba__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
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

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS965(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS958(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS953(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS930(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S923(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25coba__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse20random__with__delays(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*,
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

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

double _M0FPC14math2ln(double);

#define _M0FPC14math3cos cos

#define _M0FPC14math3sin sin

struct _M0TUdiE* _M0FPC14math5frexp(double);

struct _M0TUdiE* _M0FPC14math9normalize(double);

int32_t _M0MPC15float5Float7to__int(float);

int32_t _M0MPC15array5Array5clearGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*,
  int32_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

int32_t _M0FPB7printlnGsE(moonbit_string_t);

int32_t _M0MPC16double6Double7is__inf(double);

int32_t _M0MPC16double6Double7is__nan(double);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t*,
  int32_t,
  int32_t*,
  int32_t,
  int32_t
);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t
);

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t
);

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
} const moonbit_string_literal_19 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

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
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_29 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[115]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 114, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 111, 98, 97, 95, 110, 101, 116, 
    95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 
    112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[33]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 32, 45, 45, 
    45, 45, 45, 32, 69, 78, 68, 32, 77, 79, 79, 78, 32, 84, 69, 83, 84, 
    32, 82, 69, 83, 85, 76, 84, 32, 45, 45, 45, 45, 45, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_24 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 111, 98, 97, 95, 110, 101, 116, 
    95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 
    114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_12 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS965$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS965
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[93] =
  {
    sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953)
    / 4, 1,
    offsetof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953, $1)
    / 4
    * 2,
    sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958)
    / 4, 1,
    offsetof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

double _M0FPB18double__max__value;

double _M0FPB18double__min__value;

double _M0FPC16double14not__a__number;

double _M0FPC16double13neg__infinity;

double _M0FPC16double13min__positive;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1946
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS986,
  moonbit_string_t _M0L8filenameS955,
  int32_t _M0L5indexS957
) {
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953* _closure_1981;
  struct _M0TWEu* _M0L13handle__startS953;
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958* _closure_1982;
  struct _M0TWssbEu* _M0L14handle__resultS958;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS965;
  void* _M0L11_2atry__errS980;
  struct moonbit_result_0 _tmp_1984;
  int32_t _handle__error__result_1985;
  int32_t _M0L6_2atmpS1934;
  void* _M0L3errS981;
  moonbit_string_t _M0L4nameS983;
  struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS984;
  moonbit_string_t _M0L7_2anameS985;
  int32_t _M0L6_2acntS1975;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS955);
  _closure_1981
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953*)moonbit_malloc(sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953));
  Moonbit_object_header(_closure_1981)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1981->code
  = &_M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS953;
  _closure_1981->$0 = _M0L5indexS957;
  _closure_1981->$1 = _M0L8filenameS955;
  _M0L13handle__startS953 = (struct _M0TWEu*)_closure_1981;
  moonbit_incref_cycle_free(_M0L8filenameS955);
  _closure_1982
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958*)moonbit_malloc(sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958));
  Moonbit_object_header(_closure_1982)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1982->code
  = &_M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS958;
  _closure_1982->$0 = _M0L5indexS957;
  _closure_1982->$1 = _M0L8filenameS955;
  _M0L14handle__resultS958 = (struct _M0TWssbEu*)_closure_1982;
  _M0L17error__to__stringS965
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS965$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1984
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS986, _M0L8filenameS955, _M0L5indexS957, _M0L13handle__startS953, _M0L14handle__resultS958, _M0L17error__to__stringS965);
  if (_tmp_1984.tag) {
    int32_t const _M0L5_2aokS1943 = _tmp_1984.data.ok;
    _handle__error__result_1985 = _M0L5_2aokS1943;
  } else {
    void* const _M0L6_2aerrS1944 = _tmp_1984.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS965);
    moonbit_decref_cycle_free(_M0L13handle__startS953);
    _M0L11_2atry__errS980 = _M0L6_2aerrS1944;
    goto join_979;
  }
  if (_handle__error__result_1985) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS965);
    moonbit_decref_cycle_free(_M0L13handle__startS953);
    _M0L6_2atmpS1934 = 1;
  } else {
    struct moonbit_result_0 _tmp_1986;
    int32_t _handle__error__result_1987;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1986
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS986, _M0L8filenameS955, _M0L5indexS957, _M0L13handle__startS953, _M0L14handle__resultS958, _M0L17error__to__stringS965);
    if (_tmp_1986.tag) {
      int32_t const _M0L5_2aokS1941 = _tmp_1986.data.ok;
      _handle__error__result_1987 = _M0L5_2aokS1941;
    } else {
      void* const _M0L6_2aerrS1942 = _tmp_1986.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS965);
      moonbit_decref_cycle_free(_M0L13handle__startS953);
      _M0L11_2atry__errS980 = _M0L6_2aerrS1942;
      goto join_979;
    }
    if (_handle__error__result_1987) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS965);
      moonbit_decref_cycle_free(_M0L13handle__startS953);
      _M0L6_2atmpS1934 = 1;
    } else {
      struct moonbit_result_0 _tmp_1988;
      int32_t _handle__error__result_1989;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1988
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS986, _M0L8filenameS955, _M0L5indexS957, _M0L13handle__startS953, _M0L14handle__resultS958, _M0L17error__to__stringS965);
      if (_tmp_1988.tag) {
        int32_t const _M0L5_2aokS1939 = _tmp_1988.data.ok;
        _handle__error__result_1989 = _M0L5_2aokS1939;
      } else {
        void* const _M0L6_2aerrS1940 = _tmp_1988.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS965);
        moonbit_decref_cycle_free(_M0L13handle__startS953);
        _M0L11_2atry__errS980 = _M0L6_2aerrS1940;
        goto join_979;
      }
      if (_handle__error__result_1989) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS965);
        moonbit_decref_cycle_free(_M0L13handle__startS953);
        _M0L6_2atmpS1934 = 1;
      } else {
        struct moonbit_result_0 _tmp_1990;
        int32_t _handle__error__result_1991;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1990
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS986, _M0L8filenameS955, _M0L5indexS957, _M0L13handle__startS953, _M0L14handle__resultS958, _M0L17error__to__stringS965);
        if (_tmp_1990.tag) {
          int32_t const _M0L5_2aokS1937 = _tmp_1990.data.ok;
          _handle__error__result_1991 = _M0L5_2aokS1937;
        } else {
          void* const _M0L6_2aerrS1938 = _tmp_1990.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS965);
          moonbit_decref_cycle_free(_M0L13handle__startS953);
          _M0L11_2atry__errS980 = _M0L6_2aerrS1938;
          goto join_979;
        }
        if (_handle__error__result_1991) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS965);
          moonbit_decref_cycle_free(_M0L13handle__startS953);
          _M0L6_2atmpS1934 = 1;
        } else {
          struct moonbit_result_0 _tmp_1992;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1992
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS986, _M0L8filenameS955, _M0L5indexS957, _M0L13handle__startS953, _M0L14handle__resultS958, _M0L17error__to__stringS965);
          moonbit_decref_cycle_free(_M0L13handle__startS953);
          moonbit_decref_cycle_free(_M0L17error__to__stringS965);
          if (_tmp_1992.tag) {
            int32_t const _M0L5_2aokS1935 = _tmp_1992.data.ok;
            _M0L6_2atmpS1934 = _M0L5_2aokS1935;
          } else {
            void* const _M0L6_2aerrS1936 = _tmp_1992.data.err;
            _M0L11_2atry__errS980 = _M0L6_2aerrS1936;
            goto join_979;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1934) {
    void* _M0L128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1945 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1945)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1945)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS980
    = _M0L128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1945;
    goto join_979;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS958);
  }
  goto joinlet_1983;
  join_979:;
  _M0L3errS981 = _M0L11_2atry__errS980;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS984
  = (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS981;
  _M0L7_2anameS985 = _M0L36_2aMoonBitTestDriverInternalSkipTestS984->$0;
  _M0L6_2acntS1975
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS984));
  if (_M0L6_2acntS1975 > 1) {
    int32_t _M0L11_2anew__cntS1976 = _M0L6_2acntS1975 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS984), _M0L11_2anew__cntS1976);
    moonbit_incref_cycle_free(_M0L7_2anameS985);
  } else if (_M0L6_2acntS1975 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS984);
  }
  _M0L4nameS983 = _M0L7_2anameS985;
  goto join_982;
  goto joinlet_1993;
  join_982:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS958(_M0L14handle__resultS958, _M0L4nameS983, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS958);
  moonbit_decref_cycle_free(_M0L4nameS983);
  joinlet_1993:;
  joinlet_1983:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS965(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1933,
  void* _M0L3errS966
) {
  void* _M0L1eS968;
  moonbit_string_t _M0L1eS970;
  moonbit_string_t _result_1996;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS966)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS971 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS966;
      moonbit_string_t _M0L4_2aeS972 = _M0L10_2aFailureS971->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS972);
      _M0L1eS970 = _M0L4_2aeS972;
      goto join_969;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS973 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS966;
      moonbit_string_t _M0L4_2aeS974 = _M0L15_2aInspectErrorS973->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS974);
      _M0L1eS970 = _M0L4_2aeS974;
      goto join_969;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS975 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS966;
      moonbit_string_t _M0L4_2aeS976 = _M0L16_2aSnapshotErrorS975->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS976);
      _M0L1eS970 = _M0L4_2aeS976;
      goto join_969;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS977 =
        (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS966;
      moonbit_string_t _M0L4_2aeS978 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS977->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS978);
      _M0L1eS970 = _M0L4_2aeS978;
      goto join_969;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS966);
      _M0L1eS968 = _M0L3errS966;
      goto join_967;
      break;
    }
  }
  join_969:;
  return _M0L1eS970;
  join_967:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_1996 = _M0FP15Error10to__string(_M0L1eS968);
  moonbit_decref_cycle_free(_M0L1eS968);
  return _result_1996;
}

int32_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS958(
  struct _M0TWssbEu* _M0L6_2aenvS1930,
  moonbit_string_t _M0L10__testnameS959,
  moonbit_string_t _M0L7messageS960,
  int32_t _M0L7skippedS961
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958* _M0L14_2acasted__envS1931;
  moonbit_string_t _M0L8filenameS955;
  int32_t _M0L5indexS957;
  moonbit_string_t _M0L10file__nameS962;
  moonbit_string_t _M0L7messageS963;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS964;
  moonbit_string_t _M0L6_2atmpS1932;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1931
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c958*)_M0L6_2aenvS1930;
  _M0L8filenameS955 = _M0L14_2acasted__envS1931->$1;
  _M0L5indexS957 = _M0L14_2acasted__envS1931->$0;
  if (!_M0L7skippedS961 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS962
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS955, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS963
  = _M0MPC16string6String14escape_2einner(_M0L7messageS960, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS964
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS964, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS964, _M0L10file__nameS962);
  moonbit_decref_cycle_free(_M0L10file__nameS962);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS964, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS964, _M0L5indexS957);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS964, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS964, _M0L7messageS963);
  moonbit_decref_cycle_free(_M0L7messageS963);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS964, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1932
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS964);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS964);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1932);
  moonbit_decref_cycle_free(_M0L6_2atmpS1932);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS953(
  struct _M0TWEu* _M0L6_2aenvS1927
) {
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953* _M0L14_2acasted__envS1928;
  moonbit_string_t _M0L8filenameS955;
  int32_t _M0L5indexS957;
  moonbit_string_t _M0L10file__nameS954;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS956;
  moonbit_string_t _M0L6_2atmpS1929;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1928
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcoba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c953*)_M0L6_2aenvS1927;
  _M0L8filenameS955 = _M0L14_2acasted__envS1928->$1;
  _M0L5indexS957 = _M0L14_2acasted__envS1928->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS954
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS955, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS956
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS956, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS956, _M0L10file__nameS954);
  moonbit_decref_cycle_free(_M0L10file__nameS954);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS956, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS956, _M0L5indexS957);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS956, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1929
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS956);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS956);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1929);
  moonbit_decref_cycle_free(_M0L6_2atmpS1929);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S923;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS930;
  struct _M0TUsiE** _M0L6_2atmpS1926;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS937;
  moonbit_string_t* _M0L9cli__argsS938;
  moonbit_string_t _M0L6_2atmpS1925;
  moonbit_string_t _M0L6_2atmpS1924;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS939;
  int32_t _M0L7_2abindS940;
  moonbit_string_t* _M0L7_2abindS941;
  int32_t _M0L6_2acntS1977;
  int32_t _M0L2__S942;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S923 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS930 = 0;
  _M0L6_2atmpS1926 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS937
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS937)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS937->$0 = _M0L6_2atmpS1926;
  _M0L16file__and__indexS937->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS938
  = _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS938)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1925 = (moonbit_string_t)_M0L9cli__argsS938[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1925);
  moonbit_decref_cycle_free(_M0L9cli__argsS938);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1924
  = _M0MP46RiantR8snn__mbt8examples25coba__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1925);
  moonbit_decref_cycle_free(_M0L6_2atmpS1925);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS939
  = _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS930(_M0L51moonbit__test__driver__internal__split__mbt__stringS930, _M0L6_2atmpS1924, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1924);
  _M0L7_2abindS940 = _M0L10test__argsS939->$1;
  _M0L7_2abindS941 = _M0L10test__argsS939->$0;
  _M0L6_2acntS1977
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS939));
  if (_M0L6_2acntS1977 > 1) {
    int32_t _M0L11_2anew__cntS1978 = _M0L6_2acntS1977 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS939), _M0L11_2anew__cntS1978);
    moonbit_incref_cycle_free(_M0L7_2abindS941);
  } else if (_M0L6_2acntS1977 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS939);
  }
  _M0L2__S942 = 0;
  while (1) {
    if (_M0L2__S942 < _M0L7_2abindS940) {
      moonbit_string_t _M0L3argS943 =
        (moonbit_string_t)_M0L7_2abindS941[_M0L2__S942];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS944;
      moonbit_string_t _M0L4fileS945;
      moonbit_string_t _M0L5rangeS946;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS947;
      moonbit_string_t _M0L6_2atmpS1922;
      int32_t _M0L5startS948;
      moonbit_string_t _M0L6_2atmpS1921;
      int32_t _M0L3endS949;
      int32_t _M0L1iS950;
      int32_t _M0L6_2atmpS1923;
      moonbit_incref_cycle_free(_M0L3argS943);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS944
      = _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS930(_M0L51moonbit__test__driver__internal__split__mbt__stringS930, _M0L3argS943, 58);
      moonbit_decref_cycle_free(_M0L3argS943);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS945
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS944, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS946
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS944, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS944);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS947
      = _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS930(_M0L51moonbit__test__driver__internal__split__mbt__stringS930, _M0L5rangeS946, 45);
      moonbit_decref_cycle_free(_M0L5rangeS946);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1922
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS947, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS948
      = _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S923(_M0L45moonbit__test__driver__internal__parse__int__S923, _M0L6_2atmpS1922);
      moonbit_decref_cycle_free(_M0L6_2atmpS1922);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1921
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS947, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS947);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS949
      = _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S923(_M0L45moonbit__test__driver__internal__parse__int__S923, _M0L6_2atmpS1921);
      moonbit_decref_cycle_free(_M0L6_2atmpS1921);
      _M0L1iS950 = _M0L5startS948;
      while (1) {
        if (_M0L1iS950 < _M0L3endS949) {
          struct _M0TUsiE* _M0L8_2atupleS1919;
          int32_t _M0L6_2atmpS1920;
          moonbit_incref_cycle_free(_M0L4fileS945);
          _M0L8_2atupleS1919
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1919)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1919->$0 = _M0L4fileS945;
          _M0L8_2atupleS1919->$1 = _M0L1iS950;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS937, _M0L8_2atupleS1919);
          _M0L6_2atmpS1920 = _M0L1iS950 + 1;
          _M0L1iS950 = _M0L6_2atmpS1920;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS945);
        }
        break;
      }
      _M0L6_2atmpS1923 = _M0L2__S942 + 1;
      _M0L2__S942 = _M0L6_2atmpS1923;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS941);
    }
    break;
  }
  return _M0L16file__and__indexS937;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS930(
  int32_t _M0L6_2aenvS1900,
  moonbit_string_t _M0L1sS931,
  int32_t _M0L3sepS932
) {
  moonbit_string_t* _M0L6_2atmpS1918;
  struct _M0TPB5ArrayGsE* _M0L3resS933;
  struct _M0TPB8MutLocalGiE* _M0L1iS934;
  struct _M0TPB8MutLocalGiE* _M0L5startS935;
  int32_t _M0L3valS1913;
  int32_t _M0L6_2atmpS1914;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1918 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS933
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS933)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS933->$0 = _M0L6_2atmpS1918;
  _M0L3resS933->$1 = 0;
  _M0L1iS934
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS934)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS934->$0 = 0;
  _M0L5startS935
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS935)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS935->$0 = 0;
  while (1) {
    int32_t _M0L3valS1901 = _M0L1iS934->$0;
    int32_t _M0L6_2atmpS1902 = Moonbit_array_length(_M0L1sS931);
    if (_M0L3valS1901 < _M0L6_2atmpS1902) {
      int32_t _M0L3valS1905 = _M0L1iS934->$0;
      int32_t _M0L6_2atmpS1904;
      int32_t _M0L6_2atmpS1903;
      int32_t _M0L3valS1912;
      int32_t _M0L6_2atmpS1911;
      if (
        _M0L3valS1905 < 0
        || _M0L3valS1905 >= Moonbit_array_length(_M0L1sS931)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1904 = _M0L1sS931[_M0L3valS1905];
      _M0L6_2atmpS1903 = _M0L6_2atmpS1904;
      if (_M0L6_2atmpS1903 == _M0L3sepS932) {
        int32_t _M0L3valS1907 = _M0L5startS935->$0;
        int32_t _M0L3valS1908 = _M0L1iS934->$0;
        moonbit_string_t _M0L6_2atmpS1906;
        int32_t _M0L3valS1910;
        int32_t _M0L6_2atmpS1909;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1906
        = _M0MPC16string6String17unsafe__substring(_M0L1sS931, _M0L3valS1907, _M0L3valS1908);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS933, _M0L6_2atmpS1906);
        _M0L3valS1910 = _M0L1iS934->$0;
        _M0L6_2atmpS1909 = _M0L3valS1910 + 1;
        _M0L5startS935->$0 = _M0L6_2atmpS1909;
      }
      _M0L3valS1912 = _M0L1iS934->$0;
      _M0L6_2atmpS1911 = _M0L3valS1912 + 1;
      _M0L1iS934->$0 = _M0L6_2atmpS1911;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS934);
    }
    break;
  }
  _M0L3valS1913 = _M0L5startS935->$0;
  _M0L6_2atmpS1914 = Moonbit_array_length(_M0L1sS931);
  if (_M0L3valS1913 < _M0L6_2atmpS1914) {
    int32_t _M0L3valS1916 = _M0L5startS935->$0;
    int32_t _M0L6_2atmpS1917;
    moonbit_string_t _M0L6_2atmpS1915;
    moonbit_decref_cycle_free(_M0L5startS935);
    _M0L6_2atmpS1917 = Moonbit_array_length(_M0L1sS931);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1915
    = _M0MPC16string6String17unsafe__substring(_M0L1sS931, _M0L3valS1916, _M0L6_2atmpS1917);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS933, _M0L6_2atmpS1915);
  } else {
    moonbit_decref_cycle_free(_M0L5startS935);
  }
  return _M0L3resS933;
}

int32_t _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S923(
  int32_t _M0L6_2aenvS1893,
  moonbit_string_t _M0L1sS924
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS925;
  int32_t _M0L3lenS926;
  int32_t _M0L7_2abindS927;
  int32_t _M0L1iS928;
  int32_t _result_2001;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS925
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS925)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS925->$0 = 0;
  _M0L3lenS926 = Moonbit_array_length(_M0L1sS924);
  _M0L7_2abindS927 = 0;
  _M0L1iS928 = _M0L7_2abindS927;
  while (1) {
    if (_M0L1iS928 < _M0L3lenS926) {
      int32_t _M0L3valS1898 = _M0L3resS925->$0;
      int32_t _M0L6_2atmpS1895 = _M0L3valS1898 * 10;
      int32_t _M0L6_2atmpS1897;
      int32_t _M0L6_2atmpS1896;
      int32_t _M0L6_2atmpS1894;
      int32_t _M0L6_2atmpS1899;
      if (_M0L1iS928 < 0 || _M0L1iS928 >= Moonbit_array_length(_M0L1sS924)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1897 = _M0L1sS924[_M0L1iS928];
      _M0L6_2atmpS1896 = _M0L6_2atmpS1897 - 48;
      _M0L6_2atmpS1894 = _M0L6_2atmpS1895 + _M0L6_2atmpS1896;
      _M0L3resS925->$0 = _M0L6_2atmpS1894;
      _M0L6_2atmpS1899 = _M0L1iS928 + 1;
      _M0L1iS928 = _M0L6_2atmpS1899;
      continue;
    }
    break;
  }
  _result_2001 = _M0L3resS925->$0;
  moonbit_decref_cycle_free(_M0L3resS925);
  return _result_2001;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25coba__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS922
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS922);
  return _M0L4selfS922;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S892,
  moonbit_string_t _M0L12_2adiscard__S893,
  int32_t _M0L12_2adiscard__S894,
  struct _M0TWEu* _M0L12_2adiscard__S895,
  struct _M0TWssbEu* _M0L12_2adiscard__S896,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S897
) {
  struct moonbit_result_0 _result_2002;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2002.tag = 1;
  _result_2002.data.ok = 0;
  return _result_2002;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S898,
  moonbit_string_t _M0L12_2adiscard__S899,
  int32_t _M0L12_2adiscard__S900,
  struct _M0TWEu* _M0L12_2adiscard__S901,
  struct _M0TWssbEu* _M0L12_2adiscard__S902,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S903
) {
  struct moonbit_result_0 _result_2003;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2003.tag = 1;
  _result_2003.data.ok = 0;
  return _result_2003;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S904,
  moonbit_string_t _M0L12_2adiscard__S905,
  int32_t _M0L12_2adiscard__S906,
  struct _M0TWEu* _M0L12_2adiscard__S907,
  struct _M0TWssbEu* _M0L12_2adiscard__S908,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S909
) {
  struct moonbit_result_0 _result_2004;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2004.tag = 1;
  _result_2004.data.ok = 0;
  return _result_2004;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S910,
  moonbit_string_t _M0L12_2adiscard__S911,
  int32_t _M0L12_2adiscard__S912,
  struct _M0TWEu* _M0L12_2adiscard__S913,
  struct _M0TWssbEu* _M0L12_2adiscard__S914,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S915
) {
  struct moonbit_result_0 _result_2005;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2005.tag = 1;
  _result_2005.data.ok = 0;
  return _result_2005;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S916,
  moonbit_string_t _M0L12_2adiscard__S917,
  int32_t _M0L12_2adiscard__S918,
  struct _M0TWEu* _M0L12_2adiscard__S919,
  struct _M0TWssbEu* _M0L12_2adiscard__S920,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S921
) {
  struct moonbit_result_0 _result_2006;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2006.tag = 1;
  _result_2006.data.ok = 0;
  return _result_2006;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S891
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse20random__with__delays(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS834,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS835,
  moonbit_string_t _M0L3symS836,
  float _M0L2muS837,
  float _M0L5sigmaS838,
  float _M0L1pS839,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS840,
  float _M0L7d__meanS846,
  float _M0L6d__stdS847
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS833;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1892;
  struct _M0TPB5ArrayGfE* _M0L4valsS1891;
  int32_t _M0L1nS841;
  struct _M0TPB5ArrayGfE* _M0L6delaysS1884;
  struct _M0TPB8MutLocalGiE* _M0L1kS842;
  #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3synS833
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L3preS834, _M0L4postS835, _M0L3symS836, _M0L2muS837, _M0L5sigmaS838, _M0L1pS839, _M0L3rngS840);
  _M0L6matrixS1892 = _M0L3synS833->$4;
  _M0L4valsS1891 = _M0L6matrixS1892->$4;
  moonbit_incref_cycle_free(_M0L4valsS1891);
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS841 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1891);
  moonbit_decref_cycle_free(_M0L4valsS1891);
  _M0L6delaysS1884 = _M0L3synS833->$5;
  moonbit_incref_cycle_free(_M0L6delaysS1884);
  #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MPC15array5Array5clearGfE(_M0L6delaysS1884);
  moonbit_decref_cycle_free(_M0L6delaysS1884);
  _M0L1kS842
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS842)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS842->$0 = 0;
  while (1) {
    int32_t _M0L3valS1885 = _M0L1kS842->$0;
    if (_M0L3valS1885 < _M0L1nS841) {
      double _M0L2z1S844;
      struct _M0TUddE* _M0L7_2abindS849;
      double _M0L5_2az1S850;
      float _M0L6_2atmpS1890;
      float _M0L6_2atmpS1889;
      float _M0L1dS845;
      float _M0L7clampedS848;
      struct _M0TPB5ArrayGfE* _M0L6delaysS1886;
      int32_t _M0L3valS1888;
      int32_t _M0L6_2atmpS1887;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L7_2abindS849 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS840);
      _M0L5_2az1S850 = _M0L7_2abindS849->$0;
      moonbit_decref_cycle_free(_M0L7_2abindS849);
      _M0L2z1S844 = _M0L5_2az1S850;
      goto join_843;
      goto joinlet_2008;
      join_843:;
      _M0L6_2atmpS1890 = (float)_M0L2z1S844;
      _M0L6_2atmpS1889 = _M0L6d__stdS847 * _M0L6_2atmpS1890;
      _M0L1dS845 = _M0L7d__meanS846 + _M0L6_2atmpS1889;
      if (_M0L1dS845 < 0x0p+0f) {
        _M0L7clampedS848 = 0x0p+0f;
      } else {
        _M0L7clampedS848 = _M0L1dS845;
      }
      _M0L6delaysS1886 = _M0L3synS833->$5;
      moonbit_incref_cycle_free(_M0L6delaysS1886);
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array4pushGfE(_M0L6delaysS1886, _M0L7clampedS848);
      moonbit_decref_cycle_free(_M0L6delaysS1886);
      _M0L3valS1888 = _M0L1kS842->$0;
      _M0L6_2atmpS1887 = _M0L3valS1888 + 1;
      _M0L1kS842->$0 = _M0L6_2atmpS1887;
      joinlet_2008:;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS842);
    }
    break;
  }
  return _M0L3synS833;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS826,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS827,
  moonbit_string_t _M0L3symS832,
  float _M0L2muS828,
  float _M0L5sigmaS829,
  float _M0L1pS830,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS831
) {
  int32_t _M0L1nS1882;
  int32_t _M0L1nS1883;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS825;
  float* _M0L6_2atmpS1881;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1872;
  float* _M0L6_2atmpS1880;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1873;
  float* _M0L6_2atmpS1879;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1874;
  int32_t* _M0L6_2atmpS1878;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1875;
  float* _M0L6_2atmpS1877;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1876;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2009;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1882 = _M0L3preS826->$2;
  _M0L1nS1883 = _M0L4postS827->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS825
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS1882, _M0L1nS1883, _M0L2muS828, _M0L5sigmaS829, _M0L1pS830, _M0L3rngS831);
  _M0L6_2atmpS1881 = moonbit_empty_float_array;
  _M0L6_2atmpS1872
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1872)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1872->$0 = _M0L6_2atmpS1881;
  _M0L6_2atmpS1872->$1 = 0;
  _M0L6_2atmpS1880 = moonbit_empty_float_array;
  _M0L6_2atmpS1873
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1873)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1873->$0 = _M0L6_2atmpS1880;
  _M0L6_2atmpS1873->$1 = 0;
  _M0L6_2atmpS1879 = moonbit_empty_float_array;
  _M0L6_2atmpS1874
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1874)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1874->$0 = _M0L6_2atmpS1879;
  _M0L6_2atmpS1874->$1 = 0;
  _M0L6_2atmpS1878 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS1875
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1875)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS1875->$0 = _M0L6_2atmpS1878;
  _M0L6_2atmpS1875->$1 = 0;
  _M0L6_2atmpS1877 = moonbit_empty_float_array;
  _M0L6_2atmpS1876
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1876)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1876->$0 = _M0L6_2atmpS1877;
  _M0L6_2atmpS1876->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS826);
  moonbit_incref_cycle_free(_M0L4postS827);
  moonbit_incref_cycle_free(_M0L3symS832);
  _block_2009
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2009)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2009->$0 = _M0L3preS826;
  _block_2009->$1 = _M0L4postS827;
  _block_2009->$2 = _M0L3symS832;
  _block_2009->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2009->$4 = _M0L6matrixS825;
  _block_2009->$5 = _M0L6_2atmpS1872;
  _block_2009->$6 = _M0L6_2atmpS1873;
  _block_2009->$7 = _M0L6_2atmpS1874;
  _block_2009->$8 = _M0L6_2atmpS1875;
  _block_2009->$9 = _M0L6_2atmpS1876;
  return _block_2009;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS820,
  float _M0L2vtS821,
  float _M0L2vrS822,
  float _M0L2elS823,
  float _M0L1rS824
) {
  float _M0L1cS818;
  float _M0L2glS819;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2010;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS818 = -0x1p+0f;
  _M0L2glS819 = -0x1p+0f;
  _block_2010
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2010)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2010->$0 = _M0L1cS818;
  _block_2010->$1 = _M0L2glS819;
  _block_2010->$2 = _M0L2tmS820;
  _block_2010->$3 = _M0L2vtS821;
  _block_2010->$4 = _M0L2vrS822;
  _block_2010->$5 = _M0L2elS823;
  _block_2010->$6 = _M0L1rS824;
  _block_2010->$7 = 0x1p+1f;
  _block_2010->$8 = 0x0p+0f;
  _block_2010->$9 = 0x0p+0f;
  _block_2010->$10 = 0x0p+0f;
  return _block_2010;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS792,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS794,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS797
) {
  struct _M0TPB5ArrayGfE* _M0L1vS791;
  float _M0L2vtS1870;
  float _M0L2vrS1871;
  float _M0L6spreadS793;
  int32_t _M0L7_2abindS795;
  int32_t _M0L1kS796;
  struct _M0TPB5ArrayGfE* _M0L1wS799;
  struct _M0TPB5ArrayGbE* _M0L4fireS800;
  struct _M0TPB5ArrayGiE* _M0L4tabsS801;
  struct _M0TPB5ArrayGfE* _M0L1iS802;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS803;
  struct _M0TPB5ArrayGfE* _M0L2geS804;
  struct _M0TPB5ArrayGfE* _M0L2giS805;
  struct _M0TPB5ArrayGfE* _M0L2heS806;
  struct _M0TPB5ArrayGfE* _M0L2hiS807;
  struct _M0TPB5ArrayGfE* _M0L3gluS808;
  struct _M0TPB5ArrayGfE* _M0L4gabaS809;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS810;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS811;
  float _M0L4e__eS812;
  float _M0L4e__iS813;
  float _M0L3treS814;
  float _M0L3tdeS815;
  float _M0L3triS816;
  float _M0L3tdiS817;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS1869;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2012;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS791 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  _M0L2vtS1870 = _M0L5paramS794->$3;
  _M0L2vrS1871 = _M0L5paramS794->$4;
  _M0L6spreadS793 = _M0L2vtS1870 - _M0L2vrS1871;
  _M0L7_2abindS795 = 0;
  _M0L1kS796 = _M0L7_2abindS795;
  while (1) {
    if (_M0L1kS796 < _M0L1nS792) {
      float _M0L2vrS1865 = _M0L5paramS794->$4;
      float _M0L6_2atmpS1867;
      float _M0L6_2atmpS1866;
      float _M0L6_2atmpS1864;
      int32_t _M0L6_2atmpS1868;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1867 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS797);
      _M0L6_2atmpS1866 = _M0L6_2atmpS1867 * _M0L6spreadS793;
      _M0L6_2atmpS1864 = _M0L2vrS1865 + _M0L6_2atmpS1866;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS791, _M0L1kS796, _M0L6_2atmpS1864);
      _M0L6_2atmpS1868 = _M0L1kS796 + 1;
      _M0L1kS796 = _M0L6_2atmpS1868;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS799 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS800 = _M0MPC15array5Array4makeGbE(_M0L1nS792, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS801 = _M0MPC15array5Array4makeGiE(_M0L1nS792, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS802 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS803 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS804 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS805 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS806 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS807 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS808 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS809 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS810 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS811 = _M0MPC15array5Array4makeGfE(_M0L1nS792, 0x1p+0f);
  _M0L4e__eS812 = 0x0p+0f;
  _M0L4e__iS813 = -0x1.2cp+6f;
  _M0L3treS814 = 0x1p+0f;
  _M0L3tdeS815 = 0x1.8p+2f;
  _M0L3triS816 = 0x1p-1f;
  _M0L3tdiS817 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS1869 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS794);
  _block_2012
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2012)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2012->$0 = _M0L5paramS794;
  _block_2012->$1 = _M0L6_2atmpS1869;
  _block_2012->$2 = _M0L1nS792;
  _block_2012->$3 = _M0L1vS791;
  _block_2012->$4 = _M0L1wS799;
  _block_2012->$5 = _M0L4fireS800;
  _block_2012->$6 = _M0L4tabsS801;
  _block_2012->$7 = _M0L1iS802;
  _block_2012->$8 = _M0L9syn__currS803;
  _block_2012->$9 = _M0L2geS804;
  _block_2012->$10 = _M0L2giS805;
  _block_2012->$11 = _M0L2heS806;
  _block_2012->$12 = _M0L2hiS807;
  _block_2012->$13 = _M0L3gluS808;
  _block_2012->$14 = _M0L4gabaS809;
  _block_2012->$15 = _M0L7gsyn__eS810;
  _block_2012->$16 = _M0L7gsyn__iS811;
  _block_2012->$17 = _M0L4e__eS812;
  _block_2012->$18 = _M0L4e__iS813;
  _block_2012->$19 = _M0L3treS814;
  _block_2012->$20 = _M0L3tdeS815;
  _block_2012->$21 = _M0L3triS816;
  _block_2012->$22 = _M0L3tdiS817;
  return _block_2012;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2013;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2013
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2013)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2013->$0 = 0x1p+1f;
  return _block_2013;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt11step__model(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS766,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS764
) {
  float _M0L6t__nowS763;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS765;
  int32_t _M0L7_2abindS767;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS768;
  int32_t _M0L2__S769;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS772;
  int32_t _M0L7_2abindS773;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS774;
  int32_t _M0L2__S775;
  float _M0L2dtS778;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L7_2abindS779;
  int32_t _M0L7_2abindS780;
  struct _M0TP26RiantR8snn__mbt2IF** _M0L7_2abindS781;
  int32_t _M0L2__S782;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS785;
  int32_t _M0L7_2abindS786;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS787;
  int32_t _M0L2__S788;
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6t__nowS763 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS764);
  _M0L7_2abindS765 = _M0L5modelS766->$1;
  _M0L7_2abindS767 = _M0L7_2abindS765->$1;
  _M0L7_2abindS768 = _M0L7_2abindS765->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS768);
  _M0L2__S769 = 0;
  while (1) {
    if (_M0L2__S769 < _M0L7_2abindS767) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS770 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS768[
          _M0L2__S769
        ];
      int32_t _M0L6_2atmpS1858;
      moonbit_incref_cycle_free(_M0L1cS770);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS770, _M0L6t__nowS763);
      moonbit_decref_cycle_free(_M0L1cS770);
      _M0L6_2atmpS1858 = _M0L2__S769 + 1;
      _M0L2__S769 = _M0L6_2atmpS1858;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS768);
    }
    break;
  }
  _M0L7_2abindS772 = _M0L5modelS766->$1;
  _M0L7_2abindS773 = _M0L7_2abindS772->$1;
  _M0L7_2abindS774 = _M0L7_2abindS772->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS774);
  _M0L2__S775 = 0;
  while (1) {
    if (_M0L2__S775 < _M0L7_2abindS773) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS776 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS774[
          _M0L2__S775
        ];
      int32_t _M0L6_2atmpS1859;
      moonbit_incref_cycle_free(_M0L1cS776);
      #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS776, _M0L6t__nowS763);
      moonbit_decref_cycle_free(_M0L1cS776);
      _M0L6_2atmpS1859 = _M0L2__S775 + 1;
      _M0L2__S775 = _M0L6_2atmpS1859;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS774);
    }
    break;
  }
  _M0L2dtS778 = _M0L4timeS764->$2;
  _M0L7_2abindS779 = _M0L5modelS766->$0;
  _M0L7_2abindS780 = _M0L7_2abindS779->$1;
  _M0L7_2abindS781 = _M0L7_2abindS779->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS781);
  _M0L2__S782 = 0;
  while (1) {
    if (_M0L2__S782 < _M0L7_2abindS780) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS783 =
        (struct _M0TP26RiantR8snn__mbt2IF*)_M0L7_2abindS781[_M0L2__S782];
      int32_t _M0L6_2atmpS1860;
      moonbit_incref_cycle_free(_M0L1pS783);
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L1pS783, _M0L2dtS778);
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1pS783);
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L1pS783, _M0L2dtS778);
      moonbit_decref_cycle_free(_M0L1pS783);
      _M0L6_2atmpS1860 = _M0L2__S782 + 1;
      _M0L2__S782 = _M0L6_2atmpS1860;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS781);
    }
    break;
  }
  _M0L7_2abindS785 = _M0L5modelS766->$2;
  _M0L7_2abindS786 = _M0L7_2abindS785->$1;
  _M0L7_2abindS787 = _M0L7_2abindS785->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS787);
  _M0L2__S788 = 0;
  while (1) {
    if (_M0L2__S788 < _M0L7_2abindS786) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS789 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS787[_M0L2__S788];
      struct _M0TPB5ArrayGfE* _M0L1tS1862 = _M0L4timeS764->$0;
      float _M0L6_2atmpS1861;
      int32_t _M0L6_2atmpS1863;
      moonbit_incref_cycle_free(_M0L1mS789);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0L6_2atmpS1861 = _M0MPC15array5Array2atGfE(_M0L1tS1862, 0);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS789, _M0L6_2atmpS1861);
      moonbit_decref_cycle_free(_M0L1mS789);
      _M0L6_2atmpS1863 = _M0L2__S788 + 1;
      _M0L2__S788 = _M0L6_2atmpS1863;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS787);
    }
    break;
  }
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS764, _M0L2dtS778);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS739,
  float _M0L6t__nowS750
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS1857;
  int32_t _M0L6_2atmpS1856;
  int32_t _M0L10use__delayS738;
  struct _M0TPB5ArrayGfE* _M0L3rhoS1855;
  int32_t _M0L6_2atmpS1854;
  int32_t _M0L8use__rhoS740;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS1857 = _M0L1cS739->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS1856 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS1857);
  _M0L10use__delayS738 = _M0L6_2atmpS1856 > 0;
  _M0L3rhoS1855 = _M0L1cS739->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS1854 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS1855);
  _M0L8use__rhoS740 = _M0L6_2atmpS1854 > 0;
  if (_M0L10use__delayS738) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1817 = _M0L1cS739->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS1816 = _M0L3preS1817->$5;
    int32_t _M0L6n__preS741;
    struct _M0TPB8MutLocalGiE* _M0L1jS742;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS741 = _M0MPC15array5Array6lengthGbE(_M0L4fireS1816);
    _M0L1jS742
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS742)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS742->$0 = 0;
    while (1) {
      int32_t _M0L3valS1785 = _M0L1jS742->$0;
      if (_M0L3valS1785 < _M0L6n__preS741) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1788 = _M0L1cS739->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS1786 = _M0L3preS1788->$5;
        int32_t _M0L3valS1787 = _M0L1jS742->$0;
        int32_t _M0L3valS1815;
        int32_t _M0L6_2atmpS1814;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS1786, _M0L3valS1787)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1813 =
            _M0L1cS739->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS1811 = _M0L6matrixS1813->$2;
          int32_t _M0L3valS1812 = _M0L1jS742->$0;
          int32_t _M0L5startS743;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1810;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS1807;
          int32_t _M0L3valS1809;
          int32_t _M0L6_2atmpS1808;
          int32_t _M0L3endS744;
          struct _M0TPB8MutLocalGiE* _M0L1sS745;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS743
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1811, _M0L3valS1812);
          _M0L6matrixS1810 = _M0L1cS739->$4;
          _M0L6rowptrS1807 = _M0L6matrixS1810->$2;
          _M0L3valS1809 = _M0L1jS742->$0;
          _M0L6_2atmpS1808 = _M0L3valS1809 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS744
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1807, _M0L6_2atmpS1808);
          _M0L1sS745
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS745)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS745->$0 = _M0L5startS743;
          while (1) {
            int32_t _M0L3valS1789 = _M0L1sS745->$0;
            if (_M0L3valS1789 < _M0L3endS744) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1806 =
                _M0L1cS739->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS1804 = _M0L6matrixS1806->$3;
              int32_t _M0L3valS1805 = _M0L1sS745->$0;
              int32_t _M0L9post__idxS746;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1803;
              struct _M0TPB5ArrayGfE* _M0L4valsS1801;
              int32_t _M0L3valS1802;
              float _M0L1wS747;
              struct _M0TPB5ArrayGfE* _M0L6delaysS1799;
              int32_t _M0L3valS1800;
              float _M0L1dS748;
              float _M0L9w__scaledS749;
              int32_t _M0L3valS1795;
              int32_t _M0L6_2atmpS1794;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS746
              = _M0MPC15array5Array2atGiE(_M0L6colptrS1804, _M0L3valS1805);
              _M0L6matrixS1803 = _M0L1cS739->$4;
              _M0L4valsS1801 = _M0L6matrixS1803->$4;
              _M0L3valS1802 = _M0L1sS745->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS747
              = _M0MPC15array5Array2atGfE(_M0L4valsS1801, _M0L3valS1802);
              _M0L6delaysS1799 = _M0L1cS739->$5;
              _M0L3valS1800 = _M0L1sS745->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS748
              = _M0MPC15array5Array2atGfE(_M0L6delaysS1799, _M0L3valS1800);
              if (_M0L8use__rhoS740) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS1797 = _M0L1cS739->$6;
                int32_t _M0L3valS1798 = _M0L1sS745->$0;
                float _M0L6_2atmpS1796;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1796
                = _M0MPC15array5Array2atGfE(_M0L3rhoS1797, _M0L3valS1798);
                _M0L9w__scaledS749 = _M0L1wS747 * _M0L6_2atmpS1796;
              } else {
                _M0L9w__scaledS749 = _M0L1wS747;
              }
              if (_M0L1dS748 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS739, _M0L9post__idxS746, _M0L9w__scaledS749);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS1790 =
                  _M0L1cS739->$7;
                float _M0L6_2atmpS1791 = _M0L6t__nowS750 + _M0L1dS748;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS1792;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1793;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS1790, _M0L6_2atmpS1791);
                _M0L14pending__postsS1792 = _M0L1cS739->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS1792, _M0L9post__idxS746);
                _M0L16pending__weightsS1793 = _M0L1cS739->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS1793, _M0L9w__scaledS749);
              }
              _M0L3valS1795 = _M0L1sS745->$0;
              _M0L6_2atmpS1794 = _M0L3valS1795 + 1;
              _M0L1sS745->$0 = _M0L6_2atmpS1794;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS745);
            }
            break;
          }
        }
        _M0L3valS1815 = _M0L1jS742->$0;
        _M0L6_2atmpS1814 = _M0L3valS1815 + 1;
        _M0L1jS742->$0 = _M0L6_2atmpS1814;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS742);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS1851 = _M0L1cS739->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS753;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS1851 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS1851)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS1851, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS1851) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1852 = _M0L1cS739->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS1947 = _M0L4postS1852->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS1947);
      _M0L6targetS753 = _M0L8_2afieldS1947;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1853 = _M0L1cS739->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS1948 = _M0L4postS1853->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS1948);
      _M0L6targetS753 = _M0L8_2afieldS1948;
    }
    if (_M0L8use__rhoS740) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1847 = _M0L1cS739->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1846 = _M0L3preS1847->$5;
      int32_t _M0L6n__preS754;
      struct _M0TPB8MutLocalGiE* _M0L1jS755;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS754 = _M0MPC15array5Array6lengthGbE(_M0L4fireS1846);
      _M0L1jS755
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS755)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS755->$0 = 0;
      while (1) {
        int32_t _M0L3valS1818 = _M0L1jS755->$0;
        if (_M0L3valS1818 < _M0L6n__preS754) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1821 = _M0L1cS739->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS1819 = _M0L3preS1821->$5;
          int32_t _M0L3valS1820 = _M0L1jS755->$0;
          int32_t _M0L3valS1845;
          int32_t _M0L6_2atmpS1844;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS1819, _M0L3valS1820)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1843 =
              _M0L1cS739->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS1841 = _M0L6matrixS1843->$2;
            int32_t _M0L3valS1842 = _M0L1jS755->$0;
            int32_t _M0L5startS756;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1840;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS1837;
            int32_t _M0L3valS1839;
            int32_t _M0L6_2atmpS1838;
            int32_t _M0L3endS757;
            struct _M0TPB8MutLocalGiE* _M0L1sS758;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS756
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1841, _M0L3valS1842);
            _M0L6matrixS1840 = _M0L1cS739->$4;
            _M0L6rowptrS1837 = _M0L6matrixS1840->$2;
            _M0L3valS1839 = _M0L1jS755->$0;
            _M0L6_2atmpS1838 = _M0L3valS1839 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS757
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1837, _M0L6_2atmpS1838);
            _M0L1sS758
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS758)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS758->$0 = _M0L5startS756;
            while (1) {
              int32_t _M0L3valS1822 = _M0L1sS758->$0;
              if (_M0L3valS1822 < _M0L3endS757) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1836 =
                  _M0L1cS739->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS1834 =
                  _M0L6matrixS1836->$3;
                int32_t _M0L3valS1835 = _M0L1sS758->$0;
                int32_t _M0L9post__idxS759;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1833;
                struct _M0TPB5ArrayGfE* _M0L4valsS1831;
                int32_t _M0L3valS1832;
                float _M0L6_2atmpS1827;
                struct _M0TPB5ArrayGfE* _M0L3rhoS1829;
                int32_t _M0L3valS1830;
                float _M0L6_2atmpS1828;
                float _M0L9w__scaledS760;
                float _M0L6_2atmpS1824;
                float _M0L6_2atmpS1823;
                int32_t _M0L3valS1826;
                int32_t _M0L6_2atmpS1825;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS759
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1834, _M0L3valS1835);
                _M0L6matrixS1833 = _M0L1cS739->$4;
                _M0L4valsS1831 = _M0L6matrixS1833->$4;
                _M0L3valS1832 = _M0L1sS758->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1827
                = _M0MPC15array5Array2atGfE(_M0L4valsS1831, _M0L3valS1832);
                _M0L3rhoS1829 = _M0L1cS739->$6;
                _M0L3valS1830 = _M0L1sS758->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1828
                = _M0MPC15array5Array2atGfE(_M0L3rhoS1829, _M0L3valS1830);
                _M0L9w__scaledS760 = _M0L6_2atmpS1827 * _M0L6_2atmpS1828;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1824
                = _M0MPC15array5Array2atGfE(_M0L6targetS753, _M0L9post__idxS759);
                _M0L6_2atmpS1823 = _M0L6_2atmpS1824 + _M0L9w__scaledS760;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS753, _M0L9post__idxS759, _M0L6_2atmpS1823);
                _M0L3valS1826 = _M0L1sS758->$0;
                _M0L6_2atmpS1825 = _M0L3valS1826 + 1;
                _M0L1sS758->$0 = _M0L6_2atmpS1825;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS758);
              }
              break;
            }
          }
          _M0L3valS1845 = _M0L1jS755->$0;
          _M0L6_2atmpS1844 = _M0L3valS1845 + 1;
          _M0L1jS755->$0 = _M0L6_2atmpS1844;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS755);
          moonbit_decref_cycle_free(_M0L6targetS753);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1848 =
        _M0L1cS739->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1850 = _M0L1cS739->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1849 = _M0L3preS1850->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS1848, _M0L4fireS1849, _M0L6targetS753);
      moonbit_decref_cycle_free(_M0L6targetS753);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS731,
  float _M0L6t__nowS734
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS1784;
  int32_t _M0L1nS730;
  struct _M0TPB8MutLocalGiE* _M0L4keptS732;
  struct _M0TPB8MutLocalGiE* _M0L1kS733;
  int32_t _M0L3valS1783;
  int32_t _M0L6_2atmpS1782;
  struct _M0TPB8MutLocalGiE* _M0L4dropS736;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS1784 = _M0L1cS731->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS730 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS1784);
  if (_M0L1nS730 == 0) {
    return 0;
  }
  _M0L4keptS732
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS732)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS732->$0 = 0;
  _M0L1kS733
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS733)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS733->$0 = 0;
  while (1) {
    int32_t _M0L3valS1745 = _M0L1kS733->$0;
    if (_M0L3valS1745 < _M0L1nS730) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS1747 = _M0L1cS731->$7;
      int32_t _M0L3valS1748 = _M0L1kS733->$0;
      float _M0L6_2atmpS1746;
      int32_t _M0L3valS1775;
      int32_t _M0L6_2atmpS1774;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1746
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS1747, _M0L3valS1748);
      if (_M0L6_2atmpS1746 <= _M0L6t__nowS734) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS1753 = _M0L1cS731->$8;
        int32_t _M0L3valS1754 = _M0L1kS733->$0;
        int32_t _M0L6_2atmpS1749;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1751;
        int32_t _M0L3valS1752;
        float _M0L6_2atmpS1750;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS1749
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS1753, _M0L3valS1754);
        _M0L16pending__weightsS1751 = _M0L1cS731->$9;
        _M0L3valS1752 = _M0L1kS733->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS1750
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS1751, _M0L3valS1752);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS731, _M0L6_2atmpS1749, _M0L6_2atmpS1750);
      } else {
        int32_t _M0L3valS1755 = _M0L4keptS732->$0;
        int32_t _M0L3valS1756 = _M0L1kS733->$0;
        int32_t _M0L3valS1773;
        int32_t _M0L6_2atmpS1772;
        if (_M0L3valS1755 != _M0L3valS1756) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS1757 = _M0L1cS731->$7;
          int32_t _M0L3valS1758 = _M0L4keptS732->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS1760 = _M0L1cS731->$7;
          int32_t _M0L3valS1761 = _M0L1kS733->$0;
          float _M0L6_2atmpS1759;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS1762;
          int32_t _M0L3valS1763;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS1765;
          int32_t _M0L3valS1766;
          int32_t _M0L6_2atmpS1764;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1767;
          int32_t _M0L3valS1768;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1770;
          int32_t _M0L3valS1771;
          float _M0L6_2atmpS1769;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1759
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS1760, _M0L3valS1761);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS1757, _M0L3valS1758, _M0L6_2atmpS1759);
          _M0L14pending__postsS1762 = _M0L1cS731->$8;
          _M0L3valS1763 = _M0L4keptS732->$0;
          _M0L14pending__postsS1765 = _M0L1cS731->$8;
          _M0L3valS1766 = _M0L1kS733->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1764
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS1765, _M0L3valS1766);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS1762, _M0L3valS1763, _M0L6_2atmpS1764);
          _M0L16pending__weightsS1767 = _M0L1cS731->$9;
          _M0L3valS1768 = _M0L4keptS732->$0;
          _M0L16pending__weightsS1770 = _M0L1cS731->$9;
          _M0L3valS1771 = _M0L1kS733->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1769
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS1770, _M0L3valS1771);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS1767, _M0L3valS1768, _M0L6_2atmpS1769);
        }
        _M0L3valS1773 = _M0L4keptS732->$0;
        _M0L6_2atmpS1772 = _M0L3valS1773 + 1;
        _M0L4keptS732->$0 = _M0L6_2atmpS1772;
      }
      _M0L3valS1775 = _M0L1kS733->$0;
      _M0L6_2atmpS1774 = _M0L3valS1775 + 1;
      _M0L1kS733->$0 = _M0L6_2atmpS1774;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS733);
    }
    break;
  }
  _M0L3valS1783 = _M0L4keptS732->$0;
  moonbit_decref_cycle_free(_M0L4keptS732);
  _M0L6_2atmpS1782 = _M0L1nS730 - _M0L3valS1783;
  _M0L4dropS736
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS736)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS736->$0 = _M0L6_2atmpS1782;
  while (1) {
    int32_t _M0L3valS1776 = _M0L4dropS736->$0;
    if (_M0L3valS1776 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS1777 = _M0L1cS731->$7;
      void* _M0L6_2atmpS1950;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS1778;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1779;
      void* _M0L6_2atmpS1949;
      int32_t _M0L3valS1781;
      int32_t _M0L6_2atmpS1780;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1950
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS1777);
      moonbit_decref_cycle_free(_M0L6_2atmpS1950);
      _M0L14pending__postsS1778 = _M0L1cS731->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS1778);
      _M0L16pending__weightsS1779 = _M0L1cS731->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1949
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS1779);
      moonbit_decref_cycle_free(_M0L6_2atmpS1949);
      _M0L3valS1781 = _M0L4dropS736->$0;
      _M0L6_2atmpS1780 = _M0L3valS1781 - 1;
      _M0L4dropS736->$0 = _M0L6_2atmpS1780;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS736);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS727,
  int32_t _M0L9post__idxS728,
  float _M0L1wS729
) {
  moonbit_string_t _M0L3symS1732;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS1732 = _M0L1cS727->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS1732 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS1732)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS1732, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS1732) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1738 = _M0L1cS727->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1733 = _M0L4postS1738->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1737 = _M0L1cS727->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1736 = _M0L4postS1737->$13;
    float _M0L6_2atmpS1735;
    float _M0L6_2atmpS1734;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1735
    = _M0MPC15array5Array2atGfE(_M0L3gluS1736, _M0L9post__idxS728);
    _M0L6_2atmpS1734 = _M0L6_2atmpS1735 + _M0L1wS729;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS1733, _M0L9post__idxS728, _M0L6_2atmpS1734);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1744 = _M0L1cS727->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1739 = _M0L4postS1744->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1743 = _M0L1cS727->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1742 = _M0L4postS1743->$14;
    float _M0L6_2atmpS1741;
    float _M0L6_2atmpS1740;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1741
    = _M0MPC15array5Array2atGfE(_M0L4gabaS1742, _M0L9post__idxS728);
    _M0L6_2atmpS1740 = _M0L6_2atmpS1741 + _M0L1wS729;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS1739, _M0L9post__idxS728, _M0L6_2atmpS1740);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS721
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS720;
  int32_t _M0L7_2abindS722;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS723;
  int32_t _M0L2__S724;
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS720 = _M0L5modelS721->$2;
  _M0L7_2abindS722 = _M0L7_2abindS720->$1;
  _M0L7_2abindS723 = _M0L7_2abindS720->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS723);
  _M0L2__S724 = 0;
  while (1) {
    if (_M0L2__S724 < _M0L7_2abindS722) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS725 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS723[_M0L2__S724];
      int32_t _M0L6_2atmpS1731;
      moonbit_incref_cycle_free(_M0L1mS725);
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS725, 0x0p+0f);
      moonbit_decref_cycle_free(_M0L1mS725);
      _M0L6_2atmpS1731 = _M0L2__S724 + 1;
      _M0L2__S724 = _M0L6_2atmpS1731;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS723);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS717,
  float _M0L1tS719
) {
  int32_t _M0L11step__countS1717;
  int32_t _M0L6_2atmpS1716;
  int32_t _M0L11step__countS1719;
  int32_t _M0L9rec__stepS1720;
  int32_t _M0L6_2atmpS1718;
  moonbit_string_t _M0L3symS1723;
  float _M0L1vS718;
  struct _M0TPB5ArrayGfE* _M0L4dataS1721;
  struct _M0TPB5ArrayGfE* _M0L5timesS1722;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS1717 = _M0L1mS717->$6;
  _M0L6_2atmpS1716 = _M0L11step__countS1717 + 1;
  _M0L1mS717->$6 = _M0L6_2atmpS1716;
  _M0L11step__countS1719 = _M0L1mS717->$6;
  _M0L9rec__stepS1720 = _M0L1mS717->$5;
  _M0L6_2atmpS1718 = _M0L11step__countS1719 % _M0L9rec__stepS1720;
  if (_M0L6_2atmpS1718 != 0) {
    return 0;
  }
  _M0L3symS1723 = _M0L1mS717->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS1723 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS1723)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS1723, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS1723) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1726 = _M0L1mS717->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS1724 = _M0L3popS1726->$3;
    int32_t _M0L6neuronS1725 = _M0L1mS717->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS718 = _M0MPC15array5Array2atGfE(_M0L1vS1724, _M0L6neuronS1725);
  } else {
    moonbit_string_t _M0L3symS1727 = _M0L1mS717->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS1727 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS1727)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS1727, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS1727) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1730 = _M0L1mS717->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1728 = _M0L3popS1730->$5;
      int32_t _M0L6neuronS1729 = _M0L1mS717->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1728, _M0L6neuronS1729)) {
        _M0L1vS718 = 0x1p+0f;
      } else {
        _M0L1vS718 = 0x0p+0f;
      }
    } else {
      _M0L1vS718 = 0x0p+0f;
    }
  }
  _M0L4dataS1721 = _M0L1mS717->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS1721, _M0L1vS718);
  _M0L5timesS1722 = _M0L1mS717->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS1722, _M0L1tS719);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS715,
  int32_t _M0L6neuronS716
) {
  float* _M0L6_2atmpS1715;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1712;
  float* _M0L6_2atmpS1714;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1713;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_2025;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS1715 = moonbit_empty_float_array;
  _M0L6_2atmpS1712
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1712)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1712->$0 = _M0L6_2atmpS1715;
  _M0L6_2atmpS1712->$1 = 0;
  _M0L6_2atmpS1714 = moonbit_empty_float_array;
  _M0L6_2atmpS1713
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1713)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1713->$0 = _M0L6_2atmpS1714;
  _M0L6_2atmpS1713->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS715);
  _block_2025
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_2025)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2025->$0 = _M0L3popS715;
  _block_2025->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_2025->$2 = _M0L6_2atmpS1712;
  _block_2025->$3 = _M0L6_2atmpS1713;
  _block_2025->$4 = _M0L6neuronS716;
  _block_2025->$5 = 1;
  _block_2025->$6 = 0;
  return _block_2025;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS711
) {
  int32_t _M0L1nS710;
  int32_t _M0L7_2abindS712;
  int32_t _M0L1iS713;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS710 = _M0L1pS711->$2;
  _M0L7_2abindS712 = 0;
  _M0L1iS713 = _M0L7_2abindS712;
  while (1) {
    if (_M0L1iS713 < _M0L1nS710) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1689 = _M0L1pS711->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS1710 = _M0L1pS711->$9;
      float _M0L6_2atmpS1705;
      struct _M0TPB5ArrayGfE* _M0L1vS1709;
      float _M0L6_2atmpS1707;
      float _M0L4e__eS1708;
      float _M0L6_2atmpS1706;
      float _M0L6_2atmpS1702;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1704;
      float _M0L6_2atmpS1703;
      float _M0L6_2atmpS1691;
      struct _M0TPB5ArrayGfE* _M0L2giS1701;
      float _M0L6_2atmpS1696;
      struct _M0TPB5ArrayGfE* _M0L1vS1700;
      float _M0L6_2atmpS1698;
      float _M0L4e__iS1699;
      float _M0L6_2atmpS1697;
      float _M0L6_2atmpS1693;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1695;
      float _M0L6_2atmpS1694;
      float _M0L6_2atmpS1692;
      float _M0L6_2atmpS1690;
      int32_t _M0L6_2atmpS1711;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1705 = _M0MPC15array5Array2atGfE(_M0L2geS1710, _M0L1iS713);
      _M0L1vS1709 = _M0L1pS711->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1707 = _M0MPC15array5Array2atGfE(_M0L1vS1709, _M0L1iS713);
      _M0L4e__eS1708 = _M0L1pS711->$17;
      _M0L6_2atmpS1706 = _M0L6_2atmpS1707 - _M0L4e__eS1708;
      _M0L6_2atmpS1702 = _M0L6_2atmpS1705 * _M0L6_2atmpS1706;
      _M0L7gsyn__eS1704 = _M0L1pS711->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1703
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1704, _M0L1iS713);
      _M0L6_2atmpS1691 = _M0L6_2atmpS1702 * _M0L6_2atmpS1703;
      _M0L2giS1701 = _M0L1pS711->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1696 = _M0MPC15array5Array2atGfE(_M0L2giS1701, _M0L1iS713);
      _M0L1vS1700 = _M0L1pS711->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1698 = _M0MPC15array5Array2atGfE(_M0L1vS1700, _M0L1iS713);
      _M0L4e__iS1699 = _M0L1pS711->$18;
      _M0L6_2atmpS1697 = _M0L6_2atmpS1698 - _M0L4e__iS1699;
      _M0L6_2atmpS1693 = _M0L6_2atmpS1696 * _M0L6_2atmpS1697;
      _M0L7gsyn__iS1695 = _M0L1pS711->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1694
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1695, _M0L1iS713);
      _M0L6_2atmpS1692 = _M0L6_2atmpS1693 * _M0L6_2atmpS1694;
      _M0L6_2atmpS1690 = _M0L6_2atmpS1691 + _M0L6_2atmpS1692;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1689, _M0L1iS713, _M0L6_2atmpS1690);
      _M0L6_2atmpS1711 = _M0L1iS713 + 1;
      _M0L1iS713 = _M0L6_2atmpS1711;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS702,
  float _M0L2dtS705
) {
  int32_t _M0L1nS701;
  int32_t _M0L7_2abindS703;
  int32_t _M0L1iS704;
  int32_t _M0L7_2abindS707;
  int32_t _M0L1iS708;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS701 = _M0L1pS702->$2;
  _M0L7_2abindS703 = 0;
  _M0L1iS704 = _M0L7_2abindS703;
  while (1) {
    if (_M0L1iS704 < _M0L1nS701) {
      struct _M0TPB5ArrayGfE* _M0L2heS1627 = _M0L1pS702->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1632 = _M0L1pS702->$11;
      float _M0L6_2atmpS1629;
      struct _M0TPB5ArrayGfE* _M0L3gluS1631;
      float _M0L6_2atmpS1630;
      float _M0L6_2atmpS1628;
      struct _M0TPB5ArrayGfE* _M0L2hiS1633;
      struct _M0TPB5ArrayGfE* _M0L2hiS1638;
      float _M0L6_2atmpS1635;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1637;
      float _M0L6_2atmpS1636;
      float _M0L6_2atmpS1634;
      struct _M0TPB5ArrayGfE* _M0L2geS1639;
      struct _M0TPB5ArrayGfE* _M0L2geS1651;
      float _M0L6_2atmpS1641;
      struct _M0TPB5ArrayGfE* _M0L2geS1650;
      float _M0L6_2atmpS1649;
      float _M0L6_2atmpS1647;
      float _M0L3tdeS1648;
      float _M0L6_2atmpS1644;
      struct _M0TPB5ArrayGfE* _M0L2heS1646;
      float _M0L6_2atmpS1645;
      float _M0L6_2atmpS1643;
      float _M0L6_2atmpS1642;
      float _M0L6_2atmpS1640;
      struct _M0TPB5ArrayGfE* _M0L2heS1652;
      struct _M0TPB5ArrayGfE* _M0L2heS1661;
      float _M0L6_2atmpS1654;
      struct _M0TPB5ArrayGfE* _M0L2heS1660;
      float _M0L6_2atmpS1659;
      float _M0L6_2atmpS1657;
      float _M0L3treS1658;
      float _M0L6_2atmpS1656;
      float _M0L6_2atmpS1655;
      float _M0L6_2atmpS1653;
      struct _M0TPB5ArrayGfE* _M0L2giS1662;
      struct _M0TPB5ArrayGfE* _M0L2giS1674;
      float _M0L6_2atmpS1664;
      struct _M0TPB5ArrayGfE* _M0L2giS1673;
      float _M0L6_2atmpS1672;
      float _M0L6_2atmpS1670;
      float _M0L3tdiS1671;
      float _M0L6_2atmpS1667;
      struct _M0TPB5ArrayGfE* _M0L2hiS1669;
      float _M0L6_2atmpS1668;
      float _M0L6_2atmpS1666;
      float _M0L6_2atmpS1665;
      float _M0L6_2atmpS1663;
      struct _M0TPB5ArrayGfE* _M0L2hiS1675;
      struct _M0TPB5ArrayGfE* _M0L2hiS1684;
      float _M0L6_2atmpS1677;
      struct _M0TPB5ArrayGfE* _M0L2hiS1683;
      float _M0L6_2atmpS1682;
      float _M0L6_2atmpS1680;
      float _M0L3triS1681;
      float _M0L6_2atmpS1679;
      float _M0L6_2atmpS1678;
      float _M0L6_2atmpS1676;
      int32_t _M0L6_2atmpS1685;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1629 = _M0MPC15array5Array2atGfE(_M0L2heS1632, _M0L1iS704);
      _M0L3gluS1631 = _M0L1pS702->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1630 = _M0MPC15array5Array2atGfE(_M0L3gluS1631, _M0L1iS704);
      _M0L6_2atmpS1628 = _M0L6_2atmpS1629 + _M0L6_2atmpS1630;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1627, _M0L1iS704, _M0L6_2atmpS1628);
      _M0L2hiS1633 = _M0L1pS702->$12;
      _M0L2hiS1638 = _M0L1pS702->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1635 = _M0MPC15array5Array2atGfE(_M0L2hiS1638, _M0L1iS704);
      _M0L4gabaS1637 = _M0L1pS702->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1636
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1637, _M0L1iS704);
      _M0L6_2atmpS1634 = _M0L6_2atmpS1635 + _M0L6_2atmpS1636;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1633, _M0L1iS704, _M0L6_2atmpS1634);
      _M0L2geS1639 = _M0L1pS702->$9;
      _M0L2geS1651 = _M0L1pS702->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1641 = _M0MPC15array5Array2atGfE(_M0L2geS1651, _M0L1iS704);
      _M0L2geS1650 = _M0L1pS702->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1649 = _M0MPC15array5Array2atGfE(_M0L2geS1650, _M0L1iS704);
      _M0L6_2atmpS1647 = -_M0L6_2atmpS1649;
      _M0L3tdeS1648 = _M0L1pS702->$20;
      _M0L6_2atmpS1644 = _M0L6_2atmpS1647 / _M0L3tdeS1648;
      _M0L2heS1646 = _M0L1pS702->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1645 = _M0MPC15array5Array2atGfE(_M0L2heS1646, _M0L1iS704);
      _M0L6_2atmpS1643 = _M0L6_2atmpS1644 + _M0L6_2atmpS1645;
      _M0L6_2atmpS1642 = _M0L2dtS705 * _M0L6_2atmpS1643;
      _M0L6_2atmpS1640 = _M0L6_2atmpS1641 + _M0L6_2atmpS1642;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1639, _M0L1iS704, _M0L6_2atmpS1640);
      _M0L2heS1652 = _M0L1pS702->$11;
      _M0L2heS1661 = _M0L1pS702->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1654 = _M0MPC15array5Array2atGfE(_M0L2heS1661, _M0L1iS704);
      _M0L2heS1660 = _M0L1pS702->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1659 = _M0MPC15array5Array2atGfE(_M0L2heS1660, _M0L1iS704);
      _M0L6_2atmpS1657 = -_M0L6_2atmpS1659;
      _M0L3treS1658 = _M0L1pS702->$19;
      _M0L6_2atmpS1656 = _M0L6_2atmpS1657 / _M0L3treS1658;
      _M0L6_2atmpS1655 = _M0L2dtS705 * _M0L6_2atmpS1656;
      _M0L6_2atmpS1653 = _M0L6_2atmpS1654 + _M0L6_2atmpS1655;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1652, _M0L1iS704, _M0L6_2atmpS1653);
      _M0L2giS1662 = _M0L1pS702->$10;
      _M0L2giS1674 = _M0L1pS702->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1664 = _M0MPC15array5Array2atGfE(_M0L2giS1674, _M0L1iS704);
      _M0L2giS1673 = _M0L1pS702->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1672 = _M0MPC15array5Array2atGfE(_M0L2giS1673, _M0L1iS704);
      _M0L6_2atmpS1670 = -_M0L6_2atmpS1672;
      _M0L3tdiS1671 = _M0L1pS702->$22;
      _M0L6_2atmpS1667 = _M0L6_2atmpS1670 / _M0L3tdiS1671;
      _M0L2hiS1669 = _M0L1pS702->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1668 = _M0MPC15array5Array2atGfE(_M0L2hiS1669, _M0L1iS704);
      _M0L6_2atmpS1666 = _M0L6_2atmpS1667 + _M0L6_2atmpS1668;
      _M0L6_2atmpS1665 = _M0L2dtS705 * _M0L6_2atmpS1666;
      _M0L6_2atmpS1663 = _M0L6_2atmpS1664 + _M0L6_2atmpS1665;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1662, _M0L1iS704, _M0L6_2atmpS1663);
      _M0L2hiS1675 = _M0L1pS702->$12;
      _M0L2hiS1684 = _M0L1pS702->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1677 = _M0MPC15array5Array2atGfE(_M0L2hiS1684, _M0L1iS704);
      _M0L2hiS1683 = _M0L1pS702->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1682 = _M0MPC15array5Array2atGfE(_M0L2hiS1683, _M0L1iS704);
      _M0L6_2atmpS1680 = -_M0L6_2atmpS1682;
      _M0L3triS1681 = _M0L1pS702->$21;
      _M0L6_2atmpS1679 = _M0L6_2atmpS1680 / _M0L3triS1681;
      _M0L6_2atmpS1678 = _M0L2dtS705 * _M0L6_2atmpS1679;
      _M0L6_2atmpS1676 = _M0L6_2atmpS1677 + _M0L6_2atmpS1678;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1675, _M0L1iS704, _M0L6_2atmpS1676);
      _M0L6_2atmpS1685 = _M0L1iS704 + 1;
      _M0L1iS704 = _M0L6_2atmpS1685;
      continue;
    }
    break;
  }
  _M0L7_2abindS707 = 0;
  _M0L1iS708 = _M0L7_2abindS707;
  while (1) {
    if (_M0L1iS708 < _M0L1nS701) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1686 = _M0L1pS702->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1687;
      int32_t _M0L6_2atmpS1688;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1686, _M0L1iS708, 0x0p+0f);
      _M0L4gabaS1687 = _M0L1pS702->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1687, _M0L1iS708, 0x0p+0f);
      _M0L6_2atmpS1688 = _M0L1iS708 + 1;
      _M0L1iS708 = _M0L6_2atmpS1688;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS687,
  float _M0L2dtS696
) {
  int32_t _M0L1nS686;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S688;
  float _M0L2tmS689;
  float _M0L2elS690;
  float _M0L1rS691;
  float _M0L2vtS692;
  float _M0L2vrS693;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1626;
  float _M0L11tabs__constS694;
  float _M0L6_2atmpS1625;
  int32_t _M0L11tabs__stepsS695;
  int32_t _M0L7_2abindS697;
  int32_t _M0L1iS698;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS686 = _M0L1pS687->$2;
  _M0L3p__S688 = _M0L1pS687->$0;
  _M0L2tmS689 = _M0L3p__S688->$2;
  _M0L2elS690 = _M0L3p__S688->$5;
  _M0L1rS691 = _M0L3p__S688->$6;
  _M0L2vtS692 = _M0L3p__S688->$3;
  _M0L2vrS693 = _M0L3p__S688->$4;
  _M0L5spikeS1626 = _M0L1pS687->$1;
  _M0L11tabs__constS694 = _M0L5spikeS1626->$0;
  _M0L6_2atmpS1625 = _M0L11tabs__constS694 / _M0L2dtS696;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS695 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1625);
  _M0L7_2abindS697 = 0;
  _M0L1iS698 = _M0L7_2abindS697;
  while (1) {
    if (_M0L1iS698 < _M0L1nS686) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1585 = _M0L1pS687->$6;
      int32_t _M0L6_2atmpS1584;
      struct _M0TPB5ArrayGfE* _M0L1vS1591;
      struct _M0TPB5ArrayGfE* _M0L1vS1612;
      float _M0L6_2atmpS1593;
      float _M0L6_2atmpS1595;
      struct _M0TPB5ArrayGfE* _M0L1vS1611;
      float _M0L6_2atmpS1610;
      float _M0L6_2atmpS1609;
      float _M0L6_2atmpS1601;
      struct _M0TPB5ArrayGfE* _M0L1wS1608;
      float _M0L6_2atmpS1607;
      float _M0L6_2atmpS1604;
      struct _M0TPB5ArrayGfE* _M0L1iS1606;
      float _M0L6_2atmpS1605;
      float _M0L6_2atmpS1603;
      float _M0L6_2atmpS1602;
      float _M0L6_2atmpS1597;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1600;
      float _M0L6_2atmpS1599;
      float _M0L6_2atmpS1598;
      float _M0L6_2atmpS1596;
      float _M0L6_2atmpS1594;
      float _M0L6_2atmpS1592;
      struct _M0TPB5ArrayGbE* _M0L4fireS1613;
      struct _M0TPB5ArrayGfE* _M0L1vS1616;
      float _M0L6_2atmpS1615;
      int32_t _M0L6_2atmpS1614;
      struct _M0TPB5ArrayGfE* _M0L1vS1617;
      struct _M0TPB5ArrayGbE* _M0L4fireS1619;
      float _M0L6_2atmpS1618;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1621;
      struct _M0TPB5ArrayGbE* _M0L4fireS1623;
      int32_t _M0L6_2atmpS1622;
      int32_t _M0L6_2atmpS1583;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1584
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1585, _M0L1iS698);
      if (_M0L6_2atmpS1584 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1586 = _M0L1pS687->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1587;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1590;
        int32_t _M0L6_2atmpS1589;
        int32_t _M0L6_2atmpS1588;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1586, _M0L1iS698, 0);
        _M0L4tabsS1587 = _M0L1pS687->$6;
        _M0L4tabsS1590 = _M0L1pS687->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1589
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1590, _M0L1iS698);
        _M0L6_2atmpS1588 = _M0L6_2atmpS1589 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1587, _M0L1iS698, _M0L6_2atmpS1588);
        goto join_699;
      }
      _M0L1vS1591 = _M0L1pS687->$3;
      _M0L1vS1612 = _M0L1pS687->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1593 = _M0MPC15array5Array2atGfE(_M0L1vS1612, _M0L1iS698);
      _M0L6_2atmpS1595 = _M0L2dtS696 / _M0L2tmS689;
      _M0L1vS1611 = _M0L1pS687->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1610 = _M0MPC15array5Array2atGfE(_M0L1vS1611, _M0L1iS698);
      _M0L6_2atmpS1609 = _M0L6_2atmpS1610 - _M0L2elS690;
      _M0L6_2atmpS1601 = -_M0L6_2atmpS1609;
      _M0L1wS1608 = _M0L1pS687->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1607 = _M0MPC15array5Array2atGfE(_M0L1wS1608, _M0L1iS698);
      _M0L6_2atmpS1604 = -_M0L6_2atmpS1607;
      _M0L1iS1606 = _M0L1pS687->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1605 = _M0MPC15array5Array2atGfE(_M0L1iS1606, _M0L1iS698);
      _M0L6_2atmpS1603 = _M0L6_2atmpS1604 + _M0L6_2atmpS1605;
      _M0L6_2atmpS1602 = _M0L1rS691 * _M0L6_2atmpS1603;
      _M0L6_2atmpS1597 = _M0L6_2atmpS1601 + _M0L6_2atmpS1602;
      _M0L9syn__currS1600 = _M0L1pS687->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1599
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1600, _M0L1iS698);
      _M0L6_2atmpS1598 = _M0L1rS691 * _M0L6_2atmpS1599;
      _M0L6_2atmpS1596 = _M0L6_2atmpS1597 - _M0L6_2atmpS1598;
      _M0L6_2atmpS1594 = _M0L6_2atmpS1595 * _M0L6_2atmpS1596;
      _M0L6_2atmpS1592 = _M0L6_2atmpS1593 + _M0L6_2atmpS1594;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1591, _M0L1iS698, _M0L6_2atmpS1592);
      _M0L4fireS1613 = _M0L1pS687->$5;
      _M0L1vS1616 = _M0L1pS687->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1615 = _M0MPC15array5Array2atGfE(_M0L1vS1616, _M0L1iS698);
      _M0L6_2atmpS1614 = _M0L6_2atmpS1615 > _M0L2vtS692;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1613, _M0L1iS698, _M0L6_2atmpS1614);
      _M0L1vS1617 = _M0L1pS687->$3;
      _M0L4fireS1619 = _M0L1pS687->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1619, _M0L1iS698)) {
        _M0L6_2atmpS1618 = _M0L2vrS693;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1620 = _M0L1pS687->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1618 = _M0MPC15array5Array2atGfE(_M0L1vS1620, _M0L1iS698);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1617, _M0L1iS698, _M0L6_2atmpS1618);
      _M0L4tabsS1621 = _M0L1pS687->$6;
      _M0L4fireS1623 = _M0L1pS687->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1623, _M0L1iS698)) {
        _M0L6_2atmpS1622 = _M0L11tabs__stepsS695;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1624 = _M0L1pS687->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1622
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1624, _M0L1iS698);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1621, _M0L1iS698, _M0L6_2atmpS1622);
      goto join_699;
      goto joinlet_2030;
      join_699:;
      _M0L6_2atmpS1583 = _M0L1iS698 + 1;
      _M0L1iS698 = _M0L6_2atmpS1583;
      continue;
      joinlet_2030:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS674,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS677,
  struct _M0TPB5ArrayGfE* _M0L7post__gS683
) {
  int32_t _M0L4rowsS673;
  int32_t _M0L7_2abindS675;
  int32_t _M0L1iS676;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS673 = _M0L1mS674->$0;
  _M0L7_2abindS675 = 0;
  _M0L1iS676 = _M0L7_2abindS675;
  while (1) {
    if (_M0L1iS676 < _M0L4rowsS673) {
      int32_t _M0L6_2atmpS1582;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS677, _M0L1iS676)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1581 = _M0L1mS674->$2;
        int32_t _M0L5startS678;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1579;
        int32_t _M0L6_2atmpS1580;
        int32_t _M0L3endS679;
        int32_t _M0L1kS680;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS678
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1581, _M0L1iS676);
        _M0L6rowptrS1579 = _M0L1mS674->$2;
        _M0L6_2atmpS1580 = _M0L1iS676 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS679
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1579, _M0L6_2atmpS1580);
        _M0L1kS680 = _M0L5startS678;
        while (1) {
          if (_M0L1kS680 < _M0L3endS679) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1577 = _M0L1mS674->$3;
            int32_t _M0L9post__idxS681;
            struct _M0TPB5ArrayGfE* _M0L4valsS1576;
            float _M0L1wS682;
            float _M0L6_2atmpS1575;
            float _M0L6_2atmpS1574;
            int32_t _M0L6_2atmpS1578;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS681
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1577, _M0L1kS680);
            _M0L4valsS1576 = _M0L1mS674->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS682
            = _M0MPC15array5Array2atGfE(_M0L4valsS1576, _M0L1kS680);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1575
            = _M0MPC15array5Array2atGfE(_M0L7post__gS683, _M0L9post__idxS681);
            _M0L6_2atmpS1574 = _M0L6_2atmpS1575 + _M0L1wS682;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS683, _M0L9post__idxS681, _M0L6_2atmpS1574);
            _M0L6_2atmpS1578 = _M0L1kS680 + 1;
            _M0L1kS680 = _M0L6_2atmpS1578;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1582 = _M0L1iS676 + 1;
      _M0L1iS676 = _M0L6_2atmpS1582;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS667,
  int32_t _M0L4colsS668,
  float _M0L2muS669,
  float _M0L5sigmaS670,
  float _M0L1pS671,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS672
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS667, _M0L4colsS668, _M0L2muS669, _M0L5sigmaS670, _M0L1pS671, 0, _M0L3rngS672);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS581,
  int32_t _M0L4colsS585,
  float _M0L2muS591,
  float _M0L5sigmaS592,
  float _M0L1pS604,
  int32_t _M0L4ruleS598,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS594
) {
  float* _M0L6_2atmpS1573;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1572;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS580;
  int32_t _M0L7_2abindS582;
  int32_t _M0L1iS583;
  int32_t _M0L6_2atmpS1571;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS657;
  int32_t* _M0L6_2atmpS1570;
  struct _M0TPB5ArrayGiE* _M0L6colptrS658;
  float* _M0L6_2atmpS1569;
  struct _M0TPB5ArrayGfE* _M0L4valsS659;
  int32_t _M0L7_2abindS660;
  int32_t _M0L1iS661;
  int32_t _M0L6_2atmpS1568;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2052;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1573 = moonbit_empty_float_array;
  _M0L6_2atmpS1572
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1572)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1572->$0 = _M0L6_2atmpS1573;
  _M0L6_2atmpS1572->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS580
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS581, _M0L6_2atmpS1572);
  _M0L7_2abindS582 = 0;
  _M0L1iS583 = _M0L7_2abindS582;
  while (1) {
    if (_M0L1iS583 < _M0L4rowsS581) {
      struct _M0TPB5ArrayGfE* _M0L3rowS584;
      int32_t _M0L7_2abindS586;
      int32_t _M0L1jS587;
      int32_t _M0L6_2atmpS1524;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS584 = _M0MPC15array5Array4makeGfE(_M0L4colsS585, 0x0p+0f);
      _M0L7_2abindS586 = 0;
      _M0L1jS587 = _M0L7_2abindS586;
      while (1) {
        if (_M0L1jS587 < _M0L4colsS585) {
          double _M0L2z1S589;
          struct _M0TUddE* _M0L7_2abindS593;
          double _M0L5_2az1S595;
          float _M0L6_2atmpS1522;
          float _M0L6_2atmpS1521;
          float _M0L1wS590;
          int32_t _M0L6_2atmpS1523;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS593
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS594);
          _M0L5_2az1S595 = _M0L7_2abindS593->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS593);
          _M0L2z1S589 = _M0L5_2az1S595;
          goto join_588;
          goto joinlet_2035;
          join_588:;
          _M0L6_2atmpS1522 = (float)_M0L2z1S589;
          _M0L6_2atmpS1521 = _M0L5sigmaS592 * _M0L6_2atmpS1522;
          _M0L1wS590 = _M0L2muS591 + _M0L6_2atmpS1521;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS584, _M0L1jS587, _M0L1wS590);
          joinlet_2035:;
          _M0L6_2atmpS1523 = _M0L1jS587 + 1;
          _M0L1jS587 = _M0L6_2atmpS1523;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS580, _M0L1iS583, _M0L3rowS584);
      _M0L6_2atmpS1524 = _M0L1iS583 + 1;
      _M0L1iS583 = _M0L6_2atmpS1524;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS598) {
    case 0: {
      int32_t _M0L7_2abindS599 = 0;
      int32_t _M0L1iS600 = _M0L7_2abindS599;
      while (1) {
        if (_M0L1iS600 < _M0L4rowsS581) {
          int32_t _M0L7_2abindS601 = 0;
          int32_t _M0L1jS602 = _M0L7_2abindS601;
          int32_t _M0L6_2atmpS1527;
          while (1) {
            if (_M0L1jS602 < _M0L4colsS585) {
              float _M0L1uS603;
              int32_t _M0L6_2atmpS1526;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS603 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS594);
              if (_M0L1uS603 >= _M0L1pS604) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1525;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1525
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS580, _M0L1iS600);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1525, _M0L1jS602, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1525);
              }
              _M0L6_2atmpS1526 = _M0L1jS602 + 1;
              _M0L1jS602 = _M0L6_2atmpS1526;
              continue;
            }
            break;
          }
          _M0L6_2atmpS1527 = _M0L1iS600 + 1;
          _M0L1iS600 = _M0L6_2atmpS1527;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS1545 = (float)_M0L4rowsS581;
      float _M0L6_2atmpS1544 = _M0L6_2atmpS1545 * _M0L1pS604;
      int32_t _M0L7n__keepS607;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS607 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1544);
      if (_M0L7n__keepS607 > 0 && _M0L7n__keepS607 <= _M0L4rowsS581) {
        int32_t _M0L7_2abindS608 = 0;
        int32_t _M0L1jS609 = _M0L7_2abindS608;
        while (1) {
          if (_M0L1jS609 < _M0L4colsS585) {
            int32_t* _M0L6_2atmpS1539 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS610 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS611;
            int32_t _M0L1kS612;
            int32_t _M0L7n__dropS614;
            int32_t _M0L7_2abindS615;
            int32_t _M0L1kS616;
            int32_t _M0L7_2abindS622;
            int32_t _M0L1kS623;
            int32_t _M0L6_2atmpS1540;
            Moonbit_object_header(_M0L8pre__idxS610)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L8pre__idxS610->$0 = _M0L6_2atmpS1539;
            _M0L8pre__idxS610->$1 = 0;
            _M0L7_2abindS611 = 0;
            _M0L1kS612 = _M0L7_2abindS611;
            while (1) {
              if (_M0L1kS612 < _M0L4rowsS581) {
                int32_t _M0L6_2atmpS1528;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS610, _M0L1kS612);
                _M0L6_2atmpS1528 = _M0L1kS612 + 1;
                _M0L1kS612 = _M0L6_2atmpS1528;
                continue;
              }
              break;
            }
            _M0L7n__dropS614 = _M0L4rowsS581 - _M0L7n__keepS607;
            _M0L7_2abindS615 = 0;
            _M0L1kS616 = _M0L7_2abindS615;
            while (1) {
              if (_M0L1kS616 < _M0L7n__dropS614) {
                float _M0L1uS617;
                float _M0L6_2atmpS1532;
                float _M0L6_2atmpS1534;
                float _M0L6_2atmpS1533;
                float _M0L6_2atmpS1531;
                int32_t _M0L6_2atmpS1530;
                int32_t _M0L6r__idxS618;
                int32_t _M0L10r__clampedS619;
                int32_t _M0L3tmpS620;
                int32_t _M0L6_2atmpS1529;
                int32_t _M0L6_2atmpS1535;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS617 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS594);
                _M0L6_2atmpS1532 = (float)_M0L4rowsS581;
                _M0L6_2atmpS1534 = (float)_M0L1kS616;
                _M0L6_2atmpS1533 = _M0L6_2atmpS1534 * _M0L1uS617;
                _M0L6_2atmpS1531 = _M0L6_2atmpS1532 - _M0L6_2atmpS1533;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1530
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1531);
                _M0L6r__idxS618 = _M0L1kS616 + _M0L6_2atmpS1530;
                if (_M0L6r__idxS618 >= _M0L4rowsS581) {
                  _M0L10r__clampedS619 = _M0L4rowsS581 - 1;
                } else {
                  _M0L10r__clampedS619 = _M0L6r__idxS618;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS620
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS610, _M0L1kS616);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1529
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS610, _M0L10r__clampedS619);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS610, _M0L1kS616, _M0L6_2atmpS1529);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS610, _M0L10r__clampedS619, _M0L3tmpS620);
                _M0L6_2atmpS1535 = _M0L1kS616 + 1;
                _M0L1kS616 = _M0L6_2atmpS1535;
                continue;
              }
              break;
            }
            _M0L7_2abindS622 = 0;
            _M0L1kS623 = _M0L7_2abindS622;
            while (1) {
              if (_M0L1kS623 < _M0L7n__dropS614) {
                int32_t _M0L6_2atmpS1537;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1536;
                int32_t _M0L6_2atmpS1538;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1537
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS610, _M0L1kS623);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1536
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS580, _M0L6_2atmpS1537);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1536, _M0L1jS609, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1536);
                _M0L6_2atmpS1538 = _M0L1kS623 + 1;
                _M0L1kS623 = _M0L6_2atmpS1538;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS610);
              }
              break;
            }
            _M0L6_2atmpS1540 = _M0L1jS609 + 1;
            _M0L1jS609 = _M0L6_2atmpS1540;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS607 == 0) {
        int32_t _M0L7_2abindS626 = 0;
        int32_t _M0L1iS627 = _M0L7_2abindS626;
        while (1) {
          if (_M0L1iS627 < _M0L4rowsS581) {
            int32_t _M0L7_2abindS628 = 0;
            int32_t _M0L1jS629 = _M0L7_2abindS628;
            int32_t _M0L6_2atmpS1543;
            while (1) {
              if (_M0L1jS629 < _M0L4colsS585) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1541;
                int32_t _M0L6_2atmpS1542;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1541
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS580, _M0L1iS627);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1541, _M0L1jS629, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1541);
                _M0L6_2atmpS1542 = _M0L1jS629 + 1;
                _M0L1jS629 = _M0L6_2atmpS1542;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1543 = _M0L1iS627 + 1;
            _M0L1iS627 = _M0L6_2atmpS1543;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS1563 = (float)_M0L4colsS585;
      float _M0L6_2atmpS1562 = _M0L6_2atmpS1563 * _M0L1pS604;
      int32_t _M0L7n__keepS632;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS632 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1562);
      if (_M0L7n__keepS632 > 0 && _M0L7n__keepS632 <= _M0L4colsS585) {
        int32_t _M0L7_2abindS633 = 0;
        int32_t _M0L1iS634 = _M0L7_2abindS633;
        while (1) {
          if (_M0L1iS634 < _M0L4rowsS581) {
            int32_t* _M0L6_2atmpS1557 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS635 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS636;
            int32_t _M0L1kS637;
            int32_t _M0L7n__dropS639;
            int32_t _M0L7_2abindS640;
            int32_t _M0L1kS641;
            int32_t _M0L7_2abindS647;
            int32_t _M0L1kS648;
            int32_t _M0L6_2atmpS1558;
            Moonbit_object_header(_M0L9post__idxS635)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L9post__idxS635->$0 = _M0L6_2atmpS1557;
            _M0L9post__idxS635->$1 = 0;
            _M0L7_2abindS636 = 0;
            _M0L1kS637 = _M0L7_2abindS636;
            while (1) {
              if (_M0L1kS637 < _M0L4colsS585) {
                int32_t _M0L6_2atmpS1546;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS635, _M0L1kS637);
                _M0L6_2atmpS1546 = _M0L1kS637 + 1;
                _M0L1kS637 = _M0L6_2atmpS1546;
                continue;
              }
              break;
            }
            _M0L7n__dropS639 = _M0L4colsS585 - _M0L7n__keepS632;
            _M0L7_2abindS640 = 0;
            _M0L1kS641 = _M0L7_2abindS640;
            while (1) {
              if (_M0L1kS641 < _M0L7n__dropS639) {
                float _M0L1uS642;
                float _M0L6_2atmpS1550;
                float _M0L6_2atmpS1552;
                float _M0L6_2atmpS1551;
                float _M0L6_2atmpS1549;
                int32_t _M0L6_2atmpS1548;
                int32_t _M0L6r__idxS643;
                int32_t _M0L10r__clampedS644;
                int32_t _M0L3tmpS645;
                int32_t _M0L6_2atmpS1547;
                int32_t _M0L6_2atmpS1553;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS642 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS594);
                _M0L6_2atmpS1550 = (float)_M0L4colsS585;
                _M0L6_2atmpS1552 = (float)_M0L1kS641;
                _M0L6_2atmpS1551 = _M0L6_2atmpS1552 * _M0L1uS642;
                _M0L6_2atmpS1549 = _M0L6_2atmpS1550 - _M0L6_2atmpS1551;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1548
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1549);
                _M0L6r__idxS643 = _M0L1kS641 + _M0L6_2atmpS1548;
                if (_M0L6r__idxS643 >= _M0L4colsS585) {
                  _M0L10r__clampedS644 = _M0L4colsS585 - 1;
                } else {
                  _M0L10r__clampedS644 = _M0L6r__idxS643;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS645
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS635, _M0L1kS641);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1547
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS635, _M0L10r__clampedS644);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS635, _M0L1kS641, _M0L6_2atmpS1547);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS635, _M0L10r__clampedS644, _M0L3tmpS645);
                _M0L6_2atmpS1553 = _M0L1kS641 + 1;
                _M0L1kS641 = _M0L6_2atmpS1553;
                continue;
              }
              break;
            }
            _M0L7_2abindS647 = 0;
            _M0L1kS648 = _M0L7_2abindS647;
            while (1) {
              if (_M0L1kS648 < _M0L7n__dropS639) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1554;
                int32_t _M0L6_2atmpS1555;
                int32_t _M0L6_2atmpS1556;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1554
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS580, _M0L1iS634);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1555
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS635, _M0L1kS648);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1554, _M0L6_2atmpS1555, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1554);
                _M0L6_2atmpS1556 = _M0L1kS648 + 1;
                _M0L1kS648 = _M0L6_2atmpS1556;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS635);
              }
              break;
            }
            _M0L6_2atmpS1558 = _M0L1iS634 + 1;
            _M0L1iS634 = _M0L6_2atmpS1558;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS632 == 0) {
        int32_t _M0L7_2abindS651 = 0;
        int32_t _M0L1iS652 = _M0L7_2abindS651;
        while (1) {
          if (_M0L1iS652 < _M0L4rowsS581) {
            int32_t _M0L7_2abindS653 = 0;
            int32_t _M0L1jS654 = _M0L7_2abindS653;
            int32_t _M0L6_2atmpS1561;
            while (1) {
              if (_M0L1jS654 < _M0L4colsS585) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1559;
                int32_t _M0L6_2atmpS1560;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1559
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS580, _M0L1iS652);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1559, _M0L1jS654, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1559);
                _M0L6_2atmpS1560 = _M0L1jS654 + 1;
                _M0L1jS654 = _M0L6_2atmpS1560;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1561 = _M0L1iS652 + 1;
            _M0L1iS652 = _M0L6_2atmpS1561;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS1571 = _M0L4rowsS581 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS657 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS1571, 0);
  _M0L6_2atmpS1570 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS658
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS658)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6colptrS658->$0 = _M0L6_2atmpS1570;
  _M0L6colptrS658->$1 = 0;
  _M0L6_2atmpS1569 = moonbit_empty_float_array;
  _M0L4valsS659
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS659)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS659->$0 = _M0L6_2atmpS1569;
  _M0L4valsS659->$1 = 0;
  _M0L7_2abindS660 = 0;
  _M0L1iS661 = _M0L7_2abindS660;
  while (1) {
    if (_M0L1iS661 < _M0L4rowsS581) {
      int32_t _M0L6_2atmpS1564;
      int32_t _M0L7_2abindS662;
      int32_t _M0L1jS663;
      int32_t _M0L6_2atmpS1567;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS1564 = _M0MPC15array5Array6lengthGfE(_M0L4valsS659);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS657, _M0L1iS661, _M0L6_2atmpS1564);
      _M0L7_2abindS662 = 0;
      _M0L1jS663 = _M0L7_2abindS662;
      while (1) {
        if (_M0L1jS663 < _M0L4colsS585) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS1565;
          float _M0L1vS664;
          int32_t _M0L6_2atmpS1566;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS1565
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS580, _M0L1iS661);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS664
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS1565, _M0L1jS663);
          moonbit_decref_cycle_free(_M0L6_2atmpS1565);
          if (_M0L1vS664 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS658, _M0L1jS663);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS659, _M0L1vS664);
          }
          _M0L6_2atmpS1566 = _M0L1jS663 + 1;
          _M0L1jS663 = _M0L6_2atmpS1566;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1567 = _M0L1iS661 + 1;
      _M0L1iS661 = _M0L6_2atmpS1567;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS580);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1568 = _M0MPC15array5Array6lengthGfE(_M0L4valsS659);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS657, _M0L4rowsS581, _M0L6_2atmpS1568);
  _block_2052
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2052)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_2052->$0 = _M0L4rowsS581;
  _block_2052->$1 = _M0L4colsS585;
  _block_2052->$2 = _M0L6rowptrS657;
  _block_2052->$3 = _M0L6colptrS658;
  _block_2052->$4 = _M0L4valsS659;
  return _block_2052;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS579
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS1520;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS1520 = _M0L1mS579->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS1520);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS577
) {
  struct _M0TUmmmmE* _M0L1sS576;
  uint64_t _M0L6_2atmpS1519;
  struct _M0TUmmmmE* _M0L1tS578;
  uint64_t _M0L6_2atmpS1515;
  uint64_t _M0L6_2atmpS1516;
  uint64_t _M0L6_2atmpS1517;
  uint64_t _M0L6_2atmpS1518;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2053;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS576 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS577);
  _M0L6_2atmpS1519 = _M0L1sS576->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS578 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1519);
  _M0L6_2atmpS1515 = _M0L1sS576->$0;
  _M0L6_2atmpS1516 = _M0L1sS576->$1;
  _M0L6_2atmpS1517 = _M0L1sS576->$2;
  moonbit_decref_cycle_free(_M0L1sS576);
  _M0L6_2atmpS1518 = _M0L1tS578->$0;
  moonbit_decref_cycle_free(_M0L1tS578);
  _block_2053
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2053)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2053->$0 = _M0L6_2atmpS1515;
  _block_2053->$1 = _M0L6_2atmpS1516;
  _block_2053->$2 = _M0L6_2atmpS1517;
  _block_2053->$3 = _M0L6_2atmpS1518;
  return _block_2053;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS568) {
  uint64_t _M0L2s1S567;
  uint64_t _M0L2z1S569;
  uint64_t _M0L2s2S570;
  uint64_t _M0L2z2S571;
  uint64_t _M0L2s3S572;
  uint64_t _M0L2z3S573;
  uint64_t _M0L2s4S574;
  uint64_t _M0L2z4S575;
  struct _M0TUmmmmE* _block_2054;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S567 = _M0L4seedS568 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S569 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S567);
  _M0L2s2S570 = _M0L2s1S567 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S571 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S570);
  _M0L2s3S572 = _M0L2s2S570 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S573 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S572);
  _M0L2s4S574 = _M0L2s3S572 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S575 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S574);
  _block_2054 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2054)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2054->$0 = _M0L2z1S569;
  _block_2054->$1 = _M0L2z2S571;
  _block_2054->$2 = _M0L2z3S573;
  _block_2054->$3 = _M0L2z4S575;
  return _block_2054;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS565) {
  uint64_t _M0L6_2atmpS1514;
  uint64_t _M0L6_2atmpS1513;
  uint64_t _M0L1zS564;
  uint64_t _M0L6_2atmpS1512;
  uint64_t _M0L6_2atmpS1511;
  uint64_t _M0L1zS566;
  uint64_t _M0L6_2atmpS1510;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1514 = _M0L1zS565 >> 30;
  _M0L6_2atmpS1513 = _M0L1zS565 ^ _M0L6_2atmpS1514;
  _M0L1zS564 = _M0L6_2atmpS1513 * 13787848793156543929ull;
  _M0L6_2atmpS1512 = _M0L1zS564 >> 27;
  _M0L6_2atmpS1511 = _M0L1zS564 ^ _M0L6_2atmpS1512;
  _M0L1zS566 = _M0L6_2atmpS1511 * 10723151780598845931ull;
  _M0L6_2atmpS1510 = _M0L1zS566 >> 31;
  return _M0L1zS566 ^ _M0L6_2atmpS1510;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS559
) {
  double _M0L2u1S558;
  double _M0L8u1__safeS560;
  double _M0L2u2S561;
  double _M0L6_2atmpS1509;
  double _M0L6_2atmpS1508;
  double _M0L1rS562;
  double _M0L5thetaS563;
  double _M0L6_2atmpS1507;
  double _M0L6_2atmpS1504;
  double _M0L6_2atmpS1506;
  double _M0L6_2atmpS1505;
  struct _M0TUddE* _block_2055;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S558 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS559);
  if (_M0L2u1S558 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS560 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS560 = _M0L2u1S558;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S561 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS559);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1509 = _M0FPC14math2ln(_M0L8u1__safeS560);
  _M0L6_2atmpS1508 = -0x1p+1 * _M0L6_2atmpS1509;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS562 = sqrt(_M0L6_2atmpS1508);
  _M0L5thetaS563 = 0x1.921fb54442d18p+2 * _M0L2u2S561;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1507 = _M0FPC14math3cos(_M0L5thetaS563);
  _M0L6_2atmpS1504 = _M0L1rS562 * _M0L6_2atmpS1507;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1506 = _M0FPC14math3sin(_M0L5thetaS563);
  _M0L6_2atmpS1505 = _M0L1rS562 * _M0L6_2atmpS1506;
  _block_2055 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2055)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2055->$0 = _M0L6_2atmpS1504;
  _block_2055->$1 = _M0L6_2atmpS1505;
  return _block_2055;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS556
) {
  uint64_t _M0L1uS555;
  uint64_t _M0L4bitsS557;
  double _M0L6_2atmpS1503;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS555 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS556);
  _M0L4bitsS557 = _M0L1uS555 >> 11;
  _M0L6_2atmpS1503 = (double)_M0L4bitsS557;
  return _M0L6_2atmpS1503 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS553,
  float _M0L2dtS554
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1495;
  struct _M0TPB5ArrayGfE* _M0L1tS1498;
  float _M0L6_2atmpS1497;
  float _M0L6_2atmpS1496;
  struct _M0TPB5ArrayGiE* _M0L2ttS1499;
  struct _M0TPB5ArrayGiE* _M0L2ttS1502;
  int32_t _M0L6_2atmpS1501;
  int32_t _M0L6_2atmpS1500;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1495 = _M0L1tS553->$0;
  _M0L1tS1498 = _M0L1tS553->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1497 = _M0MPC15array5Array2atGfE(_M0L1tS1498, 0);
  _M0L6_2atmpS1496 = _M0L6_2atmpS1497 + _M0L2dtS554;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS1495, 0, _M0L6_2atmpS1496);
  _M0L2ttS1499 = _M0L1tS553->$1;
  _M0L2ttS1502 = _M0L1tS553->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1501 = _M0MPC15array5Array2atGiE(_M0L2ttS1502, 0);
  _M0L6_2atmpS1500 = _M0L6_2atmpS1501 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS1499, 0, _M0L6_2atmpS1500);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS551,
  float _M0L1vS552
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS551->$2 = _M0L1vS552;
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS550
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1494;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1494 = _M0L1tS550->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS1494, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS1493;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1490;
  int32_t* _M0L6_2atmpS1492;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1491;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2056;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1493 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS1493[0] = 0x0p+0f;
  _M0L6_2atmpS1490
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1490)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1490->$0 = _M0L6_2atmpS1493;
  _M0L6_2atmpS1490->$1 = 1;
  _M0L6_2atmpS1492 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS1492[0] = 0;
  _M0L6_2atmpS1491
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1491)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS1491->$0 = _M0L6_2atmpS1492;
  _M0L6_2atmpS1491->$1 = 1;
  _block_2056
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2056)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 65, 0);
  _block_2056->$0 = _M0L6_2atmpS1490;
  _block_2056->$1 = _M0L6_2atmpS1491;
  _block_2056->$2 = 0x1p-3f;
  return _block_2056;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS548
) {
  uint32_t _M0L1uS547;
  uint32_t _M0L4bitsS549;
  double _M0L6_2atmpS1489;
  double _M0L6_2atmpS1488;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS547 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS548);
  _M0L4bitsS549 = _M0L1uS547 >> 8;
  _M0L6_2atmpS1489 = (double)_M0L4bitsS549;
  _M0L6_2atmpS1488 = _M0L6_2atmpS1489 * 0x1p-24;
  return (float)_M0L6_2atmpS1488;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS546
) {
  uint64_t _M0L1uS545;
  uint64_t _M0L6_2atmpS1487;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS545 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS546);
  _M0L6_2atmpS1487 = _M0L1uS545 >> 32;
  return (uint32_t)_M0L6_2atmpS1487;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS538
) {
  uint64_t _M0L2s0S537;
  uint64_t _M0L2s1S539;
  uint64_t _M0L2s2S540;
  uint64_t _M0L2s3S541;
  uint64_t _M0L3tmpS542;
  uint64_t _M0L6_2atmpS1486;
  uint64_t _M0L3resS543;
  uint64_t _M0L1tS544;
  uint64_t _M0L6_2atmpS1476;
  uint64_t _M0L6_2atmpS1477;
  uint64_t _M0L2s2S1479;
  uint64_t _M0L6_2atmpS1478;
  uint64_t _M0L2s3S1481;
  uint64_t _M0L6_2atmpS1480;
  uint64_t _M0L2s2S1483;
  uint64_t _M0L6_2atmpS1482;
  uint64_t _M0L2s3S1485;
  uint64_t _M0L6_2atmpS1484;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S537 = _M0L1rS538->$0;
  _M0L2s1S539 = _M0L1rS538->$1;
  _M0L2s2S540 = _M0L1rS538->$2;
  _M0L2s3S541 = _M0L1rS538->$3;
  _M0L3tmpS542 = _M0L2s0S537 + _M0L2s3S541;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1486 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS542, 23);
  _M0L3resS543 = _M0L6_2atmpS1486 + _M0L2s0S537;
  _M0L1tS544 = _M0L2s1S539 << 17;
  _M0L6_2atmpS1476 = _M0L2s2S540 ^ _M0L2s0S537;
  _M0L1rS538->$2 = _M0L6_2atmpS1476;
  _M0L6_2atmpS1477 = _M0L2s3S541 ^ _M0L2s1S539;
  _M0L1rS538->$3 = _M0L6_2atmpS1477;
  _M0L2s2S1479 = _M0L1rS538->$2;
  _M0L6_2atmpS1478 = _M0L2s1S539 ^ _M0L2s2S1479;
  _M0L1rS538->$1 = _M0L6_2atmpS1478;
  _M0L2s3S1481 = _M0L1rS538->$3;
  _M0L6_2atmpS1480 = _M0L2s0S537 ^ _M0L2s3S1481;
  _M0L1rS538->$0 = _M0L6_2atmpS1480;
  _M0L2s2S1483 = _M0L1rS538->$2;
  _M0L6_2atmpS1482 = _M0L2s2S1483 ^ _M0L1tS544;
  _M0L1rS538->$2 = _M0L6_2atmpS1482;
  _M0L2s3S1485 = _M0L1rS538->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1484 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1485, 45);
  _M0L1rS538->$3 = _M0L6_2atmpS1484;
  return _M0L3resS543;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS535, int32_t _M0L1kS536) {
  uint64_t _M0L6_2atmpS1473;
  int32_t _M0L6_2atmpS1475;
  uint64_t _M0L6_2atmpS1474;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1473 = _M0L1xS535 << (_M0L1kS536 & 63);
  _M0L6_2atmpS1475 = 64 - _M0L1kS536;
  _M0L6_2atmpS1474 = _M0L1xS535 >> (_M0L6_2atmpS1475 & 63);
  return _M0L6_2atmpS1473 | _M0L6_2atmpS1474;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS528
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS1472;
  int32_t _M0L1nS527;
  struct _M0TPB8MutLocalGiE* _M0L5countS529;
  struct _M0TPB8MutLocalGfE* _M0L4prevS530;
  int32_t _M0L7_2abindS531;
  int32_t _M0L1iS532;
  int32_t _result_2058;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS1472 = _M0L1mS528->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS527 = _M0MPC15array5Array6lengthGfE(_M0L4dataS1472);
  if (_M0L1nS527 == 0) {
    return 0;
  }
  _M0L5countS529
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS529)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS529->$0 = 0;
  _M0L4prevS530
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS530)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS530->$0 = 0x0p+0f;
  _M0L7_2abindS531 = 0;
  _M0L1iS532 = _M0L7_2abindS531;
  while (1) {
    if (_M0L1iS532 < _M0L1nS527) {
      struct _M0TPB5ArrayGfE* _M0L4dataS1470 = _M0L1mS528->$2;
      float _M0L3curS533;
      float _M0L3valS1467;
      int32_t _M0L6_2atmpS1471;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS533 = _M0MPC15array5Array2atGfE(_M0L4dataS1470, _M0L1iS532);
      _M0L3valS1467 = _M0L4prevS530->$0;
      if (_M0L3valS1467 < 0x1p-1f && _M0L3curS533 >= 0x1p-1f) {
        int32_t _M0L3valS1469 = _M0L5countS529->$0;
        int32_t _M0L6_2atmpS1468 = _M0L3valS1469 + 1;
        _M0L5countS529->$0 = _M0L6_2atmpS1468;
      }
      _M0L4prevS530->$0 = _M0L3curS533;
      _M0L6_2atmpS1471 = _M0L1iS532 + 1;
      _M0L1iS532 = _M0L6_2atmpS1471;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS530);
    }
    break;
  }
  _result_2058 = _M0L5countS529->$0;
  moonbit_decref_cycle_free(_M0L5countS529);
  return _result_2058;
}

double _M0FPC14math2ln(double _M0L1xS513) {
  struct _M0TUdiE* _M0L7_2abindS514;
  double _M0L5_2af1S515;
  int32_t _M0L5_2akiS516;
  double _M0L1fS518;
  double _M0L1kS519;
  double _M0L6_2atmpS1460;
  double _M0L1sS520;
  double _M0L2s2S521;
  double _M0L2s4S522;
  double _M0L6_2atmpS1459;
  double _M0L6_2atmpS1458;
  double _M0L6_2atmpS1457;
  double _M0L6_2atmpS1456;
  double _M0L6_2atmpS1455;
  double _M0L6_2atmpS1454;
  double _M0L2t1S523;
  double _M0L6_2atmpS1453;
  double _M0L6_2atmpS1452;
  double _M0L6_2atmpS1451;
  double _M0L6_2atmpS1450;
  double _M0L2t2S524;
  double _M0L1rS525;
  double _M0L6_2atmpS1449;
  double _M0L4hfsqS526;
  double _M0L6_2atmpS1442;
  double _M0L6_2atmpS1448;
  double _M0L6_2atmpS1446;
  double _M0L6_2atmpS1447;
  double _M0L6_2atmpS1445;
  double _M0L6_2atmpS1444;
  double _M0L6_2atmpS1443;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS513 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS513)
      || _M0MPC16double6Double7is__inf(_M0L1xS513)
    ) {
      return _M0L1xS513;
    } else if (_M0L1xS513 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS514 = _M0FPC14math5frexp(_M0L1xS513);
  _M0L5_2af1S515 = _M0L7_2abindS514->$0;
  _M0L5_2akiS516 = _M0L7_2abindS514->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS514);
  if (_M0L5_2af1S515 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1464 = _M0L5_2af1S515 * 0x1p+1;
    double _M0L6_2atmpS1461 = _M0L6_2atmpS1464 - 0x1p+0;
    int32_t _M0L6_2atmpS1463 = _M0L5_2akiS516 - 1;
    double _M0L6_2atmpS1462 = (double)_M0L6_2atmpS1463;
    _M0L1fS518 = _M0L6_2atmpS1461;
    _M0L1kS519 = _M0L6_2atmpS1462;
    goto join_517;
  } else {
    double _M0L6_2atmpS1465 = _M0L5_2af1S515 - 0x1p+0;
    double _M0L6_2atmpS1466 = (double)_M0L5_2akiS516;
    _M0L1fS518 = _M0L6_2atmpS1465;
    _M0L1kS519 = _M0L6_2atmpS1466;
    goto join_517;
  }
  join_517:;
  _M0L6_2atmpS1460 = 0x1p+1 + _M0L1fS518;
  _M0L1sS520 = _M0L1fS518 / _M0L6_2atmpS1460;
  _M0L2s2S521 = _M0L1sS520 * _M0L1sS520;
  _M0L2s4S522 = _M0L2s2S521 * _M0L2s2S521;
  _M0L6_2atmpS1459 = _M0L2s4S522 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1458 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1459;
  _M0L6_2atmpS1457 = _M0L2s4S522 * _M0L6_2atmpS1458;
  _M0L6_2atmpS1456 = 0x1.2492494229359p-2 + _M0L6_2atmpS1457;
  _M0L6_2atmpS1455 = _M0L2s4S522 * _M0L6_2atmpS1456;
  _M0L6_2atmpS1454 = 0x1.5555555555593p-1 + _M0L6_2atmpS1455;
  _M0L2t1S523 = _M0L2s2S521 * _M0L6_2atmpS1454;
  _M0L6_2atmpS1453 = _M0L2s4S522 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1452 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1453;
  _M0L6_2atmpS1451 = _M0L2s4S522 * _M0L6_2atmpS1452;
  _M0L6_2atmpS1450 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1451;
  _M0L2t2S524 = _M0L2s4S522 * _M0L6_2atmpS1450;
  _M0L1rS525 = _M0L2t1S523 + _M0L2t2S524;
  _M0L6_2atmpS1449 = 0x1p-1 * _M0L1fS518;
  _M0L4hfsqS526 = _M0L6_2atmpS1449 * _M0L1fS518;
  _M0L6_2atmpS1442 = _M0L1kS519 * 0x1.62e42feep-1;
  _M0L6_2atmpS1448 = _M0L4hfsqS526 + _M0L1rS525;
  _M0L6_2atmpS1446 = _M0L1sS520 * _M0L6_2atmpS1448;
  _M0L6_2atmpS1447 = _M0L1kS519 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1445 = _M0L6_2atmpS1446 + _M0L6_2atmpS1447;
  _M0L6_2atmpS1444 = _M0L4hfsqS526 - _M0L6_2atmpS1445;
  _M0L6_2atmpS1443 = _M0L6_2atmpS1444 - _M0L1fS518;
  return _M0L6_2atmpS1442 - _M0L6_2atmpS1443;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS506) {
  struct _M0TUdiE* _M0L7_2abindS507;
  double _M0L10_2anorm__fS508;
  int32_t _M0L6_2aexpS509;
  uint64_t _M0L1uS510;
  uint64_t _M0L6_2atmpS1441;
  uint64_t _M0L6_2atmpS1440;
  int32_t _M0L6_2atmpS1439;
  int32_t _M0L6_2atmpS1438;
  int32_t _M0L3expS511;
  uint64_t _M0L6_2atmpS1437;
  uint64_t _M0L6_2atmpS1436;
  uint64_t _M0L6_2atmpS1435;
  double _M0L4fracS512;
  struct _M0TUdiE* _block_2061;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS506 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS506)
    || _M0MPC16double6Double7is__nan(_M0L1fS506)
  ) {
    struct _M0TUdiE* _block_2060 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2060)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2060->$0 = _M0L1fS506;
    _block_2060->$1 = 0;
    return _block_2060;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS507 = _M0FPC14math9normalize(_M0L1fS506);
  _M0L10_2anorm__fS508 = _M0L7_2abindS507->$0;
  _M0L6_2aexpS509 = _M0L7_2abindS507->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS507);
  _M0L1uS510 = *(int64_t*)&_M0L10_2anorm__fS508;
  _M0L6_2atmpS1441 = _M0L1uS510 >> 52;
  _M0L6_2atmpS1440 = _M0L6_2atmpS1441 & 2047ull;
  _M0L6_2atmpS1439 = (int32_t)_M0L6_2atmpS1440;
  _M0L6_2atmpS1438 = _M0L6_2aexpS509 + _M0L6_2atmpS1439;
  _M0L3expS511 = _M0L6_2atmpS1438 - 1022;
  _M0L6_2atmpS1437 = ~9218868437227405312ull;
  _M0L6_2atmpS1436 = _M0L1uS510 & _M0L6_2atmpS1437;
  _M0L6_2atmpS1435 = _M0L6_2atmpS1436 | 4602678819172646912ull;
  _M0L4fracS512 = *(double*)&_M0L6_2atmpS1435;
  _block_2061 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2061)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2061->$0 = _M0L4fracS512;
  _block_2061->$1 = _M0L3expS511;
  return _block_2061;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS505) {
  double _M0L6_2atmpS1432;
  struct _M0TUdiE* _block_2063;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1432 = fabs(_M0L1fS505);
  if (_M0L6_2atmpS1432 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1434 = (double)4503599627370496ll;
    double _M0L6_2atmpS1433 = _M0L1fS505 * _M0L6_2atmpS1434;
    struct _M0TUdiE* _block_2062 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2062)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2062->$0 = _M0L6_2atmpS1433;
    _block_2062->$1 = -52;
    return _block_2062;
  }
  _block_2063 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2063)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2063->$0 = _M0L1fS505;
  _block_2063->$1 = 0;
  return _block_2063;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS504) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS504 != _M0L4selfS504) {
    return 0;
  } else if (_M0L4selfS504 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS504 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS504;
  }
}

int32_t _M0MPC15array5Array5clearGfE(struct _M0TPB5ArrayGfE* _M0L4selfS503) {
  #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 579 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(_M0L4selfS503, 0);
  return 0;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS484,
  float _M0L4elemS486
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS483;
  int32_t _M0L1iS485;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS483 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS484);
  _M0L1iS485 = 0;
  while (1) {
    if (_M0L1iS485 < _M0L3lenS484) {
      float* _M0L3bufS1424 = _M0L3arrS483->$0;
      int32_t _M0L6_2atmpS1425;
      _M0L3bufS1424[_M0L1iS485] = _M0L4elemS486;
      _M0L6_2atmpS1425 = _M0L1iS485 + 1;
      _M0L1iS485 = _M0L6_2atmpS1425;
      continue;
    }
    break;
  }
  return _M0L3arrS483;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS489,
  int32_t _M0L4elemS491
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS488;
  int32_t _M0L1iS490;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS488 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS489);
  _M0L1iS490 = 0;
  while (1) {
    if (_M0L1iS490 < _M0L3lenS489) {
      uint8_t* _M0L3bufS1426 = _M0L3arrS488->$0;
      int32_t _M0L6_2atmpS1427;
      _M0L3bufS1426[_M0L1iS490] = _M0L4elemS491;
      _M0L6_2atmpS1427 = _M0L1iS490 + 1;
      _M0L1iS490 = _M0L6_2atmpS1427;
      continue;
    }
    break;
  }
  return _M0L3arrS488;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS494,
  int32_t _M0L4elemS496
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS493;
  int32_t _M0L1iS495;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS493 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS494);
  _M0L1iS495 = 0;
  while (1) {
    if (_M0L1iS495 < _M0L3lenS494) {
      int32_t* _M0L3bufS1428 = _M0L3arrS493->$0;
      int32_t _M0L6_2atmpS1429;
      _M0L3bufS1428[_M0L1iS495] = _M0L4elemS496;
      _M0L6_2atmpS1429 = _M0L1iS495 + 1;
      _M0L1iS495 = _M0L6_2atmpS1429;
      continue;
    }
    break;
  }
  return _M0L3arrS493;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS499,
  struct _M0TPB5ArrayGfE* _M0L4elemS501
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS498;
  int32_t _M0L1iS500;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS498
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS499);
  _M0L1iS500 = 0;
  while (1) {
    if (_M0L1iS500 < _M0L3lenS499) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1430 = _M0L3arrS498->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS1951 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1430[_M0L1iS500];
      int32_t _M0L6_2atmpS1431;
      moonbit_incref_cycle_free(_M0L4elemS501);
      if (_M0L6_2aoldS1951) {
        moonbit_decref_cycle_free(_M0L6_2aoldS1951);
      }
      _M0L3bufS1430[_M0L1iS500] = _M0L4elemS501;
      _M0L6_2atmpS1431 = _M0L1iS500 + 1;
      _M0L1iS500 = _M0L6_2atmpS1431;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS501);
    }
    break;
  }
  return _M0L3arrS498;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS468,
  int32_t _M0L5indexS469,
  float _M0L5valueS470
) {
  int32_t _M0L3lenS467;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS467 = _M0L4selfS468->$1;
  if (_M0L5indexS469 >= 0 && _M0L5indexS469 < _M0L3lenS467) {
    float* _M0L6_2atmpS1420;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1420 = _M0MPC15array5Array6bufferGfE(_M0L4selfS468);
    _M0L6_2atmpS1420[_M0L5indexS469] = _M0L5valueS470;
    moonbit_decref_cycle_free(_M0L6_2atmpS1420);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS472,
  int32_t _M0L5indexS473,
  int32_t _M0L5valueS474
) {
  int32_t _M0L3lenS471;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS471 = _M0L4selfS472->$1;
  if (_M0L5indexS473 >= 0 && _M0L5indexS473 < _M0L3lenS471) {
    int32_t* _M0L6_2atmpS1421;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1421 = _M0MPC15array5Array6bufferGiE(_M0L4selfS472);
    _M0L6_2atmpS1421[_M0L5indexS473] = _M0L5valueS474;
    moonbit_decref_cycle_free(_M0L6_2atmpS1421);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS476,
  int32_t _M0L5indexS477,
  int32_t _M0L5valueS478
) {
  int32_t _M0L3lenS475;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS475 = _M0L4selfS476->$1;
  if (_M0L5indexS477 >= 0 && _M0L5indexS477 < _M0L3lenS475) {
    uint8_t* _M0L6_2atmpS1422;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1422 = _M0MPC15array5Array6bufferGbE(_M0L4selfS476);
    _M0L6_2atmpS1422[_M0L5indexS477] = _M0L5valueS478;
    moonbit_decref_cycle_free(_M0L6_2atmpS1422);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS480,
  int32_t _M0L5indexS481,
  struct _M0TPB5ArrayGfE* _M0L5valueS482
) {
  int32_t _M0L3lenS479;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS479 = _M0L4selfS480->$1;
  if (_M0L5indexS481 >= 0 && _M0L5indexS481 < _M0L3lenS479) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1423;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS1952;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1423
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS480);
    _M0L6_2aoldS1952
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1423[_M0L5indexS481];
    if (_M0L6_2aoldS1952) {
      moonbit_decref_cycle_free(_M0L6_2aoldS1952);
    }
    _M0L6_2atmpS1423[_M0L5indexS481] = _M0L5valueS482;
    moonbit_decref_cycle_free(_M0L6_2atmpS1423);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS482);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS460) {
  int32_t _M0L3lenS459;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS459 = _M0L4selfS460->$1;
  if (_M0L3lenS459 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS461 = _M0L3lenS459 - 1;
    float* _M0L3bufS1418 = _M0L4selfS460->$0;
    float _M0L1vS462 = (float)_M0L3bufS1418[_M0L5indexS461];
    void* _block_2068;
    _M0L4selfS460->$1 = _M0L5indexS461;
    _block_2068
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_2068)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_2068)->$0 = _M0L1vS462;
    return _block_2068;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS464) {
  int32_t _M0L3lenS463;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS463 = _M0L4selfS464->$1;
  if (_M0L3lenS463 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS465 = _M0L3lenS463 - 1;
    int32_t* _M0L3bufS1419 = _M0L4selfS464->$0;
    int32_t _M0L1vS466 = (int32_t)_M0L3bufS1419[_M0L5indexS465];
    _M0L4selfS464->$1 = _M0L5indexS465;
    return (int64_t)_M0L1vS466;
  }
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS442,
  int32_t _M0L5indexS443
) {
  int32_t _M0L3lenS441;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS441 = _M0L4selfS442->$1;
  if (_M0L5indexS443 >= 0 && _M0L5indexS443 < _M0L3lenS441) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS1412;
    struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS1953;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1412
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(_M0L4selfS442);
    _M0L6_2atmpS1953
    = (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L6_2atmpS1412[
        _M0L5indexS443
      ];
    if (_M0L6_2atmpS1953) {
      moonbit_incref_cycle_free(_M0L6_2atmpS1953);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1412);
    return _M0L6_2atmpS1953;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS445,
  int32_t _M0L5indexS446
) {
  int32_t _M0L3lenS444;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS444 = _M0L4selfS445->$1;
  if (_M0L5indexS446 >= 0 && _M0L5indexS446 < _M0L3lenS444) {
    float* _M0L6_2atmpS1413;
    float _result_2069;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1413 = _M0MPC15array5Array6bufferGfE(_M0L4selfS445);
    _result_2069 = (float)_M0L6_2atmpS1413[_M0L5indexS446];
    moonbit_decref_cycle_free(_M0L6_2atmpS1413);
    return _result_2069;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS448,
  int32_t _M0L5indexS449
) {
  int32_t _M0L3lenS447;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS447 = _M0L4selfS448->$1;
  if (_M0L5indexS449 >= 0 && _M0L5indexS449 < _M0L3lenS447) {
    moonbit_string_t* _M0L6_2atmpS1414;
    moonbit_string_t _M0L6_2atmpS1954;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1414 = _M0MPC15array5Array6bufferGsE(_M0L4selfS448);
    _M0L6_2atmpS1954 = (moonbit_string_t)_M0L6_2atmpS1414[_M0L5indexS449];
    moonbit_incref_cycle_free(_M0L6_2atmpS1954);
    moonbit_decref_cycle_free(_M0L6_2atmpS1414);
    return _M0L6_2atmpS1954;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS451,
  int32_t _M0L5indexS452
) {
  int32_t _M0L3lenS450;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS450 = _M0L4selfS451->$1;
  if (_M0L5indexS452 >= 0 && _M0L5indexS452 < _M0L3lenS450) {
    uint8_t* _M0L6_2atmpS1415;
    int32_t _result_2070;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1415 = _M0MPC15array5Array6bufferGbE(_M0L4selfS451);
    _result_2070 = (int32_t)_M0L6_2atmpS1415[_M0L5indexS452];
    moonbit_decref_cycle_free(_M0L6_2atmpS1415);
    return _result_2070;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS454,
  int32_t _M0L5indexS455
) {
  int32_t _M0L3lenS453;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS453 = _M0L4selfS454->$1;
  if (_M0L5indexS455 >= 0 && _M0L5indexS455 < _M0L3lenS453) {
    int32_t* _M0L6_2atmpS1416;
    int32_t _result_2071;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1416 = _M0MPC15array5Array6bufferGiE(_M0L4selfS454);
    _result_2071 = (int32_t)_M0L6_2atmpS1416[_M0L5indexS455];
    moonbit_decref_cycle_free(_M0L6_2atmpS1416);
    return _result_2071;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS457,
  int32_t _M0L5indexS458
) {
  int32_t _M0L3lenS456;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS456 = _M0L4selfS457->$1;
  if (_M0L5indexS458 >= 0 && _M0L5indexS458 < _M0L3lenS456) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1417;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS1955;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1417
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS457);
    _M0L6_2atmpS1955
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1417[_M0L5indexS458];
    if (_M0L6_2atmpS1955) {
      moonbit_incref_cycle_free(_M0L6_2atmpS1955);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1417);
    return _M0L6_2atmpS1955;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS440,
  int32_t _M0L8new__lenS439
) {
  int32_t _M0L3lenS1411;
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1411 = _M0L4selfS440->$1;
  if (_M0L8new__lenS439 <= _M0L3lenS1411) {
    _M0L4selfS440->$1 = _M0L8new__lenS439;
  } else {
    #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS438) {
  moonbit_string_t _M0L6_2atmpS1410;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1410 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS438);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1410);
  moonbit_decref_cycle_free(_M0L6_2atmpS1410);
  return 0;
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS437) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS437 > _M0FPB18double__max__value
         || _M0L4selfS437 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS436) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS436 != _M0L4selfS436;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS432
) {
  float* _M0L6_2atmpS1406;
  struct _M0TPB5ArrayGfE* _block_2072;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1406 = (float*)moonbit_make_float_array_raw(_M0L3lenS432);
  _block_2072
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2072)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2072->$0 = _M0L6_2atmpS1406;
  _block_2072->$1 = _M0L3lenS432;
  return _block_2072;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS433
) {
  uint8_t* _M0L6_2atmpS1407;
  struct _M0TPB5ArrayGbE* _block_2073;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1407 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS433);
  _block_2073
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2073)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _block_2073->$0 = _M0L6_2atmpS1407;
  _block_2073->$1 = _M0L3lenS433;
  return _block_2073;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS434
) {
  int32_t* _M0L6_2atmpS1408;
  struct _M0TPB5ArrayGiE* _block_2074;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1408 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS434);
  _block_2074
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2074)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2074->$0 = _M0L6_2atmpS1408;
  _block_2074->$1 = _M0L3lenS434;
  return _block_2074;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS435
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1409;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2075;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1409
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS435, 0);
  _block_2075
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2075)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 72, 0);
  _block_2075->$0 = _M0L6_2atmpS1409;
  _block_2075->$1 = _M0L3lenS435;
  return _block_2075;
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS431) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS431, 10);
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS419,
  float _M0L5valueS421
) {
  int32_t _M0L3lenS1378;
  float* _M0L6_2atmpS1380;
  int32_t _M0L6_2atmpS1379;
  int32_t _M0L6lengthS420;
  float* _M0L3bufS1383;
  int32_t _M0L6_2atmpS1384;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1378 = _M0L4selfS419->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1380 = _M0MPC15array5Array6bufferGfE(_M0L4selfS419);
  _M0L6_2atmpS1379 = Moonbit_array_length(_M0L6_2atmpS1380);
  moonbit_decref_cycle_free(_M0L6_2atmpS1380);
  if (_M0L3lenS1378 == _M0L6_2atmpS1379) {
    int32_t _M0L3lenS1382 = _M0L4selfS419->$1;
    int32_t _M0L6_2atmpS1381 = _M0L3lenS1382 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS419, _M0L6_2atmpS1381);
  }
  _M0L6lengthS420 = _M0L4selfS419->$1;
  _M0L3bufS1383 = _M0L4selfS419->$0;
  _M0L3bufS1383[_M0L6lengthS420] = _M0L5valueS421;
  _M0L6_2atmpS1384 = _M0L6lengthS420 + 1;
  _M0L4selfS419->$1 = _M0L6_2atmpS1384;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS422,
  moonbit_string_t _M0L5valueS424
) {
  int32_t _M0L3lenS1385;
  moonbit_string_t* _M0L6_2atmpS1387;
  int32_t _M0L6_2atmpS1386;
  int32_t _M0L6lengthS423;
  moonbit_string_t* _M0L3bufS1390;
  moonbit_string_t _M0L6_2aoldS1956;
  int32_t _M0L6_2atmpS1391;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1385 = _M0L4selfS422->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1387 = _M0MPC15array5Array6bufferGsE(_M0L4selfS422);
  _M0L6_2atmpS1386 = Moonbit_array_length(_M0L6_2atmpS1387);
  moonbit_decref_cycle_free(_M0L6_2atmpS1387);
  if (_M0L3lenS1385 == _M0L6_2atmpS1386) {
    int32_t _M0L3lenS1389 = _M0L4selfS422->$1;
    int32_t _M0L6_2atmpS1388 = _M0L3lenS1389 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS422, _M0L6_2atmpS1388);
  }
  _M0L6lengthS423 = _M0L4selfS422->$1;
  _M0L3bufS1390 = _M0L4selfS422->$0;
  _M0L6_2aoldS1956 = (moonbit_string_t)_M0L3bufS1390[_M0L6lengthS423];
  moonbit_decref_cycle_free(_M0L6_2aoldS1956);
  _M0L3bufS1390[_M0L6lengthS423] = _M0L5valueS424;
  _M0L6_2atmpS1391 = _M0L6lengthS423 + 1;
  _M0L4selfS422->$1 = _M0L6_2atmpS1391;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS425,
  struct _M0TUsiE* _M0L5valueS427
) {
  int32_t _M0L3lenS1392;
  struct _M0TUsiE** _M0L6_2atmpS1394;
  int32_t _M0L6_2atmpS1393;
  int32_t _M0L6lengthS426;
  struct _M0TUsiE** _M0L3bufS1397;
  struct _M0TUsiE* _M0L6_2aoldS1957;
  int32_t _M0L6_2atmpS1398;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1392 = _M0L4selfS425->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1394 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS425);
  _M0L6_2atmpS1393 = Moonbit_array_length(_M0L6_2atmpS1394);
  moonbit_decref_cycle_free(_M0L6_2atmpS1394);
  if (_M0L3lenS1392 == _M0L6_2atmpS1393) {
    int32_t _M0L3lenS1396 = _M0L4selfS425->$1;
    int32_t _M0L6_2atmpS1395 = _M0L3lenS1396 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS425, _M0L6_2atmpS1395);
  }
  _M0L6lengthS426 = _M0L4selfS425->$1;
  _M0L3bufS1397 = _M0L4selfS425->$0;
  _M0L6_2aoldS1957 = (struct _M0TUsiE*)_M0L3bufS1397[_M0L6lengthS426];
  if (_M0L6_2aoldS1957) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1957);
  }
  _M0L3bufS1397[_M0L6lengthS426] = _M0L5valueS427;
  _M0L6_2atmpS1398 = _M0L6lengthS426 + 1;
  _M0L4selfS425->$1 = _M0L6_2atmpS1398;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS428,
  int32_t _M0L5valueS430
) {
  int32_t _M0L3lenS1399;
  int32_t* _M0L6_2atmpS1401;
  int32_t _M0L6_2atmpS1400;
  int32_t _M0L6lengthS429;
  int32_t* _M0L3bufS1404;
  int32_t _M0L6_2atmpS1405;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1399 = _M0L4selfS428->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1401 = _M0MPC15array5Array6bufferGiE(_M0L4selfS428);
  _M0L6_2atmpS1400 = Moonbit_array_length(_M0L6_2atmpS1401);
  moonbit_decref_cycle_free(_M0L6_2atmpS1401);
  if (_M0L3lenS1399 == _M0L6_2atmpS1400) {
    int32_t _M0L3lenS1403 = _M0L4selfS428->$1;
    int32_t _M0L6_2atmpS1402 = _M0L3lenS1403 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS428, _M0L6_2atmpS1402);
  }
  _M0L6lengthS429 = _M0L4selfS428->$1;
  _M0L3bufS1404 = _M0L4selfS428->$0;
  _M0L3bufS1404[_M0L6lengthS429] = _M0L5valueS430;
  _M0L6_2atmpS1405 = _M0L6lengthS429 + 1;
  _M0L4selfS428->$1 = _M0L6_2atmpS1405;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS404,
  int32_t _M0L8requiredS406
) {
  int32_t _M0L8old__capS403;
  int32_t _M0L3lenS1374;
  int32_t _M0L8new__capS405;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS403 = _M0MPC15array5Array8capacityGfE(_M0L4selfS404);
  _M0L3lenS1374 = _M0L4selfS404->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS405
  = _M0FPB23array__growth__capacity(_M0L8old__capS403, _M0L3lenS1374, _M0L8requiredS406);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS404, _M0L8new__capS405);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS408,
  int32_t _M0L8requiredS410
) {
  int32_t _M0L8old__capS407;
  int32_t _M0L3lenS1375;
  int32_t _M0L8new__capS409;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS407 = _M0MPC15array5Array8capacityGsE(_M0L4selfS408);
  _M0L3lenS1375 = _M0L4selfS408->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS409
  = _M0FPB23array__growth__capacity(_M0L8old__capS407, _M0L3lenS1375, _M0L8requiredS410);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS408, _M0L8new__capS409);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS412,
  int32_t _M0L8requiredS414
) {
  int32_t _M0L8old__capS411;
  int32_t _M0L3lenS1376;
  int32_t _M0L8new__capS413;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS411 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS412);
  _M0L3lenS1376 = _M0L4selfS412->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS413
  = _M0FPB23array__growth__capacity(_M0L8old__capS411, _M0L3lenS1376, _M0L8requiredS414);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS412, _M0L8new__capS413);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS416,
  int32_t _M0L8requiredS418
) {
  int32_t _M0L8old__capS415;
  int32_t _M0L3lenS1377;
  int32_t _M0L8new__capS417;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS415 = _M0MPC15array5Array8capacityGiE(_M0L4selfS416);
  _M0L3lenS1377 = _M0L4selfS416->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS417
  = _M0FPB23array__growth__capacity(_M0L8old__capS415, _M0L3lenS1377, _M0L8requiredS418);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS416, _M0L8new__capS417);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS380,
  int32_t _M0L13new__capacityS383
) {
  float* _M0L8old__bufS379;
  int32_t _M0L3lenS381;
  int32_t _M0L9copy__lenS382;
  float* _M0L8new__bufS384;
  float* _M0L6_2aoldS1958;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS379 = _M0L4selfS380->$0;
  _M0L3lenS381 = _M0L4selfS380->$1;
  if (_M0L3lenS381 < _M0L13new__capacityS383) {
    _M0L9copy__lenS382 = _M0L3lenS381;
  } else {
    _M0L9copy__lenS382 = _M0L13new__capacityS383;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS379);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS384
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS379, _M0L13new__capacityS383, _M0L9copy__lenS382, 0, 0);
  _M0L6_2aoldS1958 = _M0L4selfS380->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1958);
  _M0L4selfS380->$0 = _M0L8new__bufS384;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS386,
  int32_t _M0L13new__capacityS389
) {
  moonbit_string_t* _M0L8old__bufS385;
  int32_t _M0L3lenS387;
  int32_t _M0L9copy__lenS388;
  moonbit_string_t* _M0L8new__bufS390;
  moonbit_string_t* _M0L6_2aoldS1959;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS385 = _M0L4selfS386->$0;
  _M0L3lenS387 = _M0L4selfS386->$1;
  if (_M0L3lenS387 < _M0L13new__capacityS389) {
    _M0L9copy__lenS388 = _M0L3lenS387;
  } else {
    _M0L9copy__lenS388 = _M0L13new__capacityS389;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS385);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS390
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS385, _M0L13new__capacityS389, _M0L9copy__lenS388, 0, 0);
  _M0L6_2aoldS1959 = _M0L4selfS386->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1959);
  _M0L4selfS386->$0 = _M0L8new__bufS390;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS392,
  int32_t _M0L13new__capacityS395
) {
  struct _M0TUsiE** _M0L8old__bufS391;
  int32_t _M0L3lenS393;
  int32_t _M0L9copy__lenS394;
  struct _M0TUsiE** _M0L8new__bufS396;
  struct _M0TUsiE** _M0L6_2aoldS1960;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS391 = _M0L4selfS392->$0;
  _M0L3lenS393 = _M0L4selfS392->$1;
  if (_M0L3lenS393 < _M0L13new__capacityS395) {
    _M0L9copy__lenS394 = _M0L3lenS393;
  } else {
    _M0L9copy__lenS394 = _M0L13new__capacityS395;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS391);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS396
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS391, _M0L13new__capacityS395, _M0L9copy__lenS394, 0, 0);
  _M0L6_2aoldS1960 = _M0L4selfS392->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1960);
  _M0L4selfS392->$0 = _M0L8new__bufS396;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS398,
  int32_t _M0L13new__capacityS401
) {
  int32_t* _M0L8old__bufS397;
  int32_t _M0L3lenS399;
  int32_t _M0L9copy__lenS400;
  int32_t* _M0L8new__bufS402;
  int32_t* _M0L6_2aoldS1961;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS397 = _M0L4selfS398->$0;
  _M0L3lenS399 = _M0L4selfS398->$1;
  if (_M0L3lenS399 < _M0L13new__capacityS401) {
    _M0L9copy__lenS400 = _M0L3lenS399;
  } else {
    _M0L9copy__lenS400 = _M0L13new__capacityS401;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS397);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS402
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS397, _M0L13new__capacityS401, _M0L9copy__lenS400, 0, 0);
  _M0L6_2aoldS1961 = _M0L4selfS398->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1961);
  _M0L4selfS398->$0 = _M0L8new__bufS402;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS375
) {
  float* _M0L6_2atmpS1370;
  int32_t _result_2076;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1370 = _M0MPC15array5Array6bufferGfE(_M0L4selfS375);
  _result_2076 = Moonbit_array_length(_M0L6_2atmpS1370);
  moonbit_decref_cycle_free(_M0L6_2atmpS1370);
  return _result_2076;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS376
) {
  moonbit_string_t* _M0L6_2atmpS1371;
  int32_t _result_2077;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1371 = _M0MPC15array5Array6bufferGsE(_M0L4selfS376);
  _result_2077 = Moonbit_array_length(_M0L6_2atmpS1371);
  moonbit_decref_cycle_free(_M0L6_2atmpS1371);
  return _result_2077;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS377
) {
  struct _M0TUsiE** _M0L6_2atmpS1372;
  int32_t _result_2078;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1372 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS377);
  _result_2078 = Moonbit_array_length(_M0L6_2atmpS1372);
  moonbit_decref_cycle_free(_M0L6_2atmpS1372);
  return _result_2078;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS378
) {
  int32_t* _M0L6_2atmpS1373;
  int32_t _result_2079;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1373 = _M0MPC15array5Array6bufferGiE(_M0L4selfS378);
  _result_2079 = Moonbit_array_length(_M0L6_2atmpS1373);
  moonbit_decref_cycle_free(_M0L6_2atmpS1373);
  return _result_2079;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS371,
  int32_t _M0L3lenS369,
  int32_t _M0L8requiredS368
) {
  int32_t _M0L5startS370;
  int32_t _M0L5spaceS372;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS368 < _M0L3lenS369) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  if (_M0L7currentS371 == 0) {
    _M0L5startS370 = 8;
  } else {
    _M0L5startS370 = _M0L7currentS371;
  }
  _M0L5spaceS372 = _M0L5startS370;
  while (1) {
    if (_M0L5spaceS372 < _M0L8requiredS368) {
      int32_t _M0L4nextS373 = _M0L5spaceS372 * 2;
      if (_M0L4nextS373 <= _M0L5spaceS372) {
        return _M0L8requiredS368;
      }
      _M0L5spaceS372 = _M0L4nextS373;
      continue;
    } else {
      return _M0L5spaceS372;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS366) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS366->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS367) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS367->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS359) {
  float* _M0L8_2afieldS1962;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1962 = _M0L4selfS359->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1962);
  return _M0L8_2afieldS1962;
}

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS360
) {
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L8_2afieldS1963;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1963 = _M0L4selfS360->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1963);
  return _M0L8_2afieldS1963;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS361
) {
  moonbit_string_t* _M0L8_2afieldS1964;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1964 = _M0L4selfS361->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1964);
  return _M0L8_2afieldS1964;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS362
) {
  struct _M0TUsiE** _M0L8_2afieldS1965;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1965 = _M0L4selfS362->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1965);
  return _M0L8_2afieldS1965;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS363) {
  uint8_t* _M0L8_2afieldS1966;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1966 = _M0L4selfS363->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1966);
  return _M0L8_2afieldS1966;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS364) {
  int32_t* _M0L8_2afieldS1967;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1967 = _M0L4selfS364->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1967);
  return _M0L8_2afieldS1967;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS365
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS1968;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1968 = _M0L4selfS365->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1968);
  return _M0L8_2afieldS1968;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS358
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS358);
  return _M0L4selfS358;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS357,
  struct _M0TPC16string10StringView _M0L3strS355
) {
  int32_t _M0L3endS1368;
  int32_t _M0L5startS1369;
  int32_t _M0L8str__lenS354;
  int32_t _M0L3lenS1367;
  int32_t _M0L8requiredS356;
  uint16_t* _M0L4dataS1360;
  int32_t _M0L6_2atmpS1359;
  int32_t _if__result_2081;
  uint16_t* _M0L4dataS1361;
  int32_t _M0L3lenS1362;
  moonbit_string_t _M0L6_2atmpS1363;
  int32_t _M0L6_2atmpS1364;
  int32_t _M0L3lenS1366;
  int32_t _M0L6_2atmpS1365;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1368 = _M0L3strS355.$2;
  _M0L5startS1369 = _M0L3strS355.$1;
  _M0L8str__lenS354 = _M0L3endS1368 - _M0L5startS1369;
  if (_M0L8str__lenS354 == 0) {
    return 0;
  }
  _M0L3lenS1367 = _M0L4selfS357->$1;
  _M0L8requiredS356 = _M0L3lenS1367 + _M0L8str__lenS354;
  _M0L4dataS1360 = _M0L4selfS357->$0;
  _M0L6_2atmpS1359 = Moonbit_array_length(_M0L4dataS1360);
  if (_M0L8requiredS356 > _M0L6_2atmpS1359) {
    _if__result_2081 = 1;
  } else {
    int32_t _M0L3lenS1358 = _M0L4selfS357->$1;
    _if__result_2081 = _M0L8requiredS356 < _M0L3lenS1358;
  }
  if (_if__result_2081) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS357, _M0L8requiredS356);
  }
  _M0L4dataS1361 = _M0L4selfS357->$0;
  _M0L3lenS1362 = _M0L4selfS357->$1;
  moonbit_incref_cycle_free(_M0L4dataS1361);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1363 = _M0MPC16string10StringView4data(_M0L3strS355);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1364 = _M0MPC16string10StringView13start__offset(_M0L3strS355);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1361, _M0L3lenS1362, _M0L6_2atmpS1363, _M0L6_2atmpS1364, _M0L8str__lenS354);
  moonbit_decref_cycle_free(_M0L4dataS1361);
  moonbit_decref_cycle_free(_M0L6_2atmpS1363);
  _M0L3lenS1366 = _M0L4selfS357->$1;
  _M0L6_2atmpS1365 = _M0L3lenS1366 + _M0L8str__lenS354;
  _M0L4selfS357->$1 = _M0L6_2atmpS1365;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS351,
  int32_t _M0L5startS349,
  int32_t _M0L3endS350
) {
  int32_t _if__result_2082;
  int32_t _M0L3lenS352;
  int32_t _M0L6_2atmpS1357;
  moonbit_bytes_t _M0L5bytesS353;
  moonbit_bytes_t _M0L6_2atmpS1356;
  moonbit_string_t _result_2083;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS349 == 0) {
    int32_t _M0L6_2atmpS1355 = Moonbit_array_length(_M0L3strS351);
    _if__result_2082 = _M0L3endS350 == _M0L6_2atmpS1355;
  } else {
    _if__result_2082 = 0;
  }
  if (_if__result_2082) {
    moonbit_incref_cycle_free(_M0L3strS351);
    return _M0L3strS351;
  }
  _M0L3lenS352 = _M0L3endS350 - _M0L5startS349;
  _M0L6_2atmpS1357 = _M0L3lenS352 * 2;
  _M0L5bytesS353 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1357, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS353, 0, _M0L3strS351, _M0L5startS349, _M0L3lenS352);
  _M0L6_2atmpS1356 = _M0L5bytesS353;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2083
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1356, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1356);
  return _result_2083;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS344,
  int32_t _M0L6offsetS348,
  int64_t _M0L6lengthS346
) {
  int32_t _M0L3lenS343;
  int32_t _M0L6lengthS345;
  int32_t _if__result_2084;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS343 = Moonbit_array_length(_M0L4selfS344);
  if (_M0L6lengthS346 == 4294967296ll) {
    _M0L6lengthS345 = _M0L3lenS343 - _M0L6offsetS348;
  } else {
    int64_t _M0L7_2aSomeS347 = _M0L6lengthS346;
    _M0L6lengthS345 = (int32_t)_M0L7_2aSomeS347;
  }
  if (_M0L6offsetS348 >= 0) {
    if (_M0L6lengthS345 >= 0) {
      int32_t _M0L6_2atmpS1354 = _M0L6offsetS348 + _M0L6lengthS345;
      _if__result_2084 = _M0L6_2atmpS1354 <= _M0L3lenS343;
    } else {
      _if__result_2084 = 0;
    }
  } else {
    _if__result_2084 = 0;
  }
  if (_if__result_2084) {
    moonbit_incref_cycle_free(_M0L4selfS344);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS344, _M0L6offsetS348, _M0L6lengthS345);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS335,
  int32_t _M0L13bytes__offsetS330,
  moonbit_string_t _M0L3strS337,
  int32_t _M0L11str__offsetS333,
  int32_t _M0L6lengthS331
) {
  int32_t _M0L6_2atmpS1353;
  int32_t _M0L6_2atmpS1352;
  int32_t _M0L2e1S329;
  int32_t _M0L6_2atmpS1351;
  int32_t _M0L2e2S332;
  int32_t _M0L4len1S334;
  int32_t _M0L4len2S336;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1353 = _M0L6lengthS331 * 2;
  _M0L6_2atmpS1352 = _M0L13bytes__offsetS330 + _M0L6_2atmpS1353;
  _M0L2e1S329 = _M0L6_2atmpS1352 - 1;
  _M0L6_2atmpS1351 = _M0L11str__offsetS333 + _M0L6lengthS331;
  _M0L2e2S332 = _M0L6_2atmpS1351 - 1;
  _M0L4len1S334 = Moonbit_array_length(_M0L4selfS335);
  _M0L4len2S336 = Moonbit_array_length(_M0L3strS337);
  if (
    _M0L6lengthS331 >= 0
    && _M0L13bytes__offsetS330 >= 0
    && _M0L2e1S329 < _M0L4len1S334
    && _M0L11str__offsetS333 >= 0
    && _M0L2e2S332 < _M0L4len2S336
  ) {
    int32_t _M0L16end__str__offsetS338 =
      _M0L11str__offsetS333 + _M0L6lengthS331;
    int32_t _M0L1iS339 = _M0L11str__offsetS333;
    int32_t _M0L1jS340 = _M0L13bytes__offsetS330;
    while (1) {
      if (_M0L1iS339 < _M0L16end__str__offsetS338) {
        int32_t _M0L6_2atmpS1348 = _M0L3strS337[_M0L1iS339];
        int32_t _M0L6_2atmpS1347 = (int32_t)_M0L6_2atmpS1348;
        uint32_t _M0L1cS341 = *(uint32_t*)&_M0L6_2atmpS1347;
        uint32_t _M0L6_2atmpS1343 = _M0L1cS341 & 255u;
        int32_t _M0L6_2atmpS1342;
        int32_t _M0L6_2atmpS1344;
        uint32_t _M0L6_2atmpS1346;
        int32_t _M0L6_2atmpS1345;
        int32_t _M0L6_2atmpS1349;
        int32_t _M0L6_2atmpS1350;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1342 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1343);
        if (
          _M0L1jS340 < 0 || _M0L1jS340 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L1jS340] = _M0L6_2atmpS1342;
        _M0L6_2atmpS1344 = _M0L1jS340 + 1;
        _M0L6_2atmpS1346 = _M0L1cS341 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1345 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1346);
        if (
          _M0L6_2atmpS1344 < 0
          || _M0L6_2atmpS1344 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L6_2atmpS1344] = _M0L6_2atmpS1345;
        _M0L6_2atmpS1349 = _M0L1iS339 + 1;
        _M0L6_2atmpS1350 = _M0L1jS340 + 2;
        _M0L1iS339 = _M0L6_2atmpS1349;
        _M0L1jS340 = _M0L6_2atmpS1350;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS328) {
  int32_t _M0L6_2atmpS1341;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1341 = *(int32_t*)&_M0L4selfS328;
  return _M0L6_2atmpS1341 & 0xff;
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS312,
  int32_t _M0L5radixS311
) {
  int32_t _M0L12is__negativeS313;
  uint32_t _M0L3numS314;
  uint16_t* _M0L6bufferS315;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS311 < 2 || _M0L5radixS311 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_13.data);
  }
  if (_M0L4selfS312 == 0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS313 = _M0L4selfS312 < 0;
  if (_M0L12is__negativeS313) {
    int32_t _M0L6_2atmpS1340 = -_M0L4selfS312;
    _M0L3numS314 = *(uint32_t*)&_M0L6_2atmpS1340;
  } else {
    _M0L3numS314 = *(uint32_t*)&_M0L4selfS312;
  }
  switch (_M0L5radixS311) {
    case 10: {
      int32_t _M0L10digit__lenS316;
      int32_t _M0L6_2atmpS1337;
      int32_t _M0L10total__lenS317;
      uint16_t* _M0L6bufferS318;
      int32_t _M0L12digit__startS319;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS316 = _M0FPB12dec__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1337 = 1;
      } else {
        _M0L6_2atmpS1337 = 0;
      }
      _M0L10total__lenS317 = _M0L10digit__lenS316 + _M0L6_2atmpS1337;
      _M0L6bufferS318
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS317, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS319 = 1;
      } else {
        _M0L12digit__startS319 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS318, _M0L3numS314, _M0L12digit__startS319, _M0L10total__lenS317);
      _M0L6bufferS315 = _M0L6bufferS318;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS320;
      int32_t _M0L6_2atmpS1338;
      int32_t _M0L10total__lenS321;
      uint16_t* _M0L6bufferS322;
      int32_t _M0L12digit__startS323;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS320 = _M0FPB12hex__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1338 = 1;
      } else {
        _M0L6_2atmpS1338 = 0;
      }
      _M0L10total__lenS321 = _M0L10digit__lenS320 + _M0L6_2atmpS1338;
      _M0L6bufferS322
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS321, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS323 = 1;
      } else {
        _M0L12digit__startS323 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS322, _M0L3numS314, _M0L12digit__startS323, _M0L10total__lenS321);
      _M0L6bufferS315 = _M0L6bufferS322;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS324;
      int32_t _M0L6_2atmpS1339;
      int32_t _M0L10total__lenS325;
      uint16_t* _M0L6bufferS326;
      int32_t _M0L12digit__startS327;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS324
      = _M0FPB14radix__count32(_M0L3numS314, _M0L5radixS311);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1339 = 1;
      } else {
        _M0L6_2atmpS1339 = 0;
      }
      _M0L10total__lenS325 = _M0L10digit__lenS324 + _M0L6_2atmpS1339;
      _M0L6bufferS326
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS325, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS327 = 1;
      } else {
        _M0L12digit__startS327 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS326, _M0L3numS314, _M0L12digit__startS327, _M0L10total__lenS325, _M0L5radixS311);
      _M0L6bufferS315 = _M0L6bufferS326;
      break;
    }
  }
  if (_M0L12is__negativeS313) {
    _M0L6bufferS315[0] = 45;
  }
  return _M0L6bufferS315;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS305,
  int32_t _M0L5radixS307
) {
  uint32_t _M0L4baseS306;
  uint32_t _M0L3numS308;
  int32_t _M0L5countS309;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS305 == 0u) {
    return 1;
  }
  _M0L4baseS306 = *(uint32_t*)&_M0L5radixS307;
  _M0L3numS308 = _M0L5valueS305;
  _M0L5countS309 = 0;
  while (1) {
    if (_M0L3numS308 > 0u) {
      uint32_t _M0L6_2atmpS1335 = _M0L3numS308 / _M0L4baseS306;
      int32_t _M0L6_2atmpS1336 = _M0L5countS309 + 1;
      _M0L3numS308 = _M0L6_2atmpS1335;
      _M0L5countS309 = _M0L6_2atmpS1336;
      continue;
    } else {
      return _M0L5countS309;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS303) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS303 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS304;
    int32_t _M0L6_2atmpS1334;
    int32_t _M0L6_2atmpS1333;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS304 = moonbit_clz32(_M0L5valueS303);
    _M0L6_2atmpS1334 = 31 - _M0L14leading__zerosS304;
    _M0L6_2atmpS1333 = _M0L6_2atmpS1334 / 4;
    return _M0L6_2atmpS1333 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS302) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS302 >= 100000u) {
    if (_M0L5valueS302 >= 10000000u) {
      if (_M0L5valueS302 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS302 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS302 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS302 >= 1000u) {
    if (_M0L5valueS302 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS302 >= 100u) {
    return 3;
  } else if (_M0L5valueS302 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS288,
  uint32_t _M0L3numS300,
  int32_t _M0L12digit__startS289,
  int32_t _M0L10total__lenS301
) {
  int32_t _M0L6_2atmpS1332;
  uint32_t _M0L3numS278;
  int32_t _M0L6offsetS279;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1332 = _M0L10total__lenS301 - _M0L12digit__startS289;
  _M0L3numS278 = _M0L3numS300;
  _M0L6offsetS279 = _M0L6_2atmpS1332;
  while (1) {
    if (_M0L3numS278 >= 10000u) {
      uint32_t _M0L1tS280 = _M0L3numS278 / 10000u;
      uint32_t _M0L6_2atmpS1309 = _M0L3numS278 % 10000u;
      int32_t _M0L1rS281 = *(int32_t*)&_M0L6_2atmpS1309;
      int32_t _M0L2d1S282 = _M0L1rS281 / 100;
      int32_t _M0L2d2S283 = _M0L1rS281 % 100;
      int32_t _M0L6_2atmpS1308 = _M0L2d1S282 / 10;
      int32_t _M0L6_2atmpS1307 = 48 + _M0L6_2atmpS1308;
      int32_t _M0L6d1__hiS284 = (uint16_t)_M0L6_2atmpS1307;
      int32_t _M0L6_2atmpS1306 = _M0L2d1S282 % 10;
      int32_t _M0L6_2atmpS1305 = 48 + _M0L6_2atmpS1306;
      int32_t _M0L6d1__loS285 = (uint16_t)_M0L6_2atmpS1305;
      int32_t _M0L6_2atmpS1304 = _M0L2d2S283 / 10;
      int32_t _M0L6_2atmpS1303 = 48 + _M0L6_2atmpS1304;
      int32_t _M0L6d2__hiS286 = (uint16_t)_M0L6_2atmpS1303;
      int32_t _M0L6_2atmpS1302 = _M0L2d2S283 % 10;
      int32_t _M0L6_2atmpS1301 = 48 + _M0L6_2atmpS1302;
      int32_t _M0L6d2__loS287 = (uint16_t)_M0L6_2atmpS1301;
      int32_t _M0L6_2atmpS1293 = _M0L12digit__startS289 + _M0L6offsetS279;
      int32_t _M0L6_2atmpS1292 = _M0L6_2atmpS1293 - 4;
      int32_t _M0L6_2atmpS1295;
      int32_t _M0L6_2atmpS1294;
      int32_t _M0L6_2atmpS1297;
      int32_t _M0L6_2atmpS1296;
      int32_t _M0L6_2atmpS1299;
      int32_t _M0L6_2atmpS1298;
      int32_t _M0L6_2atmpS1300;
      _M0L6bufferS288[_M0L6_2atmpS1292] = _M0L6d1__hiS284;
      _M0L6_2atmpS1295 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1294 = _M0L6_2atmpS1295 - 3;
      _M0L6bufferS288[_M0L6_2atmpS1294] = _M0L6d1__loS285;
      _M0L6_2atmpS1297 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1296 = _M0L6_2atmpS1297 - 2;
      _M0L6bufferS288[_M0L6_2atmpS1296] = _M0L6d2__hiS286;
      _M0L6_2atmpS1299 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1298 = _M0L6_2atmpS1299 - 1;
      _M0L6bufferS288[_M0L6_2atmpS1298] = _M0L6d2__loS287;
      _M0L6_2atmpS1300 = _M0L6offsetS279 - 4;
      _M0L3numS278 = _M0L1tS280;
      _M0L6offsetS279 = _M0L6_2atmpS1300;
      continue;
    } else {
      int32_t _M0L6_2atmpS1331 = *(int32_t*)&_M0L3numS278;
      int32_t _M0L9remainingS291 = _M0L6_2atmpS1331;
      int32_t _M0L6offsetS292 = _M0L6offsetS279;
      while (1) {
        if (_M0L9remainingS291 >= 100) {
          int32_t _M0L1tS293 = _M0L9remainingS291 / 100;
          int32_t _M0L1dS294 = _M0L9remainingS291 % 100;
          int32_t _M0L6_2atmpS1318 = _M0L1dS294 / 10;
          int32_t _M0L6_2atmpS1317 = 48 + _M0L6_2atmpS1318;
          int32_t _M0L5d__hiS295 = (uint16_t)_M0L6_2atmpS1317;
          int32_t _M0L6_2atmpS1316 = _M0L1dS294 % 10;
          int32_t _M0L6_2atmpS1315 = 48 + _M0L6_2atmpS1316;
          int32_t _M0L5d__loS296 = (uint16_t)_M0L6_2atmpS1315;
          int32_t _M0L6_2atmpS1311 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1310 = _M0L6_2atmpS1311 - 2;
          int32_t _M0L6_2atmpS1313;
          int32_t _M0L6_2atmpS1312;
          int32_t _M0L6_2atmpS1314;
          _M0L6bufferS288[_M0L6_2atmpS1310] = _M0L5d__hiS295;
          _M0L6_2atmpS1313 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS1312 = _M0L6_2atmpS1313 - 1;
          _M0L6bufferS288[_M0L6_2atmpS1312] = _M0L5d__loS296;
          _M0L6_2atmpS1314 = _M0L6offsetS292 - 2;
          _M0L9remainingS291 = _M0L1tS293;
          _M0L6offsetS292 = _M0L6_2atmpS1314;
          continue;
        } else if (_M0L9remainingS291 >= 10) {
          int32_t _M0L6_2atmpS1326 = _M0L9remainingS291 / 10;
          int32_t _M0L6_2atmpS1325 = 48 + _M0L6_2atmpS1326;
          int32_t _M0L5d__hiS298 = (uint16_t)_M0L6_2atmpS1325;
          int32_t _M0L6_2atmpS1324 = _M0L9remainingS291 % 10;
          int32_t _M0L6_2atmpS1323 = 48 + _M0L6_2atmpS1324;
          int32_t _M0L5d__loS299 = (uint16_t)_M0L6_2atmpS1323;
          int32_t _M0L6_2atmpS1320 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1319 = _M0L6_2atmpS1320 - 2;
          int32_t _M0L6_2atmpS1322;
          int32_t _M0L6_2atmpS1321;
          _M0L6bufferS288[_M0L6_2atmpS1319] = _M0L5d__hiS298;
          _M0L6_2atmpS1322 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS1321 = _M0L6_2atmpS1322 - 1;
          _M0L6bufferS288[_M0L6_2atmpS1321] = _M0L5d__loS299;
        } else {
          int32_t _M0L6_2atmpS1330 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1327 = _M0L6_2atmpS1330 - 1;
          int32_t _M0L6_2atmpS1329 = 48 + _M0L9remainingS291;
          int32_t _M0L6_2atmpS1328 = (uint16_t)_M0L6_2atmpS1329;
          _M0L6bufferS288[_M0L6_2atmpS1327] = _M0L6_2atmpS1328;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS268,
  uint32_t _M0L3numS272,
  int32_t _M0L12digit__startS269,
  int32_t _M0L10total__lenS271,
  int32_t _M0L5radixS262
) {
  uint32_t _M0L4baseS261;
  int32_t _M0L6_2atmpS1277;
  int32_t _M0L6_2atmpS1276;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS261 = *(uint32_t*)&_M0L5radixS262;
  _M0L6_2atmpS1277 = _M0L5radixS262 - 1;
  _M0L6_2atmpS1276 = _M0L5radixS262 & _M0L6_2atmpS1277;
  if (_M0L6_2atmpS1276 == 0) {
    int32_t _M0L5shiftS263;
    uint32_t _M0L4maskS264;
    int32_t _M0L6_2atmpS1284;
    int32_t _M0L6offsetS265;
    uint32_t _M0L1nS266;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS263 = moonbit_ctz32(_M0L5radixS262);
    _M0L4maskS264 = _M0L4baseS261 - 1u;
    _M0L6_2atmpS1284 = _M0L10total__lenS271 - _M0L12digit__startS269;
    _M0L6offsetS265 = _M0L6_2atmpS1284;
    _M0L1nS266 = _M0L3numS272;
    while (1) {
      if (_M0L1nS266 > 0u) {
        uint32_t _M0L6_2atmpS1283 = _M0L1nS266 & _M0L4maskS264;
        int32_t _M0L5digitS267 = *(int32_t*)&_M0L6_2atmpS1283;
        int32_t _M0L6_2atmpS1280 = _M0L12digit__startS269 + _M0L6offsetS265;
        int32_t _M0L6_2atmpS1278 = _M0L6_2atmpS1280 - 1;
        int32_t _M0L6_2atmpS1279 =
          ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L5digitS267];
        int32_t _M0L6_2atmpS1281;
        uint32_t _M0L6_2atmpS1282;
        _M0L6bufferS268[_M0L6_2atmpS1278] = _M0L6_2atmpS1279;
        _M0L6_2atmpS1281 = _M0L6offsetS265 - 1;
        _M0L6_2atmpS1282 = _M0L1nS266 >> (_M0L5shiftS263 & 31);
        _M0L6offsetS265 = _M0L6_2atmpS1281;
        _M0L1nS266 = _M0L6_2atmpS1282;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1291 = _M0L10total__lenS271 - _M0L12digit__startS269;
    int32_t _M0L6offsetS273 = _M0L6_2atmpS1291;
    uint32_t _M0L1nS274 = _M0L3numS272;
    while (1) {
      if (_M0L1nS274 > 0u) {
        uint32_t _M0L1qS275 = _M0L1nS274 / _M0L4baseS261;
        uint32_t _M0L6_2atmpS1290 = _M0L1qS275 * _M0L4baseS261;
        uint32_t _M0L6_2atmpS1289 = _M0L1nS274 - _M0L6_2atmpS1290;
        int32_t _M0L5digitS276 = *(int32_t*)&_M0L6_2atmpS1289;
        int32_t _M0L6_2atmpS1287 = _M0L12digit__startS269 + _M0L6offsetS273;
        int32_t _M0L6_2atmpS1285 = _M0L6_2atmpS1287 - 1;
        int32_t _M0L6_2atmpS1286 =
          ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L5digitS276];
        int32_t _M0L6_2atmpS1288;
        _M0L6bufferS268[_M0L6_2atmpS1285] = _M0L6_2atmpS1286;
        _M0L6_2atmpS1288 = _M0L6offsetS273 - 1;
        _M0L6offsetS273 = _M0L6_2atmpS1288;
        _M0L1nS274 = _M0L1qS275;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS255,
  uint32_t _M0L3numS260,
  int32_t _M0L12digit__startS256,
  int32_t _M0L10total__lenS259
) {
  int32_t _M0L6_2atmpS1275;
  int32_t _M0L6offsetS250;
  uint32_t _M0L1nS251;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1275 = _M0L10total__lenS259 - _M0L12digit__startS256;
  _M0L6offsetS250 = _M0L6_2atmpS1275;
  _M0L1nS251 = _M0L3numS260;
  while (1) {
    if (_M0L6offsetS250 >= 2) {
      uint32_t _M0L6_2atmpS1272 = _M0L1nS251 & 255u;
      int32_t _M0L9byte__valS252 = *(int32_t*)&_M0L6_2atmpS1272;
      int32_t _M0L2hiS253 = _M0L9byte__valS252 / 16;
      int32_t _M0L2loS254 = _M0L9byte__valS252 % 16;
      int32_t _M0L6_2atmpS1266 = _M0L12digit__startS256 + _M0L6offsetS250;
      int32_t _M0L6_2atmpS1264 = _M0L6_2atmpS1266 - 2;
      int32_t _M0L6_2atmpS1265 =
        ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L2hiS253];
      int32_t _M0L6_2atmpS1269;
      int32_t _M0L6_2atmpS1267;
      int32_t _M0L6_2atmpS1268;
      int32_t _M0L6_2atmpS1270;
      uint32_t _M0L6_2atmpS1271;
      _M0L6bufferS255[_M0L6_2atmpS1264] = _M0L6_2atmpS1265;
      _M0L6_2atmpS1269 = _M0L12digit__startS256 + _M0L6offsetS250;
      _M0L6_2atmpS1267 = _M0L6_2atmpS1269 - 1;
      _M0L6_2atmpS1268
      = ((moonbit_string_t)moonbit_string_literal_15.data)[
        _M0L2loS254
      ];
      _M0L6bufferS255[_M0L6_2atmpS1267] = _M0L6_2atmpS1268;
      _M0L6_2atmpS1270 = _M0L6offsetS250 - 2;
      _M0L6_2atmpS1271 = _M0L1nS251 >> 8;
      _M0L6offsetS250 = _M0L6_2atmpS1270;
      _M0L1nS251 = _M0L6_2atmpS1271;
      continue;
    } else if (_M0L6offsetS250 == 1) {
      uint32_t _M0L6_2atmpS1274 = _M0L1nS251 & 15u;
      int32_t _M0L6nibbleS258 = *(int32_t*)&_M0L6_2atmpS1274;
      int32_t _M0L6_2atmpS1273 =
        ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L6nibbleS258];
      _M0L6bufferS255[_M0L12digit__startS256] = _M0L6_2atmpS1273;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS249
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS248;
  struct _M0TPB6Logger _M0L6_2atmpS1263;
  moonbit_string_t _result_2092;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS248);
  _M0L6_2atmpS1263
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS248
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS249, _M0L6_2atmpS1263);
  if (_M0L6_2atmpS1263.$1) {
    moonbit_decref(_M0L6_2atmpS1263.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2092 = _M0MPB13StringBuilder10to__string(_M0L6loggerS248);
  moonbit_decref_cycle_free(_M0L6loggerS248);
  return _result_2092;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS245,
  struct _M0TPB6Logger _M0L6loggerS244
) {
  moonbit_string_t _M0L6_2atmpS1261;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1261 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS245);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS244.$0->$method_0(_M0L6loggerS244.$1, _M0L6_2atmpS1261);
  moonbit_decref_cycle_free(_M0L6_2atmpS1261);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS247,
  struct _M0TPB6Logger _M0L6loggerS246
) {
  moonbit_string_t _M0L6_2atmpS1262;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1262 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS247);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS246.$0->$method_0(_M0L6loggerS246.$1, _M0L6_2atmpS1262);
  moonbit_decref_cycle_free(_M0L6_2atmpS1262);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS243
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS243.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS242
) {
  moonbit_string_t _M0L8_2afieldS1969;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1969 = _M0L4selfS242.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1969);
  return _M0L8_2afieldS1969;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS238,
  moonbit_string_t _M0L5valueS239,
  int32_t _M0L5startS240,
  int32_t _M0L3lenS241
) {
  int32_t _M0L6_2atmpS1260;
  int64_t _M0L6_2atmpS1259;
  struct _M0TPC16string10StringView _M0L6_2atmpS1258;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1260 = _M0L5startS240 + _M0L3lenS241;
  _M0L6_2atmpS1259 = (int64_t)_M0L6_2atmpS1260;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1258
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS239, _M0L5startS240, _M0L6_2atmpS1259);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS238, _M0L6_2atmpS1258);
  moonbit_decref_cycle_free(_M0L6_2atmpS1258.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS231,
  int32_t _M0L5startS233,
  int64_t _M0L3endS235
) {
  int32_t _M0L3lenS230;
  int32_t _M0Lm2loS232;
  int32_t _M0Lm2hiS234;
  int32_t _M0L6_2atmpS1242;
  int32_t _if__result_2093;
  int32_t _M0L6_2atmpS1250;
  int32_t _if__result_2094;
  int32_t _M0L6_2atmpS1252;
  int32_t _M0L6_2atmpS1253;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS230 = Moonbit_array_length(_M0L4selfS231);
  if (_M0L5startS233 < 0) {
    _M0Lm2loS232 = 0;
  } else if (_M0L5startS233 > _M0L3lenS230) {
    _M0Lm2loS232 = _M0L3lenS230;
  } else {
    _M0Lm2loS232 = _M0L5startS233;
  }
  if (_M0L3endS235 == 4294967296ll) {
    _M0Lm2hiS234 = _M0L3lenS230;
  } else {
    int64_t _M0L7_2aSomeS236 = _M0L3endS235;
    int32_t _M0L4_2aeS237 = (int32_t)_M0L7_2aSomeS236;
    if (_M0L4_2aeS237 < 0) {
      _M0Lm2hiS234 = 0;
    } else if (_M0L4_2aeS237 > _M0L3lenS230) {
      _M0Lm2hiS234 = _M0L3lenS230;
    } else {
      _M0Lm2hiS234 = _M0L4_2aeS237;
    }
  }
  _M0L6_2atmpS1242 = _M0Lm2loS232;
  if (_M0L6_2atmpS1242 > 0) {
    int32_t _M0L6_2atmpS1241 = _M0Lm2loS232;
    if (_M0L6_2atmpS1241 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS1240 = _M0Lm2loS232;
      int32_t _M0L6_2atmpS1239 = _M0L4selfS231[_M0L6_2atmpS1240];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1239)) {
        int32_t _M0L6_2atmpS1238 = _M0Lm2loS232;
        int32_t _M0L6_2atmpS1237 = _M0L6_2atmpS1238 - 1;
        int32_t _M0L6_2atmpS1236 = _M0L4selfS231[_M0L6_2atmpS1237];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2093
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1236);
      } else {
        _if__result_2093 = 0;
      }
    } else {
      _if__result_2093 = 0;
    }
  } else {
    _if__result_2093 = 0;
  }
  if (_if__result_2093) {
    int32_t _M0L6_2atmpS1243 = _M0Lm2loS232;
    _M0Lm2loS232 = _M0L6_2atmpS1243 + 1;
  }
  _M0L6_2atmpS1250 = _M0Lm2hiS234;
  if (_M0L6_2atmpS1250 > 0) {
    int32_t _M0L6_2atmpS1249 = _M0Lm2hiS234;
    if (_M0L6_2atmpS1249 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS1248 = _M0Lm2hiS234;
      int32_t _M0L6_2atmpS1247 = _M0L4selfS231[_M0L6_2atmpS1248];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1247)) {
        int32_t _M0L6_2atmpS1246 = _M0Lm2hiS234;
        int32_t _M0L6_2atmpS1245 = _M0L6_2atmpS1246 - 1;
        int32_t _M0L6_2atmpS1244 = _M0L4selfS231[_M0L6_2atmpS1245];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2094
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1244);
      } else {
        _if__result_2094 = 0;
      }
    } else {
      _if__result_2094 = 0;
    }
  } else {
    _if__result_2094 = 0;
  }
  if (_if__result_2094) {
    int32_t _M0L6_2atmpS1251 = _M0Lm2hiS234;
    _M0Lm2hiS234 = _M0L6_2atmpS1251 - 1;
  }
  _M0L6_2atmpS1252 = _M0Lm2loS232;
  _M0L6_2atmpS1253 = _M0Lm2hiS234;
  if (_M0L6_2atmpS1252 >= _M0L6_2atmpS1253) {
    int32_t _M0L6_2atmpS1254 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS1255 = _M0Lm2loS232;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS1254,
                                                 .$2 = _M0L6_2atmpS1255};
  } else {
    int32_t _M0L6_2atmpS1256 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS1257 = _M0Lm2hiS234;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS1256,
                                                 .$2 = _M0L6_2atmpS1257};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS229,
  struct _M0TPB4Show _M0L4showS228
) {
  struct _M0TPB6Logger _M0L6_2atmpS1235;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS229);
  _M0L6_2atmpS1235
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS229
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS228.$0->$method_0(_M0L4showS228.$1, _M0L6_2atmpS1235);
  if (_M0L6_2atmpS1235.$1) {
    moonbit_decref(_M0L6_2atmpS1235.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS227,
  struct _M0TPB4Show _M0L4showS226
) {
  struct _M0TPB6Logger _M0L6_2atmpS1234;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1234
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS227
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS226.$0->$method_0(_M0L4showS226.$1, _M0L6_2atmpS1234);
  if (_M0L6_2atmpS1234.$1) {
    moonbit_decref(_M0L6_2atmpS1234.$1);
  }
  return 0;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS224,
  int32_t _M0L5quoteS225
) {
  struct _M0TPB13StringBuilder* _M0L3bufS223;
  int32_t _M0L6_2atmpS1233;
  struct _M0TPC16string10StringView _M0L6_2atmpS1231;
  struct _M0TPB6Logger _M0L6_2atmpS1232;
  moonbit_string_t _result_2095;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS223 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1233 = Moonbit_array_length(_M0L4selfS224);
  moonbit_incref_cycle_free(_M0L4selfS224);
  _M0L6_2atmpS1231
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS224, .$1 = 0, .$2 = _M0L6_2atmpS1233
  };
  moonbit_incref_cycle_free(_M0L3bufS223);
  _M0L6_2atmpS1232
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS223
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1231, _M0L6_2atmpS1232, _M0L5quoteS225);
  moonbit_decref_cycle_free(_M0L6_2atmpS1231.$0);
  if (_M0L6_2atmpS1232.$1) {
    moonbit_decref(_M0L6_2atmpS1232.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2095 = _M0MPB13StringBuilder10to__string(_M0L3bufS223);
  moonbit_decref_cycle_free(_M0L3bufS223);
  return _result_2095;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS215,
  struct _M0TPB6Logger _M0L6loggerS213,
  int32_t _M0L5quoteS212
) {
  int32_t _M0L3endS1229;
  int32_t _M0L5startS1230;
  int32_t _M0L3lenS214;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS216;
  int32_t _M0L1iS217;
  int32_t _M0L3segS218;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS212) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 34);
  }
  _M0L3endS1229 = _M0L4selfS215.$2;
  _M0L5startS1230 = _M0L4selfS215.$1;
  _M0L3lenS214 = _M0L3endS1229 - _M0L5startS1230;
  moonbit_incref_cycle_free(_M0L4selfS215.$0);
  if (_M0L6loggerS213.$1) {
    moonbit_incref(_M0L6loggerS213.$1);
  }
  _M0L6_2aenvS216
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS216)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 82, 0);
  _M0L6_2aenvS216->$0 = _M0L4selfS215;
  _M0L6_2aenvS216->$1 = _M0L6loggerS213;
  _M0L1iS217 = 0;
  _M0L3segS218 = 0;
  _2afor_219:;
  while (1) {
    moonbit_string_t _M0L3strS1226;
    int32_t _M0L5startS1228;
    int32_t _M0L6_2atmpS1227;
    int32_t _M0L4codeS220;
    int32_t _M0L1cS222;
    int32_t _M0L6_2atmpS1210;
    int32_t _M0L6_2atmpS1211;
    int32_t _M0L6_2atmpS1212;
    if (_M0L1iS217 >= _M0L3lenS214) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
      moonbit_decref_cycle_free(_M0L6_2aenvS216);
      break;
    }
    _M0L3strS1226 = _M0L4selfS215.$0;
    _M0L5startS1228 = _M0L4selfS215.$1;
    _M0L6_2atmpS1227 = _M0L5startS1228 + _M0L1iS217;
    _M0L4codeS220 = _M0L3strS1226[_M0L6_2atmpS1227];
    switch (_M0L4codeS220) {
      case 34: {
        _M0L1cS222 = _M0L4codeS220;
        goto join_221;
        break;
      }
      
      case 92: {
        _M0L1cS222 = _M0L4codeS220;
        goto join_221;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1213;
        int32_t _M0L6_2atmpS1214;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_16.data);
        _M0L6_2atmpS1213 = _M0L1iS217 + 1;
        _M0L6_2atmpS1214 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1213;
        _M0L3segS218 = _M0L6_2atmpS1214;
        goto _2afor_219;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1215;
        int32_t _M0L6_2atmpS1216;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_17.data);
        _M0L6_2atmpS1215 = _M0L1iS217 + 1;
        _M0L6_2atmpS1216 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1215;
        _M0L3segS218 = _M0L6_2atmpS1216;
        goto _2afor_219;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1217;
        int32_t _M0L6_2atmpS1218;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1217 = _M0L1iS217 + 1;
        _M0L6_2atmpS1218 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1217;
        _M0L3segS218 = _M0L6_2atmpS1218;
        goto _2afor_219;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1219;
        int32_t _M0L6_2atmpS1220;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1219 = _M0L1iS217 + 1;
        _M0L6_2atmpS1220 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1219;
        _M0L3segS218 = _M0L6_2atmpS1220;
        goto _2afor_219;
        break;
      }
      default: {
        if (_M0L4codeS220 < 32) {
          int32_t _M0L6_2atmpS1222;
          moonbit_string_t _M0L6_2atmpS1221;
          int32_t _M0L6_2atmpS1223;
          int32_t _M0L6_2atmpS1224;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_20.data);
          _M0L6_2atmpS1222 = _M0L4codeS220 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1221 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1222);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, _M0L6_2atmpS1221);
          moonbit_decref_cycle_free(_M0L6_2atmpS1221);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1223 = _M0L1iS217 + 1;
          _M0L6_2atmpS1224 = _M0L1iS217 + 1;
          _M0L1iS217 = _M0L6_2atmpS1223;
          _M0L3segS218 = _M0L6_2atmpS1224;
          goto _2afor_219;
        } else {
          int32_t _M0L6_2atmpS1225 = _M0L1iS217 + 1;
          int32_t _tmp_2098 = _M0L3segS218;
          _M0L1iS217 = _M0L6_2atmpS1225;
          _M0L3segS218 = _tmp_2098;
          goto _2afor_219;
        }
        break;
      }
    }
    goto joinlet_2097;
    join_221:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1210 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS222);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, _M0L6_2atmpS1210);
    _M0L6_2atmpS1211 = _M0L1iS217 + 1;
    _M0L6_2atmpS1212 = _M0L1iS217 + 1;
    _M0L1iS217 = _M0L6_2atmpS1211;
    _M0L3segS218 = _M0L6_2atmpS1212;
    continue;
    joinlet_2097:;
    break;
  }
  if (_M0L5quoteS212) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS208,
  int32_t _M0L3segS211,
  int32_t _M0L1iS210
) {
  struct _M0TPB6Logger _M0L6loggerS207;
  struct _M0TPC16string10StringView _M0L4selfS209;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS207 = _M0L6_2aenvS208->$1;
  _M0L4selfS209 = _M0L6_2aenvS208->$0;
  if (_M0L1iS210 > _M0L3segS211) {
    int64_t _M0L6_2atmpS1209 = (int64_t)_M0L1iS210;
    struct _M0TPC16string10StringView _M0L6_2atmpS1208;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1208
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS209, _M0L3segS211, _M0L6_2atmpS1209);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS207.$0->$method_2(_M0L6loggerS207.$1, _M0L6_2atmpS1208);
    moonbit_decref_cycle_free(_M0L6_2atmpS1208.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS198,
  int32_t _M0L5startS200,
  int64_t _M0L3endS202
) {
  int32_t _M0L3endS1206;
  int32_t _M0L5startS1207;
  int32_t _M0L3lenS197;
  int32_t _M0Lm2loS199;
  int32_t _M0Lm2hiS201;
  moonbit_string_t _M0L3strS205;
  int32_t _M0L4baseS206;
  int32_t _M0L6_2atmpS1184;
  int32_t _if__result_2099;
  int32_t _M0L6_2atmpS1194;
  int32_t _if__result_2100;
  int32_t _M0L6_2atmpS1196;
  int32_t _M0L6_2atmpS1197;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1206 = _M0L4selfS198.$2;
  _M0L5startS1207 = _M0L4selfS198.$1;
  _M0L3lenS197 = _M0L3endS1206 - _M0L5startS1207;
  if (_M0L5startS200 < 0) {
    _M0Lm2loS199 = 0;
  } else if (_M0L5startS200 > _M0L3lenS197) {
    _M0Lm2loS199 = _M0L3lenS197;
  } else {
    _M0Lm2loS199 = _M0L5startS200;
  }
  if (_M0L3endS202 == 4294967296ll) {
    _M0Lm2hiS201 = _M0L3lenS197;
  } else {
    int64_t _M0L7_2aSomeS203 = _M0L3endS202;
    int32_t _M0L4_2aeS204 = (int32_t)_M0L7_2aSomeS203;
    if (_M0L4_2aeS204 < 0) {
      _M0Lm2hiS201 = 0;
    } else if (_M0L4_2aeS204 > _M0L3lenS197) {
      _M0Lm2hiS201 = _M0L3lenS197;
    } else {
      _M0Lm2hiS201 = _M0L4_2aeS204;
    }
  }
  _M0L3strS205 = _M0L4selfS198.$0;
  _M0L4baseS206 = _M0L4selfS198.$1;
  _M0L6_2atmpS1184 = _M0Lm2loS199;
  if (_M0L6_2atmpS1184 > 0) {
    int32_t _M0L6_2atmpS1183 = _M0Lm2loS199;
    if (_M0L6_2atmpS1183 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1182 = _M0Lm2loS199;
      int32_t _M0L6_2atmpS1181 = _M0L4baseS206 + _M0L6_2atmpS1182;
      int32_t _M0L6_2atmpS1180 = _M0L3strS205[_M0L6_2atmpS1181];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1180)) {
        int32_t _M0L6_2atmpS1179 = _M0Lm2loS199;
        int32_t _M0L6_2atmpS1178 = _M0L4baseS206 + _M0L6_2atmpS1179;
        int32_t _M0L6_2atmpS1177 = _M0L6_2atmpS1178 - 1;
        int32_t _M0L6_2atmpS1176 = _M0L3strS205[_M0L6_2atmpS1177];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2099
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1176);
      } else {
        _if__result_2099 = 0;
      }
    } else {
      _if__result_2099 = 0;
    }
  } else {
    _if__result_2099 = 0;
  }
  if (_if__result_2099) {
    int32_t _M0L6_2atmpS1185 = _M0Lm2loS199;
    _M0Lm2loS199 = _M0L6_2atmpS1185 + 1;
  }
  _M0L6_2atmpS1194 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1194 > 0) {
    int32_t _M0L6_2atmpS1193 = _M0Lm2hiS201;
    if (_M0L6_2atmpS1193 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1192 = _M0Lm2hiS201;
      int32_t _M0L6_2atmpS1191 = _M0L4baseS206 + _M0L6_2atmpS1192;
      int32_t _M0L6_2atmpS1190 = _M0L3strS205[_M0L6_2atmpS1191];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1190)) {
        int32_t _M0L6_2atmpS1189 = _M0Lm2hiS201;
        int32_t _M0L6_2atmpS1188 = _M0L4baseS206 + _M0L6_2atmpS1189;
        int32_t _M0L6_2atmpS1187 = _M0L6_2atmpS1188 - 1;
        int32_t _M0L6_2atmpS1186 = _M0L3strS205[_M0L6_2atmpS1187];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2100
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1186);
      } else {
        _if__result_2100 = 0;
      }
    } else {
      _if__result_2100 = 0;
    }
  } else {
    _if__result_2100 = 0;
  }
  if (_if__result_2100) {
    int32_t _M0L6_2atmpS1195 = _M0Lm2hiS201;
    _M0Lm2hiS201 = _M0L6_2atmpS1195 - 1;
  }
  _M0L6_2atmpS1196 = _M0Lm2loS199;
  _M0L6_2atmpS1197 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1196 >= _M0L6_2atmpS1197) {
    int32_t _M0L6_2atmpS1201 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1198 = _M0L4baseS206 + _M0L6_2atmpS1201;
    int32_t _M0L6_2atmpS1200 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1199 = _M0L4baseS206 + _M0L6_2atmpS1200;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1198,
                                                 .$2 = _M0L6_2atmpS1199};
  } else {
    int32_t _M0L6_2atmpS1205 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1202 = _M0L4baseS206 + _M0L6_2atmpS1205;
    int32_t _M0L6_2atmpS1204 = _M0Lm2hiS201;
    int32_t _M0L6_2atmpS1203 = _M0L4baseS206 + _M0L6_2atmpS1204;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1202,
                                                 .$2 = _M0L6_2atmpS1203};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS196) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS195;
  int32_t _M0L6_2atmpS1173;
  int32_t _M0L6_2atmpS1172;
  int32_t _M0L6_2atmpS1175;
  int32_t _M0L6_2atmpS1174;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1171;
  moonbit_string_t _result_2101;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS195 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1173 = _M0IPC14byte4BytePB3Div3div(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1172
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1173);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1172);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1175 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1174
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1175);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1174);
  _M0L6_2atmpS1171 = _M0L7_2aselfS195;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2101 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1171);
  moonbit_decref_cycle_free(_M0L6_2atmpS1171);
  return _result_2101;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS194) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS194 < 10) {
    int32_t _M0L6_2atmpS1168;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1168 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1168);
  } else {
    int32_t _M0L6_2atmpS1170;
    int32_t _M0L6_2atmpS1169;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1170 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1169 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1170, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1169);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS192,
  int32_t _M0L4thatS193
) {
  int32_t _M0L6_2atmpS1166;
  int32_t _M0L6_2atmpS1167;
  int32_t _M0L6_2atmpS1165;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1166 = (int32_t)_M0L4selfS192;
  _M0L6_2atmpS1167 = (int32_t)_M0L4thatS193;
  _M0L6_2atmpS1165 = _M0L6_2atmpS1166 - _M0L6_2atmpS1167;
  return _M0L6_2atmpS1165 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS190,
  int32_t _M0L4thatS191
) {
  int32_t _M0L6_2atmpS1163;
  int32_t _M0L6_2atmpS1164;
  int32_t _M0L6_2atmpS1162;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1163 = (int32_t)_M0L4selfS190;
  _M0L6_2atmpS1164 = (int32_t)_M0L4thatS191;
  _M0L6_2atmpS1162 = _M0L6_2atmpS1163 % _M0L6_2atmpS1164;
  return _M0L6_2atmpS1162 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS188,
  int32_t _M0L4thatS189
) {
  int32_t _M0L6_2atmpS1160;
  int32_t _M0L6_2atmpS1161;
  int32_t _M0L6_2atmpS1159;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1160 = (int32_t)_M0L4selfS188;
  _M0L6_2atmpS1161 = (int32_t)_M0L4thatS189;
  _M0L6_2atmpS1159 = _M0L6_2atmpS1160 / _M0L6_2atmpS1161;
  return _M0L6_2atmpS1159 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS186,
  int32_t _M0L4thatS187
) {
  int32_t _M0L6_2atmpS1157;
  int32_t _M0L6_2atmpS1158;
  int32_t _M0L6_2atmpS1156;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1157 = (int32_t)_M0L4selfS186;
  _M0L6_2atmpS1158 = (int32_t)_M0L4thatS187;
  _M0L6_2atmpS1156 = _M0L6_2atmpS1157 + _M0L6_2atmpS1158;
  return _M0L6_2atmpS1156 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS185) {
  int32_t _M0L6_2atmpS1155;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1155 = (int32_t)_M0L4selfS185;
  return _M0L6_2atmpS1155;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS184) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS184 >= 56320 && _M0L4selfS184 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS183) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS183 >= 55296 && _M0L4selfS183 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS182,
  moonbit_string_t _M0L3strS180
) {
  int32_t _M0L8str__lenS179;
  int32_t _M0L3lenS1154;
  int32_t _M0L8requiredS181;
  uint16_t* _M0L4dataS1149;
  int32_t _M0L6_2atmpS1148;
  int32_t _if__result_2102;
  uint16_t* _M0L4dataS1150;
  int32_t _M0L3lenS1151;
  int32_t _M0L3lenS1153;
  int32_t _M0L6_2atmpS1152;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS179 = Moonbit_array_length(_M0L3strS180);
  if (_M0L8str__lenS179 == 0) {
    return 0;
  }
  _M0L3lenS1154 = _M0L4selfS182->$1;
  _M0L8requiredS181 = _M0L3lenS1154 + _M0L8str__lenS179;
  _M0L4dataS1149 = _M0L4selfS182->$0;
  _M0L6_2atmpS1148 = Moonbit_array_length(_M0L4dataS1149);
  if (_M0L8requiredS181 > _M0L6_2atmpS1148) {
    _if__result_2102 = 1;
  } else {
    int32_t _M0L3lenS1147 = _M0L4selfS182->$1;
    _if__result_2102 = _M0L8requiredS181 < _M0L3lenS1147;
  }
  if (_if__result_2102) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS182, _M0L8requiredS181);
  }
  _M0L4dataS1150 = _M0L4selfS182->$0;
  _M0L3lenS1151 = _M0L4selfS182->$1;
  moonbit_incref_cycle_free(_M0L4dataS1150);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1150, _M0L3lenS1151, _M0L3strS180, 0, _M0L8str__lenS179);
  moonbit_decref_cycle_free(_M0L4dataS1150);
  _M0L3lenS1153 = _M0L4selfS182->$1;
  _M0L6_2atmpS1152 = _M0L3lenS1153 + _M0L8str__lenS179;
  _M0L4selfS182->$1 = _M0L6_2atmpS1152;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS175,
  int32_t _M0L11dst__offsetS178,
  moonbit_string_t _M0L3strS176,
  int32_t _M0L11str__offsetS171,
  int32_t _M0L3lenS172
) {
  int32_t _M0L16end__str__offsetS170;
  int32_t _M0L1iS173;
  int32_t _M0L1jS174;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS170 = _M0L11str__offsetS171 + _M0L3lenS172;
  _M0L1iS173 = _M0L11str__offsetS171;
  _M0L1jS174 = _M0L11dst__offsetS178;
  while (1) {
    if (_M0L1iS173 < _M0L16end__str__offsetS170) {
      int32_t _M0L6_2atmpS1144 = _M0L3strS176[_M0L1iS173];
      int32_t _M0L6_2atmpS1145;
      int32_t _M0L6_2atmpS1146;
      _M0L4selfS175[_M0L1jS174] = _M0L6_2atmpS1144;
      _M0L6_2atmpS1145 = _M0L1iS173 + 1;
      _M0L6_2atmpS1146 = _M0L1jS174 + 1;
      _M0L1iS173 = _M0L6_2atmpS1145;
      _M0L1jS174 = _M0L6_2atmpS1146;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS168,
  int32_t _M0L2chS167
) {
  uint32_t _M0L4codeS166;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS166 = _M0MPC14char4Char8to__uint(_M0L2chS167);
  if (_M0L4codeS166 <= 65535u) {
    int32_t _M0L3lenS1115 = _M0L4selfS168->$1;
    uint16_t* _M0L4dataS1117 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1116 = Moonbit_array_length(_M0L4dataS1117);
    uint16_t* _M0L4dataS1120;
    int32_t _M0L3lenS1121;
    int32_t _M0L6_2atmpS1122;
    int32_t _M0L3lenS1124;
    int32_t _M0L6_2atmpS1123;
    if (_M0L3lenS1115 >= _M0L6_2atmpS1116) {
      int32_t _M0L3lenS1119 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1118 = _M0L3lenS1119 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1118);
    }
    _M0L4dataS1120 = _M0L4selfS168->$0;
    _M0L3lenS1121 = _M0L4selfS168->$1;
    moonbit_incref_cycle_free(_M0L4dataS1120);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1122 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS166);
    if (
      _M0L3lenS1121 < 0
      || _M0L3lenS1121 >= Moonbit_array_length(_M0L4dataS1120)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1120[_M0L3lenS1121] = _M0L6_2atmpS1122;
    moonbit_decref_cycle_free(_M0L4dataS1120);
    _M0L3lenS1124 = _M0L4selfS168->$1;
    _M0L6_2atmpS1123 = _M0L3lenS1124 + 1;
    _M0L4selfS168->$1 = _M0L6_2atmpS1123;
  } else if (_M0L4codeS166 <= 1114111u) {
    uint16_t* _M0L4dataS1128 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1126 = Moonbit_array_length(_M0L4dataS1128);
    int32_t _M0L3lenS1127 = _M0L4selfS168->$1;
    int32_t _M0L6_2atmpS1125 = _M0L6_2atmpS1126 - _M0L3lenS1127;
    uint32_t _M0L4codeS169;
    uint16_t* _M0L4dataS1131;
    int32_t _M0L3lenS1132;
    uint32_t _M0L6_2atmpS1135;
    uint32_t _M0L6_2atmpS1134;
    int32_t _M0L6_2atmpS1133;
    uint16_t* _M0L4dataS1136;
    int32_t _M0L3lenS1141;
    int32_t _M0L6_2atmpS1137;
    uint32_t _M0L6_2atmpS1140;
    uint32_t _M0L6_2atmpS1139;
    int32_t _M0L6_2atmpS1138;
    int32_t _M0L3lenS1143;
    int32_t _M0L6_2atmpS1142;
    if (_M0L6_2atmpS1125 < 2) {
      int32_t _M0L3lenS1130 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1129 = _M0L3lenS1130 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1129);
    }
    _M0L4codeS169 = _M0L4codeS166 - 65536u;
    _M0L4dataS1131 = _M0L4selfS168->$0;
    _M0L3lenS1132 = _M0L4selfS168->$1;
    _M0L6_2atmpS1135 = _M0L4codeS169 >> 10;
    _M0L6_2atmpS1134 = 55296u + _M0L6_2atmpS1135;
    moonbit_incref_cycle_free(_M0L4dataS1131);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1133 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1134);
    if (
      _M0L3lenS1132 < 0
      || _M0L3lenS1132 >= Moonbit_array_length(_M0L4dataS1131)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1131[_M0L3lenS1132] = _M0L6_2atmpS1133;
    moonbit_decref_cycle_free(_M0L4dataS1131);
    _M0L4dataS1136 = _M0L4selfS168->$0;
    _M0L3lenS1141 = _M0L4selfS168->$1;
    _M0L6_2atmpS1137 = _M0L3lenS1141 + 1;
    _M0L6_2atmpS1140 = _M0L4codeS169 & 1023u;
    _M0L6_2atmpS1139 = 56320u + _M0L6_2atmpS1140;
    moonbit_incref_cycle_free(_M0L4dataS1136);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1138 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1139);
    if (
      _M0L6_2atmpS1137 < 0
      || _M0L6_2atmpS1137 >= Moonbit_array_length(_M0L4dataS1136)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1136[_M0L6_2atmpS1137] = _M0L6_2atmpS1138;
    moonbit_decref_cycle_free(_M0L4dataS1136);
    _M0L3lenS1143 = _M0L4selfS168->$1;
    _M0L6_2atmpS1142 = _M0L3lenS1143 + 2;
    _M0L4selfS168->$1 = _M0L6_2atmpS1142;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS163,
  int32_t _M0L8requiredS164
) {
  uint16_t* _M0L4dataS1114;
  int32_t _M0L6_2atmpS1112;
  int32_t _M0L3lenS1113;
  int32_t _M0L13new__capacityS162;
  uint16_t* _M0L4dataS1109;
  int32_t _M0L6_2atmpS1110;
  int32_t _M0L3lenS1111;
  uint16_t* _M0L9new__dataS165;
  uint16_t* _M0L6_2aoldS1970;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1114 = _M0L4selfS163->$0;
  _M0L6_2atmpS1112 = Moonbit_array_length(_M0L4dataS1114);
  _M0L3lenS1113 = _M0L4selfS163->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS162
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1112, _M0L3lenS1113, _M0L8requiredS164);
  _M0L4dataS1109 = _M0L4selfS163->$0;
  moonbit_incref_cycle_free(_M0L4dataS1109);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1110 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1111 = _M0L4selfS163->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS165
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1109, _M0L13new__capacityS162, _M0L6_2atmpS1110, _M0L3lenS1111, 0, 0);
  _M0L6_2aoldS1970 = _M0L4selfS163->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1970);
  _M0L4selfS163->$0 = _M0L9new__dataS165;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS161,
  int32_t _M0L3lenS157,
  int32_t _M0L8requiredS156
) {
  int32_t _M0L5spaceS158;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS156 < _M0L3lenS157) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  _M0L5spaceS158 = _M0L7currentS161;
  while (1) {
    if (_M0L5spaceS158 < _M0L8requiredS156) {
      int32_t _M0L4nextS159 = _M0L5spaceS158 * 2;
      if (_M0L4nextS159 <= _M0L5spaceS158) {
        return _M0L8requiredS156;
      }
      _M0L5spaceS158 = _M0L4nextS159;
      continue;
    } else {
      return _M0L5spaceS158;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS155) {
  int32_t _M0L6_2atmpS1108;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1108 = *(int32_t*)&_M0L4selfS155;
  return (uint16_t)_M0L6_2atmpS1108;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS154) {
  int32_t _M0L6_2atmpS1107;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1107 = _M0L4selfS154;
  return *(uint32_t*)&_M0L6_2atmpS1107;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS152
) {
  int32_t _M0L3lenS1098;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1098 = _M0L4selfS152->$1;
  if (_M0L3lenS1098 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1099 = _M0L4selfS152->$1;
    uint16_t* _M0L4dataS1101 = _M0L4selfS152->$0;
    int32_t _M0L6_2atmpS1100 = Moonbit_array_length(_M0L4dataS1101);
    if (_M0L3lenS1099 == _M0L6_2atmpS1100) {
      uint16_t* _M0L4dataS1102 = _M0L4selfS152->$0;
      moonbit_incref_cycle_free(_M0L4dataS1102);
      return _M0L4dataS1102;
    } else {
      uint16_t* _M0L4dataS1103 = _M0L4selfS152->$0;
      int32_t _M0L3lenS1104 = _M0L4selfS152->$1;
      int32_t _M0L6_2atmpS1105;
      int32_t _M0L3lenS1106;
      uint16_t* _M0L4dataS153;
      moonbit_incref_cycle_free(_M0L4dataS1103);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1105 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1106 = _M0L4selfS152->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS153
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1103, _M0L3lenS1104, _M0L6_2atmpS1105, _M0L3lenS1106, 0, 0);
      return _M0L4dataS153;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS149,
  int32_t _M0L13allocate__lenS145,
  int32_t _M0L4initS150,
  int32_t _M0L3lenS146,
  int32_t _M0L11src__offsetS147,
  int32_t _M0L11dst__offsetS148
) {
  int32_t _if__result_2105;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS145 >= 0) {
    if (_M0L3lenS146 >= 0) {
      if (_M0L11src__offsetS147 >= 0) {
        if (_M0L11dst__offsetS148 >= 0) {
          int32_t _M0L6_2atmpS1094 = _M0L11src__offsetS147 + _M0L3lenS146;
          int32_t _M0L6_2atmpS1095 = Moonbit_array_length(_M0L3srcS149);
          if (_M0L6_2atmpS1094 <= _M0L6_2atmpS1095) {
            int32_t _M0L6_2atmpS1093 = _M0L11dst__offsetS148 + _M0L3lenS146;
            _if__result_2105 = _M0L6_2atmpS1093 <= _M0L13allocate__lenS145;
          } else {
            _if__result_2105 = 0;
          }
        } else {
          _if__result_2105 = 0;
        }
      } else {
        _if__result_2105 = 0;
      }
    } else {
      _if__result_2105 = 0;
    }
  } else {
    _if__result_2105 = 0;
  }
  if (_if__result_2105) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS149, _M0L13allocate__lenS145, _M0L4initS150, _M0L11src__offsetS147, _M0L11dst__offsetS148, _M0L3lenS146);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS151;
    int32_t _M0L6_2atmpS1097;
    moonbit_string_t _M0L6_2atmpS1096;
    uint16_t* _result_2106;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS151
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L13allocate__lenS145);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L11src__offsetS147);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L11dst__offsetS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L3lenS146);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_27.data);
    _M0L6_2atmpS1097 = Moonbit_array_length(_M0L3srcS149);
    moonbit_decref_cycle_free(_M0L3srcS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L6_2atmpS1097);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1096
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS151);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS151);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2106 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1096);
    moonbit_decref_cycle_free(_M0L6_2atmpS1096);
    return _result_2106;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS142,
  int32_t _M0L13allocate__lenS139,
  int32_t _M0L4initS140,
  int32_t _M0L11src__offsetS143,
  int32_t _M0L11dst__offsetS141,
  int32_t _M0L9blit__lenS144
) {
  uint16_t* _M0L3dstS138;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS138
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS139, _M0L4initS140);
  moonbit_incref_cycle_free(_M0L3dstS138);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS138, _M0L11dst__offsetS141, _M0L3srcS142, _M0L11src__offsetS143, _M0L9blit__lenS144, sizeof(uint16_t));
  return _M0L3dstS138;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS136
) {
  int32_t _M0L7initialS135;
  uint16_t* _M0L4dataS137;
  struct _M0TPB13StringBuilder* _block_2107;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS136 < 1) {
    _M0L7initialS135 = 1;
  } else {
    int32_t _M0L6_2atmpS1092 = _M0L10size__hintS136 + 1;
    _M0L7initialS135 = _M0L6_2atmpS1092 / 2;
  }
  _M0L4dataS137 = (uint16_t*)moonbit_make_string(_M0L7initialS135, 0);
  _block_2107
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2107)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 87, 0);
  _block_2107->$0 = _M0L4dataS137;
  _block_2107->$1 = 0;
  return _block_2107;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS134) {
  int32_t _M0L6_2atmpS1091;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1091 = (int32_t)_M0L4selfS134;
  return _M0L6_2atmpS1091;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS114,
  int32_t _M0L13allocate__lenS110,
  int32_t _M0L3lenS111,
  int32_t _M0L11src__offsetS112,
  int32_t _M0L11dst__offsetS113
) {
  int32_t _if__result_2108;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS110 >= 0) {
    if (_M0L3lenS111 >= 0) {
      if (_M0L11src__offsetS112 >= 0) {
        if (_M0L11dst__offsetS113 >= 0) {
          int32_t _M0L6_2atmpS1072 = _M0L11src__offsetS112 + _M0L3lenS111;
          int32_t _M0L6_2atmpS1073;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1073
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS114);
          if (_M0L6_2atmpS1072 <= _M0L6_2atmpS1073) {
            int32_t _M0L6_2atmpS1071 = _M0L11dst__offsetS113 + _M0L3lenS111;
            _if__result_2108 = _M0L6_2atmpS1071 <= _M0L13allocate__lenS110;
          } else {
            _if__result_2108 = 0;
          }
        } else {
          _if__result_2108 = 0;
        }
      } else {
        _if__result_2108 = 0;
      }
    } else {
      _if__result_2108 = 0;
    }
  } else {
    _if__result_2108 = 0;
  }
  if (_if__result_2108) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS114, _M0L13allocate__lenS110, _M0L11src__offsetS112, _M0L11dst__offsetS113, _M0L3lenS111);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS115;
    int32_t _M0L6_2atmpS1075;
    moonbit_string_t _M0L6_2atmpS1074;
    float* _result_2109;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS115
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L13allocate__lenS110);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L11src__offsetS112);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L11dst__offsetS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L3lenS111);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1075 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS114);
    moonbit_decref_cycle_free(_M0L3srcS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L6_2atmpS1075);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1074
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS115);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS115);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2109
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1074);
    moonbit_decref_cycle_free(_M0L6_2atmpS1074);
    return _result_2109;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS120,
  int32_t _M0L13allocate__lenS116,
  int32_t _M0L3lenS117,
  int32_t _M0L11src__offsetS118,
  int32_t _M0L11dst__offsetS119
) {
  int32_t _if__result_2110;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS116 >= 0) {
    if (_M0L3lenS117 >= 0) {
      if (_M0L11src__offsetS118 >= 0) {
        if (_M0L11dst__offsetS119 >= 0) {
          int32_t _M0L6_2atmpS1077 = _M0L11src__offsetS118 + _M0L3lenS117;
          int32_t _M0L6_2atmpS1078;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1078
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS120);
          if (_M0L6_2atmpS1077 <= _M0L6_2atmpS1078) {
            int32_t _M0L6_2atmpS1076 = _M0L11dst__offsetS119 + _M0L3lenS117;
            _if__result_2110 = _M0L6_2atmpS1076 <= _M0L13allocate__lenS116;
          } else {
            _if__result_2110 = 0;
          }
        } else {
          _if__result_2110 = 0;
        }
      } else {
        _if__result_2110 = 0;
      }
    } else {
      _if__result_2110 = 0;
    }
  } else {
    _if__result_2110 = 0;
  }
  if (_if__result_2110) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS116, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS120, _M0L11src__offsetS118, _M0L11dst__offsetS119, _M0L3lenS117);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS121;
    int32_t _M0L6_2atmpS1080;
    moonbit_string_t _M0L6_2atmpS1079;
    moonbit_string_t* _result_2111;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS121
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L13allocate__lenS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L11src__offsetS118);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L11dst__offsetS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L3lenS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1080 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS120);
    moonbit_decref_cycle_free(_M0L3srcS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L6_2atmpS1080);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1079
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS121);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS121);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2111
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1079);
    moonbit_decref_cycle_free(_M0L6_2atmpS1079);
    return _result_2111;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS126,
  int32_t _M0L13allocate__lenS122,
  int32_t _M0L3lenS123,
  int32_t _M0L11src__offsetS124,
  int32_t _M0L11dst__offsetS125
) {
  int32_t _if__result_2112;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS122 >= 0) {
    if (_M0L3lenS123 >= 0) {
      if (_M0L11src__offsetS124 >= 0) {
        if (_M0L11dst__offsetS125 >= 0) {
          int32_t _M0L6_2atmpS1082 = _M0L11src__offsetS124 + _M0L3lenS123;
          int32_t _M0L6_2atmpS1083;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1083
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS126);
          if (_M0L6_2atmpS1082 <= _M0L6_2atmpS1083) {
            int32_t _M0L6_2atmpS1081 = _M0L11dst__offsetS125 + _M0L3lenS123;
            _if__result_2112 = _M0L6_2atmpS1081 <= _M0L13allocate__lenS122;
          } else {
            _if__result_2112 = 0;
          }
        } else {
          _if__result_2112 = 0;
        }
      } else {
        _if__result_2112 = 0;
      }
    } else {
      _if__result_2112 = 0;
    }
  } else {
    _if__result_2112 = 0;
  }
  if (_if__result_2112) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS122, 0, _M0L3srcS126, _M0L11src__offsetS124, _M0L11dst__offsetS125, _M0L3lenS123);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS127;
    int32_t _M0L6_2atmpS1085;
    moonbit_string_t _M0L6_2atmpS1084;
    struct _M0TUsiE** _result_2113;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS127
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L13allocate__lenS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11src__offsetS124);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11dst__offsetS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L3lenS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1085 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS126);
    moonbit_decref_cycle_free(_M0L3srcS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L6_2atmpS1085);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1084
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS127);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS127);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2113
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1084);
    moonbit_decref_cycle_free(_M0L6_2atmpS1084);
    return _result_2113;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS132,
  int32_t _M0L13allocate__lenS128,
  int32_t _M0L3lenS129,
  int32_t _M0L11src__offsetS130,
  int32_t _M0L11dst__offsetS131
) {
  int32_t _if__result_2114;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS128 >= 0) {
    if (_M0L3lenS129 >= 0) {
      if (_M0L11src__offsetS130 >= 0) {
        if (_M0L11dst__offsetS131 >= 0) {
          int32_t _M0L6_2atmpS1087 = _M0L11src__offsetS130 + _M0L3lenS129;
          int32_t _M0L6_2atmpS1088;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1088
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS132);
          if (_M0L6_2atmpS1087 <= _M0L6_2atmpS1088) {
            int32_t _M0L6_2atmpS1086 = _M0L11dst__offsetS131 + _M0L3lenS129;
            _if__result_2114 = _M0L6_2atmpS1086 <= _M0L13allocate__lenS128;
          } else {
            _if__result_2114 = 0;
          }
        } else {
          _if__result_2114 = 0;
        }
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
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS132, _M0L13allocate__lenS128, _M0L11src__offsetS130, _M0L11dst__offsetS131, _M0L3lenS129);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS133;
    int32_t _M0L6_2atmpS1090;
    moonbit_string_t _M0L6_2atmpS1089;
    int32_t* _result_2115;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS133
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L13allocate__lenS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L11src__offsetS130);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L11dst__offsetS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L3lenS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1090 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS132);
    moonbit_decref_cycle_free(_M0L3srcS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L6_2atmpS1090);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1089
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS133);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS133);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2115
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1089);
    moonbit_decref_cycle_free(_M0L6_2atmpS1089);
    return _result_2115;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS107,
  moonbit_string_t _M0L3objS106
) {
  struct _M0TPB6Logger _M0L6_2atmpS1069;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS107);
  _M0L6_2atmpS1069
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS107
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS106, _M0L6_2atmpS1069);
  if (_M0L6_2atmpS1069.$1) {
    moonbit_decref(_M0L6_2atmpS1069.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS109,
  int32_t _M0L3objS108
) {
  struct _M0TPB6Logger _M0L6_2atmpS1070;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS109);
  _M0L6_2atmpS1070
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS109
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS108, _M0L6_2atmpS1070);
  if (_M0L6_2atmpS1070.$1) {
    moonbit_decref(_M0L6_2atmpS1070.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS85,
  int32_t _M0L13allocate__lenS83,
  int32_t _M0L11src__offsetS86,
  int32_t _M0L11dst__offsetS84,
  int32_t _M0L9blit__lenS87
) {
  float* _M0L3dstS82;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS82 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS83);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS82, _M0L11dst__offsetS84, _M0L3srcS85, _M0L11src__offsetS86, _M0L9blit__lenS87);
  moonbit_decref_cycle_free(_M0L3srcS85);
  return _M0L3dstS82;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS91,
  int32_t _M0L13allocate__lenS89,
  int32_t _M0L11src__offsetS92,
  int32_t _M0L11dst__offsetS90,
  int32_t _M0L9blit__lenS93
) {
  moonbit_string_t* _M0L3dstS88;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS88
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS89, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS88, _M0L11dst__offsetS90, _M0L3srcS91, _M0L11src__offsetS92, _M0L9blit__lenS93);
  moonbit_decref_cycle_free(_M0L3srcS91);
  return _M0L3dstS88;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS97,
  int32_t _M0L13allocate__lenS95,
  int32_t _M0L11src__offsetS98,
  int32_t _M0L11dst__offsetS96,
  int32_t _M0L9blit__lenS99
) {
  struct _M0TUsiE** _M0L3dstS94;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS94
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS95, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS94, _M0L11dst__offsetS96, _M0L3srcS97, _M0L11src__offsetS98, _M0L9blit__lenS99);
  moonbit_decref_cycle_free(_M0L3srcS97);
  return _M0L3dstS94;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS103,
  int32_t _M0L13allocate__lenS101,
  int32_t _M0L11src__offsetS104,
  int32_t _M0L11dst__offsetS102,
  int32_t _M0L9blit__lenS105
) {
  int32_t* _M0L3dstS100;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS100
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS101);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS100, _M0L11dst__offsetS102, _M0L3srcS103, _M0L11src__offsetS104, _M0L9blit__lenS105);
  moonbit_decref_cycle_free(_M0L3srcS103);
  return _M0L3dstS100;
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS67,
  int32_t _M0L11dst__offsetS68,
  moonbit_string_t* _M0L3srcS69,
  int32_t _M0L11src__offsetS70,
  int32_t _M0L3lenS71
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS69);
  moonbit_incref_cycle_free(_M0L3dstS67);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS67, _M0L11dst__offsetS68, _M0L3srcS69, _M0L11src__offsetS70, _M0L3lenS71);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS72,
  int32_t _M0L11dst__offsetS73,
  struct _M0TUsiE** _M0L3srcS74,
  int32_t _M0L11src__offsetS75,
  int32_t _M0L3lenS76
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS74);
  moonbit_incref_cycle_free(_M0L3dstS72);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS72, _M0L11dst__offsetS73, _M0L3srcS74, _M0L11src__offsetS75, _M0L3lenS76);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS77,
  int32_t _M0L11dst__offsetS78,
  int32_t* _M0L3srcS79,
  int32_t _M0L11src__offsetS80,
  int32_t _M0L3lenS81
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS79);
  moonbit_incref_cycle_free(_M0L3dstS77);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS77, _M0L11dst__offsetS78, _M0L3srcS79, _M0L11src__offsetS80, _M0L3lenS81, sizeof(int32_t));
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
        int32_t _M0L6_2atmpS1024 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS1026 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS1025;
        int32_t _M0L6_2atmpS1027;
        if (
          _M0L6_2atmpS1026 < 0
          || _M0L6_2atmpS1026 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1025 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1026];
        if (
          _M0L6_2atmpS1024 < 0
          || _M0L6_2atmpS1024 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1024] = _M0L6_2atmpS1025;
        _M0L6_2atmpS1027 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS1027;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1032 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS1032;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS1028 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS1030 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS1029;
        int32_t _M0L6_2atmpS1031;
        if (
          _M0L6_2atmpS1030 < 0
          || _M0L6_2atmpS1030 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1029 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1030];
        if (
          _M0L6_2atmpS1028 < 0
          || _M0L6_2atmpS1028 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1028] = _M0L6_2atmpS1029;
        _M0L6_2atmpS1031 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS1031;
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
        int32_t _M0L6_2atmpS1033 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1035 = _M0L11src__offsetS29 + _M0L1iS30;
        float _M0L6_2atmpS1034;
        int32_t _M0L6_2atmpS1036;
        if (
          _M0L6_2atmpS1035 < 0
          || _M0L6_2atmpS1035 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1034 = (float)_M0L3srcS27[_M0L6_2atmpS1035];
        if (
          _M0L6_2atmpS1033 < 0
          || _M0L6_2atmpS1033 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1033] = _M0L6_2atmpS1034;
        _M0L6_2atmpS1036 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1036;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1041 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1041;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1037 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1039 = _M0L11src__offsetS29 + _M0L1iS33;
        float _M0L6_2atmpS1038;
        int32_t _M0L6_2atmpS1040;
        if (
          _M0L6_2atmpS1039 < 0
          || _M0L6_2atmpS1039 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1038 = (float)_M0L3srcS27[_M0L6_2atmpS1039];
        if (
          _M0L6_2atmpS1037 < 0
          || _M0L6_2atmpS1037 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1037] = _M0L6_2atmpS1038;
        _M0L6_2atmpS1040 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1040;
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
        int32_t _M0L6_2atmpS1042 = _M0L11dst__offsetS37 + _M0L1iS39;
        int32_t _M0L6_2atmpS1044 = _M0L11src__offsetS38 + _M0L1iS39;
        moonbit_string_t _M0L6_2atmpS1043;
        moonbit_string_t _M0L6_2aoldS1971;
        int32_t _M0L6_2atmpS1045;
        if (
          _M0L6_2atmpS1044 < 0
          || _M0L6_2atmpS1044 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1043 = (moonbit_string_t)_M0L3srcS36[_M0L6_2atmpS1044];
        if (
          _M0L6_2atmpS1042 < 0
          || _M0L6_2atmpS1042 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1971 = (moonbit_string_t)_M0L3dstS35[_M0L6_2atmpS1042];
        moonbit_incref_cycle_free(_M0L6_2atmpS1043);
        moonbit_decref_cycle_free(_M0L6_2aoldS1971);
        _M0L3dstS35[_M0L6_2atmpS1042] = _M0L6_2atmpS1043;
        _M0L6_2atmpS1045 = _M0L1iS39 + 1;
        _M0L1iS39 = _M0L6_2atmpS1045;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1050 = _M0L3lenS40 - 1;
    int32_t _M0L1iS42 = _M0L6_2atmpS1050;
    while (1) {
      if (_M0L1iS42 >= 0) {
        int32_t _M0L6_2atmpS1046 = _M0L11dst__offsetS37 + _M0L1iS42;
        int32_t _M0L6_2atmpS1048 = _M0L11src__offsetS38 + _M0L1iS42;
        moonbit_string_t _M0L6_2atmpS1047;
        moonbit_string_t _M0L6_2aoldS1972;
        int32_t _M0L6_2atmpS1049;
        if (
          _M0L6_2atmpS1048 < 0
          || _M0L6_2atmpS1048 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1047 = (moonbit_string_t)_M0L3srcS36[_M0L6_2atmpS1048];
        if (
          _M0L6_2atmpS1046 < 0
          || _M0L6_2atmpS1046 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1972 = (moonbit_string_t)_M0L3dstS35[_M0L6_2atmpS1046];
        moonbit_incref_cycle_free(_M0L6_2atmpS1047);
        moonbit_decref_cycle_free(_M0L6_2aoldS1972);
        _M0L3dstS35[_M0L6_2atmpS1046] = _M0L6_2atmpS1047;
        _M0L6_2atmpS1049 = _M0L1iS42 - 1;
        _M0L1iS42 = _M0L6_2atmpS1049;
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
        int32_t _M0L6_2atmpS1051 = _M0L11dst__offsetS46 + _M0L1iS48;
        int32_t _M0L6_2atmpS1053 = _M0L11src__offsetS47 + _M0L1iS48;
        struct _M0TUsiE* _M0L6_2atmpS1052;
        struct _M0TUsiE* _M0L6_2aoldS1973;
        int32_t _M0L6_2atmpS1054;
        if (
          _M0L6_2atmpS1053 < 0
          || _M0L6_2atmpS1053 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1052 = (struct _M0TUsiE*)_M0L3srcS45[_M0L6_2atmpS1053];
        if (
          _M0L6_2atmpS1051 < 0
          || _M0L6_2atmpS1051 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1973 = (struct _M0TUsiE*)_M0L3dstS44[_M0L6_2atmpS1051];
        if (_M0L6_2atmpS1052) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1052);
        }
        if (_M0L6_2aoldS1973) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1973);
        }
        _M0L3dstS44[_M0L6_2atmpS1051] = _M0L6_2atmpS1052;
        _M0L6_2atmpS1054 = _M0L1iS48 + 1;
        _M0L1iS48 = _M0L6_2atmpS1054;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1059 = _M0L3lenS49 - 1;
    int32_t _M0L1iS51 = _M0L6_2atmpS1059;
    while (1) {
      if (_M0L1iS51 >= 0) {
        int32_t _M0L6_2atmpS1055 = _M0L11dst__offsetS46 + _M0L1iS51;
        int32_t _M0L6_2atmpS1057 = _M0L11src__offsetS47 + _M0L1iS51;
        struct _M0TUsiE* _M0L6_2atmpS1056;
        struct _M0TUsiE* _M0L6_2aoldS1974;
        int32_t _M0L6_2atmpS1058;
        if (
          _M0L6_2atmpS1057 < 0
          || _M0L6_2atmpS1057 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1056 = (struct _M0TUsiE*)_M0L3srcS45[_M0L6_2atmpS1057];
        if (
          _M0L6_2atmpS1055 < 0
          || _M0L6_2atmpS1055 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1974 = (struct _M0TUsiE*)_M0L3dstS44[_M0L6_2atmpS1055];
        if (_M0L6_2atmpS1056) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1056);
        }
        if (_M0L6_2aoldS1974) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1974);
        }
        _M0L3dstS44[_M0L6_2atmpS1055] = _M0L6_2atmpS1056;
        _M0L6_2atmpS1058 = _M0L1iS51 - 1;
        _M0L1iS51 = _M0L6_2atmpS1058;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS53,
  int32_t _M0L11dst__offsetS55,
  int32_t* _M0L3srcS54,
  int32_t _M0L11src__offsetS56,
  int32_t _M0L3lenS58
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS53 == _M0L3srcS54 && _M0L11dst__offsetS55 < _M0L11src__offsetS56
  ) {
    int32_t _M0L1iS57 = 0;
    while (1) {
      if (_M0L1iS57 < _M0L3lenS58) {
        int32_t _M0L6_2atmpS1060 = _M0L11dst__offsetS55 + _M0L1iS57;
        int32_t _M0L6_2atmpS1062 = _M0L11src__offsetS56 + _M0L1iS57;
        int32_t _M0L6_2atmpS1061;
        int32_t _M0L6_2atmpS1063;
        if (
          _M0L6_2atmpS1062 < 0
          || _M0L6_2atmpS1062 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1061 = (int32_t)_M0L3srcS54[_M0L6_2atmpS1062];
        if (
          _M0L6_2atmpS1060 < 0
          || _M0L6_2atmpS1060 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS53[_M0L6_2atmpS1060] = _M0L6_2atmpS1061;
        _M0L6_2atmpS1063 = _M0L1iS57 + 1;
        _M0L1iS57 = _M0L6_2atmpS1063;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS54);
        moonbit_decref_cycle_free(_M0L3dstS53);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1068 = _M0L3lenS58 - 1;
    int32_t _M0L1iS60 = _M0L6_2atmpS1068;
    while (1) {
      if (_M0L1iS60 >= 0) {
        int32_t _M0L6_2atmpS1064 = _M0L11dst__offsetS55 + _M0L1iS60;
        int32_t _M0L6_2atmpS1066 = _M0L11src__offsetS56 + _M0L1iS60;
        int32_t _M0L6_2atmpS1065;
        int32_t _M0L6_2atmpS1067;
        if (
          _M0L6_2atmpS1066 < 0
          || _M0L6_2atmpS1066 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1065 = (int32_t)_M0L3srcS54[_M0L6_2atmpS1066];
        if (
          _M0L6_2atmpS1064 < 0
          || _M0L6_2atmpS1064 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS53[_M0L6_2atmpS1064] = _M0L6_2atmpS1065;
        _M0L6_2atmpS1067 = _M0L1iS60 - 1;
        _M0L1iS60 = _M0L6_2atmpS1067;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS54);
        moonbit_decref_cycle_free(_M0L3dstS53);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS13) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS13);
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
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_28.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_29.data);
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS994) {
  switch (Moonbit_object_tag(_M0L4_2aeS994)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_30.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS994);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_31.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1019,
  struct _M0TPB4Show _M0L8_2aparamS1018
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1017 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1019;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1017, _M0L8_2aparamS1018);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1016,
  struct _M0TPB4Show _M0L8_2aparamS1015
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1014 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1016;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1014, _M0L8_2aparamS1015);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1013,
  int32_t _M0L8_2aparamS1012
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1011 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1013;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1011, _M0L8_2aparamS1012);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1010,
  struct _M0TPC16string10StringView _M0L8_2aparamS1009
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1008 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1010;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1008, _M0L8_2aparamS1009);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1007,
  moonbit_string_t _M0L8_2aparamS1004,
  int32_t _M0L8_2aparamS1005,
  int32_t _M0L8_2aparamS1006
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1003 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1007;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1003, _M0L8_2aparamS1004, _M0L8_2aparamS1005, _M0L8_2aparamS1006);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1002,
  moonbit_string_t _M0L8_2aparamS1001
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1000 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1002;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1000, _M0L8_2aparamS1001);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2126 = 9218868437227405311ll;
  int64_t _tmp_2127;
  int64_t _tmp_2128;
  int64_t _tmp_2129;
  int64_t _tmp_2130;
  _M0FPB18double__max__value = *(double*)&_tmp_2126;
  _tmp_2127 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2127;
  _tmp_2128 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2128;
  _tmp_2129 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2129;
  _tmp_2130 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2130;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1023;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS987;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS988;
  int32_t _M0L7_2abindS989;
  struct _M0TUsiE** _M0L7_2abindS990;
  int32_t _M0L6_2acntS1979;
  int32_t _M0L2__S991;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1023
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS987
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS987)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 90, 0);
  _M0L12async__testsS987->$0 = _M0L6_2atmpS1023;
  _M0L12async__testsS987->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS988
  = _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS989 = _M0L7_2abindS988->$1;
  _M0L7_2abindS990 = _M0L7_2abindS988->$0;
  _M0L6_2acntS1979
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS988));
  if (_M0L6_2acntS1979 > 1) {
    int32_t _M0L11_2anew__cntS1980 = _M0L6_2acntS1979 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS988), _M0L11_2anew__cntS1980);
    moonbit_incref_cycle_free(_M0L7_2abindS990);
  } else if (_M0L6_2acntS1979 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS988);
  }
  _M0L2__S991 = 0;
  while (1) {
    if (_M0L2__S991 < _M0L7_2abindS989) {
      struct _M0TUsiE* _M0L3argS992 =
        (struct _M0TUsiE*)_M0L7_2abindS990[_M0L2__S991];
      moonbit_string_t _M0L6_2atmpS1020 = _M0L3argS992->$0;
      int32_t _M0L6_2atmpS1021 = _M0L3argS992->$1;
      int32_t _M0L6_2atmpS1022;
      moonbit_incref_cycle_free(_M0L6_2atmpS1020);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples25coba__net__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS987, _M0L6_2atmpS1020, _M0L6_2atmpS1021);
      moonbit_decref_cycle_free(_M0L6_2atmpS1020);
      _M0L6_2atmpS1022 = _M0L2__S991 + 1;
      _M0L2__S991 = _M0L6_2atmpS1022;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS990);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\coba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples25coba__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25coba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS987);
  moonbit_decref_cycle_free(_M0L12async__testsS987);
  moonbit_flush_cycles();
  return 0;
}