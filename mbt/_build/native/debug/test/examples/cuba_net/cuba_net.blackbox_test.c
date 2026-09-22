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

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TUdiE;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0BTPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939;

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

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB4Show;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TUddE;

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

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934 {
  int32_t(* code)(struct _M0TWEu*);
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

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS946(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS939(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS934(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS911(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S904(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25cuba__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
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

struct { int32_t rc; uint32_t meta; uint16_t const data[115]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 114, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 117, 98, 97, 95, 110, 101, 116, 
    95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 
    112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

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
} const moonbit_string_literal_33 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 99, 117, 98, 97, 95, 110, 101, 116, 
    95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 
    114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS946$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS946
  };

uint32_t const moonbit_layout_table_data[93] =
  {
    sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934)
    / 4, 1,
    offsetof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934, $1)
    / 4
    * 2,
    sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939)
    / 4, 1,
    offsetof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1917
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS967,
  moonbit_string_t _M0L8filenameS936,
  int32_t _M0L5indexS938
) {
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934* _closure_1952;
  struct _M0TWEu* _M0L13handle__startS934;
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939* _closure_1953;
  struct _M0TWssbEu* _M0L14handle__resultS939;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS946;
  void* _M0L11_2atry__errS961;
  struct moonbit_result_0 _tmp_1955;
  int32_t _handle__error__result_1956;
  int32_t _M0L6_2atmpS1905;
  void* _M0L3errS962;
  moonbit_string_t _M0L4nameS964;
  struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS965;
  moonbit_string_t _M0L7_2anameS966;
  int32_t _M0L6_2acntS1946;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS936);
  _closure_1952
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934*)moonbit_malloc(sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934));
  Moonbit_object_header(_closure_1952)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1952->code
  = &_M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS934;
  _closure_1952->$0 = _M0L5indexS938;
  _closure_1952->$1 = _M0L8filenameS936;
  _M0L13handle__startS934 = (struct _M0TWEu*)_closure_1952;
  moonbit_incref_cycle_free(_M0L8filenameS936);
  _closure_1953
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939*)moonbit_malloc(sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939));
  Moonbit_object_header(_closure_1953)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1953->code
  = &_M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS939;
  _closure_1953->$0 = _M0L5indexS938;
  _closure_1953->$1 = _M0L8filenameS936;
  _M0L14handle__resultS939 = (struct _M0TWssbEu*)_closure_1953;
  _M0L17error__to__stringS946
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS946$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1955
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS967, _M0L8filenameS936, _M0L5indexS938, _M0L13handle__startS934, _M0L14handle__resultS939, _M0L17error__to__stringS946);
  if (_tmp_1955.tag) {
    int32_t const _M0L5_2aokS1914 = _tmp_1955.data.ok;
    _handle__error__result_1956 = _M0L5_2aokS1914;
  } else {
    void* const _M0L6_2aerrS1915 = _tmp_1955.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS946);
    moonbit_decref_cycle_free(_M0L13handle__startS934);
    _M0L11_2atry__errS961 = _M0L6_2aerrS1915;
    goto join_960;
  }
  if (_handle__error__result_1956) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS946);
    moonbit_decref_cycle_free(_M0L13handle__startS934);
    _M0L6_2atmpS1905 = 1;
  } else {
    struct moonbit_result_0 _tmp_1957;
    int32_t _handle__error__result_1958;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1957
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS967, _M0L8filenameS936, _M0L5indexS938, _M0L13handle__startS934, _M0L14handle__resultS939, _M0L17error__to__stringS946);
    if (_tmp_1957.tag) {
      int32_t const _M0L5_2aokS1912 = _tmp_1957.data.ok;
      _handle__error__result_1958 = _M0L5_2aokS1912;
    } else {
      void* const _M0L6_2aerrS1913 = _tmp_1957.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS946);
      moonbit_decref_cycle_free(_M0L13handle__startS934);
      _M0L11_2atry__errS961 = _M0L6_2aerrS1913;
      goto join_960;
    }
    if (_handle__error__result_1958) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS946);
      moonbit_decref_cycle_free(_M0L13handle__startS934);
      _M0L6_2atmpS1905 = 1;
    } else {
      struct moonbit_result_0 _tmp_1959;
      int32_t _handle__error__result_1960;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1959
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS967, _M0L8filenameS936, _M0L5indexS938, _M0L13handle__startS934, _M0L14handle__resultS939, _M0L17error__to__stringS946);
      if (_tmp_1959.tag) {
        int32_t const _M0L5_2aokS1910 = _tmp_1959.data.ok;
        _handle__error__result_1960 = _M0L5_2aokS1910;
      } else {
        void* const _M0L6_2aerrS1911 = _tmp_1959.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS946);
        moonbit_decref_cycle_free(_M0L13handle__startS934);
        _M0L11_2atry__errS961 = _M0L6_2aerrS1911;
        goto join_960;
      }
      if (_handle__error__result_1960) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS946);
        moonbit_decref_cycle_free(_M0L13handle__startS934);
        _M0L6_2atmpS1905 = 1;
      } else {
        struct moonbit_result_0 _tmp_1961;
        int32_t _handle__error__result_1962;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1961
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS967, _M0L8filenameS936, _M0L5indexS938, _M0L13handle__startS934, _M0L14handle__resultS939, _M0L17error__to__stringS946);
        if (_tmp_1961.tag) {
          int32_t const _M0L5_2aokS1908 = _tmp_1961.data.ok;
          _handle__error__result_1962 = _M0L5_2aokS1908;
        } else {
          void* const _M0L6_2aerrS1909 = _tmp_1961.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS946);
          moonbit_decref_cycle_free(_M0L13handle__startS934);
          _M0L11_2atry__errS961 = _M0L6_2aerrS1909;
          goto join_960;
        }
        if (_handle__error__result_1962) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS946);
          moonbit_decref_cycle_free(_M0L13handle__startS934);
          _M0L6_2atmpS1905 = 1;
        } else {
          struct moonbit_result_0 _tmp_1963;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1963
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS967, _M0L8filenameS936, _M0L5indexS938, _M0L13handle__startS934, _M0L14handle__resultS939, _M0L17error__to__stringS946);
          moonbit_decref_cycle_free(_M0L13handle__startS934);
          moonbit_decref_cycle_free(_M0L17error__to__stringS946);
          if (_tmp_1963.tag) {
            int32_t const _M0L5_2aokS1906 = _tmp_1963.data.ok;
            _M0L6_2atmpS1905 = _M0L5_2aokS1906;
          } else {
            void* const _M0L6_2aerrS1907 = _tmp_1963.data.err;
            _M0L11_2atry__errS961 = _M0L6_2aerrS1907;
            goto join_960;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1905) {
    void* _M0L128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1916 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1916)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1916)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS961
    = _M0L128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1916;
    goto join_960;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS939);
  }
  goto joinlet_1954;
  join_960:;
  _M0L3errS962 = _M0L11_2atry__errS961;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS965
  = (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS962;
  _M0L7_2anameS966 = _M0L36_2aMoonBitTestDriverInternalSkipTestS965->$0;
  _M0L6_2acntS1946
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS965));
  if (_M0L6_2acntS1946 > 1) {
    int32_t _M0L11_2anew__cntS1947 = _M0L6_2acntS1946 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS965), _M0L11_2anew__cntS1947);
    moonbit_incref_cycle_free(_M0L7_2anameS966);
  } else if (_M0L6_2acntS1946 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS965);
  }
  _M0L4nameS964 = _M0L7_2anameS966;
  goto join_963;
  goto joinlet_1964;
  join_963:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS939(_M0L14handle__resultS939, _M0L4nameS964, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS939);
  moonbit_decref_cycle_free(_M0L4nameS964);
  joinlet_1964:;
  joinlet_1954:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS946(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1904,
  void* _M0L3errS947
) {
  void* _M0L1eS949;
  moonbit_string_t _M0L1eS951;
  moonbit_string_t _result_1967;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS947)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS952 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS947;
      moonbit_string_t _M0L4_2aeS953 = _M0L10_2aFailureS952->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS953);
      _M0L1eS951 = _M0L4_2aeS953;
      goto join_950;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS954 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS947;
      moonbit_string_t _M0L4_2aeS955 = _M0L15_2aInspectErrorS954->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS955);
      _M0L1eS951 = _M0L4_2aeS955;
      goto join_950;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS956 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS947;
      moonbit_string_t _M0L4_2aeS957 = _M0L16_2aSnapshotErrorS956->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS957);
      _M0L1eS951 = _M0L4_2aeS957;
      goto join_950;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS958 =
        (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS947;
      moonbit_string_t _M0L4_2aeS959 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS958->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS959);
      _M0L1eS951 = _M0L4_2aeS959;
      goto join_950;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS947);
      _M0L1eS949 = _M0L3errS947;
      goto join_948;
      break;
    }
  }
  join_950:;
  return _M0L1eS951;
  join_948:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_1967 = _M0FP15Error10to__string(_M0L1eS949);
  moonbit_decref_cycle_free(_M0L1eS949);
  return _result_1967;
}

int32_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS939(
  struct _M0TWssbEu* _M0L6_2aenvS1901,
  moonbit_string_t _M0L10__testnameS940,
  moonbit_string_t _M0L7messageS941,
  int32_t _M0L7skippedS942
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939* _M0L14_2acasted__envS1902;
  moonbit_string_t _M0L8filenameS936;
  int32_t _M0L5indexS938;
  moonbit_string_t _M0L10file__nameS943;
  moonbit_string_t _M0L7messageS944;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS945;
  moonbit_string_t _M0L6_2atmpS1903;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1902
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c939*)_M0L6_2aenvS1901;
  _M0L8filenameS936 = _M0L14_2acasted__envS1902->$1;
  _M0L5indexS938 = _M0L14_2acasted__envS1902->$0;
  if (!_M0L7skippedS942 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS943
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS936, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS944
  = _M0MPC16string6String14escape_2einner(_M0L7messageS941, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS945
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS945, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS945, _M0L10file__nameS943);
  moonbit_decref_cycle_free(_M0L10file__nameS943);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS945, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS945, _M0L5indexS938);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS945, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS945, _M0L7messageS944);
  moonbit_decref_cycle_free(_M0L7messageS944);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS945, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1903
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS945);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS945);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1903);
  moonbit_decref_cycle_free(_M0L6_2atmpS1903);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS934(
  struct _M0TWEu* _M0L6_2aenvS1898
) {
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934* _M0L14_2acasted__envS1899;
  moonbit_string_t _M0L8filenameS936;
  int32_t _M0L5indexS938;
  moonbit_string_t _M0L10file__nameS935;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS937;
  moonbit_string_t _M0L6_2atmpS1900;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1899
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fcuba__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c934*)_M0L6_2aenvS1898;
  _M0L8filenameS936 = _M0L14_2acasted__envS1899->$1;
  _M0L5indexS938 = _M0L14_2acasted__envS1899->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS935
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS936, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS937
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS937, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS937, _M0L10file__nameS935);
  moonbit_decref_cycle_free(_M0L10file__nameS935);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS937, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS937, _M0L5indexS938);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS937, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1900
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS937);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS937);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1900);
  moonbit_decref_cycle_free(_M0L6_2atmpS1900);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S904;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS911;
  struct _M0TUsiE** _M0L6_2atmpS1897;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS918;
  moonbit_string_t* _M0L9cli__argsS919;
  moonbit_string_t _M0L6_2atmpS1896;
  moonbit_string_t _M0L6_2atmpS1895;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS920;
  int32_t _M0L7_2abindS921;
  moonbit_string_t* _M0L7_2abindS922;
  int32_t _M0L6_2acntS1948;
  int32_t _M0L2__S923;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S904 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS911 = 0;
  _M0L6_2atmpS1897 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS918
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS918)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS918->$0 = _M0L6_2atmpS1897;
  _M0L16file__and__indexS918->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS919
  = _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS919)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1896 = (moonbit_string_t)_M0L9cli__argsS919[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1896);
  moonbit_decref_cycle_free(_M0L9cli__argsS919);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1895
  = _M0MP46RiantR8snn__mbt8examples25cuba__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1896);
  moonbit_decref_cycle_free(_M0L6_2atmpS1896);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS920
  = _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS911(_M0L51moonbit__test__driver__internal__split__mbt__stringS911, _M0L6_2atmpS1895, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1895);
  _M0L7_2abindS921 = _M0L10test__argsS920->$1;
  _M0L7_2abindS922 = _M0L10test__argsS920->$0;
  _M0L6_2acntS1948
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS920));
  if (_M0L6_2acntS1948 > 1) {
    int32_t _M0L11_2anew__cntS1949 = _M0L6_2acntS1948 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS920), _M0L11_2anew__cntS1949);
    moonbit_incref_cycle_free(_M0L7_2abindS922);
  } else if (_M0L6_2acntS1948 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS920);
  }
  _M0L2__S923 = 0;
  while (1) {
    if (_M0L2__S923 < _M0L7_2abindS921) {
      moonbit_string_t _M0L3argS924 =
        (moonbit_string_t)_M0L7_2abindS922[_M0L2__S923];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS925;
      moonbit_string_t _M0L4fileS926;
      moonbit_string_t _M0L5rangeS927;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS928;
      moonbit_string_t _M0L6_2atmpS1893;
      int32_t _M0L5startS929;
      moonbit_string_t _M0L6_2atmpS1892;
      int32_t _M0L3endS930;
      int32_t _M0L1iS931;
      int32_t _M0L6_2atmpS1894;
      moonbit_incref_cycle_free(_M0L3argS924);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS925
      = _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS911(_M0L51moonbit__test__driver__internal__split__mbt__stringS911, _M0L3argS924, 58);
      moonbit_decref_cycle_free(_M0L3argS924);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS926
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS925, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS927
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS925, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS925);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS928
      = _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS911(_M0L51moonbit__test__driver__internal__split__mbt__stringS911, _M0L5rangeS927, 45);
      moonbit_decref_cycle_free(_M0L5rangeS927);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1893
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS928, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS929
      = _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S904(_M0L45moonbit__test__driver__internal__parse__int__S904, _M0L6_2atmpS1893);
      moonbit_decref_cycle_free(_M0L6_2atmpS1893);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1892
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS928, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS928);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS930
      = _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S904(_M0L45moonbit__test__driver__internal__parse__int__S904, _M0L6_2atmpS1892);
      moonbit_decref_cycle_free(_M0L6_2atmpS1892);
      _M0L1iS931 = _M0L5startS929;
      while (1) {
        if (_M0L1iS931 < _M0L3endS930) {
          struct _M0TUsiE* _M0L8_2atupleS1890;
          int32_t _M0L6_2atmpS1891;
          moonbit_incref_cycle_free(_M0L4fileS926);
          _M0L8_2atupleS1890
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1890)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1890->$0 = _M0L4fileS926;
          _M0L8_2atupleS1890->$1 = _M0L1iS931;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS918, _M0L8_2atupleS1890);
          _M0L6_2atmpS1891 = _M0L1iS931 + 1;
          _M0L1iS931 = _M0L6_2atmpS1891;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS926);
        }
        break;
      }
      _M0L6_2atmpS1894 = _M0L2__S923 + 1;
      _M0L2__S923 = _M0L6_2atmpS1894;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS922);
    }
    break;
  }
  return _M0L16file__and__indexS918;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS911(
  int32_t _M0L6_2aenvS1871,
  moonbit_string_t _M0L1sS912,
  int32_t _M0L3sepS913
) {
  moonbit_string_t* _M0L6_2atmpS1889;
  struct _M0TPB5ArrayGsE* _M0L3resS914;
  struct _M0TPB8MutLocalGiE* _M0L1iS915;
  struct _M0TPB8MutLocalGiE* _M0L5startS916;
  int32_t _M0L3valS1884;
  int32_t _M0L6_2atmpS1885;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1889 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS914
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS914)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS914->$0 = _M0L6_2atmpS1889;
  _M0L3resS914->$1 = 0;
  _M0L1iS915
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS915)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS915->$0 = 0;
  _M0L5startS916
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS916)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS916->$0 = 0;
  while (1) {
    int32_t _M0L3valS1872 = _M0L1iS915->$0;
    int32_t _M0L6_2atmpS1873 = Moonbit_array_length(_M0L1sS912);
    if (_M0L3valS1872 < _M0L6_2atmpS1873) {
      int32_t _M0L3valS1876 = _M0L1iS915->$0;
      int32_t _M0L6_2atmpS1875;
      int32_t _M0L6_2atmpS1874;
      int32_t _M0L3valS1883;
      int32_t _M0L6_2atmpS1882;
      if (
        _M0L3valS1876 < 0
        || _M0L3valS1876 >= Moonbit_array_length(_M0L1sS912)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1875 = _M0L1sS912[_M0L3valS1876];
      _M0L6_2atmpS1874 = _M0L6_2atmpS1875;
      if (_M0L6_2atmpS1874 == _M0L3sepS913) {
        int32_t _M0L3valS1878 = _M0L5startS916->$0;
        int32_t _M0L3valS1879 = _M0L1iS915->$0;
        moonbit_string_t _M0L6_2atmpS1877;
        int32_t _M0L3valS1881;
        int32_t _M0L6_2atmpS1880;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1877
        = _M0MPC16string6String17unsafe__substring(_M0L1sS912, _M0L3valS1878, _M0L3valS1879);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS914, _M0L6_2atmpS1877);
        _M0L3valS1881 = _M0L1iS915->$0;
        _M0L6_2atmpS1880 = _M0L3valS1881 + 1;
        _M0L5startS916->$0 = _M0L6_2atmpS1880;
      }
      _M0L3valS1883 = _M0L1iS915->$0;
      _M0L6_2atmpS1882 = _M0L3valS1883 + 1;
      _M0L1iS915->$0 = _M0L6_2atmpS1882;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS915);
    }
    break;
  }
  _M0L3valS1884 = _M0L5startS916->$0;
  _M0L6_2atmpS1885 = Moonbit_array_length(_M0L1sS912);
  if (_M0L3valS1884 < _M0L6_2atmpS1885) {
    int32_t _M0L3valS1887 = _M0L5startS916->$0;
    int32_t _M0L6_2atmpS1888;
    moonbit_string_t _M0L6_2atmpS1886;
    moonbit_decref_cycle_free(_M0L5startS916);
    _M0L6_2atmpS1888 = Moonbit_array_length(_M0L1sS912);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1886
    = _M0MPC16string6String17unsafe__substring(_M0L1sS912, _M0L3valS1887, _M0L6_2atmpS1888);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS914, _M0L6_2atmpS1886);
  } else {
    moonbit_decref_cycle_free(_M0L5startS916);
  }
  return _M0L3resS914;
}

