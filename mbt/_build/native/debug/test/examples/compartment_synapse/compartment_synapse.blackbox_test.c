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
struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253;

struct _M0TWRPC15error5ErrorEs;

struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TUdiE;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TP26RiantR8snn__mbt8Dendrite;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248;

struct _M0TPB4Show;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TP26RiantR8snn__mbt6Tripod;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt12BallAndStick;

struct _M0TPB8MutLocalGbE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TUddE;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt13AdExPostSpike {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
};

struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253 {
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

struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  struct _M0TP26RiantR8snn__mbt12BallAndStick* $1;
  moonbit_string_t $2;
  moonbit_string_t $3;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGiE* $8;
  struct _M0TPB5ArrayGfE* $9;
  
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

struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
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

struct _M0TP26RiantR8snn__mbt12BallAndStick {
  struct _M0TP26RiantR8snn__mbt13AdExParameter* $0;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* $1;
  struct _M0TP26RiantR8snn__mbt8Dendrite* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGfE* $8;
  struct _M0TPB5ArrayGfE* $9;
  struct _M0TPB5ArrayGfE* $10;
  struct _M0TPB5ArrayGfE* $11;
  struct _M0TPB5ArrayGfE* $12;
  float $13;
  float $14;
  float $15;
  float $16;
  float $17;
  float $18;
  int32_t $19;
  struct _M0TPB5ArrayGfE* $20;
  struct _M0TPB5ArrayGfE* $21;
  struct _M0TPB5ArrayGfE* $22;
  struct _M0TPB5ArrayGbE* $23;
  struct _M0TPB5ArrayGfE* $24;
  struct _M0TPB5ArrayGiE* $25;
  struct _M0TPB5ArrayGfE* $26;
  struct _M0TPB5ArrayGfE* $27;
  struct _M0TPB5ArrayGfE* $28;
  struct _M0TPB5ArrayGfE* $29;
  float $30;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1260(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1253(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1248(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1225(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1218(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt28forward__compartment__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*,
  float
);

int32_t _M0FP26RiantR8snn__mbt26forward__compartment__ball(
  struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall*,
  float
);

int32_t _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*,
  int32_t,
  float
);

int32_t _M0FP26RiantR8snn__mbt32apply__compartment__weight__ball(
  struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall*,
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

struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall* _M0MP26RiantR8snn__mbt22CompartmentSynapseBall6random(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt12BallAndStick*,
  moonbit_string_t,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0MP26RiantR8snn__mbt12BallAndStick3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
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

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
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

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
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

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

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

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

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
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_28 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[124]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 123, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 111, 109, 112, 97, 114, 116, 
    109, 101, 110, 116, 95, 115, 121, 110, 97, 112, 115, 101, 95, 98, 
    108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 
    111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_21 =
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
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_27 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 100, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_36 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 50, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 49, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[33]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 32, 45, 45, 
    45, 45, 45, 32, 69, 78, 68, 32, 77, 79, 79, 78, 32, 84, 69, 83, 84, 
    32, 82, 69, 83, 85, 76, 84, 32, 45, 45, 45, 45, 45, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[126]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 125, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 111, 109, 112, 97, 114, 116, 
    109, 101, 110, 116, 95, 115, 121, 110, 97, 112, 115, 101, 95, 98, 
    108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_31 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 115, 111, 
    109, 97, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_20 =
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
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_29 =
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
} const _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1260$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1260
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[162] =
  {
    sizeof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248)
    / 4, 1,
    offsetof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248, $1)
    / 4
    * 2,
    sizeof(struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253)
    / 4, 1,
    offsetof(struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    * 2, sizeof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall) / 4,
    10,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $0)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $1)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $2)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $3)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $4)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $5)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $6)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $7)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $8)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall, $9)
    / 4
    * 2, sizeof(struct _M0TP26RiantR8snn__mbt12BallAndStick) / 4, 23,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $20) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $21) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $22) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $23) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $24) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $25) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $26) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $27) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $28) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $29) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2587
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1281,
  moonbit_string_t _M0L8filenameS1250,
  int32_t _M0L5indexS1252
) {
  struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248* _closure_2640;
  struct _M0TWEu* _M0L13handle__startS1248;
  struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253* _closure_2641;
  struct _M0TWssbEu* _M0L14handle__resultS1253;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1260;
  void* _M0L11_2atry__errS1275;
  struct moonbit_result_0 _tmp_2643;
  int32_t _handle__error__result_2644;
  int32_t _M0L6_2atmpS2575;
  void* _M0L3errS1276;
  moonbit_string_t _M0L4nameS1278;
  struct _M0DTPC15error5Error139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1279;
  moonbit_string_t _M0L7_2anameS1280;
  int32_t _M0L6_2acntS2634;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1250);
  _closure_2640
  = (struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248*)moonbit_malloc(sizeof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248));
  Moonbit_object_header(_closure_2640)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2640->code
  = &_M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1248;
  _closure_2640->$0 = _M0L5indexS1252;
  _closure_2640->$1 = _M0L8filenameS1250;
  _M0L13handle__startS1248 = (struct _M0TWEu*)_closure_2640;
  moonbit_incref_cycle_free(_M0L8filenameS1250);
  _closure_2641
  = (struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253*)moonbit_malloc(sizeof(struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253));
  Moonbit_object_header(_closure_2641)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2641->code
  = &_M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1253;
  _closure_2641->$0 = _M0L5indexS1252;
  _closure_2641->$1 = _M0L8filenameS1250;
  _M0L14handle__resultS1253 = (struct _M0TWssbEu*)_closure_2641;
  _M0L17error__to__stringS1260
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1260$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2643
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1281, _M0L8filenameS1250, _M0L5indexS1252, _M0L13handle__startS1248, _M0L14handle__resultS1253, _M0L17error__to__stringS1260);
  if (_tmp_2643.tag) {
    int32_t const _M0L5_2aokS2584 = _tmp_2643.data.ok;
    _handle__error__result_2644 = _M0L5_2aokS2584;
  } else {
    void* const _M0L6_2aerrS2585 = _tmp_2643.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
    moonbit_decref_cycle_free(_M0L13handle__startS1248);
    _M0L11_2atry__errS1275 = _M0L6_2aerrS2585;
    goto join_1274;
  }
  if (_handle__error__result_2644) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
    moonbit_decref_cycle_free(_M0L13handle__startS1248);
    _M0L6_2atmpS2575 = 1;
  } else {
    struct moonbit_result_0 _tmp_2645;
    int32_t _handle__error__result_2646;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2645
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1281, _M0L8filenameS1250, _M0L5indexS1252, _M0L13handle__startS1248, _M0L14handle__resultS1253, _M0L17error__to__stringS1260);
    if (_tmp_2645.tag) {
      int32_t const _M0L5_2aokS2582 = _tmp_2645.data.ok;
      _handle__error__result_2646 = _M0L5_2aokS2582;
    } else {
      void* const _M0L6_2aerrS2583 = _tmp_2645.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
      moonbit_decref_cycle_free(_M0L13handle__startS1248);
      _M0L11_2atry__errS1275 = _M0L6_2aerrS2583;
      goto join_1274;
    }
    if (_handle__error__result_2646) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
      moonbit_decref_cycle_free(_M0L13handle__startS1248);
      _M0L6_2atmpS2575 = 1;
    } else {
      struct moonbit_result_0 _tmp_2647;
      int32_t _handle__error__result_2648;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2647
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1281, _M0L8filenameS1250, _M0L5indexS1252, _M0L13handle__startS1248, _M0L14handle__resultS1253, _M0L17error__to__stringS1260);
      if (_tmp_2647.tag) {
        int32_t const _M0L5_2aokS2580 = _tmp_2647.data.ok;
        _handle__error__result_2648 = _M0L5_2aokS2580;
      } else {
        void* const _M0L6_2aerrS2581 = _tmp_2647.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
        moonbit_decref_cycle_free(_M0L13handle__startS1248);
        _M0L11_2atry__errS1275 = _M0L6_2aerrS2581;
        goto join_1274;
      }
      if (_handle__error__result_2648) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
        moonbit_decref_cycle_free(_M0L13handle__startS1248);
        _M0L6_2atmpS2575 = 1;
      } else {
        struct moonbit_result_0 _tmp_2649;
        int32_t _handle__error__result_2650;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2649
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1281, _M0L8filenameS1250, _M0L5indexS1252, _M0L13handle__startS1248, _M0L14handle__resultS1253, _M0L17error__to__stringS1260);
        if (_tmp_2649.tag) {
          int32_t const _M0L5_2aokS2578 = _tmp_2649.data.ok;
          _handle__error__result_2650 = _M0L5_2aokS2578;
        } else {
          void* const _M0L6_2aerrS2579 = _tmp_2649.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
          moonbit_decref_cycle_free(_M0L13handle__startS1248);
          _M0L11_2atry__errS1275 = _M0L6_2aerrS2579;
          goto join_1274;
        }
        if (_handle__error__result_2650) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
          moonbit_decref_cycle_free(_M0L13handle__startS1248);
          _M0L6_2atmpS2575 = 1;
        } else {
          struct moonbit_result_0 _tmp_2651;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2651
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1281, _M0L8filenameS1250, _M0L5indexS1252, _M0L13handle__startS1248, _M0L14handle__resultS1253, _M0L17error__to__stringS1260);
          moonbit_decref_cycle_free(_M0L13handle__startS1248);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1260);
          if (_tmp_2651.tag) {
            int32_t const _M0L5_2aokS2576 = _tmp_2651.data.ok;
            _M0L6_2atmpS2575 = _M0L5_2aokS2576;
          } else {
            void* const _M0L6_2aerrS2577 = _tmp_2651.data.err;
            _M0L11_2atry__errS1275 = _M0L6_2aerrS2577;
            goto join_1274;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2575) {
    void* _M0L139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2586 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2586)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2586)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1275
    = _M0L139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2586;
    goto join_1274;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1253);
  }
  goto joinlet_2642;
  join_1274:;
  _M0L3errS1276 = _M0L11_2atry__errS1275;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1279
  = (struct _M0DTPC15error5Error139RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1276;
  _M0L7_2anameS1280 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1279->$0;
  _M0L6_2acntS2634
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1279));
  if (_M0L6_2acntS2634 > 1) {
    int32_t _M0L11_2anew__cntS2635 = _M0L6_2acntS2634 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1279), _M0L11_2anew__cntS2635);
    moonbit_incref_cycle_free(_M0L7_2anameS1280);
  } else if (_M0L6_2acntS2634 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1279);
  }
  _M0L4nameS1278 = _M0L7_2anameS1280;
  goto join_1277;
  goto joinlet_2652;
  join_1277:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1253(_M0L14handle__resultS1253, _M0L4nameS1278, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1253);
  moonbit_decref_cycle_free(_M0L4nameS1278);
  joinlet_2652:;
  joinlet_2642:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1260(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2574,
  void* _M0L3errS1261
) {
  void* _M0L1eS1263;
  moonbit_string_t _M0L1eS1265;
  moonbit_string_t _result_2655;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1261)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1266 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1261;
      moonbit_string_t _M0L4_2aeS1267 = _M0L10_2aFailureS1266->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1267);
      _M0L1eS1265 = _M0L4_2aeS1267;
      goto join_1264;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1268 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1261;
      moonbit_string_t _M0L4_2aeS1269 = _M0L15_2aInspectErrorS1268->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1269);
      _M0L1eS1265 = _M0L4_2aeS1269;
      goto join_1264;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1270 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1261;
      moonbit_string_t _M0L4_2aeS1271 = _M0L16_2aSnapshotErrorS1270->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1271);
      _M0L1eS1265 = _M0L4_2aeS1271;
      goto join_1264;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1272 =
        (struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1261;
      moonbit_string_t _M0L4_2aeS1273 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1272->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1273);
      _M0L1eS1265 = _M0L4_2aeS1273;
      goto join_1264;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1261);
      _M0L1eS1263 = _M0L3errS1261;
      goto join_1262;
      break;
    }
  }
  join_1264:;
  return _M0L1eS1265;
  join_1262:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _result_2655 = _M0FP15Error10to__string(_M0L1eS1263);
  moonbit_decref_cycle_free(_M0L1eS1263);
  return _result_2655;
}

int32_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1253(
  struct _M0TWssbEu* _M0L6_2aenvS2571,
  moonbit_string_t _M0L10__testnameS1254,
  moonbit_string_t _M0L7messageS1255,
  int32_t _M0L7skippedS1256
) {
  struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253* _M0L14_2acasted__envS2572;
  moonbit_string_t _M0L8filenameS1250;
  int32_t _M0L5indexS1252;
  moonbit_string_t _M0L10file__nameS1257;
  moonbit_string_t _M0L7messageS1258;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1259;
  moonbit_string_t _M0L6_2atmpS2573;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2572
  = (struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1253*)_M0L6_2aenvS2571;
  _M0L8filenameS1250 = _M0L14_2acasted__envS2572->$1;
  _M0L5indexS1252 = _M0L14_2acasted__envS2572->$0;
  if (!_M0L7skippedS1256 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1257
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1250, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1258
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1255, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1259
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1259, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1259, _M0L10file__nameS1257);
  moonbit_decref_cycle_free(_M0L10file__nameS1257);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1259, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1259, _M0L5indexS1252);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1259, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1259, _M0L7messageS1258);
  moonbit_decref_cycle_free(_M0L7messageS1258);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1259, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2573
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1259);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1259);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2573);
  moonbit_decref_cycle_free(_M0L6_2atmpS2573);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1248(
  struct _M0TWEu* _M0L6_2aenvS2568
) {
  struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248* _M0L14_2acasted__envS2569;
  moonbit_string_t _M0L8filenameS1250;
  int32_t _M0L5indexS1252;
  moonbit_string_t _M0L10file__nameS1249;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1251;
  moonbit_string_t _M0L6_2atmpS2570;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2569
  = (struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fcompartment__synapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1248*)_M0L6_2aenvS2568;
  _M0L8filenameS1250 = _M0L14_2acasted__envS2569->$1;
  _M0L5indexS1252 = _M0L14_2acasted__envS2569->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1249
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1250, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1251
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1251, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1251, _M0L10file__nameS1249);
  moonbit_decref_cycle_free(_M0L10file__nameS1249);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1251, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1251, _M0L5indexS1252);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1251, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2570
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1251);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1251);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2570);
  moonbit_decref_cycle_free(_M0L6_2atmpS2570);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1218;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1225;
  struct _M0TUsiE** _M0L6_2atmpS2567;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1232;
  moonbit_string_t* _M0L9cli__argsS1233;
  moonbit_string_t _M0L6_2atmpS2566;
  moonbit_string_t _M0L6_2atmpS2565;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1234;
  int32_t _M0L7_2abindS1235;
  moonbit_string_t* _M0L7_2abindS1236;
  int32_t _M0L6_2acntS2636;
  int32_t _M0L2__S1237;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1218 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1225 = 0;
  _M0L6_2atmpS2567 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1232
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1232)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1232->$0 = _M0L6_2atmpS2567;
  _M0L16file__and__indexS1232->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1233
  = _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1233)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2566 = (moonbit_string_t)_M0L9cli__argsS1233[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2566);
  moonbit_decref_cycle_free(_M0L9cli__argsS1233);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2565
  = _M0MP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2566);
  moonbit_decref_cycle_free(_M0L6_2atmpS2566);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1234
  = _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1225(_M0L51moonbit__test__driver__internal__split__mbt__stringS1225, _M0L6_2atmpS2565, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2565);
  _M0L7_2abindS1235 = _M0L10test__argsS1234->$1;
  _M0L7_2abindS1236 = _M0L10test__argsS1234->$0;
  _M0L6_2acntS2636
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1234));
  if (_M0L6_2acntS2636 > 1) {
    int32_t _M0L11_2anew__cntS2637 = _M0L6_2acntS2636 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1234), _M0L11_2anew__cntS2637);
    moonbit_incref_cycle_free(_M0L7_2abindS1236);
  } else if (_M0L6_2acntS2636 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1234);
  }
  _M0L2__S1237 = 0;
  while (1) {
    if (_M0L2__S1237 < _M0L7_2abindS1235) {
      moonbit_string_t _M0L3argS1238 =
        (moonbit_string_t)_M0L7_2abindS1236[_M0L2__S1237];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1239;
      moonbit_string_t _M0L4fileS1240;
      moonbit_string_t _M0L5rangeS1241;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1242;
      moonbit_string_t _M0L6_2atmpS2563;
      int32_t _M0L5startS1243;
      moonbit_string_t _M0L6_2atmpS2562;
      int32_t _M0L3endS1244;
      int32_t _M0L1iS1245;
      int32_t _M0L6_2atmpS2564;
      moonbit_incref_cycle_free(_M0L3argS1238);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1239
      = _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1225(_M0L51moonbit__test__driver__internal__split__mbt__stringS1225, _M0L3argS1238, 58);
      moonbit_decref_cycle_free(_M0L3argS1238);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1240
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1239, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1241
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1239, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1239);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1242
      = _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1225(_M0L51moonbit__test__driver__internal__split__mbt__stringS1225, _M0L5rangeS1241, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1241);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2563
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1242, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1243
      = _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1218(_M0L45moonbit__test__driver__internal__parse__int__S1218, _M0L6_2atmpS2563);
      moonbit_decref_cycle_free(_M0L6_2atmpS2563);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2562
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1242, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1242);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1244
      = _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1218(_M0L45moonbit__test__driver__internal__parse__int__S1218, _M0L6_2atmpS2562);
      moonbit_decref_cycle_free(_M0L6_2atmpS2562);
      _M0L1iS1245 = _M0L5startS1243;
      while (1) {
        if (_M0L1iS1245 < _M0L3endS1244) {
          struct _M0TUsiE* _M0L8_2atupleS2560;
          int32_t _M0L6_2atmpS2561;
          moonbit_incref_cycle_free(_M0L4fileS1240);
          _M0L8_2atupleS2560
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2560)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2560->$0 = _M0L4fileS1240;
          _M0L8_2atupleS2560->$1 = _M0L1iS1245;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1232, _M0L8_2atupleS2560);
          _M0L6_2atmpS2561 = _M0L1iS1245 + 1;
          _M0L1iS1245 = _M0L6_2atmpS2561;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1240);
        }
        break;
      }
      _M0L6_2atmpS2564 = _M0L2__S1237 + 1;
      _M0L2__S1237 = _M0L6_2atmpS2564;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1236);
    }
    break;
  }
  return _M0L16file__and__indexS1232;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1225(
  int32_t _M0L6_2aenvS2541,
  moonbit_string_t _M0L1sS1226,
  int32_t _M0L3sepS1227
) {
  moonbit_string_t* _M0L6_2atmpS2559;
  struct _M0TPB5ArrayGsE* _M0L3resS1228;
  struct _M0TPB8MutLocalGiE* _M0L1iS1229;
  struct _M0TPB8MutLocalGiE* _M0L5startS1230;
  int32_t _M0L3valS2554;
  int32_t _M0L6_2atmpS2555;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2559 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1228
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1228)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1228->$0 = _M0L6_2atmpS2559;
  _M0L3resS1228->$1 = 0;
  _M0L1iS1229
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1229)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1229->$0 = 0;
  _M0L5startS1230
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1230)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1230->$0 = 0;
  while (1) {
    int32_t _M0L3valS2542 = _M0L1iS1229->$0;
    int32_t _M0L6_2atmpS2543 = Moonbit_array_length(_M0L1sS1226);
    if (_M0L3valS2542 < _M0L6_2atmpS2543) {
      int32_t _M0L3valS2546 = _M0L1iS1229->$0;
      int32_t _M0L6_2atmpS2545;
      int32_t _M0L6_2atmpS2544;
      int32_t _M0L3valS2553;
      int32_t _M0L6_2atmpS2552;
      if (
        _M0L3valS2546 < 0
        || _M0L3valS2546 >= Moonbit_array_length(_M0L1sS1226)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2545 = _M0L1sS1226[_M0L3valS2546];
      _M0L6_2atmpS2544 = _M0L6_2atmpS2545;
      if (_M0L6_2atmpS2544 == _M0L3sepS1227) {
        int32_t _M0L3valS2548 = _M0L5startS1230->$0;
        int32_t _M0L3valS2549 = _M0L1iS1229->$0;
        moonbit_string_t _M0L6_2atmpS2547;
        int32_t _M0L3valS2551;
        int32_t _M0L6_2atmpS2550;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2547
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1226, _M0L3valS2548, _M0L3valS2549);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1228, _M0L6_2atmpS2547);
        _M0L3valS2551 = _M0L1iS1229->$0;
        _M0L6_2atmpS2550 = _M0L3valS2551 + 1;
        _M0L5startS1230->$0 = _M0L6_2atmpS2550;
      }
      _M0L3valS2553 = _M0L1iS1229->$0;
      _M0L6_2atmpS2552 = _M0L3valS2553 + 1;
      _M0L1iS1229->$0 = _M0L6_2atmpS2552;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1229);
    }
    break;
  }
  _M0L3valS2554 = _M0L5startS1230->$0;
  _M0L6_2atmpS2555 = Moonbit_array_length(_M0L1sS1226);
  if (_M0L3valS2554 < _M0L6_2atmpS2555) {
    int32_t _M0L3valS2557 = _M0L5startS1230->$0;
    int32_t _M0L6_2atmpS2558;
    moonbit_string_t _M0L6_2atmpS2556;
    moonbit_decref_cycle_free(_M0L5startS1230);
    _M0L6_2atmpS2558 = Moonbit_array_length(_M0L1sS1226);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2556
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1226, _M0L3valS2557, _M0L6_2atmpS2558);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1228, _M0L6_2atmpS2556);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1230);
  }
  return _M0L3resS1228;
}