int32_t _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S904(
  int32_t _M0L6_2aenvS1864,
  moonbit_string_t _M0L1sS905
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS906;
  int32_t _M0L3lenS907;
  int32_t _M0L7_2abindS908;
  int32_t _M0L1iS909;
  int32_t _result_1972;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS906
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS906)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS906->$0 = 0;
  _M0L3lenS907 = Moonbit_array_length(_M0L1sS905);
  _M0L7_2abindS908 = 0;
  _M0L1iS909 = _M0L7_2abindS908;
  while (1) {
    if (_M0L1iS909 < _M0L3lenS907) {
      int32_t _M0L3valS1869 = _M0L3resS906->$0;
      int32_t _M0L6_2atmpS1866 = _M0L3valS1869 * 10;
      int32_t _M0L6_2atmpS1868;
      int32_t _M0L6_2atmpS1867;
      int32_t _M0L6_2atmpS1865;
      int32_t _M0L6_2atmpS1870;
      if (_M0L1iS909 < 0 || _M0L1iS909 >= Moonbit_array_length(_M0L1sS905)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1868 = _M0L1sS905[_M0L1iS909];
      _M0L6_2atmpS1867 = _M0L6_2atmpS1868 - 48;
      _M0L6_2atmpS1865 = _M0L6_2atmpS1866 + _M0L6_2atmpS1867;
      _M0L3resS906->$0 = _M0L6_2atmpS1865;
      _M0L6_2atmpS1870 = _M0L1iS909 + 1;
      _M0L1iS909 = _M0L6_2atmpS1870;
      continue;
    }
    break;
  }
  _result_1972 = _M0L3resS906->$0;
  moonbit_decref_cycle_free(_M0L3resS906);
  return _result_1972;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25cuba__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS903
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS903);
  return _M0L4selfS903;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S873,
  moonbit_string_t _M0L12_2adiscard__S874,
  int32_t _M0L12_2adiscard__S875,
  struct _M0TWEu* _M0L12_2adiscard__S876,
  struct _M0TWssbEu* _M0L12_2adiscard__S877,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S878
) {
  struct moonbit_result_0 _result_1973;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_1973.tag = 1;
  _result_1973.data.ok = 0;
  return _result_1973;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S879,
  moonbit_string_t _M0L12_2adiscard__S880,
  int32_t _M0L12_2adiscard__S881,
  struct _M0TWEu* _M0L12_2adiscard__S882,
  struct _M0TWssbEu* _M0L12_2adiscard__S883,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S884
) {
  struct moonbit_result_0 _result_1974;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_1974.tag = 1;
  _result_1974.data.ok = 0;
  return _result_1974;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S885,
  moonbit_string_t _M0L12_2adiscard__S886,
  int32_t _M0L12_2adiscard__S887,
  struct _M0TWEu* _M0L12_2adiscard__S888,
  struct _M0TWssbEu* _M0L12_2adiscard__S889,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S890
) {
  struct moonbit_result_0 _result_1975;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_1975.tag = 1;
  _result_1975.data.ok = 0;
  return _result_1975;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S891,
  moonbit_string_t _M0L12_2adiscard__S892,
  int32_t _M0L12_2adiscard__S893,
  struct _M0TWEu* _M0L12_2adiscard__S894,
  struct _M0TWssbEu* _M0L12_2adiscard__S895,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S896
) {
  struct moonbit_result_0 _result_1976;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_1976.tag = 1;
  _result_1976.data.ok = 0;
  return _result_1976;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S897,
  moonbit_string_t _M0L12_2adiscard__S898,
  int32_t _M0L12_2adiscard__S899,
  struct _M0TWEu* _M0L12_2adiscard__S900,
  struct _M0TWssbEu* _M0L12_2adiscard__S901,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S902
) {
  struct moonbit_result_0 _result_1977;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _result_1977.tag = 1;
  _result_1977.data.ok = 0;
  return _result_1977;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S872
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS823,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS824,
  moonbit_string_t _M0L3symS829,
  float _M0L2muS825,
  float _M0L5sigmaS826,
  float _M0L1pS827,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS828
) {
  int32_t _M0L1nS1862;
  int32_t _M0L1nS1863;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS822;
  float* _M0L6_2atmpS1861;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1852;
  float* _M0L6_2atmpS1860;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1853;
  float* _M0L6_2atmpS1859;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1854;
  int32_t* _M0L6_2atmpS1858;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1855;
  float* _M0L6_2atmpS1857;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1856;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_1978;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1862 = _M0L3preS823->$2;
  _M0L1nS1863 = _M0L4postS824->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS822
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS1862, _M0L1nS1863, _M0L2muS825, _M0L5sigmaS826, _M0L1pS827, _M0L3rngS828);
  _M0L6_2atmpS1861 = moonbit_empty_float_array;
  _M0L6_2atmpS1852
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1852)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1852->$0 = _M0L6_2atmpS1861;
  _M0L6_2atmpS1852->$1 = 0;
  _M0L6_2atmpS1860 = moonbit_empty_float_array;
  _M0L6_2atmpS1853
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1853)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1853->$0 = _M0L6_2atmpS1860;
  _M0L6_2atmpS1853->$1 = 0;
  _M0L6_2atmpS1859 = moonbit_empty_float_array;
  _M0L6_2atmpS1854
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1854)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1854->$0 = _M0L6_2atmpS1859;
  _M0L6_2atmpS1854->$1 = 0;
  _M0L6_2atmpS1858 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS1855
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1855)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS1855->$0 = _M0L6_2atmpS1858;
  _M0L6_2atmpS1855->$1 = 0;
  _M0L6_2atmpS1857 = moonbit_empty_float_array;
  _M0L6_2atmpS1856
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1856)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1856->$0 = _M0L6_2atmpS1857;
  _M0L6_2atmpS1856->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS823);
  moonbit_incref_cycle_free(_M0L4postS824);
  moonbit_incref_cycle_free(_M0L3symS829);
  _block_1978
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_1978)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_1978->$0 = _M0L3preS823;
  _block_1978->$1 = _M0L4postS824;
  _block_1978->$2 = _M0L3symS829;
  _block_1978->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_1978->$4 = _M0L6matrixS822;
  _block_1978->$5 = _M0L6_2atmpS1852;
  _block_1978->$6 = _M0L6_2atmpS1853;
  _block_1978->$7 = _M0L6_2atmpS1854;
  _block_1978->$8 = _M0L6_2atmpS1855;
  _block_1978->$9 = _M0L6_2atmpS1856;
  return _block_1978;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS817,
  float _M0L2vtS818,
  float _M0L2vrS819,
  float _M0L2elS820,
  float _M0L1rS821
) {
  float _M0L1cS815;
  float _M0L2glS816;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_1979;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS815 = -0x1p+0f;
  _M0L2glS816 = -0x1p+0f;
  _block_1979
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_1979)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1979->$0 = _M0L1cS815;
  _block_1979->$1 = _M0L2glS816;
  _block_1979->$2 = _M0L2tmS817;
  _block_1979->$3 = _M0L2vtS818;
  _block_1979->$4 = _M0L2vrS819;
  _block_1979->$5 = _M0L2elS820;
  _block_1979->$6 = _M0L1rS821;
  _block_1979->$7 = 0x1p+1f;
  _block_1979->$8 = 0x0p+0f;
  _block_1979->$9 = 0x0p+0f;
  _block_1979->$10 = 0x0p+0f;
  return _block_1979;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS789,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS791,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS794
) {
  struct _M0TPB5ArrayGfE* _M0L1vS788;
  float _M0L2vtS1850;
  float _M0L2vrS1851;
  float _M0L6spreadS790;
  int32_t _M0L7_2abindS792;
  int32_t _M0L1kS793;
  struct _M0TPB5ArrayGfE* _M0L1wS796;
  struct _M0TPB5ArrayGbE* _M0L4fireS797;
  struct _M0TPB5ArrayGiE* _M0L4tabsS798;
  struct _M0TPB5ArrayGfE* _M0L1iS799;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS800;
  struct _M0TPB5ArrayGfE* _M0L2geS801;
  struct _M0TPB5ArrayGfE* _M0L2giS802;
  struct _M0TPB5ArrayGfE* _M0L2heS803;
  struct _M0TPB5ArrayGfE* _M0L2hiS804;
  struct _M0TPB5ArrayGfE* _M0L3gluS805;
  struct _M0TPB5ArrayGfE* _M0L4gabaS806;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS807;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS808;
  float _M0L4e__eS809;
  float _M0L4e__iS810;
  float _M0L3treS811;
  float _M0L3tdeS812;
  float _M0L3triS813;
  float _M0L3tdiS814;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS1849;
  struct _M0TP26RiantR8snn__mbt2IF* _block_1981;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS788 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  _M0L2vtS1850 = _M0L5paramS791->$3;
  _M0L2vrS1851 = _M0L5paramS791->$4;
  _M0L6spreadS790 = _M0L2vtS1850 - _M0L2vrS1851;
  _M0L7_2abindS792 = 0;
  _M0L1kS793 = _M0L7_2abindS792;
  while (1) {
    if (_M0L1kS793 < _M0L1nS789) {
      float _M0L2vrS1845 = _M0L5paramS791->$4;
      float _M0L6_2atmpS1847;
      float _M0L6_2atmpS1846;
      float _M0L6_2atmpS1844;
      int32_t _M0L6_2atmpS1848;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1847 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS794);
      _M0L6_2atmpS1846 = _M0L6_2atmpS1847 * _M0L6spreadS790;
      _M0L6_2atmpS1844 = _M0L2vrS1845 + _M0L6_2atmpS1846;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS788, _M0L1kS793, _M0L6_2atmpS1844);
      _M0L6_2atmpS1848 = _M0L1kS793 + 1;
      _M0L1kS793 = _M0L6_2atmpS1848;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS796 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS797 = _M0MPC15array5Array4makeGbE(_M0L1nS789, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS798 = _M0MPC15array5Array4makeGiE(_M0L1nS789, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS799 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS800 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS801 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS802 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS803 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS804 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS805 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS806 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS807 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS808 = _M0MPC15array5Array4makeGfE(_M0L1nS789, 0x1p+0f);
  _M0L4e__eS809 = 0x0p+0f;
  _M0L4e__iS810 = -0x1.2cp+6f;
  _M0L3treS811 = 0x1p+0f;
  _M0L3tdeS812 = 0x1.8p+2f;
  _M0L3triS813 = 0x1p-1f;
  _M0L3tdiS814 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS1849 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS791);
  _block_1981
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_1981)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_1981->$0 = _M0L5paramS791;
  _block_1981->$1 = _M0L6_2atmpS1849;
  _block_1981->$2 = _M0L1nS789;
  _block_1981->$3 = _M0L1vS788;
  _block_1981->$4 = _M0L1wS796;
  _block_1981->$5 = _M0L4fireS797;
  _block_1981->$6 = _M0L4tabsS798;
  _block_1981->$7 = _M0L1iS799;
  _block_1981->$8 = _M0L9syn__currS800;
  _block_1981->$9 = _M0L2geS801;
  _block_1981->$10 = _M0L2giS802;
  _block_1981->$11 = _M0L2heS803;
  _block_1981->$12 = _M0L2hiS804;
  _block_1981->$13 = _M0L3gluS805;
  _block_1981->$14 = _M0L4gabaS806;
  _block_1981->$15 = _M0L7gsyn__eS807;
  _block_1981->$16 = _M0L7gsyn__iS808;
  _block_1981->$17 = _M0L4e__eS809;
  _block_1981->$18 = _M0L4e__iS810;
  _block_1981->$19 = _M0L3treS811;
  _block_1981->$20 = _M0L3tdeS812;
  _block_1981->$21 = _M0L3triS813;
  _block_1981->$22 = _M0L3tdiS814;
  return _block_1981;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_1982;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_1982
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_1982)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1982->$0 = 0x1p+1f;
  return _block_1982;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt11step__model(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS763,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS761
) {
  float _M0L6t__nowS760;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS762;
  int32_t _M0L7_2abindS764;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS765;
  int32_t _M0L2__S766;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS769;
  int32_t _M0L7_2abindS770;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS771;
  int32_t _M0L2__S772;
  float _M0L2dtS775;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L7_2abindS776;
  int32_t _M0L7_2abindS777;
  struct _M0TP26RiantR8snn__mbt2IF** _M0L7_2abindS778;
  int32_t _M0L2__S779;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS782;
  int32_t _M0L7_2abindS783;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS784;
  int32_t _M0L2__S785;
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6t__nowS760 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS761);
  _M0L7_2abindS762 = _M0L5modelS763->$1;
  _M0L7_2abindS764 = _M0L7_2abindS762->$1;
  _M0L7_2abindS765 = _M0L7_2abindS762->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS765);
  _M0L2__S766 = 0;
  while (1) {
    if (_M0L2__S766 < _M0L7_2abindS764) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS767 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS765[
          _M0L2__S766
        ];
      int32_t _M0L6_2atmpS1838;
      moonbit_incref_cycle_free(_M0L1cS767);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS767, _M0L6t__nowS760);
      moonbit_decref_cycle_free(_M0L1cS767);
      _M0L6_2atmpS1838 = _M0L2__S766 + 1;
      _M0L2__S766 = _M0L6_2atmpS1838;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS765);
    }
    break;
  }
  _M0L7_2abindS769 = _M0L5modelS763->$1;
  _M0L7_2abindS770 = _M0L7_2abindS769->$1;
  _M0L7_2abindS771 = _M0L7_2abindS769->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS771);
  _M0L2__S772 = 0;
  while (1) {
    if (_M0L2__S772 < _M0L7_2abindS770) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS773 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS771[
          _M0L2__S772
        ];
      int32_t _M0L6_2atmpS1839;
      moonbit_incref_cycle_free(_M0L1cS773);
      #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS773, _M0L6t__nowS760);
      moonbit_decref_cycle_free(_M0L1cS773);
      _M0L6_2atmpS1839 = _M0L2__S772 + 1;
      _M0L2__S772 = _M0L6_2atmpS1839;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS771);
    }
    break;
  }
  _M0L2dtS775 = _M0L4timeS761->$2;
  _M0L7_2abindS776 = _M0L5modelS763->$0;
  _M0L7_2abindS777 = _M0L7_2abindS776->$1;
  _M0L7_2abindS778 = _M0L7_2abindS776->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS778);
  _M0L2__S779 = 0;
  while (1) {
    if (_M0L2__S779 < _M0L7_2abindS777) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS780 =
        (struct _M0TP26RiantR8snn__mbt2IF*)_M0L7_2abindS778[_M0L2__S779];
      int32_t _M0L6_2atmpS1840;
      moonbit_incref_cycle_free(_M0L1pS780);
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L1pS780, _M0L2dtS775);
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1pS780);
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L1pS780, _M0L2dtS775);
      moonbit_decref_cycle_free(_M0L1pS780);
      _M0L6_2atmpS1840 = _M0L2__S779 + 1;
      _M0L2__S779 = _M0L6_2atmpS1840;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS778);
    }
    break;
  }
  _M0L7_2abindS782 = _M0L5modelS763->$2;
  _M0L7_2abindS783 = _M0L7_2abindS782->$1;
  _M0L7_2abindS784 = _M0L7_2abindS782->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS784);
  _M0L2__S785 = 0;
  while (1) {
    if (_M0L2__S785 < _M0L7_2abindS783) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS786 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS784[_M0L2__S785];
      struct _M0TPB5ArrayGfE* _M0L1tS1842 = _M0L4timeS761->$0;
      float _M0L6_2atmpS1841;
      int32_t _M0L6_2atmpS1843;
      moonbit_incref_cycle_free(_M0L1mS786);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0L6_2atmpS1841 = _M0MPC15array5Array2atGfE(_M0L1tS1842, 0);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS786, _M0L6_2atmpS1841);
      moonbit_decref_cycle_free(_M0L1mS786);
      _M0L6_2atmpS1843 = _M0L2__S785 + 1;
      _M0L2__S785 = _M0L6_2atmpS1843;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS784);
    }
    break;
  }
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS761, _M0L2dtS775);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS736,
  float _M0L6t__nowS747
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS1837;
  int32_t _M0L6_2atmpS1836;
  int32_t _M0L10use__delayS735;
  struct _M0TPB5ArrayGfE* _M0L3rhoS1835;
  int32_t _M0L6_2atmpS1834;
  int32_t _M0L8use__rhoS737;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS1837 = _M0L1cS736->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS1836 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS1837);
  _M0L10use__delayS735 = _M0L6_2atmpS1836 > 0;
  _M0L3rhoS1835 = _M0L1cS736->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS1834 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS1835);
  _M0L8use__rhoS737 = _M0L6_2atmpS1834 > 0;
  if (_M0L10use__delayS735) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1797 = _M0L1cS736->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS1796 = _M0L3preS1797->$5;
    int32_t _M0L6n__preS738;
    struct _M0TPB8MutLocalGiE* _M0L1jS739;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS738 = _M0MPC15array5Array6lengthGbE(_M0L4fireS1796);
    _M0L1jS739
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS739)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS739->$0 = 0;
    while (1) {
      int32_t _M0L3valS1765 = _M0L1jS739->$0;
      if (_M0L3valS1765 < _M0L6n__preS738) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1768 = _M0L1cS736->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS1766 = _M0L3preS1768->$5;
        int32_t _M0L3valS1767 = _M0L1jS739->$0;
        int32_t _M0L3valS1795;
        int32_t _M0L6_2atmpS1794;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS1766, _M0L3valS1767)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1793 =
            _M0L1cS736->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS1791 = _M0L6matrixS1793->$2;
          int32_t _M0L3valS1792 = _M0L1jS739->$0;
          int32_t _M0L5startS740;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1790;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS1787;
          int32_t _M0L3valS1789;
          int32_t _M0L6_2atmpS1788;
          int32_t _M0L3endS741;
          struct _M0TPB8MutLocalGiE* _M0L1sS742;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS740
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1791, _M0L3valS1792);
          _M0L6matrixS1790 = _M0L1cS736->$4;
          _M0L6rowptrS1787 = _M0L6matrixS1790->$2;
          _M0L3valS1789 = _M0L1jS739->$0;
          _M0L6_2atmpS1788 = _M0L3valS1789 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS741
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1787, _M0L6_2atmpS1788);
          _M0L1sS742
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS742)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS742->$0 = _M0L5startS740;
          while (1) {
            int32_t _M0L3valS1769 = _M0L1sS742->$0;
            if (_M0L3valS1769 < _M0L3endS741) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1786 =
                _M0L1cS736->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS1784 = _M0L6matrixS1786->$3;
              int32_t _M0L3valS1785 = _M0L1sS742->$0;
              int32_t _M0L9post__idxS743;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1783;
              struct _M0TPB5ArrayGfE* _M0L4valsS1781;
              int32_t _M0L3valS1782;
              float _M0L1wS744;
              struct _M0TPB5ArrayGfE* _M0L6delaysS1779;
              int32_t _M0L3valS1780;
              float _M0L1dS745;
              float _M0L9w__scaledS746;
              int32_t _M0L3valS1775;
              int32_t _M0L6_2atmpS1774;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS743
              = _M0MPC15array5Array2atGiE(_M0L6colptrS1784, _M0L3valS1785);
              _M0L6matrixS1783 = _M0L1cS736->$4;
              _M0L4valsS1781 = _M0L6matrixS1783->$4;
              _M0L3valS1782 = _M0L1sS742->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS744
              = _M0MPC15array5Array2atGfE(_M0L4valsS1781, _M0L3valS1782);
              _M0L6delaysS1779 = _M0L1cS736->$5;
              _M0L3valS1780 = _M0L1sS742->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS745
              = _M0MPC15array5Array2atGfE(_M0L6delaysS1779, _M0L3valS1780);
              if (_M0L8use__rhoS737) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS1777 = _M0L1cS736->$6;
                int32_t _M0L3valS1778 = _M0L1sS742->$0;
                float _M0L6_2atmpS1776;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1776
                = _M0MPC15array5Array2atGfE(_M0L3rhoS1777, _M0L3valS1778);
                _M0L9w__scaledS746 = _M0L1wS744 * _M0L6_2atmpS1776;
              } else {
                _M0L9w__scaledS746 = _M0L1wS744;
              }
              if (_M0L1dS745 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS736, _M0L9post__idxS743, _M0L9w__scaledS746);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS1770 =
                  _M0L1cS736->$7;
                float _M0L6_2atmpS1771 = _M0L6t__nowS747 + _M0L1dS745;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS1772;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1773;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS1770, _M0L6_2atmpS1771);
                _M0L14pending__postsS1772 = _M0L1cS736->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS1772, _M0L9post__idxS743);
                _M0L16pending__weightsS1773 = _M0L1cS736->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS1773, _M0L9w__scaledS746);
              }
              _M0L3valS1775 = _M0L1sS742->$0;
              _M0L6_2atmpS1774 = _M0L3valS1775 + 1;
              _M0L1sS742->$0 = _M0L6_2atmpS1774;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS742);
            }
            break;
          }
        }
        _M0L3valS1795 = _M0L1jS739->$0;
        _M0L6_2atmpS1794 = _M0L3valS1795 + 1;
        _M0L1jS739->$0 = _M0L6_2atmpS1794;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS739);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS1831 = _M0L1cS736->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS750;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS1831 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS1831)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS1831, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS1831) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1832 = _M0L1cS736->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS1918 = _M0L4postS1832->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS1918);
      _M0L6targetS750 = _M0L8_2afieldS1918;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1833 = _M0L1cS736->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS1919 = _M0L4postS1833->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS1919);
      _M0L6targetS750 = _M0L8_2afieldS1919;
    }
    if (_M0L8use__rhoS737) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1827 = _M0L1cS736->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1826 = _M0L3preS1827->$5;
      int32_t _M0L6n__preS751;
      struct _M0TPB8MutLocalGiE* _M0L1jS752;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS751 = _M0MPC15array5Array6lengthGbE(_M0L4fireS1826);
      _M0L1jS752
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS752)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS752->$0 = 0;
      while (1) {
        int32_t _M0L3valS1798 = _M0L1jS752->$0;
        if (_M0L3valS1798 < _M0L6n__preS751) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1801 = _M0L1cS736->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS1799 = _M0L3preS1801->$5;
          int32_t _M0L3valS1800 = _M0L1jS752->$0;
          int32_t _M0L3valS1825;
          int32_t _M0L6_2atmpS1824;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS1799, _M0L3valS1800)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1823 =
              _M0L1cS736->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS1821 = _M0L6matrixS1823->$2;
            int32_t _M0L3valS1822 = _M0L1jS752->$0;
            int32_t _M0L5startS753;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1820;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS1817;
            int32_t _M0L3valS1819;
            int32_t _M0L6_2atmpS1818;
            int32_t _M0L3endS754;
            struct _M0TPB8MutLocalGiE* _M0L1sS755;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS753
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1821, _M0L3valS1822);
            _M0L6matrixS1820 = _M0L1cS736->$4;
            _M0L6rowptrS1817 = _M0L6matrixS1820->$2;
            _M0L3valS1819 = _M0L1jS752->$0;
            _M0L6_2atmpS1818 = _M0L3valS1819 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS754
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1817, _M0L6_2atmpS1818);
            _M0L1sS755
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS755)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS755->$0 = _M0L5startS753;
            while (1) {
              int32_t _M0L3valS1802 = _M0L1sS755->$0;
              if (_M0L3valS1802 < _M0L3endS754) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1816 =
                  _M0L1cS736->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS1814 =
                  _M0L6matrixS1816->$3;
                int32_t _M0L3valS1815 = _M0L1sS755->$0;
                int32_t _M0L9post__idxS756;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1813;
                struct _M0TPB5ArrayGfE* _M0L4valsS1811;
                int32_t _M0L3valS1812;
                float _M0L6_2atmpS1807;
                struct _M0TPB5ArrayGfE* _M0L3rhoS1809;
                int32_t _M0L3valS1810;
                float _M0L6_2atmpS1808;
                float _M0L9w__scaledS757;
                float _M0L6_2atmpS1804;
                float _M0L6_2atmpS1803;
                int32_t _M0L3valS1806;
                int32_t _M0L6_2atmpS1805;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS756
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1814, _M0L3valS1815);
                _M0L6matrixS1813 = _M0L1cS736->$4;
                _M0L4valsS1811 = _M0L6matrixS1813->$4;
                _M0L3valS1812 = _M0L1sS755->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1807
                = _M0MPC15array5Array2atGfE(_M0L4valsS1811, _M0L3valS1812);
                _M0L3rhoS1809 = _M0L1cS736->$6;
                _M0L3valS1810 = _M0L1sS755->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1808
                = _M0MPC15array5Array2atGfE(_M0L3rhoS1809, _M0L3valS1810);
                _M0L9w__scaledS757 = _M0L6_2atmpS1807 * _M0L6_2atmpS1808;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1804
                = _M0MPC15array5Array2atGfE(_M0L6targetS750, _M0L9post__idxS756);
                _M0L6_2atmpS1803 = _M0L6_2atmpS1804 + _M0L9w__scaledS757;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS750, _M0L9post__idxS756, _M0L6_2atmpS1803);
                _M0L3valS1806 = _M0L1sS755->$0;
                _M0L6_2atmpS1805 = _M0L3valS1806 + 1;
                _M0L1sS755->$0 = _M0L6_2atmpS1805;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS755);
              }
              break;
            }
          }
          _M0L3valS1825 = _M0L1jS752->$0;
          _M0L6_2atmpS1824 = _M0L3valS1825 + 1;
          _M0L1jS752->$0 = _M0L6_2atmpS1824;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS752);
          moonbit_decref_cycle_free(_M0L6targetS750);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1828 =
        _M0L1cS736->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1830 = _M0L1cS736->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1829 = _M0L3preS1830->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS1828, _M0L4fireS1829, _M0L6targetS750);
      moonbit_decref_cycle_free(_M0L6targetS750);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS728,
  float _M0L6t__nowS731
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS1764;
  int32_t _M0L1nS727;
  struct _M0TPB8MutLocalGiE* _M0L4keptS729;
  struct _M0TPB8MutLocalGiE* _M0L1kS730;
  int32_t _M0L3valS1763;
  int32_t _M0L6_2atmpS1762;
  struct _M0TPB8MutLocalGiE* _M0L4dropS733;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS1764 = _M0L1cS728->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS727 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS1764);
  if (_M0L1nS727 == 0) {
    return 0;
  }
  _M0L4keptS729
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS729)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS729->$0 = 0;
  _M0L1kS730
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS730)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS730->$0 = 0;
  while (1) {
    int32_t _M0L3valS1725 = _M0L1kS730->$0;
    if (_M0L3valS1725 < _M0L1nS727) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS1727 = _M0L1cS728->$7;
      int32_t _M0L3valS1728 = _M0L1kS730->$0;
      float _M0L6_2atmpS1726;
      int32_t _M0L3valS1755;
      int32_t _M0L6_2atmpS1754;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1726
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS1727, _M0L3valS1728);
      if (_M0L6_2atmpS1726 <= _M0L6t__nowS731) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS1733 = _M0L1cS728->$8;
        int32_t _M0L3valS1734 = _M0L1kS730->$0;
        int32_t _M0L6_2atmpS1729;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1731;
        int32_t _M0L3valS1732;
        float _M0L6_2atmpS1730;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS1729
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS1733, _M0L3valS1734);
        _M0L16pending__weightsS1731 = _M0L1cS728->$9;
        _M0L3valS1732 = _M0L1kS730->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS1730
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS1731, _M0L3valS1732);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS728, _M0L6_2atmpS1729, _M0L6_2atmpS1730);
      } else {
        int32_t _M0L3valS1735 = _M0L4keptS729->$0;
        int32_t _M0L3valS1736 = _M0L1kS730->$0;
        int32_t _M0L3valS1753;
        int32_t _M0L6_2atmpS1752;
        if (_M0L3valS1735 != _M0L3valS1736) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS1737 = _M0L1cS728->$7;
          int32_t _M0L3valS1738 = _M0L4keptS729->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS1740 = _M0L1cS728->$7;
          int32_t _M0L3valS1741 = _M0L1kS730->$0;
          float _M0L6_2atmpS1739;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS1742;
          int32_t _M0L3valS1743;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS1745;
          int32_t _M0L3valS1746;
          int32_t _M0L6_2atmpS1744;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1747;
          int32_t _M0L3valS1748;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1750;
          int32_t _M0L3valS1751;
          float _M0L6_2atmpS1749;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1739
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS1740, _M0L3valS1741);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS1737, _M0L3valS1738, _M0L6_2atmpS1739);
          _M0L14pending__postsS1742 = _M0L1cS728->$8;
          _M0L3valS1743 = _M0L4keptS729->$0;
          _M0L14pending__postsS1745 = _M0L1cS728->$8;
          _M0L3valS1746 = _M0L1kS730->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1744
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS1745, _M0L3valS1746);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS1742, _M0L3valS1743, _M0L6_2atmpS1744);
          _M0L16pending__weightsS1747 = _M0L1cS728->$9;
          _M0L3valS1748 = _M0L4keptS729->$0;
          _M0L16pending__weightsS1750 = _M0L1cS728->$9;
          _M0L3valS1751 = _M0L1kS730->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1749
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS1750, _M0L3valS1751);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS1747, _M0L3valS1748, _M0L6_2atmpS1749);
        }
        _M0L3valS1753 = _M0L4keptS729->$0;
        _M0L6_2atmpS1752 = _M0L3valS1753 + 1;
        _M0L4keptS729->$0 = _M0L6_2atmpS1752;
      }
      _M0L3valS1755 = _M0L1kS730->$0;
      _M0L6_2atmpS1754 = _M0L3valS1755 + 1;
      _M0L1kS730->$0 = _M0L6_2atmpS1754;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS730);
    }
    break;
  }
  _M0L3valS1763 = _M0L4keptS729->$0;
  moonbit_decref_cycle_free(_M0L4keptS729);
  _M0L6_2atmpS1762 = _M0L1nS727 - _M0L3valS1763;
  _M0L4dropS733
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS733)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS733->$0 = _M0L6_2atmpS1762;
  while (1) {
    int32_t _M0L3valS1756 = _M0L4dropS733->$0;
    if (_M0L3valS1756 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS1757 = _M0L1cS728->$7;
      void* _M0L6_2atmpS1921;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS1758;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1759;
      void* _M0L6_2atmpS1920;
      int32_t _M0L3valS1761;
      int32_t _M0L6_2atmpS1760;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1921
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS1757);
      moonbit_decref_cycle_free(_M0L6_2atmpS1921);
      _M0L14pending__postsS1758 = _M0L1cS728->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS1758);
      _M0L16pending__weightsS1759 = _M0L1cS728->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1920
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS1759);
      moonbit_decref_cycle_free(_M0L6_2atmpS1920);
      _M0L3valS1761 = _M0L4dropS733->$0;
      _M0L6_2atmpS1760 = _M0L3valS1761 - 1;
      _M0L4dropS733->$0 = _M0L6_2atmpS1760;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS733);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS724,
  int32_t _M0L9post__idxS725,
  float _M0L1wS726
) {
  moonbit_string_t _M0L3symS1712;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS1712 = _M0L1cS724->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS1712 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS1712)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS1712, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS1712) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1718 = _M0L1cS724->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1713 = _M0L4postS1718->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1717 = _M0L1cS724->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1716 = _M0L4postS1717->$13;
    float _M0L6_2atmpS1715;
    float _M0L6_2atmpS1714;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1715
    = _M0MPC15array5Array2atGfE(_M0L3gluS1716, _M0L9post__idxS725);
    _M0L6_2atmpS1714 = _M0L6_2atmpS1715 + _M0L1wS726;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS1713, _M0L9post__idxS725, _M0L6_2atmpS1714);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1724 = _M0L1cS724->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1719 = _M0L4postS1724->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1723 = _M0L1cS724->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1722 = _M0L4postS1723->$14;
    float _M0L6_2atmpS1721;
    float _M0L6_2atmpS1720;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1721
    = _M0MPC15array5Array2atGfE(_M0L4gabaS1722, _M0L9post__idxS725);
    _M0L6_2atmpS1720 = _M0L6_2atmpS1721 + _M0L1wS726;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS1719, _M0L9post__idxS725, _M0L6_2atmpS1720);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS718
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS717;
  int32_t _M0L7_2abindS719;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS720;
  int32_t _M0L2__S721;
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS717 = _M0L5modelS718->$2;
  _M0L7_2abindS719 = _M0L7_2abindS717->$1;
  _M0L7_2abindS720 = _M0L7_2abindS717->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS720);
  _M0L2__S721 = 0;
  while (1) {
    if (_M0L2__S721 < _M0L7_2abindS719) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS722 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS720[_M0L2__S721];
      int32_t _M0L6_2atmpS1711;
      moonbit_incref_cycle_free(_M0L1mS722);
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS722, 0x0p+0f);
      moonbit_decref_cycle_free(_M0L1mS722);
      _M0L6_2atmpS1711 = _M0L2__S721 + 1;
      _M0L2__S721 = _M0L6_2atmpS1711;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS720);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS714,
  float _M0L1tS716
) {
  int32_t _M0L11step__countS1697;
  int32_t _M0L6_2atmpS1696;
  int32_t _M0L11step__countS1699;
  int32_t _M0L9rec__stepS1700;
  int32_t _M0L6_2atmpS1698;
  moonbit_string_t _M0L3symS1703;
  float _M0L1vS715;
  struct _M0TPB5ArrayGfE* _M0L4dataS1701;
  struct _M0TPB5ArrayGfE* _M0L5timesS1702;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS1697 = _M0L1mS714->$6;
  _M0L6_2atmpS1696 = _M0L11step__countS1697 + 1;
  _M0L1mS714->$6 = _M0L6_2atmpS1696;
  _M0L11step__countS1699 = _M0L1mS714->$6;
  _M0L9rec__stepS1700 = _M0L1mS714->$5;
  _M0L6_2atmpS1698 = _M0L11step__countS1699 % _M0L9rec__stepS1700;
  if (_M0L6_2atmpS1698 != 0) {
    return 0;
  }
  _M0L3symS1703 = _M0L1mS714->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS1703 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS1703)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS1703, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS1703) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1706 = _M0L1mS714->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS1704 = _M0L3popS1706->$3;
    int32_t _M0L6neuronS1705 = _M0L1mS714->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS715 = _M0MPC15array5Array2atGfE(_M0L1vS1704, _M0L6neuronS1705);
  } else {
    moonbit_string_t _M0L3symS1707 = _M0L1mS714->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS1707 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS1707)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS1707, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS1707) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1710 = _M0L1mS714->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1708 = _M0L3popS1710->$5;
      int32_t _M0L6neuronS1709 = _M0L1mS714->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1708, _M0L6neuronS1709)) {
        _M0L1vS715 = 0x1p+0f;
      } else {
        _M0L1vS715 = 0x0p+0f;
      }
    } else {
      _M0L1vS715 = 0x0p+0f;
    }
  }
  _M0L4dataS1701 = _M0L1mS714->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS1701, _M0L1vS715);
  _M0L5timesS1702 = _M0L1mS714->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS1702, _M0L1tS716);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS712,
  int32_t _M0L6neuronS713
) {
  float* _M0L6_2atmpS1695;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1692;
  float* _M0L6_2atmpS1694;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1693;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_1994;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS1695 = moonbit_empty_float_array;
  _M0L6_2atmpS1692
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1692)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1692->$0 = _M0L6_2atmpS1695;
  _M0L6_2atmpS1692->$1 = 0;
  _M0L6_2atmpS1694 = moonbit_empty_float_array;
  _M0L6_2atmpS1693
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1693)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1693->$0 = _M0L6_2atmpS1694;
  _M0L6_2atmpS1693->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS712);
  _block_1994
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_1994)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_1994->$0 = _M0L3popS712;
  _block_1994->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_1994->$2 = _M0L6_2atmpS1692;
  _block_1994->$3 = _M0L6_2atmpS1693;
  _block_1994->$4 = _M0L6neuronS713;
  _block_1994->$5 = 1;
  _block_1994->$6 = 0;
  return _block_1994;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS708
) {
  int32_t _M0L1nS707;
  int32_t _M0L7_2abindS709;
  int32_t _M0L1iS710;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS707 = _M0L1pS708->$2;
  _M0L7_2abindS709 = 0;
  _M0L1iS710 = _M0L7_2abindS709;
  while (1) {
    if (_M0L1iS710 < _M0L1nS707) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1669 = _M0L1pS708->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS1690 = _M0L1pS708->$9;
      float _M0L6_2atmpS1685;
      struct _M0TPB5ArrayGfE* _M0L1vS1689;
      float _M0L6_2atmpS1687;
      float _M0L4e__eS1688;
      float _M0L6_2atmpS1686;
      float _M0L6_2atmpS1682;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1684;
      float _M0L6_2atmpS1683;
      float _M0L6_2atmpS1671;
      struct _M0TPB5ArrayGfE* _M0L2giS1681;
      float _M0L6_2atmpS1676;
      struct _M0TPB5ArrayGfE* _M0L1vS1680;
      float _M0L6_2atmpS1678;
      float _M0L4e__iS1679;
      float _M0L6_2atmpS1677;
      float _M0L6_2atmpS1673;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1675;
      float _M0L6_2atmpS1674;
      float _M0L6_2atmpS1672;
      float _M0L6_2atmpS1670;
      int32_t _M0L6_2atmpS1691;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1685 = _M0MPC15array5Array2atGfE(_M0L2geS1690, _M0L1iS710);
      _M0L1vS1689 = _M0L1pS708->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1687 = _M0MPC15array5Array2atGfE(_M0L1vS1689, _M0L1iS710);
      _M0L4e__eS1688 = _M0L1pS708->$17;
      _M0L6_2atmpS1686 = _M0L6_2atmpS1687 - _M0L4e__eS1688;
      _M0L6_2atmpS1682 = _M0L6_2atmpS1685 * _M0L6_2atmpS1686;
      _M0L7gsyn__eS1684 = _M0L1pS708->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1683
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1684, _M0L1iS710);
      _M0L6_2atmpS1671 = _M0L6_2atmpS1682 * _M0L6_2atmpS1683;
      _M0L2giS1681 = _M0L1pS708->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1676 = _M0MPC15array5Array2atGfE(_M0L2giS1681, _M0L1iS710);
      _M0L1vS1680 = _M0L1pS708->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1678 = _M0MPC15array5Array2atGfE(_M0L1vS1680, _M0L1iS710);
      _M0L4e__iS1679 = _M0L1pS708->$18;
      _M0L6_2atmpS1677 = _M0L6_2atmpS1678 - _M0L4e__iS1679;
      _M0L6_2atmpS1673 = _M0L6_2atmpS1676 * _M0L6_2atmpS1677;
      _M0L7gsyn__iS1675 = _M0L1pS708->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1674
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1675, _M0L1iS710);
      _M0L6_2atmpS1672 = _M0L6_2atmpS1673 * _M0L6_2atmpS1674;
      _M0L6_2atmpS1670 = _M0L6_2atmpS1671 + _M0L6_2atmpS1672;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1669, _M0L1iS710, _M0L6_2atmpS1670);
      _M0L6_2atmpS1691 = _M0L1iS710 + 1;
      _M0L1iS710 = _M0L6_2atmpS1691;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS699,
  float _M0L2dtS702
) {
  int32_t _M0L1nS698;
  int32_t _M0L7_2abindS700;
  int32_t _M0L1iS701;
  int32_t _M0L7_2abindS704;
  int32_t _M0L1iS705;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS698 = _M0L1pS699->$2;
  _M0L7_2abindS700 = 0;
  _M0L1iS701 = _M0L7_2abindS700;
  while (1) {
    if (_M0L1iS701 < _M0L1nS698) {
      struct _M0TPB5ArrayGfE* _M0L2heS1607 = _M0L1pS699->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1612 = _M0L1pS699->$11;
      float _M0L6_2atmpS1609;
      struct _M0TPB5ArrayGfE* _M0L3gluS1611;
      float _M0L6_2atmpS1610;
      float _M0L6_2atmpS1608;
      struct _M0TPB5ArrayGfE* _M0L2hiS1613;
      struct _M0TPB5ArrayGfE* _M0L2hiS1618;
      float _M0L6_2atmpS1615;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1617;
      float _M0L6_2atmpS1616;
      float _M0L6_2atmpS1614;
      struct _M0TPB5ArrayGfE* _M0L2geS1619;
      struct _M0TPB5ArrayGfE* _M0L2geS1631;
      float _M0L6_2atmpS1621;
      struct _M0TPB5ArrayGfE* _M0L2geS1630;
      float _M0L6_2atmpS1629;
      float _M0L6_2atmpS1627;
      float _M0L3tdeS1628;
      float _M0L6_2atmpS1624;
      struct _M0TPB5ArrayGfE* _M0L2heS1626;
      float _M0L6_2atmpS1625;
      float _M0L6_2atmpS1623;
      float _M0L6_2atmpS1622;
      float _M0L6_2atmpS1620;
      struct _M0TPB5ArrayGfE* _M0L2heS1632;
      struct _M0TPB5ArrayGfE* _M0L2heS1641;
      float _M0L6_2atmpS1634;
      struct _M0TPB5ArrayGfE* _M0L2heS1640;
      float _M0L6_2atmpS1639;
      float _M0L6_2atmpS1637;
      float _M0L3treS1638;
      float _M0L6_2atmpS1636;
      float _M0L6_2atmpS1635;
      float _M0L6_2atmpS1633;
      struct _M0TPB5ArrayGfE* _M0L2giS1642;
      struct _M0TPB5ArrayGfE* _M0L2giS1654;
      float _M0L6_2atmpS1644;
      struct _M0TPB5ArrayGfE* _M0L2giS1653;
      float _M0L6_2atmpS1652;
      float _M0L6_2atmpS1650;
      float _M0L3tdiS1651;
      float _M0L6_2atmpS1647;
      struct _M0TPB5ArrayGfE* _M0L2hiS1649;
      float _M0L6_2atmpS1648;
      float _M0L6_2atmpS1646;
      float _M0L6_2atmpS1645;
      float _M0L6_2atmpS1643;
      struct _M0TPB5ArrayGfE* _M0L2hiS1655;
      struct _M0TPB5ArrayGfE* _M0L2hiS1664;
      float _M0L6_2atmpS1657;
      struct _M0TPB5ArrayGfE* _M0L2hiS1663;
      float _M0L6_2atmpS1662;
      float _M0L6_2atmpS1660;
      float _M0L3triS1661;
      float _M0L6_2atmpS1659;
      float _M0L6_2atmpS1658;
      float _M0L6_2atmpS1656;
      int32_t _M0L6_2atmpS1665;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1609 = _M0MPC15array5Array2atGfE(_M0L2heS1612, _M0L1iS701);
      _M0L3gluS1611 = _M0L1pS699->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1610 = _M0MPC15array5Array2atGfE(_M0L3gluS1611, _M0L1iS701);
      _M0L6_2atmpS1608 = _M0L6_2atmpS1609 + _M0L6_2atmpS1610;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1607, _M0L1iS701, _M0L6_2atmpS1608);
      _M0L2hiS1613 = _M0L1pS699->$12;
      _M0L2hiS1618 = _M0L1pS699->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1615 = _M0MPC15array5Array2atGfE(_M0L2hiS1618, _M0L1iS701);
      _M0L4gabaS1617 = _M0L1pS699->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1616
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1617, _M0L1iS701);
      _M0L6_2atmpS1614 = _M0L6_2atmpS1615 + _M0L6_2atmpS1616;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1613, _M0L1iS701, _M0L6_2atmpS1614);
      _M0L2geS1619 = _M0L1pS699->$9;
      _M0L2geS1631 = _M0L1pS699->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1621 = _M0MPC15array5Array2atGfE(_M0L2geS1631, _M0L1iS701);
      _M0L2geS1630 = _M0L1pS699->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1629 = _M0MPC15array5Array2atGfE(_M0L2geS1630, _M0L1iS701);
      _M0L6_2atmpS1627 = -_M0L6_2atmpS1629;
      _M0L3tdeS1628 = _M0L1pS699->$20;
      _M0L6_2atmpS1624 = _M0L6_2atmpS1627 / _M0L3tdeS1628;
      _M0L2heS1626 = _M0L1pS699->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1625 = _M0MPC15array5Array2atGfE(_M0L2heS1626, _M0L1iS701);
      _M0L6_2atmpS1623 = _M0L6_2atmpS1624 + _M0L6_2atmpS1625;
      _M0L6_2atmpS1622 = _M0L2dtS702 * _M0L6_2atmpS1623;
      _M0L6_2atmpS1620 = _M0L6_2atmpS1621 + _M0L6_2atmpS1622;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1619, _M0L1iS701, _M0L6_2atmpS1620);
      _M0L2heS1632 = _M0L1pS699->$11;
      _M0L2heS1641 = _M0L1pS699->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1634 = _M0MPC15array5Array2atGfE(_M0L2heS1641, _M0L1iS701);
      _M0L2heS1640 = _M0L1pS699->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1639 = _M0MPC15array5Array2atGfE(_M0L2heS1640, _M0L1iS701);
      _M0L6_2atmpS1637 = -_M0L6_2atmpS1639;
      _M0L3treS1638 = _M0L1pS699->$19;
      _M0L6_2atmpS1636 = _M0L6_2atmpS1637 / _M0L3treS1638;
      _M0L6_2atmpS1635 = _M0L2dtS702 * _M0L6_2atmpS1636;
      _M0L6_2atmpS1633 = _M0L6_2atmpS1634 + _M0L6_2atmpS1635;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1632, _M0L1iS701, _M0L6_2atmpS1633);
      _M0L2giS1642 = _M0L1pS699->$10;
      _M0L2giS1654 = _M0L1pS699->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1644 = _M0MPC15array5Array2atGfE(_M0L2giS1654, _M0L1iS701);
      _M0L2giS1653 = _M0L1pS699->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1652 = _M0MPC15array5Array2atGfE(_M0L2giS1653, _M0L1iS701);
      _M0L6_2atmpS1650 = -_M0L6_2atmpS1652;
      _M0L3tdiS1651 = _M0L1pS699->$22;
      _M0L6_2atmpS1647 = _M0L6_2atmpS1650 / _M0L3tdiS1651;
      _M0L2hiS1649 = _M0L1pS699->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1648 = _M0MPC15array5Array2atGfE(_M0L2hiS1649, _M0L1iS701);
      _M0L6_2atmpS1646 = _M0L6_2atmpS1647 + _M0L6_2atmpS1648;
      _M0L6_2atmpS1645 = _M0L2dtS702 * _M0L6_2atmpS1646;
      _M0L6_2atmpS1643 = _M0L6_2atmpS1644 + _M0L6_2atmpS1645;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1642, _M0L1iS701, _M0L6_2atmpS1643);
      _M0L2hiS1655 = _M0L1pS699->$12;
      _M0L2hiS1664 = _M0L1pS699->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1657 = _M0MPC15array5Array2atGfE(_M0L2hiS1664, _M0L1iS701);
      _M0L2hiS1663 = _M0L1pS699->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1662 = _M0MPC15array5Array2atGfE(_M0L2hiS1663, _M0L1iS701);
      _M0L6_2atmpS1660 = -_M0L6_2atmpS1662;
      _M0L3triS1661 = _M0L1pS699->$21;
      _M0L6_2atmpS1659 = _M0L6_2atmpS1660 / _M0L3triS1661;
      _M0L6_2atmpS1658 = _M0L2dtS702 * _M0L6_2atmpS1659;
      _M0L6_2atmpS1656 = _M0L6_2atmpS1657 + _M0L6_2atmpS1658;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1655, _M0L1iS701, _M0L6_2atmpS1656);
      _M0L6_2atmpS1665 = _M0L1iS701 + 1;
      _M0L1iS701 = _M0L6_2atmpS1665;
      continue;
    }
    break;
  }
  _M0L7_2abindS704 = 0;
  _M0L1iS705 = _M0L7_2abindS704;
  while (1) {
    if (_M0L1iS705 < _M0L1nS698) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1666 = _M0L1pS699->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1667;
      int32_t _M0L6_2atmpS1668;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1666, _M0L1iS705, 0x0p+0f);
      _M0L4gabaS1667 = _M0L1pS699->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1667, _M0L1iS705, 0x0p+0f);
      _M0L6_2atmpS1668 = _M0L1iS705 + 1;
      _M0L1iS705 = _M0L6_2atmpS1668;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS684,
  float _M0L2dtS693
) {
  int32_t _M0L1nS683;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S685;
  float _M0L2tmS686;
  float _M0L2elS687;
  float _M0L1rS688;
  float _M0L2vtS689;
  float _M0L2vrS690;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1606;
  float _M0L11tabs__constS691;
  float _M0L6_2atmpS1605;
  int32_t _M0L11tabs__stepsS692;
  int32_t _M0L7_2abindS694;
  int32_t _M0L1iS695;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS683 = _M0L1pS684->$2;
  _M0L3p__S685 = _M0L1pS684->$0;
  _M0L2tmS686 = _M0L3p__S685->$2;
  _M0L2elS687 = _M0L3p__S685->$5;
  _M0L1rS688 = _M0L3p__S685->$6;
  _M0L2vtS689 = _M0L3p__S685->$3;
  _M0L2vrS690 = _M0L3p__S685->$4;
  _M0L5spikeS1606 = _M0L1pS684->$1;
  _M0L11tabs__constS691 = _M0L5spikeS1606->$0;
  _M0L6_2atmpS1605 = _M0L11tabs__constS691 / _M0L2dtS693;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS692 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1605);
  _M0L7_2abindS694 = 0;
  _M0L1iS695 = _M0L7_2abindS694;
  while (1) {
    if (_M0L1iS695 < _M0L1nS683) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1565 = _M0L1pS684->$6;
      int32_t _M0L6_2atmpS1564;
      struct _M0TPB5ArrayGfE* _M0L1vS1571;
      struct _M0TPB5ArrayGfE* _M0L1vS1592;
      float _M0L6_2atmpS1573;
      float _M0L6_2atmpS1575;
      struct _M0TPB5ArrayGfE* _M0L1vS1591;
      float _M0L6_2atmpS1590;
      float _M0L6_2atmpS1589;
      float _M0L6_2atmpS1581;
      struct _M0TPB5ArrayGfE* _M0L1wS1588;
      float _M0L6_2atmpS1587;
      float _M0L6_2atmpS1584;
      struct _M0TPB5ArrayGfE* _M0L1iS1586;
      float _M0L6_2atmpS1585;
      float _M0L6_2atmpS1583;
      float _M0L6_2atmpS1582;
      float _M0L6_2atmpS1577;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1580;
      float _M0L6_2atmpS1579;
      float _M0L6_2atmpS1578;
      float _M0L6_2atmpS1576;
      float _M0L6_2atmpS1574;
      float _M0L6_2atmpS1572;
      struct _M0TPB5ArrayGbE* _M0L4fireS1593;
      struct _M0TPB5ArrayGfE* _M0L1vS1596;
      float _M0L6_2atmpS1595;
      int32_t _M0L6_2atmpS1594;
      struct _M0TPB5ArrayGfE* _M0L1vS1597;
      struct _M0TPB5ArrayGbE* _M0L4fireS1599;
      float _M0L6_2atmpS1598;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1601;
      struct _M0TPB5ArrayGbE* _M0L4fireS1603;
      int32_t _M0L6_2atmpS1602;
      int32_t _M0L6_2atmpS1563;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1564
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1565, _M0L1iS695);
      if (_M0L6_2atmpS1564 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1566 = _M0L1pS684->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1567;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1570;
        int32_t _M0L6_2atmpS1569;
        int32_t _M0L6_2atmpS1568;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1566, _M0L1iS695, 0);
        _M0L4tabsS1567 = _M0L1pS684->$6;
        _M0L4tabsS1570 = _M0L1pS684->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1569
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1570, _M0L1iS695);
        _M0L6_2atmpS1568 = _M0L6_2atmpS1569 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1567, _M0L1iS695, _M0L6_2atmpS1568);
        goto join_696;
      }
      _M0L1vS1571 = _M0L1pS684->$3;
      _M0L1vS1592 = _M0L1pS684->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1573 = _M0MPC15array5Array2atGfE(_M0L1vS1592, _M0L1iS695);
      _M0L6_2atmpS1575 = _M0L2dtS693 / _M0L2tmS686;
      _M0L1vS1591 = _M0L1pS684->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1590 = _M0MPC15array5Array2atGfE(_M0L1vS1591, _M0L1iS695);
      _M0L6_2atmpS1589 = _M0L6_2atmpS1590 - _M0L2elS687;
      _M0L6_2atmpS1581 = -_M0L6_2atmpS1589;
      _M0L1wS1588 = _M0L1pS684->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1587 = _M0MPC15array5Array2atGfE(_M0L1wS1588, _M0L1iS695);
      _M0L6_2atmpS1584 = -_M0L6_2atmpS1587;
      _M0L1iS1586 = _M0L1pS684->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1585 = _M0MPC15array5Array2atGfE(_M0L1iS1586, _M0L1iS695);
      _M0L6_2atmpS1583 = _M0L6_2atmpS1584 + _M0L6_2atmpS1585;
      _M0L6_2atmpS1582 = _M0L1rS688 * _M0L6_2atmpS1583;
      _M0L6_2atmpS1577 = _M0L6_2atmpS1581 + _M0L6_2atmpS1582;
      _M0L9syn__currS1580 = _M0L1pS684->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1579
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1580, _M0L1iS695);
      _M0L6_2atmpS1578 = _M0L1rS688 * _M0L6_2atmpS1579;
      _M0L6_2atmpS1576 = _M0L6_2atmpS1577 - _M0L6_2atmpS1578;
      _M0L6_2atmpS1574 = _M0L6_2atmpS1575 * _M0L6_2atmpS1576;
      _M0L6_2atmpS1572 = _M0L6_2atmpS1573 + _M0L6_2atmpS1574;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1571, _M0L1iS695, _M0L6_2atmpS1572);
      _M0L4fireS1593 = _M0L1pS684->$5;
      _M0L1vS1596 = _M0L1pS684->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1595 = _M0MPC15array5Array2atGfE(_M0L1vS1596, _M0L1iS695);
      _M0L6_2atmpS1594 = _M0L6_2atmpS1595 > _M0L2vtS689;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1593, _M0L1iS695, _M0L6_2atmpS1594);
      _M0L1vS1597 = _M0L1pS684->$3;
      _M0L4fireS1599 = _M0L1pS684->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1599, _M0L1iS695)) {
        _M0L6_2atmpS1598 = _M0L2vrS690;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1600 = _M0L1pS684->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1598 = _M0MPC15array5Array2atGfE(_M0L1vS1600, _M0L1iS695);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1597, _M0L1iS695, _M0L6_2atmpS1598);
      _M0L4tabsS1601 = _M0L1pS684->$6;
      _M0L4fireS1603 = _M0L1pS684->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1603, _M0L1iS695)) {
        _M0L6_2atmpS1602 = _M0L11tabs__stepsS692;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1604 = _M0L1pS684->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1602
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1604, _M0L1iS695);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1601, _M0L1iS695, _M0L6_2atmpS1602);
      goto join_696;
      goto joinlet_1999;
      join_696:;
      _M0L6_2atmpS1563 = _M0L1iS695 + 1;
      _M0L1iS695 = _M0L6_2atmpS1563;
      continue;
      joinlet_1999:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS671,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS674,
  struct _M0TPB5ArrayGfE* _M0L7post__gS680
) {
  int32_t _M0L4rowsS670;
  int32_t _M0L7_2abindS672;
  int32_t _M0L1iS673;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS670 = _M0L1mS671->$0;
  _M0L7_2abindS672 = 0;
  _M0L1iS673 = _M0L7_2abindS672;
  while (1) {
    if (_M0L1iS673 < _M0L4rowsS670) {
      int32_t _M0L6_2atmpS1562;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS674, _M0L1iS673)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1561 = _M0L1mS671->$2;
        int32_t _M0L5startS675;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1559;
        int32_t _M0L6_2atmpS1560;
        int32_t _M0L3endS676;
        int32_t _M0L1kS677;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS675
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1561, _M0L1iS673);
        _M0L6rowptrS1559 = _M0L1mS671->$2;
        _M0L6_2atmpS1560 = _M0L1iS673 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS676
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1559, _M0L6_2atmpS1560);
        _M0L1kS677 = _M0L5startS675;
        while (1) {
          if (_M0L1kS677 < _M0L3endS676) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1557 = _M0L1mS671->$3;
            int32_t _M0L9post__idxS678;
            struct _M0TPB5ArrayGfE* _M0L4valsS1556;
            float _M0L1wS679;
            float _M0L6_2atmpS1555;
            float _M0L6_2atmpS1554;
            int32_t _M0L6_2atmpS1558;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS678
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1557, _M0L1kS677);
            _M0L4valsS1556 = _M0L1mS671->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS679
            = _M0MPC15array5Array2atGfE(_M0L4valsS1556, _M0L1kS677);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1555
            = _M0MPC15array5Array2atGfE(_M0L7post__gS680, _M0L9post__idxS678);
            _M0L6_2atmpS1554 = _M0L6_2atmpS1555 + _M0L1wS679;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS680, _M0L9post__idxS678, _M0L6_2atmpS1554);
            _M0L6_2atmpS1558 = _M0L1kS677 + 1;
            _M0L1kS677 = _M0L6_2atmpS1558;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1562 = _M0L1iS673 + 1;
      _M0L1iS673 = _M0L6_2atmpS1562;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS664,
  int32_t _M0L4colsS665,
  float _M0L2muS666,
  float _M0L5sigmaS667,
  float _M0L1pS668,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS669
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS664, _M0L4colsS665, _M0L2muS666, _M0L5sigmaS667, _M0L1pS668, 0, _M0L3rngS669);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS578,
  int32_t _M0L4colsS582,
  float _M0L2muS588,
  float _M0L5sigmaS589,
  float _M0L1pS601,
  int32_t _M0L4ruleS595,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS591
) {
  float* _M0L6_2atmpS1553;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1552;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS577;
  int32_t _M0L7_2abindS579;
  int32_t _M0L1iS580;
  int32_t _M0L6_2atmpS1551;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS654;
  int32_t* _M0L6_2atmpS1550;
  struct _M0TPB5ArrayGiE* _M0L6colptrS655;
  float* _M0L6_2atmpS1549;
  struct _M0TPB5ArrayGfE* _M0L4valsS656;
  int32_t _M0L7_2abindS657;
  int32_t _M0L1iS658;
  int32_t _M0L6_2atmpS1548;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2021;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1553 = moonbit_empty_float_array;
  _M0L6_2atmpS1552
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1552)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1552->$0 = _M0L6_2atmpS1553;
  _M0L6_2atmpS1552->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS577
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS578, _M0L6_2atmpS1552);
  _M0L7_2abindS579 = 0;
  _M0L1iS580 = _M0L7_2abindS579;
  while (1) {
    if (_M0L1iS580 < _M0L4rowsS578) {
      struct _M0TPB5ArrayGfE* _M0L3rowS581;
      int32_t _M0L7_2abindS583;
      int32_t _M0L1jS584;
      int32_t _M0L6_2atmpS1504;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS581 = _M0MPC15array5Array4makeGfE(_M0L4colsS582, 0x0p+0f);
      _M0L7_2abindS583 = 0;
      _M0L1jS584 = _M0L7_2abindS583;
      while (1) {
        if (_M0L1jS584 < _M0L4colsS582) {
          double _M0L2z1S586;
          struct _M0TUddE* _M0L7_2abindS590;
          double _M0L5_2az1S592;
          float _M0L6_2atmpS1502;
          float _M0L6_2atmpS1501;
          float _M0L1wS587;
          int32_t _M0L6_2atmpS1503;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS590
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS591);
          _M0L5_2az1S592 = _M0L7_2abindS590->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS590);
          _M0L2z1S586 = _M0L5_2az1S592;
          goto join_585;
          goto joinlet_2004;
          join_585:;
          _M0L6_2atmpS1502 = (float)_M0L2z1S586;
          _M0L6_2atmpS1501 = _M0L5sigmaS589 * _M0L6_2atmpS1502;
          _M0L1wS587 = _M0L2muS588 + _M0L6_2atmpS1501;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS581, _M0L1jS584, _M0L1wS587);
          joinlet_2004:;
          _M0L6_2atmpS1503 = _M0L1jS584 + 1;
          _M0L1jS584 = _M0L6_2atmpS1503;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS577, _M0L1iS580, _M0L3rowS581);
      _M0L6_2atmpS1504 = _M0L1iS580 + 1;
      _M0L1iS580 = _M0L6_2atmpS1504;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS595) {
    case 0: {
      int32_t _M0L7_2abindS596 = 0;
      int32_t _M0L1iS597 = _M0L7_2abindS596;
      while (1) {
        if (_M0L1iS597 < _M0L4rowsS578) {
          int32_t _M0L7_2abindS598 = 0;
          int32_t _M0L1jS599 = _M0L7_2abindS598;
          int32_t _M0L6_2atmpS1507;
          while (1) {
            if (_M0L1jS599 < _M0L4colsS582) {
              float _M0L1uS600;
              int32_t _M0L6_2atmpS1506;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS600 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS591);
              if (_M0L1uS600 >= _M0L1pS601) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1505;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1505
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS577, _M0L1iS597);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1505, _M0L1jS599, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1505);
              }
              _M0L6_2atmpS1506 = _M0L1jS599 + 1;
              _M0L1jS599 = _M0L6_2atmpS1506;
              continue;
            }
            break;
          }
          _M0L6_2atmpS1507 = _M0L1iS597 + 1;
          _M0L1iS597 = _M0L6_2atmpS1507;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS1525 = (float)_M0L4rowsS578;
      float _M0L6_2atmpS1524 = _M0L6_2atmpS1525 * _M0L1pS601;
      int32_t _M0L7n__keepS604;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS604 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1524);
      if (_M0L7n__keepS604 > 0 && _M0L7n__keepS604 <= _M0L4rowsS578) {
        int32_t _M0L7_2abindS605 = 0;
        int32_t _M0L1jS606 = _M0L7_2abindS605;
        while (1) {
          if (_M0L1jS606 < _M0L4colsS582) {
            int32_t* _M0L6_2atmpS1519 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS607 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS608;
            int32_t _M0L1kS609;
            int32_t _M0L7n__dropS611;
            int32_t _M0L7_2abindS612;
            int32_t _M0L1kS613;
            int32_t _M0L7_2abindS619;
            int32_t _M0L1kS620;
            int32_t _M0L6_2atmpS1520;
            Moonbit_object_header(_M0L8pre__idxS607)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L8pre__idxS607->$0 = _M0L6_2atmpS1519;
            _M0L8pre__idxS607->$1 = 0;
            _M0L7_2abindS608 = 0;
            _M0L1kS609 = _M0L7_2abindS608;
            while (1) {
              if (_M0L1kS609 < _M0L4rowsS578) {
                int32_t _M0L6_2atmpS1508;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS607, _M0L1kS609);
                _M0L6_2atmpS1508 = _M0L1kS609 + 1;
                _M0L1kS609 = _M0L6_2atmpS1508;
                continue;
              }
              break;
            }
            _M0L7n__dropS611 = _M0L4rowsS578 - _M0L7n__keepS604;
            _M0L7_2abindS612 = 0;
            _M0L1kS613 = _M0L7_2abindS612;
            while (1) {
              if (_M0L1kS613 < _M0L7n__dropS611) {
                float _M0L1uS614;
                float _M0L6_2atmpS1512;
                float _M0L6_2atmpS1514;
                float _M0L6_2atmpS1513;
                float _M0L6_2atmpS1511;
                int32_t _M0L6_2atmpS1510;
                int32_t _M0L6r__idxS615;
                int32_t _M0L10r__clampedS616;
                int32_t _M0L3tmpS617;
                int32_t _M0L6_2atmpS1509;
                int32_t _M0L6_2atmpS1515;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS614 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS591);
                _M0L6_2atmpS1512 = (float)_M0L4rowsS578;
                _M0L6_2atmpS1514 = (float)_M0L1kS613;
                _M0L6_2atmpS1513 = _M0L6_2atmpS1514 * _M0L1uS614;
                _M0L6_2atmpS1511 = _M0L6_2atmpS1512 - _M0L6_2atmpS1513;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1510
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1511);
                _M0L6r__idxS615 = _M0L1kS613 + _M0L6_2atmpS1510;
                if (_M0L6r__idxS615 >= _M0L4rowsS578) {
                  _M0L10r__clampedS616 = _M0L4rowsS578 - 1;
                } else {
                  _M0L10r__clampedS616 = _M0L6r__idxS615;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS617
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS607, _M0L1kS613);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1509
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS607, _M0L10r__clampedS616);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS607, _M0L1kS613, _M0L6_2atmpS1509);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS607, _M0L10r__clampedS616, _M0L3tmpS617);
                _M0L6_2atmpS1515 = _M0L1kS613 + 1;
                _M0L1kS613 = _M0L6_2atmpS1515;
                continue;
              }
              break;
            }
            _M0L7_2abindS619 = 0;
            _M0L1kS620 = _M0L7_2abindS619;
            while (1) {
              if (_M0L1kS620 < _M0L7n__dropS611) {
                int32_t _M0L6_2atmpS1517;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1516;
                int32_t _M0L6_2atmpS1518;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1517
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS607, _M0L1kS620);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1516
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS577, _M0L6_2atmpS1517);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1516, _M0L1jS606, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1516);
                _M0L6_2atmpS1518 = _M0L1kS620 + 1;
                _M0L1kS620 = _M0L6_2atmpS1518;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS607);
              }
              break;
            }
            _M0L6_2atmpS1520 = _M0L1jS606 + 1;
            _M0L1jS606 = _M0L6_2atmpS1520;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS604 == 0) {
        int32_t _M0L7_2abindS623 = 0;
        int32_t _M0L1iS624 = _M0L7_2abindS623;
        while (1) {
          if (_M0L1iS624 < _M0L4rowsS578) {
            int32_t _M0L7_2abindS625 = 0;
            int32_t _M0L1jS626 = _M0L7_2abindS625;
            int32_t _M0L6_2atmpS1523;
            while (1) {
              if (_M0L1jS626 < _M0L4colsS582) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1521;
                int32_t _M0L6_2atmpS1522;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1521
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS577, _M0L1iS624);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1521, _M0L1jS626, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1521);
                _M0L6_2atmpS1522 = _M0L1jS626 + 1;
                _M0L1jS626 = _M0L6_2atmpS1522;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1523 = _M0L1iS624 + 1;
            _M0L1iS624 = _M0L6_2atmpS1523;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS1543 = (float)_M0L4colsS582;
      float _M0L6_2atmpS1542 = _M0L6_2atmpS1543 * _M0L1pS601;
      int32_t _M0L7n__keepS629;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS629 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1542);
      if (_M0L7n__keepS629 > 0 && _M0L7n__keepS629 <= _M0L4colsS582) {
        int32_t _M0L7_2abindS630 = 0;
        int32_t _M0L1iS631 = _M0L7_2abindS630;
        while (1) {
          if (_M0L1iS631 < _M0L4rowsS578) {
            int32_t* _M0L6_2atmpS1537 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS632 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS633;
            int32_t _M0L1kS634;
            int32_t _M0L7n__dropS636;
            int32_t _M0L7_2abindS637;
            int32_t _M0L1kS638;
            int32_t _M0L7_2abindS644;
            int32_t _M0L1kS645;
            int32_t _M0L6_2atmpS1538;
            Moonbit_object_header(_M0L9post__idxS632)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L9post__idxS632->$0 = _M0L6_2atmpS1537;
            _M0L9post__idxS632->$1 = 0;
            _M0L7_2abindS633 = 0;
            _M0L1kS634 = _M0L7_2abindS633;
            while (1) {
              if (_M0L1kS634 < _M0L4colsS582) {
                int32_t _M0L6_2atmpS1526;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS632, _M0L1kS634);
                _M0L6_2atmpS1526 = _M0L1kS634 + 1;
                _M0L1kS634 = _M0L6_2atmpS1526;
                continue;
              }
              break;
            }
            _M0L7n__dropS636 = _M0L4colsS582 - _M0L7n__keepS629;
            _M0L7_2abindS637 = 0;
            _M0L1kS638 = _M0L7_2abindS637;
            while (1) {
              if (_M0L1kS638 < _M0L7n__dropS636) {
                float _M0L1uS639;
                float _M0L6_2atmpS1530;
                float _M0L6_2atmpS1532;
                float _M0L6_2atmpS1531;
                float _M0L6_2atmpS1529;
                int32_t _M0L6_2atmpS1528;
                int32_t _M0L6r__idxS640;
                int32_t _M0L10r__clampedS641;
                int32_t _M0L3tmpS642;
                int32_t _M0L6_2atmpS1527;
                int32_t _M0L6_2atmpS1533;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS639 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS591);
                _M0L6_2atmpS1530 = (float)_M0L4colsS582;
                _M0L6_2atmpS1532 = (float)_M0L1kS638;
                _M0L6_2atmpS1531 = _M0L6_2atmpS1532 * _M0L1uS639;
                _M0L6_2atmpS1529 = _M0L6_2atmpS1530 - _M0L6_2atmpS1531;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1528
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1529);
                _M0L6r__idxS640 = _M0L1kS638 + _M0L6_2atmpS1528;
                if (_M0L6r__idxS640 >= _M0L4colsS582) {
                  _M0L10r__clampedS641 = _M0L4colsS582 - 1;
                } else {
                  _M0L10r__clampedS641 = _M0L6r__idxS640;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS642
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS632, _M0L1kS638);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1527
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS632, _M0L10r__clampedS641);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS632, _M0L1kS638, _M0L6_2atmpS1527);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS632, _M0L10r__clampedS641, _M0L3tmpS642);
                _M0L6_2atmpS1533 = _M0L1kS638 + 1;
                _M0L1kS638 = _M0L6_2atmpS1533;
                continue;
              }
              break;
            }
            _M0L7_2abindS644 = 0;
            _M0L1kS645 = _M0L7_2abindS644;
            while (1) {
              if (_M0L1kS645 < _M0L7n__dropS636) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1534;
                int32_t _M0L6_2atmpS1535;
                int32_t _M0L6_2atmpS1536;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1534
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS577, _M0L1iS631);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1535
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS632, _M0L1kS645);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1534, _M0L6_2atmpS1535, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1534);
                _M0L6_2atmpS1536 = _M0L1kS645 + 1;
                _M0L1kS645 = _M0L6_2atmpS1536;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS632);
              }
              break;
            }
            _M0L6_2atmpS1538 = _M0L1iS631 + 1;
            _M0L1iS631 = _M0L6_2atmpS1538;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS629 == 0) {
        int32_t _M0L7_2abindS648 = 0;
        int32_t _M0L1iS649 = _M0L7_2abindS648;
        while (1) {
          if (_M0L1iS649 < _M0L4rowsS578) {
            int32_t _M0L7_2abindS650 = 0;
            int32_t _M0L1jS651 = _M0L7_2abindS650;
            int32_t _M0L6_2atmpS1541;
            while (1) {
              if (_M0L1jS651 < _M0L4colsS582) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1539;
                int32_t _M0L6_2atmpS1540;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1539
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS577, _M0L1iS649);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1539, _M0L1jS651, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1539);
                _M0L6_2atmpS1540 = _M0L1jS651 + 1;
                _M0L1jS651 = _M0L6_2atmpS1540;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1541 = _M0L1iS649 + 1;
            _M0L1iS649 = _M0L6_2atmpS1541;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS1551 = _M0L4rowsS578 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS654 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS1551, 0);
  _M0L6_2atmpS1550 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS655
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS655)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6colptrS655->$0 = _M0L6_2atmpS1550;
  _M0L6colptrS655->$1 = 0;
  _M0L6_2atmpS1549 = moonbit_empty_float_array;
  _M0L4valsS656
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS656)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS656->$0 = _M0L6_2atmpS1549;
  _M0L4valsS656->$1 = 0;
  _M0L7_2abindS657 = 0;
  _M0L1iS658 = _M0L7_2abindS657;
  while (1) {
    if (_M0L1iS658 < _M0L4rowsS578) {
      int32_t _M0L6_2atmpS1544;
      int32_t _M0L7_2abindS659;
      int32_t _M0L1jS660;
      int32_t _M0L6_2atmpS1547;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS1544 = _M0MPC15array5Array6lengthGfE(_M0L4valsS656);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS654, _M0L1iS658, _M0L6_2atmpS1544);
      _M0L7_2abindS659 = 0;
      _M0L1jS660 = _M0L7_2abindS659;
      while (1) {
        if (_M0L1jS660 < _M0L4colsS582) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS1545;
          float _M0L1vS661;
          int32_t _M0L6_2atmpS1546;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS1545
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS577, _M0L1iS658);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS661
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS1545, _M0L1jS660);
          moonbit_decref_cycle_free(_M0L6_2atmpS1545);
          if (_M0L1vS661 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS655, _M0L1jS660);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS656, _M0L1vS661);
          }
          _M0L6_2atmpS1546 = _M0L1jS660 + 1;
          _M0L1jS660 = _M0L6_2atmpS1546;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1547 = _M0L1iS658 + 1;
      _M0L1iS658 = _M0L6_2atmpS1547;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS577);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1548 = _M0MPC15array5Array6lengthGfE(_M0L4valsS656);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS654, _M0L4rowsS578, _M0L6_2atmpS1548);
  _block_2021
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2021)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_2021->$0 = _M0L4rowsS578;
  _block_2021->$1 = _M0L4colsS582;
  _block_2021->$2 = _M0L6rowptrS654;
  _block_2021->$3 = _M0L6colptrS655;
  _block_2021->$4 = _M0L4valsS656;
  return _block_2021;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS576
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS1500;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS1500 = _M0L1mS576->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS1500);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS574
) {
  struct _M0TUmmmmE* _M0L1sS573;
  uint64_t _M0L6_2atmpS1499;
  struct _M0TUmmmmE* _M0L1tS575;
  uint64_t _M0L6_2atmpS1495;
  uint64_t _M0L6_2atmpS1496;
  uint64_t _M0L6_2atmpS1497;
  uint64_t _M0L6_2atmpS1498;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2022;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS573 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS574);
  _M0L6_2atmpS1499 = _M0L1sS573->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS575 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1499);
  _M0L6_2atmpS1495 = _M0L1sS573->$0;
  _M0L6_2atmpS1496 = _M0L1sS573->$1;
  _M0L6_2atmpS1497 = _M0L1sS573->$2;
  moonbit_decref_cycle_free(_M0L1sS573);
  _M0L6_2atmpS1498 = _M0L1tS575->$0;
  moonbit_decref_cycle_free(_M0L1tS575);
  _block_2022
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2022)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2022->$0 = _M0L6_2atmpS1495;
  _block_2022->$1 = _M0L6_2atmpS1496;
  _block_2022->$2 = _M0L6_2atmpS1497;
  _block_2022->$3 = _M0L6_2atmpS1498;
  return _block_2022;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS565) {
  uint64_t _M0L2s1S564;
  uint64_t _M0L2z1S566;
  uint64_t _M0L2s2S567;
  uint64_t _M0L2z2S568;
  uint64_t _M0L2s3S569;
  uint64_t _M0L2z3S570;
  uint64_t _M0L2s4S571;
  uint64_t _M0L2z4S572;
  struct _M0TUmmmmE* _block_2023;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S564 = _M0L4seedS565 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S566 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S564);
  _M0L2s2S567 = _M0L2s1S564 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S568 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S567);
  _M0L2s3S569 = _M0L2s2S567 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S570 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S569);
  _M0L2s4S571 = _M0L2s3S569 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S572 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S571);
  _block_2023 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2023)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2023->$0 = _M0L2z1S566;
  _block_2023->$1 = _M0L2z2S568;
  _block_2023->$2 = _M0L2z3S570;
  _block_2023->$3 = _M0L2z4S572;
  return _block_2023;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS562) {
  uint64_t _M0L6_2atmpS1494;
  uint64_t _M0L6_2atmpS1493;
  uint64_t _M0L1zS561;
  uint64_t _M0L6_2atmpS1492;
  uint64_t _M0L6_2atmpS1491;
  uint64_t _M0L1zS563;
  uint64_t _M0L6_2atmpS1490;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1494 = _M0L1zS562 >> 30;
  _M0L6_2atmpS1493 = _M0L1zS562 ^ _M0L6_2atmpS1494;
  _M0L1zS561 = _M0L6_2atmpS1493 * 13787848793156543929ull;
  _M0L6_2atmpS1492 = _M0L1zS561 >> 27;
  _M0L6_2atmpS1491 = _M0L1zS561 ^ _M0L6_2atmpS1492;
  _M0L1zS563 = _M0L6_2atmpS1491 * 10723151780598845931ull;
  _M0L6_2atmpS1490 = _M0L1zS563 >> 31;
  return _M0L1zS563 ^ _M0L6_2atmpS1490;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS556
) {
  double _M0L2u1S555;
  double _M0L8u1__safeS557;
  double _M0L2u2S558;
  double _M0L6_2atmpS1489;
  double _M0L6_2atmpS1488;
  double _M0L1rS559;
  double _M0L5thetaS560;
  double _M0L6_2atmpS1487;
  double _M0L6_2atmpS1484;
  double _M0L6_2atmpS1486;
  double _M0L6_2atmpS1485;
  struct _M0TUddE* _block_2024;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S555 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS556);
  if (_M0L2u1S555 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS557 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS557 = _M0L2u1S555;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S558 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS556);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1489 = _M0FPC14math2ln(_M0L8u1__safeS557);
  _M0L6_2atmpS1488 = -0x1p+1 * _M0L6_2atmpS1489;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS559 = sqrt(_M0L6_2atmpS1488);
  _M0L5thetaS560 = 0x1.921fb54442d18p+2 * _M0L2u2S558;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1487 = _M0FPC14math3cos(_M0L5thetaS560);
  _M0L6_2atmpS1484 = _M0L1rS559 * _M0L6_2atmpS1487;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1486 = _M0FPC14math3sin(_M0L5thetaS560);
  _M0L6_2atmpS1485 = _M0L1rS559 * _M0L6_2atmpS1486;
  _block_2024 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2024)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2024->$0 = _M0L6_2atmpS1484;
  _block_2024->$1 = _M0L6_2atmpS1485;
  return _block_2024;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS553
) {
  uint64_t _M0L1uS552;
  uint64_t _M0L4bitsS554;
  double _M0L6_2atmpS1483;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS552 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS553);
  _M0L4bitsS554 = _M0L1uS552 >> 11;
  _M0L6_2atmpS1483 = (double)_M0L4bitsS554;
  return _M0L6_2atmpS1483 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS550,
  float _M0L2dtS551
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1475;
  struct _M0TPB5ArrayGfE* _M0L1tS1478;
  float _M0L6_2atmpS1477;
  float _M0L6_2atmpS1476;
  struct _M0TPB5ArrayGiE* _M0L2ttS1479;
  struct _M0TPB5ArrayGiE* _M0L2ttS1482;
  int32_t _M0L6_2atmpS1481;
  int32_t _M0L6_2atmpS1480;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1475 = _M0L1tS550->$0;
  _M0L1tS1478 = _M0L1tS550->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1477 = _M0MPC15array5Array2atGfE(_M0L1tS1478, 0);
  _M0L6_2atmpS1476 = _M0L6_2atmpS1477 + _M0L2dtS551;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS1475, 0, _M0L6_2atmpS1476);
  _M0L2ttS1479 = _M0L1tS550->$1;
  _M0L2ttS1482 = _M0L1tS550->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1481 = _M0MPC15array5Array2atGiE(_M0L2ttS1482, 0);
  _M0L6_2atmpS1480 = _M0L6_2atmpS1481 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS1479, 0, _M0L6_2atmpS1480);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS548,
  float _M0L1vS549
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS548->$2 = _M0L1vS549;
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS547
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1474;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1474 = _M0L1tS547->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS1474, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS1473;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1470;
  int32_t* _M0L6_2atmpS1472;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1471;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2025;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1473 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS1473[0] = 0x0p+0f;
  _M0L6_2atmpS1470
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1470)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1470->$0 = _M0L6_2atmpS1473;
  _M0L6_2atmpS1470->$1 = 1;
  _M0L6_2atmpS1472 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS1472[0] = 0;
  _M0L6_2atmpS1471
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1471)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS1471->$0 = _M0L6_2atmpS1472;
  _M0L6_2atmpS1471->$1 = 1;
  _block_2025
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2025)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 65, 0);
  _block_2025->$0 = _M0L6_2atmpS1470;
  _block_2025->$1 = _M0L6_2atmpS1471;
  _block_2025->$2 = 0x1p-3f;
  return _block_2025;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS545
) {
  uint32_t _M0L1uS544;
  uint32_t _M0L4bitsS546;
  double _M0L6_2atmpS1469;
  double _M0L6_2atmpS1468;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS544 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS545);
  _M0L4bitsS546 = _M0L1uS544 >> 8;
  _M0L6_2atmpS1469 = (double)_M0L4bitsS546;
  _M0L6_2atmpS1468 = _M0L6_2atmpS1469 * 0x1p-24;
  return (float)_M0L6_2atmpS1468;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS543
) {
  uint64_t _M0L1uS542;
  uint64_t _M0L6_2atmpS1467;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS542 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS543);
  _M0L6_2atmpS1467 = _M0L1uS542 >> 32;
  return (uint32_t)_M0L6_2atmpS1467;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS535
) {
  uint64_t _M0L2s0S534;
  uint64_t _M0L2s1S536;
  uint64_t _M0L2s2S537;
  uint64_t _M0L2s3S538;
  uint64_t _M0L3tmpS539;
  uint64_t _M0L6_2atmpS1466;
  uint64_t _M0L3resS540;
  uint64_t _M0L1tS541;
  uint64_t _M0L6_2atmpS1456;
  uint64_t _M0L6_2atmpS1457;
  uint64_t _M0L2s2S1459;
  uint64_t _M0L6_2atmpS1458;
  uint64_t _M0L2s3S1461;
  uint64_t _M0L6_2atmpS1460;
  uint64_t _M0L2s2S1463;
  uint64_t _M0L6_2atmpS1462;
  uint64_t _M0L2s3S1465;
  uint64_t _M0L6_2atmpS1464;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S534 = _M0L1rS535->$0;
  _M0L2s1S536 = _M0L1rS535->$1;
  _M0L2s2S537 = _M0L1rS535->$2;
  _M0L2s3S538 = _M0L1rS535->$3;
  _M0L3tmpS539 = _M0L2s0S534 + _M0L2s3S538;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1466 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS539, 23);
  _M0L3resS540 = _M0L6_2atmpS1466 + _M0L2s0S534;
  _M0L1tS541 = _M0L2s1S536 << 17;
  _M0L6_2atmpS1456 = _M0L2s2S537 ^ _M0L2s0S534;
  _M0L1rS535->$2 = _M0L6_2atmpS1456;
  _M0L6_2atmpS1457 = _M0L2s3S538 ^ _M0L2s1S536;
  _M0L1rS535->$3 = _M0L6_2atmpS1457;
  _M0L2s2S1459 = _M0L1rS535->$2;
  _M0L6_2atmpS1458 = _M0L2s1S536 ^ _M0L2s2S1459;
  _M0L1rS535->$1 = _M0L6_2atmpS1458;
  _M0L2s3S1461 = _M0L1rS535->$3;
  _M0L6_2atmpS1460 = _M0L2s0S534 ^ _M0L2s3S1461;
  _M0L1rS535->$0 = _M0L6_2atmpS1460;
  _M0L2s2S1463 = _M0L1rS535->$2;
  _M0L6_2atmpS1462 = _M0L2s2S1463 ^ _M0L1tS541;
  _M0L1rS535->$2 = _M0L6_2atmpS1462;
  _M0L2s3S1465 = _M0L1rS535->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1464 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1465, 45);
  _M0L1rS535->$3 = _M0L6_2atmpS1464;
  return _M0L3resS540;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS532, int32_t _M0L1kS533) {
  uint64_t _M0L6_2atmpS1453;
  int32_t _M0L6_2atmpS1455;
  uint64_t _M0L6_2atmpS1454;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1453 = _M0L1xS532 << (_M0L1kS533 & 63);
  _M0L6_2atmpS1455 = 64 - _M0L1kS533;
  _M0L6_2atmpS1454 = _M0L1xS532 >> (_M0L6_2atmpS1455 & 63);
  return _M0L6_2atmpS1453 | _M0L6_2atmpS1454;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS525
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS1452;
  int32_t _M0L1nS524;
  struct _M0TPB8MutLocalGiE* _M0L5countS526;
  struct _M0TPB8MutLocalGfE* _M0L4prevS527;
  int32_t _M0L7_2abindS528;
  int32_t _M0L1iS529;
  int32_t _result_2027;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS1452 = _M0L1mS525->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS524 = _M0MPC15array5Array6lengthGfE(_M0L4dataS1452);
  if (_M0L1nS524 == 0) {
    return 0;
  }
  _M0L5countS526
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS526)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS526->$0 = 0;
  _M0L4prevS527
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS527)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS527->$0 = 0x0p+0f;
  _M0L7_2abindS528 = 0;
  _M0L1iS529 = _M0L7_2abindS528;
  while (1) {
    if (_M0L1iS529 < _M0L1nS524) {
      struct _M0TPB5ArrayGfE* _M0L4dataS1450 = _M0L1mS525->$2;
      float _M0L3curS530;
      float _M0L3valS1447;
      int32_t _M0L6_2atmpS1451;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS530 = _M0MPC15array5Array2atGfE(_M0L4dataS1450, _M0L1iS529);
      _M0L3valS1447 = _M0L4prevS527->$0;
      if (_M0L3valS1447 < 0x1p-1f && _M0L3curS530 >= 0x1p-1f) {
        int32_t _M0L3valS1449 = _M0L5countS526->$0;
        int32_t _M0L6_2atmpS1448 = _M0L3valS1449 + 1;
        _M0L5countS526->$0 = _M0L6_2atmpS1448;
      }
      _M0L4prevS527->$0 = _M0L3curS530;
      _M0L6_2atmpS1451 = _M0L1iS529 + 1;
      _M0L1iS529 = _M0L6_2atmpS1451;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS527);
    }
    break;
  }
  _result_2027 = _M0L5countS526->$0;
  moonbit_decref_cycle_free(_M0L5countS526);
  return _result_2027;
}