int32_t _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1218(
  int32_t _M0L6_2aenvS2534,
  moonbit_string_t _M0L1sS1219
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1220;
  int32_t _M0L3lenS1221;
  int32_t _M0L7_2abindS1222;
  int32_t _M0L1iS1223;
  int32_t _result_2660;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1220
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1220)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1220->$0 = 0;
  _M0L3lenS1221 = Moonbit_array_length(_M0L1sS1219);
  _M0L7_2abindS1222 = 0;
  _M0L1iS1223 = _M0L7_2abindS1222;
  while (1) {
    if (_M0L1iS1223 < _M0L3lenS1221) {
      int32_t _M0L3valS2539 = _M0L3resS1220->$0;
      int32_t _M0L6_2atmpS2536 = _M0L3valS2539 * 10;
      int32_t _M0L6_2atmpS2538;
      int32_t _M0L6_2atmpS2537;
      int32_t _M0L6_2atmpS2535;
      int32_t _M0L6_2atmpS2540;
      if (
        _M0L1iS1223 < 0 || _M0L1iS1223 >= Moonbit_array_length(_M0L1sS1219)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2538 = _M0L1sS1219[_M0L1iS1223];
      _M0L6_2atmpS2537 = _M0L6_2atmpS2538 - 48;
      _M0L6_2atmpS2535 = _M0L6_2atmpS2536 + _M0L6_2atmpS2537;
      _M0L3resS1220->$0 = _M0L6_2atmpS2535;
      _M0L6_2atmpS2540 = _M0L1iS1223 + 1;
      _M0L1iS1223 = _M0L6_2atmpS2540;
      continue;
    }
    break;
  }
  _result_2660 = _M0L3resS1220->$0;
  moonbit_decref_cycle_free(_M0L3resS1220);
  return _result_2660;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1217
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1217);
  return _M0L4selfS1217;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1187,
  moonbit_string_t _M0L12_2adiscard__S1188,
  int32_t _M0L12_2adiscard__S1189,
  struct _M0TWEu* _M0L12_2adiscard__S1190,
  struct _M0TWssbEu* _M0L12_2adiscard__S1191,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1192
) {
  struct moonbit_result_0 _result_2661;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _result_2661.tag = 1;
  _result_2661.data.ok = 0;
  return _result_2661;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1193,
  moonbit_string_t _M0L12_2adiscard__S1194,
  int32_t _M0L12_2adiscard__S1195,
  struct _M0TWEu* _M0L12_2adiscard__S1196,
  struct _M0TWssbEu* _M0L12_2adiscard__S1197,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1198
) {
  struct moonbit_result_0 _result_2662;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _result_2662.tag = 1;
  _result_2662.data.ok = 0;
  return _result_2662;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1199,
  moonbit_string_t _M0L12_2adiscard__S1200,
  int32_t _M0L12_2adiscard__S1201,
  struct _M0TWEu* _M0L12_2adiscard__S1202,
  struct _M0TWssbEu* _M0L12_2adiscard__S1203,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1204
) {
  struct moonbit_result_0 _result_2663;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _result_2663.tag = 1;
  _result_2663.data.ok = 0;
  return _result_2663;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1205,
  moonbit_string_t _M0L12_2adiscard__S1206,
  int32_t _M0L12_2adiscard__S1207,
  struct _M0TWEu* _M0L12_2adiscard__S1208,
  struct _M0TWssbEu* _M0L12_2adiscard__S1209,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1210
) {
  struct moonbit_result_0 _result_2664;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _result_2664.tag = 1;
  _result_2664.data.ok = 0;
  return _result_2664;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1211,
  moonbit_string_t _M0L12_2adiscard__S1212,
  int32_t _M0L12_2adiscard__S1213,
  struct _M0TWEu* _M0L12_2adiscard__S1214,
  struct _M0TWssbEu* _M0L12_2adiscard__S1215,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1216
) {
  struct moonbit_result_0 _result_2665;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _result_2665.tag = 1;
  _result_2665.data.ok = 0;
  return _result_2665;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1186
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28forward__compartment__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0L1cS1150,
  float _M0L6t__nowS1160
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS2533;
  int32_t _M0L6_2atmpS2532;
  int32_t _M0L10use__delayS1149;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2531;
  int32_t _M0L6_2atmpS2530;
  int32_t _M0L8use__rhoS1151;
  #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6delaysS2533 = _M0L1cS1150->$6;
  #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS2532 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS2533);
  _M0L10use__delayS1149 = _M0L6_2atmpS2532 > 0;
  _M0L3rhoS2531 = _M0L1cS1150->$5;
  #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS2530 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2531);
  _M0L8use__rhoS1151 = _M0L6_2atmpS2530 > 0;
  if (_M0L10use__delayS1149) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2487 = _M0L1cS1150->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS2486 = _M0L3preS2487->$5;
    int32_t _M0L6n__preS1152;
    struct _M0TPB8MutLocalGiE* _M0L1jS1153;
    #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
    _M0L6n__preS1152 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2486);
    _M0L1jS1153
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1153)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1153->$0 = 0;
    while (1) {
      int32_t _M0L3valS2451 = _M0L1jS1153->$0;
      if (_M0L3valS2451 < _M0L6n__preS1152) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2454 = _M0L1cS1150->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS2452 = _M0L3preS2454->$5;
        int32_t _M0L3valS2453 = _M0L1jS1153->$0;
        int32_t _M0L3valS2485;
        int32_t _M0L6_2atmpS2484;
        #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS2452, _M0L3valS2453)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2483 =
            _M0L1cS1150->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2481 = _M0L6matrixS2483->$2;
          int32_t _M0L3valS2482 = _M0L1jS1153->$0;
          int32_t _M0L5startS1154;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2480;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2477;
          int32_t _M0L3valS2479;
          int32_t _M0L6_2atmpS2478;
          int32_t _M0L3endS1155;
          struct _M0TPB8MutLocalGiE* _M0L1sS1156;
          #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          _M0L5startS1154
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2481, _M0L3valS2482);
          _M0L6matrixS2480 = _M0L1cS1150->$4;
          _M0L6rowptrS2477 = _M0L6matrixS2480->$2;
          _M0L3valS2479 = _M0L1jS1153->$0;
          _M0L6_2atmpS2478 = _M0L3valS2479 + 1;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          _M0L3endS1155
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2477, _M0L6_2atmpS2478);
          _M0L1sS1156
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1156)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1156->$0 = _M0L5startS1154;
          while (1) {
            int32_t _M0L3valS2455 = _M0L1sS1156->$0;
            if (_M0L3valS2455 < _M0L3endS1155) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2476 =
                _M0L1cS1150->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS2474 = _M0L6matrixS2476->$3;
              int32_t _M0L3valS2475 = _M0L1sS1156->$0;
              int32_t _M0L9post__idxS1157;
              float _M0L1wS1158;
              struct _M0TPB5ArrayGfE* _M0L6delaysS2462;
              int32_t _M0L3valS2463;
              float _M0L1dS1159;
              int32_t _M0L3valS2461;
              int32_t _M0L6_2atmpS2460;
              #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
              _M0L9post__idxS1157
              = _M0MPC15array5Array2atGiE(_M0L6colptrS2474, _M0L3valS2475);
              if (_M0L8use__rhoS1151) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2470 =
                  _M0L1cS1150->$4;
                struct _M0TPB5ArrayGfE* _M0L4valsS2468 = _M0L6matrixS2470->$4;
                int32_t _M0L3valS2469 = _M0L1sS1156->$0;
                float _M0L6_2atmpS2464;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2466;
                int32_t _M0L3valS2467;
                float _M0L6_2atmpS2465;
                #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2464
                = _M0MPC15array5Array2atGfE(_M0L4valsS2468, _M0L3valS2469);
                _M0L3rhoS2466 = _M0L1cS1150->$5;
                _M0L3valS2467 = _M0L1sS1156->$0;
                #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2465
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2466, _M0L3valS2467);
                _M0L1wS1158 = _M0L6_2atmpS2464 * _M0L6_2atmpS2465;
              } else {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2473 =
                  _M0L1cS1150->$4;
                struct _M0TPB5ArrayGfE* _M0L4valsS2471 = _M0L6matrixS2473->$4;
                int32_t _M0L3valS2472 = _M0L1sS1156->$0;
                #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L1wS1158
                = _M0MPC15array5Array2atGfE(_M0L4valsS2471, _M0L3valS2472);
              }
              _M0L6delaysS2462 = _M0L1cS1150->$6;
              _M0L3valS2463 = _M0L1sS1156->$0;
              #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
              _M0L1dS1159
              = _M0MPC15array5Array2atGfE(_M0L6delaysS2462, _M0L3valS2463);
              if (_M0L1dS1159 == 0x0p+0f) {
                #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(_M0L1cS1150, _M0L9post__idxS1157, _M0L1wS1158);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS2456 =
                  _M0L1cS1150->$7;
                float _M0L6_2atmpS2457 = _M0L6t__nowS1160 + _M0L1dS1159;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS2458;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2459;
                #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS2456, _M0L6_2atmpS2457);
                _M0L14pending__postsS2458 = _M0L1cS1150->$8;
                #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS2458, _M0L9post__idxS1157);
                _M0L16pending__weightsS2459 = _M0L1cS1150->$9;
                #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS2459, _M0L1wS1158);
              }
              _M0L3valS2461 = _M0L1sS1156->$0;
              _M0L6_2atmpS2460 = _M0L3valS2461 + 1;
              _M0L1sS1156->$0 = _M0L6_2atmpS2460;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1156);
            }
            break;
          }
        }
        _M0L3valS2485 = _M0L1jS1153->$0;
        _M0L6_2atmpS2484 = _M0L3valS2485 + 1;
        _M0L1jS1153->$0 = _M0L6_2atmpS2484;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1153);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2521 = _M0L1cS1150->$2;
    struct _M0TPB5ArrayGfE* _M0L3bufS1163;
    #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
    if (
      _M0L3symS2521 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS2521)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS2521, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2521) * 2)
    ) {
      moonbit_string_t _M0L7_2abindS1164 = _M0L1cS1150->$3;
      if (
        _M0L7_2abindS1164 == (moonbit_string_t)moonbit_string_literal_12.data
        || Moonbit_array_length(_M0L7_2abindS1164) == 4
           && 0
              == memcmp(_M0L7_2abindS1164, (moonbit_string_t)moonbit_string_literal_12.data, 8)
      ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2522 =
          _M0L1cS1150->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2588 = _M0L4postS2522->$13;
        moonbit_incref_cycle_free(_M0L8_2afieldS2588);
        _M0L3bufS1163 = _M0L8_2afieldS2588;
      } else if (
               _M0L7_2abindS1164
               == (moonbit_string_t)moonbit_string_literal_11.data
               || Moonbit_array_length(_M0L7_2abindS1164) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1164, (moonbit_string_t)moonbit_string_literal_11.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2523 =
          _M0L1cS1150->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2589 = _M0L4postS2523->$15;
        moonbit_incref_cycle_free(_M0L8_2afieldS2589);
        _M0L3bufS1163 = _M0L8_2afieldS2589;
      } else if (
               _M0L7_2abindS1164
               == (moonbit_string_t)moonbit_string_literal_10.data
               || Moonbit_array_length(_M0L7_2abindS1164) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1164, (moonbit_string_t)moonbit_string_literal_10.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2524 =
          _M0L1cS1150->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2590 = _M0L4postS2524->$17;
        moonbit_incref_cycle_free(_M0L8_2afieldS2590);
        _M0L3bufS1163 = _M0L8_2afieldS2590;
      } else {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2525 =
          _M0L1cS1150->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2591 = _M0L4postS2525->$13;
        moonbit_incref_cycle_free(_M0L8_2afieldS2591);
        _M0L3bufS1163 = _M0L8_2afieldS2591;
      }
    } else {
      moonbit_string_t _M0L7_2abindS1165 = _M0L1cS1150->$3;
      if (
        _M0L7_2abindS1165 == (moonbit_string_t)moonbit_string_literal_12.data
        || Moonbit_array_length(_M0L7_2abindS1165) == 4
           && 0
              == memcmp(_M0L7_2abindS1165, (moonbit_string_t)moonbit_string_literal_12.data, 8)
      ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2526 =
          _M0L1cS1150->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2592 = _M0L4postS2526->$14;
        moonbit_incref_cycle_free(_M0L8_2afieldS2592);
        _M0L3bufS1163 = _M0L8_2afieldS2592;
      } else if (
               _M0L7_2abindS1165
               == (moonbit_string_t)moonbit_string_literal_11.data
               || Moonbit_array_length(_M0L7_2abindS1165) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1165, (moonbit_string_t)moonbit_string_literal_11.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2527 =
          _M0L1cS1150->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2593 = _M0L4postS2527->$16;
        moonbit_incref_cycle_free(_M0L8_2afieldS2593);
        _M0L3bufS1163 = _M0L8_2afieldS2593;
      } else if (
               _M0L7_2abindS1165
               == (moonbit_string_t)moonbit_string_literal_10.data
               || Moonbit_array_length(_M0L7_2abindS1165) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1165, (moonbit_string_t)moonbit_string_literal_10.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2528 =
          _M0L1cS1150->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2594 = _M0L4postS2528->$18;
        moonbit_incref_cycle_free(_M0L8_2afieldS2594);
        _M0L3bufS1163 = _M0L8_2afieldS2594;
      } else {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2529 =
          _M0L1cS1150->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2595 = _M0L4postS2529->$14;
        moonbit_incref_cycle_free(_M0L8_2afieldS2595);
        _M0L3bufS1163 = _M0L8_2afieldS2595;
      }
    }
    if (_M0L8use__rhoS1151) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2517 = _M0L1cS1150->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2516 = _M0L3preS2517->$5;
      int32_t _M0L6n__preS1166;
      struct _M0TPB8MutLocalGiE* _M0L1jS1167;
      #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      _M0L6n__preS1166 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2516);
      _M0L1jS1167
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1167)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1167->$0 = 0;
      while (1) {
        int32_t _M0L3valS2488 = _M0L1jS1167->$0;
        if (_M0L3valS2488 < _M0L6n__preS1166) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2491 = _M0L1cS1150->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS2489 = _M0L3preS2491->$5;
          int32_t _M0L3valS2490 = _M0L1jS1167->$0;
          int32_t _M0L3valS2515;
          int32_t _M0L6_2atmpS2514;
          #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS2489, _M0L3valS2490)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2513 =
              _M0L1cS1150->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2511 = _M0L6matrixS2513->$2;
            int32_t _M0L3valS2512 = _M0L1jS1167->$0;
            int32_t _M0L5startS1168;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2510;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2507;
            int32_t _M0L3valS2509;
            int32_t _M0L6_2atmpS2508;
            int32_t _M0L3endS1169;
            struct _M0TPB8MutLocalGiE* _M0L1sS1170;
            #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
            _M0L5startS1168
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2511, _M0L3valS2512);
            _M0L6matrixS2510 = _M0L1cS1150->$4;
            _M0L6rowptrS2507 = _M0L6matrixS2510->$2;
            _M0L3valS2509 = _M0L1jS1167->$0;
            _M0L6_2atmpS2508 = _M0L3valS2509 + 1;
            #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
            _M0L3endS1169
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2507, _M0L6_2atmpS2508);
            _M0L1sS1170
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1170)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1170->$0 = _M0L5startS1168;
            while (1) {
              int32_t _M0L3valS2492 = _M0L1sS1170->$0;
              if (_M0L3valS2492 < _M0L3endS1169) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2506 =
                  _M0L1cS1150->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS2504 =
                  _M0L6matrixS2506->$3;
                int32_t _M0L3valS2505 = _M0L1sS1170->$0;
                int32_t _M0L9post__idxS1171;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2503;
                struct _M0TPB5ArrayGfE* _M0L4valsS2501;
                int32_t _M0L3valS2502;
                float _M0L6_2atmpS2497;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2499;
                int32_t _M0L3valS2500;
                float _M0L6_2atmpS2498;
                float _M0L9w__scaledS1172;
                float _M0L6_2atmpS2494;
                float _M0L6_2atmpS2493;
                int32_t _M0L3valS2496;
                int32_t _M0L6_2atmpS2495;
                #line 284 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L9post__idxS1171
                = _M0MPC15array5Array2atGiE(_M0L6colptrS2504, _M0L3valS2505);
                _M0L6matrixS2503 = _M0L1cS1150->$4;
                _M0L4valsS2501 = _M0L6matrixS2503->$4;
                _M0L3valS2502 = _M0L1sS1170->$0;
                #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2497
                = _M0MPC15array5Array2atGfE(_M0L4valsS2501, _M0L3valS2502);
                _M0L3rhoS2499 = _M0L1cS1150->$5;
                _M0L3valS2500 = _M0L1sS1170->$0;
                #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2498
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2499, _M0L3valS2500);
                _M0L9w__scaledS1172 = _M0L6_2atmpS2497 * _M0L6_2atmpS2498;
                #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2494
                = _M0MPC15array5Array2atGfE(_M0L3bufS1163, _M0L9post__idxS1171);
                _M0L6_2atmpS2493 = _M0L6_2atmpS2494 + _M0L9w__scaledS1172;
                #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array3setGfE(_M0L3bufS1163, _M0L9post__idxS1171, _M0L6_2atmpS2493);
                _M0L3valS2496 = _M0L1sS1170->$0;
                _M0L6_2atmpS2495 = _M0L3valS2496 + 1;
                _M0L1sS1170->$0 = _M0L6_2atmpS2495;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1170);
              }
              break;
            }
          }
          _M0L3valS2515 = _M0L1jS1167->$0;
          _M0L6_2atmpS2514 = _M0L3valS2515 + 1;
          _M0L1jS1167->$0 = _M0L6_2atmpS2514;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1167);
          moonbit_decref_cycle_free(_M0L3bufS1163);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2518 =
        _M0L1cS1150->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2520 = _M0L1cS1150->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2519 = _M0L3preS2520->$5;
      #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2518, _M0L4fireS2519, _M0L3bufS1163);
      moonbit_decref_cycle_free(_M0L3bufS1163);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt26forward__compartment__ball(
  struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall* _M0L1cS1126,
  float _M0L6t__nowS1136
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS2450;
  int32_t _M0L6_2atmpS2449;
  int32_t _M0L10use__delayS1125;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2448;
  int32_t _M0L6_2atmpS2447;
  int32_t _M0L8use__rhoS1127;
  #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6delaysS2450 = _M0L1cS1126->$6;
  #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS2449 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS2450);
  _M0L10use__delayS1125 = _M0L6_2atmpS2449 > 0;
  _M0L3rhoS2448 = _M0L1cS1126->$5;
  #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS2447 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2448);
  _M0L8use__rhoS1127 = _M0L6_2atmpS2447 > 0;
  if (_M0L10use__delayS1125) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2406 = _M0L1cS1126->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS2405 = _M0L3preS2406->$5;
    int32_t _M0L6n__preS1128;
    struct _M0TPB8MutLocalGiE* _M0L1jS1129;
    #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
    _M0L6n__preS1128 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2405);
    _M0L1jS1129
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1129)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1129->$0 = 0;
    while (1) {
      int32_t _M0L3valS2370 = _M0L1jS1129->$0;
      if (_M0L3valS2370 < _M0L6n__preS1128) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2373 = _M0L1cS1126->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS2371 = _M0L3preS2373->$5;
        int32_t _M0L3valS2372 = _M0L1jS1129->$0;
        int32_t _M0L3valS2404;
        int32_t _M0L6_2atmpS2403;
        #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS2371, _M0L3valS2372)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2402 =
            _M0L1cS1126->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2400 = _M0L6matrixS2402->$2;
          int32_t _M0L3valS2401 = _M0L1jS1129->$0;
          int32_t _M0L5startS1130;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2399;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2396;
          int32_t _M0L3valS2398;
          int32_t _M0L6_2atmpS2397;
          int32_t _M0L3endS1131;
          struct _M0TPB8MutLocalGiE* _M0L1sS1132;
          #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          _M0L5startS1130
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2400, _M0L3valS2401);
          _M0L6matrixS2399 = _M0L1cS1126->$4;
          _M0L6rowptrS2396 = _M0L6matrixS2399->$2;
          _M0L3valS2398 = _M0L1jS1129->$0;
          _M0L6_2atmpS2397 = _M0L3valS2398 + 1;
          #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          _M0L3endS1131
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2396, _M0L6_2atmpS2397);
          _M0L1sS1132
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1132)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1132->$0 = _M0L5startS1130;
          while (1) {
            int32_t _M0L3valS2374 = _M0L1sS1132->$0;
            if (_M0L3valS2374 < _M0L3endS1131) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2395 =
                _M0L1cS1126->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS2393 = _M0L6matrixS2395->$3;
              int32_t _M0L3valS2394 = _M0L1sS1132->$0;
              int32_t _M0L9post__idxS1133;
              float _M0L1wS1134;
              struct _M0TPB5ArrayGfE* _M0L6delaysS2381;
              int32_t _M0L3valS2382;
              float _M0L1dS1135;
              int32_t _M0L3valS2380;
              int32_t _M0L6_2atmpS2379;
              #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
              _M0L9post__idxS1133
              = _M0MPC15array5Array2atGiE(_M0L6colptrS2393, _M0L3valS2394);
              if (_M0L8use__rhoS1127) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2389 =
                  _M0L1cS1126->$4;
                struct _M0TPB5ArrayGfE* _M0L4valsS2387 = _M0L6matrixS2389->$4;
                int32_t _M0L3valS2388 = _M0L1sS1132->$0;
                float _M0L6_2atmpS2383;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2385;
                int32_t _M0L3valS2386;
                float _M0L6_2atmpS2384;
                #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2383
                = _M0MPC15array5Array2atGfE(_M0L4valsS2387, _M0L3valS2388);
                _M0L3rhoS2385 = _M0L1cS1126->$5;
                _M0L3valS2386 = _M0L1sS1132->$0;
                #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2384
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2385, _M0L3valS2386);
                _M0L1wS1134 = _M0L6_2atmpS2383 * _M0L6_2atmpS2384;
              } else {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2392 =
                  _M0L1cS1126->$4;
                struct _M0TPB5ArrayGfE* _M0L4valsS2390 = _M0L6matrixS2392->$4;
                int32_t _M0L3valS2391 = _M0L1sS1132->$0;
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L1wS1134
                = _M0MPC15array5Array2atGfE(_M0L4valsS2390, _M0L3valS2391);
              }
              _M0L6delaysS2381 = _M0L1cS1126->$6;
              _M0L3valS2382 = _M0L1sS1132->$0;
              #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
              _M0L1dS1135
              = _M0MPC15array5Array2atGfE(_M0L6delaysS2381, _M0L3valS2382);
              if (_M0L1dS1135 == 0x0p+0f) {
                #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0FP26RiantR8snn__mbt32apply__compartment__weight__ball(_M0L1cS1126, _M0L9post__idxS1133, _M0L1wS1134);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS2375 =
                  _M0L1cS1126->$7;
                float _M0L6_2atmpS2376 = _M0L6t__nowS1136 + _M0L1dS1135;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS2377;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2378;
                #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS2375, _M0L6_2atmpS2376);
                _M0L14pending__postsS2377 = _M0L1cS1126->$8;
                #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS2377, _M0L9post__idxS1133);
                _M0L16pending__weightsS2378 = _M0L1cS1126->$9;
                #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS2378, _M0L1wS1134);
              }
              _M0L3valS2380 = _M0L1sS1132->$0;
              _M0L6_2atmpS2379 = _M0L3valS2380 + 1;
              _M0L1sS1132->$0 = _M0L6_2atmpS2379;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1132);
            }
            break;
          }
        }
        _M0L3valS2404 = _M0L1jS1129->$0;
        _M0L6_2atmpS2403 = _M0L3valS2404 + 1;
        _M0L1jS1129->$0 = _M0L6_2atmpS2403;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1129);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2440 = _M0L1cS1126->$2;
    struct _M0TPB5ArrayGfE* _M0L3bufS1139;
    #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
    if (
      _M0L3symS2440 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS2440)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS2440, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2440) * 2)
    ) {
      moonbit_string_t _M0L6targetS2441 = _M0L1cS1126->$3;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      if (
        _M0L6targetS2441 == (moonbit_string_t)moonbit_string_literal_12.data
        || Moonbit_array_length(_M0L6targetS2441)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_12.data)
           && 0
              == memcmp(_M0L6targetS2441, (moonbit_string_t)moonbit_string_literal_12.data, Moonbit_array_length(_M0L6targetS2441) * 2)
      ) {
        struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2442 =
          _M0L1cS1126->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2596 = _M0L4postS2442->$9;
        moonbit_incref_cycle_free(_M0L8_2afieldS2596);
        _M0L3bufS1139 = _M0L8_2afieldS2596;
      } else {
        struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2443 =
          _M0L1cS1126->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2597 = _M0L4postS2443->$11;
        moonbit_incref_cycle_free(_M0L8_2afieldS2597);
        _M0L3bufS1139 = _M0L8_2afieldS2597;
      }
    } else {
      moonbit_string_t _M0L6targetS2444 = _M0L1cS1126->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      if (
        _M0L6targetS2444 == (moonbit_string_t)moonbit_string_literal_12.data
        || Moonbit_array_length(_M0L6targetS2444)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_12.data)
           && 0
              == memcmp(_M0L6targetS2444, (moonbit_string_t)moonbit_string_literal_12.data, Moonbit_array_length(_M0L6targetS2444) * 2)
      ) {
        struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2445 =
          _M0L1cS1126->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2598 = _M0L4postS2445->$10;
        moonbit_incref_cycle_free(_M0L8_2afieldS2598);
        _M0L3bufS1139 = _M0L8_2afieldS2598;
      } else {
        struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2446 =
          _M0L1cS1126->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2599 = _M0L4postS2446->$12;
        moonbit_incref_cycle_free(_M0L8_2afieldS2599);
        _M0L3bufS1139 = _M0L8_2afieldS2599;
      }
    }
    if (_M0L8use__rhoS1127) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2436 = _M0L1cS1126->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2435 = _M0L3preS2436->$5;
      int32_t _M0L6n__preS1140;
      struct _M0TPB8MutLocalGiE* _M0L1jS1141;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      _M0L6n__preS1140 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2435);
      _M0L1jS1141
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1141)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1141->$0 = 0;
      while (1) {
        int32_t _M0L3valS2407 = _M0L1jS1141->$0;
        if (_M0L3valS2407 < _M0L6n__preS1140) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2410 = _M0L1cS1126->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS2408 = _M0L3preS2410->$5;
          int32_t _M0L3valS2409 = _M0L1jS1141->$0;
          int32_t _M0L3valS2434;
          int32_t _M0L6_2atmpS2433;
          #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS2408, _M0L3valS2409)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2432 =
              _M0L1cS1126->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2430 = _M0L6matrixS2432->$2;
            int32_t _M0L3valS2431 = _M0L1jS1141->$0;
            int32_t _M0L5startS1142;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2429;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2426;
            int32_t _M0L3valS2428;
            int32_t _M0L6_2atmpS2427;
            int32_t _M0L3endS1143;
            struct _M0TPB8MutLocalGiE* _M0L1sS1144;
            #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
            _M0L5startS1142
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2430, _M0L3valS2431);
            _M0L6matrixS2429 = _M0L1cS1126->$4;
            _M0L6rowptrS2426 = _M0L6matrixS2429->$2;
            _M0L3valS2428 = _M0L1jS1141->$0;
            _M0L6_2atmpS2427 = _M0L3valS2428 + 1;
            #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
            _M0L3endS1143
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2426, _M0L6_2atmpS2427);
            _M0L1sS1144
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1144)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1144->$0 = _M0L5startS1142;
            while (1) {
              int32_t _M0L3valS2411 = _M0L1sS1144->$0;
              if (_M0L3valS2411 < _M0L3endS1143) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2425 =
                  _M0L1cS1126->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS2423 =
                  _M0L6matrixS2425->$3;
                int32_t _M0L3valS2424 = _M0L1sS1144->$0;
                int32_t _M0L9post__idxS1145;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2422;
                struct _M0TPB5ArrayGfE* _M0L4valsS2420;
                int32_t _M0L3valS2421;
                float _M0L6_2atmpS2416;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2418;
                int32_t _M0L3valS2419;
                float _M0L6_2atmpS2417;
                float _M0L9w__scaledS1146;
                float _M0L6_2atmpS2413;
                float _M0L6_2atmpS2412;
                int32_t _M0L3valS2415;
                int32_t _M0L6_2atmpS2414;
                #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L9post__idxS1145
                = _M0MPC15array5Array2atGiE(_M0L6colptrS2423, _M0L3valS2424);
                _M0L6matrixS2422 = _M0L1cS1126->$4;
                _M0L4valsS2420 = _M0L6matrixS2422->$4;
                _M0L3valS2421 = _M0L1sS1144->$0;
                #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2416
                = _M0MPC15array5Array2atGfE(_M0L4valsS2420, _M0L3valS2421);
                _M0L3rhoS2418 = _M0L1cS1126->$5;
                _M0L3valS2419 = _M0L1sS1144->$0;
                #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2417
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2418, _M0L3valS2419);
                _M0L9w__scaledS1146 = _M0L6_2atmpS2416 * _M0L6_2atmpS2417;
                #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2413
                = _M0MPC15array5Array2atGfE(_M0L3bufS1139, _M0L9post__idxS1145);
                _M0L6_2atmpS2412 = _M0L6_2atmpS2413 + _M0L9w__scaledS1146;
                #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array3setGfE(_M0L3bufS1139, _M0L9post__idxS1145, _M0L6_2atmpS2412);
                _M0L3valS2415 = _M0L1sS1144->$0;
                _M0L6_2atmpS2414 = _M0L3valS2415 + 1;
                _M0L1sS1144->$0 = _M0L6_2atmpS2414;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1144);
              }
              break;
            }
          }
          _M0L3valS2434 = _M0L1jS1141->$0;
          _M0L6_2atmpS2433 = _M0L3valS2434 + 1;
          _M0L1jS1141->$0 = _M0L6_2atmpS2433;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1141);
          moonbit_decref_cycle_free(_M0L3bufS1139);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2437 =
        _M0L1cS1126->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2439 = _M0L1cS1126->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2438 = _M0L3preS2439->$5;
      #line 218 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2437, _M0L4fireS2438, _M0L3bufS1139);
      moonbit_decref_cycle_free(_M0L3bufS1139);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0L1cS1120,
  int32_t _M0L9post__idxS1123,
  float _M0L1wS1124
) {
  moonbit_string_t _M0L3symS2369;
  int32_t _M0L6is__geS1119;
  moonbit_string_t _M0L7_2abindS1122;
  struct _M0TPB5ArrayGfE* _M0L3bufS1121;
  float _M0L6_2atmpS2361;
  float _M0L6_2atmpS2360;
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L3symS2369 = _M0L1cS1120->$2;
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6is__geS1119
  = _M0L3symS2369 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2369)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2369, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2369) * 2);
  _M0L7_2abindS1122 = _M0L1cS1120->$3;
  if (
    _M0L7_2abindS1122 == (moonbit_string_t)moonbit_string_literal_12.data
    || Moonbit_array_length(_M0L7_2abindS1122) == 4
       && 0
          == memcmp(_M0L7_2abindS1122, (moonbit_string_t)moonbit_string_literal_12.data, 8)
  ) {
    if (_M0L6is__geS1119 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2363 = _M0L1cS1120->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2600 = _M0L4postS2363->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS2600);
      _M0L3bufS1121 = _M0L8_2afieldS2600;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2362 = _M0L1cS1120->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2601 = _M0L4postS2362->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS2601);
      _M0L3bufS1121 = _M0L8_2afieldS2601;
    }
  } else if (
           _M0L7_2abindS1122
           == (moonbit_string_t)moonbit_string_literal_11.data
           || Moonbit_array_length(_M0L7_2abindS1122) == 2
              && 0
                 == memcmp(_M0L7_2abindS1122, (moonbit_string_t)moonbit_string_literal_11.data, 4)
         ) {
    if (_M0L6is__geS1119 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2365 = _M0L1cS1120->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2602 = _M0L4postS2365->$15;
      moonbit_incref_cycle_free(_M0L8_2afieldS2602);
      _M0L3bufS1121 = _M0L8_2afieldS2602;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2364 = _M0L1cS1120->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2603 = _M0L4postS2364->$16;
      moonbit_incref_cycle_free(_M0L8_2afieldS2603);
      _M0L3bufS1121 = _M0L8_2afieldS2603;
    }
  } else if (
           _M0L7_2abindS1122
           == (moonbit_string_t)moonbit_string_literal_10.data
           || Moonbit_array_length(_M0L7_2abindS1122) == 2
              && 0
                 == memcmp(_M0L7_2abindS1122, (moonbit_string_t)moonbit_string_literal_10.data, 4)
         ) {
    if (_M0L6is__geS1119 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2367 = _M0L1cS1120->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2604 = _M0L4postS2367->$17;
      moonbit_incref_cycle_free(_M0L8_2afieldS2604);
      _M0L3bufS1121 = _M0L8_2afieldS2604;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2366 = _M0L1cS1120->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2605 = _M0L4postS2366->$18;
      moonbit_incref_cycle_free(_M0L8_2afieldS2605);
      _M0L3bufS1121 = _M0L8_2afieldS2605;
    }
  } else {
    struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2368 = _M0L1cS1120->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2606 = _M0L4postS2368->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS2606);
    _M0L3bufS1121 = _M0L8_2afieldS2606;
  }
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS2361
  = _M0MPC15array5Array2atGfE(_M0L3bufS1121, _M0L9post__idxS1123);
  _M0L6_2atmpS2360 = _M0L6_2atmpS2361 + _M0L1wS1124;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0MPC15array5Array3setGfE(_M0L3bufS1121, _M0L9post__idxS1123, _M0L6_2atmpS2360);
  moonbit_decref_cycle_free(_M0L3bufS1121);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt32apply__compartment__weight__ball(
  struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall* _M0L1cS1114,
  int32_t _M0L9post__idxS1117,
  float _M0L1wS1118
) {
  moonbit_string_t _M0L3symS2359;
  int32_t _M0L6is__geS1113;
  moonbit_string_t _M0L7_2abindS1116;
  struct _M0TPB5ArrayGfE* _M0L3bufS1115;
  float _M0L6_2atmpS2353;
  float _M0L6_2atmpS2352;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L3symS2359 = _M0L1cS1114->$2;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6is__geS1113
  = _M0L3symS2359 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2359)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2359, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2359) * 2);
  _M0L7_2abindS1116 = _M0L1cS1114->$3;
  if (
    _M0L7_2abindS1116 == (moonbit_string_t)moonbit_string_literal_12.data
    || Moonbit_array_length(_M0L7_2abindS1116) == 4
       && 0
          == memcmp(_M0L7_2abindS1116, (moonbit_string_t)moonbit_string_literal_12.data, 8)
  ) {
    if (_M0L6is__geS1113 == 1) {
      struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2355 =
        _M0L1cS1114->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2607 = _M0L4postS2355->$9;
      moonbit_incref_cycle_free(_M0L8_2afieldS2607);
      _M0L3bufS1115 = _M0L8_2afieldS2607;
    } else {
      struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2354 =
        _M0L1cS1114->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2608 = _M0L4postS2354->$10;
      moonbit_incref_cycle_free(_M0L8_2afieldS2608);
      _M0L3bufS1115 = _M0L8_2afieldS2608;
    }
  } else if (
           _M0L7_2abindS1116
           == (moonbit_string_t)moonbit_string_literal_13.data
           || Moonbit_array_length(_M0L7_2abindS1116) == 1
              && 0
                 == memcmp(_M0L7_2abindS1116, (moonbit_string_t)moonbit_string_literal_13.data, 2)
         ) {
    if (_M0L6is__geS1113 == 1) {
      struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2357 =
        _M0L1cS1114->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2609 = _M0L4postS2357->$11;
      moonbit_incref_cycle_free(_M0L8_2afieldS2609);
      _M0L3bufS1115 = _M0L8_2afieldS2609;
    } else {
      struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2356 =
        _M0L1cS1114->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2610 = _M0L4postS2356->$12;
      moonbit_incref_cycle_free(_M0L8_2afieldS2610);
      _M0L3bufS1115 = _M0L8_2afieldS2610;
    }
  } else {
    struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2358 =
      _M0L1cS1114->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2611 = _M0L4postS2358->$9;
    moonbit_incref_cycle_free(_M0L8_2afieldS2611);
    _M0L3bufS1115 = _M0L8_2afieldS2611;
  }
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS2353
  = _M0MPC15array5Array2atGfE(_M0L3bufS1115, _M0L9post__idxS1117);
  _M0L6_2atmpS2352 = _M0L6_2atmpS2353 + _M0L1wS1118;
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0MPC15array5Array3setGfE(_M0L3bufS1115, _M0L9post__idxS1117, _M0L6_2atmpS2352);
  moonbit_decref_cycle_free(_M0L3bufS1115);
  return 0;
}

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0MP26RiantR8snn__mbt24CompartmentSynapseTripod6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1105,
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS1106,
  moonbit_string_t _M0L3symS1111,
  moonbit_string_t _M0L6targetS1112,
  float _M0L2muS1107,
  float _M0L5sigmaS1108,
  float _M0L1pS1109,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1110
) {
  int32_t _M0L1nS2350;
  int32_t _M0L1nS2351;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1104;
  float* _M0L6_2atmpS2349;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2340;
  float* _M0L6_2atmpS2348;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2341;
  float* _M0L6_2atmpS2347;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2342;
  int32_t* _M0L6_2atmpS2346;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2343;
  float* _M0L6_2atmpS2345;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2344;
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _block_2674;
  #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L1nS2350 = _M0L3preS1105->$2;
  _M0L1nS2351 = _M0L4postS1106->$25;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L1mS1104
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2350, _M0L1nS2351, _M0L2muS1107, _M0L5sigmaS1108, _M0L1pS1109, _M0L3rngS1110);
  _M0L6_2atmpS2349 = moonbit_empty_float_array;
  _M0L6_2atmpS2340
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2340)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2340->$0 = _M0L6_2atmpS2349;
  _M0L6_2atmpS2340->$1 = 0;
  _M0L6_2atmpS2348 = moonbit_empty_float_array;
  _M0L6_2atmpS2341
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2341)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2341->$0 = _M0L6_2atmpS2348;
  _M0L6_2atmpS2341->$1 = 0;
  _M0L6_2atmpS2347 = moonbit_empty_float_array;
  _M0L6_2atmpS2342
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2342)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2342->$0 = _M0L6_2atmpS2347;
  _M0L6_2atmpS2342->$1 = 0;
  _M0L6_2atmpS2346 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2343
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2343)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2343->$0 = _M0L6_2atmpS2346;
  _M0L6_2atmpS2343->$1 = 0;
  _M0L6_2atmpS2345 = moonbit_empty_float_array;
  _M0L6_2atmpS2344
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2344)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2344->$0 = _M0L6_2atmpS2345;
  _M0L6_2atmpS2344->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1105);
  moonbit_incref_cycle_free(_M0L4postS1106);
  moonbit_incref_cycle_free(_M0L3symS1111);
  moonbit_incref_cycle_free(_M0L6targetS1112);
  _block_2674
  = (struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod));
  Moonbit_object_header(_block_2674)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2674->$0 = _M0L3preS1105;
  _block_2674->$1 = _M0L4postS1106;
  _block_2674->$2 = _M0L3symS1111;
  _block_2674->$3 = _M0L6targetS1112;
  _block_2674->$4 = _M0L1mS1104;
  _block_2674->$5 = _M0L6_2atmpS2340;
  _block_2674->$6 = _M0L6_2atmpS2341;
  _block_2674->$7 = _M0L6_2atmpS2342;
  _block_2674->$8 = _M0L6_2atmpS2343;
  _block_2674->$9 = _M0L6_2atmpS2344;
  return _block_2674;
}

struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall* _M0MP26RiantR8snn__mbt22CompartmentSynapseBall6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1096,
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS1097,
  moonbit_string_t _M0L3symS1102,
  moonbit_string_t _M0L6targetS1103,
  float _M0L2muS1098,
  float _M0L5sigmaS1099,
  float _M0L1pS1100,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1101
) {
  int32_t _M0L1nS2338;
  int32_t _M0L1nS2339;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1095;
  float* _M0L6_2atmpS2337;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2328;
  float* _M0L6_2atmpS2336;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2329;
  float* _M0L6_2atmpS2335;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2330;
  int32_t* _M0L6_2atmpS2334;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2331;
  float* _M0L6_2atmpS2333;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2332;
  struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall* _block_2675;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L1nS2338 = _M0L3preS1096->$2;
  _M0L1nS2339 = _M0L4postS1097->$19;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L1mS1095
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2338, _M0L1nS2339, _M0L2muS1098, _M0L5sigmaS1099, _M0L1pS1100, _M0L3rngS1101);
  _M0L6_2atmpS2337 = moonbit_empty_float_array;
  _M0L6_2atmpS2328
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2328)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2328->$0 = _M0L6_2atmpS2337;
  _M0L6_2atmpS2328->$1 = 0;
  _M0L6_2atmpS2336 = moonbit_empty_float_array;
  _M0L6_2atmpS2329
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2329)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2329->$0 = _M0L6_2atmpS2336;
  _M0L6_2atmpS2329->$1 = 0;
  _M0L6_2atmpS2335 = moonbit_empty_float_array;
  _M0L6_2atmpS2330
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2330)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2330->$0 = _M0L6_2atmpS2335;
  _M0L6_2atmpS2330->$1 = 0;
  _M0L6_2atmpS2334 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2331
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2331)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2331->$0 = _M0L6_2atmpS2334;
  _M0L6_2atmpS2331->$1 = 0;
  _M0L6_2atmpS2333 = moonbit_empty_float_array;
  _M0L6_2atmpS2332
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2332)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2332->$0 = _M0L6_2atmpS2333;
  _M0L6_2atmpS2332->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1096);
  moonbit_incref_cycle_free(_M0L4postS1097);
  moonbit_incref_cycle_free(_M0L3symS1102);
  moonbit_incref_cycle_free(_M0L6targetS1103);
  _block_2675
  = (struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt22CompartmentSynapseBall));
  Moonbit_object_header(_block_2675)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2675->$0 = _M0L3preS1096;
  _block_2675->$1 = _M0L4postS1097;
  _block_2675->$2 = _M0L3symS1102;
  _block_2675->$3 = _M0L6targetS1103;
  _block_2675->$4 = _M0L1mS1095;
  _block_2675->$5 = _M0L6_2atmpS2328;
  _block_2675->$6 = _M0L6_2atmpS2329;
  _block_2675->$7 = _M0L6_2atmpS2330;
  _block_2675->$8 = _M0L6_2atmpS2331;
  _block_2675->$9 = _M0L6_2atmpS2332;
  return _block_2675;
}

struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0MP26RiantR8snn__mbt12BallAndStick3new(
  int32_t _M0L1nS1064,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L11soma__paramS1066,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1069
) {
  struct _M0TPB5ArrayGfE* _M0L4v__sS1063;
  float _M0L2vtS2326;
  float _M0L2vrS2327;
  float _M0L6spreadS1065;
  int32_t _M0L7_2abindS1067;
  int32_t _M0L1kS1068;
  struct _M0TPB5ArrayGfE* _M0L4w__sS1071;
  struct _M0TPB5ArrayGfE* _M0L4v__dS1072;
  int32_t _M0L7_2abindS1073;
  int32_t _M0L1kS1074;
  struct _M0TPB5ArrayGbE* _M0L4fireS1076;
  float _M0L2vtS2325;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS1077;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1078;
  struct _M0TPB5ArrayGfE* _M0L4i__sS1079;
  struct _M0TPB5ArrayGfE* _M0L4i__dS1080;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS1081;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS1082;
  struct _M0TPB5ArrayGfE* _M0L5ge__dS1083;
  struct _M0TPB5ArrayGfE* _M0L5gi__dS1084;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS1085;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS1086;
  struct _M0TPB5ArrayGfE* _M0L6glu__dS1087;
  struct _M0TPB5ArrayGfE* _M0L7gaba__dS1088;
  int32_t _M0L6total3S1089;
  struct _M0TPB5ArrayGfE* _M0L2dvS1090;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS1091;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS1092;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__dS1093;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L4dendS1094;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2324;
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _block_2678;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4v__sS1063 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  _M0L2vtS2326 = _M0L11soma__paramS1066->$2;
  _M0L2vrS2327 = _M0L11soma__paramS1066->$3;
  _M0L6spreadS1065 = _M0L2vtS2326 - _M0L2vrS2327;
  _M0L7_2abindS1067 = 0;
  _M0L1kS1068 = _M0L7_2abindS1067;
  while (1) {
    if (_M0L1kS1068 < _M0L1nS1064) {
      float _M0L2vrS2315 = _M0L11soma__paramS1066->$3;
      float _M0L6_2atmpS2317;
      float _M0L6_2atmpS2316;
      float _M0L6_2atmpS2314;
      int32_t _M0L6_2atmpS2318;
      #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2317 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1069);
      _M0L6_2atmpS2316 = _M0L6_2atmpS2317 * _M0L6spreadS1065;
      _M0L6_2atmpS2314 = _M0L2vrS2315 + _M0L6_2atmpS2316;
      #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS1063, _M0L1kS1068, _M0L6_2atmpS2314);
      _M0L6_2atmpS2318 = _M0L1kS1068 + 1;
      _M0L1kS1068 = _M0L6_2atmpS2318;
      continue;
    }
    break;
  }
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4w__sS1071 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4v__dS1072 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  _M0L7_2abindS1073 = 0;
  _M0L1kS1074 = _M0L7_2abindS1073;
  while (1) {
    if (_M0L1kS1074 < _M0L1nS1064) {
      float _M0L2vrS2320 = _M0L11soma__paramS1066->$3;
      float _M0L6_2atmpS2322;
      float _M0L6_2atmpS2321;
      float _M0L6_2atmpS2319;
      int32_t _M0L6_2atmpS2323;
      #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2322 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1069);
      _M0L6_2atmpS2321 = _M0L6_2atmpS2322 * _M0L6spreadS1065;
      _M0L6_2atmpS2319 = _M0L2vrS2320 + _M0L6_2atmpS2321;
      #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS1072, _M0L1kS1074, _M0L6_2atmpS2319);
      _M0L6_2atmpS2323 = _M0L1kS1074 + 1;
      _M0L1kS1074 = _M0L6_2atmpS2323;
      continue;
    }
    break;
  }
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4fireS1076 = _M0MPC15array5Array4makeGbE(_M0L1nS1064, 0);
  _M0L2vtS2325 = _M0L11soma__paramS1066->$2;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L9thresholdS1077
  = _M0MPC15array5Array4makeGfE(_M0L1nS1064, _M0L2vtS2325);
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4tabsS1078 = _M0MPC15array5Array4makeGiE(_M0L1nS1064, 1);
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4i__sS1079 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4i__dS1080 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L5ge__sS1081 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L5gi__sS1082 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L5ge__dS1083 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L5gi__dS1084 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L6glu__sS1085 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L7gaba__sS1086 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L6glu__dS1087 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L7gaba__dS1088 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  _M0L6total3S1089 = _M0L1nS1064 * 3;
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L2dvS1090 = _M0MPC15array5Array4makeGfE(_M0L6total3S1089, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L8dv__tempS1091 = _M0MPC15array5Array4makeGfE(_M0L6total3S1089, 0x0p+0f);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L12syn__curr__sS1092 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L12syn__curr__dS1093 = _M0MPC15array5Array4makeGfE(_M0L1nS1064, 0x0p+0f);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4dendS1094 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS1064);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L6_2atmpS2324 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref_cycle_free(_M0L11soma__paramS1066);
  _block_2678
  = (struct _M0TP26RiantR8snn__mbt12BallAndStick*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12BallAndStick));
  Moonbit_object_header(_block_2678)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _block_2678->$0 = _M0L11soma__paramS1066;
  _block_2678->$1 = _M0L6_2atmpS2324;
  _block_2678->$2 = _M0L4dendS1094;
  _block_2678->$3 = _M0L4i__sS1079;
  _block_2678->$4 = _M0L4i__dS1080;
  _block_2678->$5 = _M0L5ge__sS1081;
  _block_2678->$6 = _M0L5gi__sS1082;
  _block_2678->$7 = _M0L5ge__dS1083;
  _block_2678->$8 = _M0L5gi__dS1084;
  _block_2678->$9 = _M0L6glu__sS1085;
  _block_2678->$10 = _M0L7gaba__sS1086;
  _block_2678->$11 = _M0L6glu__dS1087;
  _block_2678->$12 = _M0L7gaba__dS1088;
  _block_2678->$13 = 0x0p+0f;
  _block_2678->$14 = -0x1.2cp+6f;
  _block_2678->$15 = 0x1.8p+2f;
  _block_2678->$16 = 0x1p+1f;
  _block_2678->$17 = 0x1p+0f;
  _block_2678->$18 = 0x1p+0f;
  _block_2678->$19 = _M0L1nS1064;
  _block_2678->$20 = _M0L4v__sS1063;
  _block_2678->$21 = _M0L4w__sS1071;
  _block_2678->$22 = _M0L4v__dS1072;
  _block_2678->$23 = _M0L4fireS1076;
  _block_2678->$24 = _M0L9thresholdS1077;
  _block_2678->$25 = _M0L4tabsS1078;
  _block_2678->$26 = _M0L2dvS1090;
  _block_2678->$27 = _M0L8dv__tempS1091;
  _block_2678->$28 = _M0L12syn__curr__sS1092;
  _block_2678->$29 = _M0L12syn__curr__dS1093;
  _block_2678->$30 = 0x0p+0f;
  return _block_2678;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1061;
  float _M0L2glS1062;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2679;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1061 = -0x1p+0f;
  _M0L2glS1062 = -0x1p+0f;
  _block_2679
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2679)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2679->$0 = _M0L1cS1061;
  _block_2679->$1 = _M0L2glS1062;
  _block_2679->$2 = 0x1.ep+3f;
  _block_2679->$3 = -0x1.9p+5f;
  _block_2679->$4 = -0x1.ep+5f;
  _block_2679->$5 = -0x1.18p+6f;
  _block_2679->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2679->$7 = 0x1p+1f;
  _block_2679->$8 = 0x0p+0f;
  _block_2679->$9 = 0x0p+0f;
  _block_2679->$10 = 0x0p+0f;
  return _block_2679;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1035,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1037,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1040
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1034;
  float _M0L2vtS2312;
  float _M0L2vrS2313;
  float _M0L6spreadS1036;
  int32_t _M0L7_2abindS1038;
  int32_t _M0L1kS1039;
  struct _M0TPB5ArrayGfE* _M0L1wS1042;
  struct _M0TPB5ArrayGbE* _M0L4fireS1043;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1044;
  struct _M0TPB5ArrayGfE* _M0L1iS1045;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1046;
  struct _M0TPB5ArrayGfE* _M0L2geS1047;
  struct _M0TPB5ArrayGfE* _M0L2giS1048;
  struct _M0TPB5ArrayGfE* _M0L2heS1049;
  struct _M0TPB5ArrayGfE* _M0L2hiS1050;
  struct _M0TPB5ArrayGfE* _M0L3gluS1051;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1052;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1053;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1054;
  float _M0L4e__eS1055;
  float _M0L4e__iS1056;
  float _M0L3treS1057;
  float _M0L3tdeS1058;
  float _M0L3triS1059;
  float _M0L3tdiS1060;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2311;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2681;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1034 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  _M0L2vtS2312 = _M0L5paramS1037->$3;
  _M0L2vrS2313 = _M0L5paramS1037->$4;
  _M0L6spreadS1036 = _M0L2vtS2312 - _M0L2vrS2313;
  _M0L7_2abindS1038 = 0;
  _M0L1kS1039 = _M0L7_2abindS1038;
  while (1) {
    if (_M0L1kS1039 < _M0L1nS1035) {
      float _M0L2vrS2307 = _M0L5paramS1037->$4;
      float _M0L6_2atmpS2309;
      float _M0L6_2atmpS2308;
      float _M0L6_2atmpS2306;
      int32_t _M0L6_2atmpS2310;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2309 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1040);
      _M0L6_2atmpS2308 = _M0L6_2atmpS2309 * _M0L6spreadS1036;
      _M0L6_2atmpS2306 = _M0L2vrS2307 + _M0L6_2atmpS2308;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1034, _M0L1kS1039, _M0L6_2atmpS2306);
      _M0L6_2atmpS2310 = _M0L1kS1039 + 1;
      _M0L1kS1039 = _M0L6_2atmpS2310;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1042 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1043 = _M0MPC15array5Array4makeGbE(_M0L1nS1035, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1044 = _M0MPC15array5Array4makeGiE(_M0L1nS1035, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1045 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1046 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1047 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1048 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1049 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1050 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1051 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1052 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1053 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1054 = _M0MPC15array5Array4makeGfE(_M0L1nS1035, 0x1p+0f);
  _M0L4e__eS1055 = 0x0p+0f;
  _M0L4e__iS1056 = -0x1.2cp+6f;
  _M0L3treS1057 = 0x1p+0f;
  _M0L3tdeS1058 = 0x1.8p+2f;
  _M0L3triS1059 = 0x1p-1f;
  _M0L3tdiS1060 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2311 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1037);
  _block_2681
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2681)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 73, 0);
  _block_2681->$0 = _M0L5paramS1037;
  _block_2681->$1 = _M0L6_2atmpS2311;
  _block_2681->$2 = _M0L1nS1035;
  _block_2681->$3 = _M0L1vS1034;
  _block_2681->$4 = _M0L1wS1042;
  _block_2681->$5 = _M0L4fireS1043;
  _block_2681->$6 = _M0L4tabsS1044;
  _block_2681->$7 = _M0L1iS1045;
  _block_2681->$8 = _M0L9syn__currS1046;
  _block_2681->$9 = _M0L2geS1047;
  _block_2681->$10 = _M0L2giS1048;
  _block_2681->$11 = _M0L2heS1049;
  _block_2681->$12 = _M0L2hiS1050;
  _block_2681->$13 = _M0L3gluS1051;
  _block_2681->$14 = _M0L4gabaS1052;
  _block_2681->$15 = _M0L7gsyn__eS1053;
  _block_2681->$16 = _M0L7gsyn__iS1054;
  _block_2681->$17 = _M0L4e__eS1055;
  _block_2681->$18 = _M0L4e__iS1056;
  _block_2681->$19 = _M0L3treS1057;
  _block_2681->$20 = _M0L3tdeS1058;
  _block_2681->$21 = _M0L3triS1059;
  _block_2681->$22 = _M0L3tdiS1060;
  return _block_2681;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2682;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2682
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2682)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2682->$0 = 0x1p+1f;
  return _block_2682;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS1030;
  float _M0L2glS1031;
  float _M0L2tmS1032;
  float _M0L1rS1033;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_2683;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS1030 = 0x1.19p+8f;
  _M0L2glS1031 = 0x1.4p+5f;
  _M0L2tmS1032 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS1033 = 0x1p+0f / 0x1.4p+5f;
  _block_2683
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_2683)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2683->$0 = _M0L1cS1030;
  _block_2683->$1 = _M0L2glS1031;
  _block_2683->$2 = -0x1.9p+5f;
  _block_2683->$3 = -0x1.1a66666666666p+6f;
  _block_2683->$4 = -0x1.1a66666666666p+6f;
  _block_2683->$5 = _M0L2tmS1032;
  _block_2683->$6 = _M0L1rS1033;
  _block_2683->$7 = 0x1p+1f;
  _block_2683->$8 = 0x1.2p+7f;
  _block_2683->$9 = 0x1p+2f;
  _block_2683->$10 = 0x1.42p+6f;
  return _block_2683;
}

struct _M0TP26RiantR8snn__mbt6Tripod* _M0MP26RiantR8snn__mbt6Tripod3new(
  int32_t _M0L1nS991,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L11soma__paramS993,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS996
) {
  struct _M0TPB5ArrayGfE* _M0L4v__sS990;
  float _M0L2vtS2304;
  float _M0L2vrS2305;
  float _M0L6spreadS992;
  int32_t _M0L7_2abindS994;
  int32_t _M0L1kS995;
  struct _M0TPB5ArrayGfE* _M0L4w__sS998;
  struct _M0TPB5ArrayGfE* _M0L5v__d1S999;
  struct _M0TPB5ArrayGfE* _M0L5v__d2S1000;
  int32_t _M0L7_2abindS1001;
  int32_t _M0L1kS1002;
  struct _M0TPB5ArrayGbE* _M0L4fireS1004;
  float _M0L2vtS2303;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS1005;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1006;
  struct _M0TPB5ArrayGfE* _M0L4i__sS1007;
  struct _M0TPB5ArrayGfE* _M0L5i__d1S1008;
  struct _M0TPB5ArrayGfE* _M0L5i__d2S1009;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS1010;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS1011;
  struct _M0TPB5ArrayGfE* _M0L6ge__d1S1012;
  struct _M0TPB5ArrayGfE* _M0L6gi__d1S1013;
  struct _M0TPB5ArrayGfE* _M0L6ge__d2S1014;
  struct _M0TPB5ArrayGfE* _M0L6gi__d2S1015;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS1016;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS1017;
  struct _M0TPB5ArrayGfE* _M0L7glu__d1S1018;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d1S1019;
  struct _M0TPB5ArrayGfE* _M0L7glu__d2S1020;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d2S1021;
  int32_t _M0L6total4S1022;
  struct _M0TPB5ArrayGfE* _M0L2dvS1023;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS1024;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS1025;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S1026;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S1027;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S1028;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S1029;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2302;
  struct _M0TP26RiantR8snn__mbt6Tripod* _block_2686;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4v__sS990 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  _M0L2vtS2304 = _M0L11soma__paramS993->$2;
  _M0L2vrS2305 = _M0L11soma__paramS993->$3;
  _M0L6spreadS992 = _M0L2vtS2304 - _M0L2vrS2305;
  _M0L7_2abindS994 = 0;
  _M0L1kS995 = _M0L7_2abindS994;
  while (1) {
    if (_M0L1kS995 < _M0L1nS991) {
      float _M0L2vrS2289 = _M0L11soma__paramS993->$3;
      float _M0L6_2atmpS2291;
      float _M0L6_2atmpS2290;
      float _M0L6_2atmpS2288;
      int32_t _M0L6_2atmpS2292;
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2291 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS996);
      _M0L6_2atmpS2290 = _M0L6_2atmpS2291 * _M0L6spreadS992;
      _M0L6_2atmpS2288 = _M0L2vrS2289 + _M0L6_2atmpS2290;
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS990, _M0L1kS995, _M0L6_2atmpS2288);
      _M0L6_2atmpS2292 = _M0L1kS995 + 1;
      _M0L1kS995 = _M0L6_2atmpS2292;
      continue;
    }
    break;
  }
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4w__sS998 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5v__d1S999 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5v__d2S1000 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  _M0L7_2abindS1001 = 0;
  _M0L1kS1002 = _M0L7_2abindS1001;
  while (1) {
    if (_M0L1kS1002 < _M0L1nS991) {
      float _M0L2vrS2294 = _M0L11soma__paramS993->$3;
      float _M0L6_2atmpS2296;
      float _M0L6_2atmpS2295;
      float _M0L6_2atmpS2293;
      float _M0L2vrS2298;
      float _M0L6_2atmpS2300;
      float _M0L6_2atmpS2299;
      float _M0L6_2atmpS2297;
      int32_t _M0L6_2atmpS2301;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2296 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS996);
      _M0L6_2atmpS2295 = _M0L6_2atmpS2296 * _M0L6spreadS992;
      _M0L6_2atmpS2293 = _M0L2vrS2294 + _M0L6_2atmpS2295;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S999, _M0L1kS1002, _M0L6_2atmpS2293);
      _M0L2vrS2298 = _M0L11soma__paramS993->$3;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2300 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS996);
      _M0L6_2atmpS2299 = _M0L6_2atmpS2300 * _M0L6spreadS992;
      _M0L6_2atmpS2297 = _M0L2vrS2298 + _M0L6_2atmpS2299;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S1000, _M0L1kS1002, _M0L6_2atmpS2297);
      _M0L6_2atmpS2301 = _M0L1kS1002 + 1;
      _M0L1kS1002 = _M0L6_2atmpS2301;
      continue;
    }
    break;
  }
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4fireS1004 = _M0MPC15array5Array4makeGbE(_M0L1nS991, 0);
  _M0L2vtS2303 = _M0L11soma__paramS993->$2;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L9thresholdS1005 = _M0MPC15array5Array4makeGfE(_M0L1nS991, _M0L2vtS2303);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4tabsS1006 = _M0MPC15array5Array4makeGiE(_M0L1nS991, 1);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4i__sS1007 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5i__d1S1008 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5i__d2S1009 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5ge__sS1010 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5gi__sS1011 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6ge__d1S1012 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6gi__d1S1013 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6ge__d2S1014 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6gi__d2S1015 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6glu__sS1016 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7gaba__sS1017 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7glu__d1S1018 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8gaba__d1S1019 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7glu__d2S1020 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8gaba__d2S1021 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  _M0L6total4S1022 = _M0L1nS991 * 4;
  #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2dvS1023 = _M0MPC15array5Array4makeGfE(_M0L6total4S1022, 0x0p+0f);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8dv__tempS1024 = _M0MPC15array5Array4makeGfE(_M0L6total4S1022, 0x0p+0f);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L12syn__curr__sS1025 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L13syn__curr__d1S1026 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L13syn__curr__d2S1027 = _M0MPC15array5Array4makeGfE(_M0L1nS991, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2d1S1028 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS991);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2d2S1029 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS991);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6_2atmpS2302 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref_cycle_free(_M0L11soma__paramS993);
  _block_2686
  = (struct _M0TP26RiantR8snn__mbt6Tripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt6Tripod));
  Moonbit_object_header(_block_2686)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 91, 0);
  _block_2686->$0 = _M0L11soma__paramS993;
  _block_2686->$1 = _M0L6_2atmpS2302;
  _block_2686->$2 = _M0L2d1S1028;
  _block_2686->$3 = _M0L2d2S1029;
  _block_2686->$4 = _M0L4i__sS1007;
  _block_2686->$5 = _M0L5i__d1S1008;
  _block_2686->$6 = _M0L5i__d2S1009;
  _block_2686->$7 = _M0L5ge__sS1010;
  _block_2686->$8 = _M0L5gi__sS1011;
  _block_2686->$9 = _M0L6ge__d1S1012;
  _block_2686->$10 = _M0L6gi__d1S1013;
  _block_2686->$11 = _M0L6ge__d2S1014;
  _block_2686->$12 = _M0L6gi__d2S1015;
  _block_2686->$13 = _M0L6glu__sS1016;
  _block_2686->$14 = _M0L7gaba__sS1017;
  _block_2686->$15 = _M0L7glu__d1S1018;
  _block_2686->$16 = _M0L8gaba__d1S1019;
  _block_2686->$17 = _M0L7glu__d2S1020;
  _block_2686->$18 = _M0L8gaba__d2S1021;
  _block_2686->$19 = 0x0p+0f;
  _block_2686->$20 = -0x1.2cp+6f;
  _block_2686->$21 = 0x1.8p+2f;
  _block_2686->$22 = 0x1p+1f;
  _block_2686->$23 = 0x1p+0f;
  _block_2686->$24 = 0x1p+0f;
  _block_2686->$25 = _M0L1nS991;
  _block_2686->$26 = _M0L4v__sS990;
  _block_2686->$27 = _M0L4w__sS998;
  _block_2686->$28 = _M0L5v__d1S999;
  _block_2686->$29 = _M0L5v__d2S1000;
  _block_2686->$30 = _M0L4fireS1004;
  _block_2686->$31 = _M0L9thresholdS1005;
  _block_2686->$32 = _M0L4tabsS1006;
  _block_2686->$33 = _M0L2dvS1023;
  _block_2686->$34 = _M0L8dv__tempS1024;
  _block_2686->$35 = _M0L12syn__curr__sS1025;
  _block_2686->$36 = _M0L13syn__curr__d1S1026;
  _block_2686->$37 = _M0L13syn__curr__d2S1027;
  return _block_2686;
}

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t _M0L1nS983
) {
  struct _M0TPB5ArrayGfE* _M0L2elS982;
  struct _M0TPB5ArrayGfE* _M0L1cS984;
  struct _M0TPB5ArrayGfE* _M0L3gaxS985;
  struct _M0TPB5ArrayGfE* _M0L2gmS986;
  struct _M0TPB5ArrayGfE* _M0L1lS987;
  struct _M0TPB5ArrayGfE* _M0L1dS988;
  struct _M0TPB5ArrayGfE* _M0L11gax__parentS989;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _block_2687;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2elS982
  = _M0MPC15array5Array4makeGfE(_M0L1nS983, -0x1.1a66666666666p+6f);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1cS984 = _M0MPC15array5Array4makeGfE(_M0L1nS983, 0x1.4p+3f);
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L3gaxS985 = _M0MPC15array5Array4makeGfE(_M0L1nS983, 0x1.4p+3f);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2gmS986 = _M0MPC15array5Array4makeGfE(_M0L1nS983, 0x1p+0f);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1lS987 = _M0MPC15array5Array4makeGfE(_M0L1nS983, 0x1.2cp+7f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1dS988 = _M0MPC15array5Array4makeGfE(_M0L1nS983, 0x1p+2f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L11gax__parentS989 = _M0MPC15array5Array4makeGfE(_M0L1nS983, 0x0p+0f);
  _block_2687
  = (struct _M0TP26RiantR8snn__mbt8Dendrite*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite));
  Moonbit_object_header(_block_2687)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 124, 0);
  _block_2687->$0 = _M0L1nS983;
  _block_2687->$1 = _M0L2elS982;
  _block_2687->$2 = _M0L1cS984;
  _block_2687->$3 = _M0L3gaxS985;
  _block_2687->$4 = _M0L2gmS986;
  _block_2687->$5 = _M0L1lS987;
  _block_2687->$6 = _M0L1dS988;
  _block_2687->$7 = _M0L11gax__parentS989;
  return _block_2687;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_2688;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_2688
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_2688)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2688->$0 = 0x0p+0f;
  _block_2688->$1 = 0x1.4p+3f;
  _block_2688->$2 = 0x1.4p+3f;
  _block_2688->$3 = 0x1p+0f;
  _block_2688->$4 = 0x1p+0f;
  return _block_2688;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS970,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS973,
  struct _M0TPB5ArrayGfE* _M0L7post__gS979
) {
  int32_t _M0L4rowsS969;
  int32_t _M0L7_2abindS971;
  int32_t _M0L1iS972;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS969 = _M0L1mS970->$0;
  _M0L7_2abindS971 = 0;
  _M0L1iS972 = _M0L7_2abindS971;
  while (1) {
    if (_M0L1iS972 < _M0L4rowsS969) {
      int32_t _M0L6_2atmpS2287;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS973, _M0L1iS972)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2286 = _M0L1mS970->$2;
        int32_t _M0L5startS974;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2284;
        int32_t _M0L6_2atmpS2285;
        int32_t _M0L3endS975;
        int32_t _M0L1kS976;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS974
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2286, _M0L1iS972);
        _M0L6rowptrS2284 = _M0L1mS970->$2;
        _M0L6_2atmpS2285 = _M0L1iS972 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS975
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2284, _M0L6_2atmpS2285);
        _M0L1kS976 = _M0L5startS974;
        while (1) {
          if (_M0L1kS976 < _M0L3endS975) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS2282 = _M0L1mS970->$3;
            int32_t _M0L9post__idxS977;
            struct _M0TPB5ArrayGfE* _M0L4valsS2281;
            float _M0L1wS978;
            float _M0L6_2atmpS2280;
            float _M0L6_2atmpS2279;
            int32_t _M0L6_2atmpS2283;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS977
            = _M0MPC15array5Array2atGiE(_M0L6colptrS2282, _M0L1kS976);
            _M0L4valsS2281 = _M0L1mS970->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS978
            = _M0MPC15array5Array2atGfE(_M0L4valsS2281, _M0L1kS976);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS2280
            = _M0MPC15array5Array2atGfE(_M0L7post__gS979, _M0L9post__idxS977);
            _M0L6_2atmpS2279 = _M0L6_2atmpS2280 + _M0L1wS978;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS979, _M0L9post__idxS977, _M0L6_2atmpS2279);
            _M0L6_2atmpS2283 = _M0L1kS976 + 1;
            _M0L1kS976 = _M0L6_2atmpS2283;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2287 = _M0L1iS972 + 1;
      _M0L1iS972 = _M0L6_2atmpS2287;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS963,
  int32_t _M0L4colsS964,
  float _M0L2muS965,
  float _M0L5sigmaS966,
  float _M0L1pS967,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS968
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS963, _M0L4colsS964, _M0L2muS965, _M0L5sigmaS966, _M0L1pS967, 0, _M0L3rngS968);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS877,
  int32_t _M0L4colsS881,
  float _M0L2muS887,
  float _M0L5sigmaS888,
  float _M0L1pS900,
  int32_t _M0L4ruleS894,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS890
) {
  float* _M0L6_2atmpS2278;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2277;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS876;
  int32_t _M0L7_2abindS878;
  int32_t _M0L1iS879;
  int32_t _M0L6_2atmpS2276;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS953;
  int32_t* _M0L6_2atmpS2275;
  struct _M0TPB5ArrayGiE* _M0L6colptrS954;
  float* _M0L6_2atmpS2274;
  struct _M0TPB5ArrayGfE* _M0L4valsS955;
  int32_t _M0L7_2abindS956;
  int32_t _M0L1iS957;
  int32_t _M0L6_2atmpS2273;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2710;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2278 = moonbit_empty_float_array;
  _M0L6_2atmpS2277
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2277)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2277->$0 = _M0L6_2atmpS2278;
  _M0L6_2atmpS2277->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS876
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS877, _M0L6_2atmpS2277);
  _M0L7_2abindS878 = 0;
  _M0L1iS879 = _M0L7_2abindS878;
  while (1) {
    if (_M0L1iS879 < _M0L4rowsS877) {
      struct _M0TPB5ArrayGfE* _M0L3rowS880;
      int32_t _M0L7_2abindS882;
      int32_t _M0L1jS883;
      int32_t _M0L6_2atmpS2229;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS880 = _M0MPC15array5Array4makeGfE(_M0L4colsS881, 0x0p+0f);
      _M0L7_2abindS882 = 0;
      _M0L1jS883 = _M0L7_2abindS882;
      while (1) {
        if (_M0L1jS883 < _M0L4colsS881) {
          double _M0L2z1S885;
          struct _M0TUddE* _M0L7_2abindS889;
          double _M0L5_2az1S891;
          float _M0L6_2atmpS2227;
          float _M0L6_2atmpS2226;
          float _M0L1wS886;
          int32_t _M0L6_2atmpS2228;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS889
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS890);
          _M0L5_2az1S891 = _M0L7_2abindS889->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS889);
          _M0L2z1S885 = _M0L5_2az1S891;
          goto join_884;
          goto joinlet_2693;
          join_884:;
          _M0L6_2atmpS2227 = (float)_M0L2z1S885;
          _M0L6_2atmpS2226 = _M0L5sigmaS888 * _M0L6_2atmpS2227;
          _M0L1wS886 = _M0L2muS887 + _M0L6_2atmpS2226;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS880, _M0L1jS883, _M0L1wS886);
          joinlet_2693:;
          _M0L6_2atmpS2228 = _M0L1jS883 + 1;
          _M0L1jS883 = _M0L6_2atmpS2228;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS876, _M0L1iS879, _M0L3rowS880);
      _M0L6_2atmpS2229 = _M0L1iS879 + 1;
      _M0L1iS879 = _M0L6_2atmpS2229;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS894) {
    case 0: {
      int32_t _M0L7_2abindS895 = 0;
      int32_t _M0L1iS896 = _M0L7_2abindS895;
      while (1) {
        if (_M0L1iS896 < _M0L4rowsS877) {
          int32_t _M0L7_2abindS897 = 0;
          int32_t _M0L1jS898 = _M0L7_2abindS897;
          int32_t _M0L6_2atmpS2232;
          while (1) {
            if (_M0L1jS898 < _M0L4colsS881) {
              float _M0L1uS899;
              int32_t _M0L6_2atmpS2231;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS899 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS890);
              if (_M0L1uS899 >= _M0L1pS900) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2230;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2230
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS876, _M0L1iS896);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2230, _M0L1jS898, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2230);
              }
              _M0L6_2atmpS2231 = _M0L1jS898 + 1;
              _M0L1jS898 = _M0L6_2atmpS2231;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2232 = _M0L1iS896 + 1;
          _M0L1iS896 = _M0L6_2atmpS2232;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2250 = (float)_M0L4rowsS877;
      float _M0L6_2atmpS2249 = _M0L6_2atmpS2250 * _M0L1pS900;
      int32_t _M0L7n__keepS903;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS903 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2249);
      if (_M0L7n__keepS903 > 0 && _M0L7n__keepS903 <= _M0L4rowsS877) {
        int32_t _M0L7_2abindS904 = 0;
        int32_t _M0L1jS905 = _M0L7_2abindS904;
        while (1) {
          if (_M0L1jS905 < _M0L4colsS881) {
            int32_t* _M0L6_2atmpS2244 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS906 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS907;
            int32_t _M0L1kS908;
            int32_t _M0L7n__dropS910;
            int32_t _M0L7_2abindS911;
            int32_t _M0L1kS912;
            int32_t _M0L7_2abindS918;
            int32_t _M0L1kS919;
            int32_t _M0L6_2atmpS2245;
            Moonbit_object_header(_M0L8pre__idxS906)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L8pre__idxS906->$0 = _M0L6_2atmpS2244;
            _M0L8pre__idxS906->$1 = 0;
            _M0L7_2abindS907 = 0;
            _M0L1kS908 = _M0L7_2abindS907;
            while (1) {
              if (_M0L1kS908 < _M0L4rowsS877) {
                int32_t _M0L6_2atmpS2233;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS906, _M0L1kS908);
                _M0L6_2atmpS2233 = _M0L1kS908 + 1;
                _M0L1kS908 = _M0L6_2atmpS2233;
                continue;
              }
              break;
            }
            _M0L7n__dropS910 = _M0L4rowsS877 - _M0L7n__keepS903;
            _M0L7_2abindS911 = 0;
            _M0L1kS912 = _M0L7_2abindS911;
            while (1) {
              if (_M0L1kS912 < _M0L7n__dropS910) {
                float _M0L1uS913;
                float _M0L6_2atmpS2237;
                float _M0L6_2atmpS2239;
                float _M0L6_2atmpS2238;
                float _M0L6_2atmpS2236;
                int32_t _M0L6_2atmpS2235;
                int32_t _M0L6r__idxS914;
                int32_t _M0L10r__clampedS915;
                int32_t _M0L3tmpS916;
                int32_t _M0L6_2atmpS2234;
                int32_t _M0L6_2atmpS2240;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS913 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS890);
                _M0L6_2atmpS2237 = (float)_M0L4rowsS877;
                _M0L6_2atmpS2239 = (float)_M0L1kS912;
                _M0L6_2atmpS2238 = _M0L6_2atmpS2239 * _M0L1uS913;
                _M0L6_2atmpS2236 = _M0L6_2atmpS2237 - _M0L6_2atmpS2238;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2235
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2236);
                _M0L6r__idxS914 = _M0L1kS912 + _M0L6_2atmpS2235;
                if (_M0L6r__idxS914 >= _M0L4rowsS877) {
                  _M0L10r__clampedS915 = _M0L4rowsS877 - 1;
                } else {
                  _M0L10r__clampedS915 = _M0L6r__idxS914;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS916
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS906, _M0L1kS912);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2234
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS906, _M0L10r__clampedS915);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS906, _M0L1kS912, _M0L6_2atmpS2234);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS906, _M0L10r__clampedS915, _M0L3tmpS916);
                _M0L6_2atmpS2240 = _M0L1kS912 + 1;
                _M0L1kS912 = _M0L6_2atmpS2240;
                continue;
              }
              break;
            }
            _M0L7_2abindS918 = 0;
            _M0L1kS919 = _M0L7_2abindS918;
            while (1) {
              if (_M0L1kS919 < _M0L7n__dropS910) {
                int32_t _M0L6_2atmpS2242;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2241;
                int32_t _M0L6_2atmpS2243;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2242
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS906, _M0L1kS919);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2241
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS876, _M0L6_2atmpS2242);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2241, _M0L1jS905, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2241);
                _M0L6_2atmpS2243 = _M0L1kS919 + 1;
                _M0L1kS919 = _M0L6_2atmpS2243;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS906);
              }
              break;
            }
            _M0L6_2atmpS2245 = _M0L1jS905 + 1;
            _M0L1jS905 = _M0L6_2atmpS2245;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS903 == 0) {
        int32_t _M0L7_2abindS922 = 0;
        int32_t _M0L1iS923 = _M0L7_2abindS922;
        while (1) {
          if (_M0L1iS923 < _M0L4rowsS877) {
            int32_t _M0L7_2abindS924 = 0;
            int32_t _M0L1jS925 = _M0L7_2abindS924;
            int32_t _M0L6_2atmpS2248;
            while (1) {
              if (_M0L1jS925 < _M0L4colsS881) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2246;
                int32_t _M0L6_2atmpS2247;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2246
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS876, _M0L1iS923);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2246, _M0L1jS925, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2246);
                _M0L6_2atmpS2247 = _M0L1jS925 + 1;
                _M0L1jS925 = _M0L6_2atmpS2247;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2248 = _M0L1iS923 + 1;
            _M0L1iS923 = _M0L6_2atmpS2248;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2268 = (float)_M0L4colsS881;
      float _M0L6_2atmpS2267 = _M0L6_2atmpS2268 * _M0L1pS900;
      int32_t _M0L7n__keepS928;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS928 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2267);
      if (_M0L7n__keepS928 > 0 && _M0L7n__keepS928 <= _M0L4colsS881) {
        int32_t _M0L7_2abindS929 = 0;
        int32_t _M0L1iS930 = _M0L7_2abindS929;
        while (1) {
          if (_M0L1iS930 < _M0L4rowsS877) {
            int32_t* _M0L6_2atmpS2262 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS931 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS932;
            int32_t _M0L1kS933;
            int32_t _M0L7n__dropS935;
            int32_t _M0L7_2abindS936;
            int32_t _M0L1kS937;
            int32_t _M0L7_2abindS943;
            int32_t _M0L1kS944;
            int32_t _M0L6_2atmpS2263;
            Moonbit_object_header(_M0L9post__idxS931)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L9post__idxS931->$0 = _M0L6_2atmpS2262;
            _M0L9post__idxS931->$1 = 0;
            _M0L7_2abindS932 = 0;
            _M0L1kS933 = _M0L7_2abindS932;
            while (1) {
              if (_M0L1kS933 < _M0L4colsS881) {
                int32_t _M0L6_2atmpS2251;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS931, _M0L1kS933);
                _M0L6_2atmpS2251 = _M0L1kS933 + 1;
                _M0L1kS933 = _M0L6_2atmpS2251;
                continue;
              }
              break;
            }
            _M0L7n__dropS935 = _M0L4colsS881 - _M0L7n__keepS928;
            _M0L7_2abindS936 = 0;
            _M0L1kS937 = _M0L7_2abindS936;
            while (1) {
              if (_M0L1kS937 < _M0L7n__dropS935) {
                float _M0L1uS938;
                float _M0L6_2atmpS2255;
                float _M0L6_2atmpS2257;
                float _M0L6_2atmpS2256;
                float _M0L6_2atmpS2254;
                int32_t _M0L6_2atmpS2253;
                int32_t _M0L6r__idxS939;
                int32_t _M0L10r__clampedS940;
                int32_t _M0L3tmpS941;
                int32_t _M0L6_2atmpS2252;
                int32_t _M0L6_2atmpS2258;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS938 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS890);
                _M0L6_2atmpS2255 = (float)_M0L4colsS881;
                _M0L6_2atmpS2257 = (float)_M0L1kS937;
                _M0L6_2atmpS2256 = _M0L6_2atmpS2257 * _M0L1uS938;
                _M0L6_2atmpS2254 = _M0L6_2atmpS2255 - _M0L6_2atmpS2256;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2253
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2254);
                _M0L6r__idxS939 = _M0L1kS937 + _M0L6_2atmpS2253;
                if (_M0L6r__idxS939 >= _M0L4colsS881) {
                  _M0L10r__clampedS940 = _M0L4colsS881 - 1;
                } else {
                  _M0L10r__clampedS940 = _M0L6r__idxS939;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS941
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS931, _M0L1kS937);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2252
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS931, _M0L10r__clampedS940);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS931, _M0L1kS937, _M0L6_2atmpS2252);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS931, _M0L10r__clampedS940, _M0L3tmpS941);
                _M0L6_2atmpS2258 = _M0L1kS937 + 1;
                _M0L1kS937 = _M0L6_2atmpS2258;
                continue;
              }
              break;
            }
            _M0L7_2abindS943 = 0;
            _M0L1kS944 = _M0L7_2abindS943;
            while (1) {
              if (_M0L1kS944 < _M0L7n__dropS935) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2259;
                int32_t _M0L6_2atmpS2260;
                int32_t _M0L6_2atmpS2261;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2259
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS876, _M0L1iS930);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2260
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS931, _M0L1kS944);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2259, _M0L6_2atmpS2260, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2259);
                _M0L6_2atmpS2261 = _M0L1kS944 + 1;
                _M0L1kS944 = _M0L6_2atmpS2261;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS931);
              }
              break;
            }
            _M0L6_2atmpS2263 = _M0L1iS930 + 1;
            _M0L1iS930 = _M0L6_2atmpS2263;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS928 == 0) {
        int32_t _M0L7_2abindS947 = 0;
        int32_t _M0L1iS948 = _M0L7_2abindS947;
        while (1) {
          if (_M0L1iS948 < _M0L4rowsS877) {
            int32_t _M0L7_2abindS949 = 0;
            int32_t _M0L1jS950 = _M0L7_2abindS949;
            int32_t _M0L6_2atmpS2266;
            while (1) {
              if (_M0L1jS950 < _M0L4colsS881) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2264;
                int32_t _M0L6_2atmpS2265;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2264
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS876, _M0L1iS948);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2264, _M0L1jS950, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2264);
                _M0L6_2atmpS2265 = _M0L1jS950 + 1;
                _M0L1jS950 = _M0L6_2atmpS2265;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2266 = _M0L1iS948 + 1;
            _M0L1iS948 = _M0L6_2atmpS2266;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2276 = _M0L4rowsS877 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS953 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2276, 0);
  _M0L6_2atmpS2275 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS954
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS954)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6colptrS954->$0 = _M0L6_2atmpS2275;
  _M0L6colptrS954->$1 = 0;
  _M0L6_2atmpS2274 = moonbit_empty_float_array;
  _M0L4valsS955
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS955)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS955->$0 = _M0L6_2atmpS2274;
  _M0L4valsS955->$1 = 0;
  _M0L7_2abindS956 = 0;
  _M0L1iS957 = _M0L7_2abindS956;
  while (1) {
    if (_M0L1iS957 < _M0L4rowsS877) {
      int32_t _M0L6_2atmpS2269;
      int32_t _M0L7_2abindS958;
      int32_t _M0L1jS959;
      int32_t _M0L6_2atmpS2272;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2269 = _M0MPC15array5Array6lengthGfE(_M0L4valsS955);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS953, _M0L1iS957, _M0L6_2atmpS2269);
      _M0L7_2abindS958 = 0;
      _M0L1jS959 = _M0L7_2abindS958;
      while (1) {
        if (_M0L1jS959 < _M0L4colsS881) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2270;
          float _M0L1vS960;
          int32_t _M0L6_2atmpS2271;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2270
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS876, _M0L1iS957);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS960
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2270, _M0L1jS959);
          moonbit_decref_cycle_free(_M0L6_2atmpS2270);
          if (_M0L1vS960 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS954, _M0L1jS959);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS955, _M0L1vS960);
          }
          _M0L6_2atmpS2271 = _M0L1jS959 + 1;
          _M0L1jS959 = _M0L6_2atmpS2271;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2272 = _M0L1iS957 + 1;
      _M0L1iS957 = _M0L6_2atmpS2272;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS876);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2273 = _M0MPC15array5Array6lengthGfE(_M0L4valsS955);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS953, _M0L4rowsS877, _M0L6_2atmpS2273);
  _block_2710
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2710)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 133, 0);
  _block_2710->$0 = _M0L4rowsS877;
  _block_2710->$1 = _M0L4colsS881;
  _block_2710->$2 = _M0L6rowptrS953;
  _block_2710->$3 = _M0L6colptrS954;
  _block_2710->$4 = _M0L4valsS955;
  return _block_2710;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS875
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS2225;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS2225 = _M0L1mS875->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS2225);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS873
) {
  struct _M0TUmmmmE* _M0L1sS872;
  uint64_t _M0L6_2atmpS2224;
  struct _M0TUmmmmE* _M0L1tS874;
  uint64_t _M0L6_2atmpS2220;
  uint64_t _M0L6_2atmpS2221;
  uint64_t _M0L6_2atmpS2222;
  uint64_t _M0L6_2atmpS2223;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2711;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS872 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS873);
  _M0L6_2atmpS2224 = _M0L1sS872->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS874 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2224);
  _M0L6_2atmpS2220 = _M0L1sS872->$0;
  _M0L6_2atmpS2221 = _M0L1sS872->$1;
  _M0L6_2atmpS2222 = _M0L1sS872->$2;
  moonbit_decref_cycle_free(_M0L1sS872);
  _M0L6_2atmpS2223 = _M0L1tS874->$0;
  moonbit_decref_cycle_free(_M0L1tS874);
  _block_2711
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2711)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2711->$0 = _M0L6_2atmpS2220;
  _block_2711->$1 = _M0L6_2atmpS2221;
  _block_2711->$2 = _M0L6_2atmpS2222;
  _block_2711->$3 = _M0L6_2atmpS2223;
  return _block_2711;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS864) {
  uint64_t _M0L2s1S863;
  uint64_t _M0L2z1S865;
  uint64_t _M0L2s2S866;
  uint64_t _M0L2z2S867;
  uint64_t _M0L2s3S868;
  uint64_t _M0L2z3S869;
  uint64_t _M0L2s4S870;
  uint64_t _M0L2z4S871;
  struct _M0TUmmmmE* _block_2712;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S863 = _M0L4seedS864 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S865 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S863);
  _M0L2s2S866 = _M0L2s1S863 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S867 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S866);
  _M0L2s3S868 = _M0L2s2S866 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S869 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S868);
  _M0L2s4S870 = _M0L2s3S868 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S871 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S870);
  _block_2712 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2712)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2712->$0 = _M0L2z1S865;
  _block_2712->$1 = _M0L2z2S867;
  _block_2712->$2 = _M0L2z3S869;
  _block_2712->$3 = _M0L2z4S871;
  return _block_2712;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS861) {
  uint64_t _M0L6_2atmpS2219;
  uint64_t _M0L6_2atmpS2218;
  uint64_t _M0L1zS860;
  uint64_t _M0L6_2atmpS2217;
  uint64_t _M0L6_2atmpS2216;
  uint64_t _M0L1zS862;
  uint64_t _M0L6_2atmpS2215;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2219 = _M0L1zS861 >> 30;
  _M0L6_2atmpS2218 = _M0L1zS861 ^ _M0L6_2atmpS2219;
  _M0L1zS860 = _M0L6_2atmpS2218 * 13787848793156543929ull;
  _M0L6_2atmpS2217 = _M0L1zS860 >> 27;
  _M0L6_2atmpS2216 = _M0L1zS860 ^ _M0L6_2atmpS2217;
  _M0L1zS862 = _M0L6_2atmpS2216 * 10723151780598845931ull;
  _M0L6_2atmpS2215 = _M0L1zS862 >> 31;
  return _M0L1zS862 ^ _M0L6_2atmpS2215;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS855
) {
  double _M0L2u1S854;
  double _M0L8u1__safeS856;
  double _M0L2u2S857;
  double _M0L6_2atmpS2214;
  double _M0L6_2atmpS2213;
  double _M0L1rS858;
  double _M0L5thetaS859;
  double _M0L6_2atmpS2212;
  double _M0L6_2atmpS2209;
  double _M0L6_2atmpS2211;
  double _M0L6_2atmpS2210;
  struct _M0TUddE* _block_2713;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S854 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS855);
  if (_M0L2u1S854 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS856 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS856 = _M0L2u1S854;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S857 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS855);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2214 = _M0FPC14math2ln(_M0L8u1__safeS856);
  _M0L6_2atmpS2213 = -0x1p+1 * _M0L6_2atmpS2214;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS858 = sqrt(_M0L6_2atmpS2213);
  _M0L5thetaS859 = 0x1.921fb54442d18p+2 * _M0L2u2S857;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2212 = _M0FPC14math3cos(_M0L5thetaS859);
  _M0L6_2atmpS2209 = _M0L1rS858 * _M0L6_2atmpS2212;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2211 = _M0FPC14math3sin(_M0L5thetaS859);
  _M0L6_2atmpS2210 = _M0L1rS858 * _M0L6_2atmpS2211;
  _block_2713 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2713)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2713->$0 = _M0L6_2atmpS2209;
  _block_2713->$1 = _M0L6_2atmpS2210;
  return _block_2713;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS852
) {
  uint64_t _M0L1uS851;
  uint64_t _M0L4bitsS853;
  double _M0L6_2atmpS2208;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS851 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS852);
  _M0L4bitsS853 = _M0L1uS851 >> 11;
  _M0L6_2atmpS2208 = (double)_M0L4bitsS853;
  return _M0L6_2atmpS2208 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS849
) {
  uint32_t _M0L1uS848;
  uint32_t _M0L4bitsS850;
  double _M0L6_2atmpS2207;
  double _M0L6_2atmpS2206;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS848 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS849);
  _M0L4bitsS850 = _M0L1uS848 >> 8;
  _M0L6_2atmpS2207 = (double)_M0L4bitsS850;
  _M0L6_2atmpS2206 = _M0L6_2atmpS2207 * 0x1p-24;
  return (float)_M0L6_2atmpS2206;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS847
) {
  uint64_t _M0L1uS846;
  uint64_t _M0L6_2atmpS2205;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS846 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS847);
  _M0L6_2atmpS2205 = _M0L1uS846 >> 32;
  return (uint32_t)_M0L6_2atmpS2205;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS839
) {
  uint64_t _M0L2s0S838;
  uint64_t _M0L2s1S840;
  uint64_t _M0L2s2S841;
  uint64_t _M0L2s3S842;
  uint64_t _M0L3tmpS843;
  uint64_t _M0L6_2atmpS2204;
  uint64_t _M0L3resS844;
  uint64_t _M0L1tS845;
  uint64_t _M0L6_2atmpS2194;
  uint64_t _M0L6_2atmpS2195;
  uint64_t _M0L2s2S2197;
  uint64_t _M0L6_2atmpS2196;
  uint64_t _M0L2s3S2199;
  uint64_t _M0L6_2atmpS2198;
  uint64_t _M0L2s2S2201;
  uint64_t _M0L6_2atmpS2200;
  uint64_t _M0L2s3S2203;
  uint64_t _M0L6_2atmpS2202;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S838 = _M0L1rS839->$0;
  _M0L2s1S840 = _M0L1rS839->$1;
  _M0L2s2S841 = _M0L1rS839->$2;
  _M0L2s3S842 = _M0L1rS839->$3;
  _M0L3tmpS843 = _M0L2s0S838 + _M0L2s3S842;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2204 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS843, 23);
  _M0L3resS844 = _M0L6_2atmpS2204 + _M0L2s0S838;
  _M0L1tS845 = _M0L2s1S840 << 17;
  _M0L6_2atmpS2194 = _M0L2s2S841 ^ _M0L2s0S838;
  _M0L1rS839->$2 = _M0L6_2atmpS2194;
  _M0L6_2atmpS2195 = _M0L2s3S842 ^ _M0L2s1S840;
  _M0L1rS839->$3 = _M0L6_2atmpS2195;
  _M0L2s2S2197 = _M0L1rS839->$2;
  _M0L6_2atmpS2196 = _M0L2s1S840 ^ _M0L2s2S2197;
  _M0L1rS839->$1 = _M0L6_2atmpS2196;
  _M0L2s3S2199 = _M0L1rS839->$3;
  _M0L6_2atmpS2198 = _M0L2s0S838 ^ _M0L2s3S2199;
  _M0L1rS839->$0 = _M0L6_2atmpS2198;
  _M0L2s2S2201 = _M0L1rS839->$2;
  _M0L6_2atmpS2200 = _M0L2s2S2201 ^ _M0L1tS845;
  _M0L1rS839->$2 = _M0L6_2atmpS2200;
  _M0L2s3S2203 = _M0L1rS839->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2202 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2203, 45);
  _M0L1rS839->$3 = _M0L6_2atmpS2202;
  return _M0L3resS844;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS836, int32_t _M0L1kS837) {
  uint64_t _M0L6_2atmpS2191;
  int32_t _M0L6_2atmpS2193;
  uint64_t _M0L6_2atmpS2192;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2191 = _M0L1xS836 << (_M0L1kS837 & 63);
  _M0L6_2atmpS2193 = 64 - _M0L1kS837;
  _M0L6_2atmpS2192 = _M0L1xS836 >> (_M0L6_2atmpS2193 & 63);
  return _M0L6_2atmpS2191 | _M0L6_2atmpS2192;
}