double _M0FPC14math2ln(double _M0L1xS510) {
  struct _M0TUdiE* _M0L7_2abindS511;
  double _M0L5_2af1S512;
  int32_t _M0L5_2akiS513;
  double _M0L1fS515;
  double _M0L1kS516;
  double _M0L6_2atmpS1440;
  double _M0L1sS517;
  double _M0L2s2S518;
  double _M0L2s4S519;
  double _M0L6_2atmpS1439;
  double _M0L6_2atmpS1438;
  double _M0L6_2atmpS1437;
  double _M0L6_2atmpS1436;
  double _M0L6_2atmpS1435;
  double _M0L6_2atmpS1434;
  double _M0L2t1S520;
  double _M0L6_2atmpS1433;
  double _M0L6_2atmpS1432;
  double _M0L6_2atmpS1431;
  double _M0L6_2atmpS1430;
  double _M0L2t2S521;
  double _M0L1rS522;
  double _M0L6_2atmpS1429;
  double _M0L4hfsqS523;
  double _M0L6_2atmpS1422;
  double _M0L6_2atmpS1428;
  double _M0L6_2atmpS1426;
  double _M0L6_2atmpS1427;
  double _M0L6_2atmpS1425;
  double _M0L6_2atmpS1424;
  double _M0L6_2atmpS1423;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS510 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS510)
      || _M0MPC16double6Double7is__inf(_M0L1xS510)
    ) {
      return _M0L1xS510;
    } else if (_M0L1xS510 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS511 = _M0FPC14math5frexp(_M0L1xS510);
  _M0L5_2af1S512 = _M0L7_2abindS511->$0;
  _M0L5_2akiS513 = _M0L7_2abindS511->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS511);
  if (_M0L5_2af1S512 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1444 = _M0L5_2af1S512 * 0x1p+1;
    double _M0L6_2atmpS1441 = _M0L6_2atmpS1444 - 0x1p+0;
    int32_t _M0L6_2atmpS1443 = _M0L5_2akiS513 - 1;
    double _M0L6_2atmpS1442 = (double)_M0L6_2atmpS1443;
    _M0L1fS515 = _M0L6_2atmpS1441;
    _M0L1kS516 = _M0L6_2atmpS1442;
    goto join_514;
  } else {
    double _M0L6_2atmpS1445 = _M0L5_2af1S512 - 0x1p+0;
    double _M0L6_2atmpS1446 = (double)_M0L5_2akiS513;
    _M0L1fS515 = _M0L6_2atmpS1445;
    _M0L1kS516 = _M0L6_2atmpS1446;
    goto join_514;
  }
  join_514:;
  _M0L6_2atmpS1440 = 0x1p+1 + _M0L1fS515;
  _M0L1sS517 = _M0L1fS515 / _M0L6_2atmpS1440;
  _M0L2s2S518 = _M0L1sS517 * _M0L1sS517;
  _M0L2s4S519 = _M0L2s2S518 * _M0L2s2S518;
  _M0L6_2atmpS1439 = _M0L2s4S519 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1438 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1439;
  _M0L6_2atmpS1437 = _M0L2s4S519 * _M0L6_2atmpS1438;
  _M0L6_2atmpS1436 = 0x1.2492494229359p-2 + _M0L6_2atmpS1437;
  _M0L6_2atmpS1435 = _M0L2s4S519 * _M0L6_2atmpS1436;
  _M0L6_2atmpS1434 = 0x1.5555555555593p-1 + _M0L6_2atmpS1435;
  _M0L2t1S520 = _M0L2s2S518 * _M0L6_2atmpS1434;
  _M0L6_2atmpS1433 = _M0L2s4S519 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1432 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1433;
  _M0L6_2atmpS1431 = _M0L2s4S519 * _M0L6_2atmpS1432;
  _M0L6_2atmpS1430 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1431;
  _M0L2t2S521 = _M0L2s4S519 * _M0L6_2atmpS1430;
  _M0L1rS522 = _M0L2t1S520 + _M0L2t2S521;
  _M0L6_2atmpS1429 = 0x1p-1 * _M0L1fS515;
  _M0L4hfsqS523 = _M0L6_2atmpS1429 * _M0L1fS515;
  _M0L6_2atmpS1422 = _M0L1kS516 * 0x1.62e42feep-1;
  _M0L6_2atmpS1428 = _M0L4hfsqS523 + _M0L1rS522;
  _M0L6_2atmpS1426 = _M0L1sS517 * _M0L6_2atmpS1428;
  _M0L6_2atmpS1427 = _M0L1kS516 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1425 = _M0L6_2atmpS1426 + _M0L6_2atmpS1427;
  _M0L6_2atmpS1424 = _M0L4hfsqS523 - _M0L6_2atmpS1425;
  _M0L6_2atmpS1423 = _M0L6_2atmpS1424 - _M0L1fS515;
  return _M0L6_2atmpS1422 - _M0L6_2atmpS1423;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS503) {
  struct _M0TUdiE* _M0L7_2abindS504;
  double _M0L10_2anorm__fS505;
  int32_t _M0L6_2aexpS506;
  uint64_t _M0L1uS507;
  uint64_t _M0L6_2atmpS1421;
  uint64_t _M0L6_2atmpS1420;
  int32_t _M0L6_2atmpS1419;
  int32_t _M0L6_2atmpS1418;
  int32_t _M0L3expS508;
  uint64_t _M0L6_2atmpS1417;
  uint64_t _M0L6_2atmpS1416;
  uint64_t _M0L6_2atmpS1415;
  double _M0L4fracS509;
  struct _M0TUdiE* _block_2030;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS503 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS503)
    || _M0MPC16double6Double7is__nan(_M0L1fS503)
  ) {
    struct _M0TUdiE* _block_2029 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2029)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2029->$0 = _M0L1fS503;
    _block_2029->$1 = 0;
    return _block_2029;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS504 = _M0FPC14math9normalize(_M0L1fS503);
  _M0L10_2anorm__fS505 = _M0L7_2abindS504->$0;
  _M0L6_2aexpS506 = _M0L7_2abindS504->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS504);
  _M0L1uS507 = *(int64_t*)&_M0L10_2anorm__fS505;
  _M0L6_2atmpS1421 = _M0L1uS507 >> 52;
  _M0L6_2atmpS1420 = _M0L6_2atmpS1421 & 2047ull;
  _M0L6_2atmpS1419 = (int32_t)_M0L6_2atmpS1420;
  _M0L6_2atmpS1418 = _M0L6_2aexpS506 + _M0L6_2atmpS1419;
  _M0L3expS508 = _M0L6_2atmpS1418 - 1022;
  _M0L6_2atmpS1417 = ~9218868437227405312ull;
  _M0L6_2atmpS1416 = _M0L1uS507 & _M0L6_2atmpS1417;
  _M0L6_2atmpS1415 = _M0L6_2atmpS1416 | 4602678819172646912ull;
  _M0L4fracS509 = *(double*)&_M0L6_2atmpS1415;
  _block_2030 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2030)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2030->$0 = _M0L4fracS509;
  _block_2030->$1 = _M0L3expS508;
  return _block_2030;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS502) {
  double _M0L6_2atmpS1412;
  struct _M0TUdiE* _block_2032;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1412 = fabs(_M0L1fS502);
  if (_M0L6_2atmpS1412 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1414 = (double)4503599627370496ll;
    double _M0L6_2atmpS1413 = _M0L1fS502 * _M0L6_2atmpS1414;
    struct _M0TUdiE* _block_2031 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2031)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2031->$0 = _M0L6_2atmpS1413;
    _block_2031->$1 = -52;
    return _block_2031;
  }
  _block_2032 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2032)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2032->$0 = _M0L1fS502;
  _block_2032->$1 = 0;
  return _block_2032;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS501) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS501 != _M0L4selfS501) {
    return 0;
  } else if (_M0L4selfS501 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS501 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS501;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS482,
  float _M0L4elemS484
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS481;
  int32_t _M0L1iS483;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS481 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS482);
  _M0L1iS483 = 0;
  while (1) {
    if (_M0L1iS483 < _M0L3lenS482) {
      float* _M0L3bufS1404 = _M0L3arrS481->$0;
      int32_t _M0L6_2atmpS1405;
      _M0L3bufS1404[_M0L1iS483] = _M0L4elemS484;
      _M0L6_2atmpS1405 = _M0L1iS483 + 1;
      _M0L1iS483 = _M0L6_2atmpS1405;
      continue;
    }
    break;
  }
  return _M0L3arrS481;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS487,
  int32_t _M0L4elemS489
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS486;
  int32_t _M0L1iS488;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS486 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS487);
  _M0L1iS488 = 0;
  while (1) {
    if (_M0L1iS488 < _M0L3lenS487) {
      uint8_t* _M0L3bufS1406 = _M0L3arrS486->$0;
      int32_t _M0L6_2atmpS1407;
      _M0L3bufS1406[_M0L1iS488] = _M0L4elemS489;
      _M0L6_2atmpS1407 = _M0L1iS488 + 1;
      _M0L1iS488 = _M0L6_2atmpS1407;
      continue;
    }
    break;
  }
  return _M0L3arrS486;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS492,
  int32_t _M0L4elemS494
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS491;
  int32_t _M0L1iS493;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS491 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS492);
  _M0L1iS493 = 0;
  while (1) {
    if (_M0L1iS493 < _M0L3lenS492) {
      int32_t* _M0L3bufS1408 = _M0L3arrS491->$0;
      int32_t _M0L6_2atmpS1409;
      _M0L3bufS1408[_M0L1iS493] = _M0L4elemS494;
      _M0L6_2atmpS1409 = _M0L1iS493 + 1;
      _M0L1iS493 = _M0L6_2atmpS1409;
      continue;
    }
    break;
  }
  return _M0L3arrS491;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS497,
  struct _M0TPB5ArrayGfE* _M0L4elemS499
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS496;
  int32_t _M0L1iS498;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS496
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS497);
  _M0L1iS498 = 0;
  while (1) {
    if (_M0L1iS498 < _M0L3lenS497) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1410 = _M0L3arrS496->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS1922 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1410[_M0L1iS498];
      int32_t _M0L6_2atmpS1411;
      moonbit_incref_cycle_free(_M0L4elemS499);
      if (_M0L6_2aoldS1922) {
        moonbit_decref_cycle_free(_M0L6_2aoldS1922);
      }
      _M0L3bufS1410[_M0L1iS498] = _M0L4elemS499;
      _M0L6_2atmpS1411 = _M0L1iS498 + 1;
      _M0L1iS498 = _M0L6_2atmpS1411;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS499);
    }
    break;
  }
  return _M0L3arrS496;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS466,
  int32_t _M0L5indexS467,
  float _M0L5valueS468
) {
  int32_t _M0L3lenS465;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS465 = _M0L4selfS466->$1;
  if (_M0L5indexS467 >= 0 && _M0L5indexS467 < _M0L3lenS465) {
    float* _M0L6_2atmpS1400;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1400 = _M0MPC15array5Array6bufferGfE(_M0L4selfS466);
    _M0L6_2atmpS1400[_M0L5indexS467] = _M0L5valueS468;
    moonbit_decref_cycle_free(_M0L6_2atmpS1400);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS470,
  int32_t _M0L5indexS471,
  int32_t _M0L5valueS472
) {
  int32_t _M0L3lenS469;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS469 = _M0L4selfS470->$1;
  if (_M0L5indexS471 >= 0 && _M0L5indexS471 < _M0L3lenS469) {
    int32_t* _M0L6_2atmpS1401;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1401 = _M0MPC15array5Array6bufferGiE(_M0L4selfS470);
    _M0L6_2atmpS1401[_M0L5indexS471] = _M0L5valueS472;
    moonbit_decref_cycle_free(_M0L6_2atmpS1401);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS474,
  int32_t _M0L5indexS475,
  int32_t _M0L5valueS476
) {
  int32_t _M0L3lenS473;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS473 = _M0L4selfS474->$1;
  if (_M0L5indexS475 >= 0 && _M0L5indexS475 < _M0L3lenS473) {
    uint8_t* _M0L6_2atmpS1402;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1402 = _M0MPC15array5Array6bufferGbE(_M0L4selfS474);
    _M0L6_2atmpS1402[_M0L5indexS475] = _M0L5valueS476;
    moonbit_decref_cycle_free(_M0L6_2atmpS1402);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS478,
  int32_t _M0L5indexS479,
  struct _M0TPB5ArrayGfE* _M0L5valueS480
) {
  int32_t _M0L3lenS477;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS477 = _M0L4selfS478->$1;
  if (_M0L5indexS479 >= 0 && _M0L5indexS479 < _M0L3lenS477) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1403;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS1923;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1403
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS478);
    _M0L6_2aoldS1923
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1403[_M0L5indexS479];
    if (_M0L6_2aoldS1923) {
      moonbit_decref_cycle_free(_M0L6_2aoldS1923);
    }
    _M0L6_2atmpS1403[_M0L5indexS479] = _M0L5valueS480;
    moonbit_decref_cycle_free(_M0L6_2atmpS1403);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS480);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS458) {
  int32_t _M0L3lenS457;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS457 = _M0L4selfS458->$1;
  if (_M0L3lenS457 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS459 = _M0L3lenS457 - 1;
    float* _M0L3bufS1398 = _M0L4selfS458->$0;
    float _M0L1vS460 = (float)_M0L3bufS1398[_M0L5indexS459];
    void* _block_2037;
    _M0L4selfS458->$1 = _M0L5indexS459;
    _block_2037
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_2037)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_2037)->$0 = _M0L1vS460;
    return _block_2037;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS462) {
  int32_t _M0L3lenS461;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS461 = _M0L4selfS462->$1;
  if (_M0L3lenS461 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS463 = _M0L3lenS461 - 1;
    int32_t* _M0L3bufS1399 = _M0L4selfS462->$0;
    int32_t _M0L1vS464 = (int32_t)_M0L3bufS1399[_M0L5indexS463];
    _M0L4selfS462->$1 = _M0L5indexS463;
    return (int64_t)_M0L1vS464;
  }
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS440,
  int32_t _M0L5indexS441
) {
  int32_t _M0L3lenS439;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS439 = _M0L4selfS440->$1;
  if (_M0L5indexS441 >= 0 && _M0L5indexS441 < _M0L3lenS439) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS1392;
    struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS1924;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1392
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(_M0L4selfS440);
    _M0L6_2atmpS1924
    = (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L6_2atmpS1392[
        _M0L5indexS441
      ];
    if (_M0L6_2atmpS1924) {
      moonbit_incref_cycle_free(_M0L6_2atmpS1924);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1392);
    return _M0L6_2atmpS1924;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS443,
  int32_t _M0L5indexS444
) {
  int32_t _M0L3lenS442;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS442 = _M0L4selfS443->$1;
  if (_M0L5indexS444 >= 0 && _M0L5indexS444 < _M0L3lenS442) {
    float* _M0L6_2atmpS1393;
    float _result_2038;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1393 = _M0MPC15array5Array6bufferGfE(_M0L4selfS443);
    _result_2038 = (float)_M0L6_2atmpS1393[_M0L5indexS444];
    moonbit_decref_cycle_free(_M0L6_2atmpS1393);
    return _result_2038;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS446,
  int32_t _M0L5indexS447
) {
  int32_t _M0L3lenS445;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS445 = _M0L4selfS446->$1;
  if (_M0L5indexS447 >= 0 && _M0L5indexS447 < _M0L3lenS445) {
    moonbit_string_t* _M0L6_2atmpS1394;
    moonbit_string_t _M0L6_2atmpS1925;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1394 = _M0MPC15array5Array6bufferGsE(_M0L4selfS446);
    _M0L6_2atmpS1925 = (moonbit_string_t)_M0L6_2atmpS1394[_M0L5indexS447];
    moonbit_incref_cycle_free(_M0L6_2atmpS1925);
    moonbit_decref_cycle_free(_M0L6_2atmpS1394);
    return _M0L6_2atmpS1925;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS449,
  int32_t _M0L5indexS450
) {
  int32_t _M0L3lenS448;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS448 = _M0L4selfS449->$1;
  if (_M0L5indexS450 >= 0 && _M0L5indexS450 < _M0L3lenS448) {
    uint8_t* _M0L6_2atmpS1395;
    int32_t _result_2039;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1395 = _M0MPC15array5Array6bufferGbE(_M0L4selfS449);
    _result_2039 = (int32_t)_M0L6_2atmpS1395[_M0L5indexS450];
    moonbit_decref_cycle_free(_M0L6_2atmpS1395);
    return _result_2039;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS452,
  int32_t _M0L5indexS453
) {
  int32_t _M0L3lenS451;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS451 = _M0L4selfS452->$1;
  if (_M0L5indexS453 >= 0 && _M0L5indexS453 < _M0L3lenS451) {
    int32_t* _M0L6_2atmpS1396;
    int32_t _result_2040;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1396 = _M0MPC15array5Array6bufferGiE(_M0L4selfS452);
    _result_2040 = (int32_t)_M0L6_2atmpS1396[_M0L5indexS453];
    moonbit_decref_cycle_free(_M0L6_2atmpS1396);
    return _result_2040;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS455,
  int32_t _M0L5indexS456
) {
  int32_t _M0L3lenS454;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS454 = _M0L4selfS455->$1;
  if (_M0L5indexS456 >= 0 && _M0L5indexS456 < _M0L3lenS454) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1397;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS1926;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1397
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS455);
    _M0L6_2atmpS1926
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1397[_M0L5indexS456];
    if (_M0L6_2atmpS1926) {
      moonbit_incref_cycle_free(_M0L6_2atmpS1926);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1397);
    return _M0L6_2atmpS1926;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS438) {
  moonbit_string_t _M0L6_2atmpS1391;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1391 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS438);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1391);
  moonbit_decref_cycle_free(_M0L6_2atmpS1391);
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
  float* _M0L6_2atmpS1387;
  struct _M0TPB5ArrayGfE* _block_2041;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1387 = (float*)moonbit_make_float_array_raw(_M0L3lenS432);
  _block_2041
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2041)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2041->$0 = _M0L6_2atmpS1387;
  _block_2041->$1 = _M0L3lenS432;
  return _block_2041;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS433
) {
  uint8_t* _M0L6_2atmpS1388;
  struct _M0TPB5ArrayGbE* _block_2042;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1388 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS433);
  _block_2042
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2042)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _block_2042->$0 = _M0L6_2atmpS1388;
  _block_2042->$1 = _M0L3lenS433;
  return _block_2042;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS434
) {
  int32_t* _M0L6_2atmpS1389;
  struct _M0TPB5ArrayGiE* _block_2043;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1389 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS434);
  _block_2043
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2043)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2043->$0 = _M0L6_2atmpS1389;
  _block_2043->$1 = _M0L3lenS434;
  return _block_2043;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS435
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1390;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2044;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1390
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS435, 0);
  _block_2044
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2044)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 72, 0);
  _block_2044->$0 = _M0L6_2atmpS1390;
  _block_2044->$1 = _M0L3lenS435;
  return _block_2044;
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS431) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS431, 10);
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS419,
  moonbit_string_t _M0L5valueS421
) {
  int32_t _M0L3lenS1359;
  moonbit_string_t* _M0L6_2atmpS1361;
  int32_t _M0L6_2atmpS1360;
  int32_t _M0L6lengthS420;
  moonbit_string_t* _M0L3bufS1364;
  moonbit_string_t _M0L6_2aoldS1927;
  int32_t _M0L6_2atmpS1365;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1359 = _M0L4selfS419->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1361 = _M0MPC15array5Array6bufferGsE(_M0L4selfS419);
  _M0L6_2atmpS1360 = Moonbit_array_length(_M0L6_2atmpS1361);
  moonbit_decref_cycle_free(_M0L6_2atmpS1361);
  if (_M0L3lenS1359 == _M0L6_2atmpS1360) {
    int32_t _M0L3lenS1363 = _M0L4selfS419->$1;
    int32_t _M0L6_2atmpS1362 = _M0L3lenS1363 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS419, _M0L6_2atmpS1362);
  }
  _M0L6lengthS420 = _M0L4selfS419->$1;
  _M0L3bufS1364 = _M0L4selfS419->$0;
  _M0L6_2aoldS1927 = (moonbit_string_t)_M0L3bufS1364[_M0L6lengthS420];
  moonbit_decref_cycle_free(_M0L6_2aoldS1927);
  _M0L3bufS1364[_M0L6lengthS420] = _M0L5valueS421;
  _M0L6_2atmpS1365 = _M0L6lengthS420 + 1;
  _M0L4selfS419->$1 = _M0L6_2atmpS1365;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS422,
  struct _M0TUsiE* _M0L5valueS424
) {
  int32_t _M0L3lenS1366;
  struct _M0TUsiE** _M0L6_2atmpS1368;
  int32_t _M0L6_2atmpS1367;
  int32_t _M0L6lengthS423;
  struct _M0TUsiE** _M0L3bufS1371;
  struct _M0TUsiE* _M0L6_2aoldS1928;
  int32_t _M0L6_2atmpS1372;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1366 = _M0L4selfS422->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1368 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS422);
  _M0L6_2atmpS1367 = Moonbit_array_length(_M0L6_2atmpS1368);
  moonbit_decref_cycle_free(_M0L6_2atmpS1368);
  if (_M0L3lenS1366 == _M0L6_2atmpS1367) {
    int32_t _M0L3lenS1370 = _M0L4selfS422->$1;
    int32_t _M0L6_2atmpS1369 = _M0L3lenS1370 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS422, _M0L6_2atmpS1369);
  }
  _M0L6lengthS423 = _M0L4selfS422->$1;
  _M0L3bufS1371 = _M0L4selfS422->$0;
  _M0L6_2aoldS1928 = (struct _M0TUsiE*)_M0L3bufS1371[_M0L6lengthS423];
  if (_M0L6_2aoldS1928) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1928);
  }
  _M0L3bufS1371[_M0L6lengthS423] = _M0L5valueS424;
  _M0L6_2atmpS1372 = _M0L6lengthS423 + 1;
  _M0L4selfS422->$1 = _M0L6_2atmpS1372;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS425,
  float _M0L5valueS427
) {
  int32_t _M0L3lenS1373;
  float* _M0L6_2atmpS1375;
  int32_t _M0L6_2atmpS1374;
  int32_t _M0L6lengthS426;
  float* _M0L3bufS1378;
  int32_t _M0L6_2atmpS1379;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1373 = _M0L4selfS425->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1375 = _M0MPC15array5Array6bufferGfE(_M0L4selfS425);
  _M0L6_2atmpS1374 = Moonbit_array_length(_M0L6_2atmpS1375);
  moonbit_decref_cycle_free(_M0L6_2atmpS1375);
  if (_M0L3lenS1373 == _M0L6_2atmpS1374) {
    int32_t _M0L3lenS1377 = _M0L4selfS425->$1;
    int32_t _M0L6_2atmpS1376 = _M0L3lenS1377 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS425, _M0L6_2atmpS1376);
  }
  _M0L6lengthS426 = _M0L4selfS425->$1;
  _M0L3bufS1378 = _M0L4selfS425->$0;
  _M0L3bufS1378[_M0L6lengthS426] = _M0L5valueS427;
  _M0L6_2atmpS1379 = _M0L6lengthS426 + 1;
  _M0L4selfS425->$1 = _M0L6_2atmpS1379;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS428,
  int32_t _M0L5valueS430
) {
  int32_t _M0L3lenS1380;
  int32_t* _M0L6_2atmpS1382;
  int32_t _M0L6_2atmpS1381;
  int32_t _M0L6lengthS429;
  int32_t* _M0L3bufS1385;
  int32_t _M0L6_2atmpS1386;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1380 = _M0L4selfS428->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1382 = _M0MPC15array5Array6bufferGiE(_M0L4selfS428);
  _M0L6_2atmpS1381 = Moonbit_array_length(_M0L6_2atmpS1382);
  moonbit_decref_cycle_free(_M0L6_2atmpS1382);
  if (_M0L3lenS1380 == _M0L6_2atmpS1381) {
    int32_t _M0L3lenS1384 = _M0L4selfS428->$1;
    int32_t _M0L6_2atmpS1383 = _M0L3lenS1384 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS428, _M0L6_2atmpS1383);
  }
  _M0L6lengthS429 = _M0L4selfS428->$1;
  _M0L3bufS1385 = _M0L4selfS428->$0;
  _M0L3bufS1385[_M0L6lengthS429] = _M0L5valueS430;
  _M0L6_2atmpS1386 = _M0L6lengthS429 + 1;
  _M0L4selfS428->$1 = _M0L6_2atmpS1386;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS404,
  int32_t _M0L8requiredS406
) {
  int32_t _M0L8old__capS403;
  int32_t _M0L3lenS1355;
  int32_t _M0L8new__capS405;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS403 = _M0MPC15array5Array8capacityGsE(_M0L4selfS404);
  _M0L3lenS1355 = _M0L4selfS404->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS405
  = _M0FPB23array__growth__capacity(_M0L8old__capS403, _M0L3lenS1355, _M0L8requiredS406);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS404, _M0L8new__capS405);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS408,
  int32_t _M0L8requiredS410
) {
  int32_t _M0L8old__capS407;
  int32_t _M0L3lenS1356;
  int32_t _M0L8new__capS409;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS407 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS408);
  _M0L3lenS1356 = _M0L4selfS408->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS409
  = _M0FPB23array__growth__capacity(_M0L8old__capS407, _M0L3lenS1356, _M0L8requiredS410);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS408, _M0L8new__capS409);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS412,
  int32_t _M0L8requiredS414
) {
  int32_t _M0L8old__capS411;
  int32_t _M0L3lenS1357;
  int32_t _M0L8new__capS413;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS411 = _M0MPC15array5Array8capacityGfE(_M0L4selfS412);
  _M0L3lenS1357 = _M0L4selfS412->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS413
  = _M0FPB23array__growth__capacity(_M0L8old__capS411, _M0L3lenS1357, _M0L8requiredS414);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS412, _M0L8new__capS413);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS416,
  int32_t _M0L8requiredS418
) {
  int32_t _M0L8old__capS415;
  int32_t _M0L3lenS1358;
  int32_t _M0L8new__capS417;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS415 = _M0MPC15array5Array8capacityGiE(_M0L4selfS416);
  _M0L3lenS1358 = _M0L4selfS416->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS417
  = _M0FPB23array__growth__capacity(_M0L8old__capS415, _M0L3lenS1358, _M0L8requiredS418);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS416, _M0L8new__capS417);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS380,
  int32_t _M0L13new__capacityS383
) {
  moonbit_string_t* _M0L8old__bufS379;
  int32_t _M0L3lenS381;
  int32_t _M0L9copy__lenS382;
  moonbit_string_t* _M0L8new__bufS384;
  moonbit_string_t* _M0L6_2aoldS1929;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS379, _M0L13new__capacityS383, _M0L9copy__lenS382, 0, 0);
  _M0L6_2aoldS1929 = _M0L4selfS380->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1929);
  _M0L4selfS380->$0 = _M0L8new__bufS384;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS386,
  int32_t _M0L13new__capacityS389
) {
  struct _M0TUsiE** _M0L8old__bufS385;
  int32_t _M0L3lenS387;
  int32_t _M0L9copy__lenS388;
  struct _M0TUsiE** _M0L8new__bufS390;
  struct _M0TUsiE** _M0L6_2aoldS1930;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS385, _M0L13new__capacityS389, _M0L9copy__lenS388, 0, 0);
  _M0L6_2aoldS1930 = _M0L4selfS386->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1930);
  _M0L4selfS386->$0 = _M0L8new__bufS390;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS392,
  int32_t _M0L13new__capacityS395
) {
  float* _M0L8old__bufS391;
  int32_t _M0L3lenS393;
  int32_t _M0L9copy__lenS394;
  float* _M0L8new__bufS396;
  float* _M0L6_2aoldS1931;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS391, _M0L13new__capacityS395, _M0L9copy__lenS394, 0, 0);
  _M0L6_2aoldS1931 = _M0L4selfS392->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1931);
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
  int32_t* _M0L6_2aoldS1932;
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
  _M0L6_2aoldS1932 = _M0L4selfS398->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1932);
  _M0L4selfS398->$0 = _M0L8new__bufS402;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS375
) {
  moonbit_string_t* _M0L6_2atmpS1351;
  int32_t _result_2045;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1351 = _M0MPC15array5Array6bufferGsE(_M0L4selfS375);
  _result_2045 = Moonbit_array_length(_M0L6_2atmpS1351);
  moonbit_decref_cycle_free(_M0L6_2atmpS1351);
  return _result_2045;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS376
) {
  struct _M0TUsiE** _M0L6_2atmpS1352;
  int32_t _result_2046;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1352 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS376);
  _result_2046 = Moonbit_array_length(_M0L6_2atmpS1352);
  moonbit_decref_cycle_free(_M0L6_2atmpS1352);
  return _result_2046;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS377
) {
  float* _M0L6_2atmpS1353;
  int32_t _result_2047;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1353 = _M0MPC15array5Array6bufferGfE(_M0L4selfS377);
  _result_2047 = Moonbit_array_length(_M0L6_2atmpS1353);
  moonbit_decref_cycle_free(_M0L6_2atmpS1353);
  return _result_2047;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS378
) {
  int32_t* _M0L6_2atmpS1354;
  int32_t _result_2048;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1354 = _M0MPC15array5Array6bufferGiE(_M0L4selfS378);
  _result_2048 = Moonbit_array_length(_M0L6_2atmpS1354);
  moonbit_decref_cycle_free(_M0L6_2atmpS1354);
  return _result_2048;
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
  float* _M0L8_2afieldS1933;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1933 = _M0L4selfS359->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1933);
  return _M0L8_2afieldS1933;
}

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS360
) {
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L8_2afieldS1934;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1934 = _M0L4selfS360->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1934);
  return _M0L8_2afieldS1934;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS361
) {
  moonbit_string_t* _M0L8_2afieldS1935;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1935 = _M0L4selfS361->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1935);
  return _M0L8_2afieldS1935;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS362
) {
  struct _M0TUsiE** _M0L8_2afieldS1936;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1936 = _M0L4selfS362->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1936);
  return _M0L8_2afieldS1936;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS363) {
  uint8_t* _M0L8_2afieldS1937;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1937 = _M0L4selfS363->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1937);
  return _M0L8_2afieldS1937;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS364) {
  int32_t* _M0L8_2afieldS1938;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1938 = _M0L4selfS364->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1938);
  return _M0L8_2afieldS1938;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS365
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS1939;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1939 = _M0L4selfS365->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1939);
  return _M0L8_2afieldS1939;
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
  int32_t _M0L3endS1349;
  int32_t _M0L5startS1350;
  int32_t _M0L8str__lenS354;
  int32_t _M0L3lenS1348;
  int32_t _M0L8requiredS356;
  uint16_t* _M0L4dataS1341;
  int32_t _M0L6_2atmpS1340;
  int32_t _if__result_2050;
  uint16_t* _M0L4dataS1342;
  int32_t _M0L3lenS1343;
  moonbit_string_t _M0L6_2atmpS1344;
  int32_t _M0L6_2atmpS1345;
  int32_t _M0L3lenS1347;
  int32_t _M0L6_2atmpS1346;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1349 = _M0L3strS355.$2;
  _M0L5startS1350 = _M0L3strS355.$1;
  _M0L8str__lenS354 = _M0L3endS1349 - _M0L5startS1350;
  if (_M0L8str__lenS354 == 0) {
    return 0;
  }
  _M0L3lenS1348 = _M0L4selfS357->$1;
  _M0L8requiredS356 = _M0L3lenS1348 + _M0L8str__lenS354;
  _M0L4dataS1341 = _M0L4selfS357->$0;
  _M0L6_2atmpS1340 = Moonbit_array_length(_M0L4dataS1341);
  if (_M0L8requiredS356 > _M0L6_2atmpS1340) {
    _if__result_2050 = 1;
  } else {
    int32_t _M0L3lenS1339 = _M0L4selfS357->$1;
    _if__result_2050 = _M0L8requiredS356 < _M0L3lenS1339;
  }
  if (_if__result_2050) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS357, _M0L8requiredS356);
  }
  _M0L4dataS1342 = _M0L4selfS357->$0;
  _M0L3lenS1343 = _M0L4selfS357->$1;
  moonbit_incref_cycle_free(_M0L4dataS1342);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1344 = _M0MPC16string10StringView4data(_M0L3strS355);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1345 = _M0MPC16string10StringView13start__offset(_M0L3strS355);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1342, _M0L3lenS1343, _M0L6_2atmpS1344, _M0L6_2atmpS1345, _M0L8str__lenS354);
  moonbit_decref_cycle_free(_M0L4dataS1342);
  moonbit_decref_cycle_free(_M0L6_2atmpS1344);
  _M0L3lenS1347 = _M0L4selfS357->$1;
  _M0L6_2atmpS1346 = _M0L3lenS1347 + _M0L8str__lenS354;
  _M0L4selfS357->$1 = _M0L6_2atmpS1346;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS351,
  int32_t _M0L5startS349,
  int32_t _M0L3endS350
) {
  int32_t _if__result_2051;
  int32_t _M0L3lenS352;
  int32_t _M0L6_2atmpS1338;
  moonbit_bytes_t _M0L5bytesS353;
  moonbit_bytes_t _M0L6_2atmpS1337;
  moonbit_string_t _result_2052;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS349 == 0) {
    int32_t _M0L6_2atmpS1336 = Moonbit_array_length(_M0L3strS351);
    _if__result_2051 = _M0L3endS350 == _M0L6_2atmpS1336;
  } else {
    _if__result_2051 = 0;
  }
  if (_if__result_2051) {
    moonbit_incref_cycle_free(_M0L3strS351);
    return _M0L3strS351;
  }
  _M0L3lenS352 = _M0L3endS350 - _M0L5startS349;
  _M0L6_2atmpS1338 = _M0L3lenS352 * 2;
  _M0L5bytesS353 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1338, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS353, 0, _M0L3strS351, _M0L5startS349, _M0L3lenS352);
  _M0L6_2atmpS1337 = _M0L5bytesS353;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2052
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1337, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1337);
  return _result_2052;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS344,
  int32_t _M0L6offsetS348,
  int64_t _M0L6lengthS346
) {
  int32_t _M0L3lenS343;
  int32_t _M0L6lengthS345;
  int32_t _if__result_2053;
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
      int32_t _M0L6_2atmpS1335 = _M0L6offsetS348 + _M0L6lengthS345;
      _if__result_2053 = _M0L6_2atmpS1335 <= _M0L3lenS343;
    } else {
      _if__result_2053 = 0;
    }
  } else {
    _if__result_2053 = 0;
  }
  if (_if__result_2053) {
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
  int32_t _M0L6_2atmpS1334;
  int32_t _M0L6_2atmpS1333;
  int32_t _M0L2e1S329;
  int32_t _M0L6_2atmpS1332;
  int32_t _M0L2e2S332;
  int32_t _M0L4len1S334;
  int32_t _M0L4len2S336;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1334 = _M0L6lengthS331 * 2;
  _M0L6_2atmpS1333 = _M0L13bytes__offsetS330 + _M0L6_2atmpS1334;
  _M0L2e1S329 = _M0L6_2atmpS1333 - 1;
  _M0L6_2atmpS1332 = _M0L11str__offsetS333 + _M0L6lengthS331;
  _M0L2e2S332 = _M0L6_2atmpS1332 - 1;
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
        int32_t _M0L6_2atmpS1329 = _M0L3strS337[_M0L1iS339];
        int32_t _M0L6_2atmpS1328 = (int32_t)_M0L6_2atmpS1329;
        uint32_t _M0L1cS341 = *(uint32_t*)&_M0L6_2atmpS1328;
        uint32_t _M0L6_2atmpS1324 = _M0L1cS341 & 255u;
        int32_t _M0L6_2atmpS1323;
        int32_t _M0L6_2atmpS1325;
        uint32_t _M0L6_2atmpS1327;
        int32_t _M0L6_2atmpS1326;
        int32_t _M0L6_2atmpS1330;
        int32_t _M0L6_2atmpS1331;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1323 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1324);
        if (
          _M0L1jS340 < 0 || _M0L1jS340 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L1jS340] = _M0L6_2atmpS1323;
        _M0L6_2atmpS1325 = _M0L1jS340 + 1;
        _M0L6_2atmpS1327 = _M0L1cS341 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1326 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1327);
        if (
          _M0L6_2atmpS1325 < 0
          || _M0L6_2atmpS1325 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L6_2atmpS1325] = _M0L6_2atmpS1326;
        _M0L6_2atmpS1330 = _M0L1iS339 + 1;
        _M0L6_2atmpS1331 = _M0L1jS340 + 2;
        _M0L1iS339 = _M0L6_2atmpS1330;
        _M0L1jS340 = _M0L6_2atmpS1331;
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
  int32_t _M0L6_2atmpS1322;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1322 = *(int32_t*)&_M0L4selfS328;
  return _M0L6_2atmpS1322 & 0xff;
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
    int32_t _M0L6_2atmpS1321 = -_M0L4selfS312;
    _M0L3numS314 = *(uint32_t*)&_M0L6_2atmpS1321;
  } else {
    _M0L3numS314 = *(uint32_t*)&_M0L4selfS312;
  }
  switch (_M0L5radixS311) {
    case 10: {
      int32_t _M0L10digit__lenS316;
      int32_t _M0L6_2atmpS1318;
      int32_t _M0L10total__lenS317;
      uint16_t* _M0L6bufferS318;
      int32_t _M0L12digit__startS319;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS316 = _M0FPB12dec__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1318 = 1;
      } else {
        _M0L6_2atmpS1318 = 0;
      }
      _M0L10total__lenS317 = _M0L10digit__lenS316 + _M0L6_2atmpS1318;
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
      int32_t _M0L6_2atmpS1319;
      int32_t _M0L10total__lenS321;
      uint16_t* _M0L6bufferS322;
      int32_t _M0L12digit__startS323;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS320 = _M0FPB12hex__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1319 = 1;
      } else {
        _M0L6_2atmpS1319 = 0;
      }
      _M0L10total__lenS321 = _M0L10digit__lenS320 + _M0L6_2atmpS1319;
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
      int32_t _M0L6_2atmpS1320;
      int32_t _M0L10total__lenS325;
      uint16_t* _M0L6bufferS326;
      int32_t _M0L12digit__startS327;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS324
      = _M0FPB14radix__count32(_M0L3numS314, _M0L5radixS311);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1320 = 1;
      } else {
        _M0L6_2atmpS1320 = 0;
      }
      _M0L10total__lenS325 = _M0L10digit__lenS324 + _M0L6_2atmpS1320;
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
      uint32_t _M0L6_2atmpS1316 = _M0L3numS308 / _M0L4baseS306;
      int32_t _M0L6_2atmpS1317 = _M0L5countS309 + 1;
      _M0L3numS308 = _M0L6_2atmpS1316;
      _M0L5countS309 = _M0L6_2atmpS1317;
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
    int32_t _M0L6_2atmpS1315;
    int32_t _M0L6_2atmpS1314;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS304 = moonbit_clz32(_M0L5valueS303);
    _M0L6_2atmpS1315 = 31 - _M0L14leading__zerosS304;
    _M0L6_2atmpS1314 = _M0L6_2atmpS1315 / 4;
    return _M0L6_2atmpS1314 + 1;
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
  int32_t _M0L6_2atmpS1313;
  uint32_t _M0L3numS278;
  int32_t _M0L6offsetS279;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1313 = _M0L10total__lenS301 - _M0L12digit__startS289;
  _M0L3numS278 = _M0L3numS300;
  _M0L6offsetS279 = _M0L6_2atmpS1313;
  while (1) {
    if (_M0L3numS278 >= 10000u) {
      uint32_t _M0L1tS280 = _M0L3numS278 / 10000u;
      uint32_t _M0L6_2atmpS1290 = _M0L3numS278 % 10000u;
      int32_t _M0L1rS281 = *(int32_t*)&_M0L6_2atmpS1290;
      int32_t _M0L2d1S282 = _M0L1rS281 / 100;
      int32_t _M0L2d2S283 = _M0L1rS281 % 100;
      int32_t _M0L6_2atmpS1289 = _M0L2d1S282 / 10;
      int32_t _M0L6_2atmpS1288 = 48 + _M0L6_2atmpS1289;
      int32_t _M0L6d1__hiS284 = (uint16_t)_M0L6_2atmpS1288;
      int32_t _M0L6_2atmpS1287 = _M0L2d1S282 % 10;
      int32_t _M0L6_2atmpS1286 = 48 + _M0L6_2atmpS1287;
      int32_t _M0L6d1__loS285 = (uint16_t)_M0L6_2atmpS1286;
      int32_t _M0L6_2atmpS1285 = _M0L2d2S283 / 10;
      int32_t _M0L6_2atmpS1284 = 48 + _M0L6_2atmpS1285;
      int32_t _M0L6d2__hiS286 = (uint16_t)_M0L6_2atmpS1284;
      int32_t _M0L6_2atmpS1283 = _M0L2d2S283 % 10;
      int32_t _M0L6_2atmpS1282 = 48 + _M0L6_2atmpS1283;
      int32_t _M0L6d2__loS287 = (uint16_t)_M0L6_2atmpS1282;
      int32_t _M0L6_2atmpS1274 = _M0L12digit__startS289 + _M0L6offsetS279;
      int32_t _M0L6_2atmpS1273 = _M0L6_2atmpS1274 - 4;
      int32_t _M0L6_2atmpS1276;
      int32_t _M0L6_2atmpS1275;
      int32_t _M0L6_2atmpS1278;
      int32_t _M0L6_2atmpS1277;
      int32_t _M0L6_2atmpS1280;
      int32_t _M0L6_2atmpS1279;
      int32_t _M0L6_2atmpS1281;
      _M0L6bufferS288[_M0L6_2atmpS1273] = _M0L6d1__hiS284;
      _M0L6_2atmpS1276 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1275 = _M0L6_2atmpS1276 - 3;
      _M0L6bufferS288[_M0L6_2atmpS1275] = _M0L6d1__loS285;
      _M0L6_2atmpS1278 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1277 = _M0L6_2atmpS1278 - 2;
      _M0L6bufferS288[_M0L6_2atmpS1277] = _M0L6d2__hiS286;
      _M0L6_2atmpS1280 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1279 = _M0L6_2atmpS1280 - 1;
      _M0L6bufferS288[_M0L6_2atmpS1279] = _M0L6d2__loS287;
      _M0L6_2atmpS1281 = _M0L6offsetS279 - 4;
      _M0L3numS278 = _M0L1tS280;
      _M0L6offsetS279 = _M0L6_2atmpS1281;
      continue;
    } else {
      int32_t _M0L6_2atmpS1312 = *(int32_t*)&_M0L3numS278;
      int32_t _M0L9remainingS291 = _M0L6_2atmpS1312;
      int32_t _M0L6offsetS292 = _M0L6offsetS279;
      while (1) {
        if (_M0L9remainingS291 >= 100) {
          int32_t _M0L1tS293 = _M0L9remainingS291 / 100;
          int32_t _M0L1dS294 = _M0L9remainingS291 % 100;
          int32_t _M0L6_2atmpS1299 = _M0L1dS294 / 10;
          int32_t _M0L6_2atmpS1298 = 48 + _M0L6_2atmpS1299;
          int32_t _M0L5d__hiS295 = (uint16_t)_M0L6_2atmpS1298;
          int32_t _M0L6_2atmpS1297 = _M0L1dS294 % 10;
          int32_t _M0L6_2atmpS1296 = 48 + _M0L6_2atmpS1297;
          int32_t _M0L5d__loS296 = (uint16_t)_M0L6_2atmpS1296;
          int32_t _M0L6_2atmpS1292 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1291 = _M0L6_2atmpS1292 - 2;
          int32_t _M0L6_2atmpS1294;
          int32_t _M0L6_2atmpS1293;
          int32_t _M0L6_2atmpS1295;
          _M0L6bufferS288[_M0L6_2atmpS1291] = _M0L5d__hiS295;
          _M0L6_2atmpS1294 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS1293 = _M0L6_2atmpS1294 - 1;
          _M0L6bufferS288[_M0L6_2atmpS1293] = _M0L5d__loS296;
          _M0L6_2atmpS1295 = _M0L6offsetS292 - 2;
          _M0L9remainingS291 = _M0L1tS293;
          _M0L6offsetS292 = _M0L6_2atmpS1295;
          continue;
        } else if (_M0L9remainingS291 >= 10) {
          int32_t _M0L6_2atmpS1307 = _M0L9remainingS291 / 10;
          int32_t _M0L6_2atmpS1306 = 48 + _M0L6_2atmpS1307;
          int32_t _M0L5d__hiS298 = (uint16_t)_M0L6_2atmpS1306;
          int32_t _M0L6_2atmpS1305 = _M0L9remainingS291 % 10;
          int32_t _M0L6_2atmpS1304 = 48 + _M0L6_2atmpS1305;
          int32_t _M0L5d__loS299 = (uint16_t)_M0L6_2atmpS1304;
          int32_t _M0L6_2atmpS1301 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1300 = _M0L6_2atmpS1301 - 2;
          int32_t _M0L6_2atmpS1303;
          int32_t _M0L6_2atmpS1302;
          _M0L6bufferS288[_M0L6_2atmpS1300] = _M0L5d__hiS298;
          _M0L6_2atmpS1303 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS1302 = _M0L6_2atmpS1303 - 1;
          _M0L6bufferS288[_M0L6_2atmpS1302] = _M0L5d__loS299;
        } else {
          int32_t _M0L6_2atmpS1311 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1308 = _M0L6_2atmpS1311 - 1;
          int32_t _M0L6_2atmpS1310 = 48 + _M0L9remainingS291;
          int32_t _M0L6_2atmpS1309 = (uint16_t)_M0L6_2atmpS1310;
          _M0L6bufferS288[_M0L6_2atmpS1308] = _M0L6_2atmpS1309;
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
  int32_t _M0L6_2atmpS1258;
  int32_t _M0L6_2atmpS1257;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS261 = *(uint32_t*)&_M0L5radixS262;
  _M0L6_2atmpS1258 = _M0L5radixS262 - 1;
  _M0L6_2atmpS1257 = _M0L5radixS262 & _M0L6_2atmpS1258;
  if (_M0L6_2atmpS1257 == 0) {
    int32_t _M0L5shiftS263;
    uint32_t _M0L4maskS264;
    int32_t _M0L6_2atmpS1265;
    int32_t _M0L6offsetS265;
    uint32_t _M0L1nS266;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS263 = moonbit_ctz32(_M0L5radixS262);
    _M0L4maskS264 = _M0L4baseS261 - 1u;
    _M0L6_2atmpS1265 = _M0L10total__lenS271 - _M0L12digit__startS269;
    _M0L6offsetS265 = _M0L6_2atmpS1265;
    _M0L1nS266 = _M0L3numS272;
    while (1) {
      if (_M0L1nS266 > 0u) {
        uint32_t _M0L6_2atmpS1264 = _M0L1nS266 & _M0L4maskS264;
        int32_t _M0L5digitS267 = *(int32_t*)&_M0L6_2atmpS1264;
        int32_t _M0L6_2atmpS1261 = _M0L12digit__startS269 + _M0L6offsetS265;
        int32_t _M0L6_2atmpS1259 = _M0L6_2atmpS1261 - 1;
        int32_t _M0L6_2atmpS1260 =
          ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L5digitS267];
        int32_t _M0L6_2atmpS1262;
        uint32_t _M0L6_2atmpS1263;
        _M0L6bufferS268[_M0L6_2atmpS1259] = _M0L6_2atmpS1260;
        _M0L6_2atmpS1262 = _M0L6offsetS265 - 1;
        _M0L6_2atmpS1263 = _M0L1nS266 >> (_M0L5shiftS263 & 31);
        _M0L6offsetS265 = _M0L6_2atmpS1262;
        _M0L1nS266 = _M0L6_2atmpS1263;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1272 = _M0L10total__lenS271 - _M0L12digit__startS269;
    int32_t _M0L6offsetS273 = _M0L6_2atmpS1272;
    uint32_t _M0L1nS274 = _M0L3numS272;
    while (1) {
      if (_M0L1nS274 > 0u) {
        uint32_t _M0L1qS275 = _M0L1nS274 / _M0L4baseS261;
        uint32_t _M0L6_2atmpS1271 = _M0L1qS275 * _M0L4baseS261;
        uint32_t _M0L6_2atmpS1270 = _M0L1nS274 - _M0L6_2atmpS1271;
        int32_t _M0L5digitS276 = *(int32_t*)&_M0L6_2atmpS1270;
        int32_t _M0L6_2atmpS1268 = _M0L12digit__startS269 + _M0L6offsetS273;
        int32_t _M0L6_2atmpS1266 = _M0L6_2atmpS1268 - 1;
        int32_t _M0L6_2atmpS1267 =
          ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L5digitS276];
        int32_t _M0L6_2atmpS1269;
        _M0L6bufferS268[_M0L6_2atmpS1266] = _M0L6_2atmpS1267;
        _M0L6_2atmpS1269 = _M0L6offsetS273 - 1;
        _M0L6offsetS273 = _M0L6_2atmpS1269;
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
  int32_t _M0L6_2atmpS1256;
  int32_t _M0L6offsetS250;
  uint32_t _M0L1nS251;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1256 = _M0L10total__lenS259 - _M0L12digit__startS256;
  _M0L6offsetS250 = _M0L6_2atmpS1256;
  _M0L1nS251 = _M0L3numS260;
  while (1) {
    if (_M0L6offsetS250 >= 2) {
      uint32_t _M0L6_2atmpS1253 = _M0L1nS251 & 255u;
      int32_t _M0L9byte__valS252 = *(int32_t*)&_M0L6_2atmpS1253;
      int32_t _M0L2hiS253 = _M0L9byte__valS252 / 16;
      int32_t _M0L2loS254 = _M0L9byte__valS252 % 16;
      int32_t _M0L6_2atmpS1247 = _M0L12digit__startS256 + _M0L6offsetS250;
      int32_t _M0L6_2atmpS1245 = _M0L6_2atmpS1247 - 2;
      int32_t _M0L6_2atmpS1246 =
        ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L2hiS253];
      int32_t _M0L6_2atmpS1250;
      int32_t _M0L6_2atmpS1248;
      int32_t _M0L6_2atmpS1249;
      int32_t _M0L6_2atmpS1251;
      uint32_t _M0L6_2atmpS1252;
      _M0L6bufferS255[_M0L6_2atmpS1245] = _M0L6_2atmpS1246;
      _M0L6_2atmpS1250 = _M0L12digit__startS256 + _M0L6offsetS250;
      _M0L6_2atmpS1248 = _M0L6_2atmpS1250 - 1;
      _M0L6_2atmpS1249
      = ((moonbit_string_t)moonbit_string_literal_15.data)[
        _M0L2loS254
      ];
      _M0L6bufferS255[_M0L6_2atmpS1248] = _M0L6_2atmpS1249;
      _M0L6_2atmpS1251 = _M0L6offsetS250 - 2;
      _M0L6_2atmpS1252 = _M0L1nS251 >> 8;
      _M0L6offsetS250 = _M0L6_2atmpS1251;
      _M0L1nS251 = _M0L6_2atmpS1252;
      continue;
    } else if (_M0L6offsetS250 == 1) {
      uint32_t _M0L6_2atmpS1255 = _M0L1nS251 & 15u;
      int32_t _M0L6nibbleS258 = *(int32_t*)&_M0L6_2atmpS1255;
      int32_t _M0L6_2atmpS1254 =
        ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L6nibbleS258];
      _M0L6bufferS255[_M0L12digit__startS256] = _M0L6_2atmpS1254;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS249
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS248;
  struct _M0TPB6Logger _M0L6_2atmpS1244;
  moonbit_string_t _result_2061;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS248);
  _M0L6_2atmpS1244
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS248
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS249, _M0L6_2atmpS1244);
  if (_M0L6_2atmpS1244.$1) {
    moonbit_decref(_M0L6_2atmpS1244.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2061 = _M0MPB13StringBuilder10to__string(_M0L6loggerS248);
  moonbit_decref_cycle_free(_M0L6loggerS248);
  return _result_2061;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS245,
  struct _M0TPB6Logger _M0L6loggerS244
) {
  moonbit_string_t _M0L6_2atmpS1242;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1242 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS245);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS244.$0->$method_0(_M0L6loggerS244.$1, _M0L6_2atmpS1242);
  moonbit_decref_cycle_free(_M0L6_2atmpS1242);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS247,
  struct _M0TPB6Logger _M0L6loggerS246
) {
  moonbit_string_t _M0L6_2atmpS1243;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1243 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS247);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS246.$0->$method_0(_M0L6loggerS246.$1, _M0L6_2atmpS1243);
  moonbit_decref_cycle_free(_M0L6_2atmpS1243);
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
  moonbit_string_t _M0L8_2afieldS1940;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1940 = _M0L4selfS242.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1940);
  return _M0L8_2afieldS1940;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS238,
  moonbit_string_t _M0L5valueS239,
  int32_t _M0L5startS240,
  int32_t _M0L3lenS241
) {
  int32_t _M0L6_2atmpS1241;
  int64_t _M0L6_2atmpS1240;
  struct _M0TPC16string10StringView _M0L6_2atmpS1239;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1241 = _M0L5startS240 + _M0L3lenS241;
  _M0L6_2atmpS1240 = (int64_t)_M0L6_2atmpS1241;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1239
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS239, _M0L5startS240, _M0L6_2atmpS1240);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS238, _M0L6_2atmpS1239);
  moonbit_decref_cycle_free(_M0L6_2atmpS1239.$0);
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
  int32_t _M0L6_2atmpS1223;
  int32_t _if__result_2062;
  int32_t _M0L6_2atmpS1231;
  int32_t _if__result_2063;
  int32_t _M0L6_2atmpS1233;
  int32_t _M0L6_2atmpS1234;
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
  _M0L6_2atmpS1223 = _M0Lm2loS232;
  if (_M0L6_2atmpS1223 > 0) {
    int32_t _M0L6_2atmpS1222 = _M0Lm2loS232;
    if (_M0L6_2atmpS1222 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS1221 = _M0Lm2loS232;
      int32_t _M0L6_2atmpS1220 = _M0L4selfS231[_M0L6_2atmpS1221];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1220)) {
        int32_t _M0L6_2atmpS1219 = _M0Lm2loS232;
        int32_t _M0L6_2atmpS1218 = _M0L6_2atmpS1219 - 1;
        int32_t _M0L6_2atmpS1217 = _M0L4selfS231[_M0L6_2atmpS1218];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2062
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1217);
      } else {
        _if__result_2062 = 0;
      }
    } else {
      _if__result_2062 = 0;
    }
  } else {
    _if__result_2062 = 0;
  }
  if (_if__result_2062) {
    int32_t _M0L6_2atmpS1224 = _M0Lm2loS232;
    _M0Lm2loS232 = _M0L6_2atmpS1224 + 1;
  }
  _M0L6_2atmpS1231 = _M0Lm2hiS234;
  if (_M0L6_2atmpS1231 > 0) {
    int32_t _M0L6_2atmpS1230 = _M0Lm2hiS234;
    if (_M0L6_2atmpS1230 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS1229 = _M0Lm2hiS234;
      int32_t _M0L6_2atmpS1228 = _M0L4selfS231[_M0L6_2atmpS1229];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1228)) {
        int32_t _M0L6_2atmpS1227 = _M0Lm2hiS234;
        int32_t _M0L6_2atmpS1226 = _M0L6_2atmpS1227 - 1;
        int32_t _M0L6_2atmpS1225 = _M0L4selfS231[_M0L6_2atmpS1226];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2063
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1225);
      } else {
        _if__result_2063 = 0;
      }
    } else {
      _if__result_2063 = 0;
    }
  } else {
    _if__result_2063 = 0;
  }
  if (_if__result_2063) {
    int32_t _M0L6_2atmpS1232 = _M0Lm2hiS234;
    _M0Lm2hiS234 = _M0L6_2atmpS1232 - 1;
  }
  _M0L6_2atmpS1233 = _M0Lm2loS232;
  _M0L6_2atmpS1234 = _M0Lm2hiS234;
  if (_M0L6_2atmpS1233 >= _M0L6_2atmpS1234) {
    int32_t _M0L6_2atmpS1235 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS1236 = _M0Lm2loS232;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS1235,
                                                 .$2 = _M0L6_2atmpS1236};
  } else {
    int32_t _M0L6_2atmpS1237 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS1238 = _M0Lm2hiS234;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS1237,
                                                 .$2 = _M0L6_2atmpS1238};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS229,
  struct _M0TPB4Show _M0L4showS228
) {
  struct _M0TPB6Logger _M0L6_2atmpS1216;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS229);
  _M0L6_2atmpS1216
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS229
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS228.$0->$method_0(_M0L4showS228.$1, _M0L6_2atmpS1216);
  if (_M0L6_2atmpS1216.$1) {
    moonbit_decref(_M0L6_2atmpS1216.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS227,
  struct _M0TPB4Show _M0L4showS226
) {
  struct _M0TPB6Logger _M0L6_2atmpS1215;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1215
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS227
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS226.$0->$method_0(_M0L4showS226.$1, _M0L6_2atmpS1215);
  if (_M0L6_2atmpS1215.$1) {
    moonbit_decref(_M0L6_2atmpS1215.$1);
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
  int32_t _M0L6_2atmpS1214;
  struct _M0TPC16string10StringView _M0L6_2atmpS1212;
  struct _M0TPB6Logger _M0L6_2atmpS1213;
  moonbit_string_t _result_2064;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS223 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1214 = Moonbit_array_length(_M0L4selfS224);
  moonbit_incref_cycle_free(_M0L4selfS224);
  _M0L6_2atmpS1212
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS224, .$1 = 0, .$2 = _M0L6_2atmpS1214
  };
  moonbit_incref_cycle_free(_M0L3bufS223);
  _M0L6_2atmpS1213
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS223
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1212, _M0L6_2atmpS1213, _M0L5quoteS225);
  moonbit_decref_cycle_free(_M0L6_2atmpS1212.$0);
  if (_M0L6_2atmpS1213.$1) {
    moonbit_decref(_M0L6_2atmpS1213.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2064 = _M0MPB13StringBuilder10to__string(_M0L3bufS223);
  moonbit_decref_cycle_free(_M0L3bufS223);
  return _result_2064;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS215,
  struct _M0TPB6Logger _M0L6loggerS213,
  int32_t _M0L5quoteS212
) {
  int32_t _M0L3endS1210;
  int32_t _M0L5startS1211;
  int32_t _M0L3lenS214;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS216;
  int32_t _M0L1iS217;
  int32_t _M0L3segS218;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS212) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 34);
  }
  _M0L3endS1210 = _M0L4selfS215.$2;
  _M0L5startS1211 = _M0L4selfS215.$1;
  _M0L3lenS214 = _M0L3endS1210 - _M0L5startS1211;
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
    moonbit_string_t _M0L3strS1207;
    int32_t _M0L5startS1209;
    int32_t _M0L6_2atmpS1208;
    int32_t _M0L4codeS220;
    int32_t _M0L1cS222;
    int32_t _M0L6_2atmpS1191;
    int32_t _M0L6_2atmpS1192;
    int32_t _M0L6_2atmpS1193;
    if (_M0L1iS217 >= _M0L3lenS214) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
      moonbit_decref_cycle_free(_M0L6_2aenvS216);
      break;
    }
    _M0L3strS1207 = _M0L4selfS215.$0;
    _M0L5startS1209 = _M0L4selfS215.$1;
    _M0L6_2atmpS1208 = _M0L5startS1209 + _M0L1iS217;
    _M0L4codeS220 = _M0L3strS1207[_M0L6_2atmpS1208];
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
        int32_t _M0L6_2atmpS1194;
        int32_t _M0L6_2atmpS1195;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_16.data);
        _M0L6_2atmpS1194 = _M0L1iS217 + 1;
        _M0L6_2atmpS1195 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1194;
        _M0L3segS218 = _M0L6_2atmpS1195;
        goto _2afor_219;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1196;
        int32_t _M0L6_2atmpS1197;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_17.data);
        _M0L6_2atmpS1196 = _M0L1iS217 + 1;
        _M0L6_2atmpS1197 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1196;
        _M0L3segS218 = _M0L6_2atmpS1197;
        goto _2afor_219;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1198;
        int32_t _M0L6_2atmpS1199;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1198 = _M0L1iS217 + 1;
        _M0L6_2atmpS1199 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1198;
        _M0L3segS218 = _M0L6_2atmpS1199;
        goto _2afor_219;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1200;
        int32_t _M0L6_2atmpS1201;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1200 = _M0L1iS217 + 1;
        _M0L6_2atmpS1201 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1200;
        _M0L3segS218 = _M0L6_2atmpS1201;
        goto _2afor_219;
        break;
      }
      default: {
        if (_M0L4codeS220 < 32) {
          int32_t _M0L6_2atmpS1203;
          moonbit_string_t _M0L6_2atmpS1202;
          int32_t _M0L6_2atmpS1204;
          int32_t _M0L6_2atmpS1205;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_20.data);
          _M0L6_2atmpS1203 = _M0L4codeS220 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1202 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1203);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, _M0L6_2atmpS1202);
          moonbit_decref_cycle_free(_M0L6_2atmpS1202);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1204 = _M0L1iS217 + 1;
          _M0L6_2atmpS1205 = _M0L1iS217 + 1;
          _M0L1iS217 = _M0L6_2atmpS1204;
          _M0L3segS218 = _M0L6_2atmpS1205;
          goto _2afor_219;
        } else {
          int32_t _M0L6_2atmpS1206 = _M0L1iS217 + 1;
          int32_t _tmp_2067 = _M0L3segS218;
          _M0L1iS217 = _M0L6_2atmpS1206;
          _M0L3segS218 = _tmp_2067;
          goto _2afor_219;
        }
        break;
      }
    }
    goto joinlet_2066;
    join_221:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1191 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS222);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, _M0L6_2atmpS1191);
    _M0L6_2atmpS1192 = _M0L1iS217 + 1;
    _M0L6_2atmpS1193 = _M0L1iS217 + 1;
    _M0L1iS217 = _M0L6_2atmpS1192;
    _M0L3segS218 = _M0L6_2atmpS1193;
    continue;
    joinlet_2066:;
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
    int64_t _M0L6_2atmpS1190 = (int64_t)_M0L1iS210;
    struct _M0TPC16string10StringView _M0L6_2atmpS1189;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1189
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS209, _M0L3segS211, _M0L6_2atmpS1190);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS207.$0->$method_2(_M0L6loggerS207.$1, _M0L6_2atmpS1189);
    moonbit_decref_cycle_free(_M0L6_2atmpS1189.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS198,
  int32_t _M0L5startS200,
  int64_t _M0L3endS202
) {
  int32_t _M0L3endS1187;
  int32_t _M0L5startS1188;
  int32_t _M0L3lenS197;
  int32_t _M0Lm2loS199;
  int32_t _M0Lm2hiS201;
  moonbit_string_t _M0L3strS205;
  int32_t _M0L4baseS206;
  int32_t _M0L6_2atmpS1165;
  int32_t _if__result_2068;
  int32_t _M0L6_2atmpS1175;
  int32_t _if__result_2069;
  int32_t _M0L6_2atmpS1177;
  int32_t _M0L6_2atmpS1178;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1187 = _M0L4selfS198.$2;
  _M0L5startS1188 = _M0L4selfS198.$1;
  _M0L3lenS197 = _M0L3endS1187 - _M0L5startS1188;
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
  _M0L6_2atmpS1165 = _M0Lm2loS199;
  if (_M0L6_2atmpS1165 > 0) {
    int32_t _M0L6_2atmpS1164 = _M0Lm2loS199;
    if (_M0L6_2atmpS1164 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1163 = _M0Lm2loS199;
      int32_t _M0L6_2atmpS1162 = _M0L4baseS206 + _M0L6_2atmpS1163;
      int32_t _M0L6_2atmpS1161 = _M0L3strS205[_M0L6_2atmpS1162];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1161)) {
        int32_t _M0L6_2atmpS1160 = _M0Lm2loS199;
        int32_t _M0L6_2atmpS1159 = _M0L4baseS206 + _M0L6_2atmpS1160;
        int32_t _M0L6_2atmpS1158 = _M0L6_2atmpS1159 - 1;
        int32_t _M0L6_2atmpS1157 = _M0L3strS205[_M0L6_2atmpS1158];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2068
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1157);
      } else {
        _if__result_2068 = 0;
      }
    } else {
      _if__result_2068 = 0;
    }
  } else {
    _if__result_2068 = 0;
  }
  if (_if__result_2068) {
    int32_t _M0L6_2atmpS1166 = _M0Lm2loS199;
    _M0Lm2loS199 = _M0L6_2atmpS1166 + 1;
  }
  _M0L6_2atmpS1175 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1175 > 0) {
    int32_t _M0L6_2atmpS1174 = _M0Lm2hiS201;
    if (_M0L6_2atmpS1174 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1173 = _M0Lm2hiS201;
      int32_t _M0L6_2atmpS1172 = _M0L4baseS206 + _M0L6_2atmpS1173;
      int32_t _M0L6_2atmpS1171 = _M0L3strS205[_M0L6_2atmpS1172];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1171)) {
        int32_t _M0L6_2atmpS1170 = _M0Lm2hiS201;
        int32_t _M0L6_2atmpS1169 = _M0L4baseS206 + _M0L6_2atmpS1170;
        int32_t _M0L6_2atmpS1168 = _M0L6_2atmpS1169 - 1;
        int32_t _M0L6_2atmpS1167 = _M0L3strS205[_M0L6_2atmpS1168];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2069
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1167);
      } else {
        _if__result_2069 = 0;
      }
    } else {
      _if__result_2069 = 0;
    }
  } else {
    _if__result_2069 = 0;
  }
  if (_if__result_2069) {
    int32_t _M0L6_2atmpS1176 = _M0Lm2hiS201;
    _M0Lm2hiS201 = _M0L6_2atmpS1176 - 1;
  }
  _M0L6_2atmpS1177 = _M0Lm2loS199;
  _M0L6_2atmpS1178 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1177 >= _M0L6_2atmpS1178) {
    int32_t _M0L6_2atmpS1182 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1179 = _M0L4baseS206 + _M0L6_2atmpS1182;
    int32_t _M0L6_2atmpS1181 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1180 = _M0L4baseS206 + _M0L6_2atmpS1181;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1179,
                                                 .$2 = _M0L6_2atmpS1180};
  } else {
    int32_t _M0L6_2atmpS1186 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1183 = _M0L4baseS206 + _M0L6_2atmpS1186;
    int32_t _M0L6_2atmpS1185 = _M0Lm2hiS201;
    int32_t _M0L6_2atmpS1184 = _M0L4baseS206 + _M0L6_2atmpS1185;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1183,
                                                 .$2 = _M0L6_2atmpS1184};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS196) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS195;
  int32_t _M0L6_2atmpS1154;
  int32_t _M0L6_2atmpS1153;
  int32_t _M0L6_2atmpS1156;
  int32_t _M0L6_2atmpS1155;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1152;
  moonbit_string_t _result_2070;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS195 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1154 = _M0IPC14byte4BytePB3Div3div(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1153
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1154);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1153);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1156 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1155
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1156);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1155);
  _M0L6_2atmpS1152 = _M0L7_2aselfS195;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2070 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1152);
  moonbit_decref_cycle_free(_M0L6_2atmpS1152);
  return _result_2070;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS194) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS194 < 10) {
    int32_t _M0L6_2atmpS1149;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1149 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1149);
  } else {
    int32_t _M0L6_2atmpS1151;
    int32_t _M0L6_2atmpS1150;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1151 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1150 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1151, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1150);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS192,
  int32_t _M0L4thatS193
) {
  int32_t _M0L6_2atmpS1147;
  int32_t _M0L6_2atmpS1148;
  int32_t _M0L6_2atmpS1146;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1147 = (int32_t)_M0L4selfS192;
  _M0L6_2atmpS1148 = (int32_t)_M0L4thatS193;
  _M0L6_2atmpS1146 = _M0L6_2atmpS1147 - _M0L6_2atmpS1148;
  return _M0L6_2atmpS1146 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS190,
  int32_t _M0L4thatS191
) {
  int32_t _M0L6_2atmpS1144;
  int32_t _M0L6_2atmpS1145;
  int32_t _M0L6_2atmpS1143;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1144 = (int32_t)_M0L4selfS190;
  _M0L6_2atmpS1145 = (int32_t)_M0L4thatS191;
  _M0L6_2atmpS1143 = _M0L6_2atmpS1144 % _M0L6_2atmpS1145;
  return _M0L6_2atmpS1143 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS188,
  int32_t _M0L4thatS189
) {
  int32_t _M0L6_2atmpS1141;
  int32_t _M0L6_2atmpS1142;
  int32_t _M0L6_2atmpS1140;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1141 = (int32_t)_M0L4selfS188;
  _M0L6_2atmpS1142 = (int32_t)_M0L4thatS189;
  _M0L6_2atmpS1140 = _M0L6_2atmpS1141 / _M0L6_2atmpS1142;
  return _M0L6_2atmpS1140 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS186,
  int32_t _M0L4thatS187
) {
  int32_t _M0L6_2atmpS1138;
  int32_t _M0L6_2atmpS1139;
  int32_t _M0L6_2atmpS1137;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1138 = (int32_t)_M0L4selfS186;
  _M0L6_2atmpS1139 = (int32_t)_M0L4thatS187;
  _M0L6_2atmpS1137 = _M0L6_2atmpS1138 + _M0L6_2atmpS1139;
  return _M0L6_2atmpS1137 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS185) {
  int32_t _M0L6_2atmpS1136;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1136 = (int32_t)_M0L4selfS185;
  return _M0L6_2atmpS1136;
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
  int32_t _M0L3lenS1135;
  int32_t _M0L8requiredS181;
  uint16_t* _M0L4dataS1130;
  int32_t _M0L6_2atmpS1129;
  int32_t _if__result_2071;
  uint16_t* _M0L4dataS1131;
  int32_t _M0L3lenS1132;
  int32_t _M0L3lenS1134;
  int32_t _M0L6_2atmpS1133;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS179 = Moonbit_array_length(_M0L3strS180);
  if (_M0L8str__lenS179 == 0) {
    return 0;
  }
  _M0L3lenS1135 = _M0L4selfS182->$1;
  _M0L8requiredS181 = _M0L3lenS1135 + _M0L8str__lenS179;
  _M0L4dataS1130 = _M0L4selfS182->$0;
  _M0L6_2atmpS1129 = Moonbit_array_length(_M0L4dataS1130);
  if (_M0L8requiredS181 > _M0L6_2atmpS1129) {
    _if__result_2071 = 1;
  } else {
    int32_t _M0L3lenS1128 = _M0L4selfS182->$1;
    _if__result_2071 = _M0L8requiredS181 < _M0L3lenS1128;
  }
  if (_if__result_2071) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS182, _M0L8requiredS181);
  }
  _M0L4dataS1131 = _M0L4selfS182->$0;
  _M0L3lenS1132 = _M0L4selfS182->$1;
  moonbit_incref_cycle_free(_M0L4dataS1131);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1131, _M0L3lenS1132, _M0L3strS180, 0, _M0L8str__lenS179);
  moonbit_decref_cycle_free(_M0L4dataS1131);
  _M0L3lenS1134 = _M0L4selfS182->$1;
  _M0L6_2atmpS1133 = _M0L3lenS1134 + _M0L8str__lenS179;
  _M0L4selfS182->$1 = _M0L6_2atmpS1133;
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
      int32_t _M0L6_2atmpS1125 = _M0L3strS176[_M0L1iS173];
      int32_t _M0L6_2atmpS1126;
      int32_t _M0L6_2atmpS1127;
      _M0L4selfS175[_M0L1jS174] = _M0L6_2atmpS1125;
      _M0L6_2atmpS1126 = _M0L1iS173 + 1;
      _M0L6_2atmpS1127 = _M0L1jS174 + 1;
      _M0L1iS173 = _M0L6_2atmpS1126;
      _M0L1jS174 = _M0L6_2atmpS1127;
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
    int32_t _M0L3lenS1096 = _M0L4selfS168->$1;
    uint16_t* _M0L4dataS1098 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1097 = Moonbit_array_length(_M0L4dataS1098);
    uint16_t* _M0L4dataS1101;
    int32_t _M0L3lenS1102;
    int32_t _M0L6_2atmpS1103;
    int32_t _M0L3lenS1105;
    int32_t _M0L6_2atmpS1104;
    if (_M0L3lenS1096 >= _M0L6_2atmpS1097) {
      int32_t _M0L3lenS1100 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1099 = _M0L3lenS1100 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1099);
    }
    _M0L4dataS1101 = _M0L4selfS168->$0;
    _M0L3lenS1102 = _M0L4selfS168->$1;
    moonbit_incref_cycle_free(_M0L4dataS1101);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1103 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS166);
    if (
      _M0L3lenS1102 < 0
      || _M0L3lenS1102 >= Moonbit_array_length(_M0L4dataS1101)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1101[_M0L3lenS1102] = _M0L6_2atmpS1103;
    moonbit_decref_cycle_free(_M0L4dataS1101);
    _M0L3lenS1105 = _M0L4selfS168->$1;
    _M0L6_2atmpS1104 = _M0L3lenS1105 + 1;
    _M0L4selfS168->$1 = _M0L6_2atmpS1104;
  } else if (_M0L4codeS166 <= 1114111u) {
    uint16_t* _M0L4dataS1109 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1107 = Moonbit_array_length(_M0L4dataS1109);
    int32_t _M0L3lenS1108 = _M0L4selfS168->$1;
    int32_t _M0L6_2atmpS1106 = _M0L6_2atmpS1107 - _M0L3lenS1108;
    uint32_t _M0L4codeS169;
    uint16_t* _M0L4dataS1112;
    int32_t _M0L3lenS1113;
    uint32_t _M0L6_2atmpS1116;
    uint32_t _M0L6_2atmpS1115;
    int32_t _M0L6_2atmpS1114;
    uint16_t* _M0L4dataS1117;
    int32_t _M0L3lenS1122;
    int32_t _M0L6_2atmpS1118;
    uint32_t _M0L6_2atmpS1121;
    uint32_t _M0L6_2atmpS1120;
    int32_t _M0L6_2atmpS1119;
    int32_t _M0L3lenS1124;
    int32_t _M0L6_2atmpS1123;
    if (_M0L6_2atmpS1106 < 2) {
      int32_t _M0L3lenS1111 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1110 = _M0L3lenS1111 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1110);
    }
    _M0L4codeS169 = _M0L4codeS166 - 65536u;
    _M0L4dataS1112 = _M0L4selfS168->$0;
    _M0L3lenS1113 = _M0L4selfS168->$1;
    _M0L6_2atmpS1116 = _M0L4codeS169 >> 10;
    _M0L6_2atmpS1115 = 55296u + _M0L6_2atmpS1116;
    moonbit_incref_cycle_free(_M0L4dataS1112);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1114 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1115);
    if (
      _M0L3lenS1113 < 0
      || _M0L3lenS1113 >= Moonbit_array_length(_M0L4dataS1112)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1112[_M0L3lenS1113] = _M0L6_2atmpS1114;
    moonbit_decref_cycle_free(_M0L4dataS1112);
    _M0L4dataS1117 = _M0L4selfS168->$0;
    _M0L3lenS1122 = _M0L4selfS168->$1;
    _M0L6_2atmpS1118 = _M0L3lenS1122 + 1;
    _M0L6_2atmpS1121 = _M0L4codeS169 & 1023u;
    _M0L6_2atmpS1120 = 56320u + _M0L6_2atmpS1121;
    moonbit_incref_cycle_free(_M0L4dataS1117);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1119 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1120);
    if (
      _M0L6_2atmpS1118 < 0
      || _M0L6_2atmpS1118 >= Moonbit_array_length(_M0L4dataS1117)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1117[_M0L6_2atmpS1118] = _M0L6_2atmpS1119;
    moonbit_decref_cycle_free(_M0L4dataS1117);
    _M0L3lenS1124 = _M0L4selfS168->$1;
    _M0L6_2atmpS1123 = _M0L3lenS1124 + 2;
    _M0L4selfS168->$1 = _M0L6_2atmpS1123;
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
  uint16_t* _M0L4dataS1095;
  int32_t _M0L6_2atmpS1093;
  int32_t _M0L3lenS1094;
  int32_t _M0L13new__capacityS162;
  uint16_t* _M0L4dataS1090;
  int32_t _M0L6_2atmpS1091;
  int32_t _M0L3lenS1092;
  uint16_t* _M0L9new__dataS165;
  uint16_t* _M0L6_2aoldS1941;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1095 = _M0L4selfS163->$0;
  _M0L6_2atmpS1093 = Moonbit_array_length(_M0L4dataS1095);
  _M0L3lenS1094 = _M0L4selfS163->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS162
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1093, _M0L3lenS1094, _M0L8requiredS164);
  _M0L4dataS1090 = _M0L4selfS163->$0;
  moonbit_incref_cycle_free(_M0L4dataS1090);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1091 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1092 = _M0L4selfS163->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS165
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1090, _M0L13new__capacityS162, _M0L6_2atmpS1091, _M0L3lenS1092, 0, 0);
  _M0L6_2aoldS1941 = _M0L4selfS163->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1941);
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
  int32_t _M0L6_2atmpS1089;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1089 = *(int32_t*)&_M0L4selfS155;
  return (uint16_t)_M0L6_2atmpS1089;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS154) {
  int32_t _M0L6_2atmpS1088;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1088 = _M0L4selfS154;
  return *(uint32_t*)&_M0L6_2atmpS1088;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS152
) {
  int32_t _M0L3lenS1079;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1079 = _M0L4selfS152->$1;
  if (_M0L3lenS1079 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1080 = _M0L4selfS152->$1;
    uint16_t* _M0L4dataS1082 = _M0L4selfS152->$0;
    int32_t _M0L6_2atmpS1081 = Moonbit_array_length(_M0L4dataS1082);
    if (_M0L3lenS1080 == _M0L6_2atmpS1081) {
      uint16_t* _M0L4dataS1083 = _M0L4selfS152->$0;
      moonbit_incref_cycle_free(_M0L4dataS1083);
      return _M0L4dataS1083;
    } else {
      uint16_t* _M0L4dataS1084 = _M0L4selfS152->$0;
      int32_t _M0L3lenS1085 = _M0L4selfS152->$1;
      int32_t _M0L6_2atmpS1086;
      int32_t _M0L3lenS1087;
      uint16_t* _M0L4dataS153;
      moonbit_incref_cycle_free(_M0L4dataS1084);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1086 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1087 = _M0L4selfS152->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS153
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1084, _M0L3lenS1085, _M0L6_2atmpS1086, _M0L3lenS1087, 0, 0);
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
  int32_t _if__result_2074;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS145 >= 0) {
    if (_M0L3lenS146 >= 0) {
      if (_M0L11src__offsetS147 >= 0) {
        if (_M0L11dst__offsetS148 >= 0) {
          int32_t _M0L6_2atmpS1075 = _M0L11src__offsetS147 + _M0L3lenS146;
          int32_t _M0L6_2atmpS1076 = Moonbit_array_length(_M0L3srcS149);
          if (_M0L6_2atmpS1075 <= _M0L6_2atmpS1076) {
            int32_t _M0L6_2atmpS1074 = _M0L11dst__offsetS148 + _M0L3lenS146;
            _if__result_2074 = _M0L6_2atmpS1074 <= _M0L13allocate__lenS145;
          } else {
            _if__result_2074 = 0;
          }
        } else {
          _if__result_2074 = 0;
        }
      } else {
        _if__result_2074 = 0;
      }
    } else {
      _if__result_2074 = 0;
    }
  } else {
    _if__result_2074 = 0;
  }
  if (_if__result_2074) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS149, _M0L13allocate__lenS145, _M0L4initS150, _M0L11src__offsetS147, _M0L11dst__offsetS148, _M0L3lenS146);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS151;
    int32_t _M0L6_2atmpS1078;
    moonbit_string_t _M0L6_2atmpS1077;
    uint16_t* _result_2075;
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
    _M0L6_2atmpS1078 = Moonbit_array_length(_M0L3srcS149);
    moonbit_decref_cycle_free(_M0L3srcS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L6_2atmpS1078);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1077
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS151);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS151);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2075 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1077);
    moonbit_decref_cycle_free(_M0L6_2atmpS1077);
    return _result_2075;
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
  struct _M0TPB13StringBuilder* _block_2076;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS136 < 1) {
    _M0L7initialS135 = 1;
  } else {
    int32_t _M0L6_2atmpS1073 = _M0L10size__hintS136 + 1;
    _M0L7initialS135 = _M0L6_2atmpS1073 / 2;
  }
  _M0L4dataS137 = (uint16_t*)moonbit_make_string(_M0L7initialS135, 0);
  _block_2076
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2076)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 87, 0);
  _block_2076->$0 = _M0L4dataS137;
  _block_2076->$1 = 0;
  return _block_2076;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS134) {
  int32_t _M0L6_2atmpS1072;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1072 = (int32_t)_M0L4selfS134;
  return _M0L6_2atmpS1072;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS114,
  int32_t _M0L13allocate__lenS110,
  int32_t _M0L3lenS111,
  int32_t _M0L11src__offsetS112,
  int32_t _M0L11dst__offsetS113
) {
  int32_t _if__result_2077;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS110 >= 0) {
    if (_M0L3lenS111 >= 0) {
      if (_M0L11src__offsetS112 >= 0) {
        if (_M0L11dst__offsetS113 >= 0) {
          int32_t _M0L6_2atmpS1053 = _M0L11src__offsetS112 + _M0L3lenS111;
          int32_t _M0L6_2atmpS1054;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1054
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS114);
          if (_M0L6_2atmpS1053 <= _M0L6_2atmpS1054) {
            int32_t _M0L6_2atmpS1052 = _M0L11dst__offsetS113 + _M0L3lenS111;
            _if__result_2077 = _M0L6_2atmpS1052 <= _M0L13allocate__lenS110;
          } else {
            _if__result_2077 = 0;
          }
        } else {
          _if__result_2077 = 0;
        }
      } else {
        _if__result_2077 = 0;
      }
    } else {
      _if__result_2077 = 0;
    }
  } else {
    _if__result_2077 = 0;
  }
  if (_if__result_2077) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS110, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS114, _M0L11src__offsetS112, _M0L11dst__offsetS113, _M0L3lenS111);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS115;
    int32_t _M0L6_2atmpS1056;
    moonbit_string_t _M0L6_2atmpS1055;
    moonbit_string_t* _result_2078;
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
    _M0L6_2atmpS1056 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS114);
    moonbit_decref_cycle_free(_M0L3srcS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L6_2atmpS1056);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1055
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS115);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS115);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2078
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1055);
    moonbit_decref_cycle_free(_M0L6_2atmpS1055);
    return _result_2078;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS120,
  int32_t _M0L13allocate__lenS116,
  int32_t _M0L3lenS117,
  int32_t _M0L11src__offsetS118,
  int32_t _M0L11dst__offsetS119
) {
  int32_t _if__result_2079;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS116 >= 0) {
    if (_M0L3lenS117 >= 0) {
      if (_M0L11src__offsetS118 >= 0) {
        if (_M0L11dst__offsetS119 >= 0) {
          int32_t _M0L6_2atmpS1058 = _M0L11src__offsetS118 + _M0L3lenS117;
          int32_t _M0L6_2atmpS1059;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1059
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS120);
          if (_M0L6_2atmpS1058 <= _M0L6_2atmpS1059) {
            int32_t _M0L6_2atmpS1057 = _M0L11dst__offsetS119 + _M0L3lenS117;
            _if__result_2079 = _M0L6_2atmpS1057 <= _M0L13allocate__lenS116;
          } else {
            _if__result_2079 = 0;
          }
        } else {
          _if__result_2079 = 0;
        }
      } else {
        _if__result_2079 = 0;
      }
    } else {
      _if__result_2079 = 0;
    }
  } else {
    _if__result_2079 = 0;
  }
  if (_if__result_2079) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS116, 0, _M0L3srcS120, _M0L11src__offsetS118, _M0L11dst__offsetS119, _M0L3lenS117);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS121;
    int32_t _M0L6_2atmpS1061;
    moonbit_string_t _M0L6_2atmpS1060;
    struct _M0TUsiE** _result_2080;
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
    _M0L6_2atmpS1061 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS120);
    moonbit_decref_cycle_free(_M0L3srcS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L6_2atmpS1061);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1060
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS121);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS121);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2080
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1060);
    moonbit_decref_cycle_free(_M0L6_2atmpS1060);
    return _result_2080;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS126,
  int32_t _M0L13allocate__lenS122,
  int32_t _M0L3lenS123,
  int32_t _M0L11src__offsetS124,
  int32_t _M0L11dst__offsetS125
) {
  int32_t _if__result_2081;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS122 >= 0) {
    if (_M0L3lenS123 >= 0) {
      if (_M0L11src__offsetS124 >= 0) {
        if (_M0L11dst__offsetS125 >= 0) {
          int32_t _M0L6_2atmpS1063 = _M0L11src__offsetS124 + _M0L3lenS123;
          int32_t _M0L6_2atmpS1064;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1064
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS126);
          if (_M0L6_2atmpS1063 <= _M0L6_2atmpS1064) {
            int32_t _M0L6_2atmpS1062 = _M0L11dst__offsetS125 + _M0L3lenS123;
            _if__result_2081 = _M0L6_2atmpS1062 <= _M0L13allocate__lenS122;
          } else {
            _if__result_2081 = 0;
          }
        } else {
          _if__result_2081 = 0;
        }
      } else {
        _if__result_2081 = 0;
      }
    } else {
      _if__result_2081 = 0;
    }
  } else {
    _if__result_2081 = 0;
  }
  if (_if__result_2081) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS126, _M0L13allocate__lenS122, _M0L11src__offsetS124, _M0L11dst__offsetS125, _M0L3lenS123);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS127;
    int32_t _M0L6_2atmpS1066;
    moonbit_string_t _M0L6_2atmpS1065;
    float* _result_2082;
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
    _M0L6_2atmpS1066 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS126);
    moonbit_decref_cycle_free(_M0L3srcS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L6_2atmpS1066);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1065
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS127);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS127);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2082
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1065);
    moonbit_decref_cycle_free(_M0L6_2atmpS1065);
    return _result_2082;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS132,
  int32_t _M0L13allocate__lenS128,
  int32_t _M0L3lenS129,
  int32_t _M0L11src__offsetS130,
  int32_t _M0L11dst__offsetS131
) {
  int32_t _if__result_2083;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS128 >= 0) {
    if (_M0L3lenS129 >= 0) {
      if (_M0L11src__offsetS130 >= 0) {
        if (_M0L11dst__offsetS131 >= 0) {
          int32_t _M0L6_2atmpS1068 = _M0L11src__offsetS130 + _M0L3lenS129;
          int32_t _M0L6_2atmpS1069;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1069
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS132);
          if (_M0L6_2atmpS1068 <= _M0L6_2atmpS1069) {
            int32_t _M0L6_2atmpS1067 = _M0L11dst__offsetS131 + _M0L3lenS129;
            _if__result_2083 = _M0L6_2atmpS1067 <= _M0L13allocate__lenS128;
          } else {
            _if__result_2083 = 0;
          }
        } else {
          _if__result_2083 = 0;
        }
      } else {
        _if__result_2083 = 0;
      }
    } else {
      _if__result_2083 = 0;
    }
  } else {
    _if__result_2083 = 0;
  }
  if (_if__result_2083) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS132, _M0L13allocate__lenS128, _M0L11src__offsetS130, _M0L11dst__offsetS131, _M0L3lenS129);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS133;
    int32_t _M0L6_2atmpS1071;
    moonbit_string_t _M0L6_2atmpS1070;
    int32_t* _result_2084;
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
    _M0L6_2atmpS1071 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS132);
    moonbit_decref_cycle_free(_M0L3srcS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L6_2atmpS1071);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1070
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS133);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS133);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2084
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1070);
    moonbit_decref_cycle_free(_M0L6_2atmpS1070);
    return _result_2084;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS107,
  moonbit_string_t _M0L3objS106
) {
  struct _M0TPB6Logger _M0L6_2atmpS1050;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS107);
  _M0L6_2atmpS1050
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS107
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS106, _M0L6_2atmpS1050);
  if (_M0L6_2atmpS1050.$1) {
    moonbit_decref(_M0L6_2atmpS1050.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS109,
  int32_t _M0L3objS108
) {
  struct _M0TPB6Logger _M0L6_2atmpS1051;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS109);
  _M0L6_2atmpS1051
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS109
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS108, _M0L6_2atmpS1051);
  if (_M0L6_2atmpS1051.$1) {
    moonbit_decref(_M0L6_2atmpS1051.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS85,
  int32_t _M0L13allocate__lenS83,
  int32_t _M0L11src__offsetS86,
  int32_t _M0L11dst__offsetS84,
  int32_t _M0L9blit__lenS87
) {
  moonbit_string_t* _M0L3dstS82;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS82
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS83, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS82, _M0L11dst__offsetS84, _M0L3srcS85, _M0L11src__offsetS86, _M0L9blit__lenS87);
  moonbit_decref_cycle_free(_M0L3srcS85);
  return _M0L3dstS82;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS91,
  int32_t _M0L13allocate__lenS89,
  int32_t _M0L11src__offsetS92,
  int32_t _M0L11dst__offsetS90,
  int32_t _M0L9blit__lenS93
) {
  struct _M0TUsiE** _M0L3dstS88;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS88
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS89, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS88, _M0L11dst__offsetS90, _M0L3srcS91, _M0L11src__offsetS92, _M0L9blit__lenS93);
  moonbit_decref_cycle_free(_M0L3srcS91);
  return _M0L3dstS88;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS97,
  int32_t _M0L13allocate__lenS95,
  int32_t _M0L11src__offsetS98,
  int32_t _M0L11dst__offsetS96,
  int32_t _M0L9blit__lenS99
) {
  float* _M0L3dstS94;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS94 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS95);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS94, _M0L11dst__offsetS96, _M0L3srcS97, _M0L11src__offsetS98, _M0L9blit__lenS99);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS62,
  int32_t _M0L11dst__offsetS63,
  moonbit_string_t* _M0L3srcS64,
  int32_t _M0L11src__offsetS65,
  int32_t _M0L3lenS66
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS64);
  moonbit_incref_cycle_free(_M0L3dstS62);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS62, _M0L11dst__offsetS63, _M0L3srcS64, _M0L11src__offsetS65, _M0L3lenS66);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS67,
  int32_t _M0L11dst__offsetS68,
  struct _M0TUsiE** _M0L3srcS69,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS72,
  int32_t _M0L11dst__offsetS73,
  float* _M0L3srcS74,
  int32_t _M0L11src__offsetS75,
  int32_t _M0L3lenS76
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS74);
  moonbit_incref_cycle_free(_M0L3dstS72);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS72, _M0L11dst__offsetS73, _M0L3srcS74, _M0L11src__offsetS75, _M0L3lenS76, sizeof(float));
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
        int32_t _M0L6_2atmpS1005 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS1007 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS1006;
        int32_t _M0L6_2atmpS1008;
        if (
          _M0L6_2atmpS1007 < 0
          || _M0L6_2atmpS1007 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1006 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1007];
        if (
          _M0L6_2atmpS1005 < 0
          || _M0L6_2atmpS1005 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1005] = _M0L6_2atmpS1006;
        _M0L6_2atmpS1008 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS1008;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1013 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS1013;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS1009 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS1011 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS1010;
        int32_t _M0L6_2atmpS1012;
        if (
          _M0L6_2atmpS1011 < 0
          || _M0L6_2atmpS1011 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1010 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1011];
        if (
          _M0L6_2atmpS1009 < 0
          || _M0L6_2atmpS1009 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1009] = _M0L6_2atmpS1010;
        _M0L6_2atmpS1012 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS1012;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS26,
  int32_t _M0L11dst__offsetS28,
  moonbit_string_t* _M0L3srcS27,
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
        int32_t _M0L6_2atmpS1014 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1016 = _M0L11src__offsetS29 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS1015;
        moonbit_string_t _M0L6_2aoldS1942;
        int32_t _M0L6_2atmpS1017;
        if (
          _M0L6_2atmpS1016 < 0
          || _M0L6_2atmpS1016 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1015 = (moonbit_string_t)_M0L3srcS27[_M0L6_2atmpS1016];
        if (
          _M0L6_2atmpS1014 < 0
          || _M0L6_2atmpS1014 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1942 = (moonbit_string_t)_M0L3dstS26[_M0L6_2atmpS1014];
        moonbit_incref_cycle_free(_M0L6_2atmpS1015);
        moonbit_decref_cycle_free(_M0L6_2aoldS1942);
        _M0L3dstS26[_M0L6_2atmpS1014] = _M0L6_2atmpS1015;
        _M0L6_2atmpS1017 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1017;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1022 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1022;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1018 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1020 = _M0L11src__offsetS29 + _M0L1iS33;
        moonbit_string_t _M0L6_2atmpS1019;
        moonbit_string_t _M0L6_2aoldS1943;
        int32_t _M0L6_2atmpS1021;
        if (
          _M0L6_2atmpS1020 < 0
          || _M0L6_2atmpS1020 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1019 = (moonbit_string_t)_M0L3srcS27[_M0L6_2atmpS1020];
        if (
          _M0L6_2atmpS1018 < 0
          || _M0L6_2atmpS1018 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1943 = (moonbit_string_t)_M0L3dstS26[_M0L6_2atmpS1018];
        moonbit_incref_cycle_free(_M0L6_2atmpS1019);
        moonbit_decref_cycle_free(_M0L6_2aoldS1943);
        _M0L3dstS26[_M0L6_2atmpS1018] = _M0L6_2atmpS1019;
        _M0L6_2atmpS1021 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1021;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS35,
  int32_t _M0L11dst__offsetS37,
  struct _M0TUsiE** _M0L3srcS36,
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
        int32_t _M0L6_2atmpS1023 = _M0L11dst__offsetS37 + _M0L1iS39;
        int32_t _M0L6_2atmpS1025 = _M0L11src__offsetS38 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS1024;
        struct _M0TUsiE* _M0L6_2aoldS1944;
        int32_t _M0L6_2atmpS1026;
        if (
          _M0L6_2atmpS1025 < 0
          || _M0L6_2atmpS1025 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1024 = (struct _M0TUsiE*)_M0L3srcS36[_M0L6_2atmpS1025];
        if (
          _M0L6_2atmpS1023 < 0
          || _M0L6_2atmpS1023 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1944 = (struct _M0TUsiE*)_M0L3dstS35[_M0L6_2atmpS1023];
        if (_M0L6_2atmpS1024) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1024);
        }
        if (_M0L6_2aoldS1944) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1944);
        }
        _M0L3dstS35[_M0L6_2atmpS1023] = _M0L6_2atmpS1024;
        _M0L6_2atmpS1026 = _M0L1iS39 + 1;
        _M0L1iS39 = _M0L6_2atmpS1026;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1031 = _M0L3lenS40 - 1;
    int32_t _M0L1iS42 = _M0L6_2atmpS1031;
    while (1) {
      if (_M0L1iS42 >= 0) {
        int32_t _M0L6_2atmpS1027 = _M0L11dst__offsetS37 + _M0L1iS42;
        int32_t _M0L6_2atmpS1029 = _M0L11src__offsetS38 + _M0L1iS42;
        struct _M0TUsiE* _M0L6_2atmpS1028;
        struct _M0TUsiE* _M0L6_2aoldS1945;
        int32_t _M0L6_2atmpS1030;
        if (
          _M0L6_2atmpS1029 < 0
          || _M0L6_2atmpS1029 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1028 = (struct _M0TUsiE*)_M0L3srcS36[_M0L6_2atmpS1029];
        if (
          _M0L6_2atmpS1027 < 0
          || _M0L6_2atmpS1027 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1945 = (struct _M0TUsiE*)_M0L3dstS35[_M0L6_2atmpS1027];
        if (_M0L6_2atmpS1028) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1028);
        }
        if (_M0L6_2aoldS1945) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1945);
        }
        _M0L3dstS35[_M0L6_2atmpS1027] = _M0L6_2atmpS1028;
        _M0L6_2atmpS1030 = _M0L1iS42 - 1;
        _M0L1iS42 = _M0L6_2atmpS1030;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS44,
  int32_t _M0L11dst__offsetS46,
  float* _M0L3srcS45,
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
        int32_t _M0L6_2atmpS1032 = _M0L11dst__offsetS46 + _M0L1iS48;
        int32_t _M0L6_2atmpS1034 = _M0L11src__offsetS47 + _M0L1iS48;
        float _M0L6_2atmpS1033;
        int32_t _M0L6_2atmpS1035;
        if (
          _M0L6_2atmpS1034 < 0
          || _M0L6_2atmpS1034 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1033 = (float)_M0L3srcS45[_M0L6_2atmpS1034];
        if (
          _M0L6_2atmpS1032 < 0
          || _M0L6_2atmpS1032 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS44[_M0L6_2atmpS1032] = _M0L6_2atmpS1033;
        _M0L6_2atmpS1035 = _M0L1iS48 + 1;
        _M0L1iS48 = _M0L6_2atmpS1035;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1040 = _M0L3lenS49 - 1;
    int32_t _M0L1iS51 = _M0L6_2atmpS1040;
    while (1) {
      if (_M0L1iS51 >= 0) {
        int32_t _M0L6_2atmpS1036 = _M0L11dst__offsetS46 + _M0L1iS51;
        int32_t _M0L6_2atmpS1038 = _M0L11src__offsetS47 + _M0L1iS51;
        float _M0L6_2atmpS1037;
        int32_t _M0L6_2atmpS1039;
        if (
          _M0L6_2atmpS1038 < 0
          || _M0L6_2atmpS1038 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1037 = (float)_M0L3srcS45[_M0L6_2atmpS1038];
        if (
          _M0L6_2atmpS1036 < 0
          || _M0L6_2atmpS1036 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS44[_M0L6_2atmpS1036] = _M0L6_2atmpS1037;
        _M0L6_2atmpS1039 = _M0L1iS51 - 1;
        _M0L1iS51 = _M0L6_2atmpS1039;
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
        int32_t _M0L6_2atmpS1041 = _M0L11dst__offsetS55 + _M0L1iS57;
        int32_t _M0L6_2atmpS1043 = _M0L11src__offsetS56 + _M0L1iS57;
        int32_t _M0L6_2atmpS1042;
        int32_t _M0L6_2atmpS1044;
        if (
          _M0L6_2atmpS1043 < 0
          || _M0L6_2atmpS1043 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1042 = (int32_t)_M0L3srcS54[_M0L6_2atmpS1043];
        if (
          _M0L6_2atmpS1041 < 0
          || _M0L6_2atmpS1041 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS53[_M0L6_2atmpS1041] = _M0L6_2atmpS1042;
        _M0L6_2atmpS1044 = _M0L1iS57 + 1;
        _M0L1iS57 = _M0L6_2atmpS1044;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS54);
        moonbit_decref_cycle_free(_M0L3dstS53);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1049 = _M0L3lenS58 - 1;
    int32_t _M0L1iS60 = _M0L6_2atmpS1049;
    while (1) {
      if (_M0L1iS60 >= 0) {
        int32_t _M0L6_2atmpS1045 = _M0L11dst__offsetS55 + _M0L1iS60;
        int32_t _M0L6_2atmpS1047 = _M0L11src__offsetS56 + _M0L1iS60;
        int32_t _M0L6_2atmpS1046;
        int32_t _M0L6_2atmpS1048;
        if (
          _M0L6_2atmpS1047 < 0
          || _M0L6_2atmpS1047 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1046 = (int32_t)_M0L3srcS54[_M0L6_2atmpS1047];
        if (
          _M0L6_2atmpS1045 < 0
          || _M0L6_2atmpS1045 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS53[_M0L6_2atmpS1045] = _M0L6_2atmpS1046;
        _M0L6_2atmpS1048 = _M0L1iS60 - 1;
        _M0L1iS60 = _M0L6_2atmpS1048;
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS975) {
  switch (Moonbit_object_tag(_M0L4_2aeS975)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_30.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS975);
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_31.data;
      break;
    }
    
    case 4: {
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
  void* _M0L11_2aobj__ptrS1000,
  struct _M0TPB4Show _M0L8_2aparamS999
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS998 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1000;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS998, _M0L8_2aparamS999);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS997,
  struct _M0TPB4Show _M0L8_2aparamS996
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS995 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS997;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS995, _M0L8_2aparamS996);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS994,
  int32_t _M0L8_2aparamS993
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS992 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS994;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS992, _M0L8_2aparamS993);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS991,
  struct _M0TPC16string10StringView _M0L8_2aparamS990
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS989 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS991;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS989, _M0L8_2aparamS990);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS988,
  moonbit_string_t _M0L8_2aparamS985,
  int32_t _M0L8_2aparamS986,
  int32_t _M0L8_2aparamS987
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS984 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS988;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS984, _M0L8_2aparamS985, _M0L8_2aparamS986, _M0L8_2aparamS987);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS983,
  moonbit_string_t _M0L8_2aparamS982
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS981 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS983;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS981, _M0L8_2aparamS982);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2095 = 9218868437227405311ll;
  int64_t _tmp_2096;
  int64_t _tmp_2097;
  int64_t _tmp_2098;
  int64_t _tmp_2099;
  _M0FPB18double__max__value = *(double*)&_tmp_2095;
  _tmp_2096 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2096;
  _tmp_2097 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2097;
  _tmp_2098 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2098;
  _tmp_2099 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2099;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1004;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS968;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS969;
  int32_t _M0L7_2abindS970;
  struct _M0TUsiE** _M0L7_2abindS971;
  int32_t _M0L6_2acntS1950;
  int32_t _M0L2__S972;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1004
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS968
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS968)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 90, 0);
  _M0L12async__testsS968->$0 = _M0L6_2atmpS1004;
  _M0L12async__testsS968->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS969
  = _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS970 = _M0L7_2abindS969->$1;
  _M0L7_2abindS971 = _M0L7_2abindS969->$0;
  _M0L6_2acntS1950
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS969));
  if (_M0L6_2acntS1950 > 1) {
    int32_t _M0L11_2anew__cntS1951 = _M0L6_2acntS1950 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS969), _M0L11_2anew__cntS1951);
    moonbit_incref_cycle_free(_M0L7_2abindS971);
  } else if (_M0L6_2acntS1950 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS969);
  }
  _M0L2__S972 = 0;
  while (1) {
    if (_M0L2__S972 < _M0L7_2abindS970) {
      struct _M0TUsiE* _M0L3argS973 =
        (struct _M0TUsiE*)_M0L7_2abindS971[_M0L2__S972];
      moonbit_string_t _M0L6_2atmpS1001 = _M0L3argS973->$0;
      int32_t _M0L6_2atmpS1002 = _M0L3argS973->$1;
      int32_t _M0L6_2atmpS1003;
      moonbit_incref_cycle_free(_M0L6_2atmpS1001);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples25cuba__net__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS968, _M0L6_2atmpS1001, _M0L6_2atmpS1002);
      moonbit_decref_cycle_free(_M0L6_2atmpS1001);
      _M0L6_2atmpS1003 = _M0L2__S972 + 1;
      _M0L2__S972 = _M0L6_2atmpS1003;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS971);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\cuba_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples25cuba__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25cuba__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS968);
  moonbit_decref_cycle_free(_M0L12async__testsS968);
  moonbit_flush_cycles();
  return 0;
}