double _M0FPC14math2ln(double _M0L1xS822) {
  struct _M0TUdiE* _M0L7_2abindS823;
  double _M0L5_2af1S824;
  int32_t _M0L5_2akiS825;
  double _M0L1fS827;
  double _M0L1kS828;
  double _M0L6_2atmpS2184;
  double _M0L1sS829;
  double _M0L2s2S830;
  double _M0L2s4S831;
  double _M0L6_2atmpS2183;
  double _M0L6_2atmpS2182;
  double _M0L6_2atmpS2181;
  double _M0L6_2atmpS2180;
  double _M0L6_2atmpS2179;
  double _M0L6_2atmpS2178;
  double _M0L2t1S832;
  double _M0L6_2atmpS2177;
  double _M0L6_2atmpS2176;
  double _M0L6_2atmpS2175;
  double _M0L6_2atmpS2174;
  double _M0L2t2S833;
  double _M0L1rS834;
  double _M0L6_2atmpS2173;
  double _M0L4hfsqS835;
  double _M0L6_2atmpS2166;
  double _M0L6_2atmpS2172;
  double _M0L6_2atmpS2170;
  double _M0L6_2atmpS2171;
  double _M0L6_2atmpS2169;
  double _M0L6_2atmpS2168;
  double _M0L6_2atmpS2167;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS822 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS822)
      || _M0MPC16double6Double7is__inf(_M0L1xS822)
    ) {
      return _M0L1xS822;
    } else if (_M0L1xS822 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS823 = _M0FPC14math5frexp(_M0L1xS822);
  _M0L5_2af1S824 = _M0L7_2abindS823->$0;
  _M0L5_2akiS825 = _M0L7_2abindS823->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS823);
  if (_M0L5_2af1S824 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2188 = _M0L5_2af1S824 * 0x1p+1;
    double _M0L6_2atmpS2185 = _M0L6_2atmpS2188 - 0x1p+0;
    int32_t _M0L6_2atmpS2187 = _M0L5_2akiS825 - 1;
    double _M0L6_2atmpS2186 = (double)_M0L6_2atmpS2187;
    _M0L1fS827 = _M0L6_2atmpS2185;
    _M0L1kS828 = _M0L6_2atmpS2186;
    goto join_826;
  } else {
    double _M0L6_2atmpS2189 = _M0L5_2af1S824 - 0x1p+0;
    double _M0L6_2atmpS2190 = (double)_M0L5_2akiS825;
    _M0L1fS827 = _M0L6_2atmpS2189;
    _M0L1kS828 = _M0L6_2atmpS2190;
    goto join_826;
  }
  join_826:;
  _M0L6_2atmpS2184 = 0x1p+1 + _M0L1fS827;
  _M0L1sS829 = _M0L1fS827 / _M0L6_2atmpS2184;
  _M0L2s2S830 = _M0L1sS829 * _M0L1sS829;
  _M0L2s4S831 = _M0L2s2S830 * _M0L2s2S830;
  _M0L6_2atmpS2183 = _M0L2s4S831 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2182 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2183;
  _M0L6_2atmpS2181 = _M0L2s4S831 * _M0L6_2atmpS2182;
  _M0L6_2atmpS2180 = 0x1.2492494229359p-2 + _M0L6_2atmpS2181;
  _M0L6_2atmpS2179 = _M0L2s4S831 * _M0L6_2atmpS2180;
  _M0L6_2atmpS2178 = 0x1.5555555555593p-1 + _M0L6_2atmpS2179;
  _M0L2t1S832 = _M0L2s2S830 * _M0L6_2atmpS2178;
  _M0L6_2atmpS2177 = _M0L2s4S831 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2176 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2177;
  _M0L6_2atmpS2175 = _M0L2s4S831 * _M0L6_2atmpS2176;
  _M0L6_2atmpS2174 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2175;
  _M0L2t2S833 = _M0L2s4S831 * _M0L6_2atmpS2174;
  _M0L1rS834 = _M0L2t1S832 + _M0L2t2S833;
  _M0L6_2atmpS2173 = 0x1p-1 * _M0L1fS827;
  _M0L4hfsqS835 = _M0L6_2atmpS2173 * _M0L1fS827;
  _M0L6_2atmpS2166 = _M0L1kS828 * 0x1.62e42feep-1;
  _M0L6_2atmpS2172 = _M0L4hfsqS835 + _M0L1rS834;
  _M0L6_2atmpS2170 = _M0L1sS829 * _M0L6_2atmpS2172;
  _M0L6_2atmpS2171 = _M0L1kS828 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2169 = _M0L6_2atmpS2170 + _M0L6_2atmpS2171;
  _M0L6_2atmpS2168 = _M0L4hfsqS835 - _M0L6_2atmpS2169;
  _M0L6_2atmpS2167 = _M0L6_2atmpS2168 - _M0L1fS827;
  return _M0L6_2atmpS2166 - _M0L6_2atmpS2167;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS815) {
  struct _M0TUdiE* _M0L7_2abindS816;
  double _M0L10_2anorm__fS817;
  int32_t _M0L6_2aexpS818;
  uint64_t _M0L1uS819;
  uint64_t _M0L6_2atmpS2165;
  uint64_t _M0L6_2atmpS2164;
  int32_t _M0L6_2atmpS2163;
  int32_t _M0L6_2atmpS2162;
  int32_t _M0L3expS820;
  uint64_t _M0L6_2atmpS2161;
  uint64_t _M0L6_2atmpS2160;
  uint64_t _M0L6_2atmpS2159;
  double _M0L4fracS821;
  struct _M0TUdiE* _block_2716;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS815 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS815)
    || _M0MPC16double6Double7is__nan(_M0L1fS815)
  ) {
    struct _M0TUdiE* _block_2715 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2715)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2715->$0 = _M0L1fS815;
    _block_2715->$1 = 0;
    return _block_2715;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS816 = _M0FPC14math9normalize(_M0L1fS815);
  _M0L10_2anorm__fS817 = _M0L7_2abindS816->$0;
  _M0L6_2aexpS818 = _M0L7_2abindS816->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS816);
  _M0L1uS819 = *(int64_t*)&_M0L10_2anorm__fS817;
  _M0L6_2atmpS2165 = _M0L1uS819 >> 52;
  _M0L6_2atmpS2164 = _M0L6_2atmpS2165 & 2047ull;
  _M0L6_2atmpS2163 = (int32_t)_M0L6_2atmpS2164;
  _M0L6_2atmpS2162 = _M0L6_2aexpS818 + _M0L6_2atmpS2163;
  _M0L3expS820 = _M0L6_2atmpS2162 - 1022;
  _M0L6_2atmpS2161 = ~9218868437227405312ull;
  _M0L6_2atmpS2160 = _M0L1uS819 & _M0L6_2atmpS2161;
  _M0L6_2atmpS2159 = _M0L6_2atmpS2160 | 4602678819172646912ull;
  _M0L4fracS821 = *(double*)&_M0L6_2atmpS2159;
  _block_2716 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2716)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2716->$0 = _M0L4fracS821;
  _block_2716->$1 = _M0L3expS820;
  return _block_2716;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS814) {
  double _M0L6_2atmpS2156;
  struct _M0TUdiE* _block_2718;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2156 = fabs(_M0L1fS814);
  if (_M0L6_2atmpS2156 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2158 = (double)4503599627370496ll;
    double _M0L6_2atmpS2157 = _M0L1fS814 * _M0L6_2atmpS2158;
    struct _M0TUdiE* _block_2717 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2717)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2717->$0 = _M0L6_2atmpS2157;
    _block_2717->$1 = -52;
    return _block_2717;
  }
  _block_2718 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2718)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2718->$0 = _M0L1fS814;
  _block_2718->$1 = 0;
  return _block_2718;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS813) {
  double _M0L6_2atmpS2155;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2155 = (double)_M0L4selfS813;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2155);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS812) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS812 != _M0L4selfS812) {
    return 0;
  } else if (_M0L4selfS812 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS812 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS812;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS793,
  float _M0L4elemS795
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS792;
  int32_t _M0L1iS794;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS792 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS793);
  _M0L1iS794 = 0;
  while (1) {
    if (_M0L1iS794 < _M0L3lenS793) {
      float* _M0L3bufS2147 = _M0L3arrS792->$0;
      int32_t _M0L6_2atmpS2148;
      _M0L3bufS2147[_M0L1iS794] = _M0L4elemS795;
      _M0L6_2atmpS2148 = _M0L1iS794 + 1;
      _M0L1iS794 = _M0L6_2atmpS2148;
      continue;
    }
    break;
  }
  return _M0L3arrS792;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS798,
  int32_t _M0L4elemS800
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS797;
  int32_t _M0L1iS799;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS797 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS798);
  _M0L1iS799 = 0;
  while (1) {
    if (_M0L1iS799 < _M0L3lenS798) {
      uint8_t* _M0L3bufS2149 = _M0L3arrS797->$0;
      int32_t _M0L6_2atmpS2150;
      _M0L3bufS2149[_M0L1iS799] = _M0L4elemS800;
      _M0L6_2atmpS2150 = _M0L1iS799 + 1;
      _M0L1iS799 = _M0L6_2atmpS2150;
      continue;
    }
    break;
  }
  return _M0L3arrS797;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS803,
  int32_t _M0L4elemS805
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS802;
  int32_t _M0L1iS804;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS802 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS803);
  _M0L1iS804 = 0;
  while (1) {
    if (_M0L1iS804 < _M0L3lenS803) {
      int32_t* _M0L3bufS2151 = _M0L3arrS802->$0;
      int32_t _M0L6_2atmpS2152;
      _M0L3bufS2151[_M0L1iS804] = _M0L4elemS805;
      _M0L6_2atmpS2152 = _M0L1iS804 + 1;
      _M0L1iS804 = _M0L6_2atmpS2152;
      continue;
    }
    break;
  }
  return _M0L3arrS802;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS808,
  struct _M0TPB5ArrayGfE* _M0L4elemS810
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS807;
  int32_t _M0L1iS809;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS807
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS808);
  _M0L1iS809 = 0;
  while (1) {
    if (_M0L1iS809 < _M0L3lenS808) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2153 = _M0L3arrS807->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2612 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2153[_M0L1iS809];
      int32_t _M0L6_2atmpS2154;
      moonbit_incref_cycle_free(_M0L4elemS810);
      if (_M0L6_2aoldS2612) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2612);
      }
      _M0L3bufS2153[_M0L1iS809] = _M0L4elemS810;
      _M0L6_2atmpS2154 = _M0L1iS809 + 1;
      _M0L1iS809 = _M0L6_2atmpS2154;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS810);
    }
    break;
  }
  return _M0L3arrS807;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS777,
  int32_t _M0L5indexS778,
  int32_t _M0L5valueS779
) {
  int32_t _M0L3lenS776;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS776 = _M0L4selfS777->$1;
  if (_M0L5indexS778 >= 0 && _M0L5indexS778 < _M0L3lenS776) {
    uint8_t* _M0L6_2atmpS2143;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2143 = _M0MPC15array5Array6bufferGbE(_M0L4selfS777);
    _M0L6_2atmpS2143[_M0L5indexS778] = _M0L5valueS779;
    moonbit_decref_cycle_free(_M0L6_2atmpS2143);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
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
    float* _M0L6_2atmpS2144;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2144 = _M0MPC15array5Array6bufferGfE(_M0L4selfS781);
    _M0L6_2atmpS2144[_M0L5indexS782] = _M0L5valueS783;
    moonbit_decref_cycle_free(_M0L6_2atmpS2144);
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
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2145;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2613;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2145
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS785);
    _M0L6_2aoldS2613
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2145[_M0L5indexS786];
    if (_M0L6_2aoldS2613) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2613);
    }
    _M0L6_2atmpS2145[_M0L5indexS786] = _M0L5valueS787;
    moonbit_decref_cycle_free(_M0L6_2atmpS2145);
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
    int32_t* _M0L6_2atmpS2146;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2146 = _M0MPC15array5Array6bufferGiE(_M0L4selfS789);
    _M0L6_2atmpS2146[_M0L5indexS790] = _M0L5valueS791;
    moonbit_decref_cycle_free(_M0L6_2atmpS2146);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS762,
  int32_t _M0L5indexS763
) {
  int32_t _M0L3lenS761;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS761 = _M0L4selfS762->$1;
  if (_M0L5indexS763 >= 0 && _M0L5indexS763 < _M0L3lenS761) {
    float* _M0L6_2atmpS2138;
    float _result_2723;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2138 = _M0MPC15array5Array6bufferGfE(_M0L4selfS762);
    _result_2723 = (float)_M0L6_2atmpS2138[_M0L5indexS763];
    moonbit_decref_cycle_free(_M0L6_2atmpS2138);
    return _result_2723;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS765,
  int32_t _M0L5indexS766
) {
  int32_t _M0L3lenS764;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS764 = _M0L4selfS765->$1;
  if (_M0L5indexS766 >= 0 && _M0L5indexS766 < _M0L3lenS764) {
    uint8_t* _M0L6_2atmpS2139;
    int32_t _result_2724;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2139 = _M0MPC15array5Array6bufferGbE(_M0L4selfS765);
    _result_2724 = (int32_t)_M0L6_2atmpS2139[_M0L5indexS766];
    moonbit_decref_cycle_free(_M0L6_2atmpS2139);
    return _result_2724;
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
    int32_t* _M0L6_2atmpS2140;
    int32_t _result_2725;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2140 = _M0MPC15array5Array6bufferGiE(_M0L4selfS768);
    _result_2725 = (int32_t)_M0L6_2atmpS2140[_M0L5indexS769];
    moonbit_decref_cycle_free(_M0L6_2atmpS2140);
    return _result_2725;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS771,
  int32_t _M0L5indexS772
) {
  int32_t _M0L3lenS770;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS770 = _M0L4selfS771->$1;
  if (_M0L5indexS772 >= 0 && _M0L5indexS772 < _M0L3lenS770) {
    moonbit_string_t* _M0L6_2atmpS2141;
    moonbit_string_t _M0L6_2atmpS2614;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2141 = _M0MPC15array5Array6bufferGsE(_M0L4selfS771);
    _M0L6_2atmpS2614 = (moonbit_string_t)_M0L6_2atmpS2141[_M0L5indexS772];
    moonbit_incref_cycle_free(_M0L6_2atmpS2614);
    moonbit_decref_cycle_free(_M0L6_2atmpS2141);
    return _M0L6_2atmpS2614;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS774,
  int32_t _M0L5indexS775
) {
  int32_t _M0L3lenS773;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS773 = _M0L4selfS774->$1;
  if (_M0L5indexS775 >= 0 && _M0L5indexS775 < _M0L3lenS773) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2142;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2615;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2142
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS774);
    _M0L6_2atmpS2615
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2142[_M0L5indexS775];
    if (_M0L6_2atmpS2615) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2615);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2142);
    return _M0L6_2atmpS2615;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS760) {
  moonbit_string_t _M0L6_2atmpS2137;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2137 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS760);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2137);
  moonbit_decref_cycle_free(_M0L6_2atmpS2137);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS759) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS759);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS758) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS758 > _M0FPB18double__max__value
         || _M0L4selfS758 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS757) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS757 != _M0L4selfS757;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS742) {
  uint64_t _M0L4bitsS745;
  uint64_t _M0L6_2atmpS2136;
  uint64_t _M0L6_2atmpS2135;
  int32_t _M0L8ieeeSignS746;
  uint64_t _M0L12ieeeMantissaS747;
  uint64_t _M0L6_2atmpS2134;
  uint64_t _M0L6_2atmpS2133;
  int32_t _M0L12ieeeExponentS748;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS749;
  struct _M0TPB17FloatingDecimal64* _M0L1vS750;
  moonbit_string_t _result_2727;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS742 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L3valS742 >= -0x1p+53 && _M0L3valS742 <= 0x1p+53) {
    if (_M0L3valS742 >= -0x1p+31 && _M0L3valS742 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS743;
      double _M0L6_2atmpS2122;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS743 = _M0MPC16double6Double7to__int(_M0L3valS742);
      _M0L6_2atmpS2122 = (double)_M0L1iS743;
      if (_M0L6_2atmpS2122 == _M0L3valS742) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS743, 10);
      }
    } else {
      int64_t _M0L1iS744;
      double _M0L6_2atmpS2123;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double9to__int64(_M0L3valS742);
      _M0L6_2atmpS2123 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2123 == _M0L3valS742) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS744, 10);
      }
    }
  }
  _M0L4bitsS745 = *(int64_t*)&_M0L3valS742;
  _M0L6_2atmpS2136 = _M0L4bitsS745 >> 63;
  _M0L6_2atmpS2135 = _M0L6_2atmpS2136 & 1ull;
  _M0L8ieeeSignS746 = _M0L6_2atmpS2135 != 0ull;
  _M0L12ieeeMantissaS747 = _M0L4bitsS745 & 4503599627370495ull;
  _M0L6_2atmpS2134 = _M0L4bitsS745 >> 52;
  _M0L6_2atmpS2133 = _M0L6_2atmpS2134 & 2047ull;
  _M0L12ieeeExponentS748 = (int32_t)_M0L6_2atmpS2133;
  if (
    _M0L12ieeeExponentS748 == 2047
    || _M0L12ieeeExponentS748 == 0 && _M0L12ieeeMantissaS747 == 0ull
  ) {
    int32_t _M0L6_2atmpS2124 = _M0L12ieeeExponentS748 != 0;
    int32_t _M0L6_2atmpS2125 = _M0L12ieeeMantissaS747 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS746, _M0L6_2atmpS2124, _M0L6_2atmpS2125);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS749
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS747, _M0L12ieeeExponentS748);
  if (_M0L7_2abindS749 == 0) {
    uint32_t _M0L6_2atmpS2126;
    if (_M0L7_2abindS749) {
      moonbit_decref_cycle_free(_M0L7_2abindS749);
    }
    _M0L6_2atmpS2126 = *(uint32_t*)&_M0L12ieeeExponentS748;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS750 = _M0FPB3d2d(_M0L12ieeeMantissaS747, _M0L6_2atmpS2126);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS751 = _M0L7_2abindS749;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS752 = _M0L7_2aSomeS751;
    struct _M0TPB17FloatingDecimal64* _M0L1xS753 = _M0L4_2afS752;
    while (1) {
      uint64_t _M0L8mantissaS2132 = _M0L1xS753->$0;
      uint64_t _M0L1qS754 = _M0L8mantissaS2132 / 10ull;
      uint64_t _M0L8mantissaS2130 = _M0L1xS753->$0;
      uint64_t _M0L6_2atmpS2131 = 10ull * _M0L1qS754;
      uint64_t _M0L1rS755 = _M0L8mantissaS2130 - _M0L6_2atmpS2131;
      int32_t _M0L8exponentS2129;
      int32_t _M0L6_2atmpS2128;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2127;
      if (_M0L1rS755 != 0ull) {
        _M0L1vS750 = _M0L1xS753;
        break;
      }
      _M0L8exponentS2129 = _M0L1xS753->$1;
      moonbit_decref_cycle_free(_M0L1xS753);
      _M0L6_2atmpS2128 = _M0L8exponentS2129 + 1;
      _M0L6_2atmpS2127
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2127)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2127->$0 = _M0L1qS754;
      _M0L6_2atmpS2127->$1 = _M0L6_2atmpS2128;
      _M0L1xS753 = _M0L6_2atmpS2127;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2727 = _M0FPB9to__chars(_M0L1vS750, _M0L8ieeeSignS746);
  moonbit_decref_cycle_free(_M0L1vS750);
  return _result_2727;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS737,
  int32_t _M0L12ieeeExponentS739
) {
  uint64_t _M0L2m2S736;
  int32_t _M0L6_2atmpS2121;
  int32_t _M0L2e2S738;
  int32_t _M0L6_2atmpS2120;
  uint64_t _M0L6_2atmpS2119;
  uint64_t _M0L4maskS740;
  uint64_t _M0L8fractionS741;
  int32_t _M0L6_2atmpS2118;
  uint64_t _M0L6_2atmpS2117;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2116;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S736 = 4503599627370496ull | _M0L12ieeeMantissaS737;
  _M0L6_2atmpS2121 = _M0L12ieeeExponentS739 - 1023;
  _M0L2e2S738 = _M0L6_2atmpS2121 - 52;
  if (_M0L2e2S738 > 0) {
    return 0;
  }
  if (_M0L2e2S738 < -52) {
    return 0;
  }
  _M0L6_2atmpS2120 = -_M0L2e2S738;
  _M0L6_2atmpS2119 = 1ull << (_M0L6_2atmpS2120 & 63);
  _M0L4maskS740 = _M0L6_2atmpS2119 - 1ull;
  _M0L8fractionS741 = _M0L2m2S736 & _M0L4maskS740;
  if (_M0L8fractionS741 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2118 = -_M0L2e2S738;
  _M0L6_2atmpS2117 = _M0L2m2S736 >> (_M0L6_2atmpS2118 & 63);
  _M0L6_2atmpS2116
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2116)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2116->$0 = _M0L6_2atmpS2117;
  _M0L6_2atmpS2116->$1 = 0;
  return _M0L6_2atmpS2116;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS704,
  int32_t _M0L4signS702
) {
  moonbit_bytes_t _M0L6resultS700;
  int32_t _M0Lm5indexS701;
  uint64_t _M0L6outputS703;
  int32_t _M0L7olengthS705;
  int32_t _M0L8exponentS2115;
  int32_t _M0L6_2atmpS2114;
  int32_t _M0Lm3expS706;
  int32_t _M0L6_2atmpS2113;
  int32_t _M0L6_2atmpS2111;
  int32_t _M0L18scientificNotationS707;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS700 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS701 = 0;
  if (_M0L4signS702) {
    int32_t _M0L6_2atmpS1985 = _M0Lm5indexS701;
    int32_t _M0L6_2atmpS1986;
    if (
      _M0L6_2atmpS1985 < 0
      || _M0L6_2atmpS1985 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS1985] = 45;
    _M0L6_2atmpS1986 = _M0Lm5indexS701;
    _M0Lm5indexS701 = _M0L6_2atmpS1986 + 1;
  }
  _M0L6outputS703 = _M0L1vS704->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS705 = _M0FPB17decimal__length17(_M0L6outputS703);
  _M0L8exponentS2115 = _M0L1vS704->$1;
  _M0L6_2atmpS2114 = _M0L8exponentS2115 + _M0L7olengthS705;
  _M0Lm3expS706 = _M0L6_2atmpS2114 - 1;
  _M0L6_2atmpS2113 = _M0Lm3expS706;
  if (_M0L6_2atmpS2113 >= -6) {
    int32_t _M0L6_2atmpS2112 = _M0Lm3expS706;
    _M0L6_2atmpS2111 = _M0L6_2atmpS2112 < 21;
  } else {
    _M0L6_2atmpS2111 = 0;
  }
  _M0L18scientificNotationS707 = !_M0L6_2atmpS2111;
  if (_M0L18scientificNotationS707) {
    int32_t _M0L7_2abindS708 = _M0L7olengthS705 - 1;
    uint64_t _M0L6outputS709;
    int32_t _M0L1iS710 = 0;
    uint64_t _M0L6outputS711 = _M0L6outputS703;
    int32_t _M0L6_2atmpS1987;
    int32_t _M0L6_2atmpS1991;
    int32_t _M0L6_2atmpS1990;
    int32_t _M0L6_2atmpS1989;
    int32_t _M0L6_2atmpS1988;
    int32_t _M0L6_2atmpS1995;
    int32_t _M0L6_2atmpS1996;
    int32_t _M0L6_2atmpS1997;
    int32_t _M0L6_2atmpS1998;
    int32_t _M0L6_2atmpS1999;
    int32_t _M0L6_2atmpS2005;
    int32_t _M0L6_2atmpS2038;
    moonbit_string_t _result_2729;
    while (1) {
      if (_M0L1iS710 < _M0L7_2abindS708) {
        uint64_t _M0L1cS712 = _M0L6outputS711 % 10ull;
        int32_t _M0L6_2atmpS2044 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2043 = _M0L6_2atmpS2044 + _M0L7olengthS705;
        int32_t _M0L6_2atmpS2039 = _M0L6_2atmpS2043 - _M0L1iS710;
        int32_t _M0L6_2atmpS2042 = (int32_t)_M0L1cS712;
        int32_t _M0L6_2atmpS2041 = 48 + _M0L6_2atmpS2042;
        int32_t _M0L6_2atmpS2040 = _M0L6_2atmpS2041 & 0xff;
        int32_t _M0L6_2atmpS2045;
        uint64_t _M0L6_2atmpS2046;
        if (
          _M0L6_2atmpS2039 < 0
          || _M0L6_2atmpS2039 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2039] = _M0L6_2atmpS2040;
        _M0L6_2atmpS2045 = _M0L1iS710 + 1;
        _M0L6_2atmpS2046 = _M0L6outputS711 / 10ull;
        _M0L1iS710 = _M0L6_2atmpS2045;
        _M0L6outputS711 = _M0L6_2atmpS2046;
        continue;
      } else {
        _M0L6outputS709 = _M0L6outputS711;
      }
      break;
    }
    _M0L6_2atmpS1987 = _M0Lm5indexS701;
    _M0L6_2atmpS1991 = (int32_t)_M0L6outputS709;
    _M0L6_2atmpS1990 = _M0L6_2atmpS1991 % 10;
    _M0L6_2atmpS1989 = 48 + _M0L6_2atmpS1990;
    _M0L6_2atmpS1988 = _M0L6_2atmpS1989 & 0xff;
    if (
      _M0L6_2atmpS1987 < 0
      || _M0L6_2atmpS1987 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS1987] = _M0L6_2atmpS1988;
    if (_M0L7olengthS705 > 1) {
      int32_t _M0L6_2atmpS1993 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS1992 = _M0L6_2atmpS1993 + 1;
      if (
        _M0L6_2atmpS1992 < 0
        || _M0L6_2atmpS1992 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS1992] = 46;
    } else {
      int32_t _M0L6_2atmpS1994 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS1994 - 1;
    }
    _M0L6_2atmpS1995 = _M0Lm5indexS701;
    _M0L6_2atmpS1996 = _M0L7olengthS705 + 1;
    _M0Lm5indexS701 = _M0L6_2atmpS1995 + _M0L6_2atmpS1996;
    _M0L6_2atmpS1997 = _M0Lm5indexS701;
    if (
      _M0L6_2atmpS1997 < 0
      || _M0L6_2atmpS1997 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS1997] = 101;
    _M0L6_2atmpS1998 = _M0Lm5indexS701;
    _M0Lm5indexS701 = _M0L6_2atmpS1998 + 1;
    _M0L6_2atmpS1999 = _M0Lm3expS706;
    if (_M0L6_2atmpS1999 < 0) {
      int32_t _M0L6_2atmpS2000 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2001;
      int32_t _M0L6_2atmpS2002;
      if (
        _M0L6_2atmpS2000 < 0
        || _M0L6_2atmpS2000 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2000] = 45;
      _M0L6_2atmpS2001 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2001 + 1;
      _M0L6_2atmpS2002 = _M0Lm3expS706;
      _M0Lm3expS706 = -_M0L6_2atmpS2002;
    } else {
      int32_t _M0L6_2atmpS2003 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2004;
      if (
        _M0L6_2atmpS2003 < 0
        || _M0L6_2atmpS2003 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2003] = 43;
      _M0L6_2atmpS2004 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2004 + 1;
    }
    _M0L6_2atmpS2005 = _M0Lm3expS706;
    if (_M0L6_2atmpS2005 >= 100) {
      int32_t _M0L6_2atmpS2021 = _M0Lm3expS706;
      int32_t _M0L1aS714 = _M0L6_2atmpS2021 / 100;
      int32_t _M0L6_2atmpS2020 = _M0Lm3expS706;
      int32_t _M0L6_2atmpS2019 = _M0L6_2atmpS2020 / 10;
      int32_t _M0L1bS715 = _M0L6_2atmpS2019 % 10;
      int32_t _M0L6_2atmpS2018 = _M0Lm3expS706;
      int32_t _M0L1cS716 = _M0L6_2atmpS2018 % 10;
      int32_t _M0L6_2atmpS2006 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2008 = 48 + _M0L1aS714;
      int32_t _M0L6_2atmpS2007 = _M0L6_2atmpS2008 & 0xff;
      int32_t _M0L6_2atmpS2012;
      int32_t _M0L6_2atmpS2009;
      int32_t _M0L6_2atmpS2011;
      int32_t _M0L6_2atmpS2010;
      int32_t _M0L6_2atmpS2016;
      int32_t _M0L6_2atmpS2013;
      int32_t _M0L6_2atmpS2015;
      int32_t _M0L6_2atmpS2014;
      int32_t _M0L6_2atmpS2017;
      if (
        _M0L6_2atmpS2006 < 0
        || _M0L6_2atmpS2006 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2006] = _M0L6_2atmpS2007;
      _M0L6_2atmpS2012 = _M0Lm5indexS701;
      _M0L6_2atmpS2009 = _M0L6_2atmpS2012 + 1;
      _M0L6_2atmpS2011 = 48 + _M0L1bS715;
      _M0L6_2atmpS2010 = _M0L6_2atmpS2011 & 0xff;
      if (
        _M0L6_2atmpS2009 < 0
        || _M0L6_2atmpS2009 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2009] = _M0L6_2atmpS2010;
      _M0L6_2atmpS2016 = _M0Lm5indexS701;
      _M0L6_2atmpS2013 = _M0L6_2atmpS2016 + 2;
      _M0L6_2atmpS2015 = 48 + _M0L1cS716;
      _M0L6_2atmpS2014 = _M0L6_2atmpS2015 & 0xff;
      if (
        _M0L6_2atmpS2013 < 0
        || _M0L6_2atmpS2013 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2013] = _M0L6_2atmpS2014;
      _M0L6_2atmpS2017 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2017 + 3;
    } else {
      int32_t _M0L6_2atmpS2022 = _M0Lm3expS706;
      if (_M0L6_2atmpS2022 >= 10) {
        int32_t _M0L6_2atmpS2032 = _M0Lm3expS706;
        int32_t _M0L1aS717 = _M0L6_2atmpS2032 / 10;
        int32_t _M0L6_2atmpS2031 = _M0Lm3expS706;
        int32_t _M0L1bS718 = _M0L6_2atmpS2031 % 10;
        int32_t _M0L6_2atmpS2023 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2025 = 48 + _M0L1aS717;
        int32_t _M0L6_2atmpS2024 = _M0L6_2atmpS2025 & 0xff;
        int32_t _M0L6_2atmpS2029;
        int32_t _M0L6_2atmpS2026;
        int32_t _M0L6_2atmpS2028;
        int32_t _M0L6_2atmpS2027;
        int32_t _M0L6_2atmpS2030;
        if (
          _M0L6_2atmpS2023 < 0
          || _M0L6_2atmpS2023 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2023] = _M0L6_2atmpS2024;
        _M0L6_2atmpS2029 = _M0Lm5indexS701;
        _M0L6_2atmpS2026 = _M0L6_2atmpS2029 + 1;
        _M0L6_2atmpS2028 = 48 + _M0L1bS718;
        _M0L6_2atmpS2027 = _M0L6_2atmpS2028 & 0xff;
        if (
          _M0L6_2atmpS2026 < 0
          || _M0L6_2atmpS2026 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2026] = _M0L6_2atmpS2027;
        _M0L6_2atmpS2030 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2030 + 2;
      } else {
        int32_t _M0L6_2atmpS2033 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2036 = _M0Lm3expS706;
        int32_t _M0L6_2atmpS2035 = 48 + _M0L6_2atmpS2036;
        int32_t _M0L6_2atmpS2034 = _M0L6_2atmpS2035 & 0xff;
        int32_t _M0L6_2atmpS2037;
        if (
          _M0L6_2atmpS2033 < 0
          || _M0L6_2atmpS2033 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2033] = _M0L6_2atmpS2034;
        _M0L6_2atmpS2037 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2037 + 1;
      }
    }
    _M0L6_2atmpS2038 = _M0Lm5indexS701;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2729
    = _M0FPB19string__from__bytes(_M0L6resultS700, 0, _M0L6_2atmpS2038);
    moonbit_decref_cycle_free(_M0L6resultS700);
    return _result_2729;
  } else {
    int32_t _M0L6_2atmpS2047 = _M0Lm3expS706;
    int32_t _M0L6_2atmpS2110;
    moonbit_string_t _result_2735;
    if (_M0L6_2atmpS2047 < 0) {
      int32_t _M0L6_2atmpS2048 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2050;
      int32_t _M0L6_2atmpS2049;
      int32_t _M0L6_2atmpS2051;
      int32_t _M0L1iS719;
      int32_t _M0L6_2atmpS2066;
      int32_t _M0L6_2atmpS2068;
      int32_t _M0L6_2atmpS2067;
      int32_t _M0L7currentS721;
      int32_t _M0L1iS722;
      uint64_t _M0L6outputS723;
      if (
        _M0L6_2atmpS2048 < 0
        || _M0L6_2atmpS2048 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2048] = 48;
      _M0L6_2atmpS2050 = _M0Lm5indexS701;
      _M0L6_2atmpS2049 = _M0L6_2atmpS2050 + 1;
      if (
        _M0L6_2atmpS2049 < 0
        || _M0L6_2atmpS2049 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2049] = 46;
      _M0L6_2atmpS2051 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2051 + 2;
      _M0L1iS719 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2052 = _M0Lm3expS706;
        if (_M0L1iS719 > _M0L6_2atmpS2052) {
          int32_t _M0L6_2atmpS2055 = _M0Lm5indexS701;
          int32_t _M0L6_2atmpS2054 = _M0L6_2atmpS2055 - _M0L1iS719;
          int32_t _M0L6_2atmpS2053 = _M0L6_2atmpS2054 - 1;
          int32_t _M0L6_2atmpS2056;
          if (
            _M0L6_2atmpS2053 < 0
            || _M0L6_2atmpS2053 >= Moonbit_array_length(_M0L6resultS700)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS700[_M0L6_2atmpS2053] = 48;
          _M0L6_2atmpS2056 = _M0L1iS719 - 1;
          _M0L1iS719 = _M0L6_2atmpS2056;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2066 = _M0Lm5indexS701;
      _M0L6_2atmpS2068 = _M0Lm3expS706;
      _M0L6_2atmpS2067 = -1 - _M0L6_2atmpS2068;
      _M0L7currentS721 = _M0L6_2atmpS2066 + _M0L6_2atmpS2067;
      _M0L1iS722 = 0;
      _M0L6outputS723 = _M0L6outputS703;
      while (1) {
        if (_M0L1iS722 < _M0L7olengthS705) {
          int32_t _M0L6_2atmpS2063 = _M0L7currentS721 + _M0L7olengthS705;
          int32_t _M0L6_2atmpS2062 = _M0L6_2atmpS2063 - _M0L1iS722;
          int32_t _M0L6_2atmpS2057 = _M0L6_2atmpS2062 - 1;
          uint64_t _M0L6_2atmpS2061 = _M0L6outputS723 % 10ull;
          int32_t _M0L6_2atmpS2060 = (int32_t)_M0L6_2atmpS2061;
          int32_t _M0L6_2atmpS2059 = 48 + _M0L6_2atmpS2060;
          int32_t _M0L6_2atmpS2058 = _M0L6_2atmpS2059 & 0xff;
          int32_t _M0L6_2atmpS2064;
          uint64_t _M0L6_2atmpS2065;
          if (
            _M0L6_2atmpS2057 < 0
            || _M0L6_2atmpS2057 >= Moonbit_array_length(_M0L6resultS700)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS700[_M0L6_2atmpS2057] = _M0L6_2atmpS2058;
          _M0L6_2atmpS2064 = _M0L1iS722 + 1;
          _M0L6_2atmpS2065 = _M0L6outputS723 / 10ull;
          _M0L1iS722 = _M0L6_2atmpS2064;
          _M0L6outputS723 = _M0L6_2atmpS2065;
          continue;
        }
        break;
      }
      _M0Lm5indexS701 = _M0L7currentS721 + _M0L7olengthS705;
    } else {
      int32_t _M0L6_2atmpS2070 = _M0Lm3expS706;
      int32_t _M0L6_2atmpS2069 = _M0L6_2atmpS2070 + 1;
      if (_M0L6_2atmpS2069 >= _M0L7olengthS705) {
        int32_t _M0L1iS725 = 0;
        uint64_t _M0L6outputS726 = _M0L6outputS703;
        int32_t _M0L6_2atmpS2081;
        int32_t _M0L6_2atmpS2086;
        int32_t _M0L7_2abindS728;
        int32_t _M0L1iS729;
        int32_t _M0L6_2atmpS2087;
        int32_t _M0L6_2atmpS2090;
        int32_t _M0L6_2atmpS2089;
        int32_t _M0L6_2atmpS2088;
        while (1) {
          if (_M0L1iS725 < _M0L7olengthS705) {
            int32_t _M0L6_2atmpS2078 = _M0Lm5indexS701;
            int32_t _M0L6_2atmpS2077 = _M0L6_2atmpS2078 + _M0L7olengthS705;
            int32_t _M0L6_2atmpS2076 = _M0L6_2atmpS2077 - _M0L1iS725;
            int32_t _M0L6_2atmpS2071 = _M0L6_2atmpS2076 - 1;
            uint64_t _M0L6_2atmpS2075 = _M0L6outputS726 % 10ull;
            int32_t _M0L6_2atmpS2074 = (int32_t)_M0L6_2atmpS2075;
            int32_t _M0L6_2atmpS2073 = 48 + _M0L6_2atmpS2074;
            int32_t _M0L6_2atmpS2072 = _M0L6_2atmpS2073 & 0xff;
            int32_t _M0L6_2atmpS2079;
            uint64_t _M0L6_2atmpS2080;
            if (
              _M0L6_2atmpS2071 < 0
              || _M0L6_2atmpS2071 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2071] = _M0L6_2atmpS2072;
            _M0L6_2atmpS2079 = _M0L1iS725 + 1;
            _M0L6_2atmpS2080 = _M0L6outputS726 / 10ull;
            _M0L1iS725 = _M0L6_2atmpS2079;
            _M0L6outputS726 = _M0L6_2atmpS2080;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2081 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2081 + _M0L7olengthS705;
        _M0L6_2atmpS2086 = _M0Lm3expS706;
        _M0L7_2abindS728 = _M0L6_2atmpS2086 + 1;
        _M0L1iS729 = _M0L7olengthS705;
        while (1) {
          if (_M0L1iS729 < _M0L7_2abindS728) {
            int32_t _M0L6_2atmpS2084 = _M0Lm5indexS701;
            int32_t _M0L6_2atmpS2083 = _M0L6_2atmpS2084 + _M0L1iS729;
            int32_t _M0L6_2atmpS2082 = _M0L6_2atmpS2083 - _M0L7olengthS705;
            int32_t _M0L6_2atmpS2085;
            if (
              _M0L6_2atmpS2082 < 0
              || _M0L6_2atmpS2082 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2082] = 48;
            _M0L6_2atmpS2085 = _M0L1iS729 + 1;
            _M0L1iS729 = _M0L6_2atmpS2085;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2087 = _M0Lm5indexS701;
        _M0L6_2atmpS2090 = _M0Lm3expS706;
        _M0L6_2atmpS2089 = _M0L6_2atmpS2090 + 1;
        _M0L6_2atmpS2088 = _M0L6_2atmpS2089 - _M0L7olengthS705;
        _M0Lm5indexS701 = _M0L6_2atmpS2087 + _M0L6_2atmpS2088;
      } else {
        int32_t _M0L6_2atmpS2107 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2106 = _M0L6_2atmpS2107 + 1;
        int32_t _M0L1iS731 = 0;
        int32_t _M0L7currentS732 = _M0L6_2atmpS2106;
        uint64_t _M0L6outputS733 = _M0L6outputS703;
        int32_t _M0L6_2atmpS2108;
        int32_t _M0L6_2atmpS2109;
        while (1) {
          if (_M0L1iS731 < _M0L7olengthS705) {
            int32_t _M0L6_2atmpS2102 = _M0L7olengthS705 - _M0L1iS731;
            int32_t _M0L6_2atmpS2100 = _M0L6_2atmpS2102 - 1;
            int32_t _M0L6_2atmpS2101 = _M0Lm3expS706;
            int32_t _M0L7currentS734;
            int32_t _M0L6_2atmpS2097;
            int32_t _M0L6_2atmpS2096;
            int32_t _M0L6_2atmpS2091;
            uint64_t _M0L6_2atmpS2095;
            int32_t _M0L6_2atmpS2094;
            int32_t _M0L6_2atmpS2093;
            int32_t _M0L6_2atmpS2092;
            int32_t _M0L6_2atmpS2098;
            uint64_t _M0L6_2atmpS2099;
            if (_M0L6_2atmpS2100 == _M0L6_2atmpS2101) {
              int32_t _M0L6_2atmpS2105 = _M0L7currentS732 + _M0L7olengthS705;
              int32_t _M0L6_2atmpS2104 = _M0L6_2atmpS2105 - _M0L1iS731;
              int32_t _M0L6_2atmpS2103 = _M0L6_2atmpS2104 - 1;
              if (
                _M0L6_2atmpS2103 < 0
                || _M0L6_2atmpS2103 >= Moonbit_array_length(_M0L6resultS700)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS700[_M0L6_2atmpS2103] = 46;
              _M0L7currentS734 = _M0L7currentS732 - 1;
            } else {
              _M0L7currentS734 = _M0L7currentS732;
            }
            _M0L6_2atmpS2097 = _M0L7currentS734 + _M0L7olengthS705;
            _M0L6_2atmpS2096 = _M0L6_2atmpS2097 - _M0L1iS731;
            _M0L6_2atmpS2091 = _M0L6_2atmpS2096 - 1;
            _M0L6_2atmpS2095 = _M0L6outputS733 % 10ull;
            _M0L6_2atmpS2094 = (int32_t)_M0L6_2atmpS2095;
            _M0L6_2atmpS2093 = 48 + _M0L6_2atmpS2094;
            _M0L6_2atmpS2092 = _M0L6_2atmpS2093 & 0xff;
            if (
              _M0L6_2atmpS2091 < 0
              || _M0L6_2atmpS2091 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2091] = _M0L6_2atmpS2092;
            _M0L6_2atmpS2098 = _M0L1iS731 + 1;
            _M0L6_2atmpS2099 = _M0L6outputS733 / 10ull;
            _M0L1iS731 = _M0L6_2atmpS2098;
            _M0L7currentS732 = _M0L7currentS734;
            _M0L6outputS733 = _M0L6_2atmpS2099;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2108 = _M0Lm5indexS701;
        _M0L6_2atmpS2109 = _M0L7olengthS705 + 1;
        _M0Lm5indexS701 = _M0L6_2atmpS2108 + _M0L6_2atmpS2109;
      }
    }
    _M0L6_2atmpS2110 = _M0Lm5indexS701;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2735
    = _M0FPB19string__from__bytes(_M0L6resultS700, 0, _M0L6_2atmpS2110);
    moonbit_decref_cycle_free(_M0L6resultS700);
    return _result_2735;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS646,
  uint32_t _M0L12ieeeExponentS645
) {
  int32_t _M0Lm2e2S643;
  uint64_t _M0Lm2m2S644;
  uint64_t _M0L6_2atmpS1984;
  uint64_t _M0L6_2atmpS1983;
  int32_t _M0L4evenS647;
  uint64_t _M0L6_2atmpS1982;
  uint64_t _M0L2mvS648;
  int32_t _M0L7mmShiftS649;
  uint64_t _M0Lm2vrS650;
  uint64_t _M0Lm2vpS651;
  uint64_t _M0Lm2vmS652;
  int32_t _M0Lm3e10S653;
  int32_t _M0Lm17vmIsTrailingZerosS654;
  int32_t _M0Lm17vrIsTrailingZerosS655;
  int32_t _M0L6_2atmpS1884;
  int32_t _M0Lm7removedS674;
  int32_t _M0Lm16lastRemovedDigitS675;
  uint64_t _M0Lm6outputS676;
  int32_t _M0L6_2atmpS1980;
  int32_t _M0L6_2atmpS1981;
  int32_t _M0L3expS699;
  uint64_t _M0L6_2atmpS1979;
  struct _M0TPB17FloatingDecimal64* _block_2741;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S643 = 0;
  _M0Lm2m2S644 = 0ull;
  if (_M0L12ieeeExponentS645 == 0u) {
    _M0Lm2e2S643 = -1076;
    _M0Lm2m2S644 = _M0L12ieeeMantissaS646;
  } else {
    int32_t _M0L6_2atmpS1883 = *(int32_t*)&_M0L12ieeeExponentS645;
    int32_t _M0L6_2atmpS1882 = _M0L6_2atmpS1883 - 1023;
    int32_t _M0L6_2atmpS1881 = _M0L6_2atmpS1882 - 52;
    _M0Lm2e2S643 = _M0L6_2atmpS1881 - 2;
    _M0Lm2m2S644 = 4503599627370496ull | _M0L12ieeeMantissaS646;
  }
  _M0L6_2atmpS1984 = _M0Lm2m2S644;
  _M0L6_2atmpS1983 = _M0L6_2atmpS1984 & 1ull;
  _M0L4evenS647 = _M0L6_2atmpS1983 == 0ull;
  _M0L6_2atmpS1982 = _M0Lm2m2S644;
  _M0L2mvS648 = 4ull * _M0L6_2atmpS1982;
  _M0L7mmShiftS649
  = _M0L12ieeeMantissaS646 != 0ull || _M0L12ieeeExponentS645 <= 1u;
  _M0Lm2vrS650 = 0ull;
  _M0Lm2vpS651 = 0ull;
  _M0Lm2vmS652 = 0ull;
  _M0Lm3e10S653 = 0;
  _M0Lm17vmIsTrailingZerosS654 = 0;
  _M0Lm17vrIsTrailingZerosS655 = 0;
  _M0L6_2atmpS1884 = _M0Lm2e2S643;
  if (_M0L6_2atmpS1884 >= 0) {
    int32_t _M0L6_2atmpS1906 = _M0Lm2e2S643;
    int32_t _M0L6_2atmpS1902;
    int32_t _M0L6_2atmpS1905;
    int32_t _M0L6_2atmpS1904;
    int32_t _M0L6_2atmpS1903;
    int32_t _M0L1qS656;
    int32_t _M0L6_2atmpS1901;
    int32_t _M0L6_2atmpS1900;
    int32_t _M0L1kS657;
    int32_t _M0L6_2atmpS1899;
    int32_t _M0L6_2atmpS1898;
    int32_t _M0L6_2atmpS1897;
    int32_t _M0L1iS658;
    struct _M0TPB8Pow5Pair _M0L4pow5S659;
    uint64_t _M0L6_2atmpS1896;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS660;
    uint64_t _M0L8_2avrOutS661;
    uint64_t _M0L8_2avpOutS662;
    uint64_t _M0L8_2avmOutS663;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1902 = _M0FPB9log10Pow2(_M0L6_2atmpS1906);
    _M0L6_2atmpS1905 = _M0Lm2e2S643;
    _M0L6_2atmpS1904 = _M0L6_2atmpS1905 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1903 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1904);
    _M0L1qS656 = _M0L6_2atmpS1902 - _M0L6_2atmpS1903;
    _M0Lm3e10S653 = _M0L1qS656;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1901 = _M0FPB8pow5bits(_M0L1qS656);
    _M0L6_2atmpS1900 = 125 + _M0L6_2atmpS1901;
    _M0L1kS657 = _M0L6_2atmpS1900 - 1;
    _M0L6_2atmpS1899 = _M0Lm2e2S643;
    _M0L6_2atmpS1898 = -_M0L6_2atmpS1899;
    _M0L6_2atmpS1897 = _M0L6_2atmpS1898 + _M0L1qS656;
    _M0L1iS658 = _M0L6_2atmpS1897 + _M0L1kS657;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S659 = _M0FPB22double__computeInvPow5(_M0L1qS656);
    _M0L6_2atmpS1896 = _M0Lm2m2S644;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS660
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1896, _M0L4pow5S659, _M0L1iS658, _M0L7mmShiftS649);
    _M0L8_2avrOutS661 = _M0L7_2abindS660.$0;
    _M0L8_2avpOutS662 = _M0L7_2abindS660.$1;
    _M0L8_2avmOutS663 = _M0L7_2abindS660.$2;
    _M0Lm2vrS650 = _M0L8_2avrOutS661;
    _M0Lm2vpS651 = _M0L8_2avpOutS662;
    _M0Lm2vmS652 = _M0L8_2avmOutS663;
    if (_M0L1qS656 <= 21) {
      int32_t _M0L6_2atmpS1892 = (int32_t)_M0L2mvS648;
      uint64_t _M0L6_2atmpS1895 = _M0L2mvS648 / 5ull;
      int32_t _M0L6_2atmpS1894 = (int32_t)_M0L6_2atmpS1895;
      int32_t _M0L6_2atmpS1893 = 5 * _M0L6_2atmpS1894;
      int32_t _M0L6mvMod5S664 = _M0L6_2atmpS1892 - _M0L6_2atmpS1893;
      if (_M0L6mvMod5S664 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS648, _M0L1qS656);
      } else if (_M0L4evenS647) {
        uint64_t _M0L6_2atmpS1886 = _M0L2mvS648 - 1ull;
        uint64_t _M0L6_2atmpS1887;
        uint64_t _M0L6_2atmpS1885;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1887 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS649);
        _M0L6_2atmpS1885 = _M0L6_2atmpS1886 - _M0L6_2atmpS1887;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS654
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1885, _M0L1qS656);
      } else {
        uint64_t _M0L6_2atmpS1888 = _M0Lm2vpS651;
        uint64_t _M0L6_2atmpS1891 = _M0L2mvS648 + 2ull;
        int32_t _M0L6_2atmpS1890;
        uint64_t _M0L6_2atmpS1889;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1890
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1891, _M0L1qS656);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1889 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1890);
        _M0Lm2vpS651 = _M0L6_2atmpS1888 - _M0L6_2atmpS1889;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1920 = _M0Lm2e2S643;
    int32_t _M0L6_2atmpS1919 = -_M0L6_2atmpS1920;
    int32_t _M0L6_2atmpS1914;
    int32_t _M0L6_2atmpS1918;
    int32_t _M0L6_2atmpS1917;
    int32_t _M0L6_2atmpS1916;
    int32_t _M0L6_2atmpS1915;
    int32_t _M0L1qS665;
    int32_t _M0L6_2atmpS1907;
    int32_t _M0L6_2atmpS1913;
    int32_t _M0L6_2atmpS1912;
    int32_t _M0L1iS666;
    int32_t _M0L6_2atmpS1911;
    int32_t _M0L1kS667;
    int32_t _M0L1jS668;
    struct _M0TPB8Pow5Pair _M0L4pow5S669;
    uint64_t _M0L6_2atmpS1910;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS670;
    uint64_t _M0L8_2avrOutS671;
    uint64_t _M0L8_2avpOutS672;
    uint64_t _M0L8_2avmOutS673;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1914 = _M0FPB9log10Pow5(_M0L6_2atmpS1919);
    _M0L6_2atmpS1918 = _M0Lm2e2S643;
    _M0L6_2atmpS1917 = -_M0L6_2atmpS1918;
    _M0L6_2atmpS1916 = _M0L6_2atmpS1917 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1915 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1916);
    _M0L1qS665 = _M0L6_2atmpS1914 - _M0L6_2atmpS1915;
    _M0L6_2atmpS1907 = _M0Lm2e2S643;
    _M0Lm3e10S653 = _M0L1qS665 + _M0L6_2atmpS1907;
    _M0L6_2atmpS1913 = _M0Lm2e2S643;
    _M0L6_2atmpS1912 = -_M0L6_2atmpS1913;
    _M0L1iS666 = _M0L6_2atmpS1912 - _M0L1qS665;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1911 = _M0FPB8pow5bits(_M0L1iS666);
    _M0L1kS667 = _M0L6_2atmpS1911 - 125;
    _M0L1jS668 = _M0L1qS665 - _M0L1kS667;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S669 = _M0FPB19double__computePow5(_M0L1iS666);
    _M0L6_2atmpS1910 = _M0Lm2m2S644;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS670
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1910, _M0L4pow5S669, _M0L1jS668, _M0L7mmShiftS649);
    _M0L8_2avrOutS671 = _M0L7_2abindS670.$0;
    _M0L8_2avpOutS672 = _M0L7_2abindS670.$1;
    _M0L8_2avmOutS673 = _M0L7_2abindS670.$2;
    _M0Lm2vrS650 = _M0L8_2avrOutS671;
    _M0Lm2vpS651 = _M0L8_2avpOutS672;
    _M0Lm2vmS652 = _M0L8_2avmOutS673;
    if (_M0L1qS665 <= 1) {
      _M0Lm17vrIsTrailingZerosS655 = 1;
      if (_M0L4evenS647) {
        int32_t _M0L6_2atmpS1908;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1908 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS649);
        _M0Lm17vmIsTrailingZerosS654 = _M0L6_2atmpS1908 == 1;
      } else {
        uint64_t _M0L6_2atmpS1909 = _M0Lm2vpS651;
        _M0Lm2vpS651 = _M0L6_2atmpS1909 - 1ull;
      }
    } else if (_M0L1qS665 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS655
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS648, _M0L1qS665);
    }
  }
  _M0Lm7removedS674 = 0;
  _M0Lm16lastRemovedDigitS675 = 0;
  _M0Lm6outputS676 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS654 || _M0Lm17vrIsTrailingZerosS655) {
    int32_t _if__result_2738;
    uint64_t _M0L6_2atmpS1950;
    uint64_t _M0L6_2atmpS1956;
    uint64_t _M0L6_2atmpS1957;
    int32_t _if__result_2739;
    int32_t _M0L6_2atmpS1953;
    int64_t _M0L6_2atmpS1952;
    uint64_t _M0L6_2atmpS1951;
    while (1) {
      uint64_t _M0L6_2atmpS1933 = _M0Lm2vpS651;
      uint64_t _M0L7vpDiv10S677 = _M0L6_2atmpS1933 / 10ull;
      uint64_t _M0L6_2atmpS1932 = _M0Lm2vmS652;
      uint64_t _M0L7vmDiv10S678 = _M0L6_2atmpS1932 / 10ull;
      uint64_t _M0L6_2atmpS1931;
      int32_t _M0L6_2atmpS1928;
      int32_t _M0L6_2atmpS1930;
      int32_t _M0L6_2atmpS1929;
      int32_t _M0L7vmMod10S680;
      uint64_t _M0L6_2atmpS1927;
      uint64_t _M0L7vrDiv10S681;
      uint64_t _M0L6_2atmpS1926;
      int32_t _M0L6_2atmpS1923;
      int32_t _M0L6_2atmpS1925;
      int32_t _M0L6_2atmpS1924;
      int32_t _M0L7vrMod10S682;
      int32_t _M0L6_2atmpS1922;
      if (_M0L7vpDiv10S677 <= _M0L7vmDiv10S678) {
        break;
      }
      _M0L6_2atmpS1931 = _M0Lm2vmS652;
      _M0L6_2atmpS1928 = (int32_t)_M0L6_2atmpS1931;
      _M0L6_2atmpS1930 = (int32_t)_M0L7vmDiv10S678;
      _M0L6_2atmpS1929 = 10 * _M0L6_2atmpS1930;
      _M0L7vmMod10S680 = _M0L6_2atmpS1928 - _M0L6_2atmpS1929;
      _M0L6_2atmpS1927 = _M0Lm2vrS650;
      _M0L7vrDiv10S681 = _M0L6_2atmpS1927 / 10ull;
      _M0L6_2atmpS1926 = _M0Lm2vrS650;
      _M0L6_2atmpS1923 = (int32_t)_M0L6_2atmpS1926;
      _M0L6_2atmpS1925 = (int32_t)_M0L7vrDiv10S681;
      _M0L6_2atmpS1924 = 10 * _M0L6_2atmpS1925;
      _M0L7vrMod10S682 = _M0L6_2atmpS1923 - _M0L6_2atmpS1924;
      _M0Lm17vmIsTrailingZerosS654
      = _M0Lm17vmIsTrailingZerosS654 && _M0L7vmMod10S680 == 0;
      if (_M0Lm17vrIsTrailingZerosS655) {
        int32_t _M0L6_2atmpS1921 = _M0Lm16lastRemovedDigitS675;
        _M0Lm17vrIsTrailingZerosS655 = _M0L6_2atmpS1921 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS655 = 0;
      }
      _M0Lm16lastRemovedDigitS675 = _M0L7vrMod10S682;
      _M0Lm2vrS650 = _M0L7vrDiv10S681;
      _M0Lm2vpS651 = _M0L7vpDiv10S677;
      _M0Lm2vmS652 = _M0L7vmDiv10S678;
      _M0L6_2atmpS1922 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS1922 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS654) {
      while (1) {
        uint64_t _M0L6_2atmpS1946 = _M0Lm2vmS652;
        uint64_t _M0L7vmDiv10S683 = _M0L6_2atmpS1946 / 10ull;
        uint64_t _M0L6_2atmpS1945 = _M0Lm2vmS652;
        int32_t _M0L6_2atmpS1942 = (int32_t)_M0L6_2atmpS1945;
        int32_t _M0L6_2atmpS1944 = (int32_t)_M0L7vmDiv10S683;
        int32_t _M0L6_2atmpS1943 = 10 * _M0L6_2atmpS1944;
        int32_t _M0L7vmMod10S684 = _M0L6_2atmpS1942 - _M0L6_2atmpS1943;
        uint64_t _M0L6_2atmpS1941;
        uint64_t _M0L7vpDiv10S686;
        uint64_t _M0L6_2atmpS1940;
        uint64_t _M0L7vrDiv10S687;
        uint64_t _M0L6_2atmpS1939;
        int32_t _M0L6_2atmpS1936;
        int32_t _M0L6_2atmpS1938;
        int32_t _M0L6_2atmpS1937;
        int32_t _M0L7vrMod10S688;
        int32_t _M0L6_2atmpS1935;
        if (_M0L7vmMod10S684 != 0) {
          break;
        }
        _M0L6_2atmpS1941 = _M0Lm2vpS651;
        _M0L7vpDiv10S686 = _M0L6_2atmpS1941 / 10ull;
        _M0L6_2atmpS1940 = _M0Lm2vrS650;
        _M0L7vrDiv10S687 = _M0L6_2atmpS1940 / 10ull;
        _M0L6_2atmpS1939 = _M0Lm2vrS650;
        _M0L6_2atmpS1936 = (int32_t)_M0L6_2atmpS1939;
        _M0L6_2atmpS1938 = (int32_t)_M0L7vrDiv10S687;
        _M0L6_2atmpS1937 = 10 * _M0L6_2atmpS1938;
        _M0L7vrMod10S688 = _M0L6_2atmpS1936 - _M0L6_2atmpS1937;
        if (_M0Lm17vrIsTrailingZerosS655) {
          int32_t _M0L6_2atmpS1934 = _M0Lm16lastRemovedDigitS675;
          _M0Lm17vrIsTrailingZerosS655 = _M0L6_2atmpS1934 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS655 = 0;
        }
        _M0Lm16lastRemovedDigitS675 = _M0L7vrMod10S688;
        _M0Lm2vrS650 = _M0L7vrDiv10S687;
        _M0Lm2vpS651 = _M0L7vpDiv10S686;
        _M0Lm2vmS652 = _M0L7vmDiv10S683;
        _M0L6_2atmpS1935 = _M0Lm7removedS674;
        _M0Lm7removedS674 = _M0L6_2atmpS1935 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS655) {
      int32_t _M0L6_2atmpS1949 = _M0Lm16lastRemovedDigitS675;
      if (_M0L6_2atmpS1949 == 5) {
        uint64_t _M0L6_2atmpS1948 = _M0Lm2vrS650;
        uint64_t _M0L6_2atmpS1947 = _M0L6_2atmpS1948 % 2ull;
        _if__result_2738 = _M0L6_2atmpS1947 == 0ull;
      } else {
        _if__result_2738 = 0;
      }
    } else {
      _if__result_2738 = 0;
    }
    if (_if__result_2738) {
      _M0Lm16lastRemovedDigitS675 = 4;
    }
    _M0L6_2atmpS1950 = _M0Lm2vrS650;
    _M0L6_2atmpS1956 = _M0Lm2vrS650;
    _M0L6_2atmpS1957 = _M0Lm2vmS652;
    if (_M0L6_2atmpS1956 == _M0L6_2atmpS1957) {
      if (!_M0L4evenS647) {
        _if__result_2739 = 1;
      } else {
        int32_t _M0L6_2atmpS1955 = _M0Lm17vmIsTrailingZerosS654;
        _if__result_2739 = !_M0L6_2atmpS1955;
      }
    } else {
      _if__result_2739 = 0;
    }
    if (_if__result_2739) {
      _M0L6_2atmpS1953 = 1;
    } else {
      int32_t _M0L6_2atmpS1954 = _M0Lm16lastRemovedDigitS675;
      _M0L6_2atmpS1953 = _M0L6_2atmpS1954 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1952 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1953);
    _M0L6_2atmpS1951 = *(uint64_t*)&_M0L6_2atmpS1952;
    _M0Lm6outputS676 = _M0L6_2atmpS1950 + _M0L6_2atmpS1951;
  } else {
    int32_t _M0Lm7roundUpS689 = 0;
    uint64_t _M0L6_2atmpS1978 = _M0Lm2vpS651;
    uint64_t _M0L8vpDiv100S690 = _M0L6_2atmpS1978 / 100ull;
    uint64_t _M0L6_2atmpS1977 = _M0Lm2vmS652;
    uint64_t _M0L8vmDiv100S691 = _M0L6_2atmpS1977 / 100ull;
    uint64_t _M0L6_2atmpS1972;
    uint64_t _M0L6_2atmpS1975;
    uint64_t _M0L6_2atmpS1976;
    int32_t _M0L6_2atmpS1974;
    uint64_t _M0L6_2atmpS1973;
    if (_M0L8vpDiv100S690 > _M0L8vmDiv100S691) {
      uint64_t _M0L6_2atmpS1963 = _M0Lm2vrS650;
      uint64_t _M0L8vrDiv100S692 = _M0L6_2atmpS1963 / 100ull;
      uint64_t _M0L6_2atmpS1962 = _M0Lm2vrS650;
      int32_t _M0L6_2atmpS1959 = (int32_t)_M0L6_2atmpS1962;
      int32_t _M0L6_2atmpS1961 = (int32_t)_M0L8vrDiv100S692;
      int32_t _M0L6_2atmpS1960 = 100 * _M0L6_2atmpS1961;
      int32_t _M0L8vrMod100S693 = _M0L6_2atmpS1959 - _M0L6_2atmpS1960;
      int32_t _M0L6_2atmpS1958;
      _M0Lm7roundUpS689 = _M0L8vrMod100S693 >= 50;
      _M0Lm2vrS650 = _M0L8vrDiv100S692;
      _M0Lm2vpS651 = _M0L8vpDiv100S690;
      _M0Lm2vmS652 = _M0L8vmDiv100S691;
      _M0L6_2atmpS1958 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS1958 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1971 = _M0Lm2vpS651;
      uint64_t _M0L7vpDiv10S694 = _M0L6_2atmpS1971 / 10ull;
      uint64_t _M0L6_2atmpS1970 = _M0Lm2vmS652;
      uint64_t _M0L7vmDiv10S695 = _M0L6_2atmpS1970 / 10ull;
      uint64_t _M0L6_2atmpS1969;
      uint64_t _M0L7vrDiv10S697;
      uint64_t _M0L6_2atmpS1968;
      int32_t _M0L6_2atmpS1965;
      int32_t _M0L6_2atmpS1967;
      int32_t _M0L6_2atmpS1966;
      int32_t _M0L7vrMod10S698;
      int32_t _M0L6_2atmpS1964;
      if (_M0L7vpDiv10S694 <= _M0L7vmDiv10S695) {
        break;
      }
      _M0L6_2atmpS1969 = _M0Lm2vrS650;
      _M0L7vrDiv10S697 = _M0L6_2atmpS1969 / 10ull;
      _M0L6_2atmpS1968 = _M0Lm2vrS650;
      _M0L6_2atmpS1965 = (int32_t)_M0L6_2atmpS1968;
      _M0L6_2atmpS1967 = (int32_t)_M0L7vrDiv10S697;
      _M0L6_2atmpS1966 = 10 * _M0L6_2atmpS1967;
      _M0L7vrMod10S698 = _M0L6_2atmpS1965 - _M0L6_2atmpS1966;
      _M0Lm7roundUpS689 = _M0L7vrMod10S698 >= 5;
      _M0Lm2vrS650 = _M0L7vrDiv10S697;
      _M0Lm2vpS651 = _M0L7vpDiv10S694;
      _M0Lm2vmS652 = _M0L7vmDiv10S695;
      _M0L6_2atmpS1964 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS1964 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1972 = _M0Lm2vrS650;
    _M0L6_2atmpS1975 = _M0Lm2vrS650;
    _M0L6_2atmpS1976 = _M0Lm2vmS652;
    _M0L6_2atmpS1974
    = _M0L6_2atmpS1975 == _M0L6_2atmpS1976 || _M0Lm7roundUpS689;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1973 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1974);
    _M0Lm6outputS676 = _M0L6_2atmpS1972 + _M0L6_2atmpS1973;
  }
  _M0L6_2atmpS1980 = _M0Lm3e10S653;
  _M0L6_2atmpS1981 = _M0Lm7removedS674;
  _M0L3expS699 = _M0L6_2atmpS1980 + _M0L6_2atmpS1981;
  _M0L6_2atmpS1979 = _M0Lm6outputS676;
  _block_2741
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2741)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2741->$0 = _M0L6_2atmpS1979;
  _block_2741->$1 = _M0L3expS699;
  return _block_2741;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS642) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS642) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS641) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS641) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS640) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS640) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS639) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS639 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS639 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS639 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS639 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS639 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS639 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS639 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS639 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS639 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS639 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS639 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS639 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS639 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS639 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS639 >= 100ull) {
    return 3;
  }
  if (_M0L1vS639 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS622) {
  int32_t _M0L6_2atmpS1880;
  int32_t _M0L6_2atmpS1879;
  int32_t _M0L4baseS621;
  int32_t _M0L5base2S623;
  int32_t _M0L6offsetS624;
  int32_t _M0L6_2atmpS1878;
  uint64_t _M0L4mul0S625;
  int32_t _M0L6_2atmpS1877;
  int32_t _M0L6_2atmpS1876;
  uint64_t _M0L4mul1S626;
  uint64_t _M0L1mS627;
  struct _M0TPB7Umul128 _M0L7_2abindS628;
  uint64_t _M0L7_2alow1S629;
  uint64_t _M0L8_2ahigh1S630;
  struct _M0TPB7Umul128 _M0L7_2abindS631;
  uint64_t _M0L7_2alow0S632;
  uint64_t _M0L8_2ahigh0S633;
  uint64_t _M0L3sumS634;
  uint64_t _M0Lm5high1S635;
  int32_t _M0L6_2atmpS1874;
  int32_t _M0L6_2atmpS1875;
  int32_t _M0L5deltaS636;
  uint64_t _M0L6_2atmpS1873;
  uint64_t _M0L6_2atmpS1865;
  int32_t _M0L6_2atmpS1872;
  uint32_t _M0L6_2atmpS1869;
  int32_t _M0L6_2atmpS1871;
  int32_t _M0L6_2atmpS1870;
  uint32_t _M0L6_2atmpS1868;
  uint32_t _M0L6_2atmpS1867;
  uint64_t _M0L6_2atmpS1866;
  uint64_t _M0L1aS637;
  uint64_t _M0L6_2atmpS1864;
  uint64_t _M0L1bS638;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1880 = _M0L1iS622 + 26;
  _M0L6_2atmpS1879 = _M0L6_2atmpS1880 - 1;
  _M0L4baseS621 = _M0L6_2atmpS1879 / 26;
  _M0L5base2S623 = _M0L4baseS621 * 26;
  _M0L6offsetS624 = _M0L5base2S623 - _M0L1iS622;
  _M0L6_2atmpS1878 = _M0L4baseS621 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S625
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1878);
  _M0L6_2atmpS1877 = _M0L4baseS621 * 2;
  _M0L6_2atmpS1876 = _M0L6_2atmpS1877 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1876);
  if (_M0L6offsetS624 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S625, .$1 = _M0L4mul1S626};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS624);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS628 = _M0FPB7umul128(_M0L1mS627, _M0L4mul1S626);
  _M0L7_2alow1S629 = _M0L7_2abindS628.$0;
  _M0L8_2ahigh1S630 = _M0L7_2abindS628.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS631 = _M0FPB7umul128(_M0L1mS627, _M0L4mul0S625);
  _M0L7_2alow0S632 = _M0L7_2abindS631.$0;
  _M0L8_2ahigh0S633 = _M0L7_2abindS631.$1;
  _M0L3sumS634 = _M0L8_2ahigh0S633 + _M0L7_2alow1S629;
  _M0Lm5high1S635 = _M0L8_2ahigh1S630;
  if (_M0L3sumS634 < _M0L8_2ahigh0S633) {
    uint64_t _M0L6_2atmpS1863 = _M0Lm5high1S635;
    _M0Lm5high1S635 = _M0L6_2atmpS1863 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1874 = _M0FPB8pow5bits(_M0L5base2S623);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1875 = _M0FPB8pow5bits(_M0L1iS622);
  _M0L5deltaS636 = _M0L6_2atmpS1874 - _M0L6_2atmpS1875;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1873
  = _M0FPB13shiftright128(_M0L7_2alow0S632, _M0L3sumS634, _M0L5deltaS636);
  _M0L6_2atmpS1865 = _M0L6_2atmpS1873 + 1ull;
  _M0L6_2atmpS1872 = _M0L1iS622 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1869
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1872);
  _M0L6_2atmpS1871 = _M0L1iS622 % 16;
  _M0L6_2atmpS1870 = _M0L6_2atmpS1871 << 1;
  _M0L6_2atmpS1868 = _M0L6_2atmpS1869 >> (_M0L6_2atmpS1870 & 31);
  _M0L6_2atmpS1867 = _M0L6_2atmpS1868 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1866 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1867);
  _M0L1aS637 = _M0L6_2atmpS1865 + _M0L6_2atmpS1866;
  _M0L6_2atmpS1864 = _M0Lm5high1S635;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS638
  = _M0FPB13shiftright128(_M0L3sumS634, _M0L6_2atmpS1864, _M0L5deltaS636);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS637, .$1 = _M0L1bS638};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS604) {
  int32_t _M0L4baseS603;
  int32_t _M0L5base2S605;
  int32_t _M0L6offsetS606;
  int32_t _M0L6_2atmpS1862;
  uint64_t _M0L4mul0S607;
  int32_t _M0L6_2atmpS1861;
  int32_t _M0L6_2atmpS1860;
  uint64_t _M0L4mul1S608;
  uint64_t _M0L1mS609;
  struct _M0TPB7Umul128 _M0L7_2abindS610;
  uint64_t _M0L7_2alow1S611;
  uint64_t _M0L8_2ahigh1S612;
  struct _M0TPB7Umul128 _M0L7_2abindS613;
  uint64_t _M0L7_2alow0S614;
  uint64_t _M0L8_2ahigh0S615;
  uint64_t _M0L3sumS616;
  uint64_t _M0Lm5high1S617;
  int32_t _M0L6_2atmpS1858;
  int32_t _M0L6_2atmpS1859;
  int32_t _M0L5deltaS618;
  uint64_t _M0L6_2atmpS1850;
  int32_t _M0L6_2atmpS1857;
  uint32_t _M0L6_2atmpS1854;
  int32_t _M0L6_2atmpS1856;
  int32_t _M0L6_2atmpS1855;
  uint32_t _M0L6_2atmpS1853;
  uint32_t _M0L6_2atmpS1852;
  uint64_t _M0L6_2atmpS1851;
  uint64_t _M0L1aS619;
  uint64_t _M0L6_2atmpS1849;
  uint64_t _M0L1bS620;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS603 = _M0L1iS604 / 26;
  _M0L5base2S605 = _M0L4baseS603 * 26;
  _M0L6offsetS606 = _M0L1iS604 - _M0L5base2S605;
  _M0L6_2atmpS1862 = _M0L4baseS603 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S607
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1862);
  _M0L6_2atmpS1861 = _M0L4baseS603 * 2;
  _M0L6_2atmpS1860 = _M0L6_2atmpS1861 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1860);
  if (_M0L6offsetS606 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S607, .$1 = _M0L4mul1S608};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS606);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS610 = _M0FPB7umul128(_M0L1mS609, _M0L4mul1S608);
  _M0L7_2alow1S611 = _M0L7_2abindS610.$0;
  _M0L8_2ahigh1S612 = _M0L7_2abindS610.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS613 = _M0FPB7umul128(_M0L1mS609, _M0L4mul0S607);
  _M0L7_2alow0S614 = _M0L7_2abindS613.$0;
  _M0L8_2ahigh0S615 = _M0L7_2abindS613.$1;
  _M0L3sumS616 = _M0L8_2ahigh0S615 + _M0L7_2alow1S611;
  _M0Lm5high1S617 = _M0L8_2ahigh1S612;
  if (_M0L3sumS616 < _M0L8_2ahigh0S615) {
    uint64_t _M0L6_2atmpS1848 = _M0Lm5high1S617;
    _M0Lm5high1S617 = _M0L6_2atmpS1848 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1858 = _M0FPB8pow5bits(_M0L1iS604);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1859 = _M0FPB8pow5bits(_M0L5base2S605);
  _M0L5deltaS618 = _M0L6_2atmpS1858 - _M0L6_2atmpS1859;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1850
  = _M0FPB13shiftright128(_M0L7_2alow0S614, _M0L3sumS616, _M0L5deltaS618);
  _M0L6_2atmpS1857 = _M0L1iS604 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1854
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1857);
  _M0L6_2atmpS1856 = _M0L1iS604 % 16;
  _M0L6_2atmpS1855 = _M0L6_2atmpS1856 << 1;
  _M0L6_2atmpS1853 = _M0L6_2atmpS1854 >> (_M0L6_2atmpS1855 & 31);
  _M0L6_2atmpS1852 = _M0L6_2atmpS1853 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1851 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1852);
  _M0L1aS619 = _M0L6_2atmpS1850 + _M0L6_2atmpS1851;
  _M0L6_2atmpS1849 = _M0Lm5high1S617;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS620
  = _M0FPB13shiftright128(_M0L3sumS616, _M0L6_2atmpS1849, _M0L5deltaS618);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS619, .$1 = _M0L1bS620};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS577,
  struct _M0TPB8Pow5Pair _M0L3mulS574,
  int32_t _M0L1jS590,
  int32_t _M0L7mmShiftS592
) {
  uint64_t _M0L7_2amul0S573;
  uint64_t _M0L7_2amul1S575;
  uint64_t _M0L1mS576;
  struct _M0TPB7Umul128 _M0L7_2abindS578;
  uint64_t _M0L5_2aloS579;
  uint64_t _M0L6_2atmpS580;
  struct _M0TPB7Umul128 _M0L7_2abindS581;
  uint64_t _M0L6_2alo2S582;
  uint64_t _M0L6_2ahi2S583;
  uint64_t _M0L3midS584;
  uint64_t _M0L6_2atmpS1847;
  uint64_t _M0L2hiS585;
  uint64_t _M0L3lo2S586;
  uint64_t _M0L6_2atmpS1845;
  uint64_t _M0L6_2atmpS1846;
  uint64_t _M0L4mid2S587;
  uint64_t _M0L6_2atmpS1844;
  uint64_t _M0L3hi2S588;
  int32_t _M0L6_2atmpS1843;
  int32_t _M0L6_2atmpS1842;
  uint64_t _M0L2vpS589;
  uint64_t _M0Lm2vmS591;
  int32_t _M0L6_2atmpS1841;
  int32_t _M0L6_2atmpS1840;
  uint64_t _M0L2vrS602;
  uint64_t _M0L6_2atmpS1839;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S573 = _M0L3mulS574.$0;
  _M0L7_2amul1S575 = _M0L3mulS574.$1;
  _M0L1mS576 = _M0L1mS577 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS578 = _M0FPB7umul128(_M0L1mS576, _M0L7_2amul0S573);
  _M0L5_2aloS579 = _M0L7_2abindS578.$0;
  _M0L6_2atmpS580 = _M0L7_2abindS578.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS581 = _M0FPB7umul128(_M0L1mS576, _M0L7_2amul1S575);
  _M0L6_2alo2S582 = _M0L7_2abindS581.$0;
  _M0L6_2ahi2S583 = _M0L7_2abindS581.$1;
  _M0L3midS584 = _M0L6_2atmpS580 + _M0L6_2alo2S582;
  if (_M0L3midS584 < _M0L6_2atmpS580) {
    _M0L6_2atmpS1847 = 1ull;
  } else {
    _M0L6_2atmpS1847 = 0ull;
  }
  _M0L2hiS585 = _M0L6_2ahi2S583 + _M0L6_2atmpS1847;
  _M0L3lo2S586 = _M0L5_2aloS579 + _M0L7_2amul0S573;
  _M0L6_2atmpS1845 = _M0L3midS584 + _M0L7_2amul1S575;
  if (_M0L3lo2S586 < _M0L5_2aloS579) {
    _M0L6_2atmpS1846 = 1ull;
  } else {
    _M0L6_2atmpS1846 = 0ull;
  }
  _M0L4mid2S587 = _M0L6_2atmpS1845 + _M0L6_2atmpS1846;
  if (_M0L4mid2S587 < _M0L3midS584) {
    _M0L6_2atmpS1844 = 1ull;
  } else {
    _M0L6_2atmpS1844 = 0ull;
  }
  _M0L3hi2S588 = _M0L2hiS585 + _M0L6_2atmpS1844;
  _M0L6_2atmpS1843 = _M0L1jS590 - 64;
  _M0L6_2atmpS1842 = _M0L6_2atmpS1843 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS589
  = _M0FPB13shiftright128(_M0L4mid2S587, _M0L3hi2S588, _M0L6_2atmpS1842);
  _M0Lm2vmS591 = 0ull;
  if (_M0L7mmShiftS592) {
    uint64_t _M0L3lo3S593 = _M0L5_2aloS579 - _M0L7_2amul0S573;
    uint64_t _M0L6_2atmpS1829 = _M0L3midS584 - _M0L7_2amul1S575;
    uint64_t _M0L6_2atmpS1830;
    uint64_t _M0L4mid3S594;
    uint64_t _M0L6_2atmpS1828;
    uint64_t _M0L3hi3S595;
    int32_t _M0L6_2atmpS1827;
    int32_t _M0L6_2atmpS1826;
    if (_M0L5_2aloS579 < _M0L3lo3S593) {
      _M0L6_2atmpS1830 = 1ull;
    } else {
      _M0L6_2atmpS1830 = 0ull;
    }
    _M0L4mid3S594 = _M0L6_2atmpS1829 - _M0L6_2atmpS1830;
    if (_M0L3midS584 < _M0L4mid3S594) {
      _M0L6_2atmpS1828 = 1ull;
    } else {
      _M0L6_2atmpS1828 = 0ull;
    }
    _M0L3hi3S595 = _M0L2hiS585 - _M0L6_2atmpS1828;
    _M0L6_2atmpS1827 = _M0L1jS590 - 64;
    _M0L6_2atmpS1826 = _M0L6_2atmpS1827 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS591
    = _M0FPB13shiftright128(_M0L4mid3S594, _M0L3hi3S595, _M0L6_2atmpS1826);
  } else {
    uint64_t _M0L3lo3S596 = _M0L5_2aloS579 + _M0L5_2aloS579;
    uint64_t _M0L6_2atmpS1837 = _M0L3midS584 + _M0L3midS584;
    uint64_t _M0L6_2atmpS1838;
    uint64_t _M0L4mid3S597;
    uint64_t _M0L6_2atmpS1835;
    uint64_t _M0L6_2atmpS1836;
    uint64_t _M0L3hi3S598;
    uint64_t _M0L3lo4S599;
    uint64_t _M0L6_2atmpS1833;
    uint64_t _M0L6_2atmpS1834;
    uint64_t _M0L4mid4S600;
    uint64_t _M0L6_2atmpS1832;
    uint64_t _M0L3hi4S601;
    int32_t _M0L6_2atmpS1831;
    if (_M0L3lo3S596 < _M0L5_2aloS579) {
      _M0L6_2atmpS1838 = 1ull;
    } else {
      _M0L6_2atmpS1838 = 0ull;
    }
    _M0L4mid3S597 = _M0L6_2atmpS1837 + _M0L6_2atmpS1838;
    _M0L6_2atmpS1835 = _M0L2hiS585 + _M0L2hiS585;
    if (_M0L4mid3S597 < _M0L3midS584) {
      _M0L6_2atmpS1836 = 1ull;
    } else {
      _M0L6_2atmpS1836 = 0ull;
    }
    _M0L3hi3S598 = _M0L6_2atmpS1835 + _M0L6_2atmpS1836;
    _M0L3lo4S599 = _M0L3lo3S596 - _M0L7_2amul0S573;
    _M0L6_2atmpS1833 = _M0L4mid3S597 - _M0L7_2amul1S575;
    if (_M0L3lo3S596 < _M0L3lo4S599) {
      _M0L6_2atmpS1834 = 1ull;
    } else {
      _M0L6_2atmpS1834 = 0ull;
    }
    _M0L4mid4S600 = _M0L6_2atmpS1833 - _M0L6_2atmpS1834;
    if (_M0L4mid3S597 < _M0L4mid4S600) {
      _M0L6_2atmpS1832 = 1ull;
    } else {
      _M0L6_2atmpS1832 = 0ull;
    }
    _M0L3hi4S601 = _M0L3hi3S598 - _M0L6_2atmpS1832;
    _M0L6_2atmpS1831 = _M0L1jS590 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS591
    = _M0FPB13shiftright128(_M0L4mid4S600, _M0L3hi4S601, _M0L6_2atmpS1831);
  }
  _M0L6_2atmpS1841 = _M0L1jS590 - 64;
  _M0L6_2atmpS1840 = _M0L6_2atmpS1841 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS602
  = _M0FPB13shiftright128(_M0L3midS584, _M0L2hiS585, _M0L6_2atmpS1840);
  _M0L6_2atmpS1839 = _M0Lm2vmS591;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS602,
                                                .$1 = _M0L2vpS589,
                                                .$2 = _M0L6_2atmpS1839};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS571,
  int32_t _M0L1pS572
) {
  uint64_t _M0L6_2atmpS1825;
  uint64_t _M0L6_2atmpS1824;
  uint64_t _M0L6_2atmpS1823;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1825 = 1ull << (_M0L1pS572 & 63);
  _M0L6_2atmpS1824 = _M0L6_2atmpS1825 - 1ull;
  _M0L6_2atmpS1823 = _M0L5valueS571 & _M0L6_2atmpS1824;
  return _M0L6_2atmpS1823 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS569,
  int32_t _M0L1pS570
) {
  int32_t _M0L6_2atmpS1822;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1822 = _M0FPB10pow5Factor(_M0L5valueS569);
  return _M0L6_2atmpS1822 >= _M0L1pS570;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS564) {
  uint64_t _M0L6_2atmpS1813;
  uint64_t _M0L6_2atmpS1814;
  uint64_t _M0L6_2atmpS1815;
  uint64_t _M0L6_2atmpS1816;
  uint64_t _M0L6_2atmpS1821;
  int32_t _M0L5countS565;
  uint64_t _M0L1vS566;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1813 = _M0L5valueS564 % 5ull;
  if (_M0L6_2atmpS1813 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1814 = _M0L5valueS564 % 25ull;
  if (_M0L6_2atmpS1814 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1815 = _M0L5valueS564 % 125ull;
  if (_M0L6_2atmpS1815 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1816 = _M0L5valueS564 % 625ull;
  if (_M0L6_2atmpS1816 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1821 = _M0L5valueS564 / 625ull;
  _M0L5countS565 = 4;
  _M0L1vS566 = _M0L6_2atmpS1821;
  while (1) {
    if (_M0L1vS566 > 0ull) {
      uint64_t _M0L6_2atmpS1817 = _M0L1vS566 % 5ull;
      int32_t _M0L6_2atmpS1818;
      uint64_t _M0L6_2atmpS1819;
      if (_M0L6_2atmpS1817 != 0ull) {
        return _M0L5countS565;
      }
      _M0L6_2atmpS1818 = _M0L5countS565 + 1;
      _M0L6_2atmpS1819 = _M0L1vS566 / 5ull;
      _M0L5countS565 = _M0L6_2atmpS1818;
      _M0L1vS566 = _M0L6_2atmpS1819;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS568;
      moonbit_string_t _M0L6_2atmpS1820;
      int32_t _result_2743;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS568
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS568, (moonbit_string_t)moonbit_string_literal_15.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS568, _M0L5valueS564);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1820
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS568);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS568);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2743 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1820);
      moonbit_decref_cycle_free(_M0L6_2atmpS1820);
      return _result_2743;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS563,
  uint64_t _M0L2hiS561,
  int32_t _M0L4distS562
) {
  int32_t _M0L6_2atmpS1812;
  uint64_t _M0L6_2atmpS1810;
  uint64_t _M0L6_2atmpS1811;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1812 = 64 - _M0L4distS562;
  _M0L6_2atmpS1810 = _M0L2hiS561 << (_M0L6_2atmpS1812 & 63);
  _M0L6_2atmpS1811 = _M0L2loS563 >> (_M0L4distS562 & 63);
  return _M0L6_2atmpS1810 | _M0L6_2atmpS1811;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS551,
  uint64_t _M0L1bS554
) {
  uint64_t _M0L3aLoS550;
  uint64_t _M0L3aHiS552;
  uint64_t _M0L3bLoS553;
  uint64_t _M0L3bHiS555;
  uint64_t _M0L1xS556;
  uint64_t _M0L6_2atmpS1808;
  uint64_t _M0L6_2atmpS1809;
  uint64_t _M0L1yS557;
  uint64_t _M0L6_2atmpS1806;
  uint64_t _M0L6_2atmpS1807;
  uint64_t _M0L1zS558;
  uint64_t _M0L6_2atmpS1804;
  uint64_t _M0L6_2atmpS1805;
  uint64_t _M0L6_2atmpS1802;
  uint64_t _M0L6_2atmpS1803;
  uint64_t _M0L1wS559;
  uint64_t _M0L2loS560;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS550 = _M0L1aS551 & 4294967295ull;
  _M0L3aHiS552 = _M0L1aS551 >> 32;
  _M0L3bLoS553 = _M0L1bS554 & 4294967295ull;
  _M0L3bHiS555 = _M0L1bS554 >> 32;
  _M0L1xS556 = _M0L3aLoS550 * _M0L3bLoS553;
  _M0L6_2atmpS1808 = _M0L3aHiS552 * _M0L3bLoS553;
  _M0L6_2atmpS1809 = _M0L1xS556 >> 32;
  _M0L1yS557 = _M0L6_2atmpS1808 + _M0L6_2atmpS1809;
  _M0L6_2atmpS1806 = _M0L3aLoS550 * _M0L3bHiS555;
  _M0L6_2atmpS1807 = _M0L1yS557 & 4294967295ull;
  _M0L1zS558 = _M0L6_2atmpS1806 + _M0L6_2atmpS1807;
  _M0L6_2atmpS1804 = _M0L3aHiS552 * _M0L3bHiS555;
  _M0L6_2atmpS1805 = _M0L1yS557 >> 32;
  _M0L6_2atmpS1802 = _M0L6_2atmpS1804 + _M0L6_2atmpS1805;
  _M0L6_2atmpS1803 = _M0L1zS558 >> 32;
  _M0L1wS559 = _M0L6_2atmpS1802 + _M0L6_2atmpS1803;
  _M0L2loS560 = _M0L1aS551 * _M0L1bS554;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS560, .$1 = _M0L1wS559};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS548,
  int32_t _M0L4fromS545,
  int32_t _M0L2toS544
) {
  int32_t _M0L3lenS543;
  int32_t _M0L6_2atmpS1801;
  uint16_t* _M0L6bufferS546;
  int32_t _M0L1iS547;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS543 = _M0L2toS544 - _M0L4fromS545;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1801 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS546
  = (uint16_t*)moonbit_make_string(_M0L3lenS543, _M0L6_2atmpS1801);
  _M0L1iS547 = 0;
  while (1) {
    if (_M0L1iS547 < _M0L3lenS543) {
      int32_t _M0L6_2atmpS1799 = _M0L4fromS545 + _M0L1iS547;
      int32_t _M0L6_2atmpS1798;
      int32_t _M0L6_2atmpS1797;
      int32_t _M0L6_2atmpS1800;
      if (
        _M0L6_2atmpS1799 < 0
        || _M0L6_2atmpS1799 >= Moonbit_array_length(_M0L5bytesS548)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1798 = (int32_t)_M0L5bytesS548[_M0L6_2atmpS1799];
      _M0L6_2atmpS1797 = (uint16_t)_M0L6_2atmpS1798;
      if (
        _M0L1iS547 < 0 || _M0L1iS547 >= Moonbit_array_length(_M0L6bufferS546)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS546[_M0L1iS547] = _M0L6_2atmpS1797;
      _M0L6_2atmpS1800 = _M0L1iS547 + 1;
      _M0L1iS547 = _M0L6_2atmpS1800;
      continue;
    }
    break;
  }
  return _M0L6bufferS546;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS1796;
  uint32_t _M0L6_2atmpS1795;
  uint32_t _M0L6_2atmpS1794;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1796 = _M0L1eS542 * 78913;
  _M0L6_2atmpS1795 = *(uint32_t*)&_M0L6_2atmpS1796;
  _M0L6_2atmpS1794 = _M0L6_2atmpS1795 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1794;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS541) {
  int32_t _M0L6_2atmpS1793;
  uint32_t _M0L6_2atmpS1792;
  uint32_t _M0L6_2atmpS1791;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1793 = _M0L1eS541 * 732923;
  _M0L6_2atmpS1792 = *(uint32_t*)&_M0L6_2atmpS1793;
  _M0L6_2atmpS1791 = _M0L6_2atmpS1792 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1791;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS539,
  int32_t _M0L8exponentS540,
  int32_t _M0L8mantissaS537
) {
  moonbit_string_t _M0L1sS538;
  moonbit_string_t _result_2746;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS537) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
  if (_M0L4signS539) {
    _M0L1sS538 = (moonbit_string_t)moonbit_string_literal_17.data;
  } else {
    _M0L1sS538 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS540) {
    moonbit_string_t _result_2745;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2745
    = moonbit_add_string(_M0L1sS538, (moonbit_string_t)moonbit_string_literal_18.data);
    moonbit_decref_cycle_free(_M0L1sS538);
    return _result_2745;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2746
  = moonbit_add_string(_M0L1sS538, (moonbit_string_t)moonbit_string_literal_19.data);
  moonbit_decref_cycle_free(_M0L1sS538);
  return _result_2746;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS536) {
  int32_t _M0L6_2atmpS1790;
  uint32_t _M0L6_2atmpS1789;
  uint32_t _M0L6_2atmpS1788;
  int32_t _M0L6_2atmpS1787;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1790 = _M0L1eS536 * 1217359;
  _M0L6_2atmpS1789 = *(uint32_t*)&_M0L6_2atmpS1790;
  _M0L6_2atmpS1788 = _M0L6_2atmpS1789 >> 19;
  _M0L6_2atmpS1787 = *(int32_t*)&_M0L6_2atmpS1788;
  return _M0L6_2atmpS1787 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS535) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS535 != _M0L4selfS535) {
    return 0;
  } else if (_M0L4selfS535 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS535 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS535;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS534) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS534 != _M0L4selfS534) {
    return 0ll;
  } else if (_M0L4selfS534 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS534 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS534;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS530
) {
  float* _M0L6_2atmpS1783;
  struct _M0TPB5ArrayGfE* _block_2747;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1783 = (float*)moonbit_make_float_array_raw(_M0L3lenS530);
  _block_2747
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2747)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2747->$0 = _M0L6_2atmpS1783;
  _block_2747->$1 = _M0L3lenS530;
  return _block_2747;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS531
) {
  uint8_t* _M0L6_2atmpS1784;
  struct _M0TPB5ArrayGbE* _block_2748;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1784 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS531);
  _block_2748
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2748)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 138, 0);
  _block_2748->$0 = _M0L6_2atmpS1784;
  _block_2748->$1 = _M0L3lenS531;
  return _block_2748;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS532
) {
  int32_t* _M0L6_2atmpS1785;
  struct _M0TPB5ArrayGiE* _block_2749;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1785 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS532);
  _block_2749
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2749)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2749->$0 = _M0L6_2atmpS1785;
  _block_2749->$1 = _M0L3lenS532;
  return _block_2749;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS533
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1786;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2750;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1786
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS533, 0);
  _block_2750
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2750)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 141, 0);
  _block_2750->$0 = _M0L6_2atmpS1786;
  _block_2750->$1 = _M0L3lenS533;
  return _block_2750;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS526,
  int32_t _M0L5indexS527
) {
  uint64_t* _M0L6_2atmpS1781;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1781 = _M0L4selfS526;
  if (
    _M0L5indexS527 < 0
    || _M0L5indexS527 >= Moonbit_array_length(_M0L6_2atmpS1781)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1781[_M0L5indexS527];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS528,
  int32_t _M0L5indexS529
) {
  uint32_t* _M0L6_2atmpS1782;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1782 = _M0L4selfS528;
  if (
    _M0L5indexS529 < 0
    || _M0L5indexS529 >= Moonbit_array_length(_M0L6_2atmpS1782)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1782[_M0L5indexS529];
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

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS511,
  float _M0L5valueS513
) {
  int32_t _M0L3lenS1753;
  float* _M0L6_2atmpS1755;
  int32_t _M0L6_2atmpS1754;
  int32_t _M0L6lengthS512;
  float* _M0L3bufS1758;
  int32_t _M0L6_2atmpS1759;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1753 = _M0L4selfS511->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1755 = _M0MPC15array5Array6bufferGfE(_M0L4selfS511);
  _M0L6_2atmpS1754 = Moonbit_array_length(_M0L6_2atmpS1755);
  moonbit_decref_cycle_free(_M0L6_2atmpS1755);
  if (_M0L3lenS1753 == _M0L6_2atmpS1754) {
    int32_t _M0L3lenS1757 = _M0L4selfS511->$1;
    int32_t _M0L6_2atmpS1756 = _M0L3lenS1757 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS511, _M0L6_2atmpS1756);
  }
  _M0L6lengthS512 = _M0L4selfS511->$1;
  _M0L3bufS1758 = _M0L4selfS511->$0;
  _M0L3bufS1758[_M0L6lengthS512] = _M0L5valueS513;
  _M0L6_2atmpS1759 = _M0L6lengthS512 + 1;
  _M0L4selfS511->$1 = _M0L6_2atmpS1759;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS514,
  int32_t _M0L5valueS516
) {
  int32_t _M0L3lenS1760;
  int32_t* _M0L6_2atmpS1762;
  int32_t _M0L6_2atmpS1761;
  int32_t _M0L6lengthS515;
  int32_t* _M0L3bufS1765;
  int32_t _M0L6_2atmpS1766;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1760 = _M0L4selfS514->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1762 = _M0MPC15array5Array6bufferGiE(_M0L4selfS514);
  _M0L6_2atmpS1761 = Moonbit_array_length(_M0L6_2atmpS1762);
  moonbit_decref_cycle_free(_M0L6_2atmpS1762);
  if (_M0L3lenS1760 == _M0L6_2atmpS1761) {
    int32_t _M0L3lenS1764 = _M0L4selfS514->$1;
    int32_t _M0L6_2atmpS1763 = _M0L3lenS1764 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS514, _M0L6_2atmpS1763);
  }
  _M0L6lengthS515 = _M0L4selfS514->$1;
  _M0L3bufS1765 = _M0L4selfS514->$0;
  _M0L3bufS1765[_M0L6lengthS515] = _M0L5valueS516;
  _M0L6_2atmpS1766 = _M0L6lengthS515 + 1;
  _M0L4selfS514->$1 = _M0L6_2atmpS1766;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS517,
  moonbit_string_t _M0L5valueS519
) {
  int32_t _M0L3lenS1767;
  moonbit_string_t* _M0L6_2atmpS1769;
  int32_t _M0L6_2atmpS1768;
  int32_t _M0L6lengthS518;
  moonbit_string_t* _M0L3bufS1772;
  moonbit_string_t _M0L6_2aoldS2616;
  int32_t _M0L6_2atmpS1773;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1767 = _M0L4selfS517->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1769 = _M0MPC15array5Array6bufferGsE(_M0L4selfS517);
  _M0L6_2atmpS1768 = Moonbit_array_length(_M0L6_2atmpS1769);
  moonbit_decref_cycle_free(_M0L6_2atmpS1769);
  if (_M0L3lenS1767 == _M0L6_2atmpS1768) {
    int32_t _M0L3lenS1771 = _M0L4selfS517->$1;
    int32_t _M0L6_2atmpS1770 = _M0L3lenS1771 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS517, _M0L6_2atmpS1770);
  }
  _M0L6lengthS518 = _M0L4selfS517->$1;
  _M0L3bufS1772 = _M0L4selfS517->$0;
  _M0L6_2aoldS2616 = (moonbit_string_t)_M0L3bufS1772[_M0L6lengthS518];
  moonbit_decref_cycle_free(_M0L6_2aoldS2616);
  _M0L3bufS1772[_M0L6lengthS518] = _M0L5valueS519;
  _M0L6_2atmpS1773 = _M0L6lengthS518 + 1;
  _M0L4selfS517->$1 = _M0L6_2atmpS1773;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS520,
  struct _M0TUsiE* _M0L5valueS522
) {
  int32_t _M0L3lenS1774;
  struct _M0TUsiE** _M0L6_2atmpS1776;
  int32_t _M0L6_2atmpS1775;
  int32_t _M0L6lengthS521;
  struct _M0TUsiE** _M0L3bufS1779;
  struct _M0TUsiE* _M0L6_2aoldS2617;
  int32_t _M0L6_2atmpS1780;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1774 = _M0L4selfS520->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1776 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS520);
  _M0L6_2atmpS1775 = Moonbit_array_length(_M0L6_2atmpS1776);
  moonbit_decref_cycle_free(_M0L6_2atmpS1776);
  if (_M0L3lenS1774 == _M0L6_2atmpS1775) {
    int32_t _M0L3lenS1778 = _M0L4selfS520->$1;
    int32_t _M0L6_2atmpS1777 = _M0L3lenS1778 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS520, _M0L6_2atmpS1777);
  }
  _M0L6lengthS521 = _M0L4selfS520->$1;
  _M0L3bufS1779 = _M0L4selfS520->$0;
  _M0L6_2aoldS2617 = (struct _M0TUsiE*)_M0L3bufS1779[_M0L6lengthS521];
  if (_M0L6_2aoldS2617) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2617);
  }
  _M0L3bufS1779[_M0L6lengthS521] = _M0L5valueS522;
  _M0L6_2atmpS1780 = _M0L6lengthS521 + 1;
  _M0L4selfS520->$1 = _M0L6_2atmpS1780;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS496,
  int32_t _M0L8requiredS498
) {
  int32_t _M0L8old__capS495;
  int32_t _M0L3lenS1749;
  int32_t _M0L8new__capS497;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS495 = _M0MPC15array5Array8capacityGfE(_M0L4selfS496);
  _M0L3lenS1749 = _M0L4selfS496->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS497
  = _M0FPB23array__growth__capacity(_M0L8old__capS495, _M0L3lenS1749, _M0L8requiredS498);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS496, _M0L8new__capS497);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS500,
  int32_t _M0L8requiredS502
) {
  int32_t _M0L8old__capS499;
  int32_t _M0L3lenS1750;
  int32_t _M0L8new__capS501;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS499 = _M0MPC15array5Array8capacityGiE(_M0L4selfS500);
  _M0L3lenS1750 = _M0L4selfS500->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS501
  = _M0FPB23array__growth__capacity(_M0L8old__capS499, _M0L3lenS1750, _M0L8requiredS502);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS500, _M0L8new__capS501);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS504,
  int32_t _M0L8requiredS506
) {
  int32_t _M0L8old__capS503;
  int32_t _M0L3lenS1751;
  int32_t _M0L8new__capS505;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS503 = _M0MPC15array5Array8capacityGsE(_M0L4selfS504);
  _M0L3lenS1751 = _M0L4selfS504->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS505
  = _M0FPB23array__growth__capacity(_M0L8old__capS503, _M0L3lenS1751, _M0L8requiredS506);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS504, _M0L8new__capS505);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS508,
  int32_t _M0L8requiredS510
) {
  int32_t _M0L8old__capS507;
  int32_t _M0L3lenS1752;
  int32_t _M0L8new__capS509;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS507 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS508);
  _M0L3lenS1752 = _M0L4selfS508->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS509
  = _M0FPB23array__growth__capacity(_M0L8old__capS507, _M0L3lenS1752, _M0L8requiredS510);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS508, _M0L8new__capS509);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS472,
  int32_t _M0L13new__capacityS475
) {
  float* _M0L8old__bufS471;
  int32_t _M0L3lenS473;
  int32_t _M0L9copy__lenS474;
  float* _M0L8new__bufS476;
  float* _M0L6_2aoldS2618;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS471, _M0L13new__capacityS475, _M0L9copy__lenS474, 0, 0);
  _M0L6_2aoldS2618 = _M0L4selfS472->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2618);
  _M0L4selfS472->$0 = _M0L8new__bufS476;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS478,
  int32_t _M0L13new__capacityS481
) {
  int32_t* _M0L8old__bufS477;
  int32_t _M0L3lenS479;
  int32_t _M0L9copy__lenS480;
  int32_t* _M0L8new__bufS482;
  int32_t* _M0L6_2aoldS2619;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS477, _M0L13new__capacityS481, _M0L9copy__lenS480, 0, 0);
  _M0L6_2aoldS2619 = _M0L4selfS478->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2619);
  _M0L4selfS478->$0 = _M0L8new__bufS482;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS484,
  int32_t _M0L13new__capacityS487
) {
  moonbit_string_t* _M0L8old__bufS483;
  int32_t _M0L3lenS485;
  int32_t _M0L9copy__lenS486;
  moonbit_string_t* _M0L8new__bufS488;
  moonbit_string_t* _M0L6_2aoldS2620;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS483, _M0L13new__capacityS487, _M0L9copy__lenS486, 0, 0);
  _M0L6_2aoldS2620 = _M0L4selfS484->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2620);
  _M0L4selfS484->$0 = _M0L8new__bufS488;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS490,
  int32_t _M0L13new__capacityS493
) {
  struct _M0TUsiE** _M0L8old__bufS489;
  int32_t _M0L3lenS491;
  int32_t _M0L9copy__lenS492;
  struct _M0TUsiE** _M0L8new__bufS494;
  struct _M0TUsiE** _M0L6_2aoldS2621;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS489, _M0L13new__capacityS493, _M0L9copy__lenS492, 0, 0);
  _M0L6_2aoldS2621 = _M0L4selfS490->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2621);
  _M0L4selfS490->$0 = _M0L8new__bufS494;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS467
) {
  float* _M0L6_2atmpS1745;
  int32_t _result_2751;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1745 = _M0MPC15array5Array6bufferGfE(_M0L4selfS467);
  _result_2751 = Moonbit_array_length(_M0L6_2atmpS1745);
  moonbit_decref_cycle_free(_M0L6_2atmpS1745);
  return _result_2751;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS468
) {
  int32_t* _M0L6_2atmpS1746;
  int32_t _result_2752;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1746 = _M0MPC15array5Array6bufferGiE(_M0L4selfS468);
  _result_2752 = Moonbit_array_length(_M0L6_2atmpS1746);
  moonbit_decref_cycle_free(_M0L6_2atmpS1746);
  return _result_2752;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS469
) {
  moonbit_string_t* _M0L6_2atmpS1747;
  int32_t _result_2753;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1747 = _M0MPC15array5Array6bufferGsE(_M0L4selfS469);
  _result_2753 = Moonbit_array_length(_M0L6_2atmpS1747);
  moonbit_decref_cycle_free(_M0L6_2atmpS1747);
  return _result_2753;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS470
) {
  struct _M0TUsiE** _M0L6_2atmpS1748;
  int32_t _result_2754;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1748 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS470);
  _result_2754 = Moonbit_array_length(_M0L6_2atmpS1748);
  moonbit_decref_cycle_free(_M0L6_2atmpS1748);
  return _result_2754;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
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

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS452) {
  uint8_t* _M0L8_2afieldS2622;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2622 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2622);
  return _M0L8_2afieldS2622;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS453) {
  float* _M0L8_2afieldS2623;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2623 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2623);
  return _M0L8_2afieldS2623;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS454) {
  int32_t* _M0L8_2afieldS2624;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2624 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2624);
  return _M0L8_2afieldS2624;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS2625;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2625 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2625);
  return _M0L8_2afieldS2625;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS2626;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2626 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2626);
  return _M0L8_2afieldS2626;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS457
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2627;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2627 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2627);
  return _M0L8_2afieldS2627;
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
  int32_t _M0L3endS1743;
  int32_t _M0L5startS1744;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1742;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1735;
  int32_t _M0L6_2atmpS1734;
  int32_t _if__result_2756;
  uint16_t* _M0L4dataS1736;
  int32_t _M0L3lenS1737;
  moonbit_string_t _M0L6_2atmpS1738;
  int32_t _M0L6_2atmpS1739;
  int32_t _M0L3lenS1741;
  int32_t _M0L6_2atmpS1740;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1743 = _M0L3strS448.$2;
  _M0L5startS1744 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1743 - _M0L5startS1744;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1742 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1742 + _M0L8str__lenS447;
  _M0L4dataS1735 = _M0L4selfS450->$0;
  _M0L6_2atmpS1734 = Moonbit_array_length(_M0L4dataS1735);
  if (_M0L8requiredS449 > _M0L6_2atmpS1734) {
    _if__result_2756 = 1;
  } else {
    int32_t _M0L3lenS1733 = _M0L4selfS450->$1;
    _if__result_2756 = _M0L8requiredS449 < _M0L3lenS1733;
  }
  if (_if__result_2756) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1736 = _M0L4selfS450->$0;
  _M0L3lenS1737 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1736);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1738 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1739 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1736, _M0L3lenS1737, _M0L6_2atmpS1738, _M0L6_2atmpS1739, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1736);
  moonbit_decref_cycle_free(_M0L6_2atmpS1738);
  _M0L3lenS1741 = _M0L4selfS450->$1;
  _M0L6_2atmpS1740 = _M0L3lenS1741 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1740;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2757;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1732;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1731;
  moonbit_string_t _result_2758;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1730 = Moonbit_array_length(_M0L3strS444);
    _if__result_2757 = _M0L3endS443 == _M0L6_2atmpS1730;
  } else {
    _if__result_2757 = 0;
  }
  if (_if__result_2757) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1732 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1732, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1731 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2758
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1731, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1731);
  return _result_2758;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2759;
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
      int32_t _M0L6_2atmpS1729 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2759 = _M0L6_2atmpS1729 <= _M0L3lenS436;
    } else {
      _if__result_2759 = 0;
    }
  } else {
    _if__result_2759 = 0;
  }
  if (_if__result_2759) {
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
  int32_t _M0L6_2atmpS1728;
  int32_t _M0L6_2atmpS1727;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1726;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1728 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1727 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1728;
  _M0L2e1S422 = _M0L6_2atmpS1727 - 1;
  _M0L6_2atmpS1726 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1726 - 1;
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
        int32_t _M0L6_2atmpS1723 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1722 = (int32_t)_M0L6_2atmpS1723;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1722;
        uint32_t _M0L6_2atmpS1718 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1717;
        int32_t _M0L6_2atmpS1719;
        uint32_t _M0L6_2atmpS1721;
        int32_t _M0L6_2atmpS1720;
        int32_t _M0L6_2atmpS1724;
        int32_t _M0L6_2atmpS1725;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1717 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1718);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1717;
        _M0L6_2atmpS1719 = _M0L1jS433 + 1;
        _M0L6_2atmpS1721 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1720 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1721);
        if (
          _M0L6_2atmpS1719 < 0
          || _M0L6_2atmpS1719 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1719] = _M0L6_2atmpS1720;
        _M0L6_2atmpS1724 = _M0L1iS432 + 1;
        _M0L6_2atmpS1725 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1724;
        _M0L1jS433 = _M0L6_2atmpS1725;
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
  int32_t _M0L6_2atmpS1716;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1716 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1716 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1715 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1715;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1712;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1712 = 1;
      } else {
        _M0L6_2atmpS1712 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1712;
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
      int32_t _M0L6_2atmpS1713;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1713 = 1;
      } else {
        _M0L6_2atmpS1713 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1713;
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
      int32_t _M0L6_2atmpS1714;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1714 = 1;
      } else {
        _M0L6_2atmpS1714 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1714;
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
  int32_t _M0L6_2atmpS1711;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1711 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1711;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1688 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1688;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1687 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1686 = 48 + _M0L6_2atmpS1687;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1686;
      int32_t _M0L6_2atmpS1685 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1684 = 48 + _M0L6_2atmpS1685;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1684;
      int32_t _M0L6_2atmpS1683 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1682 = 48 + _M0L6_2atmpS1683;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1682;
      int32_t _M0L6_2atmpS1681 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1680 = 48 + _M0L6_2atmpS1681;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1680;
      int32_t _M0L6_2atmpS1672 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1671 = _M0L6_2atmpS1672 - 4;
      int32_t _M0L6_2atmpS1674;
      int32_t _M0L6_2atmpS1673;
      int32_t _M0L6_2atmpS1676;
      int32_t _M0L6_2atmpS1675;
      int32_t _M0L6_2atmpS1678;
      int32_t _M0L6_2atmpS1677;
      int32_t _M0L6_2atmpS1679;
      _M0L6bufferS381[_M0L6_2atmpS1671] = _M0L6d1__hiS377;
      _M0L6_2atmpS1674 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1673 = _M0L6_2atmpS1674 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1673] = _M0L6d1__loS378;
      _M0L6_2atmpS1676 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1675 = _M0L6_2atmpS1676 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1675] = _M0L6d2__hiS379;
      _M0L6_2atmpS1678 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1677 = _M0L6_2atmpS1678 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1677] = _M0L6d2__loS380;
      _M0L6_2atmpS1679 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1679;
      continue;
    } else {
      int32_t _M0L6_2atmpS1710 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1710;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1697 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1696 = 48 + _M0L6_2atmpS1697;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1696;
          int32_t _M0L6_2atmpS1695 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1694 = 48 + _M0L6_2atmpS1695;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1694;
          int32_t _M0L6_2atmpS1690 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1689 = _M0L6_2atmpS1690 - 2;
          int32_t _M0L6_2atmpS1692;
          int32_t _M0L6_2atmpS1691;
          int32_t _M0L6_2atmpS1693;
          _M0L6bufferS381[_M0L6_2atmpS1689] = _M0L5d__hiS388;
          _M0L6_2atmpS1692 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1691 = _M0L6_2atmpS1692 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1691] = _M0L5d__loS389;
          _M0L6_2atmpS1693 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1693;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1705 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1704 = 48 + _M0L6_2atmpS1705;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1704;
          int32_t _M0L6_2atmpS1703 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1702 = 48 + _M0L6_2atmpS1703;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1702;
          int32_t _M0L6_2atmpS1699 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1698 = _M0L6_2atmpS1699 - 2;
          int32_t _M0L6_2atmpS1701;
          int32_t _M0L6_2atmpS1700;
          _M0L6bufferS381[_M0L6_2atmpS1698] = _M0L5d__hiS391;
          _M0L6_2atmpS1701 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1700 = _M0L6_2atmpS1701 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1700] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1709 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1706 = _M0L6_2atmpS1709 - 1;
          int32_t _M0L6_2atmpS1708 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1707 = (uint16_t)_M0L6_2atmpS1708;
          _M0L6bufferS381[_M0L6_2atmpS1706] = _M0L6_2atmpS1707;
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
  int32_t _M0L6_2atmpS1656;
  int32_t _M0L6_2atmpS1655;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1656 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1655 = _M0L5radixS355 & _M0L6_2atmpS1656;
  if (_M0L6_2atmpS1655 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1663;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1663 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1663;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1662 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1662;
        int32_t _M0L6_2atmpS1659 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1657 = _M0L6_2atmpS1659 - 1;
        int32_t _M0L6_2atmpS1658 =
          ((moonbit_string_t)moonbit_string_literal_22.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1660;
        uint64_t _M0L6_2atmpS1661;
        _M0L6bufferS361[_M0L6_2atmpS1657] = _M0L6_2atmpS1658;
        _M0L6_2atmpS1660 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1661 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1660;
        _M0L1nS359 = _M0L6_2atmpS1661;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1670 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1670;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1669 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1668 = _M0L1nS367 - _M0L6_2atmpS1669;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1668;
        int32_t _M0L6_2atmpS1666 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1664 = _M0L6_2atmpS1666 - 1;
        int32_t _M0L6_2atmpS1665 =
          ((moonbit_string_t)moonbit_string_literal_22.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1667;
        _M0L6bufferS361[_M0L6_2atmpS1664] = _M0L6_2atmpS1665;
        _M0L6_2atmpS1667 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1667;
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
  int32_t _M0L6_2atmpS1654;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1654 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1654;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1651 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1651;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1645 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1643 = _M0L6_2atmpS1645 - 2;
      int32_t _M0L6_2atmpS1644 =
        ((moonbit_string_t)moonbit_string_literal_22.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1648;
      int32_t _M0L6_2atmpS1646;
      int32_t _M0L6_2atmpS1647;
      int32_t _M0L6_2atmpS1649;
      uint64_t _M0L6_2atmpS1650;
      _M0L6bufferS348[_M0L6_2atmpS1643] = _M0L6_2atmpS1644;
      _M0L6_2atmpS1648 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1646 = _M0L6_2atmpS1648 - 1;
      _M0L6_2atmpS1647
      = ((moonbit_string_t)moonbit_string_literal_22.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1646] = _M0L6_2atmpS1647;
      _M0L6_2atmpS1649 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1650 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1649;
      _M0L1nS344 = _M0L6_2atmpS1650;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1653 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1653;
      int32_t _M0L6_2atmpS1652 =
        ((moonbit_string_t)moonbit_string_literal_22.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1652;
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
      uint64_t _M0L6_2atmpS1641 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1642 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1641;
      _M0L5countS341 = _M0L6_2atmpS1642;
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
    int32_t _M0L6_2atmpS1640;
    int32_t _M0L6_2atmpS1639;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1640 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1639 = _M0L6_2atmpS1640 / 4;
    return _M0L6_2atmpS1639 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1638 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1638;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1635;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1635 = 1;
      } else {
        _M0L6_2atmpS1635 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1635;
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
      int32_t _M0L6_2atmpS1636;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1636 = 1;
      } else {
        _M0L6_2atmpS1636 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1636;
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
      int32_t _M0L6_2atmpS1637;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1637 = 1;
      } else {
        _M0L6_2atmpS1637 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1637;
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
      uint32_t _M0L6_2atmpS1633 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1634 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1633;
      _M0L5countS315 = _M0L6_2atmpS1634;
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
    int32_t _M0L6_2atmpS1632;
    int32_t _M0L6_2atmpS1631;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1632 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1631 = _M0L6_2atmpS1632 / 4;
    return _M0L6_2atmpS1631 + 1;
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
  int32_t _M0L6_2atmpS1630;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1630 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1630;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1607 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1607;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1606 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1605 = 48 + _M0L6_2atmpS1606;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1605;
      int32_t _M0L6_2atmpS1604 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1603 = 48 + _M0L6_2atmpS1604;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1603;
      int32_t _M0L6_2atmpS1602 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1601 = 48 + _M0L6_2atmpS1602;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1601;
      int32_t _M0L6_2atmpS1600 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1599 = 48 + _M0L6_2atmpS1600;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1599;
      int32_t _M0L6_2atmpS1591 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1590 = _M0L6_2atmpS1591 - 4;
      int32_t _M0L6_2atmpS1593;
      int32_t _M0L6_2atmpS1592;
      int32_t _M0L6_2atmpS1595;
      int32_t _M0L6_2atmpS1594;
      int32_t _M0L6_2atmpS1597;
      int32_t _M0L6_2atmpS1596;
      int32_t _M0L6_2atmpS1598;
      _M0L6bufferS294[_M0L6_2atmpS1590] = _M0L6d1__hiS290;
      _M0L6_2atmpS1593 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1592 = _M0L6_2atmpS1593 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1592] = _M0L6d1__loS291;
      _M0L6_2atmpS1595 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1594 = _M0L6_2atmpS1595 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1594] = _M0L6d2__hiS292;
      _M0L6_2atmpS1597 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1596 = _M0L6_2atmpS1597 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1596] = _M0L6d2__loS293;
      _M0L6_2atmpS1598 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1598;
      continue;
    } else {
      int32_t _M0L6_2atmpS1629 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1629;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1616 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1615 = 48 + _M0L6_2atmpS1616;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1615;
          int32_t _M0L6_2atmpS1614 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1613 = 48 + _M0L6_2atmpS1614;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1613;
          int32_t _M0L6_2atmpS1609 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1608 = _M0L6_2atmpS1609 - 2;
          int32_t _M0L6_2atmpS1611;
          int32_t _M0L6_2atmpS1610;
          int32_t _M0L6_2atmpS1612;
          _M0L6bufferS294[_M0L6_2atmpS1608] = _M0L5d__hiS301;
          _M0L6_2atmpS1611 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1610 = _M0L6_2atmpS1611 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1610] = _M0L5d__loS302;
          _M0L6_2atmpS1612 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1612;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1624 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1623 = 48 + _M0L6_2atmpS1624;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1623;
          int32_t _M0L6_2atmpS1622 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1621 = 48 + _M0L6_2atmpS1622;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1621;
          int32_t _M0L6_2atmpS1618 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1617 = _M0L6_2atmpS1618 - 2;
          int32_t _M0L6_2atmpS1620;
          int32_t _M0L6_2atmpS1619;
          _M0L6bufferS294[_M0L6_2atmpS1617] = _M0L5d__hiS304;
          _M0L6_2atmpS1620 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1619 = _M0L6_2atmpS1620 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1619] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1628 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1625 = _M0L6_2atmpS1628 - 1;
          int32_t _M0L6_2atmpS1627 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1626 = (uint16_t)_M0L6_2atmpS1627;
          _M0L6bufferS294[_M0L6_2atmpS1625] = _M0L6_2atmpS1626;
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
  int32_t _M0L6_2atmpS1575;
  int32_t _M0L6_2atmpS1574;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1575 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1574 = _M0L5radixS268 & _M0L6_2atmpS1575;
  if (_M0L6_2atmpS1574 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1582;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1582 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1582;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1581 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1581;
        int32_t _M0L6_2atmpS1578 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1576 = _M0L6_2atmpS1578 - 1;
        int32_t _M0L6_2atmpS1577 =
          ((moonbit_string_t)moonbit_string_literal_22.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1579;
        uint32_t _M0L6_2atmpS1580;
        _M0L6bufferS274[_M0L6_2atmpS1576] = _M0L6_2atmpS1577;
        _M0L6_2atmpS1579 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1580 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1579;
        _M0L1nS272 = _M0L6_2atmpS1580;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1589 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1589;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1588 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1587 = _M0L1nS280 - _M0L6_2atmpS1588;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1587;
        int32_t _M0L6_2atmpS1585 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1583 = _M0L6_2atmpS1585 - 1;
        int32_t _M0L6_2atmpS1584 =
          ((moonbit_string_t)moonbit_string_literal_22.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1586;
        _M0L6bufferS274[_M0L6_2atmpS1583] = _M0L6_2atmpS1584;
        _M0L6_2atmpS1586 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1586;
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
  int32_t _M0L6_2atmpS1573;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1573 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1573;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1570 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1570;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1564 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1562 = _M0L6_2atmpS1564 - 2;
      int32_t _M0L6_2atmpS1563 =
        ((moonbit_string_t)moonbit_string_literal_22.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1567;
      int32_t _M0L6_2atmpS1565;
      int32_t _M0L6_2atmpS1566;
      int32_t _M0L6_2atmpS1568;
      uint32_t _M0L6_2atmpS1569;
      _M0L6bufferS261[_M0L6_2atmpS1562] = _M0L6_2atmpS1563;
      _M0L6_2atmpS1567 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1565 = _M0L6_2atmpS1567 - 1;
      _M0L6_2atmpS1566
      = ((moonbit_string_t)moonbit_string_literal_22.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1565] = _M0L6_2atmpS1566;
      _M0L6_2atmpS1568 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1569 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1568;
      _M0L1nS257 = _M0L6_2atmpS1569;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1572 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1572;
      int32_t _M0L6_2atmpS1571 =
        ((moonbit_string_t)moonbit_string_literal_22.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1571;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1561;
  moonbit_string_t _result_2773;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1561
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1561);
  if (_M0L6_2atmpS1561.$1) {
    moonbit_decref(_M0L6_2atmpS1561.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2773 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2773;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1558;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1558 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1558);
  moonbit_decref_cycle_free(_M0L6_2atmpS1558);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1559;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1559 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1559);
  moonbit_decref_cycle_free(_M0L6_2atmpS1559);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1560;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1560 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1560);
  moonbit_decref_cycle_free(_M0L6_2atmpS1560);
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
  moonbit_string_t _M0L8_2afieldS2628;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2628 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2628);
  return _M0L8_2afieldS2628;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1557;
  int64_t _M0L6_2atmpS1556;
  struct _M0TPC16string10StringView _M0L6_2atmpS1555;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1557 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1556 = (int64_t)_M0L6_2atmpS1557;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1555
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1556);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1555);
  moonbit_decref_cycle_free(_M0L6_2atmpS1555.$0);
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
  int32_t _M0L6_2atmpS1539;
  int32_t _if__result_2774;
  int32_t _M0L6_2atmpS1547;
  int32_t _if__result_2775;
  int32_t _M0L6_2atmpS1549;
  int32_t _M0L6_2atmpS1550;
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
  _M0L6_2atmpS1539 = _M0Lm2loS236;
  if (_M0L6_2atmpS1539 > 0) {
    int32_t _M0L6_2atmpS1538 = _M0Lm2loS236;
    if (_M0L6_2atmpS1538 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1537 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1536 = _M0L4selfS235[_M0L6_2atmpS1537];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1536)) {
        int32_t _M0L6_2atmpS1535 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1534 = _M0L6_2atmpS1535 - 1;
        int32_t _M0L6_2atmpS1533 = _M0L4selfS235[_M0L6_2atmpS1534];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2774
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1533);
      } else {
        _if__result_2774 = 0;
      }
    } else {
      _if__result_2774 = 0;
    }
  } else {
    _if__result_2774 = 0;
  }
  if (_if__result_2774) {
    int32_t _M0L6_2atmpS1540 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1540 + 1;
  }
  _M0L6_2atmpS1547 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1547 > 0) {
    int32_t _M0L6_2atmpS1546 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1546 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1545 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1544 = _M0L4selfS235[_M0L6_2atmpS1545];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1544)) {
        int32_t _M0L6_2atmpS1543 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1542 = _M0L6_2atmpS1543 - 1;
        int32_t _M0L6_2atmpS1541 = _M0L4selfS235[_M0L6_2atmpS1542];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2775
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1541);
      } else {
        _if__result_2775 = 0;
      }
    } else {
      _if__result_2775 = 0;
    }
  } else {
    _if__result_2775 = 0;
  }
  if (_if__result_2775) {
    int32_t _M0L6_2atmpS1548 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1548 - 1;
  }
  _M0L6_2atmpS1549 = _M0Lm2loS236;
  _M0L6_2atmpS1550 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1549 >= _M0L6_2atmpS1550) {
    int32_t _M0L6_2atmpS1551 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1552 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1551,
                                                 .$2 = _M0L6_2atmpS1552};
  } else {
    int32_t _M0L6_2atmpS1553 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1554 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1553,
                                                 .$2 = _M0L6_2atmpS1554};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1532;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1532
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1532);
  if (_M0L6_2atmpS1532.$1) {
    moonbit_decref(_M0L6_2atmpS1532.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1531;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1531
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1531);
  if (_M0L6_2atmpS1531.$1) {
    moonbit_decref(_M0L6_2atmpS1531.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1530;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1530 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1530;
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
  int32_t _M0L6_2atmpS1529;
  struct _M0TPC16string10StringView _M0L6_2atmpS1527;
  struct _M0TPB6Logger _M0L6_2atmpS1528;
  moonbit_string_t _result_2776;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1529 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1527
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1529
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1528
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1527, _M0L6_2atmpS1528, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1527.$0);
  if (_M0L6_2atmpS1528.$1) {
    moonbit_decref(_M0L6_2atmpS1528.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2776 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2776;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1525;
  int32_t _M0L5startS1526;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1525 = _M0L4selfS218.$2;
  _M0L5startS1526 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1525 - _M0L5startS1526;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 151, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1522;
    int32_t _M0L5startS1524;
    int32_t _M0L6_2atmpS1523;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1506;
    int32_t _M0L6_2atmpS1507;
    int32_t _M0L6_2atmpS1508;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1522 = _M0L4selfS218.$0;
    _M0L5startS1524 = _M0L4selfS218.$1;
    _M0L6_2atmpS1523 = _M0L5startS1524 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1522[_M0L6_2atmpS1523];
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
        int32_t _M0L6_2atmpS1509;
        int32_t _M0L6_2atmpS1510;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1509 = _M0L1iS220 + 1;
        _M0L6_2atmpS1510 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1509;
        _M0L3segS221 = _M0L6_2atmpS1510;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1511;
        int32_t _M0L6_2atmpS1512;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1511 = _M0L1iS220 + 1;
        _M0L6_2atmpS1512 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1511;
        _M0L3segS221 = _M0L6_2atmpS1512;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1513;
        int32_t _M0L6_2atmpS1514;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1513 = _M0L1iS220 + 1;
        _M0L6_2atmpS1514 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1513;
        _M0L3segS221 = _M0L6_2atmpS1514;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1515;
        int32_t _M0L6_2atmpS1516;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_26.data);
        _M0L6_2atmpS1515 = _M0L1iS220 + 1;
        _M0L6_2atmpS1516 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1515;
        _M0L3segS221 = _M0L6_2atmpS1516;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1518;
          moonbit_string_t _M0L6_2atmpS1517;
          int32_t _M0L6_2atmpS1519;
          int32_t _M0L6_2atmpS1520;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_27.data);
          _M0L6_2atmpS1518 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1517 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1518);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1517);
          moonbit_decref_cycle_free(_M0L6_2atmpS1517);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1519 = _M0L1iS220 + 1;
          _M0L6_2atmpS1520 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1519;
          _M0L3segS221 = _M0L6_2atmpS1520;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1521 = _M0L1iS220 + 1;
          int32_t _tmp_2779 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1521;
          _M0L3segS221 = _tmp_2779;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2778;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1506 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1506);
    _M0L6_2atmpS1507 = _M0L1iS220 + 1;
    _M0L6_2atmpS1508 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1507;
    _M0L3segS221 = _M0L6_2atmpS1508;
    continue;
    joinlet_2778:;
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
    int64_t _M0L6_2atmpS1505 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1504;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1504
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1505);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1504);
    moonbit_decref_cycle_free(_M0L6_2atmpS1504.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1502;
  int32_t _M0L5startS1503;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1480;
  int32_t _if__result_2780;
  int32_t _M0L6_2atmpS1490;
  int32_t _if__result_2781;
  int32_t _M0L6_2atmpS1492;
  int32_t _M0L6_2atmpS1493;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1502 = _M0L4selfS201.$2;
  _M0L5startS1503 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1502 - _M0L5startS1503;
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
  _M0L6_2atmpS1480 = _M0Lm2loS202;
  if (_M0L6_2atmpS1480 > 0) {
    int32_t _M0L6_2atmpS1479 = _M0Lm2loS202;
    if (_M0L6_2atmpS1479 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1478 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1477 = _M0L4baseS209 + _M0L6_2atmpS1478;
      int32_t _M0L6_2atmpS1476 = _M0L3strS208[_M0L6_2atmpS1477];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1476)) {
        int32_t _M0L6_2atmpS1475 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1474 = _M0L4baseS209 + _M0L6_2atmpS1475;
        int32_t _M0L6_2atmpS1473 = _M0L6_2atmpS1474 - 1;
        int32_t _M0L6_2atmpS1472 = _M0L3strS208[_M0L6_2atmpS1473];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2780
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1472);
      } else {
        _if__result_2780 = 0;
      }
    } else {
      _if__result_2780 = 0;
    }
  } else {
    _if__result_2780 = 0;
  }
  if (_if__result_2780) {
    int32_t _M0L6_2atmpS1481 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1481 + 1;
  }
  _M0L6_2atmpS1490 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1490 > 0) {
    int32_t _M0L6_2atmpS1489 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1489 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1488 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1487 = _M0L4baseS209 + _M0L6_2atmpS1488;
      int32_t _M0L6_2atmpS1486 = _M0L3strS208[_M0L6_2atmpS1487];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1486)) {
        int32_t _M0L6_2atmpS1485 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1484 = _M0L4baseS209 + _M0L6_2atmpS1485;
        int32_t _M0L6_2atmpS1483 = _M0L6_2atmpS1484 - 1;
        int32_t _M0L6_2atmpS1482 = _M0L3strS208[_M0L6_2atmpS1483];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2781
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1482);
      } else {
        _if__result_2781 = 0;
      }
    } else {
      _if__result_2781 = 0;
    }
  } else {
    _if__result_2781 = 0;
  }
  if (_if__result_2781) {
    int32_t _M0L6_2atmpS1491 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1491 - 1;
  }
  _M0L6_2atmpS1492 = _M0Lm2loS202;
  _M0L6_2atmpS1493 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1492 >= _M0L6_2atmpS1493) {
    int32_t _M0L6_2atmpS1497 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1494 = _M0L4baseS209 + _M0L6_2atmpS1497;
    int32_t _M0L6_2atmpS1496 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1495 = _M0L4baseS209 + _M0L6_2atmpS1496;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1494,
                                                 .$2 = _M0L6_2atmpS1495};
  } else {
    int32_t _M0L6_2atmpS1501 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1498 = _M0L4baseS209 + _M0L6_2atmpS1501;
    int32_t _M0L6_2atmpS1500 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1499 = _M0L4baseS209 + _M0L6_2atmpS1500;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1498,
                                                 .$2 = _M0L6_2atmpS1499};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1469;
  int32_t _M0L6_2atmpS1468;
  int32_t _M0L6_2atmpS1471;
  int32_t _M0L6_2atmpS1470;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1467;
  moonbit_string_t _result_2782;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1469 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1468
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1469);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1468);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1471 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1470
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1471);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1470);
  _M0L6_2atmpS1467 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2782 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1467);
  moonbit_decref_cycle_free(_M0L6_2atmpS1467);
  return _result_2782;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1464;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1464 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1464);
  } else {
    int32_t _M0L6_2atmpS1466;
    int32_t _M0L6_2atmpS1465;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1466 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1465 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1466, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1465);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1462;
  int32_t _M0L6_2atmpS1463;
  int32_t _M0L6_2atmpS1461;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1462 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1463 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1461 = _M0L6_2atmpS1462 - _M0L6_2atmpS1463;
  return _M0L6_2atmpS1461 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1459;
  int32_t _M0L6_2atmpS1460;
  int32_t _M0L6_2atmpS1458;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1459 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1460 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1458 = _M0L6_2atmpS1459 % _M0L6_2atmpS1460;
  return _M0L6_2atmpS1458 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1456;
  int32_t _M0L6_2atmpS1457;
  int32_t _M0L6_2atmpS1455;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1456 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1457 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1455 = _M0L6_2atmpS1456 / _M0L6_2atmpS1457;
  return _M0L6_2atmpS1455 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1453;
  int32_t _M0L6_2atmpS1454;
  int32_t _M0L6_2atmpS1452;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1453 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1454 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1452 = _M0L6_2atmpS1453 + _M0L6_2atmpS1454;
  return _M0L6_2atmpS1452 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1451;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1451 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1451;
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
  int32_t _M0L3lenS1450;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1445;
  int32_t _M0L6_2atmpS1444;
  int32_t _if__result_2783;
  uint16_t* _M0L4dataS1446;
  int32_t _M0L3lenS1447;
  int32_t _M0L3lenS1449;
  int32_t _M0L6_2atmpS1448;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1450 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1450 + _M0L8str__lenS182;
  _M0L4dataS1445 = _M0L4selfS185->$0;
  _M0L6_2atmpS1444 = Moonbit_array_length(_M0L4dataS1445);
  if (_M0L8requiredS184 > _M0L6_2atmpS1444) {
    _if__result_2783 = 1;
  } else {
    int32_t _M0L3lenS1443 = _M0L4selfS185->$1;
    _if__result_2783 = _M0L8requiredS184 < _M0L3lenS1443;
  }
  if (_if__result_2783) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1446 = _M0L4selfS185->$0;
  _M0L3lenS1447 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1446);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1446, _M0L3lenS1447, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1446);
  _M0L3lenS1449 = _M0L4selfS185->$1;
  _M0L6_2atmpS1448 = _M0L3lenS1449 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1448;
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
      int32_t _M0L6_2atmpS1440 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1441;
      int32_t _M0L6_2atmpS1442;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1440;
      _M0L6_2atmpS1441 = _M0L1iS176 + 1;
      _M0L6_2atmpS1442 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1441;
      _M0L1jS177 = _M0L6_2atmpS1442;
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
    int32_t _M0L3lenS1411 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1413 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1412 = Moonbit_array_length(_M0L4dataS1413);
    uint16_t* _M0L4dataS1416;
    int32_t _M0L3lenS1417;
    int32_t _M0L6_2atmpS1418;
    int32_t _M0L3lenS1420;
    int32_t _M0L6_2atmpS1419;
    if (_M0L3lenS1411 >= _M0L6_2atmpS1412) {
      int32_t _M0L3lenS1415 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1414 = _M0L3lenS1415 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1414);
    }
    _M0L4dataS1416 = _M0L4selfS171->$0;
    _M0L3lenS1417 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1416);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1418 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1417 < 0
      || _M0L3lenS1417 >= Moonbit_array_length(_M0L4dataS1416)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1416[_M0L3lenS1417] = _M0L6_2atmpS1418;
    moonbit_decref_cycle_free(_M0L4dataS1416);
    _M0L3lenS1420 = _M0L4selfS171->$1;
    _M0L6_2atmpS1419 = _M0L3lenS1420 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1419;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1424 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1422 = Moonbit_array_length(_M0L4dataS1424);
    int32_t _M0L3lenS1423 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1421 = _M0L6_2atmpS1422 - _M0L3lenS1423;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1427;
    int32_t _M0L3lenS1428;
    uint32_t _M0L6_2atmpS1431;
    uint32_t _M0L6_2atmpS1430;
    int32_t _M0L6_2atmpS1429;
    uint16_t* _M0L4dataS1432;
    int32_t _M0L3lenS1437;
    int32_t _M0L6_2atmpS1433;
    uint32_t _M0L6_2atmpS1436;
    uint32_t _M0L6_2atmpS1435;
    int32_t _M0L6_2atmpS1434;
    int32_t _M0L3lenS1439;
    int32_t _M0L6_2atmpS1438;
    if (_M0L6_2atmpS1421 < 2) {
      int32_t _M0L3lenS1426 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1425 = _M0L3lenS1426 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1425);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1427 = _M0L4selfS171->$0;
    _M0L3lenS1428 = _M0L4selfS171->$1;
    _M0L6_2atmpS1431 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1430 = 55296u + _M0L6_2atmpS1431;
    moonbit_incref_cycle_free(_M0L4dataS1427);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1429 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1430);
    if (
      _M0L3lenS1428 < 0
      || _M0L3lenS1428 >= Moonbit_array_length(_M0L4dataS1427)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1427[_M0L3lenS1428] = _M0L6_2atmpS1429;
    moonbit_decref_cycle_free(_M0L4dataS1427);
    _M0L4dataS1432 = _M0L4selfS171->$0;
    _M0L3lenS1437 = _M0L4selfS171->$1;
    _M0L6_2atmpS1433 = _M0L3lenS1437 + 1;
    _M0L6_2atmpS1436 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1435 = 56320u + _M0L6_2atmpS1436;
    moonbit_incref_cycle_free(_M0L4dataS1432);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1434 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1435);
    if (
      _M0L6_2atmpS1433 < 0
      || _M0L6_2atmpS1433 >= Moonbit_array_length(_M0L4dataS1432)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1432[_M0L6_2atmpS1433] = _M0L6_2atmpS1434;
    moonbit_decref_cycle_free(_M0L4dataS1432);
    _M0L3lenS1439 = _M0L4selfS171->$1;
    _M0L6_2atmpS1438 = _M0L3lenS1439 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1438;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_28.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1410;
  int32_t _M0L6_2atmpS1408;
  int32_t _M0L3lenS1409;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1405;
  int32_t _M0L6_2atmpS1406;
  int32_t _M0L3lenS1407;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2629;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1410 = _M0L4selfS166->$0;
  _M0L6_2atmpS1408 = Moonbit_array_length(_M0L4dataS1410);
  _M0L3lenS1409 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1408, _M0L3lenS1409, _M0L8requiredS167);
  _M0L4dataS1405 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1405);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1406 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1407 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1405, _M0L13new__capacityS165, _M0L6_2atmpS1406, _M0L3lenS1407, 0, 0);
  _M0L6_2aoldS2629 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2629);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_29.data);
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
  int32_t _M0L6_2atmpS1404;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1404 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1404;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1403;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1403 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1403;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1394;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1394 = _M0L4selfS155->$1;
  if (_M0L3lenS1394 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1395 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1397 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1396 = Moonbit_array_length(_M0L4dataS1397);
    if (_M0L3lenS1395 == _M0L6_2atmpS1396) {
      uint16_t* _M0L4dataS1398 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1398);
      return _M0L4dataS1398;
    } else {
      uint16_t* _M0L4dataS1399 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1400 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1401;
      int32_t _M0L3lenS1402;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1399);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1401 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1402 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1399, _M0L3lenS1400, _M0L6_2atmpS1401, _M0L3lenS1402, 0, 0);
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
  int32_t _if__result_2786;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1390 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1391 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1390 <= _M0L6_2atmpS1391) {
            int32_t _M0L6_2atmpS1389 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2786 = _M0L6_2atmpS1389 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2786 = 0;
          }
        } else {
          _if__result_2786 = 0;
        }
      } else {
        _if__result_2786 = 0;
      }
    } else {
      _if__result_2786 = 0;
    }
  } else {
    _if__result_2786 = 0;
  }
  if (_if__result_2786) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1393;
    moonbit_string_t _M0L6_2atmpS1392;
    uint16_t* _result_2787;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_34.data);
    _M0L6_2atmpS1393 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1393);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1392
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2787 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1392);
    moonbit_decref_cycle_free(_M0L6_2atmpS1392);
    return _result_2787;
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
  struct _M0TPB13StringBuilder* _block_2788;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1388 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1388 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2788
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2788)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 156, 0);
  _block_2788->$0 = _M0L4dataS140;
  _block_2788->$1 = 0;
  return _block_2788;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1387;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1387 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1387;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2789;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1368 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1369;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1369
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS117);
          if (_M0L6_2atmpS1368 <= _M0L6_2atmpS1369) {
            int32_t _M0L6_2atmpS1367 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2789 = _M0L6_2atmpS1367 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2789 = 0;
          }
        } else {
          _if__result_2789 = 0;
        }
      } else {
        _if__result_2789 = 0;
      }
    } else {
      _if__result_2789 = 0;
    }
  } else {
    _if__result_2789 = 0;
  }
  if (_if__result_2789) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1371;
    moonbit_string_t _M0L6_2atmpS1370;
    float* _result_2790;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1371 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1371);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1370
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2790
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1370);
    moonbit_decref_cycle_free(_M0L6_2atmpS1370);
    return _result_2790;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2791;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1373 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1374;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1374
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS123);
          if (_M0L6_2atmpS1373 <= _M0L6_2atmpS1374) {
            int32_t _M0L6_2atmpS1372 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2791 = _M0L6_2atmpS1372 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2791 = 0;
          }
        } else {
          _if__result_2791 = 0;
        }
      } else {
        _if__result_2791 = 0;
      }
    } else {
      _if__result_2791 = 0;
    }
  } else {
    _if__result_2791 = 0;
  }
  if (_if__result_2791) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS123, _M0L13allocate__lenS119, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1376;
    moonbit_string_t _M0L6_2atmpS1375;
    int32_t* _result_2792;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1376 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1376);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1375
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2792
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1375);
    moonbit_decref_cycle_free(_M0L6_2atmpS1375);
    return _result_2792;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2793;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1378 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1379;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1379
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
          if (_M0L6_2atmpS1378 <= _M0L6_2atmpS1379) {
            int32_t _M0L6_2atmpS1377 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2793 = _M0L6_2atmpS1377 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2793 = 0;
          }
        } else {
          _if__result_2793 = 0;
        }
      } else {
        _if__result_2793 = 0;
      }
    } else {
      _if__result_2793 = 0;
    }
  } else {
    _if__result_2793 = 0;
  }
  if (_if__result_2793) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1381;
    moonbit_string_t _M0L6_2atmpS1380;
    moonbit_string_t* _result_2794;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1381 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1381);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1380
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2794
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1380);
    moonbit_decref_cycle_free(_M0L6_2atmpS1380);
    return _result_2794;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2795;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1383 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1384;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1384
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
          if (_M0L6_2atmpS1383 <= _M0L6_2atmpS1384) {
            int32_t _M0L6_2atmpS1382 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2795 = _M0L6_2atmpS1382 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2795 = 0;
          }
        } else {
          _if__result_2795 = 0;
        }
      } else {
        _if__result_2795 = 0;
      }
    } else {
      _if__result_2795 = 0;
    }
  } else {
    _if__result_2795 = 0;
  }
  if (_if__result_2795) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS131, 0, _M0L3srcS135, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1386;
    moonbit_string_t _M0L6_2atmpS1385;
    struct _M0TUsiE** _result_2796;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1386 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1386);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1385
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2796
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1385);
    moonbit_decref_cycle_free(_M0L6_2atmpS1385);
    return _result_2796;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1364;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1364
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1364);
  if (_M0L6_2atmpS1364.$1) {
    moonbit_decref(_M0L6_2atmpS1364.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1365;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1365
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1365);
  if (_M0L6_2atmpS1365.$1) {
    moonbit_decref(_M0L6_2atmpS1365.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1366;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1366
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1366);
  if (_M0L6_2atmpS1366.$1) {
    moonbit_decref(_M0L6_2atmpS1366.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS86,
  int32_t _M0L13allocate__lenS84,
  int32_t _M0L11src__offsetS87,
  int32_t _M0L11dst__offsetS85,
  int32_t _M0L9blit__lenS88
) {
  float* _M0L3dstS83;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS83 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS84);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS83, _M0L11dst__offsetS85, _M0L3srcS86, _M0L11src__offsetS87, _M0L9blit__lenS88);
  moonbit_decref_cycle_free(_M0L3srcS86);
  return _M0L3dstS83;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS92,
  int32_t _M0L13allocate__lenS90,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS91,
  int32_t _M0L9blit__lenS94
) {
  int32_t* _M0L3dstS89;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS89
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS90);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS89, _M0L11dst__offsetS91, _M0L3srcS92, _M0L11src__offsetS93, _M0L9blit__lenS94);
  moonbit_decref_cycle_free(_M0L3srcS92);
  return _M0L3dstS89;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  moonbit_string_t* _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS96, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
  moonbit_decref_cycle_free(_M0L3srcS98);
  return _M0L3dstS95;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS104,
  int32_t _M0L13allocate__lenS102,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS103,
  int32_t _M0L9blit__lenS106
) {
  struct _M0TUsiE** _M0L3dstS101;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS101
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS102, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS101, _M0L11dst__offsetS103, _M0L3srcS104, _M0L11src__offsetS105, _M0L9blit__lenS106);
  moonbit_decref_cycle_free(_M0L3srcS104);
  return _M0L3dstS101;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS63,
  int32_t _M0L11dst__offsetS64,
  float* _M0L3srcS65,
  int32_t _M0L11src__offsetS66,
  int32_t _M0L3lenS67
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS65);
  moonbit_incref_cycle_free(_M0L3dstS63);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS63, _M0L11dst__offsetS64, _M0L3srcS65, _M0L11src__offsetS66, _M0L3lenS67, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  int32_t* _M0L3srcS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L3lenS72
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS70);
  moonbit_incref_cycle_free(_M0L3dstS68);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS68, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L3lenS72, sizeof(int32_t));
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
        int32_t _M0L6_2atmpS1319 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1321 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1320;
        int32_t _M0L6_2atmpS1322;
        if (
          _M0L6_2atmpS1321 < 0
          || _M0L6_2atmpS1321 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1320 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1321];
        if (
          _M0L6_2atmpS1319 < 0
          || _M0L6_2atmpS1319 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1319] = _M0L6_2atmpS1320;
        _M0L6_2atmpS1322 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1322;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1327 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1327;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1323 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1325 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1324;
        int32_t _M0L6_2atmpS1326;
        if (
          _M0L6_2atmpS1325 < 0
          || _M0L6_2atmpS1325 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1324 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1325];
        if (
          _M0L6_2atmpS1323 < 0
          || _M0L6_2atmpS1323 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1323] = _M0L6_2atmpS1324;
        _M0L6_2atmpS1326 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1326;
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
        int32_t _M0L6_2atmpS1328 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1330 = _M0L11src__offsetS30 + _M0L1iS31;
        float _M0L6_2atmpS1329;
        int32_t _M0L6_2atmpS1331;
        if (
          _M0L6_2atmpS1330 < 0
          || _M0L6_2atmpS1330 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1329 = (float)_M0L3srcS28[_M0L6_2atmpS1330];
        if (
          _M0L6_2atmpS1328 < 0
          || _M0L6_2atmpS1328 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1328] = _M0L6_2atmpS1329;
        _M0L6_2atmpS1331 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1331;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1336 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1336;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1332 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1334 = _M0L11src__offsetS30 + _M0L1iS34;
        float _M0L6_2atmpS1333;
        int32_t _M0L6_2atmpS1335;
        if (
          _M0L6_2atmpS1334 < 0
          || _M0L6_2atmpS1334 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1333 = (float)_M0L3srcS28[_M0L6_2atmpS1334];
        if (
          _M0L6_2atmpS1332 < 0
          || _M0L6_2atmpS1332 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1332] = _M0L6_2atmpS1333;
        _M0L6_2atmpS1335 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1335;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  int32_t* _M0L3srcS37,
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
        int32_t _M0L6_2atmpS1337 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1339 = _M0L11src__offsetS39 + _M0L1iS40;
        int32_t _M0L6_2atmpS1338;
        int32_t _M0L6_2atmpS1340;
        if (
          _M0L6_2atmpS1339 < 0
          || _M0L6_2atmpS1339 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1338 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1339];
        if (
          _M0L6_2atmpS1337 < 0
          || _M0L6_2atmpS1337 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1337] = _M0L6_2atmpS1338;
        _M0L6_2atmpS1340 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1340;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1345 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1345;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1341 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1343 = _M0L11src__offsetS39 + _M0L1iS43;
        int32_t _M0L6_2atmpS1342;
        int32_t _M0L6_2atmpS1344;
        if (
          _M0L6_2atmpS1343 < 0
          || _M0L6_2atmpS1343 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1342 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1343];
        if (
          _M0L6_2atmpS1341 < 0
          || _M0L6_2atmpS1341 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1341] = _M0L6_2atmpS1342;
        _M0L6_2atmpS1344 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1344;
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
        int32_t _M0L6_2atmpS1346 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1348 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS1347;
        moonbit_string_t _M0L6_2aoldS2630;
        int32_t _M0L6_2atmpS1349;
        if (
          _M0L6_2atmpS1348 < 0
          || _M0L6_2atmpS1348 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1347 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1348];
        if (
          _M0L6_2atmpS1346 < 0
          || _M0L6_2atmpS1346 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2630 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1346];
        moonbit_incref_cycle_free(_M0L6_2atmpS1347);
        moonbit_decref_cycle_free(_M0L6_2aoldS2630);
        _M0L3dstS45[_M0L6_2atmpS1346] = _M0L6_2atmpS1347;
        _M0L6_2atmpS1349 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1349;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1354 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1354;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1350 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1352 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1351;
        moonbit_string_t _M0L6_2aoldS2631;
        int32_t _M0L6_2atmpS1353;
        if (
          _M0L6_2atmpS1352 < 0
          || _M0L6_2atmpS1352 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1351 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1352];
        if (
          _M0L6_2atmpS1350 < 0
          || _M0L6_2atmpS1350 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2631 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1350];
        moonbit_incref_cycle_free(_M0L6_2atmpS1351);
        moonbit_decref_cycle_free(_M0L6_2aoldS2631);
        _M0L3dstS45[_M0L6_2atmpS1350] = _M0L6_2atmpS1351;
        _M0L6_2atmpS1353 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1353;
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
        int32_t _M0L6_2atmpS1355 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1357 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS1356;
        struct _M0TUsiE* _M0L6_2aoldS2632;
        int32_t _M0L6_2atmpS1358;
        if (
          _M0L6_2atmpS1357 < 0
          || _M0L6_2atmpS1357 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1356 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1357];
        if (
          _M0L6_2atmpS1355 < 0
          || _M0L6_2atmpS1355 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2632 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1355];
        if (_M0L6_2atmpS1356) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1356);
        }
        if (_M0L6_2aoldS2632) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2632);
        }
        _M0L3dstS54[_M0L6_2atmpS1355] = _M0L6_2atmpS1356;
        _M0L6_2atmpS1358 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1358;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1363 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1363;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1359 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1361 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1360;
        struct _M0TUsiE* _M0L6_2aoldS2633;
        int32_t _M0L6_2atmpS1362;
        if (
          _M0L6_2atmpS1361 < 0
          || _M0L6_2atmpS1361 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1360 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1361];
        if (
          _M0L6_2atmpS1359 < 0
          || _M0L6_2atmpS1359 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2633 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1359];
        if (_M0L6_2atmpS1360) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1360);
        }
        if (_M0L6_2aoldS2633) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2633);
        }
        _M0L3dstS54[_M0L6_2atmpS1359] = _M0L6_2atmpS1360;
        _M0L6_2atmpS1362 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1362;
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS14) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS14);
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS15) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS16) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS16);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS17
) {
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_35.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_36.data);
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS3
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1289) {
  switch (Moonbit_object_tag(_M0L4_2aeS1289)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1289);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_40.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1314,
  struct _M0TPB4Show _M0L8_2aparamS1313
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1312 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1314;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1312, _M0L8_2aparamS1313);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1311,
  struct _M0TPB4Show _M0L8_2aparamS1310
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1309 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1311;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1309, _M0L8_2aparamS1310);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1308,
  int32_t _M0L8_2aparamS1307
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1306 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1308;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1306, _M0L8_2aparamS1307);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1305,
  struct _M0TPC16string10StringView _M0L8_2aparamS1304
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1303 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1305;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1303, _M0L8_2aparamS1304);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1302,
  moonbit_string_t _M0L8_2aparamS1299,
  int32_t _M0L8_2aparamS1300,
  int32_t _M0L8_2aparamS1301
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1298 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1302;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1298, _M0L8_2aparamS1299, _M0L8_2aparamS1300, _M0L8_2aparamS1301);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1297,
  moonbit_string_t _M0L8_2aparamS1296
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1295 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1297;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1295, _M0L8_2aparamS1296);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2807 = 9218868437227405311ll;
  int64_t _tmp_2808;
  int64_t _tmp_2809;
  int64_t _tmp_2810;
  int64_t _tmp_2811;
  _M0FPB18double__max__value = *(double*)&_tmp_2807;
  _tmp_2808 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2808;
  _tmp_2809 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2809;
  _tmp_2810 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2810;
  _tmp_2811 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2811;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1318;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1282;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1283;
  int32_t _M0L7_2abindS1284;
  struct _M0TUsiE** _M0L7_2abindS1285;
  int32_t _M0L6_2acntS2638;
  int32_t _M0L2__S1286;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1318
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1282
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1282)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 159, 0);
  _M0L12async__testsS1282->$0 = _M0L6_2atmpS1318;
  _M0L12async__testsS1282->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1283
  = _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1284 = _M0L7_2abindS1283->$1;
  _M0L7_2abindS1285 = _M0L7_2abindS1283->$0;
  _M0L6_2acntS2638
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1283));
  if (_M0L6_2acntS2638 > 1) {
    int32_t _M0L11_2anew__cntS2639 = _M0L6_2acntS2638 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1283), _M0L11_2anew__cntS2639);
    moonbit_incref_cycle_free(_M0L7_2abindS1285);
  } else if (_M0L6_2acntS2638 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1283);
  }
  _M0L2__S1286 = 0;
  while (1) {
    if (_M0L2__S1286 < _M0L7_2abindS1284) {
      struct _M0TUsiE* _M0L3argS1287 =
        (struct _M0TUsiE*)_M0L7_2abindS1285[_M0L2__S1286];
      moonbit_string_t _M0L6_2atmpS1315 = _M0L3argS1287->$0;
      int32_t _M0L6_2atmpS1316 = _M0L3argS1287->$1;
      int32_t _M0L6_2atmpS1317;
      moonbit_incref_cycle_free(_M0L6_2atmpS1315);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1282, _M0L6_2atmpS1315, _M0L6_2atmpS1316);
      moonbit_decref_cycle_free(_M0L6_2atmpS1315);
      _M0L6_2atmpS1317 = _M0L2__S1286 + 1;
      _M0L2__S1286 = _M0L6_2atmpS1317;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1285);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\compartment_synapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples36compartment__synapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1282);
  moonbit_decref_cycle_free(_M0L12async__testsS1282);
  moonbit_flush_cycles();
  return 0;
}