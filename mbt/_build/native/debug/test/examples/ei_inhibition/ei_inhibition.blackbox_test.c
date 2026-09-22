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

struct _M0TP26RiantR8snn__mbt15HetRecParameter;

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

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

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__;

struct _M0TP26RiantR8snn__mbt11HHParameter;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt13STDPVariables;

struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet;

struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric;

struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE;

struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0TP26RiantR8snn__mbt11IZParameter;

struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__;

struct _M0TUdiE;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__;

struct _M0TP26RiantR8snn__mbt9STDPEntry;

struct _M0TP26RiantR8snn__mbt9IstdpRate;

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables;

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep;

struct _M0BTPB6Logger;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

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

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TP26RiantR8snn__mbt4Time {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGiE* $1;
  float $2;
  
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

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1731(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1724(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1719(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1696(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1689(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP46RiantR8snn__mbt8examples14ei__inhibition7run__ei(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  float
);

int32_t _M0FP46RiantR8snn__mbt8examples14ei__inhibition12run__e__only(
  struct _M0TP26RiantR8snn__mbt2IF*,
  float
);

int32_t _M0FP26RiantR8snn__mbt23heterogeneous__sim__for(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*,
  float
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

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter8with__el(
  float
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

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

double _M0FPC14math2ln(double);

#define _M0FPC14math3cos cos

#define _M0FPC14math3sin sin

struct _M0TUdiE* _M0FPC14math5frexp(double);

struct _M0TUdiE* _M0FPC14math9normalize(double);

int32_t _M0MPC15float5Float7is__nan(float);

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

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  int32_t
);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*
);

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*
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
} const moonbit_string_literal_32 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[118]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 117, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 101, 105, 95, 105, 110, 104, 105, 
    98, 105, 116, 105, 111, 110, 95, 98, 108, 97, 99, 107, 98, 111, 120, 
    95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 
    114, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[120]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 119, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 101, 105, 95, 105, 110, 104, 105, 
    98, 105, 116, 105, 111, 110, 95, 98, 108, 97, 99, 107, 98, 111, 120, 
    95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 
    101, 115, 116, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1731$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1731
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[123] =
  {
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719, $1)
    / 4
    * 2,
    sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724)
    / 4, 1,
    offsetof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__) / 4, 1,
    offsetof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE) / 4, 
    1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE) / 4, 
    1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

float _M0FP26RiantR8snn__mbt2ms = 0x1p+0f;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS4916
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1752,
  moonbit_string_t _M0L8filenameS1721,
  int32_t _M0L5indexS1723
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719* _closure_5125;
  struct _M0TWEu* _M0L13handle__startS1719;
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724* _closure_5126;
  struct _M0TWssbEu* _M0L14handle__resultS1724;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1731;
  void* _M0L11_2atry__errS1746;
  struct moonbit_result_0 _tmp_5128;
  int32_t _handle__error__result_5129;
  int32_t _M0L6_2atmpS4904;
  void* _M0L3errS1747;
  moonbit_string_t _M0L4nameS1749;
  struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1750;
  moonbit_string_t _M0L7_2anameS1751;
  int32_t _M0L6_2acntS4947;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1721);
  _closure_5125
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719));
  Moonbit_object_header(_closure_5125)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_5125->code
  = &_M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1719;
  _closure_5125->$0 = _M0L5indexS1723;
  _closure_5125->$1 = _M0L8filenameS1721;
  _M0L13handle__startS1719 = (struct _M0TWEu*)_closure_5125;
  moonbit_incref_cycle_free(_M0L8filenameS1721);
  _closure_5126
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724*)moonbit_malloc(sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724));
  Moonbit_object_header(_closure_5126)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_5126->code
  = &_M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1724;
  _closure_5126->$0 = _M0L5indexS1723;
  _closure_5126->$1 = _M0L8filenameS1721;
  _M0L14handle__resultS1724 = (struct _M0TWssbEu*)_closure_5126;
  _M0L17error__to__stringS1731
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1731$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _tmp_5128
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1752, _M0L8filenameS1721, _M0L5indexS1723, _M0L13handle__startS1719, _M0L14handle__resultS1724, _M0L17error__to__stringS1731);
  if (_tmp_5128.tag) {
    int32_t const _M0L5_2aokS4913 = _tmp_5128.data.ok;
    _handle__error__result_5129 = _M0L5_2aokS4913;
  } else {
    void* const _M0L6_2aerrS4914 = _tmp_5128.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
    moonbit_decref_cycle_free(_M0L13handle__startS1719);
    _M0L11_2atry__errS1746 = _M0L6_2aerrS4914;
    goto join_1745;
  }
  if (_handle__error__result_5129) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
    moonbit_decref_cycle_free(_M0L13handle__startS1719);
    _M0L6_2atmpS4904 = 1;
  } else {
    struct moonbit_result_0 _tmp_5130;
    int32_t _handle__error__result_5131;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
    _tmp_5130
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1752, _M0L8filenameS1721, _M0L5indexS1723, _M0L13handle__startS1719, _M0L14handle__resultS1724, _M0L17error__to__stringS1731);
    if (_tmp_5130.tag) {
      int32_t const _M0L5_2aokS4911 = _tmp_5130.data.ok;
      _handle__error__result_5131 = _M0L5_2aokS4911;
    } else {
      void* const _M0L6_2aerrS4912 = _tmp_5130.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
      moonbit_decref_cycle_free(_M0L13handle__startS1719);
      _M0L11_2atry__errS1746 = _M0L6_2aerrS4912;
      goto join_1745;
    }
    if (_handle__error__result_5131) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
      moonbit_decref_cycle_free(_M0L13handle__startS1719);
      _M0L6_2atmpS4904 = 1;
    } else {
      struct moonbit_result_0 _tmp_5132;
      int32_t _handle__error__result_5133;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _tmp_5132
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1752, _M0L8filenameS1721, _M0L5indexS1723, _M0L13handle__startS1719, _M0L14handle__resultS1724, _M0L17error__to__stringS1731);
      if (_tmp_5132.tag) {
        int32_t const _M0L5_2aokS4909 = _tmp_5132.data.ok;
        _handle__error__result_5133 = _M0L5_2aokS4909;
      } else {
        void* const _M0L6_2aerrS4910 = _tmp_5132.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
        moonbit_decref_cycle_free(_M0L13handle__startS1719);
        _M0L11_2atry__errS1746 = _M0L6_2aerrS4910;
        goto join_1745;
      }
      if (_handle__error__result_5133) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
        moonbit_decref_cycle_free(_M0L13handle__startS1719);
        _M0L6_2atmpS4904 = 1;
      } else {
        struct moonbit_result_0 _tmp_5134;
        int32_t _handle__error__result_5135;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
        _tmp_5134
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1752, _M0L8filenameS1721, _M0L5indexS1723, _M0L13handle__startS1719, _M0L14handle__resultS1724, _M0L17error__to__stringS1731);
        if (_tmp_5134.tag) {
          int32_t const _M0L5_2aokS4907 = _tmp_5134.data.ok;
          _handle__error__result_5135 = _M0L5_2aokS4907;
        } else {
          void* const _M0L6_2aerrS4908 = _tmp_5134.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
          moonbit_decref_cycle_free(_M0L13handle__startS1719);
          _M0L11_2atry__errS1746 = _M0L6_2aerrS4908;
          goto join_1745;
        }
        if (_handle__error__result_5135) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
          moonbit_decref_cycle_free(_M0L13handle__startS1719);
          _M0L6_2atmpS4904 = 1;
        } else {
          struct moonbit_result_0 _tmp_5136;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
          _tmp_5136
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1752, _M0L8filenameS1721, _M0L5indexS1723, _M0L13handle__startS1719, _M0L14handle__resultS1724, _M0L17error__to__stringS1731);
          moonbit_decref_cycle_free(_M0L13handle__startS1719);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1731);
          if (_tmp_5136.tag) {
            int32_t const _M0L5_2aokS4905 = _tmp_5136.data.ok;
            _M0L6_2atmpS4904 = _M0L5_2aokS4905;
          } else {
            void* const _M0L6_2aerrS4906 = _tmp_5136.data.err;
            _M0L11_2atry__errS1746 = _M0L6_2aerrS4906;
            goto join_1745;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS4904) {
    void* _M0L133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS4915 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS4915)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS4915)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1746
    = _M0L133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS4915;
    goto join_1745;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1724);
  }
  goto joinlet_5127;
  join_1745:;
  _M0L3errS1747 = _M0L11_2atry__errS1746;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1750
  = (struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1747;
  _M0L7_2anameS1751 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1750->$0;
  _M0L6_2acntS4947
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1750));
  if (_M0L6_2acntS4947 > 1) {
    int32_t _M0L11_2anew__cntS4948 = _M0L6_2acntS4947 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1750), _M0L11_2anew__cntS4948);
    moonbit_incref_cycle_free(_M0L7_2anameS1751);
  } else if (_M0L6_2acntS4947 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1750);
  }
  _M0L4nameS1749 = _M0L7_2anameS1751;
  goto join_1748;
  goto joinlet_5137;
  join_1748:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1724(_M0L14handle__resultS1724, _M0L4nameS1749, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1724);
  moonbit_decref_cycle_free(_M0L4nameS1749);
  joinlet_5137:;
  joinlet_5127:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1731(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS4903,
  void* _M0L3errS1732
) {
  void* _M0L1eS1734;
  moonbit_string_t _M0L1eS1736;
  moonbit_string_t _result_5140;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1732)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1737 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1732;
      moonbit_string_t _M0L4_2aeS1738 = _M0L10_2aFailureS1737->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1738);
      _M0L1eS1736 = _M0L4_2aeS1738;
      goto join_1735;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1739 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1732;
      moonbit_string_t _M0L4_2aeS1740 = _M0L15_2aInspectErrorS1739->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1740);
      _M0L1eS1736 = _M0L4_2aeS1740;
      goto join_1735;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1741 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1732;
      moonbit_string_t _M0L4_2aeS1742 = _M0L16_2aSnapshotErrorS1741->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1742);
      _M0L1eS1736 = _M0L4_2aeS1742;
      goto join_1735;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1743 =
        (struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1732;
      moonbit_string_t _M0L4_2aeS1744 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1743->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1744);
      _M0L1eS1736 = _M0L4_2aeS1744;
      goto join_1735;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1732);
      _M0L1eS1734 = _M0L3errS1732;
      goto join_1733;
      break;
    }
  }
  join_1735:;
  return _M0L1eS1736;
  join_1733:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _result_5140 = _M0FP15Error10to__string(_M0L1eS1734);
  moonbit_decref_cycle_free(_M0L1eS1734);
  return _result_5140;
}

int32_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1724(
  struct _M0TWssbEu* _M0L6_2aenvS4900,
  moonbit_string_t _M0L10__testnameS1725,
  moonbit_string_t _M0L7messageS1726,
  int32_t _M0L7skippedS1727
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724* _M0L14_2acasted__envS4901;
  moonbit_string_t _M0L8filenameS1721;
  int32_t _M0L5indexS1723;
  moonbit_string_t _M0L10file__nameS1728;
  moonbit_string_t _M0L7messageS1729;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1730;
  moonbit_string_t _M0L6_2atmpS4902;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS4901
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1724*)_M0L6_2aenvS4900;
  _M0L8filenameS1721 = _M0L14_2acasted__envS4901->$1;
  _M0L5indexS1723 = _M0L14_2acasted__envS4901->$0;
  if (!_M0L7skippedS1727 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1728
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1721, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1729
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1726, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1730
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1730, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1730, _M0L10file__nameS1728);
  moonbit_decref_cycle_free(_M0L10file__nameS1728);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1730, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1730, _M0L5indexS1723);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1730, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1730, _M0L7messageS1729);
  moonbit_decref_cycle_free(_M0L7messageS1729);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1730, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS4902
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1730);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1730);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS4902);
  moonbit_decref_cycle_free(_M0L6_2atmpS4902);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1719(
  struct _M0TWEu* _M0L6_2aenvS4897
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719* _M0L14_2acasted__envS4898;
  moonbit_string_t _M0L8filenameS1721;
  int32_t _M0L5indexS1723;
  moonbit_string_t _M0L10file__nameS1720;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1722;
  moonbit_string_t _M0L6_2atmpS4899;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS4898
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fei__inhibition__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1719*)_M0L6_2aenvS4897;
  _M0L8filenameS1721 = _M0L14_2acasted__envS4898->$1;
  _M0L5indexS1723 = _M0L14_2acasted__envS4898->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1720
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1721, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1722
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1722, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1722, _M0L10file__nameS1720);
  moonbit_decref_cycle_free(_M0L10file__nameS1720);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1722, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1722, _M0L5indexS1723);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1722, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS4899
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1722);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1722);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS4899);
  moonbit_decref_cycle_free(_M0L6_2atmpS4899);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1689;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1696;
  struct _M0TUsiE** _M0L6_2atmpS4896;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1703;
  moonbit_string_t* _M0L9cli__argsS1704;
  moonbit_string_t _M0L6_2atmpS4895;
  moonbit_string_t _M0L6_2atmpS4894;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1705;
  int32_t _M0L7_2abindS1706;
  moonbit_string_t* _M0L7_2abindS1707;
  int32_t _M0L6_2acntS4949;
  int32_t _M0L2__S1708;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1689 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1696 = 0;
  _M0L6_2atmpS4896 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1703
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1703)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1703->$0 = _M0L6_2atmpS4896;
  _M0L16file__and__indexS1703->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1704
  = _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1704)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS4895 = (moonbit_string_t)_M0L9cli__argsS1704[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS4895);
  moonbit_decref_cycle_free(_M0L9cli__argsS1704);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS4894
  = _M0MP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS4895);
  moonbit_decref_cycle_free(_M0L6_2atmpS4895);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1705
  = _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1696(_M0L51moonbit__test__driver__internal__split__mbt__stringS1696, _M0L6_2atmpS4894, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS4894);
  _M0L7_2abindS1706 = _M0L10test__argsS1705->$1;
  _M0L7_2abindS1707 = _M0L10test__argsS1705->$0;
  _M0L6_2acntS4949
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1705));
  if (_M0L6_2acntS4949 > 1) {
    int32_t _M0L11_2anew__cntS4950 = _M0L6_2acntS4949 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1705), _M0L11_2anew__cntS4950);
    moonbit_incref_cycle_free(_M0L7_2abindS1707);
  } else if (_M0L6_2acntS4949 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1705);
  }
  _M0L2__S1708 = 0;
  while (1) {
    if (_M0L2__S1708 < _M0L7_2abindS1706) {
      moonbit_string_t _M0L3argS1709 =
        (moonbit_string_t)_M0L7_2abindS1707[_M0L2__S1708];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1710;
      moonbit_string_t _M0L4fileS1711;
      moonbit_string_t _M0L5rangeS1712;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1713;
      moonbit_string_t _M0L6_2atmpS4892;
      int32_t _M0L5startS1714;
      moonbit_string_t _M0L6_2atmpS4891;
      int32_t _M0L3endS1715;
      int32_t _M0L1iS1716;
      int32_t _M0L6_2atmpS4893;
      moonbit_incref_cycle_free(_M0L3argS1709);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1710
      = _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1696(_M0L51moonbit__test__driver__internal__split__mbt__stringS1696, _M0L3argS1709, 58);
      moonbit_decref_cycle_free(_M0L3argS1709);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1711
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1710, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1712
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1710, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1710);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1713
      = _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1696(_M0L51moonbit__test__driver__internal__split__mbt__stringS1696, _M0L5rangeS1712, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1712);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS4892
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1713, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1714
      = _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1689(_M0L45moonbit__test__driver__internal__parse__int__S1689, _M0L6_2atmpS4892);
      moonbit_decref_cycle_free(_M0L6_2atmpS4892);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS4891
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1713, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1713);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1715
      = _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1689(_M0L45moonbit__test__driver__internal__parse__int__S1689, _M0L6_2atmpS4891);
      moonbit_decref_cycle_free(_M0L6_2atmpS4891);
      _M0L1iS1716 = _M0L5startS1714;
      while (1) {
        if (_M0L1iS1716 < _M0L3endS1715) {
          struct _M0TUsiE* _M0L8_2atupleS4889;
          int32_t _M0L6_2atmpS4890;
          moonbit_incref_cycle_free(_M0L4fileS1711);
          _M0L8_2atupleS4889
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS4889)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS4889->$0 = _M0L4fileS1711;
          _M0L8_2atupleS4889->$1 = _M0L1iS1716;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1703, _M0L8_2atupleS4889);
          _M0L6_2atmpS4890 = _M0L1iS1716 + 1;
          _M0L1iS1716 = _M0L6_2atmpS4890;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1711);
        }
        break;
      }
      _M0L6_2atmpS4893 = _M0L2__S1708 + 1;
      _M0L2__S1708 = _M0L6_2atmpS4893;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1707);
    }
    break;
  }
  return _M0L16file__and__indexS1703;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1696(
  int32_t _M0L6_2aenvS4870,
  moonbit_string_t _M0L1sS1697,
  int32_t _M0L3sepS1698
) {
  moonbit_string_t* _M0L6_2atmpS4888;
  struct _M0TPB5ArrayGsE* _M0L3resS1699;
  struct _M0TPB8MutLocalGiE* _M0L1iS1700;
  struct _M0TPB8MutLocalGiE* _M0L5startS1701;
  int32_t _M0L3valS4883;
  int32_t _M0L6_2atmpS4884;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS4888 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1699
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1699)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1699->$0 = _M0L6_2atmpS4888;
  _M0L3resS1699->$1 = 0;
  _M0L1iS1700
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1700)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1700->$0 = 0;
  _M0L5startS1701
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1701)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1701->$0 = 0;
  while (1) {
    int32_t _M0L3valS4871 = _M0L1iS1700->$0;
    int32_t _M0L6_2atmpS4872 = Moonbit_array_length(_M0L1sS1697);
    if (_M0L3valS4871 < _M0L6_2atmpS4872) {
      int32_t _M0L3valS4875 = _M0L1iS1700->$0;
      int32_t _M0L6_2atmpS4874;
      int32_t _M0L6_2atmpS4873;
      int32_t _M0L3valS4882;
      int32_t _M0L6_2atmpS4881;
      if (
        _M0L3valS4875 < 0
        || _M0L3valS4875 >= Moonbit_array_length(_M0L1sS1697)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS4874 = _M0L1sS1697[_M0L3valS4875];
      _M0L6_2atmpS4873 = _M0L6_2atmpS4874;
      if (_M0L6_2atmpS4873 == _M0L3sepS1698) {
        int32_t _M0L3valS4877 = _M0L5startS1701->$0;
        int32_t _M0L3valS4878 = _M0L1iS1700->$0;
        moonbit_string_t _M0L6_2atmpS4876;
        int32_t _M0L3valS4880;
        int32_t _M0L6_2atmpS4879;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS4876
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1697, _M0L3valS4877, _M0L3valS4878);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1699, _M0L6_2atmpS4876);
        _M0L3valS4880 = _M0L1iS1700->$0;
        _M0L6_2atmpS4879 = _M0L3valS4880 + 1;
        _M0L5startS1701->$0 = _M0L6_2atmpS4879;
      }
      _M0L3valS4882 = _M0L1iS1700->$0;
      _M0L6_2atmpS4881 = _M0L3valS4882 + 1;
      _M0L1iS1700->$0 = _M0L6_2atmpS4881;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1700);
    }
    break;
  }
  _M0L3valS4883 = _M0L5startS1701->$0;
  _M0L6_2atmpS4884 = Moonbit_array_length(_M0L1sS1697);
  if (_M0L3valS4883 < _M0L6_2atmpS4884) {
    int32_t _M0L3valS4886 = _M0L5startS1701->$0;
    int32_t _M0L6_2atmpS4887;
    moonbit_string_t _M0L6_2atmpS4885;
    moonbit_decref_cycle_free(_M0L5startS1701);
    _M0L6_2atmpS4887 = Moonbit_array_length(_M0L1sS1697);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS4885
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1697, _M0L3valS4886, _M0L6_2atmpS4887);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1699, _M0L6_2atmpS4885);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1701);
  }
  return _M0L3resS1699;
}

int32_t _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1689(
  int32_t _M0L6_2aenvS4863,
  moonbit_string_t _M0L1sS1690
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1691;
  int32_t _M0L3lenS1692;
  int32_t _M0L7_2abindS1693;
  int32_t _M0L1iS1694;
  int32_t _result_5145;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1691
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1691)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1691->$0 = 0;
  _M0L3lenS1692 = Moonbit_array_length(_M0L1sS1690);
  _M0L7_2abindS1693 = 0;
  _M0L1iS1694 = _M0L7_2abindS1693;
  while (1) {
    if (_M0L1iS1694 < _M0L3lenS1692) {
      int32_t _M0L3valS4868 = _M0L3resS1691->$0;
      int32_t _M0L6_2atmpS4865 = _M0L3valS4868 * 10;
      int32_t _M0L6_2atmpS4867;
      int32_t _M0L6_2atmpS4866;
      int32_t _M0L6_2atmpS4864;
      int32_t _M0L6_2atmpS4869;
      if (
        _M0L1iS1694 < 0 || _M0L1iS1694 >= Moonbit_array_length(_M0L1sS1690)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS4867 = _M0L1sS1690[_M0L1iS1694];
      _M0L6_2atmpS4866 = _M0L6_2atmpS4867 - 48;
      _M0L6_2atmpS4864 = _M0L6_2atmpS4865 + _M0L6_2atmpS4866;
      _M0L3resS1691->$0 = _M0L6_2atmpS4864;
      _M0L6_2atmpS4869 = _M0L1iS1694 + 1;
      _M0L1iS1694 = _M0L6_2atmpS4869;
      continue;
    }
    break;
  }
  _result_5145 = _M0L3resS1691->$0;
  moonbit_decref_cycle_free(_M0L3resS1691);
  return _result_5145;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1688
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1688);
  return _M0L4selfS1688;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1658,
  moonbit_string_t _M0L12_2adiscard__S1659,
  int32_t _M0L12_2adiscard__S1660,
  struct _M0TWEu* _M0L12_2adiscard__S1661,
  struct _M0TWssbEu* _M0L12_2adiscard__S1662,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1663
) {
  struct moonbit_result_0 _result_5146;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _result_5146.tag = 1;
  _result_5146.data.ok = 0;
  return _result_5146;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1664,
  moonbit_string_t _M0L12_2adiscard__S1665,
  int32_t _M0L12_2adiscard__S1666,
  struct _M0TWEu* _M0L12_2adiscard__S1667,
  struct _M0TWssbEu* _M0L12_2adiscard__S1668,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1669
) {
  struct moonbit_result_0 _result_5147;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _result_5147.tag = 1;
  _result_5147.data.ok = 0;
  return _result_5147;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1670,
  moonbit_string_t _M0L12_2adiscard__S1671,
  int32_t _M0L12_2adiscard__S1672,
  struct _M0TWEu* _M0L12_2adiscard__S1673,
  struct _M0TWssbEu* _M0L12_2adiscard__S1674,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1675
) {
  struct moonbit_result_0 _result_5148;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _result_5148.tag = 1;
  _result_5148.data.ok = 0;
  return _result_5148;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1676,
  moonbit_string_t _M0L12_2adiscard__S1677,
  int32_t _M0L12_2adiscard__S1678,
  struct _M0TWEu* _M0L12_2adiscard__S1679,
  struct _M0TWssbEu* _M0L12_2adiscard__S1680,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1681
) {
  struct moonbit_result_0 _result_5149;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _result_5149.tag = 1;
  _result_5149.data.ok = 0;
  return _result_5149;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1682,
  moonbit_string_t _M0L12_2adiscard__S1683,
  int32_t _M0L12_2adiscard__S1684,
  struct _M0TWEu* _M0L12_2adiscard__S1685,
  struct _M0TWssbEu* _M0L12_2adiscard__S1686,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1687
) {
  struct moonbit_result_0 _result_5150;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _result_5150.tag = 1;
  _result_5150.data.ok = 0;
  return _result_5150;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1657
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples14ei__inhibition7run__ei(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1eS1641,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1iS1643,
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L2eiS1644,
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L2ieS1645,
  float _M0L12duration__msS1646
) {
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L2fmS1640;
  void* _M0L4IF__S4861;
  void* _M0L4IF__S4862;
  void** _M0L6_2atmpS4860;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L6_2atmpS4851;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS4859;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L6_2atmpS4852;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L6_2atmpS4853;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS4858;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L6_2atmpS4857;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L6_2atmpS4854;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L6_2atmpS4855;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L6_2atmpS4856;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L5modelS1642;
  float _M0L6_2atmpS4850;
  int32_t _result_5151;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  #line 42 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  _M0L2fmS1640 = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L1eS1641, 0);
  moonbit_incref_cycle_free(_M0L1eS1641);
  _M0L4IF__S4861
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__));
  Moonbit_object_header(_M0L4IF__S4861)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  ((struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L4IF__S4861)->$0
  = _M0L1eS1641;
  moonbit_incref_cycle_free(_M0L1iS1643);
  _M0L4IF__S4862
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__));
  Moonbit_object_header(_M0L4IF__S4862)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  ((struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L4IF__S4862)->$0
  = _M0L1iS1643;
  _M0L6_2atmpS4860 = (void**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS4860[0] = _M0L4IF__S4861;
  _M0L6_2atmpS4860[1] = _M0L4IF__S4862;
  _M0L6_2atmpS4851
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE));
  Moonbit_object_header(_M0L6_2atmpS4851)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS4851->$0 = _M0L6_2atmpS4860;
  _M0L6_2atmpS4851->$1 = 2;
  moonbit_incref_cycle_free(_M0L2eiS1644);
  moonbit_incref_cycle_free(_M0L2ieS1645);
  _M0L6_2atmpS4859
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS4859[0] = _M0L2eiS1644;
  _M0L6_2atmpS4859[1] = _M0L2ieS1645;
  _M0L6_2atmpS4852
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE));
  Moonbit_object_header(_M0L6_2atmpS4852)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _M0L6_2atmpS4852->$0 = _M0L6_2atmpS4859;
  _M0L6_2atmpS4852->$1 = 2;
  _M0L6_2atmpS4853 = 0;
  moonbit_incref_cycle_free(_M0L2fmS1640);
  _M0L6_2atmpS4858
  = (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_make_ref_array_raw(1);
  _M0L6_2atmpS4858[0] = _M0L2fmS1640;
  _M0L6_2atmpS4857
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
  Moonbit_object_header(_M0L6_2atmpS4857)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
  _M0L6_2atmpS4857->$0 = _M0L6_2atmpS4858;
  _M0L6_2atmpS4857->$1 = 1;
  _M0L6_2atmpS4854 = _M0L6_2atmpS4857;
  _M0L6_2atmpS4855 = 0;
  _M0L6_2atmpS4856 = 0;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  _M0L5modelS1642
  = _M0FP26RiantR8snn__mbt7compose(_M0L6_2atmpS4851, _M0L6_2atmpS4852, _M0L6_2atmpS4853, _M0L6_2atmpS4854, _M0L6_2atmpS4855, _M0L6_2atmpS4856);
  moonbit_decref_cycle_free(_M0L6_2atmpS4851);
  moonbit_decref_cycle_free(_M0L6_2atmpS4852);
  if (_M0L6_2atmpS4853) {
    moonbit_decref_cycle_free(_M0L6_2atmpS4853);
  }
  if (_M0L6_2atmpS4854) {
    moonbit_decref_cycle_free(_M0L6_2atmpS4854);
  }
  if (_M0L6_2atmpS4855) {
    moonbit_decref_cycle_free(_M0L6_2atmpS4855);
  }
  if (_M0L6_2atmpS4856) {
    moonbit_decref_cycle_free(_M0L6_2atmpS4856);
  }
  _M0L6_2atmpS4850 = _M0L12duration__msS1646 * _M0FP26RiantR8snn__mbt2ms;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  _M0FP26RiantR8snn__mbt23heterogeneous__sim__for(_M0L5modelS1642, _M0L6_2atmpS4850);
  moonbit_decref_cycle_free(_M0L5modelS1642);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  _result_5151 = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L2fmS1640);
  moonbit_decref_cycle_free(_M0L2fmS1640);
  return _result_5151;
}

int32_t _M0FP46RiantR8snn__mbt8examples14ei__inhibition12run__e__only(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1eS1637,
  float _M0L12duration__msS1639
) {
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L2fmS1636;
  void* _M0L4IF__S4849;
  void** _M0L6_2atmpS4848;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L6_2atmpS4839;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS4847;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L6_2atmpS4840;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L6_2atmpS4841;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS4846;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L6_2atmpS4845;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L6_2atmpS4842;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L6_2atmpS4843;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L6_2atmpS4844;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L5modelS1638;
  float _M0L6_2atmpS4838;
  int32_t _result_5152;
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  _M0L2fmS1636 = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L1eS1637, 0);
  moonbit_incref_cycle_free(_M0L1eS1637);
  _M0L4IF__S4849
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__));
  Moonbit_object_header(_M0L4IF__S4849)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  ((struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L4IF__S4849)->$0
  = _M0L1eS1637;
  _M0L6_2atmpS4848 = (void**)moonbit_make_ref_array_raw(1);
  _M0L6_2atmpS4848[0] = _M0L4IF__S4849;
  _M0L6_2atmpS4839
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE));
  Moonbit_object_header(_M0L6_2atmpS4839)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS4839->$0 = _M0L6_2atmpS4848;
  _M0L6_2atmpS4839->$1 = 1;
  _M0L6_2atmpS4847
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse**)moonbit_empty_ref_array;
  _M0L6_2atmpS4840
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE));
  Moonbit_object_header(_M0L6_2atmpS4840)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _M0L6_2atmpS4840->$0 = _M0L6_2atmpS4847;
  _M0L6_2atmpS4840->$1 = 0;
  _M0L6_2atmpS4841 = 0;
  moonbit_incref_cycle_free(_M0L2fmS1636);
  _M0L6_2atmpS4846
  = (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_make_ref_array_raw(1);
  _M0L6_2atmpS4846[0] = _M0L2fmS1636;
  _M0L6_2atmpS4845
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
  Moonbit_object_header(_M0L6_2atmpS4845)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
  _M0L6_2atmpS4845->$0 = _M0L6_2atmpS4846;
  _M0L6_2atmpS4845->$1 = 1;
  _M0L6_2atmpS4842 = _M0L6_2atmpS4845;
  _M0L6_2atmpS4843 = 0;
  _M0L6_2atmpS4844 = 0;
  #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  _M0L5modelS1638
  = _M0FP26RiantR8snn__mbt7compose(_M0L6_2atmpS4839, _M0L6_2atmpS4840, _M0L6_2atmpS4841, _M0L6_2atmpS4842, _M0L6_2atmpS4843, _M0L6_2atmpS4844);
  moonbit_decref_cycle_free(_M0L6_2atmpS4839);
  moonbit_decref_cycle_free(_M0L6_2atmpS4840);
  if (_M0L6_2atmpS4841) {
    moonbit_decref_cycle_free(_M0L6_2atmpS4841);
  }
  if (_M0L6_2atmpS4842) {
    moonbit_decref_cycle_free(_M0L6_2atmpS4842);
  }
  if (_M0L6_2atmpS4843) {
    moonbit_decref_cycle_free(_M0L6_2atmpS4843);
  }
  if (_M0L6_2atmpS4844) {
    moonbit_decref_cycle_free(_M0L6_2atmpS4844);
  }
  _M0L6_2atmpS4838 = _M0L12duration__msS1639 * _M0FP26RiantR8snn__mbt2ms;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  _M0FP26RiantR8snn__mbt23heterogeneous__sim__for(_M0L5modelS1638, _M0L6_2atmpS4838);
  moonbit_decref_cycle_free(_M0L5modelS1638);
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\main.mbt"
  _result_5152 = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L2fmS1636);
  moonbit_decref_cycle_free(_M0L2fmS1636);
  return _result_5152;
}

int32_t _M0FP26RiantR8snn__mbt23heterogeneous__sim__for(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1634,
  float _M0L8durationS1631
) {
  float _M0L2dtS1629;
  float _M0L6_2atmpS4837;
  int32_t _M0L5stepsS1630;
  int32_t _M0L7_2abindS1632;
  int32_t _M0L2__S1633;
  #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L2dtS1629 = 0x1p-3f;
  _M0L6_2atmpS4837 = _M0L8durationS1631 / _M0L2dtS1629;
  #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L5stepsS1630 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4837);
  _M0L7_2abindS1632 = 0;
  _M0L2__S1633 = _M0L7_2abindS1632;
  while (1) {
    if (_M0L2__S1633 < _M0L5stepsS1630) {
      int32_t _M0L6_2atmpS4836;
      #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt19step__heterogeneous(_M0L1mS1634, _M0L2dtS1629);
      _M0L6_2atmpS4836 = _M0L2__S1633 + 1;
      _M0L2__S1633 = _M0L6_2atmpS4836;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1531,
  float _M0L2dtS1536
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2abindS1530;
  int32_t _M0L7_2abindS1532;
  void** _M0L7_2abindS1533;
  int32_t _M0L2__S1534;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1538;
  int32_t _M0L7_2abindS1539;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1540;
  int32_t _M0L2__S1541;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2abindS1544;
  int32_t _M0L7_2abindS1545;
  void** _M0L7_2abindS1546;
  int32_t _M0L2__S1547;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1565;
  int32_t _M0L7_2abindS1566;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1567;
  int32_t _M0L2__S1568;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2abindS1571;
  int32_t _M0L7_2abindS1572;
  void** _M0L7_2abindS1573;
  int32_t _M0L2__S1574;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L7_2abindS1617;
  int32_t _M0L7_2abindS1618;
  void** _M0L7_2abindS1619;
  int32_t _M0L2__S1620;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1623;
  int32_t _M0L7_2abindS1624;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1625;
  int32_t _M0L2__S1626;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4835;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L7_2abindS1530 = _M0L1mS1531->$2;
  _M0L7_2abindS1532 = _M0L7_2abindS1530->$1;
  _M0L7_2abindS1533 = _M0L7_2abindS1530->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1533);
  _M0L2__S1534 = 0;
  while (1) {
    if (_M0L2__S1534 < _M0L7_2abindS1532) {
      void* _M0L1sS1535 = (void*)_M0L7_2abindS1533[_M0L2__S1534];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4644 = _M0L1mS1531->$3;
      int32_t _M0L6_2atmpS4645;
      moonbit_incref_cycle_free(_M0L1sS1535);
      #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14stimulate__any(_M0L1sS1535, _M0L4timeS4644, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L1sS1535);
      _M0L6_2atmpS4645 = _M0L2__S1534 + 1;
      _M0L2__S1534 = _M0L6_2atmpS4645;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1533);
    }
    break;
  }
  _M0L7_2abindS1538 = _M0L1mS1531->$1;
  _M0L7_2abindS1539 = _M0L7_2abindS1538->$1;
  _M0L7_2abindS1540 = _M0L7_2abindS1538->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1540);
  _M0L2__S1541 = 0;
  while (1) {
    if (_M0L2__S1541 < _M0L7_2abindS1539) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1542 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1540[
          _M0L2__S1541
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4647 = _M0L1mS1531->$3;
      float _M0L6_2atmpS4646;
      int32_t _M0L6_2atmpS4648;
      moonbit_incref_cycle_free(_M0L1cS1542);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4646 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4647);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1542, _M0L6_2atmpS4646);
      moonbit_decref_cycle_free(_M0L1cS1542);
      _M0L6_2atmpS4648 = _M0L2__S1541 + 1;
      _M0L2__S1541 = _M0L6_2atmpS4648;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1540);
    }
    break;
  }
  _M0L7_2abindS1544 = _M0L1mS1531->$6;
  _M0L7_2abindS1545 = _M0L7_2abindS1544->$1;
  _M0L7_2abindS1546 = _M0L7_2abindS1544->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1546);
  _M0L2__S1547 = 0;
  while (1) {
    if (_M0L2__S1547 < _M0L7_2abindS1545) {
      void* _M0L5entryS1548 = (void*)_M0L7_2abindS1546[_M0L2__S1547];
      struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L1eS1550;
      struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L1eS1553;
      struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L1eS1556;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4665;
      int32_t _M0L11conn__indexS4666;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1557;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS4661;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS4662;
      int32_t _M0L6_2acntS4955;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4664;
      float _M0L6_2atmpS4663;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4659;
      int32_t _M0L11conn__indexS4660;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1554;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS4655;
      struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS4656;
      int32_t _M0L6_2acntS4953;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4658;
      float _M0L6_2atmpS4657;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4653;
      int32_t _M0L11conn__indexS4654;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1551;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS4649;
      struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS4650;
      int32_t _M0L6_2acntS4951;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4652;
      float _M0L6_2atmpS4651;
      int32_t _M0L6_2atmpS4667;
      switch (Moonbit_object_tag(_M0L5entryS1548)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__* _M0L15_2aMarkramSTP__S1558 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__*)_M0L5entryS1548;
          struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L4_2aeS1559 =
            _M0L15_2aMarkramSTP__S1558->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1559);
          _M0L1eS1556 = _M0L4_2aeS1559;
          goto join_1555;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__* _M0L18_2aMarkramSTPHet__S1560 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__*)_M0L5entryS1548;
          struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L4_2aeS1561 =
            _M0L18_2aMarkramSTPHet__S1560->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1561);
          _M0L1eS1553 = _M0L4_2aeS1561;
          goto join_1552;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__* _M0L23_2aMarkramSTPTimestep__S1562 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__*)_M0L5entryS1548;
          struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L4_2aeS1563 =
            _M0L23_2aMarkramSTPTimestep__S1562->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1563);
          _M0L1eS1550 = _M0L4_2aeS1563;
          goto join_1549;
          break;
        }
      }
      goto joinlet_5159;
      join_1555:;
      _M0L5connsS4665 = _M0L1mS1531->$1;
      _M0L11conn__indexS4666 = _M0L1eS1556->$0;
      #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1557
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4665, _M0L11conn__indexS4666);
      _M0L4varsS4661 = _M0L1eS1556->$1;
      _M0L5paramS4662 = _M0L1eS1556->$2;
      _M0L6_2acntS4955 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1556));
      if (_M0L6_2acntS4955 > 1) {
        int32_t _M0L11_2anew__cntS4956 = _M0L6_2acntS4955 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1556), _M0L11_2anew__cntS4956);
        moonbit_incref_cycle_free(_M0L5paramS4662);
        moonbit_incref_cycle_free(_M0L4varsS4661);
      } else if (_M0L6_2acntS4955 == 1) {
        #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1556);
      }
      _M0L4timeS4664 = _M0L1mS1531->$3;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4663 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4664);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt18markram__stp__step(_M0L3synS1557, _M0L4varsS4661, _M0L5paramS4662, _M0L6_2atmpS4663);
      moonbit_decref_cycle_free(_M0L3synS1557);
      moonbit_decref_cycle_free(_M0L4varsS4661);
      moonbit_decref_cycle_free(_M0L5paramS4662);
      joinlet_5159:;
      goto joinlet_5158;
      join_1552:;
      _M0L5connsS4659 = _M0L1mS1531->$1;
      _M0L11conn__indexS4660 = _M0L1eS1553->$0;
      #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1554
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4659, _M0L11conn__indexS4660);
      _M0L4varsS4655 = _M0L1eS1553->$1;
      _M0L5paramS4656 = _M0L1eS1553->$2;
      _M0L6_2acntS4953 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1553));
      if (_M0L6_2acntS4953 > 1) {
        int32_t _M0L11_2anew__cntS4954 = _M0L6_2acntS4953 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1553), _M0L11_2anew__cntS4954);
        moonbit_incref_cycle_free(_M0L5paramS4656);
        moonbit_incref_cycle_free(_M0L4varsS4655);
      } else if (_M0L6_2acntS4953 == 1) {
        #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1553);
      }
      _M0L4timeS4658 = _M0L1mS1531->$3;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4657 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4658);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt23markram__stp__step__het(_M0L3synS1554, _M0L4varsS4655, _M0L5paramS4656, _M0L6_2atmpS4657);
      moonbit_decref_cycle_free(_M0L3synS1554);
      moonbit_decref_cycle_free(_M0L4varsS4655);
      moonbit_decref_cycle_free(_M0L5paramS4656);
      joinlet_5158:;
      goto joinlet_5157;
      join_1549:;
      _M0L5connsS4653 = _M0L1mS1531->$1;
      _M0L11conn__indexS4654 = _M0L1eS1550->$0;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1551
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4653, _M0L11conn__indexS4654);
      _M0L4varsS4649 = _M0L1eS1550->$1;
      _M0L5paramS4650 = _M0L1eS1550->$2;
      _M0L6_2acntS4951 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1550));
      if (_M0L6_2acntS4951 > 1) {
        int32_t _M0L11_2anew__cntS4952 = _M0L6_2acntS4951 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1550), _M0L11_2anew__cntS4952);
        moonbit_incref_cycle_free(_M0L5paramS4650);
        moonbit_incref_cycle_free(_M0L4varsS4649);
      } else if (_M0L6_2acntS4951 == 1) {
        #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1550);
      }
      _M0L4timeS4652 = _M0L1mS1531->$3;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4651 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4652);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(_M0L3synS1551, _M0L4varsS4649, _M0L5paramS4650, _M0L6_2atmpS4651, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L3synS1551);
      moonbit_decref_cycle_free(_M0L4varsS4649);
      moonbit_decref_cycle_free(_M0L5paramS4650);
      joinlet_5157:;
      _M0L6_2atmpS4667 = _M0L2__S1547 + 1;
      _M0L2__S1547 = _M0L6_2atmpS4667;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1546);
    }
    break;
  }
  _M0L7_2abindS1565 = _M0L1mS1531->$1;
  _M0L7_2abindS1566 = _M0L7_2abindS1565->$1;
  _M0L7_2abindS1567 = _M0L7_2abindS1565->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1567);
  _M0L2__S1568 = 0;
  while (1) {
    if (_M0L2__S1568 < _M0L7_2abindS1566) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1569 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1567[
          _M0L2__S1568
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4669 = _M0L1mS1531->$3;
      float _M0L6_2atmpS4668;
      int32_t _M0L6_2atmpS4670;
      moonbit_incref_cycle_free(_M0L1cS1569);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4668 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4669);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1569, _M0L6_2atmpS4668);
      moonbit_decref_cycle_free(_M0L1cS1569);
      _M0L6_2atmpS4670 = _M0L2__S1568 + 1;
      _M0L2__S1568 = _M0L6_2atmpS4670;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1567);
    }
    break;
  }
  _M0L7_2abindS1571 = _M0L1mS1531->$5;
  _M0L7_2abindS1572 = _M0L7_2abindS1571->$1;
  _M0L7_2abindS1573 = _M0L7_2abindS1571->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1573);
  _M0L2__S1574 = 0;
  while (1) {
    if (_M0L2__S1574 < _M0L7_2abindS1572) {
      void* _M0L5entryS1575 = (void*)_M0L7_2abindS1573[_M0L2__S1574];
      struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L1eS1577;
      struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L1eS1580;
      struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L1eS1583;
      struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L1eS1586;
      struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L1eS1589;
      struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L1eS1592;
      struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L1eS1595;
      struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L1eS1598;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4828;
      int32_t _M0L11conn__indexS4829;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1599;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4823;
      struct _M0TPB5ArrayGfE* _M0L4valsS4810;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4822;
      struct _M0TPB5ArrayGbE* _M0L4fireS4811;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4821;
      struct _M0TPB5ArrayGbE* _M0L4fireS4812;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4820;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4813;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4819;
      int32_t _M0L6_2acntS5104;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4814;
      int32_t _M0L6_2acntS5115;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS4815;
      struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS4816;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4818;
      float _M0L6_2atmpS4817;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4824;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4827;
      int32_t _M0L6_2acntS5119;
      float _M0L6_2atmpS4826;
      float _M0L6_2atmpS4825;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4808;
      int32_t _M0L11conn__indexS4809;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1596;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4803;
      struct _M0TPB5ArrayGfE* _M0L4valsS4791;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4802;
      struct _M0TPB5ArrayGbE* _M0L4fireS4792;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4801;
      struct _M0TPB5ArrayGbE* _M0L4fireS4793;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4800;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4794;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4799;
      int32_t _M0L6_2acntS5084;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4795;
      int32_t _M0L6_2acntS5095;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4796;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4797;
      struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS4798;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4804;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4807;
      int32_t _M0L6_2acntS5099;
      float _M0L6_2atmpS4806;
      float _M0L6_2atmpS4805;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4789;
      int32_t _M0L11conn__indexS4790;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1593;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4784;
      struct _M0TPB5ArrayGfE* _M0L4valsS4773;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4783;
      struct _M0TPB5ArrayGbE* _M0L4fireS4774;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4782;
      struct _M0TPB5ArrayGbE* _M0L4fireS4775;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4781;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4776;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4780;
      int32_t _M0L6_2acntS5065;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4777;
      int32_t _M0L6_2acntS5076;
      struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS4778;
      struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS4779;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4785;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4788;
      int32_t _M0L6_2acntS5080;
      float _M0L6_2atmpS4787;
      float _M0L6_2atmpS4786;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4771;
      int32_t _M0L11conn__indexS4772;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1590;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4766;
      struct _M0TPB5ArrayGfE* _M0L4valsS4753;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4765;
      struct _M0TPB5ArrayGbE* _M0L4fireS4754;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4764;
      struct _M0TPB5ArrayGbE* _M0L4fireS4755;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4763;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4756;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4762;
      int32_t _M0L6_2acntS5046;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4757;
      int32_t _M0L6_2acntS5057;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS4758;
      struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS4759;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4761;
      float _M0L6_2atmpS4760;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4767;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4770;
      int32_t _M0L6_2acntS5061;
      float _M0L6_2atmpS4769;
      float _M0L6_2atmpS4768;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4751;
      int32_t _M0L11conn__indexS4752;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1587;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4746;
      struct _M0TPB5ArrayGfE* _M0L4valsS4733;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4745;
      struct _M0TPB5ArrayGbE* _M0L4fireS4734;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4744;
      struct _M0TPB5ArrayGbE* _M0L4fireS4735;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4743;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4736;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4742;
      int32_t _M0L6_2acntS5027;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4737;
      int32_t _M0L6_2acntS5038;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS4738;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS4739;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4741;
      float _M0L6_2atmpS4740;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4747;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4750;
      int32_t _M0L6_2acntS5042;
      float _M0L6_2atmpS4749;
      float _M0L6_2atmpS4748;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4731;
      int32_t _M0L11conn__indexS4732;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1584;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4726;
      struct _M0TPB5ArrayGfE* _M0L4valsS4711;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4725;
      struct _M0TPB5ArrayGbE* _M0L4fireS4712;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4724;
      struct _M0TPB5ArrayGbE* _M0L4fireS4713;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4723;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4714;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4722;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4715;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4721;
      int32_t _M0L6_2acntS4995;
      struct _M0TPB5ArrayGfE* _M0L1vS4716;
      int32_t _M0L6_2acntS5006;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS4717;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS4718;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4720;
      float _M0L6_2atmpS4719;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4727;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4730;
      int32_t _M0L6_2acntS5023;
      float _M0L6_2atmpS4729;
      float _M0L6_2atmpS4728;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4709;
      int32_t _M0L11conn__indexS4710;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1581;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4704;
      struct _M0TPB5ArrayGfE* _M0L4valsS4691;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4703;
      struct _M0TPB5ArrayGbE* _M0L4fireS4692;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4702;
      struct _M0TPB5ArrayGbE* _M0L4fireS4693;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4701;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4694;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4700;
      int32_t _M0L6_2acntS4976;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4695;
      int32_t _M0L6_2acntS4987;
      struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS4696;
      struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS4697;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4699;
      float _M0L6_2atmpS4698;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4705;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4708;
      int32_t _M0L6_2acntS4991;
      float _M0L6_2atmpS4707;
      float _M0L6_2atmpS4706;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4689;
      int32_t _M0L11conn__indexS4690;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1578;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4684;
      struct _M0TPB5ArrayGfE* _M0L4valsS4671;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4683;
      struct _M0TPB5ArrayGbE* _M0L4fireS4672;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4682;
      struct _M0TPB5ArrayGbE* _M0L4fireS4673;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4681;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4674;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4680;
      int32_t _M0L6_2acntS4957;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4675;
      int32_t _M0L6_2acntS4968;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS4676;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS4677;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4679;
      float _M0L6_2atmpS4678;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4685;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4688;
      int32_t _M0L6_2acntS4972;
      float _M0L6_2atmpS4687;
      float _M0L6_2atmpS4686;
      int32_t _M0L6_2atmpS4830;
      switch (Moonbit_object_tag(_M0L5entryS1575)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__* _M0L13_2aGerstner__S1600 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L5entryS1575;
          struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L4_2aeS1601 =
            _M0L13_2aGerstner__S1600->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1601);
          _M0L1eS1598 = _M0L4_2aeS1601;
          goto join_1597;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__* _M0L15_2aMexicanHat__S1602 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__*)_M0L5entryS1575;
          struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L4_2aeS1603 =
            _M0L15_2aMexicanHat__S1602->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1603);
          _M0L1eS1595 = _M0L4_2aeS1603;
          goto join_1594;
          break;
        }
        
        case 2: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__* _M0L18_2aAntiSymmetric__S1604 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__*)_M0L5entryS1575;
          struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L4_2aeS1605 =
            _M0L18_2aAntiSymmetric__S1604->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1605);
          _M0L1eS1592 = _M0L4_2aeS1605;
          goto join_1591;
          break;
        }
        
        case 3: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__* _M0L19_2aConfavreux2025__S1606 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__*)_M0L5entryS1575;
          struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L4_2aeS1607 =
            _M0L19_2aConfavreux2025__S1606->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1607);
          _M0L1eS1589 = _M0L4_2aeS1607;
          goto join_1588;
          break;
        }
        
        case 4: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__* _M0L14_2aIstdpRate__S1608 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__*)_M0L5entryS1575;
          struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L4_2aeS1609 =
            _M0L14_2aIstdpRate__S1608->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1609);
          _M0L1eS1586 = _M0L4_2aeS1609;
          goto join_1585;
          break;
        }
        
        case 5: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__* _M0L19_2aIstdpPotential__S1610 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__*)_M0L5entryS1575;
          struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L4_2aeS1611 =
            _M0L19_2aIstdpPotential__S1610->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1611);
          _M0L1eS1583 = _M0L4_2aeS1611;
          goto join_1582;
          break;
        }
        
        case 6: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__* _M0L14_2aSymmetric__S1612 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__*)_M0L5entryS1575;
          struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L4_2aeS1613 =
            _M0L14_2aSymmetric__S1612->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1613);
          _M0L1eS1580 = _M0L4_2aeS1613;
          goto join_1579;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__* _M0L17_2aCaPlasticity__S1614 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__*)_M0L5entryS1575;
          struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L4_2aeS1615 =
            _M0L17_2aCaPlasticity__S1614->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1615);
          _M0L1eS1577 = _M0L4_2aeS1615;
          goto join_1576;
          break;
        }
      }
      goto joinlet_5169;
      join_1597:;
      _M0L5connsS4828 = _M0L1mS1531->$1;
      _M0L11conn__indexS4829 = _M0L1eS1598->$0;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1599
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4828, _M0L11conn__indexS4829);
      _M0L6matrixS4823 = _M0L3synS1599->$4;
      _M0L4valsS4810 = _M0L6matrixS4823->$4;
      _M0L3preS4822 = _M0L3synS1599->$0;
      _M0L4fireS4811 = _M0L3preS4822->$5;
      _M0L4postS4821 = _M0L3synS1599->$1;
      _M0L4fireS4812 = _M0L4postS4821->$5;
      _M0L6matrixS4820 = _M0L3synS1599->$4;
      _M0L6colptrS4813 = _M0L6matrixS4820->$3;
      _M0L6matrixS4819 = _M0L3synS1599->$4;
      moonbit_incref_cycle_free(_M0L6colptrS4813);
      moonbit_incref_cycle_free(_M0L4fireS4812);
      moonbit_incref_cycle_free(_M0L4fireS4811);
      moonbit_incref_cycle_free(_M0L4valsS4810);
      _M0L6_2acntS5104
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1599));
      if (_M0L6_2acntS5104 > 1) {
        int32_t _M0L11_2anew__cntS5114 = _M0L6_2acntS5104 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1599), _M0L11_2anew__cntS5114);
        moonbit_incref_cycle_free(_M0L6matrixS4819);
      } else if (_M0L6_2acntS5104 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5113 = _M0L3synS1599->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5112;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5111;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5110;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5109;
        moonbit_string_t _M0L8_2afieldS5108;
        moonbit_string_t _M0L8_2afieldS5107;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5106;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5105;
        moonbit_decref_cycle_free(_M0L8_2afieldS5113);
        _M0L8_2afieldS5112 = _M0L3synS1599->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5112);
        _M0L8_2afieldS5111 = _M0L3synS1599->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5111);
        _M0L8_2afieldS5110 = _M0L3synS1599->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5110);
        _M0L8_2afieldS5109 = _M0L3synS1599->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5109);
        _M0L8_2afieldS5108 = _M0L3synS1599->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5108);
        _M0L8_2afieldS5107 = _M0L3synS1599->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5107);
        _M0L8_2afieldS5106 = _M0L3synS1599->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5106);
        _M0L8_2afieldS5105 = _M0L3synS1599->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5105);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1599);
      }
      _M0L6rowptrS4814 = _M0L6matrixS4819->$2;
      _M0L6_2acntS5115
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS4819));
      if (_M0L6_2acntS5115 > 1) {
        int32_t _M0L11_2anew__cntS5118 = _M0L6_2acntS5115 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS4819), _M0L11_2anew__cntS5118);
        moonbit_incref_cycle_free(_M0L6rowptrS4814);
      } else if (_M0L6_2acntS5115 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5117 = _M0L6matrixS4819->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5116;
        moonbit_decref_cycle_free(_M0L8_2afieldS5117);
        _M0L8_2afieldS5116 = _M0L6matrixS4819->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5116);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS4819);
      }
      _M0L4varsS4815 = _M0L1eS1598->$3;
      _M0L5paramS4816 = _M0L1eS1598->$4;
      _M0L6t__nowS4818 = _M0L1eS1598->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4818);
      moonbit_incref_cycle_free(_M0L5paramS4816);
      moonbit_incref_cycle_free(_M0L4varsS4815);
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4817 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4818, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4818);
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt10stdp__step(_M0L4valsS4810, _M0L4fireS4811, _M0L4fireS4812, _M0L6colptrS4813, _M0L6rowptrS4814, _M0L4varsS4815, _M0L5paramS4816, _M0L6_2atmpS4817, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L4valsS4810);
      moonbit_decref_cycle_free(_M0L4fireS4811);
      moonbit_decref_cycle_free(_M0L4fireS4812);
      moonbit_decref_cycle_free(_M0L6colptrS4813);
      moonbit_decref_cycle_free(_M0L6rowptrS4814);
      moonbit_decref_cycle_free(_M0L4varsS4815);
      moonbit_decref_cycle_free(_M0L5paramS4816);
      _M0L6t__nowS4824 = _M0L1eS1598->$5;
      _M0L6t__nowS4827 = _M0L1eS1598->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4824);
      _M0L6_2acntS5119 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1598));
      if (_M0L6_2acntS5119 > 1) {
        int32_t _M0L11_2anew__cntS5122 = _M0L6_2acntS5119 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1598), _M0L11_2anew__cntS5122);
        moonbit_incref_cycle_free(_M0L6t__nowS4827);
      } else if (_M0L6_2acntS5119 == 1) {
        struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L8_2afieldS5121 =
          _M0L1eS1598->$4;
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5120;
        moonbit_decref_cycle_free(_M0L8_2afieldS5121);
        _M0L8_2afieldS5120 = _M0L1eS1598->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5120);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1598);
      }
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4826 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4827, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4827);
      _M0L6_2atmpS4825 = _M0L6_2atmpS4826 + _M0L2dtS1536;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4824, 0, _M0L6_2atmpS4825);
      moonbit_decref_cycle_free(_M0L6t__nowS4824);
      joinlet_5169:;
      goto joinlet_5168;
      join_1594:;
      _M0L5connsS4808 = _M0L1mS1531->$1;
      _M0L11conn__indexS4809 = _M0L1eS1595->$0;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1596
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4808, _M0L11conn__indexS4809);
      _M0L6matrixS4803 = _M0L3synS1596->$4;
      _M0L4valsS4791 = _M0L6matrixS4803->$4;
      _M0L3preS4802 = _M0L3synS1596->$0;
      _M0L4fireS4792 = _M0L3preS4802->$5;
      _M0L4postS4801 = _M0L3synS1596->$1;
      _M0L4fireS4793 = _M0L4postS4801->$5;
      _M0L6matrixS4800 = _M0L3synS1596->$4;
      _M0L6colptrS4794 = _M0L6matrixS4800->$3;
      _M0L6matrixS4799 = _M0L3synS1596->$4;
      moonbit_incref_cycle_free(_M0L6colptrS4794);
      moonbit_incref_cycle_free(_M0L4fireS4793);
      moonbit_incref_cycle_free(_M0L4fireS4792);
      moonbit_incref_cycle_free(_M0L4valsS4791);
      _M0L6_2acntS5084
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1596));
      if (_M0L6_2acntS5084 > 1) {
        int32_t _M0L11_2anew__cntS5094 = _M0L6_2acntS5084 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1596), _M0L11_2anew__cntS5094);
        moonbit_incref_cycle_free(_M0L6matrixS4799);
      } else if (_M0L6_2acntS5084 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5093 = _M0L3synS1596->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5092;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5091;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5090;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5089;
        moonbit_string_t _M0L8_2afieldS5088;
        moonbit_string_t _M0L8_2afieldS5087;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5086;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5085;
        moonbit_decref_cycle_free(_M0L8_2afieldS5093);
        _M0L8_2afieldS5092 = _M0L3synS1596->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5092);
        _M0L8_2afieldS5091 = _M0L3synS1596->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5091);
        _M0L8_2afieldS5090 = _M0L3synS1596->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5090);
        _M0L8_2afieldS5089 = _M0L3synS1596->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5089);
        _M0L8_2afieldS5088 = _M0L3synS1596->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5088);
        _M0L8_2afieldS5087 = _M0L3synS1596->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5087);
        _M0L8_2afieldS5086 = _M0L3synS1596->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5086);
        _M0L8_2afieldS5085 = _M0L3synS1596->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5085);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1596);
      }
      _M0L6rowptrS4795 = _M0L6matrixS4799->$2;
      _M0L6_2acntS5095
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS4799));
      if (_M0L6_2acntS5095 > 1) {
        int32_t _M0L11_2anew__cntS5098 = _M0L6_2acntS5095 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS4799), _M0L11_2anew__cntS5098);
        moonbit_incref_cycle_free(_M0L6rowptrS4795);
      } else if (_M0L6_2acntS5095 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5097 = _M0L6matrixS4799->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5096;
        moonbit_decref_cycle_free(_M0L8_2afieldS5097);
        _M0L8_2afieldS5096 = _M0L6matrixS4799->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5096);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS4799);
      }
      _M0L4tpreS4796 = _M0L1eS1595->$4;
      _M0L5tpostS4797 = _M0L1eS1595->$5;
      _M0L5paramS4798 = _M0L1eS1595->$3;
      moonbit_incref_cycle_free(_M0L5paramS4798);
      moonbit_incref_cycle_free(_M0L5tpostS4797);
      moonbit_incref_cycle_free(_M0L4tpreS4796);
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(_M0L4valsS4791, _M0L4fireS4792, _M0L4fireS4793, _M0L6colptrS4794, _M0L6rowptrS4795, _M0L4tpreS4796, _M0L5tpostS4797, _M0L5paramS4798, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L4valsS4791);
      moonbit_decref_cycle_free(_M0L4fireS4792);
      moonbit_decref_cycle_free(_M0L4fireS4793);
      moonbit_decref_cycle_free(_M0L6colptrS4794);
      moonbit_decref_cycle_free(_M0L6rowptrS4795);
      moonbit_decref_cycle_free(_M0L4tpreS4796);
      moonbit_decref_cycle_free(_M0L5tpostS4797);
      moonbit_decref_cycle_free(_M0L5paramS4798);
      _M0L6t__nowS4804 = _M0L1eS1595->$6;
      _M0L6t__nowS4807 = _M0L1eS1595->$6;
      moonbit_incref_cycle_free(_M0L6t__nowS4804);
      _M0L6_2acntS5099 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1595));
      if (_M0L6_2acntS5099 > 1) {
        int32_t _M0L11_2anew__cntS5103 = _M0L6_2acntS5099 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1595), _M0L11_2anew__cntS5103);
        moonbit_incref_cycle_free(_M0L6t__nowS4807);
      } else if (_M0L6_2acntS5099 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5102 = _M0L1eS1595->$5;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5101;
        struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L8_2afieldS5100;
        moonbit_decref_cycle_free(_M0L8_2afieldS5102);
        _M0L8_2afieldS5101 = _M0L1eS1595->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5101);
        _M0L8_2afieldS5100 = _M0L1eS1595->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5100);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1595);
      }
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4806 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4807, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4807);
      _M0L6_2atmpS4805 = _M0L6_2atmpS4806 + _M0L2dtS1536;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4804, 0, _M0L6_2atmpS4805);
      moonbit_decref_cycle_free(_M0L6t__nowS4804);
      joinlet_5168:;
      goto joinlet_5167;
      join_1591:;
      _M0L5connsS4789 = _M0L1mS1531->$1;
      _M0L11conn__indexS4790 = _M0L1eS1592->$0;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1593
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4789, _M0L11conn__indexS4790);
      _M0L6matrixS4784 = _M0L3synS1593->$4;
      _M0L4valsS4773 = _M0L6matrixS4784->$4;
      _M0L3preS4783 = _M0L3synS1593->$0;
      _M0L4fireS4774 = _M0L3preS4783->$5;
      _M0L4postS4782 = _M0L3synS1593->$1;
      _M0L4fireS4775 = _M0L4postS4782->$5;
      _M0L6matrixS4781 = _M0L3synS1593->$4;
      _M0L6colptrS4776 = _M0L6matrixS4781->$3;
      _M0L6matrixS4780 = _M0L3synS1593->$4;
      moonbit_incref_cycle_free(_M0L6colptrS4776);
      moonbit_incref_cycle_free(_M0L4fireS4775);
      moonbit_incref_cycle_free(_M0L4fireS4774);
      moonbit_incref_cycle_free(_M0L4valsS4773);
      _M0L6_2acntS5065
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1593));
      if (_M0L6_2acntS5065 > 1) {
        int32_t _M0L11_2anew__cntS5075 = _M0L6_2acntS5065 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1593), _M0L11_2anew__cntS5075);
        moonbit_incref_cycle_free(_M0L6matrixS4780);
      } else if (_M0L6_2acntS5065 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5074 = _M0L3synS1593->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5073;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5072;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5071;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5070;
        moonbit_string_t _M0L8_2afieldS5069;
        moonbit_string_t _M0L8_2afieldS5068;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5067;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5066;
        moonbit_decref_cycle_free(_M0L8_2afieldS5074);
        _M0L8_2afieldS5073 = _M0L3synS1593->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5073);
        _M0L8_2afieldS5072 = _M0L3synS1593->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5072);
        _M0L8_2afieldS5071 = _M0L3synS1593->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5071);
        _M0L8_2afieldS5070 = _M0L3synS1593->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5070);
        _M0L8_2afieldS5069 = _M0L3synS1593->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5069);
        _M0L8_2afieldS5068 = _M0L3synS1593->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5068);
        _M0L8_2afieldS5067 = _M0L3synS1593->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5067);
        _M0L8_2afieldS5066 = _M0L3synS1593->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5066);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1593);
      }
      _M0L6rowptrS4777 = _M0L6matrixS4780->$2;
      _M0L6_2acntS5076
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS4780));
      if (_M0L6_2acntS5076 > 1) {
        int32_t _M0L11_2anew__cntS5079 = _M0L6_2acntS5076 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS4780), _M0L11_2anew__cntS5079);
        moonbit_incref_cycle_free(_M0L6rowptrS4777);
      } else if (_M0L6_2acntS5076 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5078 = _M0L6matrixS4780->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5077;
        moonbit_decref_cycle_free(_M0L8_2afieldS5078);
        _M0L8_2afieldS5077 = _M0L6matrixS4780->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5077);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS4780);
      }
      _M0L4varsS4778 = _M0L1eS1592->$4;
      _M0L5paramS4779 = _M0L1eS1592->$3;
      moonbit_incref_cycle_free(_M0L5paramS4779);
      moonbit_incref_cycle_free(_M0L4varsS4778);
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(_M0L4valsS4773, _M0L4fireS4774, _M0L4fireS4775, _M0L6colptrS4776, _M0L6rowptrS4777, _M0L4varsS4778, _M0L5paramS4779, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L4valsS4773);
      moonbit_decref_cycle_free(_M0L4fireS4774);
      moonbit_decref_cycle_free(_M0L4fireS4775);
      moonbit_decref_cycle_free(_M0L6colptrS4776);
      moonbit_decref_cycle_free(_M0L6rowptrS4777);
      moonbit_decref_cycle_free(_M0L4varsS4778);
      moonbit_decref_cycle_free(_M0L5paramS4779);
      _M0L6t__nowS4785 = _M0L1eS1592->$5;
      _M0L6t__nowS4788 = _M0L1eS1592->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4785);
      _M0L6_2acntS5080 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1592));
      if (_M0L6_2acntS5080 > 1) {
        int32_t _M0L11_2anew__cntS5083 = _M0L6_2acntS5080 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1592), _M0L11_2anew__cntS5083);
        moonbit_incref_cycle_free(_M0L6t__nowS4788);
      } else if (_M0L6_2acntS5080 == 1) {
        struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L8_2afieldS5082 =
          _M0L1eS1592->$4;
        struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L8_2afieldS5081;
        moonbit_decref_cycle_free(_M0L8_2afieldS5082);
        _M0L8_2afieldS5081 = _M0L1eS1592->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5081);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1592);
      }
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4787 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4788, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4788);
      _M0L6_2atmpS4786 = _M0L6_2atmpS4787 + _M0L2dtS1536;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4785, 0, _M0L6_2atmpS4786);
      moonbit_decref_cycle_free(_M0L6t__nowS4785);
      joinlet_5167:;
      goto joinlet_5166;
      join_1588:;
      _M0L5connsS4771 = _M0L1mS1531->$1;
      _M0L11conn__indexS4772 = _M0L1eS1589->$0;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1590
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4771, _M0L11conn__indexS4772);
      _M0L6matrixS4766 = _M0L3synS1590->$4;
      _M0L4valsS4753 = _M0L6matrixS4766->$4;
      _M0L3preS4765 = _M0L3synS1590->$0;
      _M0L4fireS4754 = _M0L3preS4765->$5;
      _M0L4postS4764 = _M0L3synS1590->$1;
      _M0L4fireS4755 = _M0L4postS4764->$5;
      _M0L6matrixS4763 = _M0L3synS1590->$4;
      _M0L6colptrS4756 = _M0L6matrixS4763->$3;
      _M0L6matrixS4762 = _M0L3synS1590->$4;
      moonbit_incref_cycle_free(_M0L6colptrS4756);
      moonbit_incref_cycle_free(_M0L4fireS4755);
      moonbit_incref_cycle_free(_M0L4fireS4754);
      moonbit_incref_cycle_free(_M0L4valsS4753);
      _M0L6_2acntS5046
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1590));
      if (_M0L6_2acntS5046 > 1) {
        int32_t _M0L11_2anew__cntS5056 = _M0L6_2acntS5046 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1590), _M0L11_2anew__cntS5056);
        moonbit_incref_cycle_free(_M0L6matrixS4762);
      } else if (_M0L6_2acntS5046 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5055 = _M0L3synS1590->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5054;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5053;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5052;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5051;
        moonbit_string_t _M0L8_2afieldS5050;
        moonbit_string_t _M0L8_2afieldS5049;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5048;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5047;
        moonbit_decref_cycle_free(_M0L8_2afieldS5055);
        _M0L8_2afieldS5054 = _M0L3synS1590->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5054);
        _M0L8_2afieldS5053 = _M0L3synS1590->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5053);
        _M0L8_2afieldS5052 = _M0L3synS1590->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5052);
        _M0L8_2afieldS5051 = _M0L3synS1590->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5051);
        _M0L8_2afieldS5050 = _M0L3synS1590->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5050);
        _M0L8_2afieldS5049 = _M0L3synS1590->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5049);
        _M0L8_2afieldS5048 = _M0L3synS1590->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5048);
        _M0L8_2afieldS5047 = _M0L3synS1590->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5047);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1590);
      }
      _M0L6rowptrS4757 = _M0L6matrixS4762->$2;
      _M0L6_2acntS5057
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS4762));
      if (_M0L6_2acntS5057 > 1) {
        int32_t _M0L11_2anew__cntS5060 = _M0L6_2acntS5057 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS4762), _M0L11_2anew__cntS5060);
        moonbit_incref_cycle_free(_M0L6rowptrS4757);
      } else if (_M0L6_2acntS5057 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5059 = _M0L6matrixS4762->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5058;
        moonbit_decref_cycle_free(_M0L8_2afieldS5059);
        _M0L8_2afieldS5058 = _M0L6matrixS4762->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5058);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS4762);
      }
      _M0L4varsS4758 = _M0L1eS1589->$4;
      _M0L5paramS4759 = _M0L1eS1589->$3;
      _M0L6t__nowS4761 = _M0L1eS1589->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4761);
      moonbit_incref_cycle_free(_M0L5paramS4759);
      moonbit_incref_cycle_free(_M0L4varsS4758);
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4760 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4761, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4761);
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22stdp__confavreux__step(_M0L4valsS4753, _M0L4fireS4754, _M0L4fireS4755, _M0L6colptrS4756, _M0L6rowptrS4757, _M0L4varsS4758, _M0L5paramS4759, _M0L6_2atmpS4760, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L4valsS4753);
      moonbit_decref_cycle_free(_M0L4fireS4754);
      moonbit_decref_cycle_free(_M0L4fireS4755);
      moonbit_decref_cycle_free(_M0L6colptrS4756);
      moonbit_decref_cycle_free(_M0L6rowptrS4757);
      moonbit_decref_cycle_free(_M0L4varsS4758);
      moonbit_decref_cycle_free(_M0L5paramS4759);
      _M0L6t__nowS4767 = _M0L1eS1589->$5;
      _M0L6t__nowS4770 = _M0L1eS1589->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4767);
      _M0L6_2acntS5061 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1589));
      if (_M0L6_2acntS5061 > 1) {
        int32_t _M0L11_2anew__cntS5064 = _M0L6_2acntS5061 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1589), _M0L11_2anew__cntS5064);
        moonbit_incref_cycle_free(_M0L6t__nowS4770);
      } else if (_M0L6_2acntS5061 == 1) {
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5063 =
          _M0L1eS1589->$4;
        struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L8_2afieldS5062;
        moonbit_decref_cycle_free(_M0L8_2afieldS5063);
        _M0L8_2afieldS5062 = _M0L1eS1589->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5062);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1589);
      }
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4769 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4770, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4770);
      _M0L6_2atmpS4768 = _M0L6_2atmpS4769 + _M0L2dtS1536;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4767, 0, _M0L6_2atmpS4768);
      moonbit_decref_cycle_free(_M0L6t__nowS4767);
      joinlet_5166:;
      goto joinlet_5165;
      join_1585:;
      _M0L5connsS4751 = _M0L1mS1531->$1;
      _M0L11conn__indexS4752 = _M0L1eS1586->$0;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1587
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4751, _M0L11conn__indexS4752);
      _M0L6matrixS4746 = _M0L3synS1587->$4;
      _M0L4valsS4733 = _M0L6matrixS4746->$4;
      _M0L3preS4745 = _M0L3synS1587->$0;
      _M0L4fireS4734 = _M0L3preS4745->$5;
      _M0L4postS4744 = _M0L3synS1587->$1;
      _M0L4fireS4735 = _M0L4postS4744->$5;
      _M0L6matrixS4743 = _M0L3synS1587->$4;
      _M0L6colptrS4736 = _M0L6matrixS4743->$3;
      _M0L6matrixS4742 = _M0L3synS1587->$4;
      moonbit_incref_cycle_free(_M0L6colptrS4736);
      moonbit_incref_cycle_free(_M0L4fireS4735);
      moonbit_incref_cycle_free(_M0L4fireS4734);
      moonbit_incref_cycle_free(_M0L4valsS4733);
      _M0L6_2acntS5027
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1587));
      if (_M0L6_2acntS5027 > 1) {
        int32_t _M0L11_2anew__cntS5037 = _M0L6_2acntS5027 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1587), _M0L11_2anew__cntS5037);
        moonbit_incref_cycle_free(_M0L6matrixS4742);
      } else if (_M0L6_2acntS5027 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5036 = _M0L3synS1587->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5035;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5034;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5033;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5032;
        moonbit_string_t _M0L8_2afieldS5031;
        moonbit_string_t _M0L8_2afieldS5030;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5029;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5028;
        moonbit_decref_cycle_free(_M0L8_2afieldS5036);
        _M0L8_2afieldS5035 = _M0L3synS1587->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5035);
        _M0L8_2afieldS5034 = _M0L3synS1587->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5034);
        _M0L8_2afieldS5033 = _M0L3synS1587->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5033);
        _M0L8_2afieldS5032 = _M0L3synS1587->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5032);
        _M0L8_2afieldS5031 = _M0L3synS1587->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5031);
        _M0L8_2afieldS5030 = _M0L3synS1587->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5030);
        _M0L8_2afieldS5029 = _M0L3synS1587->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5029);
        _M0L8_2afieldS5028 = _M0L3synS1587->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5028);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1587);
      }
      _M0L6rowptrS4737 = _M0L6matrixS4742->$2;
      _M0L6_2acntS5038
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS4742));
      if (_M0L6_2acntS5038 > 1) {
        int32_t _M0L11_2anew__cntS5041 = _M0L6_2acntS5038 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS4742), _M0L11_2anew__cntS5041);
        moonbit_incref_cycle_free(_M0L6rowptrS4737);
      } else if (_M0L6_2acntS5038 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5040 = _M0L6matrixS4742->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5039;
        moonbit_decref_cycle_free(_M0L8_2afieldS5040);
        _M0L8_2afieldS5039 = _M0L6matrixS4742->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5039);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS4742);
      }
      _M0L4varsS4738 = _M0L1eS1586->$4;
      _M0L5paramS4739 = _M0L1eS1586->$3;
      _M0L6t__nowS4741 = _M0L1eS1586->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4741);
      moonbit_incref_cycle_free(_M0L5paramS4739);
      moonbit_incref_cycle_free(_M0L4varsS4738);
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4740 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4741, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4741);
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS4733, _M0L4fireS4734, _M0L4fireS4735, _M0L6colptrS4736, _M0L6rowptrS4737, _M0L4varsS4738, _M0L5paramS4739, _M0L6_2atmpS4740, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L4valsS4733);
      moonbit_decref_cycle_free(_M0L4fireS4734);
      moonbit_decref_cycle_free(_M0L4fireS4735);
      moonbit_decref_cycle_free(_M0L6colptrS4736);
      moonbit_decref_cycle_free(_M0L6rowptrS4737);
      moonbit_decref_cycle_free(_M0L4varsS4738);
      moonbit_decref_cycle_free(_M0L5paramS4739);
      _M0L6t__nowS4747 = _M0L1eS1586->$5;
      _M0L6t__nowS4750 = _M0L1eS1586->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4747);
      _M0L6_2acntS5042 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1586));
      if (_M0L6_2acntS5042 > 1) {
        int32_t _M0L11_2anew__cntS5045 = _M0L6_2acntS5042 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1586), _M0L11_2anew__cntS5045);
        moonbit_incref_cycle_free(_M0L6t__nowS4750);
      } else if (_M0L6_2acntS5042 == 1) {
        struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L8_2afieldS5044 =
          _M0L1eS1586->$4;
        struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L8_2afieldS5043;
        moonbit_decref_cycle_free(_M0L8_2afieldS5044);
        _M0L8_2afieldS5043 = _M0L1eS1586->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5043);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1586);
      }
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4749 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4750, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4750);
      _M0L6_2atmpS4748 = _M0L6_2atmpS4749 + _M0L2dtS1536;
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4747, 0, _M0L6_2atmpS4748);
      moonbit_decref_cycle_free(_M0L6t__nowS4747);
      joinlet_5165:;
      goto joinlet_5164;
      join_1582:;
      _M0L5connsS4731 = _M0L1mS1531->$1;
      _M0L11conn__indexS4732 = _M0L1eS1583->$0;
      #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1584
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4731, _M0L11conn__indexS4732);
      _M0L6matrixS4726 = _M0L3synS1584->$4;
      _M0L4valsS4711 = _M0L6matrixS4726->$4;
      _M0L3preS4725 = _M0L3synS1584->$0;
      _M0L4fireS4712 = _M0L3preS4725->$5;
      _M0L4postS4724 = _M0L3synS1584->$1;
      _M0L4fireS4713 = _M0L4postS4724->$5;
      _M0L6matrixS4723 = _M0L3synS1584->$4;
      _M0L6colptrS4714 = _M0L6matrixS4723->$3;
      _M0L6matrixS4722 = _M0L3synS1584->$4;
      _M0L6rowptrS4715 = _M0L6matrixS4722->$2;
      _M0L4postS4721 = _M0L3synS1584->$1;
      moonbit_incref_cycle_free(_M0L6rowptrS4715);
      moonbit_incref_cycle_free(_M0L6colptrS4714);
      moonbit_incref_cycle_free(_M0L4fireS4713);
      moonbit_incref_cycle_free(_M0L4fireS4712);
      moonbit_incref_cycle_free(_M0L4valsS4711);
      _M0L6_2acntS4995
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1584));
      if (_M0L6_2acntS4995 > 1) {
        int32_t _M0L11_2anew__cntS5005 = _M0L6_2acntS4995 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1584), _M0L11_2anew__cntS5005);
        moonbit_incref_cycle_free(_M0L4postS4721);
      } else if (_M0L6_2acntS4995 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5004 = _M0L3synS1584->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5003;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5002;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5001;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5000;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L8_2afieldS4999;
        moonbit_string_t _M0L8_2afieldS4998;
        moonbit_string_t _M0L8_2afieldS4997;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS4996;
        moonbit_decref_cycle_free(_M0L8_2afieldS5004);
        _M0L8_2afieldS5003 = _M0L3synS1584->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5003);
        _M0L8_2afieldS5002 = _M0L3synS1584->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5002);
        _M0L8_2afieldS5001 = _M0L3synS1584->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5001);
        _M0L8_2afieldS5000 = _M0L3synS1584->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5000);
        _M0L8_2afieldS4999 = _M0L3synS1584->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS4999);
        _M0L8_2afieldS4998 = _M0L3synS1584->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS4998);
        _M0L8_2afieldS4997 = _M0L3synS1584->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS4997);
        _M0L8_2afieldS4996 = _M0L3synS1584->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS4996);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1584);
      }
      _M0L1vS4716 = _M0L4postS4721->$3;
      _M0L6_2acntS5006
      = Moonbit_rc_count(Moonbit_object_header(_M0L4postS4721));
      if (_M0L6_2acntS5006 > 1) {
        int32_t _M0L11_2anew__cntS5022 = _M0L6_2acntS5006 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L4postS4721), _M0L11_2anew__cntS5022);
        moonbit_incref_cycle_free(_M0L1vS4716);
      } else if (_M0L6_2acntS5006 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5021 = _M0L4postS4721->$16;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5020;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5019;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5018;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5017;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5016;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5015;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5014;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5013;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5012;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5011;
        struct _M0TPB5ArrayGbE* _M0L8_2afieldS5010;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5009;
        struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS5008;
        struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS5007;
        moonbit_decref_cycle_free(_M0L8_2afieldS5021);
        _M0L8_2afieldS5020 = _M0L4postS4721->$15;
        moonbit_decref_cycle_free(_M0L8_2afieldS5020);
        _M0L8_2afieldS5019 = _M0L4postS4721->$14;
        moonbit_decref_cycle_free(_M0L8_2afieldS5019);
        _M0L8_2afieldS5018 = _M0L4postS4721->$13;
        moonbit_decref_cycle_free(_M0L8_2afieldS5018);
        _M0L8_2afieldS5017 = _M0L4postS4721->$12;
        moonbit_decref_cycle_free(_M0L8_2afieldS5017);
        _M0L8_2afieldS5016 = _M0L4postS4721->$11;
        moonbit_decref_cycle_free(_M0L8_2afieldS5016);
        _M0L8_2afieldS5015 = _M0L4postS4721->$10;
        moonbit_decref_cycle_free(_M0L8_2afieldS5015);
        _M0L8_2afieldS5014 = _M0L4postS4721->$9;
        moonbit_decref_cycle_free(_M0L8_2afieldS5014);
        _M0L8_2afieldS5013 = _M0L4postS4721->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5013);
        _M0L8_2afieldS5012 = _M0L4postS4721->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5012);
        _M0L8_2afieldS5011 = _M0L4postS4721->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5011);
        _M0L8_2afieldS5010 = _M0L4postS4721->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5010);
        _M0L8_2afieldS5009 = _M0L4postS4721->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5009);
        _M0L8_2afieldS5008 = _M0L4postS4721->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5008);
        _M0L8_2afieldS5007 = _M0L4postS4721->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5007);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L4postS4721);
      }
      _M0L4varsS4717 = _M0L1eS1583->$4;
      _M0L5paramS4718 = _M0L1eS1583->$3;
      _M0L6t__nowS4720 = _M0L1eS1583->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4720);
      moonbit_incref_cycle_free(_M0L5paramS4718);
      moonbit_incref_cycle_free(_M0L4varsS4717);
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4719 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4720, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4720);
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS4711, _M0L4fireS4712, _M0L4fireS4713, _M0L6colptrS4714, _M0L6rowptrS4715, _M0L1vS4716, _M0L4varsS4717, _M0L5paramS4718, _M0L6_2atmpS4719, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L4valsS4711);
      moonbit_decref_cycle_free(_M0L4fireS4712);
      moonbit_decref_cycle_free(_M0L4fireS4713);
      moonbit_decref_cycle_free(_M0L6colptrS4714);
      moonbit_decref_cycle_free(_M0L6rowptrS4715);
      moonbit_decref_cycle_free(_M0L1vS4716);
      moonbit_decref_cycle_free(_M0L4varsS4717);
      moonbit_decref_cycle_free(_M0L5paramS4718);
      _M0L6t__nowS4727 = _M0L1eS1583->$5;
      _M0L6t__nowS4730 = _M0L1eS1583->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4727);
      _M0L6_2acntS5023 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1583));
      if (_M0L6_2acntS5023 > 1) {
        int32_t _M0L11_2anew__cntS5026 = _M0L6_2acntS5023 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1583), _M0L11_2anew__cntS5026);
        moonbit_incref_cycle_free(_M0L6t__nowS4730);
      } else if (_M0L6_2acntS5023 == 1) {
        struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L8_2afieldS5025 =
          _M0L1eS1583->$4;
        struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L8_2afieldS5024;
        moonbit_decref_cycle_free(_M0L8_2afieldS5025);
        _M0L8_2afieldS5024 = _M0L1eS1583->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5024);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1583);
      }
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4729 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4730, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4730);
      _M0L6_2atmpS4728 = _M0L6_2atmpS4729 + _M0L2dtS1536;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4727, 0, _M0L6_2atmpS4728);
      moonbit_decref_cycle_free(_M0L6t__nowS4727);
      joinlet_5164:;
      goto joinlet_5163;
      join_1579:;
      _M0L5connsS4709 = _M0L1mS1531->$1;
      _M0L11conn__indexS4710 = _M0L1eS1580->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1581
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4709, _M0L11conn__indexS4710);
      _M0L6matrixS4704 = _M0L3synS1581->$4;
      _M0L4valsS4691 = _M0L6matrixS4704->$4;
      _M0L3preS4703 = _M0L3synS1581->$0;
      _M0L4fireS4692 = _M0L3preS4703->$5;
      _M0L4postS4702 = _M0L3synS1581->$1;
      _M0L4fireS4693 = _M0L4postS4702->$5;
      _M0L6matrixS4701 = _M0L3synS1581->$4;
      _M0L6colptrS4694 = _M0L6matrixS4701->$3;
      _M0L6matrixS4700 = _M0L3synS1581->$4;
      moonbit_incref_cycle_free(_M0L6colptrS4694);
      moonbit_incref_cycle_free(_M0L4fireS4693);
      moonbit_incref_cycle_free(_M0L4fireS4692);
      moonbit_incref_cycle_free(_M0L4valsS4691);
      _M0L6_2acntS4976
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1581));
      if (_M0L6_2acntS4976 > 1) {
        int32_t _M0L11_2anew__cntS4986 = _M0L6_2acntS4976 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1581), _M0L11_2anew__cntS4986);
        moonbit_incref_cycle_free(_M0L6matrixS4700);
      } else if (_M0L6_2acntS4976 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4985 = _M0L3synS1581->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS4984;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4983;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4982;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4981;
        moonbit_string_t _M0L8_2afieldS4980;
        moonbit_string_t _M0L8_2afieldS4979;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS4978;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS4977;
        moonbit_decref_cycle_free(_M0L8_2afieldS4985);
        _M0L8_2afieldS4984 = _M0L3synS1581->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS4984);
        _M0L8_2afieldS4983 = _M0L3synS1581->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS4983);
        _M0L8_2afieldS4982 = _M0L3synS1581->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS4982);
        _M0L8_2afieldS4981 = _M0L3synS1581->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS4981);
        _M0L8_2afieldS4980 = _M0L3synS1581->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS4980);
        _M0L8_2afieldS4979 = _M0L3synS1581->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS4979);
        _M0L8_2afieldS4978 = _M0L3synS1581->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS4978);
        _M0L8_2afieldS4977 = _M0L3synS1581->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS4977);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1581);
      }
      _M0L6rowptrS4695 = _M0L6matrixS4700->$2;
      _M0L6_2acntS4987
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS4700));
      if (_M0L6_2acntS4987 > 1) {
        int32_t _M0L11_2anew__cntS4990 = _M0L6_2acntS4987 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS4700), _M0L11_2anew__cntS4990);
        moonbit_incref_cycle_free(_M0L6rowptrS4695);
      } else if (_M0L6_2acntS4987 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4989 = _M0L6matrixS4700->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS4988;
        moonbit_decref_cycle_free(_M0L8_2afieldS4989);
        _M0L8_2afieldS4988 = _M0L6matrixS4700->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS4988);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS4700);
      }
      _M0L4varsS4696 = _M0L1eS1580->$4;
      _M0L5paramS4697 = _M0L1eS1580->$3;
      _M0L6t__nowS4699 = _M0L1eS1580->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4699);
      moonbit_incref_cycle_free(_M0L5paramS4697);
      moonbit_incref_cycle_free(_M0L4varsS4696);
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4698 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4699, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4699);
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt21stdp__symmetric__step(_M0L4valsS4691, _M0L4fireS4692, _M0L4fireS4693, _M0L6colptrS4694, _M0L6rowptrS4695, _M0L4varsS4696, _M0L5paramS4697, _M0L6_2atmpS4698, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L4valsS4691);
      moonbit_decref_cycle_free(_M0L4fireS4692);
      moonbit_decref_cycle_free(_M0L4fireS4693);
      moonbit_decref_cycle_free(_M0L6colptrS4694);
      moonbit_decref_cycle_free(_M0L6rowptrS4695);
      moonbit_decref_cycle_free(_M0L4varsS4696);
      moonbit_decref_cycle_free(_M0L5paramS4697);
      _M0L6t__nowS4705 = _M0L1eS1580->$5;
      _M0L6t__nowS4708 = _M0L1eS1580->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4705);
      _M0L6_2acntS4991 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1580));
      if (_M0L6_2acntS4991 > 1) {
        int32_t _M0L11_2anew__cntS4994 = _M0L6_2acntS4991 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1580), _M0L11_2anew__cntS4994);
        moonbit_incref_cycle_free(_M0L6t__nowS4708);
      } else if (_M0L6_2acntS4991 == 1) {
        struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L8_2afieldS4993 =
          _M0L1eS1580->$4;
        struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L8_2afieldS4992;
        moonbit_decref_cycle_free(_M0L8_2afieldS4993);
        _M0L8_2afieldS4992 = _M0L1eS1580->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS4992);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1580);
      }
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4707 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4708, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4708);
      _M0L6_2atmpS4706 = _M0L6_2atmpS4707 + _M0L2dtS1536;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4705, 0, _M0L6_2atmpS4706);
      moonbit_decref_cycle_free(_M0L6t__nowS4705);
      joinlet_5163:;
      goto joinlet_5162;
      join_1576:;
      _M0L5connsS4689 = _M0L1mS1531->$1;
      _M0L11conn__indexS4690 = _M0L1eS1577->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1578
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4689, _M0L11conn__indexS4690);
      _M0L6matrixS4684 = _M0L3synS1578->$4;
      _M0L4valsS4671 = _M0L6matrixS4684->$4;
      _M0L3preS4683 = _M0L3synS1578->$0;
      _M0L4fireS4672 = _M0L3preS4683->$5;
      _M0L4postS4682 = _M0L3synS1578->$1;
      _M0L4fireS4673 = _M0L4postS4682->$5;
      _M0L6matrixS4681 = _M0L3synS1578->$4;
      _M0L6colptrS4674 = _M0L6matrixS4681->$3;
      _M0L6matrixS4680 = _M0L3synS1578->$4;
      moonbit_incref_cycle_free(_M0L6colptrS4674);
      moonbit_incref_cycle_free(_M0L4fireS4673);
      moonbit_incref_cycle_free(_M0L4fireS4672);
      moonbit_incref_cycle_free(_M0L4valsS4671);
      _M0L6_2acntS4957
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1578));
      if (_M0L6_2acntS4957 > 1) {
        int32_t _M0L11_2anew__cntS4967 = _M0L6_2acntS4957 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1578), _M0L11_2anew__cntS4967);
        moonbit_incref_cycle_free(_M0L6matrixS4680);
      } else if (_M0L6_2acntS4957 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4966 = _M0L3synS1578->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS4965;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4964;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4963;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4962;
        moonbit_string_t _M0L8_2afieldS4961;
        moonbit_string_t _M0L8_2afieldS4960;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS4959;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS4958;
        moonbit_decref_cycle_free(_M0L8_2afieldS4966);
        _M0L8_2afieldS4965 = _M0L3synS1578->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS4965);
        _M0L8_2afieldS4964 = _M0L3synS1578->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS4964);
        _M0L8_2afieldS4963 = _M0L3synS1578->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS4963);
        _M0L8_2afieldS4962 = _M0L3synS1578->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS4962);
        _M0L8_2afieldS4961 = _M0L3synS1578->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS4961);
        _M0L8_2afieldS4960 = _M0L3synS1578->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS4960);
        _M0L8_2afieldS4959 = _M0L3synS1578->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS4959);
        _M0L8_2afieldS4958 = _M0L3synS1578->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS4958);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1578);
      }
      _M0L6rowptrS4675 = _M0L6matrixS4680->$2;
      _M0L6_2acntS4968
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS4680));
      if (_M0L6_2acntS4968 > 1) {
        int32_t _M0L11_2anew__cntS4971 = _M0L6_2acntS4968 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS4680), _M0L11_2anew__cntS4971);
        moonbit_incref_cycle_free(_M0L6rowptrS4675);
      } else if (_M0L6_2acntS4968 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS4970 = _M0L6matrixS4680->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS4969;
        moonbit_decref_cycle_free(_M0L8_2afieldS4970);
        _M0L8_2afieldS4969 = _M0L6matrixS4680->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS4969);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS4680);
      }
      _M0L4varsS4676 = _M0L1eS1577->$4;
      _M0L5paramS4677 = _M0L1eS1577->$3;
      _M0L6t__nowS4679 = _M0L1eS1577->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4679);
      moonbit_incref_cycle_free(_M0L5paramS4677);
      moonbit_incref_cycle_free(_M0L4varsS4676);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4678 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4679, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4679);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt20ca__plasticity__step(_M0L4valsS4671, _M0L4fireS4672, _M0L4fireS4673, _M0L6colptrS4674, _M0L6rowptrS4675, _M0L4varsS4676, _M0L5paramS4677, _M0L6_2atmpS4678, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L4valsS4671);
      moonbit_decref_cycle_free(_M0L4fireS4672);
      moonbit_decref_cycle_free(_M0L4fireS4673);
      moonbit_decref_cycle_free(_M0L6colptrS4674);
      moonbit_decref_cycle_free(_M0L6rowptrS4675);
      moonbit_decref_cycle_free(_M0L4varsS4676);
      moonbit_decref_cycle_free(_M0L5paramS4677);
      _M0L6t__nowS4685 = _M0L1eS1577->$5;
      _M0L6t__nowS4688 = _M0L1eS1577->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS4685);
      _M0L6_2acntS4972 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1577));
      if (_M0L6_2acntS4972 > 1) {
        int32_t _M0L11_2anew__cntS4975 = _M0L6_2acntS4972 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1577), _M0L11_2anew__cntS4975);
        moonbit_incref_cycle_free(_M0L6t__nowS4688);
      } else if (_M0L6_2acntS4972 == 1) {
        struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L8_2afieldS4974 =
          _M0L1eS1577->$4;
        struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L8_2afieldS4973;
        moonbit_decref_cycle_free(_M0L8_2afieldS4974);
        _M0L8_2afieldS4973 = _M0L1eS1577->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS4973);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1577);
      }
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4687 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4688, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS4688);
      _M0L6_2atmpS4686 = _M0L6_2atmpS4687 + _M0L2dtS1536;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4685, 0, _M0L6_2atmpS4686);
      moonbit_decref_cycle_free(_M0L6t__nowS4685);
      joinlet_5162:;
      _M0L6_2atmpS4830 = _M0L2__S1574 + 1;
      _M0L2__S1574 = _M0L6_2atmpS4830;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1573);
    }
    break;
  }
  _M0L7_2abindS1617 = _M0L1mS1531->$0;
  _M0L7_2abindS1618 = _M0L7_2abindS1617->$1;
  _M0L7_2abindS1619 = _M0L7_2abindS1617->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1619);
  _M0L2__S1620 = 0;
  while (1) {
    if (_M0L2__S1620 < _M0L7_2abindS1618) {
      void* _M0L1pS1621 = (void*)_M0L7_2abindS1619[_M0L2__S1620];
      int32_t _M0L6_2atmpS4831;
      moonbit_incref_cycle_free(_M0L1pS1621);
      #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14integrate__any(_M0L1pS1621, _M0L2dtS1536);
      moonbit_decref_cycle_free(_M0L1pS1621);
      _M0L6_2atmpS4831 = _M0L2__S1620 + 1;
      _M0L2__S1620 = _M0L6_2atmpS4831;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1619);
    }
    break;
  }
  _M0L7_2abindS1623 = _M0L1mS1531->$4;
  _M0L7_2abindS1624 = _M0L7_2abindS1623->$1;
  _M0L7_2abindS1625 = _M0L7_2abindS1623->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1625);
  _M0L2__S1626 = 0;
  while (1) {
    if (_M0L2__S1626 < _M0L7_2abindS1624) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L3monS1627 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1625[
          _M0L2__S1626
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4833 = _M0L1mS1531->$3;
      float _M0L6_2atmpS4832;
      int32_t _M0L6_2atmpS4834;
      moonbit_incref_cycle_free(_M0L3monS1627);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4832 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4833);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L3monS1627, _M0L6_2atmpS4832);
      moonbit_decref_cycle_free(_M0L3monS1627);
      _M0L6_2atmpS4834 = _M0L2__S1626 + 1;
      _M0L2__S1626 = _M0L6_2atmpS4834;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1625);
    }
    break;
  }
  _M0L4timeS4835 = _M0L1mS1531->$3;
  #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS4835, _M0L2dtS1536);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1528,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1529,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L11stims_2eoptS1517,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L14monitors_2eoptS1520,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L10stdp_2eoptS1523,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L9stp_2eoptS1526
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1516;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1519;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1522;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1525;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _result_5172;
  if (_M0L11stims_2eoptS1517 == 0) {
    void** _M0L6_2atmpS4643 = (void**)moonbit_empty_ref_array;
    _M0L5stimsS1516
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
    Moonbit_object_header(_M0L5stimsS1516)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
    _M0L5stimsS1516->$0 = _M0L6_2atmpS4643;
    _M0L5stimsS1516->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2aSomeS1518 =
      _M0L11stims_2eoptS1517;
    if (_M0L7_2aSomeS1518) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1518);
    }
    _M0L5stimsS1516 = _M0L7_2aSomeS1518;
  }
  if (_M0L14monitors_2eoptS1520 == 0) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS4642 =
      (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_empty_ref_array;
    _M0L8monitorsS1519
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
    Moonbit_object_header(_M0L8monitorsS1519)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
    _M0L8monitorsS1519->$0 = _M0L6_2atmpS4642;
    _M0L8monitorsS1519->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2aSomeS1521 =
      _M0L14monitors_2eoptS1520;
    if (_M0L7_2aSomeS1521) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1521);
    }
    _M0L8monitorsS1519 = _M0L7_2aSomeS1521;
  }
  if (_M0L10stdp_2eoptS1523 == 0) {
    void** _M0L6_2atmpS4641 = (void**)moonbit_empty_ref_array;
    _M0L4stdpS1522
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
    Moonbit_object_header(_M0L4stdpS1522)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
    _M0L4stdpS1522->$0 = _M0L6_2atmpS4641;
    _M0L4stdpS1522->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2aSomeS1524 =
      _M0L10stdp_2eoptS1523;
    if (_M0L7_2aSomeS1524) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1524);
    }
    _M0L4stdpS1522 = _M0L7_2aSomeS1524;
  }
  if (_M0L9stp_2eoptS1526 == 0) {
    void** _M0L6_2atmpS4640 = (void**)moonbit_empty_ref_array;
    _M0L3stpS1525
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE));
    Moonbit_object_header(_M0L3stpS1525)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
    _M0L3stpS1525->$0 = _M0L6_2atmpS4640;
    _M0L3stpS1525->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2aSomeS1527 =
      _M0L9stp_2eoptS1526;
    if (_M0L7_2aSomeS1527) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1527);
    }
    _M0L3stpS1525 = _M0L7_2aSomeS1527;
  }
  _result_5172
  = _M0FP26RiantR8snn__mbt15compose_2einner(_M0L4popsS1528, _M0L5connsS1529, _M0L5stimsS1516, _M0L8monitorsS1519, _M0L4stdpS1522, _M0L3stpS1525);
  moonbit_decref_cycle_free(_M0L5stimsS1516);
  moonbit_decref_cycle_free(_M0L8monitorsS1519);
  moonbit_decref_cycle_free(_M0L4stdpS1522);
  moonbit_decref_cycle_free(_M0L3stpS1525);
  return _result_5172;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1510,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1511,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1512,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1513,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1514,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1515
) {
  struct _M0TP26RiantR8snn__mbt4Time* _M0L6_2atmpS4639;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _block_5173;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4639 = _M0MP26RiantR8snn__mbt4Time3new();
  moonbit_incref_cycle_free(_M0L4popsS1510);
  moonbit_incref_cycle_free(_M0L5connsS1511);
  moonbit_incref_cycle_free(_M0L5stimsS1512);
  moonbit_incref_cycle_free(_M0L8monitorsS1513);
  moonbit_incref_cycle_free(_M0L4stdpS1514);
  moonbit_incref_cycle_free(_M0L3stpS1515);
  _block_5173
  = (struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel));
  Moonbit_object_header(_block_5173)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_5173->$0 = _M0L4popsS1510;
  _block_5173->$1 = _M0L5connsS1511;
  _block_5173->$2 = _M0L5stimsS1512;
  _block_5173->$3 = _M0L6_2atmpS4639;
  _block_5173->$4 = _M0L8monitorsS1513;
  _block_5173->$5 = _M0L4stdpS1514;
  _block_5173->$6 = _M0L3stpS1515;
  return _block_5173;
}

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void* _M0L1sS1496,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS1484,
  float _M0L2dtS1491
) {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1xS1482;
  float _M0L1wS1483;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1xS1486;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1xS1488;
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1xS1490;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1xS1493;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1xS1495;
  float _M0L6_2atmpS4638;
  float _M0L6_2atmpS4637;
  float _M0L6_2atmpS4636;
  float _M0L6_2atmpS4635;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  switch (Moonbit_object_tag(_M0L1sS1496)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__* _M0L14_2aPoissonIF__S1497 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L1sS1496;
      struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L4_2axS1498 =
        _M0L14_2aPoissonIF__S1497->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1498);
      _M0L1xS1495 = _M0L4_2axS1498;
      goto join_1494;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__* _M0L17_2aPoissonLayer__S1499 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__*)_M0L1sS1496;
      struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L4_2axS1500 =
        _M0L17_2aPoissonLayer__S1499->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1500);
      _M0L1xS1493 = _M0L4_2axS1500;
      goto join_1492;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__* _M0L15_2aBalancedIF__S1501 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__*)_M0L1sS1496;
      struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L4_2axS1502 =
        _M0L15_2aBalancedIF__S1501->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1502);
      _M0L1xS1490 = _M0L4_2axS1502;
      goto join_1489;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__* _M0L14_2aCurrentIF__S1503 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__*)_M0L1sS1496;
      struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L4_2axS1504 =
        _M0L14_2aCurrentIF__S1503->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1504);
      _M0L1xS1488 = _M0L4_2axS1504;
      goto join_1487;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__* _M0L15_2aCurrentArr__S1505 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__*)_M0L1sS1496;
      struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L4_2axS1506 =
        _M0L15_2aCurrentArr__S1505->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1506);
      _M0L1xS1486 = _M0L4_2axS1506;
      goto join_1485;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__* _M0L14_2aTimedStim__S1507 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__*)_M0L1sS1496;
      struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L4_2axS1508 =
        _M0L14_2aTimedStim__S1507->$0;
      float _M0L4_2awS1509 = _M0L14_2aTimedStim__S1507->$1;
      moonbit_incref_cycle_free(_M0L4_2axS1508);
      _M0L1xS1482 = _M0L4_2axS1508;
      _M0L1wS1483 = _M0L4_2awS1509;
      goto join_1481;
      break;
    }
  }
  goto joinlet_5179;
  join_1494:;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4638 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1484);
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt13stimulate__if(_M0L1xS1495, _M0L6_2atmpS4638, _M0L2dtS1491);
  moonbit_decref_cycle_free(_M0L1xS1495);
  joinlet_5179:;
  goto joinlet_5178;
  join_1492:;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4637 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1484);
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt16stimulate__layer(_M0L1xS1493, _M0L6_2atmpS4637, _M0L2dtS1491);
  moonbit_decref_cycle_free(_M0L1xS1493);
  joinlet_5178:;
  goto joinlet_5177;
  join_1489:;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4636 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1484);
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt19stimulate__balanced(_M0L1xS1490, _M0L6_2atmpS4636, _M0L2dtS1491);
  moonbit_decref_cycle_free(_M0L1xS1490);
  joinlet_5177:;
  goto joinlet_5176;
  join_1487:;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt22stimulate__current__if(_M0L1xS1488);
  moonbit_decref_cycle_free(_M0L1xS1488);
  joinlet_5176:;
  goto joinlet_5175;
  join_1485:;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt25stimulate__current__array(_M0L1xS1486);
  moonbit_decref_cycle_free(_M0L1xS1486);
  joinlet_5175:;
  goto joinlet_5174;
  join_1481:;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4635 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1484);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L1xS1482, _M0L6_2atmpS4635, _M0L1wS1483);
  moonbit_decref_cycle_free(_M0L1xS1482);
  joinlet_5174:;
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1474,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1475,
  moonbit_string_t _M0L3symS1480,
  float _M0L2muS1476,
  float _M0L5sigmaS1477,
  float _M0L1pS1478,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1479
) {
  int32_t _M0L1nS4633;
  int32_t _M0L1nS4634;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1473;
  float* _M0L6_2atmpS4632;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4623;
  float* _M0L6_2atmpS4631;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4624;
  float* _M0L6_2atmpS4630;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4625;
  int32_t* _M0L6_2atmpS4629;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS4626;
  float* _M0L6_2atmpS4628;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4627;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_5180;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS4633 = _M0L3preS1474->$2;
  _M0L1nS4634 = _M0L4postS1475->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1473
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS4633, _M0L1nS4634, _M0L2muS1476, _M0L5sigmaS1477, _M0L1pS1478, _M0L3rngS1479);
  _M0L6_2atmpS4632 = moonbit_empty_float_array;
  _M0L6_2atmpS4623
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4623)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L6_2atmpS4623->$0 = _M0L6_2atmpS4632;
  _M0L6_2atmpS4623->$1 = 0;
  _M0L6_2atmpS4631 = moonbit_empty_float_array;
  _M0L6_2atmpS4624
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4624)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L6_2atmpS4624->$0 = _M0L6_2atmpS4631;
  _M0L6_2atmpS4624->$1 = 0;
  _M0L6_2atmpS4630 = moonbit_empty_float_array;
  _M0L6_2atmpS4625
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4625)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L6_2atmpS4625->$0 = _M0L6_2atmpS4630;
  _M0L6_2atmpS4625->$1 = 0;
  _M0L6_2atmpS4629 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS4626
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS4626)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _M0L6_2atmpS4626->$0 = _M0L6_2atmpS4629;
  _M0L6_2atmpS4626->$1 = 0;
  _M0L6_2atmpS4628 = moonbit_empty_float_array;
  _M0L6_2atmpS4627
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4627)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L6_2atmpS4627->$0 = _M0L6_2atmpS4628;
  _M0L6_2atmpS4627->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1474);
  moonbit_incref_cycle_free(_M0L4postS1475);
  moonbit_incref_cycle_free(_M0L3symS1480);
  _block_5180
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_5180)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_5180->$0 = _M0L3preS1474;
  _block_5180->$1 = _M0L4postS1475;
  _block_5180->$2 = _M0L3symS1480;
  _block_5180->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_5180->$4 = _M0L6matrixS1473;
  _block_5180->$5 = _M0L6_2atmpS4623;
  _block_5180->$6 = _M0L6_2atmpS4624;
  _block_5180->$7 = _M0L6_2atmpS4625;
  _block_5180->$8 = _M0L6_2atmpS4626;
  _block_5180->$9 = _M0L6_2atmpS4627;
  return _block_5180;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1469,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1446,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1448,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1465,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1459,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1456,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1452,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1450,
  float _M0L6t__nowS1444,
  float _M0L2dtS1453
) {
  int32_t _M0L6n__preS1445;
  int32_t _M0L7n__postS1447;
  float _M0L6tau__yS4622;
  float _M0L11inv__tau__yS1449;
  struct _M0TPB8MutLocalGiE* _M0L1jS1451;
  struct _M0TPB8MutLocalGiE* _M0L1iS1455;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1445 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1446);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1447 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1448);
  _M0L6tau__yS4622 = _M0L5paramS1450->$2;
  _M0L11inv__tau__yS1449 = 0x1p+0f / _M0L6tau__yS4622;
  _M0L1jS1451
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1451)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1451->$0 = 0;
  while (1) {
    int32_t _M0L3valS4539 = _M0L1jS1451->$0;
    if (_M0L3valS4539 < _M0L6n__preS1445) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4540 = _M0L4varsS1452->$0;
      int32_t _M0L3valS4541 = _M0L1jS1451->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4550 = _M0L4varsS1452->$0;
      int32_t _M0L3valS4551 = _M0L1jS1451->$0;
      float _M0L6_2atmpS4543;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4548;
      int32_t _M0L3valS4549;
      float _M0L6_2atmpS4547;
      float _M0L6_2atmpS4546;
      float _M0L6_2atmpS4545;
      float _M0L6_2atmpS4544;
      float _M0L6_2atmpS4542;
      int32_t _M0L3valS4552;
      int32_t _M0L3valS4560;
      int32_t _M0L6_2atmpS4559;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4543
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4550, _M0L3valS4551);
      _M0L4tpreS4548 = _M0L4varsS1452->$0;
      _M0L3valS4549 = _M0L1jS1451->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4547
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4548, _M0L3valS4549);
      _M0L6_2atmpS4546 = -_M0L6_2atmpS4547;
      _M0L6_2atmpS4545 = _M0L2dtS1453 * _M0L6_2atmpS4546;
      _M0L6_2atmpS4544 = _M0L6_2atmpS4545 * _M0L11inv__tau__yS1449;
      _M0L6_2atmpS4542 = _M0L6_2atmpS4543 + _M0L6_2atmpS4544;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4540, _M0L3valS4541, _M0L6_2atmpS4542);
      _M0L3valS4552 = _M0L1jS1451->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1446, _M0L3valS4552)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4553 = _M0L4varsS1452->$0;
        int32_t _M0L3valS4554 = _M0L1jS1451->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4557 = _M0L4varsS1452->$0;
        int32_t _M0L3valS4558 = _M0L1jS1451->$0;
        float _M0L6_2atmpS4556;
        float _M0L6_2atmpS4555;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS4556
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4557, _M0L3valS4558);
        _M0L6_2atmpS4555 = _M0L6_2atmpS4556 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4553, _M0L3valS4554, _M0L6_2atmpS4555);
      }
      _M0L3valS4560 = _M0L1jS1451->$0;
      _M0L6_2atmpS4559 = _M0L3valS4560 + 1;
      _M0L1jS1451->$0 = _M0L6_2atmpS4559;
      continue;
    }
    break;
  }
  _M0L1iS1455
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1455)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1455->$0 = 0;
  while (1) {
    int32_t _M0L3valS4561 = _M0L1iS1455->$0;
    if (_M0L3valS4561 < _M0L7n__postS1447) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4562 = _M0L4varsS1452->$1;
      int32_t _M0L3valS4563 = _M0L1iS1455->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4575 = _M0L4varsS1452->$1;
      int32_t _M0L3valS4576 = _M0L1iS1455->$0;
      float _M0L6_2atmpS4565;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4573;
      int32_t _M0L3valS4574;
      float _M0L6_2atmpS4570;
      int32_t _M0L3valS4572;
      float _M0L6_2atmpS4571;
      float _M0L6_2atmpS4569;
      float _M0L6_2atmpS4568;
      float _M0L6_2atmpS4567;
      float _M0L6_2atmpS4566;
      float _M0L6_2atmpS4564;
      int32_t _M0L3valS4577;
      int32_t _M0L3valS4585;
      int32_t _M0L6_2atmpS4584;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4565
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4575, _M0L3valS4576);
      _M0L5tpostS4573 = _M0L4varsS1452->$1;
      _M0L3valS4574 = _M0L1iS1455->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4570
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4573, _M0L3valS4574);
      _M0L3valS4572 = _M0L1iS1455->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4571
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1456, _M0L3valS4572);
      _M0L6_2atmpS4569 = _M0L6_2atmpS4570 - _M0L6_2atmpS4571;
      _M0L6_2atmpS4568 = -_M0L6_2atmpS4569;
      _M0L6_2atmpS4567 = _M0L2dtS1453 * _M0L6_2atmpS4568;
      _M0L6_2atmpS4566 = _M0L6_2atmpS4567 * _M0L11inv__tau__yS1449;
      _M0L6_2atmpS4564 = _M0L6_2atmpS4565 + _M0L6_2atmpS4566;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4562, _M0L3valS4563, _M0L6_2atmpS4564);
      _M0L3valS4577 = _M0L1iS1455->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1448, _M0L3valS4577)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4578 = _M0L4varsS1452->$1;
        int32_t _M0L3valS4579 = _M0L1iS1455->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4582 = _M0L4varsS1452->$1;
        int32_t _M0L3valS4583 = _M0L1iS1455->$0;
        float _M0L6_2atmpS4581;
        float _M0L6_2atmpS4580;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS4581
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4582, _M0L3valS4583);
        _M0L6_2atmpS4580 = _M0L6_2atmpS4581 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4578, _M0L3valS4579, _M0L6_2atmpS4580);
      }
      _M0L3valS4585 = _M0L1iS1455->$0;
      _M0L6_2atmpS4584 = _M0L3valS4585 + 1;
      _M0L1iS1455->$0 = _M0L6_2atmpS4584;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1455);
    }
    break;
  }
  _M0L1jS1451->$0 = 0;
  while (1) {
    int32_t _M0L3valS4586 = _M0L1jS1451->$0;
    if (_M0L3valS4586 < _M0L6n__preS1445) {
      int32_t _M0L3valS4621 = _M0L1jS1451->$0;
      int32_t _M0L5startS1458;
      int32_t _M0L3valS4620;
      int32_t _M0L6_2atmpS4619;
      int32_t _M0L3endS1460;
      int32_t _M0L3valS4618;
      int32_t _M0L10pre__firedS1461;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4616;
      int32_t _M0L3valS4617;
      float _M0L7tpre__jS1462;
      struct _M0TPB8MutLocalGiE* _M0L1sS1463;
      int32_t _M0L3valS4615;
      int32_t _M0L6_2atmpS4614;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1458
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1459, _M0L3valS4621);
      _M0L3valS4620 = _M0L1jS1451->$0;
      _M0L6_2atmpS4619 = _M0L3valS4620 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1460
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1459, _M0L6_2atmpS4619);
      _M0L3valS4618 = _M0L1jS1451->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1461
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1446, _M0L3valS4618);
      _M0L4tpreS4616 = _M0L4varsS1452->$0;
      _M0L3valS4617 = _M0L1jS1451->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1462
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4616, _M0L3valS4617);
      _M0L1sS1463
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1463)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1463->$0 = _M0L5startS1458;
      while (1) {
        int32_t _M0L3valS4587 = _M0L1sS1463->$0;
        if (_M0L3valS4587 < _M0L3endS1460) {
          int32_t _M0L3valS4613 = _M0L1sS1463->$0;
          int32_t _M0L9post__idxS1464;
          int32_t _M0L11post__firedS1466;
          struct _M0TPB5ArrayGfE* _M0L5tpostS4612;
          float _M0L8tpost__iS1467;
          int32_t _M0L3valS4602;
          float _M0L6_2atmpS4600;
          float _M0L6w__minS4601;
          int32_t _M0L3valS4607;
          float _M0L6_2atmpS4605;
          float _M0L6w__maxS4606;
          int32_t _M0L3valS4611;
          int32_t _M0L6_2atmpS4610;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1464
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1465, _M0L3valS4613);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1466
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1448, _M0L9post__idxS1464);
          _M0L5tpostS4612 = _M0L4varsS1452->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1467
          = _M0MPC15array5Array2atGfE(_M0L5tpostS4612, _M0L9post__idxS1464);
          if (_M0L10pre__firedS1461) {
            float _M0L3etaS4592 = _M0L5paramS1450->$0;
            float _M0L2v0S4594 = _M0L5paramS1450->$1;
            float _M0L6_2atmpS4593 = _M0L8tpost__iS1467 - _M0L2v0S4594;
            float _M0L2dwS1468 = _M0L3etaS4592 * _M0L6_2atmpS4593;
            int32_t _M0L3valS4588 = _M0L1sS1463->$0;
            int32_t _M0L3valS4591 = _M0L1sS1463->$0;
            float _M0L6_2atmpS4590;
            float _M0L6_2atmpS4589;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS4590
            = _M0MPC15array5Array2atGfE(_M0L1wS1469, _M0L3valS4591);
            _M0L6_2atmpS4589 = _M0L6_2atmpS4590 + _M0L2dwS1468;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1469, _M0L3valS4588, _M0L6_2atmpS4589);
          }
          if (_M0L11post__firedS1466) {
            float _M0L3etaS4599 = _M0L5paramS1450->$0;
            float _M0L2dwS1470 = _M0L3etaS4599 * _M0L7tpre__jS1462;
            int32_t _M0L3valS4595 = _M0L1sS1463->$0;
            int32_t _M0L3valS4598 = _M0L1sS1463->$0;
            float _M0L6_2atmpS4597;
            float _M0L6_2atmpS4596;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS4597
            = _M0MPC15array5Array2atGfE(_M0L1wS1469, _M0L3valS4598);
            _M0L6_2atmpS4596 = _M0L6_2atmpS4597 + _M0L2dwS1470;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1469, _M0L3valS4595, _M0L6_2atmpS4596);
          }
          _M0L3valS4602 = _M0L1sS1463->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS4600
          = _M0MPC15array5Array2atGfE(_M0L1wS1469, _M0L3valS4602);
          _M0L6w__minS4601 = _M0L5paramS1450->$4;
          if (_M0L6_2atmpS4600 < _M0L6w__minS4601) {
            int32_t _M0L3valS4603 = _M0L1sS1463->$0;
            float _M0L6w__minS4604 = _M0L5paramS1450->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1469, _M0L3valS4603, _M0L6w__minS4604);
          }
          _M0L3valS4607 = _M0L1sS1463->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS4605
          = _M0MPC15array5Array2atGfE(_M0L1wS1469, _M0L3valS4607);
          _M0L6w__maxS4606 = _M0L5paramS1450->$3;
          if (_M0L6_2atmpS4605 > _M0L6w__maxS4606) {
            int32_t _M0L3valS4608 = _M0L1sS1463->$0;
            float _M0L6w__maxS4609 = _M0L5paramS1450->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1469, _M0L3valS4608, _M0L6w__maxS4609);
          }
          _M0L3valS4611 = _M0L1sS1463->$0;
          _M0L6_2atmpS4610 = _M0L3valS4611 + 1;
          _M0L1sS1463->$0 = _M0L6_2atmpS4610;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1463);
        }
        break;
      }
      _M0L3valS4615 = _M0L1jS1451->$0;
      _M0L6_2atmpS4614 = _M0L3valS4615 + 1;
      _M0L1jS1451->$0 = _M0L6_2atmpS4614;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1451);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1440,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1418,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1420,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1436,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1430,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1424,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1422,
  float _M0L6t__nowS1416,
  float _M0L2dtS1425
) {
  int32_t _M0L6n__preS1417;
  int32_t _M0L7n__postS1419;
  float _M0L6tau__yS4538;
  float _M0L11inv__tau__yS1421;
  struct _M0TPB8MutLocalGiE* _M0L1jS1423;
  struct _M0TPB8MutLocalGiE* _M0L1iS1427;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1417 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1418);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1419 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1420);
  _M0L6tau__yS4538 = _M0L5paramS1422->$2;
  _M0L11inv__tau__yS1421 = 0x1p+0f / _M0L6tau__yS4538;
  _M0L1jS1423
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1423)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1423->$0 = 0;
  while (1) {
    int32_t _M0L3valS4455 = _M0L1jS1423->$0;
    if (_M0L3valS4455 < _M0L6n__preS1417) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4456 = _M0L4varsS1424->$0;
      int32_t _M0L3valS4457 = _M0L1jS1423->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4466 = _M0L4varsS1424->$0;
      int32_t _M0L3valS4467 = _M0L1jS1423->$0;
      float _M0L6_2atmpS4459;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4464;
      int32_t _M0L3valS4465;
      float _M0L6_2atmpS4463;
      float _M0L6_2atmpS4462;
      float _M0L6_2atmpS4461;
      float _M0L6_2atmpS4460;
      float _M0L6_2atmpS4458;
      int32_t _M0L3valS4468;
      int32_t _M0L3valS4476;
      int32_t _M0L6_2atmpS4475;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4459
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4466, _M0L3valS4467);
      _M0L4tpreS4464 = _M0L4varsS1424->$0;
      _M0L3valS4465 = _M0L1jS1423->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4463
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4464, _M0L3valS4465);
      _M0L6_2atmpS4462 = -_M0L6_2atmpS4463;
      _M0L6_2atmpS4461 = _M0L2dtS1425 * _M0L6_2atmpS4462;
      _M0L6_2atmpS4460 = _M0L6_2atmpS4461 * _M0L11inv__tau__yS1421;
      _M0L6_2atmpS4458 = _M0L6_2atmpS4459 + _M0L6_2atmpS4460;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4456, _M0L3valS4457, _M0L6_2atmpS4458);
      _M0L3valS4468 = _M0L1jS1423->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1418, _M0L3valS4468)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4469 = _M0L4varsS1424->$0;
        int32_t _M0L3valS4470 = _M0L1jS1423->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4473 = _M0L4varsS1424->$0;
        int32_t _M0L3valS4474 = _M0L1jS1423->$0;
        float _M0L6_2atmpS4472;
        float _M0L6_2atmpS4471;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS4472
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4473, _M0L3valS4474);
        _M0L6_2atmpS4471 = _M0L6_2atmpS4472 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4469, _M0L3valS4470, _M0L6_2atmpS4471);
      }
      _M0L3valS4476 = _M0L1jS1423->$0;
      _M0L6_2atmpS4475 = _M0L3valS4476 + 1;
      _M0L1jS1423->$0 = _M0L6_2atmpS4475;
      continue;
    }
    break;
  }
  _M0L1iS1427
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1427)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1427->$0 = 0;
  while (1) {
    int32_t _M0L3valS4477 = _M0L1iS1427->$0;
    if (_M0L3valS4477 < _M0L7n__postS1419) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4478 = _M0L4varsS1424->$1;
      int32_t _M0L3valS4479 = _M0L1iS1427->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4488 = _M0L4varsS1424->$1;
      int32_t _M0L3valS4489 = _M0L1iS1427->$0;
      float _M0L6_2atmpS4481;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4486;
      int32_t _M0L3valS4487;
      float _M0L6_2atmpS4485;
      float _M0L6_2atmpS4484;
      float _M0L6_2atmpS4483;
      float _M0L6_2atmpS4482;
      float _M0L6_2atmpS4480;
      int32_t _M0L3valS4490;
      int32_t _M0L3valS4498;
      int32_t _M0L6_2atmpS4497;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4481
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4488, _M0L3valS4489);
      _M0L5tpostS4486 = _M0L4varsS1424->$1;
      _M0L3valS4487 = _M0L1iS1427->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4485
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4486, _M0L3valS4487);
      _M0L6_2atmpS4484 = -_M0L6_2atmpS4485;
      _M0L6_2atmpS4483 = _M0L2dtS1425 * _M0L6_2atmpS4484;
      _M0L6_2atmpS4482 = _M0L6_2atmpS4483 * _M0L11inv__tau__yS1421;
      _M0L6_2atmpS4480 = _M0L6_2atmpS4481 + _M0L6_2atmpS4482;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4478, _M0L3valS4479, _M0L6_2atmpS4480);
      _M0L3valS4490 = _M0L1iS1427->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1420, _M0L3valS4490)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4491 = _M0L4varsS1424->$1;
        int32_t _M0L3valS4492 = _M0L1iS1427->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4495 = _M0L4varsS1424->$1;
        int32_t _M0L3valS4496 = _M0L1iS1427->$0;
        float _M0L6_2atmpS4494;
        float _M0L6_2atmpS4493;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS4494
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4495, _M0L3valS4496);
        _M0L6_2atmpS4493 = _M0L6_2atmpS4494 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4491, _M0L3valS4492, _M0L6_2atmpS4493);
      }
      _M0L3valS4498 = _M0L1iS1427->$0;
      _M0L6_2atmpS4497 = _M0L3valS4498 + 1;
      _M0L1iS1427->$0 = _M0L6_2atmpS4497;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1427);
    }
    break;
  }
  _M0L1jS1423->$0 = 0;
  while (1) {
    int32_t _M0L3valS4499 = _M0L1jS1423->$0;
    if (_M0L3valS4499 < _M0L6n__preS1417) {
      int32_t _M0L3valS4537 = _M0L1jS1423->$0;
      int32_t _M0L5startS1429;
      int32_t _M0L3valS4536;
      int32_t _M0L6_2atmpS4535;
      int32_t _M0L3endS1431;
      int32_t _M0L3valS4534;
      int32_t _M0L10pre__firedS1432;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4532;
      int32_t _M0L3valS4533;
      float _M0L7tpre__jS1433;
      struct _M0TPB8MutLocalGiE* _M0L1sS1434;
      int32_t _M0L3valS4531;
      int32_t _M0L6_2atmpS4530;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1429
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1430, _M0L3valS4537);
      _M0L3valS4536 = _M0L1jS1423->$0;
      _M0L6_2atmpS4535 = _M0L3valS4536 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1431
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1430, _M0L6_2atmpS4535);
      _M0L3valS4534 = _M0L1jS1423->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1432
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1418, _M0L3valS4534);
      _M0L4tpreS4532 = _M0L4varsS1424->$0;
      _M0L3valS4533 = _M0L1jS1423->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1433
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4532, _M0L3valS4533);
      _M0L1sS1434
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1434)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1434->$0 = _M0L5startS1429;
      while (1) {
        int32_t _M0L3valS4500 = _M0L1sS1434->$0;
        if (_M0L3valS4500 < _M0L3endS1431) {
          int32_t _M0L3valS4529 = _M0L1sS1434->$0;
          int32_t _M0L9post__idxS1435;
          int32_t _M0L11post__firedS1437;
          struct _M0TPB5ArrayGfE* _M0L5tpostS4528;
          float _M0L8tpost__iS1438;
          int32_t _M0L3valS4518;
          float _M0L6_2atmpS4516;
          float _M0L6w__minS4517;
          int32_t _M0L3valS4523;
          float _M0L6_2atmpS4521;
          float _M0L6w__maxS4522;
          int32_t _M0L3valS4527;
          int32_t _M0L6_2atmpS4526;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1435
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1436, _M0L3valS4529);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1437
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1420, _M0L9post__idxS1435);
          _M0L5tpostS4528 = _M0L4varsS1424->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1438
          = _M0MPC15array5Array2atGfE(_M0L5tpostS4528, _M0L9post__idxS1435);
          if (_M0L10pre__firedS1432) {
            float _M0L3etaS4505 = _M0L5paramS1422->$0;
            float _M0L1rS4510 = _M0L5paramS1422->$1;
            float _M0L6_2atmpS4508 = 0x1p+1f * _M0L1rS4510;
            float _M0L6tau__yS4509 = _M0L5paramS1422->$2;
            float _M0L6_2atmpS4507 = _M0L6_2atmpS4508 * _M0L6tau__yS4509;
            float _M0L6_2atmpS4506 = _M0L8tpost__iS1438 - _M0L6_2atmpS4507;
            float _M0L2dwS1439 = _M0L3etaS4505 * _M0L6_2atmpS4506;
            int32_t _M0L3valS4501 = _M0L1sS1434->$0;
            int32_t _M0L3valS4504 = _M0L1sS1434->$0;
            float _M0L6_2atmpS4503;
            float _M0L6_2atmpS4502;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS4503
            = _M0MPC15array5Array2atGfE(_M0L1wS1440, _M0L3valS4504);
            _M0L6_2atmpS4502 = _M0L6_2atmpS4503 + _M0L2dwS1439;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1440, _M0L3valS4501, _M0L6_2atmpS4502);
          }
          if (_M0L11post__firedS1437) {
            float _M0L3etaS4515 = _M0L5paramS1422->$0;
            float _M0L2dwS1441 = _M0L3etaS4515 * _M0L7tpre__jS1433;
            int32_t _M0L3valS4511 = _M0L1sS1434->$0;
            int32_t _M0L3valS4514 = _M0L1sS1434->$0;
            float _M0L6_2atmpS4513;
            float _M0L6_2atmpS4512;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS4513
            = _M0MPC15array5Array2atGfE(_M0L1wS1440, _M0L3valS4514);
            _M0L6_2atmpS4512 = _M0L6_2atmpS4513 + _M0L2dwS1441;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1440, _M0L3valS4511, _M0L6_2atmpS4512);
          }
          _M0L3valS4518 = _M0L1sS1434->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS4516
          = _M0MPC15array5Array2atGfE(_M0L1wS1440, _M0L3valS4518);
          _M0L6w__minS4517 = _M0L5paramS1422->$4;
          if (_M0L6_2atmpS4516 < _M0L6w__minS4517) {
            int32_t _M0L3valS4519 = _M0L1sS1434->$0;
            float _M0L6w__minS4520 = _M0L5paramS1422->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1440, _M0L3valS4519, _M0L6w__minS4520);
          }
          _M0L3valS4523 = _M0L1sS1434->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS4521
          = _M0MPC15array5Array2atGfE(_M0L1wS1440, _M0L3valS4523);
          _M0L6w__maxS4522 = _M0L5paramS1422->$3;
          if (_M0L6_2atmpS4521 > _M0L6w__maxS4522) {
            int32_t _M0L3valS4524 = _M0L1sS1434->$0;
            float _M0L6w__maxS4525 = _M0L5paramS1422->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1440, _M0L3valS4524, _M0L6w__maxS4525);
          }
          _M0L3valS4527 = _M0L1sS1434->$0;
          _M0L6_2atmpS4526 = _M0L3valS4527 + 1;
          _M0L1sS1434->$0 = _M0L6_2atmpS4526;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1434);
        }
        break;
      }
      _M0L3valS4531 = _M0L1jS1423->$0;
      _M0L6_2atmpS4530 = _M0L3valS4531 + 1;
      _M0L1jS1423->$0 = _M0L6_2atmpS4530;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1423);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter8with__el(
  float _M0L2elS1415
) {
  float _M0L1cS1413;
  float _M0L2glS1414;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_5189;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1413 = -0x1p+0f;
  _M0L2glS1414 = -0x1p+0f;
  _block_5189
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_5189)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5189->$0 = _M0L1cS1413;
  _block_5189->$1 = _M0L2glS1414;
  _block_5189->$2 = 0x1.ep+3f;
  _block_5189->$3 = -0x1.9p+5f;
  _block_5189->$4 = -0x1.ep+5f;
  _block_5189->$5 = _M0L2elS1415;
  _block_5189->$6 = 0x1.eb851eb851eb8p-5f;
  _block_5189->$7 = 0x1p+1f;
  _block_5189->$8 = 0x0p+0f;
  _block_5189->$9 = 0x0p+0f;
  _block_5189->$10 = 0x0p+0f;
  return _block_5189;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1387,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1389,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1392
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1386;
  float _M0L2vtS4453;
  float _M0L2vrS4454;
  float _M0L6spreadS1388;
  int32_t _M0L7_2abindS1390;
  int32_t _M0L1kS1391;
  struct _M0TPB5ArrayGfE* _M0L1wS1394;
  struct _M0TPB5ArrayGbE* _M0L4fireS1395;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1396;
  struct _M0TPB5ArrayGfE* _M0L1iS1397;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1398;
  struct _M0TPB5ArrayGfE* _M0L2geS1399;
  struct _M0TPB5ArrayGfE* _M0L2giS1400;
  struct _M0TPB5ArrayGfE* _M0L2heS1401;
  struct _M0TPB5ArrayGfE* _M0L2hiS1402;
  struct _M0TPB5ArrayGfE* _M0L3gluS1403;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1404;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1405;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1406;
  float _M0L4e__eS1407;
  float _M0L4e__iS1408;
  float _M0L3treS1409;
  float _M0L3tdeS1410;
  float _M0L3triS1411;
  float _M0L3tdiS1412;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS4452;
  struct _M0TP26RiantR8snn__mbt2IF* _block_5191;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1386 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  _M0L2vtS4453 = _M0L5paramS1389->$3;
  _M0L2vrS4454 = _M0L5paramS1389->$4;
  _M0L6spreadS1388 = _M0L2vtS4453 - _M0L2vrS4454;
  _M0L7_2abindS1390 = 0;
  _M0L1kS1391 = _M0L7_2abindS1390;
  while (1) {
    if (_M0L1kS1391 < _M0L1nS1387) {
      float _M0L2vrS4448 = _M0L5paramS1389->$4;
      float _M0L6_2atmpS4450;
      float _M0L6_2atmpS4449;
      float _M0L6_2atmpS4447;
      int32_t _M0L6_2atmpS4451;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4450 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1392);
      _M0L6_2atmpS4449 = _M0L6_2atmpS4450 * _M0L6spreadS1388;
      _M0L6_2atmpS4447 = _M0L2vrS4448 + _M0L6_2atmpS4449;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1386, _M0L1kS1391, _M0L6_2atmpS4447);
      _M0L6_2atmpS4451 = _M0L1kS1391 + 1;
      _M0L1kS1391 = _M0L6_2atmpS4451;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1394 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1395 = _M0MPC15array5Array4makeGbE(_M0L1nS1387, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1396 = _M0MPC15array5Array4makeGiE(_M0L1nS1387, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1397 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1398 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1399 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1400 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1401 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1402 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1403 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1404 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1405 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1406 = _M0MPC15array5Array4makeGfE(_M0L1nS1387, 0x1p+0f);
  _M0L4e__eS1407 = 0x0p+0f;
  _M0L4e__iS1408 = -0x1.2cp+6f;
  _M0L3treS1409 = 0x1p+0f;
  _M0L3tdeS1410 = 0x1.8p+2f;
  _M0L3triS1411 = 0x1p-1f;
  _M0L3tdiS1412 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS4452 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1389);
  _block_5191
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_5191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _block_5191->$0 = _M0L5paramS1389;
  _block_5191->$1 = _M0L6_2atmpS4452;
  _block_5191->$2 = _M0L1nS1387;
  _block_5191->$3 = _M0L1vS1386;
  _block_5191->$4 = _M0L1wS1394;
  _block_5191->$5 = _M0L4fireS1395;
  _block_5191->$6 = _M0L4tabsS1396;
  _block_5191->$7 = _M0L1iS1397;
  _block_5191->$8 = _M0L9syn__currS1398;
  _block_5191->$9 = _M0L2geS1399;
  _block_5191->$10 = _M0L2giS1400;
  _block_5191->$11 = _M0L2heS1401;
  _block_5191->$12 = _M0L2hiS1402;
  _block_5191->$13 = _M0L3gluS1403;
  _block_5191->$14 = _M0L4gabaS1404;
  _block_5191->$15 = _M0L7gsyn__eS1405;
  _block_5191->$16 = _M0L7gsyn__iS1406;
  _block_5191->$17 = _M0L4e__eS1407;
  _block_5191->$18 = _M0L4e__iS1408;
  _block_5191->$19 = _M0L3treS1409;
  _block_5191->$20 = _M0L3tdeS1410;
  _block_5191->$21 = _M0L3triS1411;
  _block_5191->$22 = _M0L3tdiS1412;
  return _block_5191;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_5192;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_5192
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_5192)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5192->$0 = 0x1p+1f;
  return _block_5192;
}

int32_t _M0FP26RiantR8snn__mbt14integrate__any(
  void* _M0L1pS1367,
  float _M0L2dtS1350
) {
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1xS1349;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1xS1352;
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1xS1354;
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1xS1356;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1xS1358;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1xS1360;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1xS1362;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1xS1364;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1xS1366;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  switch (Moonbit_object_tag(_M0L1pS1367)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__* _M0L7_2aIF__S1368 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4_2axS1369 =
        _M0L7_2aIF__S1368->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1369);
      _M0L1xS1366 = _M0L4_2axS1369;
      goto join_1365;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__* _M0L9_2aAdEx__S1370 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4_2axS1371 =
        _M0L9_2aAdEx__S1370->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1371);
      _M0L1xS1364 = _M0L4_2axS1371;
      goto join_1363;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__* _M0L15_2aAdExSinExp__S1372 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4_2axS1373 =
        _M0L15_2aAdExSinExp__S1372->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1373);
      _M0L1xS1362 = _M0L4_2axS1373;
      goto join_1361;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__* _M0L7_2aIZ__S1374 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt2IZ* _M0L4_2axS1375 =
        _M0L7_2aIZ__S1374->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1375);
      _M0L1xS1360 = _M0L4_2axS1375;
      goto join_1359;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__* _M0L7_2aHH__S1376 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt2HH* _M0L4_2axS1377 =
        _M0L7_2aHH__S1376->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1377);
      _M0L1xS1358 = _M0L4_2axS1377;
      goto join_1357;
      break;
    }
    
    case 5: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__* _M0L7_2aML__S1378 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L4_2axS1379 =
        _M0L7_2aML__S1378->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1379);
      _M0L1xS1356 = _M0L4_2axS1379;
      goto join_1355;
      break;
    }
    
    case 6: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__* _M0L12_2aPoisson__S1380 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt7Poisson* _M0L4_2axS1381 =
        _M0L12_2aPoisson__S1380->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1381);
      _M0L1xS1354 = _M0L4_2axS1381;
      goto join_1353;
      break;
    }
    
    case 7: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__* _M0L7_2aWC__S1382 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4_2axS1383 =
        _M0L7_2aWC__S1382->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1383);
      _M0L1xS1352 = _M0L4_2axS1383;
      goto join_1351;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__* _M0L11_2aHetRec__S1384 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__*)_M0L1pS1367;
      struct _M0TP26RiantR8snn__mbt6HetRec* _M0L4_2axS1385 =
        _M0L11_2aHetRec__S1384->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1385);
      _M0L1xS1349 = _M0L4_2axS1385;
      goto join_1348;
      break;
    }
  }
  goto joinlet_5201;
  join_1365:;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L1xS1366, _M0L2dtS1350);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1xS1366);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L1xS1366, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1366);
  joinlet_5201:;
  goto joinlet_5200;
  join_1363:;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1xS1364, _M0L2dtS1350);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1xS1364);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt10step__adex(_M0L1xS1364, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1364);
  joinlet_5200:;
  goto joinlet_5199;
  join_1361:;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L1xS1362, _M0L2dtS1350);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L1xS1362);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L1xS1362, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1362);
  joinlet_5199:;
  goto joinlet_5198;
  join_1359:;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__iz(_M0L1xS1360, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1360);
  joinlet_5198:;
  goto joinlet_5197;
  join_1357:;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__hh(_M0L1xS1358, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1358);
  joinlet_5197:;
  goto joinlet_5196;
  join_1355:;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__ml(_M0L1xS1356, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1356);
  joinlet_5196:;
  goto joinlet_5195;
  join_1353:;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt13step__poisson(_M0L1xS1354, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1354);
  joinlet_5195:;
  goto joinlet_5194;
  join_1351:;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__wc(_M0L1xS1352, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1352);
  joinlet_5194:;
  goto joinlet_5193;
  join_1348:;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__hetrec(_M0L1xS1349, _M0L2dtS1350);
  moonbit_decref_cycle_free(_M0L1xS1349);
  joinlet_5193:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS1343,
  float _M0L2dtS1346
) {
  int32_t _M0L1nS1342;
  int32_t _M0L7_2abindS1344;
  int32_t _M0L1kS1345;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS1342 = _M0L1pS1343->$1;
  _M0L7_2abindS1344 = 0;
  _M0L1kS1345 = _M0L7_2abindS1344;
  while (1) {
    if (_M0L1kS1345 < _M0L1nS1342) {
      struct _M0TPB5ArrayGfE* _M0L1xS4427 = _M0L1pS1343->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS4440 = _M0L1pS1343->$2;
      float _M0L6_2atmpS4429;
      struct _M0TPB5ArrayGfE* _M0L1xS4439;
      float _M0L6_2atmpS4438;
      float _M0L6_2atmpS4435;
      struct _M0TPB5ArrayGfE* _M0L1gS4437;
      float _M0L6_2atmpS4436;
      float _M0L6_2atmpS4432;
      struct _M0TPB5ArrayGfE* _M0L1iS4434;
      float _M0L6_2atmpS4433;
      float _M0L6_2atmpS4431;
      float _M0L6_2atmpS4430;
      float _M0L6_2atmpS4428;
      struct _M0TPB5ArrayGfE* _M0L1rS4441;
      struct _M0TPB5ArrayGfE* _M0L1xS4444;
      float _M0L6_2atmpS4443;
      float _M0L6_2atmpS4442;
      struct _M0TPB5ArrayGfE* _M0L1gS4445;
      int32_t _M0L6_2atmpS4446;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4429 = _M0MPC15array5Array2atGfE(_M0L1xS4440, _M0L1kS1345);
      _M0L1xS4439 = _M0L1pS1343->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4438 = _M0MPC15array5Array2atGfE(_M0L1xS4439, _M0L1kS1345);
      _M0L6_2atmpS4435 = -_M0L6_2atmpS4438;
      _M0L1gS4437 = _M0L1pS1343->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4436 = _M0MPC15array5Array2atGfE(_M0L1gS4437, _M0L1kS1345);
      _M0L6_2atmpS4432 = _M0L6_2atmpS4435 + _M0L6_2atmpS4436;
      _M0L1iS4434 = _M0L1pS1343->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4433 = _M0MPC15array5Array2atGfE(_M0L1iS4434, _M0L1kS1345);
      _M0L6_2atmpS4431 = _M0L6_2atmpS4432 + _M0L6_2atmpS4433;
      _M0L6_2atmpS4430 = _M0L2dtS1346 * _M0L6_2atmpS4431;
      _M0L6_2atmpS4428 = _M0L6_2atmpS4429 + _M0L6_2atmpS4430;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS4427, _M0L1kS1345, _M0L6_2atmpS4428);
      _M0L1rS4441 = _M0L1pS1343->$3;
      _M0L1xS4444 = _M0L1pS1343->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4443 = _M0MPC15array5Array2atGfE(_M0L1xS4444, _M0L1kS1345);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4442 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS4443);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS4441, _M0L1kS1345, _M0L6_2atmpS4442);
      _M0L1gS4445 = _M0L1pS1343->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS4445, _M0L1kS1345, 0x0p+0f);
      _M0L6_2atmpS4446 = _M0L1kS1345 + 1;
      _M0L1kS1345 = _M0L6_2atmpS4446;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS1335,
  float _M0L2dtS1337
) {
  int32_t _M0L1nS1334;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS4426;
  float _M0L4rateS4425;
  float _M0L8rate__dtS1336;
  int32_t _M0L7_2abindS1338;
  int32_t _M0L1iS1339;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS1334 = _M0L1pS1335->$1;
  _M0L5paramS4426 = _M0L1pS1335->$0;
  _M0L4rateS4425 = _M0L5paramS4426->$0;
  _M0L8rate__dtS1336 = _M0L4rateS4425 * _M0L2dtS1337;
  _M0L7_2abindS1338 = 0;
  _M0L1iS1339 = _M0L7_2abindS1338;
  while (1) {
    if (_M0L1iS1339 < _M0L1nS1334) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS4423 = _M0L1pS1335->$4;
      float _M0L1uS1340;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4420;
      struct _M0TPB5ArrayGbE* _M0L4fireS4421;
      int32_t _M0L6_2atmpS4422;
      int32_t _M0L6_2atmpS4424;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS1340 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS4423);
      _M0L9randcacheS4420 = _M0L1pS1335->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS4420, _M0L1iS1339, _M0L1uS1340);
      _M0L4fireS4421 = _M0L1pS1335->$2;
      _M0L6_2atmpS4422 = _M0L1uS1340 < _M0L8rate__dtS1336;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4421, _M0L1iS1339, _M0L6_2atmpS4422);
      _M0L6_2atmpS4424 = _M0L1iS1339 + 1;
      _M0L1iS1339 = _M0L6_2atmpS4424;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1pS1294,
  float _M0L2dtS1323
) {
  int32_t _M0L1nS1293;
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* _M0L3p__S1295;
  float _M0L2cmS1296;
  float _M0L2elS1297;
  float _M0L2ekS1298;
  float _M0L3ecaS1299;
  float _M0L2glS1300;
  float _M0L2gkS1301;
  float _M0L3gcaS1302;
  float _M0L6tau__eS1303;
  float _M0L6tau__iS1304;
  float _M0L2v1S1305;
  float _M0L2v2S1306;
  float _M0L2v3S1307;
  float _M0L2v4S1308;
  float _M0L3phiS1309;
  float _M0L4e__eS1310;
  float _M0L4e__iS1311;
  int32_t _M0L7_2abindS1312;
  int32_t _M0L1iS1313;
  int32_t _M0L7_2abindS1325;
  int32_t _M0L1iS1326;
  int32_t _M0L7_2abindS1328;
  int32_t _M0L1iS1329;
  int32_t _M0L7_2abindS1331;
  int32_t _M0L1iS1332;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
  _M0L1nS1293 = _M0L1pS1294->$1;
  _M0L3p__S1295 = _M0L1pS1294->$0;
  _M0L2cmS1296 = _M0L3p__S1295->$0;
  _M0L2elS1297 = _M0L3p__S1295->$1;
  _M0L2ekS1298 = _M0L3p__S1295->$2;
  _M0L3ecaS1299 = _M0L3p__S1295->$3;
  _M0L2glS1300 = _M0L3p__S1295->$4;
  _M0L2gkS1301 = _M0L3p__S1295->$5;
  _M0L3gcaS1302 = _M0L3p__S1295->$6;
  _M0L6tau__eS1303 = _M0L3p__S1295->$7;
  _M0L6tau__iS1304 = _M0L3p__S1295->$8;
  _M0L2v1S1305 = _M0L3p__S1295->$9;
  _M0L2v2S1306 = _M0L3p__S1295->$10;
  _M0L2v3S1307 = _M0L3p__S1295->$11;
  _M0L2v4S1308 = _M0L3p__S1295->$12;
  _M0L3phiS1309 = _M0L3p__S1295->$13;
  _M0L4e__eS1310 = _M0L3p__S1295->$14;
  _M0L4e__iS1311 = _M0L3p__S1295->$15;
  _M0L7_2abindS1312 = 0;
  _M0L1iS1313 = _M0L7_2abindS1312;
  while (1) {
    if (_M0L1iS1313 < _M0L1nS1293) {
      struct _M0TPB5ArrayGfE* _M0L1vS4374 = _M0L1pS1294->$2;
      float _M0L1vS1314;
      struct _M0TPB5ArrayGfE* _M0L1wS4373;
      float _M0L1wS1315;
      float _M0L6_2atmpS4372;
      float _M0L6_2atmpS4371;
      float _M0L6_2atmpS4370;
      float _M0L6_2atmpS4369;
      float _M0L5m__ssS1316;
      struct _M0TPB5ArrayGfE* _M0L1iS4368;
      float _M0L6_2atmpS4365;
      float _M0L6_2atmpS4367;
      float _M0L6_2atmpS4366;
      float _M0L6_2atmpS4361;
      float _M0L6_2atmpS4364;
      float _M0L6_2atmpS4363;
      float _M0L6_2atmpS4362;
      float _M0L6_2atmpS4357;
      float _M0L6_2atmpS4360;
      float _M0L6_2atmpS4359;
      float _M0L6_2atmpS4358;
      float _M0L2dvS1317;
      float _M0L6_2atmpS4356;
      float _M0L6_2atmpS4355;
      float _M0L6_2atmpS4354;
      float _M0L6_2atmpS4353;
      float _M0L5n__ssS1318;
      float _M0L6_2atmpS4351;
      float _M0L6_2atmpS4352;
      float _M0L9cosh__argS1319;
      float _M0L6_2atmpS4348;
      float _M0L6_2atmpS4350;
      float _M0L6_2atmpS4349;
      float _M0L6_2atmpS4347;
      float _M0L9cosh__valS1320;
      float _M0L6_2atmpS4345;
      float _M0L3tauS1321;
      float _M0L6_2atmpS4344;
      float _M0L2dwS1322;
      struct _M0TPB5ArrayGfE* _M0L1vS4337;
      float _M0L6_2atmpS4340;
      float _M0L6_2atmpS4339;
      float _M0L6_2atmpS4338;
      struct _M0TPB5ArrayGfE* _M0L1wS4341;
      float _M0L6_2atmpS4343;
      float _M0L6_2atmpS4342;
      int32_t _M0L6_2atmpS4375;
      #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1vS1314 = _M0MPC15array5Array2atGfE(_M0L1vS4374, _M0L1iS1313);
      _M0L1wS4373 = _M0L1pS1294->$3;
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1wS1315 = _M0MPC15array5Array2atGfE(_M0L1wS4373, _M0L1iS1313);
      _M0L6_2atmpS4372 = _M0L1vS1314 - _M0L2v1S1305;
      _M0L6_2atmpS4371 = _M0L6_2atmpS4372 / _M0L2v2S1306;
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4370 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS4371);
      _M0L6_2atmpS4369 = 0x1p+0f + _M0L6_2atmpS4370;
      _M0L5m__ssS1316 = 0x1p-1f * _M0L6_2atmpS4369;
      _M0L1iS4368 = _M0L1pS1294->$5;
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4365 = _M0MPC15array5Array2atGfE(_M0L1iS4368, _M0L1iS1313);
      _M0L6_2atmpS4367 = _M0L2elS1297 - _M0L1vS1314;
      _M0L6_2atmpS4366 = _M0L2glS1300 * _M0L6_2atmpS4367;
      _M0L6_2atmpS4361 = _M0L6_2atmpS4365 + _M0L6_2atmpS4366;
      _M0L6_2atmpS4364 = _M0L3ecaS1299 - _M0L1vS1314;
      _M0L6_2atmpS4363 = _M0L3gcaS1302 * _M0L6_2atmpS4364;
      _M0L6_2atmpS4362 = _M0L6_2atmpS4363 * _M0L5m__ssS1316;
      _M0L6_2atmpS4357 = _M0L6_2atmpS4361 + _M0L6_2atmpS4362;
      _M0L6_2atmpS4360 = _M0L2ekS1298 - _M0L1vS1314;
      _M0L6_2atmpS4359 = _M0L2gkS1301 * _M0L6_2atmpS4360;
      _M0L6_2atmpS4358 = _M0L6_2atmpS4359 * _M0L1wS1315;
      _M0L2dvS1317 = _M0L6_2atmpS4357 + _M0L6_2atmpS4358;
      _M0L6_2atmpS4356 = _M0L1vS1314 - _M0L2v3S1307;
      _M0L6_2atmpS4355 = _M0L6_2atmpS4356 / _M0L2v4S1308;
      #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4354 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS4355);
      _M0L6_2atmpS4353 = 0x1p+0f + _M0L6_2atmpS4354;
      _M0L5n__ssS1318 = 0x1p-1f * _M0L6_2atmpS4353;
      _M0L6_2atmpS4351 = _M0L1vS1314 - _M0L2v3S1307;
      _M0L6_2atmpS4352 = 0x1p+1f * _M0L2v4S1308;
      _M0L9cosh__argS1319 = _M0L6_2atmpS4351 / _M0L6_2atmpS4352;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4348 = _M0FP26RiantR8snn__mbt4expf(_M0L9cosh__argS1319);
      _M0L6_2atmpS4350 = -_M0L9cosh__argS1319;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4349 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4350);
      _M0L6_2atmpS4347 = _M0L6_2atmpS4348 + _M0L6_2atmpS4349;
      _M0L9cosh__valS1320 = 0x1p-1f * _M0L6_2atmpS4347;
      _M0L6_2atmpS4345 = _M0L3phiS1309 * _M0L9cosh__valS1320;
      if (_M0L6_2atmpS4345 != 0x0p+0f) {
        float _M0L6_2atmpS4346 = _M0L3phiS1309 * _M0L9cosh__valS1320;
        _M0L3tauS1321 = 0x1p+0f / _M0L6_2atmpS4346;
      } else {
        _M0L3tauS1321 = 0x0p+0f;
      }
      _M0L6_2atmpS4344 = _M0L5n__ssS1318 - _M0L1wS1315;
      _M0L2dwS1322 = _M0L6_2atmpS4344 / _M0L3tauS1321;
      _M0L1vS4337 = _M0L1pS1294->$2;
      _M0L6_2atmpS4340 = _M0L2dtS1323 / _M0L2cmS1296;
      _M0L6_2atmpS4339 = _M0L6_2atmpS4340 * _M0L2dvS1317;
      _M0L6_2atmpS4338 = _M0L1vS1314 + _M0L6_2atmpS4339;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4337, _M0L1iS1313, _M0L6_2atmpS4338);
      _M0L1wS4341 = _M0L1pS1294->$3;
      _M0L6_2atmpS4343 = _M0L2dtS1323 * _M0L2dwS1322;
      _M0L6_2atmpS4342 = _M0L1wS1315 + _M0L6_2atmpS4343;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4341, _M0L1iS1313, _M0L6_2atmpS4342);
      _M0L6_2atmpS4375 = _M0L1iS1313 + 1;
      _M0L1iS1313 = _M0L6_2atmpS4375;
      continue;
    }
    break;
  }
  _M0L7_2abindS1325 = 0;
  _M0L1iS1326 = _M0L7_2abindS1325;
  while (1) {
    if (_M0L1iS1326 < _M0L1nS1293) {
      struct _M0TPB5ArrayGfE* _M0L1vS4376 = _M0L1pS1294->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS4394 = _M0L1pS1294->$2;
      float _M0L6_2atmpS4378;
      float _M0L6_2atmpS4380;
      struct _M0TPB5ArrayGfE* _M0L2geS4393;
      float _M0L6_2atmpS4389;
      struct _M0TPB5ArrayGfE* _M0L1vS4392;
      float _M0L6_2atmpS4391;
      float _M0L6_2atmpS4390;
      float _M0L6_2atmpS4382;
      struct _M0TPB5ArrayGfE* _M0L2giS4388;
      float _M0L6_2atmpS4384;
      struct _M0TPB5ArrayGfE* _M0L1vS4387;
      float _M0L6_2atmpS4386;
      float _M0L6_2atmpS4385;
      float _M0L6_2atmpS4383;
      float _M0L6_2atmpS4381;
      float _M0L6_2atmpS4379;
      float _M0L6_2atmpS4377;
      int32_t _M0L6_2atmpS4395;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4378 = _M0MPC15array5Array2atGfE(_M0L1vS4394, _M0L1iS1326);
      _M0L6_2atmpS4380 = _M0L2dtS1323 / _M0L2cmS1296;
      _M0L2geS4393 = _M0L1pS1294->$6;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4389 = _M0MPC15array5Array2atGfE(_M0L2geS4393, _M0L1iS1326);
      _M0L1vS4392 = _M0L1pS1294->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4391 = _M0MPC15array5Array2atGfE(_M0L1vS4392, _M0L1iS1326);
      _M0L6_2atmpS4390 = _M0L4e__eS1310 - _M0L6_2atmpS4391;
      _M0L6_2atmpS4382 = _M0L6_2atmpS4389 * _M0L6_2atmpS4390;
      _M0L2giS4388 = _M0L1pS1294->$7;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4384 = _M0MPC15array5Array2atGfE(_M0L2giS4388, _M0L1iS1326);
      _M0L1vS4387 = _M0L1pS1294->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4386 = _M0MPC15array5Array2atGfE(_M0L1vS4387, _M0L1iS1326);
      _M0L6_2atmpS4385 = _M0L4e__iS1311 - _M0L6_2atmpS4386;
      _M0L6_2atmpS4383 = _M0L6_2atmpS4384 * _M0L6_2atmpS4385;
      _M0L6_2atmpS4381 = _M0L6_2atmpS4382 + _M0L6_2atmpS4383;
      _M0L6_2atmpS4379 = _M0L6_2atmpS4380 * _M0L6_2atmpS4381;
      _M0L6_2atmpS4377 = _M0L6_2atmpS4378 + _M0L6_2atmpS4379;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4376, _M0L1iS1326, _M0L6_2atmpS4377);
      _M0L6_2atmpS4395 = _M0L1iS1326 + 1;
      _M0L1iS1326 = _M0L6_2atmpS4395;
      continue;
    }
    break;
  }
  _M0L7_2abindS1328 = 0;
  _M0L1iS1329 = _M0L7_2abindS1328;
  while (1) {
    if (_M0L1iS1329 < _M0L1nS1293) {
      struct _M0TPB5ArrayGfE* _M0L2geS4396 = _M0L1pS1294->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS4404 = _M0L1pS1294->$6;
      float _M0L6_2atmpS4398;
      struct _M0TPB5ArrayGfE* _M0L2geS4403;
      float _M0L6_2atmpS4402;
      float _M0L6_2atmpS4401;
      float _M0L6_2atmpS4400;
      float _M0L6_2atmpS4399;
      float _M0L6_2atmpS4397;
      struct _M0TPB5ArrayGfE* _M0L2giS4405;
      struct _M0TPB5ArrayGfE* _M0L2giS4413;
      float _M0L6_2atmpS4407;
      struct _M0TPB5ArrayGfE* _M0L2giS4412;
      float _M0L6_2atmpS4411;
      float _M0L6_2atmpS4410;
      float _M0L6_2atmpS4409;
      float _M0L6_2atmpS4408;
      float _M0L6_2atmpS4406;
      int32_t _M0L6_2atmpS4414;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4398 = _M0MPC15array5Array2atGfE(_M0L2geS4404, _M0L1iS1329);
      _M0L2geS4403 = _M0L1pS1294->$6;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4402 = _M0MPC15array5Array2atGfE(_M0L2geS4403, _M0L1iS1329);
      _M0L6_2atmpS4401 = -_M0L6_2atmpS4402;
      _M0L6_2atmpS4400 = _M0L6_2atmpS4401 / _M0L6tau__eS1303;
      _M0L6_2atmpS4399 = _M0L2dtS1323 * _M0L6_2atmpS4400;
      _M0L6_2atmpS4397 = _M0L6_2atmpS4398 + _M0L6_2atmpS4399;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4396, _M0L1iS1329, _M0L6_2atmpS4397);
      _M0L2giS4405 = _M0L1pS1294->$7;
      _M0L2giS4413 = _M0L1pS1294->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4407 = _M0MPC15array5Array2atGfE(_M0L2giS4413, _M0L1iS1329);
      _M0L2giS4412 = _M0L1pS1294->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4411 = _M0MPC15array5Array2atGfE(_M0L2giS4412, _M0L1iS1329);
      _M0L6_2atmpS4410 = -_M0L6_2atmpS4411;
      _M0L6_2atmpS4409 = _M0L6_2atmpS4410 / _M0L6tau__iS1304;
      _M0L6_2atmpS4408 = _M0L2dtS1323 * _M0L6_2atmpS4409;
      _M0L6_2atmpS4406 = _M0L6_2atmpS4407 + _M0L6_2atmpS4408;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4405, _M0L1iS1329, _M0L6_2atmpS4406);
      _M0L6_2atmpS4414 = _M0L1iS1329 + 1;
      _M0L1iS1329 = _M0L6_2atmpS4414;
      continue;
    }
    break;
  }
  _M0L7_2abindS1331 = 0;
  _M0L1iS1332 = _M0L7_2abindS1331;
  while (1) {
    if (_M0L1iS1332 < _M0L1nS1293) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4415 = _M0L1pS1294->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS4418 = _M0L1pS1294->$2;
      float _M0L6_2atmpS4417;
      int32_t _M0L6_2atmpS4416;
      int32_t _M0L6_2atmpS4419;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4417 = _M0MPC15array5Array2atGfE(_M0L1vS4418, _M0L1iS1332);
      _M0L6_2atmpS4416 = _M0L6_2atmpS4417 > 0x1.4p+4f;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4415, _M0L1iS1332, _M0L6_2atmpS4416);
      _M0L6_2atmpS4419 = _M0L1iS1332 + 1;
      _M0L1iS1332 = _M0L6_2atmpS4419;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS1262,
  float _M0L2dtS1274
) {
  int32_t _M0L1nS1261;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S1263;
  float _M0L1aS1264;
  float _M0L1bS1265;
  float _M0L1cS1266;
  float _M0L1dS1267;
  float _M0L6tau__eS1268;
  float _M0L6tau__iS1269;
  float _M0L4e__eS1270;
  float _M0L4e__iS1271;
  int32_t _M0L7_2abindS1272;
  int32_t _M0L1iS1273;
  int32_t _M0L7_2abindS1276;
  int32_t _M0L1iS1277;
  int32_t _M0L7_2abindS1283;
  int32_t _M0L1iS1284;
  int32_t _M0L7_2abindS1287;
  int32_t _M0L1iS1288;
  int32_t _M0L7_2abindS1290;
  int32_t _M0L1iS1291;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS1261 = _M0L1pS1262->$1;
  _M0L3p__S1263 = _M0L1pS1262->$0;
  _M0L1aS1264 = _M0L3p__S1263->$0;
  _M0L1bS1265 = _M0L3p__S1263->$1;
  _M0L1cS1266 = _M0L3p__S1263->$2;
  _M0L1dS1267 = _M0L3p__S1263->$3;
  _M0L6tau__eS1268 = _M0L3p__S1263->$4;
  _M0L6tau__iS1269 = _M0L3p__S1263->$5;
  _M0L4e__eS1270 = _M0L3p__S1263->$6;
  _M0L4e__iS1271 = _M0L3p__S1263->$7;
  _M0L7_2abindS1272 = 0;
  _M0L1iS1273 = _M0L7_2abindS1272;
  while (1) {
    if (_M0L1iS1273 < _M0L1nS1261) {
      struct _M0TPB5ArrayGfE* _M0L2geS4245 = _M0L1pS1262->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS4253 = _M0L1pS1262->$6;
      float _M0L6_2atmpS4247;
      struct _M0TPB5ArrayGfE* _M0L2geS4252;
      float _M0L6_2atmpS4251;
      float _M0L6_2atmpS4250;
      float _M0L6_2atmpS4249;
      float _M0L6_2atmpS4248;
      float _M0L6_2atmpS4246;
      struct _M0TPB5ArrayGfE* _M0L2giS4254;
      struct _M0TPB5ArrayGfE* _M0L2giS4262;
      float _M0L6_2atmpS4256;
      struct _M0TPB5ArrayGfE* _M0L2giS4261;
      float _M0L6_2atmpS4260;
      float _M0L6_2atmpS4259;
      float _M0L6_2atmpS4258;
      float _M0L6_2atmpS4257;
      float _M0L6_2atmpS4255;
      int32_t _M0L6_2atmpS4263;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4247 = _M0MPC15array5Array2atGfE(_M0L2geS4253, _M0L1iS1273);
      _M0L2geS4252 = _M0L1pS1262->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4251 = _M0MPC15array5Array2atGfE(_M0L2geS4252, _M0L1iS1273);
      _M0L6_2atmpS4250 = -_M0L6_2atmpS4251;
      _M0L6_2atmpS4249 = _M0L2dtS1274 * _M0L6_2atmpS4250;
      _M0L6_2atmpS4248 = _M0L6_2atmpS4249 / _M0L6tau__eS1268;
      _M0L6_2atmpS4246 = _M0L6_2atmpS4247 + _M0L6_2atmpS4248;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4245, _M0L1iS1273, _M0L6_2atmpS4246);
      _M0L2giS4254 = _M0L1pS1262->$7;
      _M0L2giS4262 = _M0L1pS1262->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4256 = _M0MPC15array5Array2atGfE(_M0L2giS4262, _M0L1iS1273);
      _M0L2giS4261 = _M0L1pS1262->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4260 = _M0MPC15array5Array2atGfE(_M0L2giS4261, _M0L1iS1273);
      _M0L6_2atmpS4259 = -_M0L6_2atmpS4260;
      _M0L6_2atmpS4258 = _M0L2dtS1274 * _M0L6_2atmpS4259;
      _M0L6_2atmpS4257 = _M0L6_2atmpS4258 / _M0L6tau__iS1269;
      _M0L6_2atmpS4255 = _M0L6_2atmpS4256 + _M0L6_2atmpS4257;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4254, _M0L1iS1273, _M0L6_2atmpS4255);
      _M0L6_2atmpS4263 = _M0L1iS1273 + 1;
      _M0L1iS1273 = _M0L6_2atmpS4263;
      continue;
    }
    break;
  }
  _M0L7_2abindS1276 = 0;
  _M0L1iS1277 = _M0L7_2abindS1276;
  while (1) {
    if (_M0L1iS1277 < _M0L1nS1261) {
      struct _M0TPB5ArrayGfE* _M0L1vS4289 = _M0L1pS1262->$2;
      float _M0L1vS1278;
      struct _M0TPB5ArrayGfE* _M0L1uS4288;
      float _M0L1uS1279;
      struct _M0TPB5ArrayGfE* _M0L1iS4287;
      float _M0L2iiS1280;
      struct _M0TPB5ArrayGfE* _M0L1vS4264;
      float _M0L6_2atmpS4267;
      float _M0L6_2atmpS4274;
      float _M0L6_2atmpS4272;
      float _M0L6_2atmpS4273;
      float _M0L6_2atmpS4271;
      float _M0L6_2atmpS4270;
      float _M0L6_2atmpS4269;
      float _M0L6_2atmpS4268;
      float _M0L6_2atmpS4266;
      float _M0L6_2atmpS4265;
      struct _M0TPB5ArrayGfE* _M0L1vS4286;
      float _M0L2v2S1281;
      struct _M0TPB5ArrayGfE* _M0L1vS4275;
      float _M0L6_2atmpS4278;
      float _M0L6_2atmpS4285;
      float _M0L6_2atmpS4283;
      float _M0L6_2atmpS4284;
      float _M0L6_2atmpS4282;
      float _M0L6_2atmpS4281;
      float _M0L6_2atmpS4280;
      float _M0L6_2atmpS4279;
      float _M0L6_2atmpS4277;
      float _M0L6_2atmpS4276;
      int32_t _M0L6_2atmpS4290;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1278 = _M0MPC15array5Array2atGfE(_M0L1vS4289, _M0L1iS1277);
      _M0L1uS4288 = _M0L1pS1262->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS1279 = _M0MPC15array5Array2atGfE(_M0L1uS4288, _M0L1iS1277);
      _M0L1iS4287 = _M0L1pS1262->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS1280 = _M0MPC15array5Array2atGfE(_M0L1iS4287, _M0L1iS1277);
      _M0L1vS4264 = _M0L1pS1262->$2;
      _M0L6_2atmpS4267 = 0x1p-1f * _M0L2dtS1274;
      _M0L6_2atmpS4274 = 0x1.47ae147ae147bp-5f * _M0L1vS1278;
      _M0L6_2atmpS4272 = _M0L6_2atmpS4274 * _M0L1vS1278;
      _M0L6_2atmpS4273 = 0x1.4p+2f * _M0L1vS1278;
      _M0L6_2atmpS4271 = _M0L6_2atmpS4272 + _M0L6_2atmpS4273;
      _M0L6_2atmpS4270 = _M0L6_2atmpS4271 + 0x1.18p+7f;
      _M0L6_2atmpS4269 = _M0L6_2atmpS4270 - _M0L1uS1279;
      _M0L6_2atmpS4268 = _M0L6_2atmpS4269 + _M0L2iiS1280;
      _M0L6_2atmpS4266 = _M0L6_2atmpS4267 * _M0L6_2atmpS4268;
      _M0L6_2atmpS4265 = _M0L1vS1278 + _M0L6_2atmpS4266;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4264, _M0L1iS1277, _M0L6_2atmpS4265);
      _M0L1vS4286 = _M0L1pS1262->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S1281 = _M0MPC15array5Array2atGfE(_M0L1vS4286, _M0L1iS1277);
      _M0L1vS4275 = _M0L1pS1262->$2;
      _M0L6_2atmpS4278 = 0x1p-1f * _M0L2dtS1274;
      _M0L6_2atmpS4285 = 0x1.47ae147ae147bp-5f * _M0L2v2S1281;
      _M0L6_2atmpS4283 = _M0L6_2atmpS4285 * _M0L2v2S1281;
      _M0L6_2atmpS4284 = 0x1.4p+2f * _M0L2v2S1281;
      _M0L6_2atmpS4282 = _M0L6_2atmpS4283 + _M0L6_2atmpS4284;
      _M0L6_2atmpS4281 = _M0L6_2atmpS4282 + 0x1.18p+7f;
      _M0L6_2atmpS4280 = _M0L6_2atmpS4281 - _M0L1uS1279;
      _M0L6_2atmpS4279 = _M0L6_2atmpS4280 + _M0L2iiS1280;
      _M0L6_2atmpS4277 = _M0L6_2atmpS4278 * _M0L6_2atmpS4279;
      _M0L6_2atmpS4276 = _M0L2v2S1281 + _M0L6_2atmpS4277;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4275, _M0L1iS1277, _M0L6_2atmpS4276);
      _M0L6_2atmpS4290 = _M0L1iS1277 + 1;
      _M0L1iS1277 = _M0L6_2atmpS4290;
      continue;
    }
    break;
  }
  _M0L7_2abindS1283 = 0;
  _M0L1iS1284 = _M0L7_2abindS1283;
  while (1) {
    if (_M0L1iS1284 < _M0L1nS1261) {
      struct _M0TPB5ArrayGfE* _M0L1vS4301 = _M0L1pS1262->$2;
      float _M0L1vS1285;
      struct _M0TPB5ArrayGfE* _M0L1uS4291;
      struct _M0TPB5ArrayGfE* _M0L1uS4300;
      float _M0L6_2atmpS4293;
      float _M0L6_2atmpS4295;
      float _M0L6_2atmpS4297;
      struct _M0TPB5ArrayGfE* _M0L1uS4299;
      float _M0L6_2atmpS4298;
      float _M0L6_2atmpS4296;
      float _M0L6_2atmpS4294;
      float _M0L6_2atmpS4292;
      int32_t _M0L6_2atmpS4302;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1285 = _M0MPC15array5Array2atGfE(_M0L1vS4301, _M0L1iS1284);
      _M0L1uS4291 = _M0L1pS1262->$3;
      _M0L1uS4300 = _M0L1pS1262->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4293 = _M0MPC15array5Array2atGfE(_M0L1uS4300, _M0L1iS1284);
      _M0L6_2atmpS4295 = _M0L2dtS1274 * _M0L1aS1264;
      _M0L6_2atmpS4297 = _M0L1bS1265 * _M0L1vS1285;
      _M0L1uS4299 = _M0L1pS1262->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4298 = _M0MPC15array5Array2atGfE(_M0L1uS4299, _M0L1iS1284);
      _M0L6_2atmpS4296 = _M0L6_2atmpS4297 - _M0L6_2atmpS4298;
      _M0L6_2atmpS4294 = _M0L6_2atmpS4295 * _M0L6_2atmpS4296;
      _M0L6_2atmpS4292 = _M0L6_2atmpS4293 + _M0L6_2atmpS4294;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS4291, _M0L1iS1284, _M0L6_2atmpS4292);
      _M0L6_2atmpS4302 = _M0L1iS1284 + 1;
      _M0L1iS1284 = _M0L6_2atmpS4302;
      continue;
    }
    break;
  }
  _M0L7_2abindS1287 = 0;
  _M0L1iS1288 = _M0L7_2abindS1287;
  while (1) {
    if (_M0L1iS1288 < _M0L1nS1261) {
      struct _M0TPB5ArrayGfE* _M0L1vS4303 = _M0L1pS1262->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS4320 = _M0L1pS1262->$2;
      float _M0L6_2atmpS4305;
      struct _M0TPB5ArrayGfE* _M0L2geS4319;
      float _M0L6_2atmpS4315;
      struct _M0TPB5ArrayGfE* _M0L1vS4318;
      float _M0L6_2atmpS4317;
      float _M0L6_2atmpS4316;
      float _M0L6_2atmpS4308;
      struct _M0TPB5ArrayGfE* _M0L2giS4314;
      float _M0L6_2atmpS4310;
      struct _M0TPB5ArrayGfE* _M0L1vS4313;
      float _M0L6_2atmpS4312;
      float _M0L6_2atmpS4311;
      float _M0L6_2atmpS4309;
      float _M0L6_2atmpS4307;
      float _M0L6_2atmpS4306;
      float _M0L6_2atmpS4304;
      int32_t _M0L6_2atmpS4321;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4305 = _M0MPC15array5Array2atGfE(_M0L1vS4320, _M0L1iS1288);
      _M0L2geS4319 = _M0L1pS1262->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4315 = _M0MPC15array5Array2atGfE(_M0L2geS4319, _M0L1iS1288);
      _M0L1vS4318 = _M0L1pS1262->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4317 = _M0MPC15array5Array2atGfE(_M0L1vS4318, _M0L1iS1288);
      _M0L6_2atmpS4316 = _M0L4e__eS1270 - _M0L6_2atmpS4317;
      _M0L6_2atmpS4308 = _M0L6_2atmpS4315 * _M0L6_2atmpS4316;
      _M0L2giS4314 = _M0L1pS1262->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4310 = _M0MPC15array5Array2atGfE(_M0L2giS4314, _M0L1iS1288);
      _M0L1vS4313 = _M0L1pS1262->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4312 = _M0MPC15array5Array2atGfE(_M0L1vS4313, _M0L1iS1288);
      _M0L6_2atmpS4311 = _M0L4e__iS1271 - _M0L6_2atmpS4312;
      _M0L6_2atmpS4309 = _M0L6_2atmpS4310 * _M0L6_2atmpS4311;
      _M0L6_2atmpS4307 = _M0L6_2atmpS4308 + _M0L6_2atmpS4309;
      _M0L6_2atmpS4306 = _M0L2dtS1274 * _M0L6_2atmpS4307;
      _M0L6_2atmpS4304 = _M0L6_2atmpS4305 + _M0L6_2atmpS4306;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4303, _M0L1iS1288, _M0L6_2atmpS4304);
      _M0L6_2atmpS4321 = _M0L1iS1288 + 1;
      _M0L1iS1288 = _M0L6_2atmpS4321;
      continue;
    }
    break;
  }
  _M0L7_2abindS1290 = 0;
  _M0L1iS1291 = _M0L7_2abindS1290;
  while (1) {
    if (_M0L1iS1291 < _M0L1nS1261) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4322 = _M0L1pS1262->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS4325 = _M0L1pS1262->$2;
      float _M0L6_2atmpS4324;
      int32_t _M0L6_2atmpS4323;
      struct _M0TPB5ArrayGfE* _M0L1vS4326;
      struct _M0TPB5ArrayGbE* _M0L4fireS4328;
      float _M0L6_2atmpS4327;
      struct _M0TPB5ArrayGfE* _M0L1uS4330;
      struct _M0TPB5ArrayGfE* _M0L1uS4335;
      float _M0L6_2atmpS4332;
      struct _M0TPB5ArrayGbE* _M0L4fireS4334;
      float _M0L6_2atmpS4333;
      float _M0L6_2atmpS4331;
      int32_t _M0L6_2atmpS4336;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4324 = _M0MPC15array5Array2atGfE(_M0L1vS4325, _M0L1iS1291);
      _M0L6_2atmpS4323 = _M0L6_2atmpS4324 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4322, _M0L1iS1291, _M0L6_2atmpS4323);
      _M0L1vS4326 = _M0L1pS1262->$2;
      _M0L4fireS4328 = _M0L1pS1262->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4328, _M0L1iS1291)) {
        _M0L6_2atmpS4327 = _M0L1cS1266;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4329 = _M0L1pS1262->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS4327
        = _M0MPC15array5Array2atGfE(_M0L1vS4329, _M0L1iS1291);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4326, _M0L1iS1291, _M0L6_2atmpS4327);
      _M0L1uS4330 = _M0L1pS1262->$3;
      _M0L1uS4335 = _M0L1pS1262->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4332 = _M0MPC15array5Array2atGfE(_M0L1uS4335, _M0L1iS1291);
      _M0L4fireS4334 = _M0L1pS1262->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4334, _M0L1iS1291)) {
        _M0L6_2atmpS4333 = _M0L1dS1267;
      } else {
        _M0L6_2atmpS4333 = 0x0p+0f;
      }
      _M0L6_2atmpS4331 = _M0L6_2atmpS4332 + _M0L6_2atmpS4333;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS4330, _M0L1iS1291, _M0L6_2atmpS4331);
      _M0L6_2atmpS4336 = _M0L1iS1291 + 1;
      _M0L1iS1291 = _M0L6_2atmpS4336;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS1218,
  float _M0L2dtS1244
) {
  int32_t _M0L1nS1217;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S1219;
  float _M0L2cmS1220;
  float _M0L2glS1221;
  float _M0L2elS1222;
  float _M0L2ekS1223;
  float _M0L2enS1224;
  float _M0L2gnS1225;
  float _M0L2gkS1226;
  float _M0L2vtS1227;
  float _M0L6tau__eS1228;
  float _M0L6tau__iS1229;
  float _M0L4e__eS1230;
  float _M0L4e__iS1231;
  int32_t _M0L7_2abindS1232;
  int32_t _M0L1iS1233;
  int32_t _M0L7_2abindS1258;
  int32_t _M0L1iS1259;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS1217 = _M0L1pS1218->$1;
  _M0L3p__S1219 = _M0L1pS1218->$0;
  _M0L2cmS1220 = _M0L3p__S1219->$0;
  _M0L2glS1221 = _M0L3p__S1219->$1;
  _M0L2elS1222 = _M0L3p__S1219->$2;
  _M0L2ekS1223 = _M0L3p__S1219->$3;
  _M0L2enS1224 = _M0L3p__S1219->$4;
  _M0L2gnS1225 = _M0L3p__S1219->$5;
  _M0L2gkS1226 = _M0L3p__S1219->$6;
  _M0L2vtS1227 = _M0L3p__S1219->$7;
  _M0L6tau__eS1228 = _M0L3p__S1219->$8;
  _M0L6tau__iS1229 = _M0L3p__S1219->$9;
  _M0L4e__eS1230 = _M0L3p__S1219->$10;
  _M0L4e__iS1231 = _M0L3p__S1219->$11;
  _M0L7_2abindS1232 = 0;
  _M0L1iS1233 = _M0L7_2abindS1232;
  while (1) {
    if (_M0L1iS1233 < _M0L1nS1217) {
      struct _M0TPB5ArrayGfE* _M0L1vS4238 = _M0L1pS1218->$2;
      float _M0L1vS1234;
      struct _M0TPB5ArrayGfE* _M0L1mS4237;
      float _M0L1mS1235;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4236;
      float _M0L2nnS1236;
      struct _M0TPB5ArrayGfE* _M0L1hS4235;
      float _M0L1hS1237;
      struct _M0TPB5ArrayGfE* _M0L2geS4234;
      float _M0L2geS1238;
      struct _M0TPB5ArrayGfE* _M0L2giS4233;
      float _M0L2giS1239;
      struct _M0TPB5ArrayGbE* _M0L4fireS4136;
      float _M0L6_2atmpS4232;
      float _M0L7am__numS1240;
      float _M0L6_2atmpS4231;
      float _M0L7bm__numS1241;
      float _M0L6_2atmpS4226;
      float _M0L6_2atmpS4225;
      float _M0L6_2atmpS4224;
      float _M0L2amS1242;
      float _M0L6_2atmpS4219;
      float _M0L6_2atmpS4218;
      float _M0L6_2atmpS4217;
      float _M0L2bmS1243;
      struct _M0TPB5ArrayGfE* _M0L1mS4137;
      float _M0L6_2atmpS4143;
      float _M0L6_2atmpS4141;
      float _M0L6_2atmpS4142;
      float _M0L6_2atmpS4140;
      float _M0L6_2atmpS4139;
      float _M0L6_2atmpS4138;
      float _M0L6_2atmpS4216;
      float _M0L7an__numS1245;
      float _M0L6_2atmpS4211;
      float _M0L6_2atmpS4210;
      float _M0L6_2atmpS4209;
      float _M0L2anS1246;
      float _M0L6_2atmpS4208;
      float _M0L6_2atmpS4207;
      float _M0L6_2atmpS4206;
      float _M0L6_2atmpS4205;
      float _M0L2bnS1247;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4144;
      float _M0L6_2atmpS4150;
      float _M0L6_2atmpS4148;
      float _M0L6_2atmpS4149;
      float _M0L6_2atmpS4147;
      float _M0L6_2atmpS4146;
      float _M0L6_2atmpS4145;
      float _M0L6_2atmpS4204;
      float _M0L6_2atmpS4203;
      float _M0L6_2atmpS4202;
      float _M0L6_2atmpS4201;
      float _M0L2ahS1248;
      float _M0L6_2atmpS4200;
      float _M0L6_2atmpS4199;
      float _M0L6_2atmpS4198;
      float _M0L6_2atmpS4197;
      float _M0L9bh__denomS1249;
      float _M0L2bhS1250;
      struct _M0TPB5ArrayGfE* _M0L1hS4151;
      float _M0L6_2atmpS4157;
      float _M0L6_2atmpS4155;
      float _M0L6_2atmpS4156;
      float _M0L6_2atmpS4154;
      float _M0L6_2atmpS4153;
      float _M0L6_2atmpS4152;
      struct _M0TPB5ArrayGfE* _M0L1mS4196;
      float _M0L6m__newS1251;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4195;
      float _M0L6n__newS1252;
      struct _M0TPB5ArrayGfE* _M0L1hS4194;
      float _M0L6h__newS1253;
      float _M0L6_2atmpS4193;
      float _M0L6_2atmpS4192;
      float _M0L3m3hS1254;
      float _M0L6_2atmpS4191;
      float _M0L6_2atmpS4190;
      float _M0L2n4S1255;
      struct _M0TPB5ArrayGfE* _M0L1iS4189;
      float _M0L6_2atmpS4186;
      float _M0L6_2atmpS4188;
      float _M0L6_2atmpS4187;
      float _M0L6_2atmpS4183;
      float _M0L6_2atmpS4185;
      float _M0L6_2atmpS4184;
      float _M0L6_2atmpS4180;
      float _M0L6_2atmpS4182;
      float _M0L6_2atmpS4181;
      float _M0L6_2atmpS4176;
      float _M0L6_2atmpS4178;
      float _M0L6_2atmpS4179;
      float _M0L6_2atmpS4177;
      float _M0L6_2atmpS4172;
      float _M0L6_2atmpS4174;
      float _M0L6_2atmpS4175;
      float _M0L6_2atmpS4173;
      float _M0L7currentS1256;
      struct _M0TPB5ArrayGfE* _M0L1vS4158;
      float _M0L6_2atmpS4161;
      float _M0L6_2atmpS4160;
      float _M0L6_2atmpS4159;
      struct _M0TPB5ArrayGfE* _M0L2geS4162;
      float _M0L6_2atmpS4166;
      float _M0L6_2atmpS4165;
      float _M0L6_2atmpS4164;
      float _M0L6_2atmpS4163;
      struct _M0TPB5ArrayGfE* _M0L2giS4167;
      float _M0L6_2atmpS4171;
      float _M0L6_2atmpS4170;
      float _M0L6_2atmpS4169;
      float _M0L6_2atmpS4168;
      int32_t _M0L6_2atmpS4239;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS1234 = _M0MPC15array5Array2atGfE(_M0L1vS4238, _M0L1iS1233);
      _M0L1mS4237 = _M0L1pS1218->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS1235 = _M0MPC15array5Array2atGfE(_M0L1mS4237, _M0L1iS1233);
      _M0L7n__gateS4236 = _M0L1pS1218->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1236
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4236, _M0L1iS1233);
      _M0L1hS4235 = _M0L1pS1218->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1237 = _M0MPC15array5Array2atGfE(_M0L1hS4235, _M0L1iS1233);
      _M0L2geS4234 = _M0L1pS1218->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1238 = _M0MPC15array5Array2atGfE(_M0L2geS4234, _M0L1iS1233);
      _M0L2giS4233 = _M0L1pS1218->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1239 = _M0MPC15array5Array2atGfE(_M0L2giS4233, _M0L1iS1233);
      _M0L4fireS4136 = _M0L1pS1218->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4136, _M0L1iS1233, 0);
      _M0L6_2atmpS4232 = 0x1.ap+3f - _M0L1vS1234;
      _M0L7am__numS1240 = _M0L6_2atmpS4232 + _M0L2vtS1227;
      _M0L6_2atmpS4231 = _M0L1vS1234 - _M0L2vtS1227;
      _M0L7bm__numS1241 = _M0L6_2atmpS4231 - 0x1.4p+5f;
      _M0L6_2atmpS4226 = _M0L7am__numS1240 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4225 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4226);
      _M0L6_2atmpS4224 = _M0L6_2atmpS4225 - 0x1p+0f;
      if (_M0L6_2atmpS4224 != 0x0p+0f) {
        float _M0L6_2atmpS4227 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1240;
        float _M0L6_2atmpS4230 = _M0L7am__numS1240 / 0x1p+2f;
        float _M0L6_2atmpS4229;
        float _M0L6_2atmpS4228;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4229 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4230);
        _M0L6_2atmpS4228 = _M0L6_2atmpS4229 - 0x1p+0f;
        _M0L2amS1242 = _M0L6_2atmpS4227 / _M0L6_2atmpS4228;
      } else {
        _M0L2amS1242 = 0x0p+0f;
      }
      _M0L6_2atmpS4219 = _M0L7bm__numS1241 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4218 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4219);
      _M0L6_2atmpS4217 = _M0L6_2atmpS4218 - 0x1p+0f;
      if (_M0L6_2atmpS4217 != 0x0p+0f) {
        float _M0L6_2atmpS4220 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1241;
        float _M0L6_2atmpS4223 = _M0L7bm__numS1241 / 0x1.4p+2f;
        float _M0L6_2atmpS4222;
        float _M0L6_2atmpS4221;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4222 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4223);
        _M0L6_2atmpS4221 = _M0L6_2atmpS4222 - 0x1p+0f;
        _M0L2bmS1243 = _M0L6_2atmpS4220 / _M0L6_2atmpS4221;
      } else {
        _M0L2bmS1243 = 0x0p+0f;
      }
      _M0L1mS4137 = _M0L1pS1218->$3;
      _M0L6_2atmpS4143 = 0x1p+0f - _M0L1mS1235;
      _M0L6_2atmpS4141 = _M0L2amS1242 * _M0L6_2atmpS4143;
      _M0L6_2atmpS4142 = _M0L2bmS1243 * _M0L1mS1235;
      _M0L6_2atmpS4140 = _M0L6_2atmpS4141 - _M0L6_2atmpS4142;
      _M0L6_2atmpS4139 = _M0L2dtS1244 * _M0L6_2atmpS4140;
      _M0L6_2atmpS4138 = _M0L1mS1235 + _M0L6_2atmpS4139;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS4137, _M0L1iS1233, _M0L6_2atmpS4138);
      _M0L6_2atmpS4216 = 0x1.ep+3f - _M0L1vS1234;
      _M0L7an__numS1245 = _M0L6_2atmpS4216 + _M0L2vtS1227;
      _M0L6_2atmpS4211 = _M0L7an__numS1245 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4210 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4211);
      _M0L6_2atmpS4209 = _M0L6_2atmpS4210 - 0x1p+0f;
      if (_M0L6_2atmpS4209 != 0x0p+0f) {
        float _M0L6_2atmpS4212 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1245;
        float _M0L6_2atmpS4215 = _M0L7an__numS1245 / 0x1.4p+2f;
        float _M0L6_2atmpS4214;
        float _M0L6_2atmpS4213;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4214 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4215);
        _M0L6_2atmpS4213 = _M0L6_2atmpS4214 - 0x1p+0f;
        _M0L2anS1246 = _M0L6_2atmpS4212 / _M0L6_2atmpS4213;
      } else {
        _M0L2anS1246 = 0x0p+0f;
      }
      _M0L6_2atmpS4208 = 0x1.4p+3f - _M0L1vS1234;
      _M0L6_2atmpS4207 = _M0L6_2atmpS4208 + _M0L2vtS1227;
      _M0L6_2atmpS4206 = _M0L6_2atmpS4207 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4205 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4206);
      _M0L2bnS1247 = 0x1p-1f * _M0L6_2atmpS4205;
      _M0L7n__gateS4144 = _M0L1pS1218->$4;
      _M0L6_2atmpS4150 = 0x1p+0f - _M0L2nnS1236;
      _M0L6_2atmpS4148 = _M0L2anS1246 * _M0L6_2atmpS4150;
      _M0L6_2atmpS4149 = _M0L2bnS1247 * _M0L2nnS1236;
      _M0L6_2atmpS4147 = _M0L6_2atmpS4148 - _M0L6_2atmpS4149;
      _M0L6_2atmpS4146 = _M0L2dtS1244 * _M0L6_2atmpS4147;
      _M0L6_2atmpS4145 = _M0L2nnS1236 + _M0L6_2atmpS4146;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS4144, _M0L1iS1233, _M0L6_2atmpS4145);
      _M0L6_2atmpS4204 = 0x1.1p+4f - _M0L1vS1234;
      _M0L6_2atmpS4203 = _M0L6_2atmpS4204 + _M0L2vtS1227;
      _M0L6_2atmpS4202 = _M0L6_2atmpS4203 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4201 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4202);
      _M0L2ahS1248 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS4201;
      _M0L6_2atmpS4200 = 0x1.4p+5f - _M0L1vS1234;
      _M0L6_2atmpS4199 = _M0L6_2atmpS4200 + _M0L2vtS1227;
      _M0L6_2atmpS4198 = _M0L6_2atmpS4199 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4197 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4198);
      _M0L9bh__denomS1249 = 0x1p+0f + _M0L6_2atmpS4197;
      if (_M0L9bh__denomS1249 != 0x0p+0f) {
        _M0L2bhS1250 = 0x1p+2f / _M0L9bh__denomS1249;
      } else {
        _M0L2bhS1250 = 0x0p+0f;
      }
      _M0L1hS4151 = _M0L1pS1218->$5;
      _M0L6_2atmpS4157 = 0x1p+0f - _M0L1hS1237;
      _M0L6_2atmpS4155 = _M0L2ahS1248 * _M0L6_2atmpS4157;
      _M0L6_2atmpS4156 = _M0L2bhS1250 * _M0L1hS1237;
      _M0L6_2atmpS4154 = _M0L6_2atmpS4155 - _M0L6_2atmpS4156;
      _M0L6_2atmpS4153 = _M0L2dtS1244 * _M0L6_2atmpS4154;
      _M0L6_2atmpS4152 = _M0L1hS1237 + _M0L6_2atmpS4153;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS4151, _M0L1iS1233, _M0L6_2atmpS4152);
      _M0L1mS4196 = _M0L1pS1218->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1251 = _M0MPC15array5Array2atGfE(_M0L1mS4196, _M0L1iS1233);
      _M0L7n__gateS4195 = _M0L1pS1218->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1252
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4195, _M0L1iS1233);
      _M0L1hS4194 = _M0L1pS1218->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1253 = _M0MPC15array5Array2atGfE(_M0L1hS4194, _M0L1iS1233);
      _M0L6_2atmpS4193 = _M0L6m__newS1251 * _M0L6m__newS1251;
      _M0L6_2atmpS4192 = _M0L6_2atmpS4193 * _M0L6m__newS1251;
      _M0L3m3hS1254 = _M0L6_2atmpS4192 * _M0L6h__newS1253;
      _M0L6_2atmpS4191 = _M0L6n__newS1252 * _M0L6n__newS1252;
      _M0L6_2atmpS4190 = _M0L6_2atmpS4191 * _M0L6n__newS1252;
      _M0L2n4S1255 = _M0L6_2atmpS4190 * _M0L6n__newS1252;
      _M0L1iS4189 = _M0L1pS1218->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4186 = _M0MPC15array5Array2atGfE(_M0L1iS4189, _M0L1iS1233);
      _M0L6_2atmpS4188 = _M0L2elS1222 - _M0L1vS1234;
      _M0L6_2atmpS4187 = _M0L2glS1221 * _M0L6_2atmpS4188;
      _M0L6_2atmpS4183 = _M0L6_2atmpS4186 + _M0L6_2atmpS4187;
      _M0L6_2atmpS4185 = _M0L4e__eS1230 - _M0L1vS1234;
      _M0L6_2atmpS4184 = _M0L2geS1238 * _M0L6_2atmpS4185;
      _M0L6_2atmpS4180 = _M0L6_2atmpS4183 + _M0L6_2atmpS4184;
      _M0L6_2atmpS4182 = _M0L4e__iS1231 - _M0L1vS1234;
      _M0L6_2atmpS4181 = _M0L2giS1239 * _M0L6_2atmpS4182;
      _M0L6_2atmpS4176 = _M0L6_2atmpS4180 + _M0L6_2atmpS4181;
      _M0L6_2atmpS4178 = _M0L2gnS1225 * _M0L3m3hS1254;
      _M0L6_2atmpS4179 = _M0L2enS1224 - _M0L1vS1234;
      _M0L6_2atmpS4177 = _M0L6_2atmpS4178 * _M0L6_2atmpS4179;
      _M0L6_2atmpS4172 = _M0L6_2atmpS4176 + _M0L6_2atmpS4177;
      _M0L6_2atmpS4174 = _M0L2gkS1226 * _M0L2n4S1255;
      _M0L6_2atmpS4175 = _M0L2ekS1223 - _M0L1vS1234;
      _M0L6_2atmpS4173 = _M0L6_2atmpS4174 * _M0L6_2atmpS4175;
      _M0L7currentS1256 = _M0L6_2atmpS4172 + _M0L6_2atmpS4173;
      _M0L1vS4158 = _M0L1pS1218->$2;
      _M0L6_2atmpS4161 = _M0L2dtS1244 / _M0L2cmS1220;
      _M0L6_2atmpS4160 = _M0L6_2atmpS4161 * _M0L7currentS1256;
      _M0L6_2atmpS4159 = _M0L1vS1234 + _M0L6_2atmpS4160;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4158, _M0L1iS1233, _M0L6_2atmpS4159);
      _M0L2geS4162 = _M0L1pS1218->$8;
      _M0L6_2atmpS4166 = -_M0L2geS1238;
      _M0L6_2atmpS4165 = _M0L6_2atmpS4166 / _M0L6tau__eS1228;
      _M0L6_2atmpS4164 = _M0L2dtS1244 * _M0L6_2atmpS4165;
      _M0L6_2atmpS4163 = _M0L2geS1238 + _M0L6_2atmpS4164;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4162, _M0L1iS1233, _M0L6_2atmpS4163);
      _M0L2giS4167 = _M0L1pS1218->$9;
      _M0L6_2atmpS4171 = -_M0L2giS1239;
      _M0L6_2atmpS4170 = _M0L6_2atmpS4171 / _M0L6tau__iS1229;
      _M0L6_2atmpS4169 = _M0L2dtS1244 * _M0L6_2atmpS4170;
      _M0L6_2atmpS4168 = _M0L2giS1239 + _M0L6_2atmpS4169;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4167, _M0L1iS1233, _M0L6_2atmpS4168);
      _M0L6_2atmpS4239 = _M0L1iS1233 + 1;
      _M0L1iS1233 = _M0L6_2atmpS4239;
      continue;
    }
    break;
  }
  _M0L7_2abindS1258 = 0;
  _M0L1iS1259 = _M0L7_2abindS1258;
  while (1) {
    if (_M0L1iS1259 < _M0L1nS1217) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4240 = _M0L1pS1218->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS4243 = _M0L1pS1218->$2;
      float _M0L6_2atmpS4242;
      int32_t _M0L6_2atmpS4241;
      int32_t _M0L6_2atmpS4244;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4242 = _M0MPC15array5Array2atGfE(_M0L1vS4243, _M0L1iS1259);
      _M0L6_2atmpS4241 = _M0L6_2atmpS4242 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4240, _M0L1iS1259, _M0L6_2atmpS4241);
      _M0L6_2atmpS4244 = _M0L1iS1259 + 1;
      _M0L1iS1259 = _M0L6_2atmpS4244;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS1188,
  float _M0L2dtS1196
) {
  int32_t _M0L1nS1187;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4135;
  int32_t _M0L2ndS1189;
  int32_t _M0L8total__dS1190;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4134;
  float _M0L9steepnessS1191;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4133;
  float _M0L6tau__mS1192;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4132;
  float _M0L9tau__rateS1193;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4131;
  float _M0L8tau__absS1194;
  float _M0L6_2atmpS4130;
  int32_t _M0L11tabs__stepsS1195;
  int32_t _M0L7_2abindS1197;
  int32_t _M0L1iS1198;
  int32_t _M0L7_2abindS1201;
  int32_t _M0L1iS1202;
  int32_t _M0L7_2abindS1211;
  int32_t _M0L1iS1212;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS1187 = _M0L1pS1188->$1;
  _M0L5paramS4135 = _M0L1pS1188->$0;
  _M0L2ndS1189 = _M0L5paramS4135->$0;
  _M0L8total__dS1190 = _M0L1nS1187 * _M0L2ndS1189;
  _M0L5paramS4134 = _M0L1pS1188->$0;
  _M0L9steepnessS1191 = _M0L5paramS4134->$7;
  _M0L5paramS4133 = _M0L1pS1188->$0;
  _M0L6tau__mS1192 = _M0L5paramS4133->$8;
  _M0L5paramS4132 = _M0L1pS1188->$0;
  _M0L9tau__rateS1193 = _M0L5paramS4132->$9;
  _M0L5paramS4131 = _M0L1pS1188->$0;
  _M0L8tau__absS1194 = _M0L5paramS4131->$6;
  _M0L6_2atmpS4130 = _M0L8tau__absS1194 / _M0L2dtS1196;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS1195 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4130);
  _M0L7_2abindS1197 = 0;
  _M0L1iS1198 = _M0L7_2abindS1197;
  while (1) {
    if (_M0L1iS1198 < _M0L8total__dS1190) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS4058 = _M0L1pS1188->$6;
      float _M0L7tau__diS1199;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4046;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4057;
      float _M0L6_2atmpS4048;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4056;
      float _M0L6_2atmpS4055;
      float _M0L6_2atmpS4052;
      struct _M0TPB5ArrayGfE* _M0L4is__S4054;
      float _M0L6_2atmpS4053;
      float _M0L6_2atmpS4051;
      float _M0L6_2atmpS4050;
      float _M0L6_2atmpS4049;
      float _M0L6_2atmpS4047;
      int32_t _M0L6_2atmpS4059;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS1199
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS4058, _M0L1iS1198);
      _M0L4v__dS4046 = _M0L1pS1188->$2;
      _M0L4v__dS4057 = _M0L1pS1188->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4048
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4057, _M0L1iS1198);
      _M0L4v__dS4056 = _M0L1pS1188->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4055
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4056, _M0L1iS1198);
      _M0L6_2atmpS4052 = -_M0L6_2atmpS4055;
      _M0L4is__S4054 = _M0L1pS1188->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4053
      = _M0MPC15array5Array2atGfE(_M0L4is__S4054, _M0L1iS1198);
      _M0L6_2atmpS4051 = _M0L6_2atmpS4052 - _M0L6_2atmpS4053;
      _M0L6_2atmpS4050 = _M0L2dtS1196 * _M0L6_2atmpS4051;
      _M0L6_2atmpS4049 = _M0L6_2atmpS4050 / _M0L7tau__diS1199;
      _M0L6_2atmpS4047 = _M0L6_2atmpS4048 + _M0L6_2atmpS4049;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS4046, _M0L1iS1198, _M0L6_2atmpS4047);
      _M0L6_2atmpS4059 = _M0L1iS1198 + 1;
      _M0L1iS1198 = _M0L6_2atmpS4059;
      continue;
    }
    break;
  }
  _M0L7_2abindS1201 = 0;
  _M0L1iS1202 = _M0L7_2abindS1201;
  while (1) {
    if (_M0L1iS1202 < _M0L1nS1187) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4080 = _M0L1pS1188->$11;
      int32_t _M0L5startS1203;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4078;
      int32_t _M0L6_2atmpS4079;
      int32_t _M0L3endS1204;
      float _M0L16dt__over__tau__mS1205;
      struct _M0TPB8MutLocalGiE* _M0L1sS1206;
      int32_t _M0L6_2atmpS4081;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS1203
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4080, _M0L1iS1202);
      _M0L6colptrS4078 = _M0L1pS1188->$11;
      _M0L6_2atmpS4079 = _M0L1iS1202 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS1204
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4078, _M0L6_2atmpS4079);
      _M0L16dt__over__tau__mS1205 = _M0L2dtS1196 / _M0L6tau__mS1192;
      _M0L1sS1206
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1206)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1206->$0 = _M0L5startS1203;
      while (1) {
        int32_t _M0L3valS4060 = _M0L1sS1206->$0;
        if (_M0L3valS4060 < _M0L3endS1204) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS4076 = _M0L1pS1188->$12;
          int32_t _M0L3valS4077 = _M0L1sS1206->$0;
          int32_t _M0L9dend__idxS1207;
          struct _M0TPB5ArrayGfE* _M0L6w__synS4074;
          int32_t _M0L3valS4075;
          float _M0L1wS1208;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4061;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4071;
          float _M0L6_2atmpS4063;
          struct _M0TPB5ArrayGfE* _M0L4v__dS4070;
          float _M0L6_2atmpS4069;
          float _M0L6_2atmpS4066;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4068;
          float _M0L6_2atmpS4067;
          float _M0L6_2atmpS4065;
          float _M0L6_2atmpS4064;
          float _M0L6_2atmpS4062;
          int32_t _M0L3valS4073;
          int32_t _M0L6_2atmpS4072;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS1207
          = _M0MPC15array5Array2atGiE(_M0L6i__synS4076, _M0L3valS4077);
          _M0L6w__synS4074 = _M0L1pS1188->$13;
          _M0L3valS4075 = _M0L1sS1206->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS1208
          = _M0MPC15array5Array2atGfE(_M0L6w__synS4074, _M0L3valS4075);
          _M0L4v__sS4061 = _M0L1pS1188->$3;
          _M0L4v__sS4071 = _M0L1pS1188->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4063
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4071, _M0L1iS1202);
          _M0L4v__dS4070 = _M0L1pS1188->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4069
          = _M0MPC15array5Array2atGfE(_M0L4v__dS4070, _M0L9dend__idxS1207);
          _M0L6_2atmpS4066 = _M0L1wS1208 * _M0L6_2atmpS4069;
          _M0L4v__sS4068 = _M0L1pS1188->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4067
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4068, _M0L1iS1202);
          _M0L6_2atmpS4065 = _M0L6_2atmpS4066 - _M0L6_2atmpS4067;
          _M0L6_2atmpS4064 = _M0L6_2atmpS4065 * _M0L16dt__over__tau__mS1205;
          _M0L6_2atmpS4062 = _M0L6_2atmpS4063 + _M0L6_2atmpS4064;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS4061, _M0L1iS1202, _M0L6_2atmpS4062);
          _M0L3valS4073 = _M0L1sS1206->$0;
          _M0L6_2atmpS4072 = _M0L3valS4073 + 1;
          _M0L1sS1206->$0 = _M0L6_2atmpS4072;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1206);
        }
        break;
      }
      _M0L6_2atmpS4081 = _M0L1iS1202 + 1;
      _M0L1iS1202 = _M0L6_2atmpS4081;
      continue;
    }
    break;
  }
  _M0L7_2abindS1211 = 0;
  _M0L1iS1212 = _M0L7_2abindS1211;
  while (1) {
    if (_M0L1iS1212 < _M0L1nS1187) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4083 = _M0L1pS1188->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4086 = _M0L1pS1188->$8;
      int32_t _M0L6_2atmpS4085;
      int32_t _M0L6_2atmpS4084;
      struct _M0TPB5ArrayGbE* _M0L4fireS4087;
      struct _M0TPB5ArrayGfE* _M0L5traceS4088;
      struct _M0TPB5ArrayGfE* _M0L5traceS4096;
      float _M0L6_2atmpS4090;
      struct _M0TPB5ArrayGfE* _M0L5traceS4095;
      float _M0L6_2atmpS4094;
      float _M0L6_2atmpS4093;
      float _M0L6_2atmpS4092;
      float _M0L6_2atmpS4091;
      float _M0L6_2atmpS4089;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4098;
      int32_t _M0L6_2atmpS4097;
      struct _M0TPB5ArrayGfE* _M0L5traceS4099;
      struct _M0TPB5ArrayGfE* _M0L5traceS4108;
      float _M0L6_2atmpS4101;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4107;
      float _M0L6_2atmpS4104;
      struct _M0TPB5ArrayGfE* _M0L5traceS4106;
      float _M0L6_2atmpS4105;
      float _M0L6_2atmpS4103;
      float _M0L6_2atmpS4102;
      float _M0L6_2atmpS4100;
      float _M0L6_2atmpS4124;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4129;
      float _M0L6_2atmpS4126;
      struct _M0TPB5ArrayGfE* _M0L5traceS4128;
      float _M0L6_2atmpS4127;
      float _M0L6_2atmpS4125;
      float _M0L12sigmoid__argS1215;
      float _M0L4rateS1216;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4110;
      float _M0L6_2atmpS4109;
      int32_t _M0L6_2atmpS4082;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4085
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4086, _M0L1iS1212);
      _M0L6_2atmpS4084 = _M0L6_2atmpS4085 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4083, _M0L1iS1212, _M0L6_2atmpS4084);
      _M0L4fireS4087 = _M0L1pS1188->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4087, _M0L1iS1212, 0);
      _M0L5traceS4088 = _M0L1pS1188->$9;
      _M0L5traceS4096 = _M0L1pS1188->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4090
      = _M0MPC15array5Array2atGfE(_M0L5traceS4096, _M0L1iS1212);
      _M0L5traceS4095 = _M0L1pS1188->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4094
      = _M0MPC15array5Array2atGfE(_M0L5traceS4095, _M0L1iS1212);
      _M0L6_2atmpS4093 = -_M0L6_2atmpS4094;
      _M0L6_2atmpS4092 = _M0L6_2atmpS4093 / _M0L9tau__rateS1193;
      _M0L6_2atmpS4091 = _M0L2dtS1196 * _M0L6_2atmpS4092;
      _M0L6_2atmpS4089 = _M0L6_2atmpS4090 + _M0L6_2atmpS4091;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4088, _M0L1iS1212, _M0L6_2atmpS4089);
      _M0L4tabsS4098 = _M0L1pS1188->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4097
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4098, _M0L1iS1212);
      if (_M0L6_2atmpS4097 > 0) {
        goto join_1213;
      }
      _M0L5traceS4099 = _M0L1pS1188->$9;
      _M0L5traceS4108 = _M0L1pS1188->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4101
      = _M0MPC15array5Array2atGfE(_M0L5traceS4108, _M0L1iS1212);
      _M0L4v__sS4107 = _M0L1pS1188->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4104
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4107, _M0L1iS1212);
      _M0L5traceS4106 = _M0L1pS1188->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4105
      = _M0MPC15array5Array2atGfE(_M0L5traceS4106, _M0L1iS1212);
      _M0L6_2atmpS4103 = _M0L6_2atmpS4104 - _M0L6_2atmpS4105;
      _M0L6_2atmpS4102 = _M0L6_2atmpS4103 / _M0L9tau__rateS1193;
      _M0L6_2atmpS4100 = _M0L6_2atmpS4101 + _M0L6_2atmpS4102;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4099, _M0L1iS1212, _M0L6_2atmpS4100);
      _M0L6_2atmpS4124 = -_M0L9steepnessS1191;
      _M0L4v__sS4129 = _M0L1pS1188->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4126
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4129, _M0L1iS1212);
      _M0L5traceS4128 = _M0L1pS1188->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4127
      = _M0MPC15array5Array2atGfE(_M0L5traceS4128, _M0L1iS1212);
      _M0L6_2atmpS4125 = _M0L6_2atmpS4126 - _M0L6_2atmpS4127;
      _M0L12sigmoid__argS1215 = _M0L6_2atmpS4124 * _M0L6_2atmpS4125;
      if (_M0L12sigmoid__argS1215 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS4118 = _M0L1pS1188->$5;
        float _M0L6_2atmpS4117;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4117
        = _M0MPC15array5Array2atGfE(_M0L1rS4118, _M0L1iS1212);
        _M0L4rateS1216 = _M0L6_2atmpS4117 * _M0L2dtS1196;
      } else if (_M0L12sigmoid__argS1215 < -0x1.6p+6f) {
        _M0L4rateS1216 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS4123 = _M0L1pS1188->$5;
        float _M0L6_2atmpS4122;
        float _M0L6_2atmpS4119;
        float _M0L6_2atmpS4121;
        float _M0L6_2atmpS4120;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4122
        = _M0MPC15array5Array2atGfE(_M0L1rS4123, _M0L1iS1212);
        _M0L6_2atmpS4119 = _M0L6_2atmpS4122 * _M0L2dtS1196;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4121
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS1215);
        _M0L6_2atmpS4120 = 0x1p+0f + _M0L6_2atmpS4121;
        _M0L4rateS1216 = _M0L6_2atmpS4119 / _M0L6_2atmpS4120;
      }
      _M0L9randcacheS4110 = _M0L1pS1188->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4109
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS4110, _M0L1iS1212);
      if (_M0L6_2atmpS4109 < _M0L4rateS1216) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4111 = _M0L1pS1188->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4112;
        struct _M0TPB5ArrayGfE* _M0L5traceS4113;
        struct _M0TPB5ArrayGfE* _M0L5traceS4116;
        float _M0L6_2atmpS4115;
        float _M0L6_2atmpS4114;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4111, _M0L1iS1212, 1);
        _M0L4tabsS4112 = _M0L1pS1188->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4112, _M0L1iS1212, _M0L11tabs__stepsS1195);
        _M0L5traceS4113 = _M0L1pS1188->$9;
        _M0L5traceS4116 = _M0L1pS1188->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4115
        = _M0MPC15array5Array2atGfE(_M0L5traceS4116, _M0L1iS1212);
        _M0L6_2atmpS4114 = _M0L6_2atmpS4115 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS4113, _M0L1iS1212, _M0L6_2atmpS4114);
      }
      goto join_1213;
      goto joinlet_5219;
      join_1213:;
      _M0L6_2atmpS4082 = _M0L1iS1212 + 1;
      _M0L1iS1212 = _M0L6_2atmpS4082;
      continue;
      joinlet_5219:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1166,
  float _M0L2dtS1181
) {
  int32_t _M0L1nS1165;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S1167;
  float _M0L2tmS1168;
  float _M0L2vtS1169;
  float _M0L2vrS1170;
  float _M0L2elS1171;
  float _M0L1rS1172;
  float _M0L9dt__slopeS1173;
  float _M0L2twS1174;
  float _M0L1aS1175;
  float _M0L1bS1176;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4045;
  float _M0L2atS1177;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4044;
  float _M0L6tau__aS1178;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4043;
  float _M0L11tabs__constS1179;
  float _M0L6_2atmpS4042;
  int32_t _M0L11tabs__stepsS1180;
  int32_t _M0L7_2abindS1182;
  int32_t _M0L1iS1183;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1165 = _M0L1pS1166->$2;
  _M0L3p__S1167 = _M0L1pS1166->$0;
  _M0L2tmS1168 = _M0L3p__S1167->$5;
  _M0L2vtS1169 = _M0L3p__S1167->$2;
  _M0L2vrS1170 = _M0L3p__S1167->$3;
  _M0L2elS1171 = _M0L3p__S1167->$4;
  _M0L1rS1172 = _M0L3p__S1167->$6;
  _M0L9dt__slopeS1173 = _M0L3p__S1167->$7;
  _M0L2twS1174 = _M0L3p__S1167->$8;
  _M0L1aS1175 = _M0L3p__S1167->$9;
  _M0L1bS1176 = _M0L3p__S1167->$10;
  _M0L5spikeS4045 = _M0L1pS1166->$1;
  _M0L2atS1177 = _M0L5spikeS4045->$0;
  _M0L5spikeS4044 = _M0L1pS1166->$1;
  _M0L6tau__aS1178 = _M0L5spikeS4044->$1;
  _M0L5spikeS4043 = _M0L1pS1166->$1;
  _M0L11tabs__constS1179 = _M0L5spikeS4043->$3;
  _M0L6_2atmpS4042 = _M0L11tabs__constS1179 / _M0L2dtS1181;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS1180 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4042);
  _M0L7_2abindS1182 = 0;
  _M0L1iS1183 = _M0L7_2abindS1182;
  while (1) {
    if (_M0L1iS1183 < _M0L1nS1165) {
      struct _M0TPB5ArrayGfE* _M0L1vS3955 = _M0L1pS1166->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS3957 = _M0L1pS1166->$5;
      float _M0L6_2atmpS3956;
      struct _M0TPB5ArrayGbE* _M0L4fireS3959;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3960;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3963;
      int32_t _M0L6_2atmpS3962;
      int32_t _M0L6_2atmpS3961;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3965;
      int32_t _M0L6_2atmpS3964;
      struct _M0TPB5ArrayGfE* _M0L1wS3966;
      struct _M0TPB5ArrayGfE* _M0L1wS3978;
      float _M0L6_2atmpS3968;
      struct _M0TPB5ArrayGfE* _M0L1vS3977;
      float _M0L6_2atmpS3976;
      float _M0L6_2atmpS3975;
      float _M0L6_2atmpS3972;
      struct _M0TPB5ArrayGfE* _M0L1wS3974;
      float _M0L6_2atmpS3973;
      float _M0L6_2atmpS3971;
      float _M0L6_2atmpS3970;
      float _M0L6_2atmpS3969;
      float _M0L6_2atmpS3967;
      float _M0L9exp__termS1186;
      struct _M0TPB5ArrayGfE* _M0L1vS3979;
      struct _M0TPB5ArrayGfE* _M0L1vS4001;
      float _M0L6_2atmpS3981;
      struct _M0TPB5ArrayGfE* _M0L1vS4000;
      float _M0L6_2atmpS3999;
      float _M0L6_2atmpS3998;
      float _M0L6_2atmpS3997;
      float _M0L6_2atmpS3993;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS3996;
      float _M0L6_2atmpS3995;
      float _M0L6_2atmpS3994;
      float _M0L6_2atmpS3989;
      struct _M0TPB5ArrayGfE* _M0L1wS3992;
      float _M0L6_2atmpS3991;
      float _M0L6_2atmpS3990;
      float _M0L6_2atmpS3985;
      struct _M0TPB5ArrayGfE* _M0L1iS3988;
      float _M0L6_2atmpS3987;
      float _M0L6_2atmpS3986;
      float _M0L6_2atmpS3984;
      float _M0L6_2atmpS3983;
      float _M0L6_2atmpS3982;
      float _M0L6_2atmpS3980;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4002;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4010;
      float _M0L6_2atmpS4004;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4009;
      float _M0L6_2atmpS4008;
      float _M0L6_2atmpS4007;
      float _M0L6_2atmpS4006;
      float _M0L6_2atmpS4005;
      float _M0L6_2atmpS4003;
      struct _M0TPB5ArrayGbE* _M0L4fireS4011;
      struct _M0TPB5ArrayGfE* _M0L1vS4014;
      float _M0L6_2atmpS4013;
      int32_t _M0L6_2atmpS4012;
      struct _M0TPB5ArrayGfE* _M0L1vS4015;
      struct _M0TPB5ArrayGbE* _M0L4fireS4017;
      float _M0L6_2atmpS4016;
      struct _M0TPB5ArrayGfE* _M0L1wS4019;
      struct _M0TPB5ArrayGbE* _M0L4fireS4021;
      float _M0L6_2atmpS4020;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4025;
      struct _M0TPB5ArrayGbE* _M0L4fireS4027;
      float _M0L6_2atmpS4026;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4031;
      struct _M0TPB5ArrayGbE* _M0L4fireS4033;
      int32_t _M0L6_2atmpS4032;
      int32_t _M0L6_2atmpS3954;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3957, _M0L1iS1183)) {
        _M0L6_2atmpS3956 = _M0L2vrS1170;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS3958 = _M0L1pS1166->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS3956
        = _M0MPC15array5Array2atGfE(_M0L1vS3958, _M0L1iS1183);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3955, _M0L1iS1183, _M0L6_2atmpS3956);
      _M0L4fireS3959 = _M0L1pS1166->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3959, _M0L1iS1183, 0);
      _M0L4tabsS3960 = _M0L1pS1166->$7;
      _M0L4tabsS3963 = _M0L1pS1166->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3962
      = _M0MPC15array5Array2atGiE(_M0L4tabsS3963, _M0L1iS1183);
      _M0L6_2atmpS3961 = _M0L6_2atmpS3962 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS3960, _M0L1iS1183, _M0L6_2atmpS3961);
      _M0L4tabsS3965 = _M0L1pS1166->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3964
      = _M0MPC15array5Array2atGiE(_M0L4tabsS3965, _M0L1iS1183);
      if (_M0L6_2atmpS3964 > 0) {
        goto join_1184;
      }
      _M0L1wS3966 = _M0L1pS1166->$4;
      _M0L1wS3978 = _M0L1pS1166->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3968 = _M0MPC15array5Array2atGfE(_M0L1wS3978, _M0L1iS1183);
      _M0L1vS3977 = _M0L1pS1166->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3976 = _M0MPC15array5Array2atGfE(_M0L1vS3977, _M0L1iS1183);
      _M0L6_2atmpS3975 = _M0L6_2atmpS3976 - _M0L2elS1171;
      _M0L6_2atmpS3972 = _M0L1aS1175 * _M0L6_2atmpS3975;
      _M0L1wS3974 = _M0L1pS1166->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3973 = _M0MPC15array5Array2atGfE(_M0L1wS3974, _M0L1iS1183);
      _M0L6_2atmpS3971 = _M0L6_2atmpS3972 - _M0L6_2atmpS3973;
      _M0L6_2atmpS3970 = _M0L2dtS1181 * _M0L6_2atmpS3971;
      _M0L6_2atmpS3969 = _M0L6_2atmpS3970 / _M0L2twS1174;
      _M0L6_2atmpS3967 = _M0L6_2atmpS3968 + _M0L6_2atmpS3969;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS3966, _M0L1iS1183, _M0L6_2atmpS3967);
      if (_M0L9dt__slopeS1173 < 0x0p+0f) {
        _M0L9exp__termS1186 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4041 = _M0L1pS1166->$3;
        float _M0L6_2atmpS4038;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4040;
        float _M0L6_2atmpS4039;
        float _M0L6_2atmpS4037;
        float _M0L6_2atmpS4036;
        float _M0L6_2atmpS4035;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4038
        = _M0MPC15array5Array2atGfE(_M0L1vS4041, _M0L1iS1183);
        _M0L9thresholdS4040 = _M0L1pS1166->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4039
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4040, _M0L1iS1183);
        _M0L6_2atmpS4037 = _M0L6_2atmpS4038 - _M0L6_2atmpS4039;
        _M0L6_2atmpS4036 = _M0L6_2atmpS4037 / _M0L9dt__slopeS1173;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4035 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4036);
        _M0L9exp__termS1186 = _M0L9dt__slopeS1173 * _M0L6_2atmpS4035;
      }
      _M0L1vS3979 = _M0L1pS1166->$3;
      _M0L1vS4001 = _M0L1pS1166->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3981 = _M0MPC15array5Array2atGfE(_M0L1vS4001, _M0L1iS1183);
      _M0L1vS4000 = _M0L1pS1166->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3999 = _M0MPC15array5Array2atGfE(_M0L1vS4000, _M0L1iS1183);
      _M0L6_2atmpS3998 = _M0L6_2atmpS3999 - _M0L2elS1171;
      _M0L6_2atmpS3997 = -_M0L6_2atmpS3998;
      _M0L6_2atmpS3993 = _M0L6_2atmpS3997 + _M0L9exp__termS1186;
      _M0L9syn__currS3996 = _M0L1pS1166->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3995
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS3996, _M0L1iS1183);
      _M0L6_2atmpS3994 = _M0L1rS1172 * _M0L6_2atmpS3995;
      _M0L6_2atmpS3989 = _M0L6_2atmpS3993 - _M0L6_2atmpS3994;
      _M0L1wS3992 = _M0L1pS1166->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3991 = _M0MPC15array5Array2atGfE(_M0L1wS3992, _M0L1iS1183);
      _M0L6_2atmpS3990 = _M0L1rS1172 * _M0L6_2atmpS3991;
      _M0L6_2atmpS3985 = _M0L6_2atmpS3989 - _M0L6_2atmpS3990;
      _M0L1iS3988 = _M0L1pS1166->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3987 = _M0MPC15array5Array2atGfE(_M0L1iS3988, _M0L1iS1183);
      _M0L6_2atmpS3986 = _M0L1rS1172 * _M0L6_2atmpS3987;
      _M0L6_2atmpS3984 = _M0L6_2atmpS3985 + _M0L6_2atmpS3986;
      _M0L6_2atmpS3983 = _M0L2dtS1181 * _M0L6_2atmpS3984;
      _M0L6_2atmpS3982 = _M0L6_2atmpS3983 / _M0L2tmS1168;
      _M0L6_2atmpS3980 = _M0L6_2atmpS3981 + _M0L6_2atmpS3982;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3979, _M0L1iS1183, _M0L6_2atmpS3980);
      _M0L9thresholdS4002 = _M0L1pS1166->$6;
      _M0L9thresholdS4010 = _M0L1pS1166->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4004
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4010, _M0L1iS1183);
      _M0L9thresholdS4009 = _M0L1pS1166->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4008
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4009, _M0L1iS1183);
      _M0L6_2atmpS4007 = _M0L2vtS1169 - _M0L6_2atmpS4008;
      _M0L6_2atmpS4006 = _M0L2dtS1181 * _M0L6_2atmpS4007;
      _M0L6_2atmpS4005 = _M0L6_2atmpS4006 / _M0L6tau__aS1178;
      _M0L6_2atmpS4003 = _M0L6_2atmpS4004 + _M0L6_2atmpS4005;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4002, _M0L1iS1183, _M0L6_2atmpS4003);
      _M0L4fireS4011 = _M0L1pS1166->$5;
      _M0L1vS4014 = _M0L1pS1166->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4013 = _M0MPC15array5Array2atGfE(_M0L1vS4014, _M0L1iS1183);
      _M0L6_2atmpS4012 = _M0L6_2atmpS4013 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4011, _M0L1iS1183, _M0L6_2atmpS4012);
      _M0L1vS4015 = _M0L1pS1166->$3;
      _M0L4fireS4017 = _M0L1pS1166->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4017, _M0L1iS1183)) {
        _M0L6_2atmpS4016 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4018 = _M0L1pS1166->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4016
        = _M0MPC15array5Array2atGfE(_M0L1vS4018, _M0L1iS1183);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4015, _M0L1iS1183, _M0L6_2atmpS4016);
      _M0L1wS4019 = _M0L1pS1166->$4;
      _M0L4fireS4021 = _M0L1pS1166->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4021, _M0L1iS1183)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4023 = _M0L1pS1166->$4;
        float _M0L6_2atmpS4022;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4022
        = _M0MPC15array5Array2atGfE(_M0L1wS4023, _M0L1iS1183);
        _M0L6_2atmpS4020 = _M0L6_2atmpS4022 + _M0L1bS1176;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4024 = _M0L1pS1166->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4020
        = _M0MPC15array5Array2atGfE(_M0L1wS4024, _M0L1iS1183);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4019, _M0L1iS1183, _M0L6_2atmpS4020);
      _M0L9thresholdS4025 = _M0L1pS1166->$6;
      _M0L4fireS4027 = _M0L1pS1166->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4027, _M0L1iS1183)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4029 = _M0L1pS1166->$6;
        float _M0L6_2atmpS4028;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4028
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4029, _M0L1iS1183);
        _M0L6_2atmpS4026 = _M0L6_2atmpS4028 + _M0L2atS1177;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4030 = _M0L1pS1166->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4026
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4030, _M0L1iS1183);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4025, _M0L1iS1183, _M0L6_2atmpS4026);
      _M0L4tabsS4031 = _M0L1pS1166->$7;
      _M0L4fireS4033 = _M0L1pS1166->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4033, _M0L1iS1183)) {
        _M0L6_2atmpS4032 = _M0L11tabs__stepsS1180;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4034 = _M0L1pS1166->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4032
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4034, _M0L1iS1183);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4031, _M0L1iS1183, _M0L6_2atmpS4032);
      goto join_1184;
      goto joinlet_5221;
      join_1184:;
      _M0L6_2atmpS3954 = _M0L1iS1183 + 1;
      _M0L1iS1183 = _M0L6_2atmpS3954;
      continue;
      joinlet_5221:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1161
) {
  int32_t _M0L1nS1160;
  int32_t _M0L7_2abindS1162;
  int32_t _M0L1iS1163;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1160 = _M0L1pS1161->$2;
  _M0L7_2abindS1162 = 0;
  _M0L1iS1163 = _M0L7_2abindS1162;
  while (1) {
    if (_M0L1iS1163 < _M0L1nS1160) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS3931 = _M0L1pS1161->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS3952 = _M0L1pS1161->$10;
      float _M0L6_2atmpS3947;
      struct _M0TPB5ArrayGfE* _M0L1vS3951;
      float _M0L6_2atmpS3949;
      float _M0L4e__eS3950;
      float _M0L6_2atmpS3948;
      float _M0L6_2atmpS3944;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS3946;
      float _M0L6_2atmpS3945;
      float _M0L6_2atmpS3933;
      struct _M0TPB5ArrayGfE* _M0L2giS3943;
      float _M0L6_2atmpS3938;
      struct _M0TPB5ArrayGfE* _M0L1vS3942;
      float _M0L6_2atmpS3940;
      float _M0L4e__iS3941;
      float _M0L6_2atmpS3939;
      float _M0L6_2atmpS3935;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS3937;
      float _M0L6_2atmpS3936;
      float _M0L6_2atmpS3934;
      float _M0L6_2atmpS3932;
      int32_t _M0L6_2atmpS3953;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3947 = _M0MPC15array5Array2atGfE(_M0L2geS3952, _M0L1iS1163);
      _M0L1vS3951 = _M0L1pS1161->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3949 = _M0MPC15array5Array2atGfE(_M0L1vS3951, _M0L1iS1163);
      _M0L4e__eS3950 = _M0L1pS1161->$16;
      _M0L6_2atmpS3948 = _M0L6_2atmpS3949 - _M0L4e__eS3950;
      _M0L6_2atmpS3944 = _M0L6_2atmpS3947 * _M0L6_2atmpS3948;
      _M0L7gsyn__eS3946 = _M0L1pS1161->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3945
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS3946, _M0L1iS1163);
      _M0L6_2atmpS3933 = _M0L6_2atmpS3944 * _M0L6_2atmpS3945;
      _M0L2giS3943 = _M0L1pS1161->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3938 = _M0MPC15array5Array2atGfE(_M0L2giS3943, _M0L1iS1163);
      _M0L1vS3942 = _M0L1pS1161->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3940 = _M0MPC15array5Array2atGfE(_M0L1vS3942, _M0L1iS1163);
      _M0L4e__iS3941 = _M0L1pS1161->$17;
      _M0L6_2atmpS3939 = _M0L6_2atmpS3940 - _M0L4e__iS3941;
      _M0L6_2atmpS3935 = _M0L6_2atmpS3938 * _M0L6_2atmpS3939;
      _M0L7gsyn__iS3937 = _M0L1pS1161->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3936
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS3937, _M0L1iS1163);
      _M0L6_2atmpS3934 = _M0L6_2atmpS3935 * _M0L6_2atmpS3936;
      _M0L6_2atmpS3932 = _M0L6_2atmpS3933 + _M0L6_2atmpS3934;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS3931, _M0L1iS1163, _M0L6_2atmpS3932);
      _M0L6_2atmpS3953 = _M0L1iS1163 + 1;
      _M0L1iS1163 = _M0L6_2atmpS3953;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1150,
  float _M0L2dtS1155
) {
  int32_t _M0L1nS1149;
  float _M0L6tau__eS1151;
  float _M0L6tau__iS1152;
  int32_t _M0L7_2abindS1153;
  int32_t _M0L1iS1154;
  int32_t _M0L7_2abindS1157;
  int32_t _M0L1iS1158;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1149 = _M0L1pS1150->$2;
  _M0L6tau__eS1151 = _M0L1pS1150->$18;
  _M0L6tau__iS1152 = _M0L1pS1150->$19;
  _M0L7_2abindS1153 = 0;
  _M0L1iS1154 = _M0L7_2abindS1153;
  while (1) {
    if (_M0L1iS1154 < _M0L1nS1149) {
      struct _M0TPB5ArrayGfE* _M0L2geS3897 = _M0L1pS1150->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS3902 = _M0L1pS1150->$10;
      float _M0L6_2atmpS3899;
      struct _M0TPB5ArrayGfE* _M0L3gluS3901;
      float _M0L6_2atmpS3900;
      float _M0L6_2atmpS3898;
      struct _M0TPB5ArrayGfE* _M0L2giS3903;
      struct _M0TPB5ArrayGfE* _M0L2giS3908;
      float _M0L6_2atmpS3905;
      struct _M0TPB5ArrayGfE* _M0L4gabaS3907;
      float _M0L6_2atmpS3906;
      float _M0L6_2atmpS3904;
      struct _M0TPB5ArrayGfE* _M0L2geS3909;
      struct _M0TPB5ArrayGfE* _M0L2geS3917;
      float _M0L6_2atmpS3911;
      struct _M0TPB5ArrayGfE* _M0L2geS3916;
      float _M0L6_2atmpS3915;
      float _M0L6_2atmpS3914;
      float _M0L6_2atmpS3913;
      float _M0L6_2atmpS3912;
      float _M0L6_2atmpS3910;
      struct _M0TPB5ArrayGfE* _M0L2giS3918;
      struct _M0TPB5ArrayGfE* _M0L2giS3926;
      float _M0L6_2atmpS3920;
      struct _M0TPB5ArrayGfE* _M0L2giS3925;
      float _M0L6_2atmpS3924;
      float _M0L6_2atmpS3923;
      float _M0L6_2atmpS3922;
      float _M0L6_2atmpS3921;
      float _M0L6_2atmpS3919;
      int32_t _M0L6_2atmpS3927;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3899 = _M0MPC15array5Array2atGfE(_M0L2geS3902, _M0L1iS1154);
      _M0L3gluS3901 = _M0L1pS1150->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3900
      = _M0MPC15array5Array2atGfE(_M0L3gluS3901, _M0L1iS1154);
      _M0L6_2atmpS3898 = _M0L6_2atmpS3899 + _M0L6_2atmpS3900;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS3897, _M0L1iS1154, _M0L6_2atmpS3898);
      _M0L2giS3903 = _M0L1pS1150->$11;
      _M0L2giS3908 = _M0L1pS1150->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3905 = _M0MPC15array5Array2atGfE(_M0L2giS3908, _M0L1iS1154);
      _M0L4gabaS3907 = _M0L1pS1150->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3906
      = _M0MPC15array5Array2atGfE(_M0L4gabaS3907, _M0L1iS1154);
      _M0L6_2atmpS3904 = _M0L6_2atmpS3905 + _M0L6_2atmpS3906;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS3903, _M0L1iS1154, _M0L6_2atmpS3904);
      _M0L2geS3909 = _M0L1pS1150->$10;
      _M0L2geS3917 = _M0L1pS1150->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3911 = _M0MPC15array5Array2atGfE(_M0L2geS3917, _M0L1iS1154);
      _M0L2geS3916 = _M0L1pS1150->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3915 = _M0MPC15array5Array2atGfE(_M0L2geS3916, _M0L1iS1154);
      _M0L6_2atmpS3914 = -_M0L6_2atmpS3915;
      _M0L6_2atmpS3913 = _M0L6_2atmpS3914 / _M0L6tau__eS1151;
      _M0L6_2atmpS3912 = _M0L2dtS1155 * _M0L6_2atmpS3913;
      _M0L6_2atmpS3910 = _M0L6_2atmpS3911 + _M0L6_2atmpS3912;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS3909, _M0L1iS1154, _M0L6_2atmpS3910);
      _M0L2giS3918 = _M0L1pS1150->$11;
      _M0L2giS3926 = _M0L1pS1150->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3920 = _M0MPC15array5Array2atGfE(_M0L2giS3926, _M0L1iS1154);
      _M0L2giS3925 = _M0L1pS1150->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS3924 = _M0MPC15array5Array2atGfE(_M0L2giS3925, _M0L1iS1154);
      _M0L6_2atmpS3923 = -_M0L6_2atmpS3924;
      _M0L6_2atmpS3922 = _M0L6_2atmpS3923 / _M0L6tau__iS1152;
      _M0L6_2atmpS3921 = _M0L2dtS1155 * _M0L6_2atmpS3922;
      _M0L6_2atmpS3919 = _M0L6_2atmpS3920 + _M0L6_2atmpS3921;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS3918, _M0L1iS1154, _M0L6_2atmpS3919);
      _M0L6_2atmpS3927 = _M0L1iS1154 + 1;
      _M0L1iS1154 = _M0L6_2atmpS3927;
      continue;
    }
    break;
  }
  _M0L7_2abindS1157 = 0;
  _M0L1iS1158 = _M0L7_2abindS1157;
  while (1) {
    if (_M0L1iS1158 < _M0L1nS1149) {
      struct _M0TPB5ArrayGfE* _M0L3gluS3928 = _M0L1pS1150->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS3929;
      int32_t _M0L6_2atmpS3930;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS3928, _M0L1iS1158, 0x0p+0f);
      _M0L4gabaS3929 = _M0L1pS1150->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS3929, _M0L1iS1158, 0x0p+0f);
      _M0L6_2atmpS3930 = _M0L1iS1158 + 1;
      _M0L1iS1158 = _M0L6_2atmpS3930;
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
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1128,
  float _M0L2dtS1143
) {
  int32_t _M0L1nS1127;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1129;
  float _M0L2tmS1130;
  float _M0L2vtS1131;
  float _M0L2vrS1132;
  float _M0L2elS1133;
  float _M0L1rS1134;
  float _M0L9dt__slopeS1135;
  float _M0L2twS1136;
  float _M0L1aS1137;
  float _M0L1bS1138;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS3896;
  float _M0L2atS1139;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS3895;
  float _M0L6tau__aS1140;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS3894;
  float _M0L11tabs__constS1141;
  float _M0L6_2atmpS3893;
  int32_t _M0L11tabs__stepsS1142;
  int32_t _M0L7_2abindS1144;
  int32_t _M0L1iS1145;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1127 = _M0L1pS1128->$2;
  _M0L3p__S1129 = _M0L1pS1128->$0;
  _M0L2tmS1130 = _M0L3p__S1129->$5;
  _M0L2vtS1131 = _M0L3p__S1129->$2;
  _M0L2vrS1132 = _M0L3p__S1129->$3;
  _M0L2elS1133 = _M0L3p__S1129->$4;
  _M0L1rS1134 = _M0L3p__S1129->$6;
  _M0L9dt__slopeS1135 = _M0L3p__S1129->$7;
  _M0L2twS1136 = _M0L3p__S1129->$8;
  _M0L1aS1137 = _M0L3p__S1129->$9;
  _M0L1bS1138 = _M0L3p__S1129->$10;
  _M0L5spikeS3896 = _M0L1pS1128->$1;
  _M0L2atS1139 = _M0L5spikeS3896->$0;
  _M0L5spikeS3895 = _M0L1pS1128->$1;
  _M0L6tau__aS1140 = _M0L5spikeS3895->$1;
  _M0L5spikeS3894 = _M0L1pS1128->$1;
  _M0L11tabs__constS1141 = _M0L5spikeS3894->$3;
  _M0L6_2atmpS3893 = _M0L11tabs__constS1141 / _M0L2dtS1143;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1142 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3893);
  _M0L7_2abindS1144 = 0;
  _M0L1iS1145 = _M0L7_2abindS1144;
  while (1) {
    if (_M0L1iS1145 < _M0L1nS1127) {
      struct _M0TPB5ArrayGfE* _M0L1vS3806 = _M0L1pS1128->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS3808 = _M0L1pS1128->$5;
      float _M0L6_2atmpS3807;
      struct _M0TPB5ArrayGbE* _M0L4fireS3810;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3811;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3814;
      int32_t _M0L6_2atmpS3813;
      int32_t _M0L6_2atmpS3812;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3816;
      int32_t _M0L6_2atmpS3815;
      struct _M0TPB5ArrayGfE* _M0L1wS3817;
      struct _M0TPB5ArrayGfE* _M0L1wS3829;
      float _M0L6_2atmpS3819;
      struct _M0TPB5ArrayGfE* _M0L1vS3828;
      float _M0L6_2atmpS3827;
      float _M0L6_2atmpS3826;
      float _M0L6_2atmpS3823;
      struct _M0TPB5ArrayGfE* _M0L1wS3825;
      float _M0L6_2atmpS3824;
      float _M0L6_2atmpS3822;
      float _M0L6_2atmpS3821;
      float _M0L6_2atmpS3820;
      float _M0L6_2atmpS3818;
      float _M0L9exp__termS1148;
      struct _M0TPB5ArrayGfE* _M0L1vS3830;
      struct _M0TPB5ArrayGfE* _M0L1vS3852;
      float _M0L6_2atmpS3832;
      struct _M0TPB5ArrayGfE* _M0L1vS3851;
      float _M0L6_2atmpS3850;
      float _M0L6_2atmpS3849;
      float _M0L6_2atmpS3848;
      float _M0L6_2atmpS3844;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS3847;
      float _M0L6_2atmpS3846;
      float _M0L6_2atmpS3845;
      float _M0L6_2atmpS3840;
      struct _M0TPB5ArrayGfE* _M0L1wS3843;
      float _M0L6_2atmpS3842;
      float _M0L6_2atmpS3841;
      float _M0L6_2atmpS3836;
      struct _M0TPB5ArrayGfE* _M0L1iS3839;
      float _M0L6_2atmpS3838;
      float _M0L6_2atmpS3837;
      float _M0L6_2atmpS3835;
      float _M0L6_2atmpS3834;
      float _M0L6_2atmpS3833;
      float _M0L6_2atmpS3831;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS3853;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS3861;
      float _M0L6_2atmpS3855;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS3860;
      float _M0L6_2atmpS3859;
      float _M0L6_2atmpS3858;
      float _M0L6_2atmpS3857;
      float _M0L6_2atmpS3856;
      float _M0L6_2atmpS3854;
      struct _M0TPB5ArrayGbE* _M0L4fireS3862;
      struct _M0TPB5ArrayGfE* _M0L1vS3865;
      float _M0L6_2atmpS3864;
      int32_t _M0L6_2atmpS3863;
      struct _M0TPB5ArrayGfE* _M0L1vS3866;
      struct _M0TPB5ArrayGbE* _M0L4fireS3868;
      float _M0L6_2atmpS3867;
      struct _M0TPB5ArrayGfE* _M0L1wS3870;
      struct _M0TPB5ArrayGbE* _M0L4fireS3872;
      float _M0L6_2atmpS3871;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS3876;
      struct _M0TPB5ArrayGbE* _M0L4fireS3878;
      float _M0L6_2atmpS3877;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3882;
      struct _M0TPB5ArrayGbE* _M0L4fireS3884;
      int32_t _M0L6_2atmpS3883;
      int32_t _M0L6_2atmpS3805;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3808, _M0L1iS1145)) {
        _M0L6_2atmpS3807 = _M0L2vrS1132;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS3809 = _M0L1pS1128->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3807
        = _M0MPC15array5Array2atGfE(_M0L1vS3809, _M0L1iS1145);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3806, _M0L1iS1145, _M0L6_2atmpS3807);
      _M0L4fireS3810 = _M0L1pS1128->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3810, _M0L1iS1145, 0);
      _M0L4tabsS3811 = _M0L1pS1128->$7;
      _M0L4tabsS3814 = _M0L1pS1128->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3813
      = _M0MPC15array5Array2atGiE(_M0L4tabsS3814, _M0L1iS1145);
      _M0L6_2atmpS3812 = _M0L6_2atmpS3813 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS3811, _M0L1iS1145, _M0L6_2atmpS3812);
      _M0L4tabsS3816 = _M0L1pS1128->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3815
      = _M0MPC15array5Array2atGiE(_M0L4tabsS3816, _M0L1iS1145);
      if (_M0L6_2atmpS3815 > 0) {
        goto join_1146;
      }
      _M0L1wS3817 = _M0L1pS1128->$4;
      _M0L1wS3829 = _M0L1pS1128->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3819 = _M0MPC15array5Array2atGfE(_M0L1wS3829, _M0L1iS1145);
      _M0L1vS3828 = _M0L1pS1128->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3827 = _M0MPC15array5Array2atGfE(_M0L1vS3828, _M0L1iS1145);
      _M0L6_2atmpS3826 = _M0L6_2atmpS3827 - _M0L2elS1133;
      _M0L6_2atmpS3823 = _M0L1aS1137 * _M0L6_2atmpS3826;
      _M0L1wS3825 = _M0L1pS1128->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3824 = _M0MPC15array5Array2atGfE(_M0L1wS3825, _M0L1iS1145);
      _M0L6_2atmpS3822 = _M0L6_2atmpS3823 - _M0L6_2atmpS3824;
      _M0L6_2atmpS3821 = _M0L2dtS1143 * _M0L6_2atmpS3822;
      _M0L6_2atmpS3820 = _M0L6_2atmpS3821 / _M0L2twS1136;
      _M0L6_2atmpS3818 = _M0L6_2atmpS3819 + _M0L6_2atmpS3820;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS3817, _M0L1iS1145, _M0L6_2atmpS3818);
      if (_M0L9dt__slopeS1135 < 0x0p+0f) {
        _M0L9exp__termS1148 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS3892 = _M0L1pS1128->$3;
        float _M0L6_2atmpS3889;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS3891;
        float _M0L6_2atmpS3890;
        float _M0L6_2atmpS3888;
        float _M0L6_2atmpS3887;
        float _M0L6_2atmpS3886;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3889
        = _M0MPC15array5Array2atGfE(_M0L1vS3892, _M0L1iS1145);
        _M0L9thresholdS3891 = _M0L1pS1128->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3890
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS3891, _M0L1iS1145);
        _M0L6_2atmpS3888 = _M0L6_2atmpS3889 - _M0L6_2atmpS3890;
        _M0L6_2atmpS3887 = _M0L6_2atmpS3888 / _M0L9dt__slopeS1135;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3886 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3887);
        _M0L9exp__termS1148 = _M0L9dt__slopeS1135 * _M0L6_2atmpS3886;
      }
      _M0L1vS3830 = _M0L1pS1128->$3;
      _M0L1vS3852 = _M0L1pS1128->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3832 = _M0MPC15array5Array2atGfE(_M0L1vS3852, _M0L1iS1145);
      _M0L1vS3851 = _M0L1pS1128->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3850 = _M0MPC15array5Array2atGfE(_M0L1vS3851, _M0L1iS1145);
      _M0L6_2atmpS3849 = _M0L6_2atmpS3850 - _M0L2elS1133;
      _M0L6_2atmpS3848 = -_M0L6_2atmpS3849;
      _M0L6_2atmpS3844 = _M0L6_2atmpS3848 + _M0L9exp__termS1148;
      _M0L9syn__currS3847 = _M0L1pS1128->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3846
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS3847, _M0L1iS1145);
      _M0L6_2atmpS3845 = _M0L1rS1134 * _M0L6_2atmpS3846;
      _M0L6_2atmpS3840 = _M0L6_2atmpS3844 - _M0L6_2atmpS3845;
      _M0L1wS3843 = _M0L1pS1128->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3842 = _M0MPC15array5Array2atGfE(_M0L1wS3843, _M0L1iS1145);
      _M0L6_2atmpS3841 = _M0L1rS1134 * _M0L6_2atmpS3842;
      _M0L6_2atmpS3836 = _M0L6_2atmpS3840 - _M0L6_2atmpS3841;
      _M0L1iS3839 = _M0L1pS1128->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3838 = _M0MPC15array5Array2atGfE(_M0L1iS3839, _M0L1iS1145);
      _M0L6_2atmpS3837 = _M0L1rS1134 * _M0L6_2atmpS3838;
      _M0L6_2atmpS3835 = _M0L6_2atmpS3836 + _M0L6_2atmpS3837;
      _M0L6_2atmpS3834 = _M0L2dtS1143 * _M0L6_2atmpS3835;
      _M0L6_2atmpS3833 = _M0L6_2atmpS3834 / _M0L2tmS1130;
      _M0L6_2atmpS3831 = _M0L6_2atmpS3832 + _M0L6_2atmpS3833;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3830, _M0L1iS1145, _M0L6_2atmpS3831);
      _M0L9thresholdS3853 = _M0L1pS1128->$6;
      _M0L9thresholdS3861 = _M0L1pS1128->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3855
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS3861, _M0L1iS1145);
      _M0L9thresholdS3860 = _M0L1pS1128->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3859
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS3860, _M0L1iS1145);
      _M0L6_2atmpS3858 = _M0L2vtS1131 - _M0L6_2atmpS3859;
      _M0L6_2atmpS3857 = _M0L2dtS1143 * _M0L6_2atmpS3858;
      _M0L6_2atmpS3856 = _M0L6_2atmpS3857 / _M0L6tau__aS1140;
      _M0L6_2atmpS3854 = _M0L6_2atmpS3855 + _M0L6_2atmpS3856;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS3853, _M0L1iS1145, _M0L6_2atmpS3854);
      _M0L4fireS3862 = _M0L1pS1128->$5;
      _M0L1vS3865 = _M0L1pS1128->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3864 = _M0MPC15array5Array2atGfE(_M0L1vS3865, _M0L1iS1145);
      _M0L6_2atmpS3863 = _M0L6_2atmpS3864 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3862, _M0L1iS1145, _M0L6_2atmpS3863);
      _M0L1vS3866 = _M0L1pS1128->$3;
      _M0L4fireS3868 = _M0L1pS1128->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3868, _M0L1iS1145)) {
        _M0L6_2atmpS3867 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS3869 = _M0L1pS1128->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3867
        = _M0MPC15array5Array2atGfE(_M0L1vS3869, _M0L1iS1145);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3866, _M0L1iS1145, _M0L6_2atmpS3867);
      _M0L1wS3870 = _M0L1pS1128->$4;
      _M0L4fireS3872 = _M0L1pS1128->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3872, _M0L1iS1145)) {
        struct _M0TPB5ArrayGfE* _M0L1wS3874 = _M0L1pS1128->$4;
        float _M0L6_2atmpS3873;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3873
        = _M0MPC15array5Array2atGfE(_M0L1wS3874, _M0L1iS1145);
        _M0L6_2atmpS3871 = _M0L6_2atmpS3873 + _M0L1bS1138;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS3875 = _M0L1pS1128->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3871
        = _M0MPC15array5Array2atGfE(_M0L1wS3875, _M0L1iS1145);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS3870, _M0L1iS1145, _M0L6_2atmpS3871);
      _M0L9thresholdS3876 = _M0L1pS1128->$6;
      _M0L4fireS3878 = _M0L1pS1128->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3878, _M0L1iS1145)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS3880 = _M0L1pS1128->$6;
        float _M0L6_2atmpS3879;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3879
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS3880, _M0L1iS1145);
        _M0L6_2atmpS3877 = _M0L6_2atmpS3879 + _M0L2atS1139;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS3881 = _M0L1pS1128->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3877
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS3881, _M0L1iS1145);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS3876, _M0L1iS1145, _M0L6_2atmpS3877);
      _M0L4tabsS3882 = _M0L1pS1128->$7;
      _M0L4fireS3884 = _M0L1pS1128->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3884, _M0L1iS1145)) {
        _M0L6_2atmpS3883 = _M0L11tabs__stepsS1142;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS3885 = _M0L1pS1128->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS3883
        = _M0MPC15array5Array2atGiE(_M0L4tabsS3885, _M0L1iS1145);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS3882, _M0L1iS1145, _M0L6_2atmpS3883);
      goto join_1146;
      goto joinlet_5226;
      join_1146:;
      _M0L6_2atmpS3805 = _M0L1iS1145 + 1;
      _M0L1iS1145 = _M0L6_2atmpS3805;
      continue;
      joinlet_5226:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1123
) {
  int32_t _M0L1nS1122;
  int32_t _M0L7_2abindS1124;
  int32_t _M0L1iS1125;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1122 = _M0L1pS1123->$2;
  _M0L7_2abindS1124 = 0;
  _M0L1iS1125 = _M0L7_2abindS1124;
  while (1) {
    if (_M0L1iS1125 < _M0L1nS1122) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS3782 = _M0L1pS1123->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS3803 = _M0L1pS1123->$10;
      float _M0L6_2atmpS3798;
      struct _M0TPB5ArrayGfE* _M0L1vS3802;
      float _M0L6_2atmpS3800;
      float _M0L4e__eS3801;
      float _M0L6_2atmpS3799;
      float _M0L6_2atmpS3795;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS3797;
      float _M0L6_2atmpS3796;
      float _M0L6_2atmpS3784;
      struct _M0TPB5ArrayGfE* _M0L2giS3794;
      float _M0L6_2atmpS3789;
      struct _M0TPB5ArrayGfE* _M0L1vS3793;
      float _M0L6_2atmpS3791;
      float _M0L4e__iS3792;
      float _M0L6_2atmpS3790;
      float _M0L6_2atmpS3786;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS3788;
      float _M0L6_2atmpS3787;
      float _M0L6_2atmpS3785;
      float _M0L6_2atmpS3783;
      int32_t _M0L6_2atmpS3804;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3798 = _M0MPC15array5Array2atGfE(_M0L2geS3803, _M0L1iS1125);
      _M0L1vS3802 = _M0L1pS1123->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3800 = _M0MPC15array5Array2atGfE(_M0L1vS3802, _M0L1iS1125);
      _M0L4e__eS3801 = _M0L1pS1123->$18;
      _M0L6_2atmpS3799 = _M0L6_2atmpS3800 - _M0L4e__eS3801;
      _M0L6_2atmpS3795 = _M0L6_2atmpS3798 * _M0L6_2atmpS3799;
      _M0L7gsyn__eS3797 = _M0L1pS1123->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3796
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS3797, _M0L1iS1125);
      _M0L6_2atmpS3784 = _M0L6_2atmpS3795 * _M0L6_2atmpS3796;
      _M0L2giS3794 = _M0L1pS1123->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3789 = _M0MPC15array5Array2atGfE(_M0L2giS3794, _M0L1iS1125);
      _M0L1vS3793 = _M0L1pS1123->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3791 = _M0MPC15array5Array2atGfE(_M0L1vS3793, _M0L1iS1125);
      _M0L4e__iS3792 = _M0L1pS1123->$19;
      _M0L6_2atmpS3790 = _M0L6_2atmpS3791 - _M0L4e__iS3792;
      _M0L6_2atmpS3786 = _M0L6_2atmpS3789 * _M0L6_2atmpS3790;
      _M0L7gsyn__iS3788 = _M0L1pS1123->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3787
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS3788, _M0L1iS1125);
      _M0L6_2atmpS3785 = _M0L6_2atmpS3786 * _M0L6_2atmpS3787;
      _M0L6_2atmpS3783 = _M0L6_2atmpS3784 + _M0L6_2atmpS3785;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS3782, _M0L1iS1125, _M0L6_2atmpS3783);
      _M0L6_2atmpS3804 = _M0L1iS1125 + 1;
      _M0L1iS1125 = _M0L6_2atmpS3804;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1114,
  float _M0L2dtS1117
) {
  int32_t _M0L1nS1113;
  int32_t _M0L7_2abindS1115;
  int32_t _M0L1iS1116;
  int32_t _M0L7_2abindS1119;
  int32_t _M0L1iS1120;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1113 = _M0L1pS1114->$2;
  _M0L7_2abindS1115 = 0;
  _M0L1iS1116 = _M0L7_2abindS1115;
  while (1) {
    if (_M0L1iS1116 < _M0L1nS1113) {
      struct _M0TPB5ArrayGfE* _M0L2heS3720 = _M0L1pS1114->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS3725 = _M0L1pS1114->$12;
      float _M0L6_2atmpS3722;
      struct _M0TPB5ArrayGfE* _M0L3gluS3724;
      float _M0L6_2atmpS3723;
      float _M0L6_2atmpS3721;
      struct _M0TPB5ArrayGfE* _M0L2hiS3726;
      struct _M0TPB5ArrayGfE* _M0L2hiS3731;
      float _M0L6_2atmpS3728;
      struct _M0TPB5ArrayGfE* _M0L4gabaS3730;
      float _M0L6_2atmpS3729;
      float _M0L6_2atmpS3727;
      struct _M0TPB5ArrayGfE* _M0L2geS3732;
      struct _M0TPB5ArrayGfE* _M0L2geS3744;
      float _M0L6_2atmpS3734;
      struct _M0TPB5ArrayGfE* _M0L2geS3743;
      float _M0L6_2atmpS3742;
      float _M0L6_2atmpS3740;
      float _M0L3tdeS3741;
      float _M0L6_2atmpS3737;
      struct _M0TPB5ArrayGfE* _M0L2heS3739;
      float _M0L6_2atmpS3738;
      float _M0L6_2atmpS3736;
      float _M0L6_2atmpS3735;
      float _M0L6_2atmpS3733;
      struct _M0TPB5ArrayGfE* _M0L2heS3745;
      struct _M0TPB5ArrayGfE* _M0L2heS3754;
      float _M0L6_2atmpS3747;
      struct _M0TPB5ArrayGfE* _M0L2heS3753;
      float _M0L6_2atmpS3752;
      float _M0L6_2atmpS3750;
      float _M0L3treS3751;
      float _M0L6_2atmpS3749;
      float _M0L6_2atmpS3748;
      float _M0L6_2atmpS3746;
      struct _M0TPB5ArrayGfE* _M0L2giS3755;
      struct _M0TPB5ArrayGfE* _M0L2giS3767;
      float _M0L6_2atmpS3757;
      struct _M0TPB5ArrayGfE* _M0L2giS3766;
      float _M0L6_2atmpS3765;
      float _M0L6_2atmpS3763;
      float _M0L3tdiS3764;
      float _M0L6_2atmpS3760;
      struct _M0TPB5ArrayGfE* _M0L2hiS3762;
      float _M0L6_2atmpS3761;
      float _M0L6_2atmpS3759;
      float _M0L6_2atmpS3758;
      float _M0L6_2atmpS3756;
      struct _M0TPB5ArrayGfE* _M0L2hiS3768;
      struct _M0TPB5ArrayGfE* _M0L2hiS3777;
      float _M0L6_2atmpS3770;
      struct _M0TPB5ArrayGfE* _M0L2hiS3776;
      float _M0L6_2atmpS3775;
      float _M0L6_2atmpS3773;
      float _M0L3triS3774;
      float _M0L6_2atmpS3772;
      float _M0L6_2atmpS3771;
      float _M0L6_2atmpS3769;
      int32_t _M0L6_2atmpS3778;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3722 = _M0MPC15array5Array2atGfE(_M0L2heS3725, _M0L1iS1116);
      _M0L3gluS3724 = _M0L1pS1114->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3723
      = _M0MPC15array5Array2atGfE(_M0L3gluS3724, _M0L1iS1116);
      _M0L6_2atmpS3721 = _M0L6_2atmpS3722 + _M0L6_2atmpS3723;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS3720, _M0L1iS1116, _M0L6_2atmpS3721);
      _M0L2hiS3726 = _M0L1pS1114->$13;
      _M0L2hiS3731 = _M0L1pS1114->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3728 = _M0MPC15array5Array2atGfE(_M0L2hiS3731, _M0L1iS1116);
      _M0L4gabaS3730 = _M0L1pS1114->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3729
      = _M0MPC15array5Array2atGfE(_M0L4gabaS3730, _M0L1iS1116);
      _M0L6_2atmpS3727 = _M0L6_2atmpS3728 + _M0L6_2atmpS3729;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS3726, _M0L1iS1116, _M0L6_2atmpS3727);
      _M0L2geS3732 = _M0L1pS1114->$10;
      _M0L2geS3744 = _M0L1pS1114->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3734 = _M0MPC15array5Array2atGfE(_M0L2geS3744, _M0L1iS1116);
      _M0L2geS3743 = _M0L1pS1114->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3742 = _M0MPC15array5Array2atGfE(_M0L2geS3743, _M0L1iS1116);
      _M0L6_2atmpS3740 = -_M0L6_2atmpS3742;
      _M0L3tdeS3741 = _M0L1pS1114->$21;
      _M0L6_2atmpS3737 = _M0L6_2atmpS3740 / _M0L3tdeS3741;
      _M0L2heS3739 = _M0L1pS1114->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3738 = _M0MPC15array5Array2atGfE(_M0L2heS3739, _M0L1iS1116);
      _M0L6_2atmpS3736 = _M0L6_2atmpS3737 + _M0L6_2atmpS3738;
      _M0L6_2atmpS3735 = _M0L2dtS1117 * _M0L6_2atmpS3736;
      _M0L6_2atmpS3733 = _M0L6_2atmpS3734 + _M0L6_2atmpS3735;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS3732, _M0L1iS1116, _M0L6_2atmpS3733);
      _M0L2heS3745 = _M0L1pS1114->$12;
      _M0L2heS3754 = _M0L1pS1114->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3747 = _M0MPC15array5Array2atGfE(_M0L2heS3754, _M0L1iS1116);
      _M0L2heS3753 = _M0L1pS1114->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3752 = _M0MPC15array5Array2atGfE(_M0L2heS3753, _M0L1iS1116);
      _M0L6_2atmpS3750 = -_M0L6_2atmpS3752;
      _M0L3treS3751 = _M0L1pS1114->$20;
      _M0L6_2atmpS3749 = _M0L6_2atmpS3750 / _M0L3treS3751;
      _M0L6_2atmpS3748 = _M0L2dtS1117 * _M0L6_2atmpS3749;
      _M0L6_2atmpS3746 = _M0L6_2atmpS3747 + _M0L6_2atmpS3748;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS3745, _M0L1iS1116, _M0L6_2atmpS3746);
      _M0L2giS3755 = _M0L1pS1114->$11;
      _M0L2giS3767 = _M0L1pS1114->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3757 = _M0MPC15array5Array2atGfE(_M0L2giS3767, _M0L1iS1116);
      _M0L2giS3766 = _M0L1pS1114->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3765 = _M0MPC15array5Array2atGfE(_M0L2giS3766, _M0L1iS1116);
      _M0L6_2atmpS3763 = -_M0L6_2atmpS3765;
      _M0L3tdiS3764 = _M0L1pS1114->$23;
      _M0L6_2atmpS3760 = _M0L6_2atmpS3763 / _M0L3tdiS3764;
      _M0L2hiS3762 = _M0L1pS1114->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3761 = _M0MPC15array5Array2atGfE(_M0L2hiS3762, _M0L1iS1116);
      _M0L6_2atmpS3759 = _M0L6_2atmpS3760 + _M0L6_2atmpS3761;
      _M0L6_2atmpS3758 = _M0L2dtS1117 * _M0L6_2atmpS3759;
      _M0L6_2atmpS3756 = _M0L6_2atmpS3757 + _M0L6_2atmpS3758;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS3755, _M0L1iS1116, _M0L6_2atmpS3756);
      _M0L2hiS3768 = _M0L1pS1114->$13;
      _M0L2hiS3777 = _M0L1pS1114->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3770 = _M0MPC15array5Array2atGfE(_M0L2hiS3777, _M0L1iS1116);
      _M0L2hiS3776 = _M0L1pS1114->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS3775 = _M0MPC15array5Array2atGfE(_M0L2hiS3776, _M0L1iS1116);
      _M0L6_2atmpS3773 = -_M0L6_2atmpS3775;
      _M0L3triS3774 = _M0L1pS1114->$22;
      _M0L6_2atmpS3772 = _M0L6_2atmpS3773 / _M0L3triS3774;
      _M0L6_2atmpS3771 = _M0L2dtS1117 * _M0L6_2atmpS3772;
      _M0L6_2atmpS3769 = _M0L6_2atmpS3770 + _M0L6_2atmpS3771;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS3768, _M0L1iS1116, _M0L6_2atmpS3769);
      _M0L6_2atmpS3778 = _M0L1iS1116 + 1;
      _M0L1iS1116 = _M0L6_2atmpS3778;
      continue;
    }
    break;
  }
  _M0L7_2abindS1119 = 0;
  _M0L1iS1120 = _M0L7_2abindS1119;
  while (1) {
    if (_M0L1iS1120 < _M0L1nS1113) {
      struct _M0TPB5ArrayGfE* _M0L3gluS3779 = _M0L1pS1114->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS3780;
      int32_t _M0L6_2atmpS3781;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS3779, _M0L1iS1120, 0x0p+0f);
      _M0L4gabaS3780 = _M0L1pS1114->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS3780, _M0L1iS1120, 0x0p+0f);
      _M0L6_2atmpS3781 = _M0L1iS1120 + 1;
      _M0L1iS1120 = _M0L6_2atmpS3781;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1089,
  float _M0L6t__nowS1100
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS3719;
  int32_t _M0L6_2atmpS3718;
  int32_t _M0L10use__delayS1088;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3717;
  int32_t _M0L6_2atmpS3716;
  int32_t _M0L8use__rhoS1090;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS3719 = _M0L1cS1089->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS3718 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS3719);
  _M0L10use__delayS1088 = _M0L6_2atmpS3718 > 0;
  _M0L3rhoS3717 = _M0L1cS1089->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS3716 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3717);
  _M0L8use__rhoS1090 = _M0L6_2atmpS3716 > 0;
  if (_M0L10use__delayS1088) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3679 = _M0L1cS1089->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS3678 = _M0L3preS3679->$5;
    int32_t _M0L6n__preS1091;
    struct _M0TPB8MutLocalGiE* _M0L1jS1092;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1091 = _M0MPC15array5Array6lengthGbE(_M0L4fireS3678);
    _M0L1jS1092
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1092)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1092->$0 = 0;
    while (1) {
      int32_t _M0L3valS3647 = _M0L1jS1092->$0;
      if (_M0L3valS3647 < _M0L6n__preS1091) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3650 = _M0L1cS1089->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS3648 = _M0L3preS3650->$5;
        int32_t _M0L3valS3649 = _M0L1jS1092->$0;
        int32_t _M0L3valS3677;
        int32_t _M0L6_2atmpS3676;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS3648, _M0L3valS3649)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3675 =
            _M0L1cS1089->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS3673 = _M0L6matrixS3675->$2;
          int32_t _M0L3valS3674 = _M0L1jS1092->$0;
          int32_t _M0L5startS1093;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3672;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS3669;
          int32_t _M0L3valS3671;
          int32_t _M0L6_2atmpS3670;
          int32_t _M0L3endS1094;
          struct _M0TPB8MutLocalGiE* _M0L1sS1095;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1093
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS3673, _M0L3valS3674);
          _M0L6matrixS3672 = _M0L1cS1089->$4;
          _M0L6rowptrS3669 = _M0L6matrixS3672->$2;
          _M0L3valS3671 = _M0L1jS1092->$0;
          _M0L6_2atmpS3670 = _M0L3valS3671 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1094
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS3669, _M0L6_2atmpS3670);
          _M0L1sS1095
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1095)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1095->$0 = _M0L5startS1093;
          while (1) {
            int32_t _M0L3valS3651 = _M0L1sS1095->$0;
            if (_M0L3valS3651 < _M0L3endS1094) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3668 =
                _M0L1cS1089->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS3666 = _M0L6matrixS3668->$3;
              int32_t _M0L3valS3667 = _M0L1sS1095->$0;
              int32_t _M0L9post__idxS1096;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3665;
              struct _M0TPB5ArrayGfE* _M0L4valsS3663;
              int32_t _M0L3valS3664;
              float _M0L1wS1097;
              struct _M0TPB5ArrayGfE* _M0L6delaysS3661;
              int32_t _M0L3valS3662;
              float _M0L1dS1098;
              float _M0L9w__scaledS1099;
              int32_t _M0L3valS3657;
              int32_t _M0L6_2atmpS3656;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1096
              = _M0MPC15array5Array2atGiE(_M0L6colptrS3666, _M0L3valS3667);
              _M0L6matrixS3665 = _M0L1cS1089->$4;
              _M0L4valsS3663 = _M0L6matrixS3665->$4;
              _M0L3valS3664 = _M0L1sS1095->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1097
              = _M0MPC15array5Array2atGfE(_M0L4valsS3663, _M0L3valS3664);
              _M0L6delaysS3661 = _M0L1cS1089->$5;
              _M0L3valS3662 = _M0L1sS1095->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1098
              = _M0MPC15array5Array2atGfE(_M0L6delaysS3661, _M0L3valS3662);
              if (_M0L8use__rhoS1090) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS3659 = _M0L1cS1089->$6;
                int32_t _M0L3valS3660 = _M0L1sS1095->$0;
                float _M0L6_2atmpS3658;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS3658
                = _M0MPC15array5Array2atGfE(_M0L3rhoS3659, _M0L3valS3660);
                _M0L9w__scaledS1099 = _M0L1wS1097 * _M0L6_2atmpS3658;
              } else {
                _M0L9w__scaledS1099 = _M0L1wS1097;
              }
              if (_M0L1dS1098 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1089, _M0L9post__idxS1096, _M0L9w__scaledS1099);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS3652 =
                  _M0L1cS1089->$7;
                float _M0L6_2atmpS3653 = _M0L6t__nowS1100 + _M0L1dS1098;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS3654;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3655;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS3652, _M0L6_2atmpS3653);
                _M0L14pending__postsS3654 = _M0L1cS1089->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS3654, _M0L9post__idxS1096);
                _M0L16pending__weightsS3655 = _M0L1cS1089->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS3655, _M0L9w__scaledS1099);
              }
              _M0L3valS3657 = _M0L1sS1095->$0;
              _M0L6_2atmpS3656 = _M0L3valS3657 + 1;
              _M0L1sS1095->$0 = _M0L6_2atmpS3656;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1095);
            }
            break;
          }
        }
        _M0L3valS3677 = _M0L1jS1092->$0;
        _M0L6_2atmpS3676 = _M0L3valS3677 + 1;
        _M0L1jS1092->$0 = _M0L6_2atmpS3676;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1092);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS3713 = _M0L1cS1089->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1103;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS3713 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS3713)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS3713, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3713) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3714 = _M0L1cS1089->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS4917 = _M0L4postS3714->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS4917);
      _M0L6targetS1103 = _M0L8_2afieldS4917;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3715 = _M0L1cS1089->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS4918 = _M0L4postS3715->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS4918);
      _M0L6targetS1103 = _M0L8_2afieldS4918;
    }
    if (_M0L8use__rhoS1090) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3709 = _M0L1cS1089->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3708 = _M0L3preS3709->$5;
      int32_t _M0L6n__preS1104;
      struct _M0TPB8MutLocalGiE* _M0L1jS1105;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1104 = _M0MPC15array5Array6lengthGbE(_M0L4fireS3708);
      _M0L1jS1105
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1105)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1105->$0 = 0;
      while (1) {
        int32_t _M0L3valS3680 = _M0L1jS1105->$0;
        if (_M0L3valS3680 < _M0L6n__preS1104) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3683 = _M0L1cS1089->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS3681 = _M0L3preS3683->$5;
          int32_t _M0L3valS3682 = _M0L1jS1105->$0;
          int32_t _M0L3valS3707;
          int32_t _M0L6_2atmpS3706;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS3681, _M0L3valS3682)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3705 =
              _M0L1cS1089->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS3703 = _M0L6matrixS3705->$2;
            int32_t _M0L3valS3704 = _M0L1jS1105->$0;
            int32_t _M0L5startS1106;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3702;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS3699;
            int32_t _M0L3valS3701;
            int32_t _M0L6_2atmpS3700;
            int32_t _M0L3endS1107;
            struct _M0TPB8MutLocalGiE* _M0L1sS1108;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1106
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS3703, _M0L3valS3704);
            _M0L6matrixS3702 = _M0L1cS1089->$4;
            _M0L6rowptrS3699 = _M0L6matrixS3702->$2;
            _M0L3valS3701 = _M0L1jS1105->$0;
            _M0L6_2atmpS3700 = _M0L3valS3701 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1107
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS3699, _M0L6_2atmpS3700);
            _M0L1sS1108
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1108)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1108->$0 = _M0L5startS1106;
            while (1) {
              int32_t _M0L3valS3684 = _M0L1sS1108->$0;
              if (_M0L3valS3684 < _M0L3endS1107) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3698 =
                  _M0L1cS1089->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS3696 =
                  _M0L6matrixS3698->$3;
                int32_t _M0L3valS3697 = _M0L1sS1108->$0;
                int32_t _M0L9post__idxS1109;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3695;
                struct _M0TPB5ArrayGfE* _M0L4valsS3693;
                int32_t _M0L3valS3694;
                float _M0L6_2atmpS3689;
                struct _M0TPB5ArrayGfE* _M0L3rhoS3691;
                int32_t _M0L3valS3692;
                float _M0L6_2atmpS3690;
                float _M0L9w__scaledS1110;
                float _M0L6_2atmpS3686;
                float _M0L6_2atmpS3685;
                int32_t _M0L3valS3688;
                int32_t _M0L6_2atmpS3687;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1109
                = _M0MPC15array5Array2atGiE(_M0L6colptrS3696, _M0L3valS3697);
                _M0L6matrixS3695 = _M0L1cS1089->$4;
                _M0L4valsS3693 = _M0L6matrixS3695->$4;
                _M0L3valS3694 = _M0L1sS1108->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS3689
                = _M0MPC15array5Array2atGfE(_M0L4valsS3693, _M0L3valS3694);
                _M0L3rhoS3691 = _M0L1cS1089->$6;
                _M0L3valS3692 = _M0L1sS1108->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS3690
                = _M0MPC15array5Array2atGfE(_M0L3rhoS3691, _M0L3valS3692);
                _M0L9w__scaledS1110 = _M0L6_2atmpS3689 * _M0L6_2atmpS3690;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS3686
                = _M0MPC15array5Array2atGfE(_M0L6targetS1103, _M0L9post__idxS1109);
                _M0L6_2atmpS3685 = _M0L6_2atmpS3686 + _M0L9w__scaledS1110;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1103, _M0L9post__idxS1109, _M0L6_2atmpS3685);
                _M0L3valS3688 = _M0L1sS1108->$0;
                _M0L6_2atmpS3687 = _M0L3valS3688 + 1;
                _M0L1sS1108->$0 = _M0L6_2atmpS3687;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1108);
              }
              break;
            }
          }
          _M0L3valS3707 = _M0L1jS1105->$0;
          _M0L6_2atmpS3706 = _M0L3valS3707 + 1;
          _M0L1jS1105->$0 = _M0L6_2atmpS3706;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1105);
          moonbit_decref_cycle_free(_M0L6targetS1103);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3710 =
        _M0L1cS1089->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3712 = _M0L1cS1089->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3711 = _M0L3preS3712->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS3710, _M0L4fireS3711, _M0L6targetS1103);
      moonbit_decref_cycle_free(_M0L6targetS1103);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1081,
  float _M0L6t__nowS1084
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS3646;
  int32_t _M0L1nS1080;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1082;
  struct _M0TPB8MutLocalGiE* _M0L1kS1083;
  int32_t _M0L3valS3645;
  int32_t _M0L6_2atmpS3644;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1086;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS3646 = _M0L1cS1081->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1080 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS3646);
  if (_M0L1nS1080 == 0) {
    return 0;
  }
  _M0L4keptS1082
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1082)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1082->$0 = 0;
  _M0L1kS1083
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1083)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1083->$0 = 0;
  while (1) {
    int32_t _M0L3valS3607 = _M0L1kS1083->$0;
    if (_M0L3valS3607 < _M0L1nS1080) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS3609 = _M0L1cS1081->$7;
      int32_t _M0L3valS3610 = _M0L1kS1083->$0;
      float _M0L6_2atmpS3608;
      int32_t _M0L3valS3637;
      int32_t _M0L6_2atmpS3636;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS3608
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS3609, _M0L3valS3610);
      if (_M0L6_2atmpS3608 <= _M0L6t__nowS1084) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS3615 = _M0L1cS1081->$8;
        int32_t _M0L3valS3616 = _M0L1kS1083->$0;
        int32_t _M0L6_2atmpS3611;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3613;
        int32_t _M0L3valS3614;
        float _M0L6_2atmpS3612;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS3611
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS3615, _M0L3valS3616);
        _M0L16pending__weightsS3613 = _M0L1cS1081->$9;
        _M0L3valS3614 = _M0L1kS1083->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS3612
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS3613, _M0L3valS3614);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1081, _M0L6_2atmpS3611, _M0L6_2atmpS3612);
      } else {
        int32_t _M0L3valS3617 = _M0L4keptS1082->$0;
        int32_t _M0L3valS3618 = _M0L1kS1083->$0;
        int32_t _M0L3valS3635;
        int32_t _M0L6_2atmpS3634;
        if (_M0L3valS3617 != _M0L3valS3618) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS3619 = _M0L1cS1081->$7;
          int32_t _M0L3valS3620 = _M0L4keptS1082->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS3622 = _M0L1cS1081->$7;
          int32_t _M0L3valS3623 = _M0L1kS1083->$0;
          float _M0L6_2atmpS3621;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS3624;
          int32_t _M0L3valS3625;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS3627;
          int32_t _M0L3valS3628;
          int32_t _M0L6_2atmpS3626;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3629;
          int32_t _M0L3valS3630;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3632;
          int32_t _M0L3valS3633;
          float _M0L6_2atmpS3631;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS3621
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS3622, _M0L3valS3623);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS3619, _M0L3valS3620, _M0L6_2atmpS3621);
          _M0L14pending__postsS3624 = _M0L1cS1081->$8;
          _M0L3valS3625 = _M0L4keptS1082->$0;
          _M0L14pending__postsS3627 = _M0L1cS1081->$8;
          _M0L3valS3628 = _M0L1kS1083->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS3626
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS3627, _M0L3valS3628);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS3624, _M0L3valS3625, _M0L6_2atmpS3626);
          _M0L16pending__weightsS3629 = _M0L1cS1081->$9;
          _M0L3valS3630 = _M0L4keptS1082->$0;
          _M0L16pending__weightsS3632 = _M0L1cS1081->$9;
          _M0L3valS3633 = _M0L1kS1083->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS3631
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS3632, _M0L3valS3633);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS3629, _M0L3valS3630, _M0L6_2atmpS3631);
        }
        _M0L3valS3635 = _M0L4keptS1082->$0;
        _M0L6_2atmpS3634 = _M0L3valS3635 + 1;
        _M0L4keptS1082->$0 = _M0L6_2atmpS3634;
      }
      _M0L3valS3637 = _M0L1kS1083->$0;
      _M0L6_2atmpS3636 = _M0L3valS3637 + 1;
      _M0L1kS1083->$0 = _M0L6_2atmpS3636;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1083);
    }
    break;
  }
  _M0L3valS3645 = _M0L4keptS1082->$0;
  moonbit_decref_cycle_free(_M0L4keptS1082);
  _M0L6_2atmpS3644 = _M0L1nS1080 - _M0L3valS3645;
  _M0L4dropS1086
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1086->$0 = _M0L6_2atmpS3644;
  while (1) {
    int32_t _M0L3valS3638 = _M0L4dropS1086->$0;
    if (_M0L3valS3638 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS3639 = _M0L1cS1081->$7;
      void* _M0L6_2atmpS4920;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS3640;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3641;
      void* _M0L6_2atmpS4919;
      int32_t _M0L3valS3643;
      int32_t _M0L6_2atmpS3642;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS4920
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS3639);
      moonbit_decref_cycle_free(_M0L6_2atmpS4920);
      _M0L14pending__postsS3640 = _M0L1cS1081->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS3640);
      _M0L16pending__weightsS3641 = _M0L1cS1081->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS4919
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS3641);
      moonbit_decref_cycle_free(_M0L6_2atmpS4919);
      _M0L3valS3643 = _M0L4dropS1086->$0;
      _M0L6_2atmpS3642 = _M0L3valS3643 - 1;
      _M0L4dropS1086->$0 = _M0L6_2atmpS3642;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS1086);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1077,
  int32_t _M0L9post__idxS1078,
  float _M0L1wS1079
) {
  moonbit_string_t _M0L3symS3594;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS3594 = _M0L1cS1077->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS3594 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS3594)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS3594, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3594) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3600 = _M0L1cS1077->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS3595 = _M0L4postS3600->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3599 = _M0L1cS1077->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS3598 = _M0L4postS3599->$13;
    float _M0L6_2atmpS3597;
    float _M0L6_2atmpS3596;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS3597
    = _M0MPC15array5Array2atGfE(_M0L3gluS3598, _M0L9post__idxS1078);
    _M0L6_2atmpS3596 = _M0L6_2atmpS3597 + _M0L1wS1079;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS3595, _M0L9post__idxS1078, _M0L6_2atmpS3596);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3606 = _M0L1cS1077->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS3601 = _M0L4postS3606->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3605 = _M0L1cS1077->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS3604 = _M0L4postS3605->$14;
    float _M0L6_2atmpS3603;
    float _M0L6_2atmpS3602;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS3603
    = _M0MPC15array5Array2atGfE(_M0L4gabaS3604, _M0L9post__idxS1078);
    _M0L6_2atmpS3602 = _M0L6_2atmpS3603 + _M0L1wS1079;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS3601, _M0L9post__idxS1078, _M0L6_2atmpS3602);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1074,
  float _M0L1tS1076
) {
  int32_t _M0L11step__countS3580;
  int32_t _M0L6_2atmpS3579;
  int32_t _M0L11step__countS3582;
  int32_t _M0L9rec__stepS3583;
  int32_t _M0L6_2atmpS3581;
  moonbit_string_t _M0L3symS3586;
  float _M0L1vS1075;
  struct _M0TPB5ArrayGfE* _M0L4dataS3584;
  struct _M0TPB5ArrayGfE* _M0L5timesS3585;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS3580 = _M0L1mS1074->$6;
  _M0L6_2atmpS3579 = _M0L11step__countS3580 + 1;
  _M0L1mS1074->$6 = _M0L6_2atmpS3579;
  _M0L11step__countS3582 = _M0L1mS1074->$6;
  _M0L9rec__stepS3583 = _M0L1mS1074->$5;
  _M0L6_2atmpS3581 = _M0L11step__countS3582 % _M0L9rec__stepS3583;
  if (_M0L6_2atmpS3581 != 0) {
    return 0;
  }
  _M0L3symS3586 = _M0L1mS1074->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS3586 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS3586)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS3586, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS3586) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS3589 = _M0L1mS1074->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS3587 = _M0L3popS3589->$3;
    int32_t _M0L6neuronS3588 = _M0L1mS1074->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1075 = _M0MPC15array5Array2atGfE(_M0L1vS3587, _M0L6neuronS3588);
  } else {
    moonbit_string_t _M0L3symS3590 = _M0L1mS1074->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS3590 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS3590)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS3590, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS3590) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS3593 = _M0L1mS1074->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3591 = _M0L3popS3593->$5;
      int32_t _M0L6neuronS3592 = _M0L1mS1074->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3591, _M0L6neuronS3592)) {
        _M0L1vS1075 = 0x1p+0f;
      } else {
        _M0L1vS1075 = 0x0p+0f;
      }
    } else {
      _M0L1vS1075 = 0x0p+0f;
    }
  }
  _M0L4dataS3584 = _M0L1mS1074->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS3584, _M0L1vS1075);
  _M0L5timesS3585 = _M0L1mS1074->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS3585, _M0L1tS1076);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1072,
  int32_t _M0L6neuronS1073
) {
  float* _M0L6_2atmpS3578;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3575;
  float* _M0L6_2atmpS3577;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3576;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_5236;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS3578 = moonbit_empty_float_array;
  _M0L6_2atmpS3575
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3575)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L6_2atmpS3575->$0 = _M0L6_2atmpS3578;
  _M0L6_2atmpS3575->$1 = 0;
  _M0L6_2atmpS3577 = moonbit_empty_float_array;
  _M0L6_2atmpS3576
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3576)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L6_2atmpS3576->$0 = _M0L6_2atmpS3577;
  _M0L6_2atmpS3576->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1072);
  _block_5236
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_5236)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 84, 0);
  _block_5236->$0 = _M0L3popS1072;
  _block_5236->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_5236->$2 = _M0L6_2atmpS3575;
  _block_5236->$3 = _M0L6_2atmpS3576;
  _block_5236->$4 = _M0L6neuronS1073;
  _block_5236->$5 = 1;
  _block_5236->$6 = 0;
  return _block_5236;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1068
) {
  int32_t _M0L1nS1067;
  int32_t _M0L7_2abindS1069;
  int32_t _M0L1iS1070;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1067 = _M0L1pS1068->$2;
  _M0L7_2abindS1069 = 0;
  _M0L1iS1070 = _M0L7_2abindS1069;
  while (1) {
    if (_M0L1iS1070 < _M0L1nS1067) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS3552 = _M0L1pS1068->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS3573 = _M0L1pS1068->$9;
      float _M0L6_2atmpS3568;
      struct _M0TPB5ArrayGfE* _M0L1vS3572;
      float _M0L6_2atmpS3570;
      float _M0L4e__eS3571;
      float _M0L6_2atmpS3569;
      float _M0L6_2atmpS3565;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS3567;
      float _M0L6_2atmpS3566;
      float _M0L6_2atmpS3554;
      struct _M0TPB5ArrayGfE* _M0L2giS3564;
      float _M0L6_2atmpS3559;
      struct _M0TPB5ArrayGfE* _M0L1vS3563;
      float _M0L6_2atmpS3561;
      float _M0L4e__iS3562;
      float _M0L6_2atmpS3560;
      float _M0L6_2atmpS3556;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS3558;
      float _M0L6_2atmpS3557;
      float _M0L6_2atmpS3555;
      float _M0L6_2atmpS3553;
      int32_t _M0L6_2atmpS3574;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3568 = _M0MPC15array5Array2atGfE(_M0L2geS3573, _M0L1iS1070);
      _M0L1vS3572 = _M0L1pS1068->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3570 = _M0MPC15array5Array2atGfE(_M0L1vS3572, _M0L1iS1070);
      _M0L4e__eS3571 = _M0L1pS1068->$17;
      _M0L6_2atmpS3569 = _M0L6_2atmpS3570 - _M0L4e__eS3571;
      _M0L6_2atmpS3565 = _M0L6_2atmpS3568 * _M0L6_2atmpS3569;
      _M0L7gsyn__eS3567 = _M0L1pS1068->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3566
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS3567, _M0L1iS1070);
      _M0L6_2atmpS3554 = _M0L6_2atmpS3565 * _M0L6_2atmpS3566;
      _M0L2giS3564 = _M0L1pS1068->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3559 = _M0MPC15array5Array2atGfE(_M0L2giS3564, _M0L1iS1070);
      _M0L1vS3563 = _M0L1pS1068->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3561 = _M0MPC15array5Array2atGfE(_M0L1vS3563, _M0L1iS1070);
      _M0L4e__iS3562 = _M0L1pS1068->$18;
      _M0L6_2atmpS3560 = _M0L6_2atmpS3561 - _M0L4e__iS3562;
      _M0L6_2atmpS3556 = _M0L6_2atmpS3559 * _M0L6_2atmpS3560;
      _M0L7gsyn__iS3558 = _M0L1pS1068->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3557
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS3558, _M0L1iS1070);
      _M0L6_2atmpS3555 = _M0L6_2atmpS3556 * _M0L6_2atmpS3557;
      _M0L6_2atmpS3553 = _M0L6_2atmpS3554 + _M0L6_2atmpS3555;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS3552, _M0L1iS1070, _M0L6_2atmpS3553);
      _M0L6_2atmpS3574 = _M0L1iS1070 + 1;
      _M0L1iS1070 = _M0L6_2atmpS3574;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1059,
  float _M0L2dtS1062
) {
  int32_t _M0L1nS1058;
  int32_t _M0L7_2abindS1060;
  int32_t _M0L1iS1061;
  int32_t _M0L7_2abindS1064;
  int32_t _M0L1iS1065;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1058 = _M0L1pS1059->$2;
  _M0L7_2abindS1060 = 0;
  _M0L1iS1061 = _M0L7_2abindS1060;
  while (1) {
    if (_M0L1iS1061 < _M0L1nS1058) {
      struct _M0TPB5ArrayGfE* _M0L2heS3490 = _M0L1pS1059->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS3495 = _M0L1pS1059->$11;
      float _M0L6_2atmpS3492;
      struct _M0TPB5ArrayGfE* _M0L3gluS3494;
      float _M0L6_2atmpS3493;
      float _M0L6_2atmpS3491;
      struct _M0TPB5ArrayGfE* _M0L2hiS3496;
      struct _M0TPB5ArrayGfE* _M0L2hiS3501;
      float _M0L6_2atmpS3498;
      struct _M0TPB5ArrayGfE* _M0L4gabaS3500;
      float _M0L6_2atmpS3499;
      float _M0L6_2atmpS3497;
      struct _M0TPB5ArrayGfE* _M0L2geS3502;
      struct _M0TPB5ArrayGfE* _M0L2geS3514;
      float _M0L6_2atmpS3504;
      struct _M0TPB5ArrayGfE* _M0L2geS3513;
      float _M0L6_2atmpS3512;
      float _M0L6_2atmpS3510;
      float _M0L3tdeS3511;
      float _M0L6_2atmpS3507;
      struct _M0TPB5ArrayGfE* _M0L2heS3509;
      float _M0L6_2atmpS3508;
      float _M0L6_2atmpS3506;
      float _M0L6_2atmpS3505;
      float _M0L6_2atmpS3503;
      struct _M0TPB5ArrayGfE* _M0L2heS3515;
      struct _M0TPB5ArrayGfE* _M0L2heS3524;
      float _M0L6_2atmpS3517;
      struct _M0TPB5ArrayGfE* _M0L2heS3523;
      float _M0L6_2atmpS3522;
      float _M0L6_2atmpS3520;
      float _M0L3treS3521;
      float _M0L6_2atmpS3519;
      float _M0L6_2atmpS3518;
      float _M0L6_2atmpS3516;
      struct _M0TPB5ArrayGfE* _M0L2giS3525;
      struct _M0TPB5ArrayGfE* _M0L2giS3537;
      float _M0L6_2atmpS3527;
      struct _M0TPB5ArrayGfE* _M0L2giS3536;
      float _M0L6_2atmpS3535;
      float _M0L6_2atmpS3533;
      float _M0L3tdiS3534;
      float _M0L6_2atmpS3530;
      struct _M0TPB5ArrayGfE* _M0L2hiS3532;
      float _M0L6_2atmpS3531;
      float _M0L6_2atmpS3529;
      float _M0L6_2atmpS3528;
      float _M0L6_2atmpS3526;
      struct _M0TPB5ArrayGfE* _M0L2hiS3538;
      struct _M0TPB5ArrayGfE* _M0L2hiS3547;
      float _M0L6_2atmpS3540;
      struct _M0TPB5ArrayGfE* _M0L2hiS3546;
      float _M0L6_2atmpS3545;
      float _M0L6_2atmpS3543;
      float _M0L3triS3544;
      float _M0L6_2atmpS3542;
      float _M0L6_2atmpS3541;
      float _M0L6_2atmpS3539;
      int32_t _M0L6_2atmpS3548;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3492 = _M0MPC15array5Array2atGfE(_M0L2heS3495, _M0L1iS1061);
      _M0L3gluS3494 = _M0L1pS1059->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3493
      = _M0MPC15array5Array2atGfE(_M0L3gluS3494, _M0L1iS1061);
      _M0L6_2atmpS3491 = _M0L6_2atmpS3492 + _M0L6_2atmpS3493;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS3490, _M0L1iS1061, _M0L6_2atmpS3491);
      _M0L2hiS3496 = _M0L1pS1059->$12;
      _M0L2hiS3501 = _M0L1pS1059->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3498 = _M0MPC15array5Array2atGfE(_M0L2hiS3501, _M0L1iS1061);
      _M0L4gabaS3500 = _M0L1pS1059->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3499
      = _M0MPC15array5Array2atGfE(_M0L4gabaS3500, _M0L1iS1061);
      _M0L6_2atmpS3497 = _M0L6_2atmpS3498 + _M0L6_2atmpS3499;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS3496, _M0L1iS1061, _M0L6_2atmpS3497);
      _M0L2geS3502 = _M0L1pS1059->$9;
      _M0L2geS3514 = _M0L1pS1059->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3504 = _M0MPC15array5Array2atGfE(_M0L2geS3514, _M0L1iS1061);
      _M0L2geS3513 = _M0L1pS1059->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3512 = _M0MPC15array5Array2atGfE(_M0L2geS3513, _M0L1iS1061);
      _M0L6_2atmpS3510 = -_M0L6_2atmpS3512;
      _M0L3tdeS3511 = _M0L1pS1059->$20;
      _M0L6_2atmpS3507 = _M0L6_2atmpS3510 / _M0L3tdeS3511;
      _M0L2heS3509 = _M0L1pS1059->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3508 = _M0MPC15array5Array2atGfE(_M0L2heS3509, _M0L1iS1061);
      _M0L6_2atmpS3506 = _M0L6_2atmpS3507 + _M0L6_2atmpS3508;
      _M0L6_2atmpS3505 = _M0L2dtS1062 * _M0L6_2atmpS3506;
      _M0L6_2atmpS3503 = _M0L6_2atmpS3504 + _M0L6_2atmpS3505;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS3502, _M0L1iS1061, _M0L6_2atmpS3503);
      _M0L2heS3515 = _M0L1pS1059->$11;
      _M0L2heS3524 = _M0L1pS1059->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3517 = _M0MPC15array5Array2atGfE(_M0L2heS3524, _M0L1iS1061);
      _M0L2heS3523 = _M0L1pS1059->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3522 = _M0MPC15array5Array2atGfE(_M0L2heS3523, _M0L1iS1061);
      _M0L6_2atmpS3520 = -_M0L6_2atmpS3522;
      _M0L3treS3521 = _M0L1pS1059->$19;
      _M0L6_2atmpS3519 = _M0L6_2atmpS3520 / _M0L3treS3521;
      _M0L6_2atmpS3518 = _M0L2dtS1062 * _M0L6_2atmpS3519;
      _M0L6_2atmpS3516 = _M0L6_2atmpS3517 + _M0L6_2atmpS3518;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS3515, _M0L1iS1061, _M0L6_2atmpS3516);
      _M0L2giS3525 = _M0L1pS1059->$10;
      _M0L2giS3537 = _M0L1pS1059->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3527 = _M0MPC15array5Array2atGfE(_M0L2giS3537, _M0L1iS1061);
      _M0L2giS3536 = _M0L1pS1059->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3535 = _M0MPC15array5Array2atGfE(_M0L2giS3536, _M0L1iS1061);
      _M0L6_2atmpS3533 = -_M0L6_2atmpS3535;
      _M0L3tdiS3534 = _M0L1pS1059->$22;
      _M0L6_2atmpS3530 = _M0L6_2atmpS3533 / _M0L3tdiS3534;
      _M0L2hiS3532 = _M0L1pS1059->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3531 = _M0MPC15array5Array2atGfE(_M0L2hiS3532, _M0L1iS1061);
      _M0L6_2atmpS3529 = _M0L6_2atmpS3530 + _M0L6_2atmpS3531;
      _M0L6_2atmpS3528 = _M0L2dtS1062 * _M0L6_2atmpS3529;
      _M0L6_2atmpS3526 = _M0L6_2atmpS3527 + _M0L6_2atmpS3528;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS3525, _M0L1iS1061, _M0L6_2atmpS3526);
      _M0L2hiS3538 = _M0L1pS1059->$12;
      _M0L2hiS3547 = _M0L1pS1059->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3540 = _M0MPC15array5Array2atGfE(_M0L2hiS3547, _M0L1iS1061);
      _M0L2hiS3546 = _M0L1pS1059->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3545 = _M0MPC15array5Array2atGfE(_M0L2hiS3546, _M0L1iS1061);
      _M0L6_2atmpS3543 = -_M0L6_2atmpS3545;
      _M0L3triS3544 = _M0L1pS1059->$21;
      _M0L6_2atmpS3542 = _M0L6_2atmpS3543 / _M0L3triS3544;
      _M0L6_2atmpS3541 = _M0L2dtS1062 * _M0L6_2atmpS3542;
      _M0L6_2atmpS3539 = _M0L6_2atmpS3540 + _M0L6_2atmpS3541;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS3538, _M0L1iS1061, _M0L6_2atmpS3539);
      _M0L6_2atmpS3548 = _M0L1iS1061 + 1;
      _M0L1iS1061 = _M0L6_2atmpS3548;
      continue;
    }
    break;
  }
  _M0L7_2abindS1064 = 0;
  _M0L1iS1065 = _M0L7_2abindS1064;
  while (1) {
    if (_M0L1iS1065 < _M0L1nS1058) {
      struct _M0TPB5ArrayGfE* _M0L3gluS3549 = _M0L1pS1059->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS3550;
      int32_t _M0L6_2atmpS3551;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS3549, _M0L1iS1065, 0x0p+0f);
      _M0L4gabaS3550 = _M0L1pS1059->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS3550, _M0L1iS1065, 0x0p+0f);
      _M0L6_2atmpS3551 = _M0L1iS1065 + 1;
      _M0L1iS1065 = _M0L6_2atmpS3551;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1044,
  float _M0L2dtS1053
) {
  int32_t _M0L1nS1043;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1045;
  float _M0L2tmS1046;
  float _M0L2elS1047;
  float _M0L1rS1048;
  float _M0L2vtS1049;
  float _M0L2vrS1050;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS3489;
  float _M0L11tabs__constS1051;
  float _M0L6_2atmpS3488;
  int32_t _M0L11tabs__stepsS1052;
  int32_t _M0L7_2abindS1054;
  int32_t _M0L1iS1055;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1043 = _M0L1pS1044->$2;
  _M0L3p__S1045 = _M0L1pS1044->$0;
  _M0L2tmS1046 = _M0L3p__S1045->$2;
  _M0L2elS1047 = _M0L3p__S1045->$5;
  _M0L1rS1048 = _M0L3p__S1045->$6;
  _M0L2vtS1049 = _M0L3p__S1045->$3;
  _M0L2vrS1050 = _M0L3p__S1045->$4;
  _M0L5spikeS3489 = _M0L1pS1044->$1;
  _M0L11tabs__constS1051 = _M0L5spikeS3489->$0;
  _M0L6_2atmpS3488 = _M0L11tabs__constS1051 / _M0L2dtS1053;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1052 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3488);
  _M0L7_2abindS1054 = 0;
  _M0L1iS1055 = _M0L7_2abindS1054;
  while (1) {
    if (_M0L1iS1055 < _M0L1nS1043) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS3448 = _M0L1pS1044->$6;
      int32_t _M0L6_2atmpS3447;
      struct _M0TPB5ArrayGfE* _M0L1vS3454;
      struct _M0TPB5ArrayGfE* _M0L1vS3475;
      float _M0L6_2atmpS3456;
      float _M0L6_2atmpS3458;
      struct _M0TPB5ArrayGfE* _M0L1vS3474;
      float _M0L6_2atmpS3473;
      float _M0L6_2atmpS3472;
      float _M0L6_2atmpS3464;
      struct _M0TPB5ArrayGfE* _M0L1wS3471;
      float _M0L6_2atmpS3470;
      float _M0L6_2atmpS3467;
      struct _M0TPB5ArrayGfE* _M0L1iS3469;
      float _M0L6_2atmpS3468;
      float _M0L6_2atmpS3466;
      float _M0L6_2atmpS3465;
      float _M0L6_2atmpS3460;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS3463;
      float _M0L6_2atmpS3462;
      float _M0L6_2atmpS3461;
      float _M0L6_2atmpS3459;
      float _M0L6_2atmpS3457;
      float _M0L6_2atmpS3455;
      struct _M0TPB5ArrayGbE* _M0L4fireS3476;
      struct _M0TPB5ArrayGfE* _M0L1vS3479;
      float _M0L6_2atmpS3478;
      int32_t _M0L6_2atmpS3477;
      struct _M0TPB5ArrayGfE* _M0L1vS3480;
      struct _M0TPB5ArrayGbE* _M0L4fireS3482;
      float _M0L6_2atmpS3481;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3484;
      struct _M0TPB5ArrayGbE* _M0L4fireS3486;
      int32_t _M0L6_2atmpS3485;
      int32_t _M0L6_2atmpS3446;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3447
      = _M0MPC15array5Array2atGiE(_M0L4tabsS3448, _M0L1iS1055);
      if (_M0L6_2atmpS3447 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS3449 = _M0L1pS1044->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS3450;
        struct _M0TPB5ArrayGiE* _M0L4tabsS3453;
        int32_t _M0L6_2atmpS3452;
        int32_t _M0L6_2atmpS3451;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS3449, _M0L1iS1055, 0);
        _M0L4tabsS3450 = _M0L1pS1044->$6;
        _M0L4tabsS3453 = _M0L1pS1044->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS3452
        = _M0MPC15array5Array2atGiE(_M0L4tabsS3453, _M0L1iS1055);
        _M0L6_2atmpS3451 = _M0L6_2atmpS3452 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS3450, _M0L1iS1055, _M0L6_2atmpS3451);
        goto join_1056;
      }
      _M0L1vS3454 = _M0L1pS1044->$3;
      _M0L1vS3475 = _M0L1pS1044->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3456 = _M0MPC15array5Array2atGfE(_M0L1vS3475, _M0L1iS1055);
      _M0L6_2atmpS3458 = _M0L2dtS1053 / _M0L2tmS1046;
      _M0L1vS3474 = _M0L1pS1044->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3473 = _M0MPC15array5Array2atGfE(_M0L1vS3474, _M0L1iS1055);
      _M0L6_2atmpS3472 = _M0L6_2atmpS3473 - _M0L2elS1047;
      _M0L6_2atmpS3464 = -_M0L6_2atmpS3472;
      _M0L1wS3471 = _M0L1pS1044->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3470 = _M0MPC15array5Array2atGfE(_M0L1wS3471, _M0L1iS1055);
      _M0L6_2atmpS3467 = -_M0L6_2atmpS3470;
      _M0L1iS3469 = _M0L1pS1044->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3468 = _M0MPC15array5Array2atGfE(_M0L1iS3469, _M0L1iS1055);
      _M0L6_2atmpS3466 = _M0L6_2atmpS3467 + _M0L6_2atmpS3468;
      _M0L6_2atmpS3465 = _M0L1rS1048 * _M0L6_2atmpS3466;
      _M0L6_2atmpS3460 = _M0L6_2atmpS3464 + _M0L6_2atmpS3465;
      _M0L9syn__currS3463 = _M0L1pS1044->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3462
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS3463, _M0L1iS1055);
      _M0L6_2atmpS3461 = _M0L1rS1048 * _M0L6_2atmpS3462;
      _M0L6_2atmpS3459 = _M0L6_2atmpS3460 - _M0L6_2atmpS3461;
      _M0L6_2atmpS3457 = _M0L6_2atmpS3458 * _M0L6_2atmpS3459;
      _M0L6_2atmpS3455 = _M0L6_2atmpS3456 + _M0L6_2atmpS3457;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3454, _M0L1iS1055, _M0L6_2atmpS3455);
      _M0L4fireS3476 = _M0L1pS1044->$5;
      _M0L1vS3479 = _M0L1pS1044->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3478 = _M0MPC15array5Array2atGfE(_M0L1vS3479, _M0L1iS1055);
      _M0L6_2atmpS3477 = _M0L6_2atmpS3478 > _M0L2vtS1049;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3476, _M0L1iS1055, _M0L6_2atmpS3477);
      _M0L1vS3480 = _M0L1pS1044->$3;
      _M0L4fireS3482 = _M0L1pS1044->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3482, _M0L1iS1055)) {
        _M0L6_2atmpS3481 = _M0L2vrS1050;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS3483 = _M0L1pS1044->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS3481
        = _M0MPC15array5Array2atGfE(_M0L1vS3483, _M0L1iS1055);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3480, _M0L1iS1055, _M0L6_2atmpS3481);
      _M0L4tabsS3484 = _M0L1pS1044->$6;
      _M0L4fireS3486 = _M0L1pS1044->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3486, _M0L1iS1055)) {
        _M0L6_2atmpS3485 = _M0L11tabs__stepsS1052;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS3487 = _M0L1pS1044->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS3485
        = _M0MPC15array5Array2atGiE(_M0L4tabsS3487, _M0L1iS1055);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS3484, _M0L1iS1055, _M0L6_2atmpS3485);
      goto join_1056;
      goto joinlet_5241;
      join_1056:;
      _M0L6_2atmpS3446 = _M0L1iS1055 + 1;
      _M0L1iS1055 = _M0L6_2atmpS3446;
      continue;
      joinlet_5241:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1031,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1034,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1040
) {
  int32_t _M0L4rowsS1030;
  int32_t _M0L7_2abindS1032;
  int32_t _M0L1iS1033;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1030 = _M0L1mS1031->$0;
  _M0L7_2abindS1032 = 0;
  _M0L1iS1033 = _M0L7_2abindS1032;
  while (1) {
    if (_M0L1iS1033 < _M0L4rowsS1030) {
      int32_t _M0L6_2atmpS3445;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1034, _M0L1iS1033)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3444 = _M0L1mS1031->$2;
        int32_t _M0L5startS1035;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3442;
        int32_t _M0L6_2atmpS3443;
        int32_t _M0L3endS1036;
        int32_t _M0L1kS1037;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1035
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3444, _M0L1iS1033);
        _M0L6rowptrS3442 = _M0L1mS1031->$2;
        _M0L6_2atmpS3443 = _M0L1iS1033 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1036
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3442, _M0L6_2atmpS3443);
        _M0L1kS1037 = _M0L5startS1035;
        while (1) {
          if (_M0L1kS1037 < _M0L3endS1036) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS3440 = _M0L1mS1031->$3;
            int32_t _M0L9post__idxS1038;
            struct _M0TPB5ArrayGfE* _M0L4valsS3439;
            float _M0L1wS1039;
            float _M0L6_2atmpS3438;
            float _M0L6_2atmpS3437;
            int32_t _M0L6_2atmpS3441;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1038
            = _M0MPC15array5Array2atGiE(_M0L6colptrS3440, _M0L1kS1037);
            _M0L4valsS3439 = _M0L1mS1031->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1039
            = _M0MPC15array5Array2atGfE(_M0L4valsS3439, _M0L1kS1037);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS3438
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1040, _M0L9post__idxS1038);
            _M0L6_2atmpS3437 = _M0L6_2atmpS3438 + _M0L1wS1039;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1040, _M0L9post__idxS1038, _M0L6_2atmpS3437);
            _M0L6_2atmpS3441 = _M0L1kS1037 + 1;
            _M0L1kS1037 = _M0L6_2atmpS3441;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS3445 = _M0L1iS1033 + 1;
      _M0L1iS1033 = _M0L6_2atmpS3445;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS1024,
  int32_t _M0L4colsS1025,
  float _M0L2muS1026,
  float _M0L5sigmaS1027,
  float _M0L1pS1028,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1029
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS1024, _M0L4colsS1025, _M0L2muS1026, _M0L5sigmaS1027, _M0L1pS1028, 0, _M0L3rngS1029);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS938,
  int32_t _M0L4colsS942,
  float _M0L2muS948,
  float _M0L5sigmaS949,
  float _M0L1pS961,
  int32_t _M0L4ruleS955,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS951
) {
  float* _M0L6_2atmpS3436;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3435;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS937;
  int32_t _M0L7_2abindS939;
  int32_t _M0L1iS940;
  int32_t _M0L6_2atmpS3434;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1014;
  int32_t* _M0L6_2atmpS3433;
  struct _M0TPB5ArrayGiE* _M0L6colptrS1015;
  float* _M0L6_2atmpS3432;
  struct _M0TPB5ArrayGfE* _M0L4valsS1016;
  int32_t _M0L7_2abindS1017;
  int32_t _M0L1iS1018;
  int32_t _M0L6_2atmpS3431;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_5263;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS3436 = moonbit_empty_float_array;
  _M0L6_2atmpS3435
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3435)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L6_2atmpS3435->$0 = _M0L6_2atmpS3436;
  _M0L6_2atmpS3435->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS937
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS938, _M0L6_2atmpS3435);
  _M0L7_2abindS939 = 0;
  _M0L1iS940 = _M0L7_2abindS939;
  while (1) {
    if (_M0L1iS940 < _M0L4rowsS938) {
      struct _M0TPB5ArrayGfE* _M0L3rowS941;
      int32_t _M0L7_2abindS943;
      int32_t _M0L1jS944;
      int32_t _M0L6_2atmpS3387;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS941 = _M0MPC15array5Array4makeGfE(_M0L4colsS942, 0x0p+0f);
      _M0L7_2abindS943 = 0;
      _M0L1jS944 = _M0L7_2abindS943;
      while (1) {
        if (_M0L1jS944 < _M0L4colsS942) {
          double _M0L2z1S946;
          struct _M0TUddE* _M0L7_2abindS950;
          double _M0L5_2az1S952;
          float _M0L6_2atmpS3385;
          float _M0L6_2atmpS3384;
          float _M0L1wS947;
          int32_t _M0L6_2atmpS3386;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS950
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS951);
          _M0L5_2az1S952 = _M0L7_2abindS950->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS950);
          _M0L2z1S946 = _M0L5_2az1S952;
          goto join_945;
          goto joinlet_5246;
          join_945:;
          _M0L6_2atmpS3385 = (float)_M0L2z1S946;
          _M0L6_2atmpS3384 = _M0L5sigmaS949 * _M0L6_2atmpS3385;
          _M0L1wS947 = _M0L2muS948 + _M0L6_2atmpS3384;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS941, _M0L1jS944, _M0L1wS947);
          joinlet_5246:;
          _M0L6_2atmpS3386 = _M0L1jS944 + 1;
          _M0L1jS944 = _M0L6_2atmpS3386;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS937, _M0L1iS940, _M0L3rowS941);
      _M0L6_2atmpS3387 = _M0L1iS940 + 1;
      _M0L1iS940 = _M0L6_2atmpS3387;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS955) {
    case 0: {
      int32_t _M0L7_2abindS956 = 0;
      int32_t _M0L1iS957 = _M0L7_2abindS956;
      while (1) {
        if (_M0L1iS957 < _M0L4rowsS938) {
          int32_t _M0L7_2abindS958 = 0;
          int32_t _M0L1jS959 = _M0L7_2abindS958;
          int32_t _M0L6_2atmpS3390;
          while (1) {
            if (_M0L1jS959 < _M0L4colsS942) {
              float _M0L1uS960;
              int32_t _M0L6_2atmpS3389;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS960 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS951);
              if (_M0L1uS960 >= _M0L1pS961) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3388;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3388
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS937, _M0L1iS957);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3388, _M0L1jS959, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS3388);
              }
              _M0L6_2atmpS3389 = _M0L1jS959 + 1;
              _M0L1jS959 = _M0L6_2atmpS3389;
              continue;
            }
            break;
          }
          _M0L6_2atmpS3390 = _M0L1iS957 + 1;
          _M0L1iS957 = _M0L6_2atmpS3390;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS3408 = (float)_M0L4rowsS938;
      float _M0L6_2atmpS3407 = _M0L6_2atmpS3408 * _M0L1pS961;
      int32_t _M0L7n__keepS964;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS964 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3407);
      if (_M0L7n__keepS964 > 0 && _M0L7n__keepS964 <= _M0L4rowsS938) {
        int32_t _M0L7_2abindS965 = 0;
        int32_t _M0L1jS966 = _M0L7_2abindS965;
        while (1) {
          if (_M0L1jS966 < _M0L4colsS942) {
            int32_t* _M0L6_2atmpS3402 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS967 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS968;
            int32_t _M0L1kS969;
            int32_t _M0L7n__dropS971;
            int32_t _M0L7_2abindS972;
            int32_t _M0L1kS973;
            int32_t _M0L7_2abindS979;
            int32_t _M0L1kS980;
            int32_t _M0L6_2atmpS3403;
            Moonbit_object_header(_M0L8pre__idxS967)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
            _M0L8pre__idxS967->$0 = _M0L6_2atmpS3402;
            _M0L8pre__idxS967->$1 = 0;
            _M0L7_2abindS968 = 0;
            _M0L1kS969 = _M0L7_2abindS968;
            while (1) {
              if (_M0L1kS969 < _M0L4rowsS938) {
                int32_t _M0L6_2atmpS3391;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS967, _M0L1kS969);
                _M0L6_2atmpS3391 = _M0L1kS969 + 1;
                _M0L1kS969 = _M0L6_2atmpS3391;
                continue;
              }
              break;
            }
            _M0L7n__dropS971 = _M0L4rowsS938 - _M0L7n__keepS964;
            _M0L7_2abindS972 = 0;
            _M0L1kS973 = _M0L7_2abindS972;
            while (1) {
              if (_M0L1kS973 < _M0L7n__dropS971) {
                float _M0L1uS974;
                float _M0L6_2atmpS3395;
                float _M0L6_2atmpS3397;
                float _M0L6_2atmpS3396;
                float _M0L6_2atmpS3394;
                int32_t _M0L6_2atmpS3393;
                int32_t _M0L6r__idxS975;
                int32_t _M0L10r__clampedS976;
                int32_t _M0L3tmpS977;
                int32_t _M0L6_2atmpS3392;
                int32_t _M0L6_2atmpS3398;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS974 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS951);
                _M0L6_2atmpS3395 = (float)_M0L4rowsS938;
                _M0L6_2atmpS3397 = (float)_M0L1kS973;
                _M0L6_2atmpS3396 = _M0L6_2atmpS3397 * _M0L1uS974;
                _M0L6_2atmpS3394 = _M0L6_2atmpS3395 - _M0L6_2atmpS3396;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3393
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS3394);
                _M0L6r__idxS975 = _M0L1kS973 + _M0L6_2atmpS3393;
                if (_M0L6r__idxS975 >= _M0L4rowsS938) {
                  _M0L10r__clampedS976 = _M0L4rowsS938 - 1;
                } else {
                  _M0L10r__clampedS976 = _M0L6r__idxS975;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS977
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS967, _M0L1kS973);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3392
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS967, _M0L10r__clampedS976);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS967, _M0L1kS973, _M0L6_2atmpS3392);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS967, _M0L10r__clampedS976, _M0L3tmpS977);
                _M0L6_2atmpS3398 = _M0L1kS973 + 1;
                _M0L1kS973 = _M0L6_2atmpS3398;
                continue;
              }
              break;
            }
            _M0L7_2abindS979 = 0;
            _M0L1kS980 = _M0L7_2abindS979;
            while (1) {
              if (_M0L1kS980 < _M0L7n__dropS971) {
                int32_t _M0L6_2atmpS3400;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3399;
                int32_t _M0L6_2atmpS3401;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3400
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS967, _M0L1kS980);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3399
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS937, _M0L6_2atmpS3400);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3399, _M0L1jS966, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS3399);
                _M0L6_2atmpS3401 = _M0L1kS980 + 1;
                _M0L1kS980 = _M0L6_2atmpS3401;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS967);
              }
              break;
            }
            _M0L6_2atmpS3403 = _M0L1jS966 + 1;
            _M0L1jS966 = _M0L6_2atmpS3403;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS964 == 0) {
        int32_t _M0L7_2abindS983 = 0;
        int32_t _M0L1iS984 = _M0L7_2abindS983;
        while (1) {
          if (_M0L1iS984 < _M0L4rowsS938) {
            int32_t _M0L7_2abindS985 = 0;
            int32_t _M0L1jS986 = _M0L7_2abindS985;
            int32_t _M0L6_2atmpS3406;
            while (1) {
              if (_M0L1jS986 < _M0L4colsS942) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3404;
                int32_t _M0L6_2atmpS3405;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3404
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS937, _M0L1iS984);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3404, _M0L1jS986, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS3404);
                _M0L6_2atmpS3405 = _M0L1jS986 + 1;
                _M0L1jS986 = _M0L6_2atmpS3405;
                continue;
              }
              break;
            }
            _M0L6_2atmpS3406 = _M0L1iS984 + 1;
            _M0L1iS984 = _M0L6_2atmpS3406;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS3426 = (float)_M0L4colsS942;
      float _M0L6_2atmpS3425 = _M0L6_2atmpS3426 * _M0L1pS961;
      int32_t _M0L7n__keepS989;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS989 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3425);
      if (_M0L7n__keepS989 > 0 && _M0L7n__keepS989 <= _M0L4colsS942) {
        int32_t _M0L7_2abindS990 = 0;
        int32_t _M0L1iS991 = _M0L7_2abindS990;
        while (1) {
          if (_M0L1iS991 < _M0L4rowsS938) {
            int32_t* _M0L6_2atmpS3420 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS992 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS993;
            int32_t _M0L1kS994;
            int32_t _M0L7n__dropS996;
            int32_t _M0L7_2abindS997;
            int32_t _M0L1kS998;
            int32_t _M0L7_2abindS1004;
            int32_t _M0L1kS1005;
            int32_t _M0L6_2atmpS3421;
            Moonbit_object_header(_M0L9post__idxS992)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
            _M0L9post__idxS992->$0 = _M0L6_2atmpS3420;
            _M0L9post__idxS992->$1 = 0;
            _M0L7_2abindS993 = 0;
            _M0L1kS994 = _M0L7_2abindS993;
            while (1) {
              if (_M0L1kS994 < _M0L4colsS942) {
                int32_t _M0L6_2atmpS3409;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS992, _M0L1kS994);
                _M0L6_2atmpS3409 = _M0L1kS994 + 1;
                _M0L1kS994 = _M0L6_2atmpS3409;
                continue;
              }
              break;
            }
            _M0L7n__dropS996 = _M0L4colsS942 - _M0L7n__keepS989;
            _M0L7_2abindS997 = 0;
            _M0L1kS998 = _M0L7_2abindS997;
            while (1) {
              if (_M0L1kS998 < _M0L7n__dropS996) {
                float _M0L1uS999;
                float _M0L6_2atmpS3413;
                float _M0L6_2atmpS3415;
                float _M0L6_2atmpS3414;
                float _M0L6_2atmpS3412;
                int32_t _M0L6_2atmpS3411;
                int32_t _M0L6r__idxS1000;
                int32_t _M0L10r__clampedS1001;
                int32_t _M0L3tmpS1002;
                int32_t _M0L6_2atmpS3410;
                int32_t _M0L6_2atmpS3416;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS999 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS951);
                _M0L6_2atmpS3413 = (float)_M0L4colsS942;
                _M0L6_2atmpS3415 = (float)_M0L1kS998;
                _M0L6_2atmpS3414 = _M0L6_2atmpS3415 * _M0L1uS999;
                _M0L6_2atmpS3412 = _M0L6_2atmpS3413 - _M0L6_2atmpS3414;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3411
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS3412);
                _M0L6r__idxS1000 = _M0L1kS998 + _M0L6_2atmpS3411;
                if (_M0L6r__idxS1000 >= _M0L4colsS942) {
                  _M0L10r__clampedS1001 = _M0L4colsS942 - 1;
                } else {
                  _M0L10r__clampedS1001 = _M0L6r__idxS1000;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1002
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS992, _M0L1kS998);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3410
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS992, _M0L10r__clampedS1001);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS992, _M0L1kS998, _M0L6_2atmpS3410);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS992, _M0L10r__clampedS1001, _M0L3tmpS1002);
                _M0L6_2atmpS3416 = _M0L1kS998 + 1;
                _M0L1kS998 = _M0L6_2atmpS3416;
                continue;
              }
              break;
            }
            _M0L7_2abindS1004 = 0;
            _M0L1kS1005 = _M0L7_2abindS1004;
            while (1) {
              if (_M0L1kS1005 < _M0L7n__dropS996) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3417;
                int32_t _M0L6_2atmpS3418;
                int32_t _M0L6_2atmpS3419;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3417
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS937, _M0L1iS991);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3418
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS992, _M0L1kS1005);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3417, _M0L6_2atmpS3418, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS3417);
                _M0L6_2atmpS3419 = _M0L1kS1005 + 1;
                _M0L1kS1005 = _M0L6_2atmpS3419;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS992);
              }
              break;
            }
            _M0L6_2atmpS3421 = _M0L1iS991 + 1;
            _M0L1iS991 = _M0L6_2atmpS3421;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS989 == 0) {
        int32_t _M0L7_2abindS1008 = 0;
        int32_t _M0L1iS1009 = _M0L7_2abindS1008;
        while (1) {
          if (_M0L1iS1009 < _M0L4rowsS938) {
            int32_t _M0L7_2abindS1010 = 0;
            int32_t _M0L1jS1011 = _M0L7_2abindS1010;
            int32_t _M0L6_2atmpS3424;
            while (1) {
              if (_M0L1jS1011 < _M0L4colsS942) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3422;
                int32_t _M0L6_2atmpS3423;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3422
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS937, _M0L1iS1009);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3422, _M0L1jS1011, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS3422);
                _M0L6_2atmpS3423 = _M0L1jS1011 + 1;
                _M0L1jS1011 = _M0L6_2atmpS3423;
                continue;
              }
              break;
            }
            _M0L6_2atmpS3424 = _M0L1iS1009 + 1;
            _M0L1iS1009 = _M0L6_2atmpS3424;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS3434 = _M0L4rowsS938 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS1014 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS3434, 0);
  _M0L6_2atmpS3433 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS1015
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS1015)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _M0L6colptrS1015->$0 = _M0L6_2atmpS3433;
  _M0L6colptrS1015->$1 = 0;
  _M0L6_2atmpS3432 = moonbit_empty_float_array;
  _M0L4valsS1016
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS1016)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L4valsS1016->$0 = _M0L6_2atmpS3432;
  _M0L4valsS1016->$1 = 0;
  _M0L7_2abindS1017 = 0;
  _M0L1iS1018 = _M0L7_2abindS1017;
  while (1) {
    if (_M0L1iS1018 < _M0L4rowsS938) {
      int32_t _M0L6_2atmpS3427;
      int32_t _M0L7_2abindS1019;
      int32_t _M0L1jS1020;
      int32_t _M0L6_2atmpS3430;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS3427 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1016);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS1014, _M0L1iS1018, _M0L6_2atmpS3427);
      _M0L7_2abindS1019 = 0;
      _M0L1jS1020 = _M0L7_2abindS1019;
      while (1) {
        if (_M0L1jS1020 < _M0L4colsS942) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS3428;
          float _M0L1vS1021;
          int32_t _M0L6_2atmpS3429;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS3428
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS937, _M0L1iS1018);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS1021
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS3428, _M0L1jS1020);
          moonbit_decref_cycle_free(_M0L6_2atmpS3428);
          if (_M0L1vS1021 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS1015, _M0L1jS1020);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS1016, _M0L1vS1021);
          }
          _M0L6_2atmpS3429 = _M0L1jS1020 + 1;
          _M0L1jS1020 = _M0L6_2atmpS3429;
          continue;
        }
        break;
      }
      _M0L6_2atmpS3430 = _M0L1iS1018 + 1;
      _M0L1iS1018 = _M0L6_2atmpS3430;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS937);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS3431 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1016);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS1014, _M0L4rowsS938, _M0L6_2atmpS3431);
  _block_5263
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_5263)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 90, 0);
  _block_5263->$0 = _M0L4rowsS938;
  _block_5263->$1 = _M0L4colsS942;
  _block_5263->$2 = _M0L6rowptrS1014;
  _block_5263->$3 = _M0L6colptrS1015;
  _block_5263->$4 = _M0L4valsS1016;
  return _block_5263;
}

int32_t _M0FP26RiantR8snn__mbt20ca__plasticity__step(
  struct _M0TPB5ArrayGfE* _M0L1wS915,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS902,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS904,
  struct _M0TPB5ArrayGiE* _M0L6colptrS914,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS910,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS899,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS906,
  float _M0L6t__nowS900,
  float _M0L2dtS927
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3275;
  int32_t _M0L6_2atmpS3274;
  int32_t _if__result_5264;
  int32_t _M0L6n__preS901;
  int32_t _M0L7n__postS903;
  float _M0L8tau__preS3383;
  float _M0L13inv__tau__preS905;
  float _M0L9tau__postS3382;
  float _M0L14inv__tau__postS907;
  struct _M0TPB8MutLocalGiE* _M0L1jS908;
  struct _M0TPB8MutLocalGiE* _M0L1kS918;
  struct _M0TPB8MutLocalGiE* _M0L2jjS926;
  struct _M0TPB8MutLocalGiE* _M0L2iiS929;
  struct _M0TPB8MutLocalGiE* _M0L3jj2S931;
  struct _M0TPB8MutLocalGiE* _M0L3ii2S933;
  struct _M0TPB8MutLocalGiE* _M0L2s2S935;
  #line 1495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3275 = _M0L4varsS899->$4;
  #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3274 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3275);
  if (_M0L6_2atmpS3274 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3273 = _M0L4varsS899->$4;
    int32_t _M0L6_2atmpS3272;
    #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3272 = _M0MPC15array5Array2atGbE(_M0L6activeS3273, 0);
    _if__result_5264 = !_M0L6_2atmpS3272;
  } else {
    _if__result_5264 = 0;
  }
  if (_if__result_5264) {
    return 0;
  }
  #line 1509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS901 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS902);
  #line 1510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS903 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS904);
  _M0L8tau__preS3383 = _M0L5paramS906->$2;
  _M0L13inv__tau__preS905 = 0x1p+0f / _M0L8tau__preS3383;
  _M0L9tau__postS3382 = _M0L5paramS906->$3;
  _M0L14inv__tau__postS907 = 0x1p+0f / _M0L9tau__postS3382;
  _M0L1jS908
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS908)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS908->$0 = 0;
  while (1) {
    int32_t _M0L3valS3276 = _M0L1jS908->$0;
    if (_M0L3valS3276 < _M0L6n__preS901) {
      int32_t _M0L3valS3277 = _M0L1jS908->$0;
      int32_t _M0L3valS3292;
      int32_t _M0L6_2atmpS3291;
      #line 1516 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS902, _M0L3valS3277)) {
        int32_t _M0L3valS3290 = _M0L1jS908->$0;
        int32_t _M0L5startS909;
        int32_t _M0L3valS3289;
        int32_t _M0L6_2atmpS3288;
        int32_t _M0L5end__S911;
        struct _M0TPB8MutLocalGiE* _M0L1sS912;
        #line 1517 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS909
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS910, _M0L3valS3290);
        _M0L3valS3289 = _M0L1jS908->$0;
        _M0L6_2atmpS3288 = _M0L3valS3289 + 1;
        #line 1518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5end__S911
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS910, _M0L6_2atmpS3288);
        _M0L1sS912
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS912)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS912->$0 = _M0L5startS909;
        while (1) {
          int32_t _M0L3valS3278 = _M0L1sS912->$0;
          if (_M0L3valS3278 < _M0L5end__S911) {
            int32_t _M0L3valS3287 = _M0L1sS912->$0;
            int32_t _M0L1iS913;
            int32_t _M0L3valS3279;
            int32_t _M0L3valS3284;
            float _M0L6_2atmpS3281;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3283;
            float _M0L6_2atmpS3282;
            float _M0L6_2atmpS3280;
            int32_t _M0L3valS3286;
            int32_t _M0L6_2atmpS3285;
            #line 1521 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L1iS913
            = _M0MPC15array5Array2atGiE(_M0L6colptrS914, _M0L3valS3287);
            _M0L3valS3279 = _M0L1sS912->$0;
            _M0L3valS3284 = _M0L1sS912->$0;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3281
            = _M0MPC15array5Array2atGfE(_M0L1wS915, _M0L3valS3284);
            _M0L5tpostS3283 = _M0L4varsS899->$3;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3282
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3283, _M0L1iS913);
            _M0L6_2atmpS3280 = _M0L6_2atmpS3281 + _M0L6_2atmpS3282;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS915, _M0L3valS3279, _M0L6_2atmpS3280);
            _M0L3valS3286 = _M0L1sS912->$0;
            _M0L6_2atmpS3285 = _M0L3valS3286 + 1;
            _M0L1sS912->$0 = _M0L6_2atmpS3285;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS912);
          }
          break;
        }
      }
      _M0L3valS3292 = _M0L1jS908->$0;
      _M0L6_2atmpS3291 = _M0L3valS3292 + 1;
      _M0L1jS908->$0 = _M0L6_2atmpS3291;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS908);
    }
    break;
  }
  _M0L1kS918
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS918)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS918->$0 = 0;
  while (1) {
    int32_t _M0L3valS3293 = _M0L1kS918->$0;
    if (_M0L3valS3293 < _M0L7n__postS903) {
      int32_t _M0L3valS3294 = _M0L1kS918->$0;
      int32_t _M0L3valS3315;
      int32_t _M0L6_2atmpS3314;
      #line 1531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS904, _M0L3valS3294)) {
        struct _M0TPB8MutLocalGiE* _M0L2j2S919 =
          (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L2j2S919)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2j2S919->$0 = 0;
        while (1) {
          int32_t _M0L3valS3295 = _M0L2j2S919->$0;
          if (_M0L3valS3295 < _M0L6n__preS901) {
            int32_t _M0L3valS3313 = _M0L2j2S919->$0;
            int32_t _M0L5startS920;
            int32_t _M0L3valS3312;
            int32_t _M0L6_2atmpS3311;
            int32_t _M0L5end__S921;
            struct _M0TPB8MutLocalGiE* _M0L1sS922;
            int32_t _M0L3valS3310;
            int32_t _M0L6_2atmpS3309;
            #line 1537 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5startS920
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS910, _M0L3valS3313);
            _M0L3valS3312 = _M0L2j2S919->$0;
            _M0L6_2atmpS3311 = _M0L3valS3312 + 1;
            #line 1538 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5end__S921
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS910, _M0L6_2atmpS3311);
            _M0L1sS922
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS922)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS922->$0 = _M0L5startS920;
            while (1) {
              int32_t _M0L3valS3296 = _M0L1sS922->$0;
              if (_M0L3valS3296 < _M0L5end__S921) {
                int32_t _M0L3valS3299 = _M0L1sS922->$0;
                int32_t _M0L6_2atmpS3297;
                int32_t _M0L3valS3298;
                int32_t _M0L3valS3308;
                int32_t _M0L6_2atmpS3307;
                #line 1541 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                _M0L6_2atmpS3297
                = _M0MPC15array5Array2atGiE(_M0L6colptrS914, _M0L3valS3299);
                _M0L3valS3298 = _M0L1kS918->$0;
                if (_M0L6_2atmpS3297 == _M0L3valS3298) {
                  int32_t _M0L3valS3300 = _M0L1sS922->$0;
                  int32_t _M0L3valS3306 = _M0L1sS922->$0;
                  float _M0L6_2atmpS3302;
                  struct _M0TPB5ArrayGfE* _M0L4tpreS3304;
                  int32_t _M0L3valS3305;
                  float _M0L6_2atmpS3303;
                  float _M0L6_2atmpS3301;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS3302
                  = _M0MPC15array5Array2atGfE(_M0L1wS915, _M0L3valS3306);
                  _M0L4tpreS3304 = _M0L4varsS899->$2;
                  _M0L3valS3305 = _M0L2j2S919->$0;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS3303
                  = _M0MPC15array5Array2atGfE(_M0L4tpreS3304, _M0L3valS3305);
                  _M0L6_2atmpS3301 = _M0L6_2atmpS3302 + _M0L6_2atmpS3303;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0MPC15array5Array3setGfE(_M0L1wS915, _M0L3valS3300, _M0L6_2atmpS3301);
                }
                _M0L3valS3308 = _M0L1sS922->$0;
                _M0L6_2atmpS3307 = _M0L3valS3308 + 1;
                _M0L1sS922->$0 = _M0L6_2atmpS3307;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS922);
              }
              break;
            }
            _M0L3valS3310 = _M0L2j2S919->$0;
            _M0L6_2atmpS3309 = _M0L3valS3310 + 1;
            _M0L2j2S919->$0 = _M0L6_2atmpS3309;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L2j2S919);
          }
          break;
        }
      }
      _M0L3valS3315 = _M0L1kS918->$0;
      _M0L6_2atmpS3314 = _M0L3valS3315 + 1;
      _M0L1kS918->$0 = _M0L6_2atmpS3314;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS918);
    }
    break;
  }
  _M0L2jjS926
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2jjS926)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2jjS926->$0 = 0;
  while (1) {
    int32_t _M0L3valS3316 = _M0L2jjS926->$0;
    if (_M0L3valS3316 < _M0L6n__preS901) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3317 = _M0L4varsS899->$2;
      int32_t _M0L3valS3318 = _M0L2jjS926->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3327 = _M0L4varsS899->$2;
      int32_t _M0L3valS3328 = _M0L2jjS926->$0;
      float _M0L6_2atmpS3320;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3325;
      int32_t _M0L3valS3326;
      float _M0L6_2atmpS3324;
      float _M0L6_2atmpS3323;
      float _M0L6_2atmpS3322;
      float _M0L6_2atmpS3321;
      float _M0L6_2atmpS3319;
      int32_t _M0L3valS3330;
      int32_t _M0L6_2atmpS3329;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3320
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3327, _M0L3valS3328);
      _M0L4tpreS3325 = _M0L4varsS899->$2;
      _M0L3valS3326 = _M0L2jjS926->$0;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3324
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3325, _M0L3valS3326);
      _M0L6_2atmpS3323 = -_M0L6_2atmpS3324;
      _M0L6_2atmpS3322 = _M0L2dtS927 * _M0L6_2atmpS3323;
      _M0L6_2atmpS3321 = _M0L6_2atmpS3322 * _M0L13inv__tau__preS905;
      _M0L6_2atmpS3319 = _M0L6_2atmpS3320 + _M0L6_2atmpS3321;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3317, _M0L3valS3318, _M0L6_2atmpS3319);
      _M0L3valS3330 = _M0L2jjS926->$0;
      _M0L6_2atmpS3329 = _M0L3valS3330 + 1;
      _M0L2jjS926->$0 = _M0L6_2atmpS3329;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2jjS926);
    }
    break;
  }
  _M0L2iiS929
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2iiS929)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2iiS929->$0 = 0;
  while (1) {
    int32_t _M0L3valS3331 = _M0L2iiS929->$0;
    if (_M0L3valS3331 < _M0L7n__postS903) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3332 = _M0L4varsS899->$3;
      int32_t _M0L3valS3333 = _M0L2iiS929->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3342 = _M0L4varsS899->$3;
      int32_t _M0L3valS3343 = _M0L2iiS929->$0;
      float _M0L6_2atmpS3335;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3340;
      int32_t _M0L3valS3341;
      float _M0L6_2atmpS3339;
      float _M0L6_2atmpS3338;
      float _M0L6_2atmpS3337;
      float _M0L6_2atmpS3336;
      float _M0L6_2atmpS3334;
      int32_t _M0L3valS3345;
      int32_t _M0L6_2atmpS3344;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3335
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3342, _M0L3valS3343);
      _M0L5tpostS3340 = _M0L4varsS899->$3;
      _M0L3valS3341 = _M0L2iiS929->$0;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3339
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3340, _M0L3valS3341);
      _M0L6_2atmpS3338 = -_M0L6_2atmpS3339;
      _M0L6_2atmpS3337 = _M0L2dtS927 * _M0L6_2atmpS3338;
      _M0L6_2atmpS3336 = _M0L6_2atmpS3337 * _M0L14inv__tau__postS907;
      _M0L6_2atmpS3334 = _M0L6_2atmpS3335 + _M0L6_2atmpS3336;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3332, _M0L3valS3333, _M0L6_2atmpS3334);
      _M0L3valS3345 = _M0L2iiS929->$0;
      _M0L6_2atmpS3344 = _M0L3valS3345 + 1;
      _M0L2iiS929->$0 = _M0L6_2atmpS3344;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2iiS929);
    }
    break;
  }
  _M0L3jj2S931
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3jj2S931)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3jj2S931->$0 = 0;
  while (1) {
    int32_t _M0L3valS3346 = _M0L3jj2S931->$0;
    if (_M0L3valS3346 < _M0L6n__preS901) {
      int32_t _M0L3valS3347 = _M0L3jj2S931->$0;
      int32_t _M0L3valS3356;
      int32_t _M0L6_2atmpS3355;
      #line 1565 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS902, _M0L3valS3347)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3348 = _M0L4varsS899->$2;
        int32_t _M0L3valS3349 = _M0L3jj2S931->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3353 = _M0L4varsS899->$2;
        int32_t _M0L3valS3354 = _M0L3jj2S931->$0;
        float _M0L6_2atmpS3351;
        float _M0L6a__preS3352;
        float _M0L6_2atmpS3350;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3351
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3353, _M0L3valS3354);
        _M0L6a__preS3352 = _M0L5paramS906->$0;
        _M0L6_2atmpS3350 = _M0L6_2atmpS3351 + _M0L6a__preS3352;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3348, _M0L3valS3349, _M0L6_2atmpS3350);
      }
      _M0L3valS3356 = _M0L3jj2S931->$0;
      _M0L6_2atmpS3355 = _M0L3valS3356 + 1;
      _M0L3jj2S931->$0 = _M0L6_2atmpS3355;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3jj2S931);
    }
    break;
  }
  _M0L3ii2S933
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3ii2S933)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3ii2S933->$0 = 0;
  while (1) {
    int32_t _M0L3valS3357 = _M0L3ii2S933->$0;
    if (_M0L3valS3357 < _M0L7n__postS903) {
      int32_t _M0L3valS3358 = _M0L3ii2S933->$0;
      int32_t _M0L3valS3367;
      int32_t _M0L6_2atmpS3366;
      #line 1572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS904, _M0L3valS3358)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3359 = _M0L4varsS899->$3;
        int32_t _M0L3valS3360 = _M0L3ii2S933->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3364 = _M0L4varsS899->$3;
        int32_t _M0L3valS3365 = _M0L3ii2S933->$0;
        float _M0L6_2atmpS3362;
        float _M0L7a__postS3363;
        float _M0L6_2atmpS3361;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3362
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3364, _M0L3valS3365);
        _M0L7a__postS3363 = _M0L5paramS906->$1;
        _M0L6_2atmpS3361 = _M0L6_2atmpS3362 + _M0L7a__postS3363;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3359, _M0L3valS3360, _M0L6_2atmpS3361);
      }
      _M0L3valS3367 = _M0L3ii2S933->$0;
      _M0L6_2atmpS3366 = _M0L3valS3367 + 1;
      _M0L3ii2S933->$0 = _M0L6_2atmpS3366;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3ii2S933);
    }
    break;
  }
  _M0L2s2S935
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S935)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S935->$0 = 0;
  while (1) {
    int32_t _M0L3valS3368 = _M0L2s2S935->$0;
    int32_t _M0L6_2atmpS3369;
    #line 1579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3369 = _M0MPC15array5Array6lengthGfE(_M0L1wS915);
    if (_M0L3valS3368 < _M0L6_2atmpS3369) {
      int32_t _M0L3valS3372 = _M0L2s2S935->$0;
      float _M0L6_2atmpS3370;
      float _M0L6w__minS3371;
      int32_t _M0L3valS3377;
      float _M0L6_2atmpS3375;
      float _M0L6w__maxS3376;
      int32_t _M0L3valS3381;
      int32_t _M0L6_2atmpS3380;
      #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3370 = _M0MPC15array5Array2atGfE(_M0L1wS915, _M0L3valS3372);
      _M0L6w__minS3371 = _M0L5paramS906->$5;
      if (_M0L6_2atmpS3370 < _M0L6w__minS3371) {
        int32_t _M0L3valS3373 = _M0L2s2S935->$0;
        float _M0L6w__minS3374 = _M0L5paramS906->$5;
        #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS915, _M0L3valS3373, _M0L6w__minS3374);
      }
      _M0L3valS3377 = _M0L2s2S935->$0;
      #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3375 = _M0MPC15array5Array2atGfE(_M0L1wS915, _M0L3valS3377);
      _M0L6w__maxS3376 = _M0L5paramS906->$4;
      if (_M0L6_2atmpS3375 > _M0L6w__maxS3376) {
        int32_t _M0L3valS3378 = _M0L2s2S935->$0;
        float _M0L6w__maxS3379 = _M0L5paramS906->$4;
        #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS915, _M0L3valS3378, _M0L6w__maxS3379);
      }
      _M0L3valS3381 = _M0L2s2S935->$0;
      _M0L6_2atmpS3380 = _M0L3valS3381 + 1;
      _M0L2s2S935->$0 = _M0L6_2atmpS3380;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S935);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS895,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS868,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS870,
  struct _M0TPB5ArrayGiE* _M0L6colptrS890,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS883,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS875,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS872,
  float _M0L6t__nowS866,
  float _M0L2dtS876
) {
  int32_t _M0L6n__preS867;
  int32_t _M0L7n__postS869;
  float _M0L6tau__xS3271;
  float _M0L11inv__tau__xS871;
  float _M0L6tau__yS3270;
  float _M0L11inv__tau__yS873;
  struct _M0TPB8MutLocalGiE* _M0L1jS874;
  struct _M0TPB8MutLocalGiE* _M0L1iS878;
  float _M0L4a__xS3267;
  float _M0L6tau__xS3269;
  float _M0L6_2atmpS3268;
  float _M0L7coef__xS880;
  float _M0L4a__yS3264;
  float _M0L6tau__yS3266;
  float _M0L6_2atmpS3265;
  float _M0L7coef__yS881;
  #line 1296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS867 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS868);
  #line 1309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS869 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS870);
  _M0L6tau__xS3271 = _M0L5paramS872->$2;
  _M0L11inv__tau__xS871 = 0x1p+0f / _M0L6tau__xS3271;
  _M0L6tau__yS3270 = _M0L5paramS872->$3;
  _M0L11inv__tau__yS873 = 0x1p+0f / _M0L6tau__yS3270;
  _M0L1jS874
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS874)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS874->$0 = 0;
  while (1) {
    int32_t _M0L3valS3141 = _M0L1jS874->$0;
    if (_M0L3valS3141 < _M0L6n__preS867) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3142 = _M0L4varsS875->$0;
      int32_t _M0L3valS3143 = _M0L1jS874->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3152 = _M0L4varsS875->$0;
      int32_t _M0L3valS3153 = _M0L1jS874->$0;
      float _M0L6_2atmpS3145;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3150;
      int32_t _M0L3valS3151;
      float _M0L6_2atmpS3149;
      float _M0L6_2atmpS3148;
      float _M0L6_2atmpS3147;
      float _M0L6_2atmpS3146;
      float _M0L6_2atmpS3144;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3154;
      int32_t _M0L3valS3155;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3164;
      int32_t _M0L3valS3165;
      float _M0L6_2atmpS3157;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3162;
      int32_t _M0L3valS3163;
      float _M0L6_2atmpS3161;
      float _M0L6_2atmpS3160;
      float _M0L6_2atmpS3159;
      float _M0L6_2atmpS3158;
      float _M0L6_2atmpS3156;
      int32_t _M0L3valS3166;
      int32_t _M0L3valS3180;
      int32_t _M0L6_2atmpS3179;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3145
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3152, _M0L3valS3153);
      _M0L5tr__xS3150 = _M0L4varsS875->$0;
      _M0L3valS3151 = _M0L1jS874->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3149
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3150, _M0L3valS3151);
      _M0L6_2atmpS3148 = -_M0L6_2atmpS3149;
      _M0L6_2atmpS3147 = _M0L2dtS876 * _M0L6_2atmpS3148;
      _M0L6_2atmpS3146 = _M0L6_2atmpS3147 * _M0L11inv__tau__xS871;
      _M0L6_2atmpS3144 = _M0L6_2atmpS3145 + _M0L6_2atmpS3146;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3142, _M0L3valS3143, _M0L6_2atmpS3144);
      _M0L5tr__yS3154 = _M0L4varsS875->$1;
      _M0L3valS3155 = _M0L1jS874->$0;
      _M0L5tr__yS3164 = _M0L4varsS875->$1;
      _M0L3valS3165 = _M0L1jS874->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3157
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3164, _M0L3valS3165);
      _M0L5tr__yS3162 = _M0L4varsS875->$1;
      _M0L3valS3163 = _M0L1jS874->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3161
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3162, _M0L3valS3163);
      _M0L6_2atmpS3160 = -_M0L6_2atmpS3161;
      _M0L6_2atmpS3159 = _M0L2dtS876 * _M0L6_2atmpS3160;
      _M0L6_2atmpS3158 = _M0L6_2atmpS3159 * _M0L11inv__tau__yS873;
      _M0L6_2atmpS3156 = _M0L6_2atmpS3157 + _M0L6_2atmpS3158;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__yS3154, _M0L3valS3155, _M0L6_2atmpS3156);
      _M0L3valS3166 = _M0L1jS874->$0;
      #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS868, _M0L3valS3166)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3167 = _M0L4varsS875->$0;
        int32_t _M0L3valS3168 = _M0L1jS874->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3171 = _M0L4varsS875->$0;
        int32_t _M0L3valS3172 = _M0L1jS874->$0;
        float _M0L6_2atmpS3170;
        float _M0L6_2atmpS3169;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3173;
        int32_t _M0L3valS3174;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3177;
        int32_t _M0L3valS3178;
        float _M0L6_2atmpS3176;
        float _M0L6_2atmpS3175;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3170
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3171, _M0L3valS3172);
        _M0L6_2atmpS3169 = _M0L6_2atmpS3170 + 0x1p+0f;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3167, _M0L3valS3168, _M0L6_2atmpS3169);
        _M0L5tr__yS3173 = _M0L4varsS875->$1;
        _M0L3valS3174 = _M0L1jS874->$0;
        _M0L5tr__yS3177 = _M0L4varsS875->$1;
        _M0L3valS3178 = _M0L1jS874->$0;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3176
        = _M0MPC15array5Array2atGfE(_M0L5tr__yS3177, _M0L3valS3178);
        _M0L6_2atmpS3175 = _M0L6_2atmpS3176 + 0x1p+0f;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__yS3173, _M0L3valS3174, _M0L6_2atmpS3175);
      }
      _M0L3valS3180 = _M0L1jS874->$0;
      _M0L6_2atmpS3179 = _M0L3valS3180 + 1;
      _M0L1jS874->$0 = _M0L6_2atmpS3179;
      continue;
    }
    break;
  }
  _M0L1iS878
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS878)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS878->$0 = 0;
  while (1) {
    int32_t _M0L3valS3181 = _M0L1iS878->$0;
    if (_M0L3valS3181 < _M0L7n__postS869) {
      struct _M0TPB5ArrayGfE* _M0L5to__xS3182 = _M0L4varsS875->$2;
      int32_t _M0L3valS3183 = _M0L1iS878->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3192 = _M0L4varsS875->$2;
      int32_t _M0L3valS3193 = _M0L1iS878->$0;
      float _M0L6_2atmpS3185;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3190;
      int32_t _M0L3valS3191;
      float _M0L6_2atmpS3189;
      float _M0L6_2atmpS3188;
      float _M0L6_2atmpS3187;
      float _M0L6_2atmpS3186;
      float _M0L6_2atmpS3184;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3194;
      int32_t _M0L3valS3195;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3204;
      int32_t _M0L3valS3205;
      float _M0L6_2atmpS3197;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3202;
      int32_t _M0L3valS3203;
      float _M0L6_2atmpS3201;
      float _M0L6_2atmpS3200;
      float _M0L6_2atmpS3199;
      float _M0L6_2atmpS3198;
      float _M0L6_2atmpS3196;
      int32_t _M0L3valS3206;
      int32_t _M0L3valS3220;
      int32_t _M0L6_2atmpS3219;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3185
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3192, _M0L3valS3193);
      _M0L5to__xS3190 = _M0L4varsS875->$2;
      _M0L3valS3191 = _M0L1iS878->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3189
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3190, _M0L3valS3191);
      _M0L6_2atmpS3188 = -_M0L6_2atmpS3189;
      _M0L6_2atmpS3187 = _M0L2dtS876 * _M0L6_2atmpS3188;
      _M0L6_2atmpS3186 = _M0L6_2atmpS3187 * _M0L11inv__tau__xS871;
      _M0L6_2atmpS3184 = _M0L6_2atmpS3185 + _M0L6_2atmpS3186;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__xS3182, _M0L3valS3183, _M0L6_2atmpS3184);
      _M0L5to__yS3194 = _M0L4varsS875->$3;
      _M0L3valS3195 = _M0L1iS878->$0;
      _M0L5to__yS3204 = _M0L4varsS875->$3;
      _M0L3valS3205 = _M0L1iS878->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3197
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3204, _M0L3valS3205);
      _M0L5to__yS3202 = _M0L4varsS875->$3;
      _M0L3valS3203 = _M0L1iS878->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3201
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3202, _M0L3valS3203);
      _M0L6_2atmpS3200 = -_M0L6_2atmpS3201;
      _M0L6_2atmpS3199 = _M0L2dtS876 * _M0L6_2atmpS3200;
      _M0L6_2atmpS3198 = _M0L6_2atmpS3199 * _M0L11inv__tau__yS873;
      _M0L6_2atmpS3196 = _M0L6_2atmpS3197 + _M0L6_2atmpS3198;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3194, _M0L3valS3195, _M0L6_2atmpS3196);
      _M0L3valS3206 = _M0L1iS878->$0;
      #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS870, _M0L3valS3206)) {
        struct _M0TPB5ArrayGfE* _M0L5to__xS3207 = _M0L4varsS875->$2;
        int32_t _M0L3valS3208 = _M0L1iS878->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__xS3211 = _M0L4varsS875->$2;
        int32_t _M0L3valS3212 = _M0L1iS878->$0;
        float _M0L6_2atmpS3210;
        float _M0L6_2atmpS3209;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3213;
        int32_t _M0L3valS3214;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3217;
        int32_t _M0L3valS3218;
        float _M0L6_2atmpS3216;
        float _M0L6_2atmpS3215;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3210
        = _M0MPC15array5Array2atGfE(_M0L5to__xS3211, _M0L3valS3212);
        _M0L6_2atmpS3209 = _M0L6_2atmpS3210 + 0x1p+0f;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__xS3207, _M0L3valS3208, _M0L6_2atmpS3209);
        _M0L5to__yS3213 = _M0L4varsS875->$3;
        _M0L3valS3214 = _M0L1iS878->$0;
        _M0L5to__yS3217 = _M0L4varsS875->$3;
        _M0L3valS3218 = _M0L1iS878->$0;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3216
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3217, _M0L3valS3218);
        _M0L6_2atmpS3215 = _M0L6_2atmpS3216 + 0x1p+0f;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3213, _M0L3valS3214, _M0L6_2atmpS3215);
      }
      _M0L3valS3220 = _M0L1iS878->$0;
      _M0L6_2atmpS3219 = _M0L3valS3220 + 1;
      _M0L1iS878->$0 = _M0L6_2atmpS3219;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS878);
    }
    break;
  }
  _M0L4a__xS3267 = _M0L5paramS872->$0;
  _M0L6tau__xS3269 = _M0L5paramS872->$2;
  _M0L6_2atmpS3268 = 0x1p+1f * _M0L6tau__xS3269;
  _M0L7coef__xS880 = _M0L4a__xS3267 / _M0L6_2atmpS3268;
  _M0L4a__yS3264 = _M0L5paramS872->$1;
  _M0L6tau__yS3266 = _M0L5paramS872->$3;
  _M0L6_2atmpS3265 = 0x1p+1f * _M0L6tau__yS3266;
  _M0L7coef__yS881 = _M0L4a__yS3264 / _M0L6_2atmpS3265;
  _M0L1jS874->$0 = 0;
  while (1) {
    int32_t _M0L3valS3221 = _M0L1jS874->$0;
    if (_M0L3valS3221 < _M0L6n__preS867) {
      int32_t _M0L3valS3263 = _M0L1jS874->$0;
      int32_t _M0L5startS882;
      int32_t _M0L3valS3262;
      int32_t _M0L6_2atmpS3261;
      int32_t _M0L3endS884;
      int32_t _M0L3valS3260;
      int32_t _M0L10pre__firedS885;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3258;
      int32_t _M0L3valS3259;
      float _M0L8tr__x__jS886;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3256;
      int32_t _M0L3valS3257;
      float _M0L8tr__y__jS887;
      struct _M0TPB8MutLocalGiE* _M0L1sS888;
      int32_t _M0L3valS3255;
      int32_t _M0L6_2atmpS3254;
      #line 1346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS882
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS883, _M0L3valS3263);
      _M0L3valS3262 = _M0L1jS874->$0;
      _M0L6_2atmpS3261 = _M0L3valS3262 + 1;
      #line 1347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS884
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS883, _M0L6_2atmpS3261);
      _M0L3valS3260 = _M0L1jS874->$0;
      #line 1348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS885
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS868, _M0L3valS3260);
      _M0L5tr__xS3258 = _M0L4varsS875->$0;
      _M0L3valS3259 = _M0L1jS874->$0;
      #line 1349 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__x__jS886
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3258, _M0L3valS3259);
      _M0L5tr__yS3256 = _M0L4varsS875->$1;
      _M0L3valS3257 = _M0L1jS874->$0;
      #line 1350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__y__jS887
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3256, _M0L3valS3257);
      _M0L1sS888
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS888)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS888->$0 = _M0L5startS882;
      while (1) {
        int32_t _M0L3valS3222 = _M0L1sS888->$0;
        if (_M0L3valS3222 < _M0L3endS884) {
          int32_t _M0L3valS3253 = _M0L1sS888->$0;
          int32_t _M0L9post__idxS889;
          int32_t _M0L11post__firedS891;
          struct _M0TPB5ArrayGfE* _M0L5to__xS3252;
          float _M0L8to__x__iS892;
          struct _M0TPB5ArrayGfE* _M0L5to__yS3251;
          float _M0L8to__y__iS893;
          int32_t _M0L3valS3241;
          float _M0L6_2atmpS3239;
          float _M0L6w__minS3240;
          int32_t _M0L3valS3246;
          float _M0L6_2atmpS3244;
          float _M0L6w__maxS3245;
          int32_t _M0L3valS3250;
          int32_t _M0L6_2atmpS3249;
          #line 1353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS889
          = _M0MPC15array5Array2atGiE(_M0L6colptrS890, _M0L3valS3253);
          #line 1354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS891
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS870, _M0L9post__idxS889);
          _M0L5to__xS3252 = _M0L4varsS875->$2;
          #line 1355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__x__iS892
          = _M0MPC15array5Array2atGfE(_M0L5to__xS3252, _M0L9post__idxS889);
          _M0L5to__yS3251 = _M0L4varsS875->$3;
          #line 1356 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__y__iS893
          = _M0MPC15array5Array2atGfE(_M0L5to__yS3251, _M0L9post__idxS889);
          if (_M0L10pre__firedS885) {
            float _M0L10alpha__preS3229 = _M0L5paramS872->$4;
            float _M0L6_2atmpS3230 = _M0L7coef__xS880 * _M0L8to__x__iS892;
            float _M0L6_2atmpS3227 = _M0L10alpha__preS3229 + _M0L6_2atmpS3230;
            float _M0L6_2atmpS3228 = _M0L7coef__yS881 * _M0L8to__y__iS893;
            float _M0L2dwS894 = _M0L6_2atmpS3227 - _M0L6_2atmpS3228;
            int32_t _M0L3valS3223 = _M0L1sS888->$0;
            int32_t _M0L3valS3226 = _M0L1sS888->$0;
            float _M0L6_2atmpS3225;
            float _M0L6_2atmpS3224;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3225
            = _M0MPC15array5Array2atGfE(_M0L1wS895, _M0L3valS3226);
            _M0L6_2atmpS3224 = _M0L6_2atmpS3225 + _M0L2dwS894;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS895, _M0L3valS3223, _M0L6_2atmpS3224);
          }
          if (_M0L11post__firedS891) {
            float _M0L11alpha__postS3237 = _M0L5paramS872->$5;
            float _M0L6_2atmpS3238 = _M0L7coef__xS880 * _M0L8tr__x__jS886;
            float _M0L6_2atmpS3235 =
              _M0L11alpha__postS3237 + _M0L6_2atmpS3238;
            float _M0L6_2atmpS3236 = _M0L7coef__yS881 * _M0L8tr__y__jS887;
            float _M0L2dwS896 = _M0L6_2atmpS3235 - _M0L6_2atmpS3236;
            int32_t _M0L3valS3231 = _M0L1sS888->$0;
            int32_t _M0L3valS3234 = _M0L1sS888->$0;
            float _M0L6_2atmpS3233;
            float _M0L6_2atmpS3232;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3233
            = _M0MPC15array5Array2atGfE(_M0L1wS895, _M0L3valS3234);
            _M0L6_2atmpS3232 = _M0L6_2atmpS3233 + _M0L2dwS896;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS895, _M0L3valS3231, _M0L6_2atmpS3232);
          }
          _M0L3valS3241 = _M0L1sS888->$0;
          #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3239
          = _M0MPC15array5Array2atGfE(_M0L1wS895, _M0L3valS3241);
          _M0L6w__minS3240 = _M0L5paramS872->$7;
          if (_M0L6_2atmpS3239 < _M0L6w__minS3240) {
            int32_t _M0L3valS3242 = _M0L1sS888->$0;
            float _M0L6w__minS3243 = _M0L5paramS872->$7;
            #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS895, _M0L3valS3242, _M0L6w__minS3243);
          }
          _M0L3valS3246 = _M0L1sS888->$0;
          #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3244
          = _M0MPC15array5Array2atGfE(_M0L1wS895, _M0L3valS3246);
          _M0L6w__maxS3245 = _M0L5paramS872->$6;
          if (_M0L6_2atmpS3244 > _M0L6w__maxS3245) {
            int32_t _M0L3valS3247 = _M0L1sS888->$0;
            float _M0L6w__maxS3248 = _M0L5paramS872->$6;
            #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS895, _M0L3valS3247, _M0L6w__maxS3248);
          }
          _M0L3valS3250 = _M0L1sS888->$0;
          _M0L6_2atmpS3249 = _M0L3valS3250 + 1;
          _M0L1sS888->$0 = _M0L6_2atmpS3249;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS888);
        }
        break;
      }
      _M0L3valS3255 = _M0L1jS874->$0;
      _M0L6_2atmpS3254 = _M0L3valS3255 + 1;
      _M0L1jS874->$0 = _M0L6_2atmpS3254;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS874);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE* _M0L1wS863,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS840,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS842,
  struct _M0TPB5ArrayGiE* _M0L6colptrS860,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS854,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS848,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS845,
  float _M0L6t__nowS849,
  float _M0L2dtS844
) {
  int32_t _M0L6n__preS839;
  int32_t _M0L7n__postS841;
  float _M0L6_2atmpS3139;
  float _M0L8tau__preS3140;
  float _M0L6_2atmpS3138;
  float _M0L10decay__preS843;
  float _M0L6_2atmpS3136;
  float _M0L9tau__postS3137;
  float _M0L6_2atmpS3135;
  float _M0L11decay__postS846;
  struct _M0TPB8MutLocalGiE* _M0L1jS847;
  struct _M0TPB8MutLocalGiE* _M0L1iS851;
  #line 1080 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1091 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS839 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS840);
  #line 1092 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS841 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS842);
  _M0L6_2atmpS3139 = -_M0L2dtS844;
  _M0L8tau__preS3140 = _M0L5paramS845->$5;
  _M0L6_2atmpS3138 = _M0L6_2atmpS3139 / _M0L8tau__preS3140;
  #line 1093 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS843 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3138);
  _M0L6_2atmpS3136 = -_M0L2dtS844;
  _M0L9tau__postS3137 = _M0L5paramS845->$6;
  _M0L6_2atmpS3135 = _M0L6_2atmpS3136 / _M0L9tau__postS3137;
  #line 1094 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS846 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3135);
  _M0L1jS847
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS847)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS847->$0 = 0;
  while (1) {
    int32_t _M0L3valS3055 = _M0L1jS847->$0;
    if (_M0L3valS3055 < _M0L6n__preS839) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3056 = _M0L4varsS848->$0;
      int32_t _M0L3valS3057 = _M0L1jS847->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3060 = _M0L4varsS848->$0;
      int32_t _M0L3valS3061 = _M0L1jS847->$0;
      float _M0L6_2atmpS3059;
      float _M0L6_2atmpS3058;
      int32_t _M0L3valS3062;
      int32_t _M0L3valS3072;
      int32_t _M0L6_2atmpS3071;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3059
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3060, _M0L3valS3061);
      _M0L6_2atmpS3058 = _M0L6_2atmpS3059 * _M0L10decay__preS843;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3056, _M0L3valS3057, _M0L6_2atmpS3058);
      _M0L3valS3062 = _M0L1jS847->$0;
      #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS840, _M0L3valS3062)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3063 = _M0L4varsS848->$0;
        int32_t _M0L3valS3064 = _M0L1jS847->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3067 = _M0L4varsS848->$0;
        int32_t _M0L3valS3068 = _M0L1jS847->$0;
        float _M0L6_2atmpS3066;
        float _M0L6_2atmpS3065;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3069;
        int32_t _M0L3valS3070;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3066
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3067, _M0L3valS3068);
        _M0L6_2atmpS3065 = _M0L6_2atmpS3066 + 0x1p+0f;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3063, _M0L3valS3064, _M0L6_2atmpS3065);
        _M0L9last__preS3069 = _M0L4varsS848->$2;
        _M0L3valS3070 = _M0L1jS847->$0;
        #line 1101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3069, _M0L3valS3070, _M0L6t__nowS849);
      }
      _M0L3valS3072 = _M0L1jS847->$0;
      _M0L6_2atmpS3071 = _M0L3valS3072 + 1;
      _M0L1jS847->$0 = _M0L6_2atmpS3071;
      continue;
    }
    break;
  }
  _M0L1iS851
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS851)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS851->$0 = 0;
  while (1) {
    int32_t _M0L3valS3073 = _M0L1iS851->$0;
    if (_M0L3valS3073 < _M0L7n__postS841) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3074 = _M0L4varsS848->$1;
      int32_t _M0L3valS3075 = _M0L1iS851->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3078 = _M0L4varsS848->$1;
      int32_t _M0L3valS3079 = _M0L1iS851->$0;
      float _M0L6_2atmpS3077;
      float _M0L6_2atmpS3076;
      int32_t _M0L3valS3080;
      int32_t _M0L3valS3090;
      int32_t _M0L6_2atmpS3089;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3077
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3078, _M0L3valS3079);
      _M0L6_2atmpS3076 = _M0L6_2atmpS3077 * _M0L11decay__postS846;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3074, _M0L3valS3075, _M0L6_2atmpS3076);
      _M0L3valS3080 = _M0L1iS851->$0;
      #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS842, _M0L3valS3080)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3081 = _M0L4varsS848->$1;
        int32_t _M0L3valS3082 = _M0L1iS851->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3085 = _M0L4varsS848->$1;
        int32_t _M0L3valS3086 = _M0L1iS851->$0;
        float _M0L6_2atmpS3084;
        float _M0L6_2atmpS3083;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3087;
        int32_t _M0L3valS3088;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3084
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3085, _M0L3valS3086);
        _M0L6_2atmpS3083 = _M0L6_2atmpS3084 + 0x1p+0f;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3081, _M0L3valS3082, _M0L6_2atmpS3083);
        _M0L10last__postS3087 = _M0L4varsS848->$3;
        _M0L3valS3088 = _M0L1iS851->$0;
        #line 1110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3087, _M0L3valS3088, _M0L6t__nowS849);
      }
      _M0L3valS3090 = _M0L1iS851->$0;
      _M0L6_2atmpS3089 = _M0L3valS3090 + 1;
      _M0L1iS851->$0 = _M0L6_2atmpS3089;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS851);
    }
    break;
  }
  _M0L1jS847->$0 = 0;
  while (1) {
    int32_t _M0L3valS3091 = _M0L1jS847->$0;
    if (_M0L3valS3091 < _M0L6n__preS839) {
      int32_t _M0L3valS3134 = _M0L1jS847->$0;
      int32_t _M0L5startS853;
      int32_t _M0L3valS3133;
      int32_t _M0L6_2atmpS3132;
      int32_t _M0L3endS855;
      int32_t _M0L3valS3131;
      int32_t _M0L10pre__firedS856;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3129;
      int32_t _M0L3valS3130;
      float _M0L7tpre__jS857;
      struct _M0TPB8MutLocalGiE* _M0L1sS858;
      int32_t _M0L3valS3128;
      int32_t _M0L6_2atmpS3127;
      #line 1120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS853
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS854, _M0L3valS3134);
      _M0L3valS3133 = _M0L1jS847->$0;
      _M0L6_2atmpS3132 = _M0L3valS3133 + 1;
      #line 1121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS855
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS854, _M0L6_2atmpS3132);
      _M0L3valS3131 = _M0L1jS847->$0;
      #line 1122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS856
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS840, _M0L3valS3131);
      _M0L4tpreS3129 = _M0L4varsS848->$0;
      _M0L3valS3130 = _M0L1jS847->$0;
      #line 1123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L7tpre__jS857
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3129, _M0L3valS3130);
      _M0L1sS858
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS858)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS858->$0 = _M0L5startS853;
      while (1) {
        int32_t _M0L3valS3092 = _M0L1sS858->$0;
        if (_M0L3valS3092 < _M0L3endS855) {
          int32_t _M0L3valS3126 = _M0L1sS858->$0;
          int32_t _M0L9post__idxS859;
          int32_t _M0L11post__firedS861;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3125;
          float _M0L8tpost__iS862;
          int32_t _M0L3valS3115;
          float _M0L6_2atmpS3113;
          float _M0L6w__minS3114;
          int32_t _M0L3valS3120;
          float _M0L6_2atmpS3118;
          float _M0L6w__maxS3119;
          int32_t _M0L3valS3124;
          int32_t _M0L6_2atmpS3123;
          #line 1126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS859
          = _M0MPC15array5Array2atGiE(_M0L6colptrS860, _M0L3valS3126);
          #line 1127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS861
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS842, _M0L9post__idxS859);
          _M0L5tpostS3125 = _M0L4varsS848->$1;
          #line 1128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8tpost__iS862
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3125, _M0L9post__idxS859);
          if (_M0L10pre__firedS856) {
            int32_t _M0L3valS3093 = _M0L1sS858->$0;
            int32_t _M0L3valS3102 = _M0L1sS858->$0;
            float _M0L6_2atmpS3095;
            float _M0L3etaS3097;
            float _M0L5kappaS3101;
            float _M0L6_2atmpS3099;
            float _M0L5alphaS3100;
            float _M0L6_2atmpS3098;
            float _M0L6_2atmpS3096;
            float _M0L6_2atmpS3094;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3095
            = _M0MPC15array5Array2atGfE(_M0L1wS863, _M0L3valS3102);
            _M0L3etaS3097 = _M0L5paramS845->$0;
            _M0L5kappaS3101 = _M0L5paramS845->$3;
            _M0L6_2atmpS3099 = _M0L5kappaS3101 * _M0L8tpost__iS862;
            _M0L5alphaS3100 = _M0L5paramS845->$1;
            _M0L6_2atmpS3098 = _M0L6_2atmpS3099 + _M0L5alphaS3100;
            _M0L6_2atmpS3096 = _M0L3etaS3097 * _M0L6_2atmpS3098;
            _M0L6_2atmpS3094 = _M0L6_2atmpS3095 + _M0L6_2atmpS3096;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS863, _M0L3valS3093, _M0L6_2atmpS3094);
          }
          if (_M0L11post__firedS861) {
            int32_t _M0L3valS3103 = _M0L1sS858->$0;
            int32_t _M0L3valS3112 = _M0L1sS858->$0;
            float _M0L6_2atmpS3105;
            float _M0L3etaS3107;
            float _M0L5gammaS3111;
            float _M0L6_2atmpS3109;
            float _M0L4betaS3110;
            float _M0L6_2atmpS3108;
            float _M0L6_2atmpS3106;
            float _M0L6_2atmpS3104;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3105
            = _M0MPC15array5Array2atGfE(_M0L1wS863, _M0L3valS3112);
            _M0L3etaS3107 = _M0L5paramS845->$0;
            _M0L5gammaS3111 = _M0L5paramS845->$4;
            _M0L6_2atmpS3109 = _M0L5gammaS3111 * _M0L7tpre__jS857;
            _M0L4betaS3110 = _M0L5paramS845->$2;
            _M0L6_2atmpS3108 = _M0L6_2atmpS3109 + _M0L4betaS3110;
            _M0L6_2atmpS3106 = _M0L3etaS3107 * _M0L6_2atmpS3108;
            _M0L6_2atmpS3104 = _M0L6_2atmpS3105 + _M0L6_2atmpS3106;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS863, _M0L3valS3103, _M0L6_2atmpS3104);
          }
          _M0L3valS3115 = _M0L1sS858->$0;
          #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3113
          = _M0MPC15array5Array2atGfE(_M0L1wS863, _M0L3valS3115);
          _M0L6w__minS3114 = _M0L5paramS845->$8;
          if (_M0L6_2atmpS3113 < _M0L6w__minS3114) {
            int32_t _M0L3valS3116 = _M0L1sS858->$0;
            float _M0L6w__minS3117 = _M0L5paramS845->$8;
            #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS863, _M0L3valS3116, _M0L6w__minS3117);
          }
          _M0L3valS3120 = _M0L1sS858->$0;
          #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3118
          = _M0MPC15array5Array2atGfE(_M0L1wS863, _M0L3valS3120);
          _M0L6w__maxS3119 = _M0L5paramS845->$7;
          if (_M0L6_2atmpS3118 > _M0L6w__maxS3119) {
            int32_t _M0L3valS3121 = _M0L1sS858->$0;
            float _M0L6w__maxS3122 = _M0L5paramS845->$7;
            #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS863, _M0L3valS3121, _M0L6w__maxS3122);
          }
          _M0L3valS3124 = _M0L1sS858->$0;
          _M0L6_2atmpS3123 = _M0L3valS3124 + 1;
          _M0L1sS858->$0 = _M0L6_2atmpS3123;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS858);
        }
        break;
      }
      _M0L3valS3128 = _M0L1jS847->$0;
      _M0L6_2atmpS3127 = _M0L3valS3128 + 1;
      _M0L1jS847->$0 = _M0L6_2atmpS3127;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS847);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS836,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS816,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS818,
  struct _M0TPB5ArrayGiE* _M0L6colptrS833,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS829,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS814,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS821,
  float _M0L6t__nowS824,
  float _M0L2dtS820
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2972;
  int32_t _M0L6_2atmpS2971;
  int32_t _if__result_5283;
  int32_t _M0L6n__preS815;
  int32_t _M0L7n__postS817;
  float _M0L6_2atmpS3053;
  float _M0L8tau__preS3054;
  float _M0L6_2atmpS3052;
  float _M0L10decay__preS819;
  float _M0L6_2atmpS3050;
  float _M0L9tau__postS3051;
  float _M0L6_2atmpS3049;
  float _M0L11decay__postS822;
  struct _M0TPB8MutLocalGiE* _M0L1jS823;
  struct _M0TPB8MutLocalGiE* _M0L1iS826;
  #line 905 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS2972 = _M0L4varsS814->$4;
  #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2971 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2972);
  if (_M0L6_2atmpS2971 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2970 = _M0L4varsS814->$4;
    int32_t _M0L6_2atmpS2969;
    #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS2969 = _M0MPC15array5Array2atGbE(_M0L6activeS2970, 0);
    _if__result_5283 = !_M0L6_2atmpS2969;
  } else {
    _if__result_5283 = 0;
  }
  if (_if__result_5283) {
    return 0;
  }
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS815 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS816);
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS817 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS818);
  _M0L6_2atmpS3053 = -_M0L2dtS820;
  _M0L8tau__preS3054 = _M0L5paramS821->$2;
  _M0L6_2atmpS3052 = _M0L6_2atmpS3053 / _M0L8tau__preS3054;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS819 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3052);
  _M0L6_2atmpS3050 = -_M0L2dtS820;
  _M0L9tau__postS3051 = _M0L5paramS821->$3;
  _M0L6_2atmpS3049 = _M0L6_2atmpS3050 / _M0L9tau__postS3051;
  #line 924 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS822 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3049);
  _M0L1jS823
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS823)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS823->$0 = 0;
  while (1) {
    int32_t _M0L3valS2973 = _M0L1jS823->$0;
    if (_M0L3valS2973 < _M0L6n__preS815) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS2974 = _M0L4varsS814->$0;
      int32_t _M0L3valS2975 = _M0L1jS823->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS2978 = _M0L4varsS814->$0;
      int32_t _M0L3valS2979 = _M0L1jS823->$0;
      float _M0L6_2atmpS2977;
      float _M0L6_2atmpS2976;
      int32_t _M0L3valS2980;
      int32_t _M0L3valS2991;
      int32_t _M0L6_2atmpS2990;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2977
      = _M0MPC15array5Array2atGfE(_M0L4tpreS2978, _M0L3valS2979);
      _M0L6_2atmpS2976 = _M0L6_2atmpS2977 * _M0L10decay__preS819;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS2974, _M0L3valS2975, _M0L6_2atmpS2976);
      _M0L3valS2980 = _M0L1jS823->$0;
      #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS816, _M0L3valS2980)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS2981 = _M0L4varsS814->$0;
        int32_t _M0L3valS2982 = _M0L1jS823->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS2986 = _M0L4varsS814->$0;
        int32_t _M0L3valS2987 = _M0L1jS823->$0;
        float _M0L6_2atmpS2984;
        float _M0L6a__preS2985;
        float _M0L6_2atmpS2983;
        struct _M0TPB5ArrayGfE* _M0L9last__preS2988;
        int32_t _M0L3valS2989;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2984
        = _M0MPC15array5Array2atGfE(_M0L4tpreS2986, _M0L3valS2987);
        _M0L6a__preS2985 = _M0L5paramS821->$0;
        _M0L6_2atmpS2983 = _M0L6_2atmpS2984 + _M0L6a__preS2985;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS2981, _M0L3valS2982, _M0L6_2atmpS2983);
        _M0L9last__preS2988 = _M0L4varsS814->$2;
        _M0L3valS2989 = _M0L1jS823->$0;
        #line 930 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS2988, _M0L3valS2989, _M0L6t__nowS824);
      }
      _M0L3valS2991 = _M0L1jS823->$0;
      _M0L6_2atmpS2990 = _M0L3valS2991 + 1;
      _M0L1jS823->$0 = _M0L6_2atmpS2990;
      continue;
    }
    break;
  }
  _M0L1iS826
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS826)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS826->$0 = 0;
  while (1) {
    int32_t _M0L3valS2992 = _M0L1iS826->$0;
    if (_M0L3valS2992 < _M0L7n__postS817) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS2993 = _M0L4varsS814->$1;
      int32_t _M0L3valS2994 = _M0L1iS826->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS2997 = _M0L4varsS814->$1;
      int32_t _M0L3valS2998 = _M0L1iS826->$0;
      float _M0L6_2atmpS2996;
      float _M0L6_2atmpS2995;
      int32_t _M0L3valS2999;
      int32_t _M0L3valS3010;
      int32_t _M0L6_2atmpS3009;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2996
      = _M0MPC15array5Array2atGfE(_M0L5tpostS2997, _M0L3valS2998);
      _M0L6_2atmpS2995 = _M0L6_2atmpS2996 * _M0L11decay__postS822;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS2993, _M0L3valS2994, _M0L6_2atmpS2995);
      _M0L3valS2999 = _M0L1iS826->$0;
      #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS818, _M0L3valS2999)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3000 = _M0L4varsS814->$1;
        int32_t _M0L3valS3001 = _M0L1iS826->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3005 = _M0L4varsS814->$1;
        int32_t _M0L3valS3006 = _M0L1iS826->$0;
        float _M0L6_2atmpS3003;
        float _M0L7a__postS3004;
        float _M0L6_2atmpS3002;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3007;
        int32_t _M0L3valS3008;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3003
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3005, _M0L3valS3006);
        _M0L7a__postS3004 = _M0L5paramS821->$1;
        _M0L6_2atmpS3002 = _M0L6_2atmpS3003 + _M0L7a__postS3004;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3000, _M0L3valS3001, _M0L6_2atmpS3002);
        _M0L10last__postS3007 = _M0L4varsS814->$3;
        _M0L3valS3008 = _M0L1iS826->$0;
        #line 939 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3007, _M0L3valS3008, _M0L6t__nowS824);
      }
      _M0L3valS3010 = _M0L1iS826->$0;
      _M0L6_2atmpS3009 = _M0L3valS3010 + 1;
      _M0L1iS826->$0 = _M0L6_2atmpS3009;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS826);
    }
    break;
  }
  _M0L1jS823->$0 = 0;
  while (1) {
    int32_t _M0L3valS3011 = _M0L1jS823->$0;
    if (_M0L3valS3011 < _M0L6n__preS815) {
      int32_t _M0L3valS3048 = _M0L1jS823->$0;
      int32_t _M0L5startS828;
      int32_t _M0L3valS3047;
      int32_t _M0L6_2atmpS3046;
      int32_t _M0L3endS830;
      struct _M0TPB8MutLocalGiE* _M0L1sS831;
      int32_t _M0L3valS3045;
      int32_t _M0L6_2atmpS3044;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS828
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS829, _M0L3valS3048);
      _M0L3valS3047 = _M0L1jS823->$0;
      _M0L6_2atmpS3046 = _M0L3valS3047 + 1;
      #line 948 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS830
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS829, _M0L6_2atmpS3046);
      _M0L1sS831
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS831)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS831->$0 = _M0L5startS828;
      while (1) {
        int32_t _M0L3valS3012 = _M0L1sS831->$0;
        if (_M0L3valS3012 < _M0L3endS830) {
          int32_t _M0L3valS3043 = _M0L1sS831->$0;
          int32_t _M0L9post__idxS832;
          int32_t _M0L3valS3042;
          int32_t _M0L10pre__firedS834;
          int32_t _M0L11post__firedS835;
          int32_t _M0L3valS3032;
          float _M0L6_2atmpS3030;
          float _M0L6w__minS3031;
          int32_t _M0L3valS3037;
          float _M0L6_2atmpS3035;
          float _M0L6w__maxS3036;
          int32_t _M0L3valS3041;
          int32_t _M0L6_2atmpS3040;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS832
          = _M0MPC15array5Array2atGiE(_M0L6colptrS833, _M0L3valS3043);
          _M0L3valS3042 = _M0L1jS823->$0;
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS834
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS816, _M0L3valS3042);
          #line 953 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS835
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS818, _M0L9post__idxS832);
          if (_M0L10pre__firedS834) {
            int32_t _M0L3valS3013 = _M0L1sS831->$0;
            int32_t _M0L3valS3020 = _M0L1sS831->$0;
            float _M0L6_2atmpS3015;
            float _M0L7a__postS3017;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3019;
            float _M0L6_2atmpS3018;
            float _M0L6_2atmpS3016;
            float _M0L6_2atmpS3014;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3015
            = _M0MPC15array5Array2atGfE(_M0L1wS836, _M0L3valS3020);
            _M0L7a__postS3017 = _M0L5paramS821->$1;
            _M0L5tpostS3019 = _M0L4varsS814->$1;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3018
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3019, _M0L9post__idxS832);
            _M0L6_2atmpS3016 = _M0L7a__postS3017 * _M0L6_2atmpS3018;
            _M0L6_2atmpS3014 = _M0L6_2atmpS3015 + _M0L6_2atmpS3016;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS836, _M0L3valS3013, _M0L6_2atmpS3014);
          }
          if (_M0L11post__firedS835) {
            int32_t _M0L3valS3021 = _M0L1sS831->$0;
            int32_t _M0L3valS3029 = _M0L1sS831->$0;
            float _M0L6_2atmpS3023;
            float _M0L6a__preS3025;
            struct _M0TPB5ArrayGfE* _M0L4tpreS3027;
            int32_t _M0L3valS3028;
            float _M0L6_2atmpS3026;
            float _M0L6_2atmpS3024;
            float _M0L6_2atmpS3022;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3023
            = _M0MPC15array5Array2atGfE(_M0L1wS836, _M0L3valS3029);
            _M0L6a__preS3025 = _M0L5paramS821->$0;
            _M0L4tpreS3027 = _M0L4varsS814->$0;
            _M0L3valS3028 = _M0L1jS823->$0;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3026
            = _M0MPC15array5Array2atGfE(_M0L4tpreS3027, _M0L3valS3028);
            _M0L6_2atmpS3024 = _M0L6a__preS3025 * _M0L6_2atmpS3026;
            _M0L6_2atmpS3022 = _M0L6_2atmpS3023 + _M0L6_2atmpS3024;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS836, _M0L3valS3021, _M0L6_2atmpS3022);
          }
          _M0L3valS3032 = _M0L1sS831->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3030
          = _M0MPC15array5Array2atGfE(_M0L1wS836, _M0L3valS3032);
          _M0L6w__minS3031 = _M0L5paramS821->$5;
          if (_M0L6_2atmpS3030 < _M0L6w__minS3031) {
            int32_t _M0L3valS3033 = _M0L1sS831->$0;
            float _M0L6w__minS3034 = _M0L5paramS821->$5;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS836, _M0L3valS3033, _M0L6w__minS3034);
          }
          _M0L3valS3037 = _M0L1sS831->$0;
          #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3035
          = _M0MPC15array5Array2atGfE(_M0L1wS836, _M0L3valS3037);
          _M0L6w__maxS3036 = _M0L5paramS821->$4;
          if (_M0L6_2atmpS3035 > _M0L6w__maxS3036) {
            int32_t _M0L3valS3038 = _M0L1sS831->$0;
            float _M0L6w__maxS3039 = _M0L5paramS821->$4;
            #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS836, _M0L3valS3038, _M0L6w__maxS3039);
          }
          _M0L3valS3041 = _M0L1sS831->$0;
          _M0L6_2atmpS3040 = _M0L3valS3041 + 1;
          _M0L1sS831->$0 = _M0L6_2atmpS3040;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS831);
        }
        break;
      }
      _M0L3valS3045 = _M0L1jS823->$0;
      _M0L6_2atmpS3044 = _M0L3valS3045 + 1;
      _M0L1jS823->$0 = _M0L6_2atmpS3044;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS823);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS793,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS783,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS785,
  struct _M0TPB5ArrayGiE* _M0L6colptrS792,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS788,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS795,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS794,
  float _M0L2dtS807
) {
  int32_t _M0L6n__preS782;
  int32_t _M0L7n__postS784;
  struct _M0TPB8MutLocalGiE* _M0L1jS786;
  int32_t _M0L3nnzS798;
  float _M0L4a__xS2967;
  float _M0L6tau__xS2968;
  float _M0L18a__x__over__tau__xS799;
  struct _M0TPB8MutLocalGiE* _M0L2s2S800;
  float _M0L6tau__xS2966;
  float _M0L11inv__tau__xS804;
  float _M0L6tau__yS2965;
  float _M0L11inv__tau__yS805;
  struct _M0TPB8MutLocalGiE* _M0L1iS806;
  struct _M0TPB8MutLocalGiE* _M0L2s3S812;
  #line 622 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 632 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS782 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS783);
  #line 633 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS784 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS785);
  _M0L1jS786
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS786)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS786->$0 = 0;
  while (1) {
    int32_t _M0L3valS2865 = _M0L1jS786->$0;
    if (_M0L3valS2865 < _M0L6n__preS782) {
      int32_t _M0L3valS2866 = _M0L1jS786->$0;
      int32_t _M0L3valS2887;
      int32_t _M0L6_2atmpS2886;
      #line 637 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS783, _M0L3valS2866)) {
        int32_t _M0L3valS2885 = _M0L1jS786->$0;
        int32_t _M0L5startS787;
        int32_t _M0L3valS2884;
        int32_t _M0L6_2atmpS2883;
        int32_t _M0L3endS789;
        struct _M0TPB8MutLocalGiE* _M0L1sS790;
        #line 638 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS787
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS788, _M0L3valS2885);
        _M0L3valS2884 = _M0L1jS786->$0;
        _M0L6_2atmpS2883 = _M0L3valS2884 + 1;
        #line 639 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS789
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS788, _M0L6_2atmpS2883);
        _M0L1sS790
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS790)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS790->$0 = _M0L5startS787;
        while (1) {
          int32_t _M0L3valS2867 = _M0L1sS790->$0;
          if (_M0L3valS2867 < _M0L3endS789) {
            int32_t _M0L3valS2882 = _M0L1sS790->$0;
            int32_t _M0L9post__idxS791;
            int32_t _M0L3valS2868;
            int32_t _M0L3valS2879;
            float _M0L6_2atmpS2877;
            float _M0L10alpha__preS2878;
            float _M0L6_2atmpS2870;
            float _M0L4a__yS2875;
            float _M0L6tau__yS2876;
            float _M0L6_2atmpS2872;
            struct _M0TPB5ArrayGfE* _M0L5to__yS2874;
            float _M0L6_2atmpS2873;
            float _M0L6_2atmpS2871;
            float _M0L6_2atmpS2869;
            int32_t _M0L3valS2881;
            int32_t _M0L6_2atmpS2880;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS791
            = _M0MPC15array5Array2atGiE(_M0L6colptrS792, _M0L3valS2882);
            _M0L3valS2868 = _M0L1sS790->$0;
            _M0L3valS2879 = _M0L1sS790->$0;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2877
            = _M0MPC15array5Array2atGfE(_M0L1wS793, _M0L3valS2879);
            _M0L10alpha__preS2878 = _M0L5paramS794->$4;
            _M0L6_2atmpS2870 = _M0L6_2atmpS2877 + _M0L10alpha__preS2878;
            _M0L4a__yS2875 = _M0L5paramS794->$1;
            _M0L6tau__yS2876 = _M0L5paramS794->$3;
            _M0L6_2atmpS2872 = _M0L4a__yS2875 / _M0L6tau__yS2876;
            _M0L5to__yS2874 = _M0L4varsS795->$1;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2873
            = _M0MPC15array5Array2atGfE(_M0L5to__yS2874, _M0L9post__idxS791);
            _M0L6_2atmpS2871 = _M0L6_2atmpS2872 * _M0L6_2atmpS2873;
            _M0L6_2atmpS2869 = _M0L6_2atmpS2870 - _M0L6_2atmpS2871;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS793, _M0L3valS2868, _M0L6_2atmpS2869);
            _M0L3valS2881 = _M0L1sS790->$0;
            _M0L6_2atmpS2880 = _M0L3valS2881 + 1;
            _M0L1sS790->$0 = _M0L6_2atmpS2880;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS790);
          }
          break;
        }
      }
      _M0L3valS2887 = _M0L1jS786->$0;
      _M0L6_2atmpS2886 = _M0L3valS2887 + 1;
      _M0L1jS786->$0 = _M0L6_2atmpS2886;
      continue;
    }
    break;
  }
  #line 650 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS798 = _M0MPC15array5Array6lengthGfE(_M0L1wS793);
  _M0L4a__xS2967 = _M0L5paramS794->$0;
  _M0L6tau__xS2968 = _M0L5paramS794->$2;
  _M0L18a__x__over__tau__xS799 = _M0L4a__xS2967 / _M0L6tau__xS2968;
  _M0L2s2S800
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S800)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S800->$0 = 0;
  while (1) {
    int32_t _M0L3valS2888 = _M0L2s2S800->$0;
    if (_M0L3valS2888 < _M0L3nnzS798) {
      int32_t _M0L3valS2901 = _M0L2s2S800->$0;
      int32_t _M0L9post__idxS801;
      int32_t _M0L3valS2900;
      int32_t _M0L6_2atmpS2899;
      #line 654 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS801
      = _M0MPC15array5Array2atGiE(_M0L6colptrS792, _M0L3valS2901);
      #line 655 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS785, _M0L9post__idxS801)
      ) {
        int32_t _M0L3valS2898 = _M0L2s2S800->$0;
        int32_t _M0L6j__preS802;
        int32_t _M0L3valS2889;
        int32_t _M0L3valS2897;
        float _M0L6_2atmpS2895;
        float _M0L11alpha__postS2896;
        float _M0L6_2atmpS2891;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS2894;
        float _M0L6_2atmpS2893;
        float _M0L6_2atmpS2892;
        float _M0L6_2atmpS2890;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS802
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS788, _M0L3valS2898);
        _M0L3valS2889 = _M0L2s2S800->$0;
        _M0L3valS2897 = _M0L2s2S800->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2895
        = _M0MPC15array5Array2atGfE(_M0L1wS793, _M0L3valS2897);
        _M0L11alpha__postS2896 = _M0L5paramS794->$5;
        _M0L6_2atmpS2891 = _M0L6_2atmpS2895 + _M0L11alpha__postS2896;
        _M0L5tr__xS2894 = _M0L4varsS795->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2893
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS2894, _M0L6j__preS802);
        _M0L6_2atmpS2892 = _M0L18a__x__over__tau__xS799 * _M0L6_2atmpS2893;
        _M0L6_2atmpS2890 = _M0L6_2atmpS2891 + _M0L6_2atmpS2892;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS793, _M0L3valS2889, _M0L6_2atmpS2890);
      }
      _M0L3valS2900 = _M0L2s2S800->$0;
      _M0L6_2atmpS2899 = _M0L3valS2900 + 1;
      _M0L2s2S800->$0 = _M0L6_2atmpS2899;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S800);
    }
    break;
  }
  _M0L6tau__xS2966 = _M0L5paramS794->$2;
  _M0L11inv__tau__xS804 = 0x1p+0f / _M0L6tau__xS2966;
  _M0L6tau__yS2965 = _M0L5paramS794->$3;
  _M0L11inv__tau__yS805 = 0x1p+0f / _M0L6tau__yS2965;
  _M0L1iS806
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS806)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS806->$0 = 0;
  while (1) {
    int32_t _M0L3valS2902 = _M0L1iS806->$0;
    if (_M0L3valS2902 < _M0L7n__postS784) {
      struct _M0TPB5ArrayGfE* _M0L5to__yS2903 = _M0L4varsS795->$1;
      int32_t _M0L3valS2904 = _M0L1iS806->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__yS2913 = _M0L4varsS795->$1;
      int32_t _M0L3valS2914 = _M0L1iS806->$0;
      float _M0L6_2atmpS2906;
      struct _M0TPB5ArrayGfE* _M0L5to__yS2911;
      int32_t _M0L3valS2912;
      float _M0L6_2atmpS2910;
      float _M0L6_2atmpS2909;
      float _M0L6_2atmpS2908;
      float _M0L6_2atmpS2907;
      float _M0L6_2atmpS2905;
      int32_t _M0L3valS2916;
      int32_t _M0L6_2atmpS2915;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2906
      = _M0MPC15array5Array2atGfE(_M0L5to__yS2913, _M0L3valS2914);
      _M0L5to__yS2911 = _M0L4varsS795->$1;
      _M0L3valS2912 = _M0L1iS806->$0;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2910
      = _M0MPC15array5Array2atGfE(_M0L5to__yS2911, _M0L3valS2912);
      _M0L6_2atmpS2909 = -_M0L6_2atmpS2910;
      _M0L6_2atmpS2908 = _M0L2dtS807 * _M0L6_2atmpS2909;
      _M0L6_2atmpS2907 = _M0L6_2atmpS2908 * _M0L11inv__tau__yS805;
      _M0L6_2atmpS2905 = _M0L6_2atmpS2906 + _M0L6_2atmpS2907;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS2903, _M0L3valS2904, _M0L6_2atmpS2905);
      _M0L3valS2916 = _M0L1iS806->$0;
      _M0L6_2atmpS2915 = _M0L3valS2916 + 1;
      _M0L1iS806->$0 = _M0L6_2atmpS2915;
      continue;
    }
    break;
  }
  _M0L1jS786->$0 = 0;
  while (1) {
    int32_t _M0L3valS2917 = _M0L1jS786->$0;
    if (_M0L3valS2917 < _M0L6n__preS782) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS2918 = _M0L4varsS795->$0;
      int32_t _M0L3valS2919 = _M0L1jS786->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS2928 = _M0L4varsS795->$0;
      int32_t _M0L3valS2929 = _M0L1jS786->$0;
      float _M0L6_2atmpS2921;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS2926;
      int32_t _M0L3valS2927;
      float _M0L6_2atmpS2925;
      float _M0L6_2atmpS2924;
      float _M0L6_2atmpS2923;
      float _M0L6_2atmpS2922;
      float _M0L6_2atmpS2920;
      int32_t _M0L3valS2931;
      int32_t _M0L6_2atmpS2930;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2921
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS2928, _M0L3valS2929);
      _M0L5tr__xS2926 = _M0L4varsS795->$0;
      _M0L3valS2927 = _M0L1jS786->$0;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2925
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS2926, _M0L3valS2927);
      _M0L6_2atmpS2924 = -_M0L6_2atmpS2925;
      _M0L6_2atmpS2923 = _M0L2dtS807 * _M0L6_2atmpS2924;
      _M0L6_2atmpS2922 = _M0L6_2atmpS2923 * _M0L11inv__tau__xS804;
      _M0L6_2atmpS2920 = _M0L6_2atmpS2921 + _M0L6_2atmpS2922;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS2918, _M0L3valS2919, _M0L6_2atmpS2920);
      _M0L3valS2931 = _M0L1jS786->$0;
      _M0L6_2atmpS2930 = _M0L3valS2931 + 1;
      _M0L1jS786->$0 = _M0L6_2atmpS2930;
      continue;
    }
    break;
  }
  _M0L1iS806->$0 = 0;
  while (1) {
    int32_t _M0L3valS2932 = _M0L1iS806->$0;
    if (_M0L3valS2932 < _M0L7n__postS784) {
      int32_t _M0L3valS2933 = _M0L1iS806->$0;
      int32_t _M0L3valS2941;
      int32_t _M0L6_2atmpS2940;
      #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS785, _M0L3valS2933)) {
        struct _M0TPB5ArrayGfE* _M0L5to__yS2934 = _M0L4varsS795->$1;
        int32_t _M0L3valS2935 = _M0L1iS806->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__yS2938 = _M0L4varsS795->$1;
        int32_t _M0L3valS2939 = _M0L1iS806->$0;
        float _M0L6_2atmpS2937;
        float _M0L6_2atmpS2936;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2937
        = _M0MPC15array5Array2atGfE(_M0L5to__yS2938, _M0L3valS2939);
        _M0L6_2atmpS2936 = _M0L6_2atmpS2937 + 0x1p+0f;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS2934, _M0L3valS2935, _M0L6_2atmpS2936);
      }
      _M0L3valS2941 = _M0L1iS806->$0;
      _M0L6_2atmpS2940 = _M0L3valS2941 + 1;
      _M0L1iS806->$0 = _M0L6_2atmpS2940;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS806);
    }
    break;
  }
  _M0L1jS786->$0 = 0;
  while (1) {
    int32_t _M0L3valS2942 = _M0L1jS786->$0;
    if (_M0L3valS2942 < _M0L6n__preS782) {
      int32_t _M0L3valS2943 = _M0L1jS786->$0;
      int32_t _M0L3valS2951;
      int32_t _M0L6_2atmpS2950;
      #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS783, _M0L3valS2943)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS2944 = _M0L4varsS795->$0;
        int32_t _M0L3valS2945 = _M0L1jS786->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS2948 = _M0L4varsS795->$0;
        int32_t _M0L3valS2949 = _M0L1jS786->$0;
        float _M0L6_2atmpS2947;
        float _M0L6_2atmpS2946;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2947
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS2948, _M0L3valS2949);
        _M0L6_2atmpS2946 = _M0L6_2atmpS2947 + 0x1p+0f;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS2944, _M0L3valS2945, _M0L6_2atmpS2946);
      }
      _M0L3valS2951 = _M0L1jS786->$0;
      _M0L6_2atmpS2950 = _M0L3valS2951 + 1;
      _M0L1jS786->$0 = _M0L6_2atmpS2950;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS786);
    }
    break;
  }
  _M0L2s3S812
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S812)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S812->$0 = 0;
  while (1) {
    int32_t _M0L3valS2952 = _M0L2s3S812->$0;
    if (_M0L3valS2952 < _M0L3nnzS798) {
      int32_t _M0L3valS2955 = _M0L2s3S812->$0;
      float _M0L6_2atmpS2953;
      float _M0L6w__minS2954;
      int32_t _M0L3valS2964;
      int32_t _M0L6_2atmpS2963;
      #line 692 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2953 = _M0MPC15array5Array2atGfE(_M0L1wS793, _M0L3valS2955);
      _M0L6w__minS2954 = _M0L5paramS794->$7;
      if (_M0L6_2atmpS2953 < _M0L6w__minS2954) {
        int32_t _M0L3valS2956 = _M0L2s3S812->$0;
        float _M0L6w__minS2957 = _M0L5paramS794->$7;
        #line 693 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS793, _M0L3valS2956, _M0L6w__minS2957);
      } else {
        int32_t _M0L3valS2960 = _M0L2s3S812->$0;
        float _M0L6_2atmpS2958;
        float _M0L6w__maxS2959;
        #line 694 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2958
        = _M0MPC15array5Array2atGfE(_M0L1wS793, _M0L3valS2960);
        _M0L6w__maxS2959 = _M0L5paramS794->$6;
        if (_M0L6_2atmpS2958 > _M0L6w__maxS2959) {
          int32_t _M0L3valS2961 = _M0L2s3S812->$0;
          float _M0L6w__maxS2962 = _M0L5paramS794->$6;
          #line 695 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS793, _M0L3valS2961, _M0L6w__maxS2962);
        }
      }
      _M0L3valS2964 = _M0L2s3S812->$0;
      _M0L6_2atmpS2963 = _M0L3valS2964 + 1;
      _M0L2s3S812->$0 = _M0L6_2atmpS2963;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S812);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE* _M0L1wS768,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS744,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS746,
  struct _M0TPB5ArrayGiE* _M0L6colptrS763,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS759,
  struct _M0TPB5ArrayGfE* _M0L4tpreS754,
  struct _M0TPB5ArrayGfE* _M0L5tpostS750,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS748,
  float _M0L2dtS751
) {
  int32_t _M0L6n__preS743;
  int32_t _M0L7n__postS745;
  float _M0L3tauS2864;
  float _M0L8inv__tauS747;
  struct _M0TPB8MutLocalGiE* _M0L1iS749;
  struct _M0TPB8MutLocalGiE* _M0L1jS753;
  int32_t _M0L3nnzS771;
  struct _M0TPB8MutLocalGiE* _M0L2s2S772;
  struct _M0TPB8MutLocalGiE* _M0L2s3S780;
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 461 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS743 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS744);
  #line 462 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS745 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS746);
  _M0L3tauS2864 = _M0L5paramS748->$1;
  _M0L8inv__tauS747 = 0x1p+0f / _M0L3tauS2864;
  _M0L1iS749
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS749)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS749->$0 = 0;
  while (1) {
    int32_t _M0L3valS2778 = _M0L1iS749->$0;
    if (_M0L3valS2778 < _M0L7n__postS745) {
      int32_t _M0L3valS2779 = _M0L1iS749->$0;
      int32_t _M0L3valS2787 = _M0L1iS749->$0;
      float _M0L6_2atmpS2781;
      int32_t _M0L3valS2786;
      float _M0L6_2atmpS2785;
      float _M0L6_2atmpS2784;
      float _M0L6_2atmpS2783;
      float _M0L6_2atmpS2782;
      float _M0L6_2atmpS2780;
      int32_t _M0L3valS2789;
      int32_t _M0L6_2atmpS2788;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2781
      = _M0MPC15array5Array2atGfE(_M0L5tpostS750, _M0L3valS2787);
      _M0L3valS2786 = _M0L1iS749->$0;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2785
      = _M0MPC15array5Array2atGfE(_M0L5tpostS750, _M0L3valS2786);
      _M0L6_2atmpS2784 = -_M0L6_2atmpS2785;
      _M0L6_2atmpS2783 = _M0L2dtS751 * _M0L6_2atmpS2784;
      _M0L6_2atmpS2782 = _M0L6_2atmpS2783 * _M0L8inv__tauS747;
      _M0L6_2atmpS2780 = _M0L6_2atmpS2781 + _M0L6_2atmpS2782;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS750, _M0L3valS2779, _M0L6_2atmpS2780);
      _M0L3valS2789 = _M0L1iS749->$0;
      _M0L6_2atmpS2788 = _M0L3valS2789 + 1;
      _M0L1iS749->$0 = _M0L6_2atmpS2788;
      continue;
    }
    break;
  }
  _M0L1jS753
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS753)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS753->$0 = 0;
  while (1) {
    int32_t _M0L3valS2790 = _M0L1jS753->$0;
    if (_M0L3valS2790 < _M0L6n__preS743) {
      int32_t _M0L3valS2791 = _M0L1jS753->$0;
      int32_t _M0L3valS2799 = _M0L1jS753->$0;
      float _M0L6_2atmpS2793;
      int32_t _M0L3valS2798;
      float _M0L6_2atmpS2797;
      float _M0L6_2atmpS2796;
      float _M0L6_2atmpS2795;
      float _M0L6_2atmpS2794;
      float _M0L6_2atmpS2792;
      int32_t _M0L3valS2801;
      int32_t _M0L6_2atmpS2800;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2793
      = _M0MPC15array5Array2atGfE(_M0L4tpreS754, _M0L3valS2799);
      _M0L3valS2798 = _M0L1jS753->$0;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2797
      = _M0MPC15array5Array2atGfE(_M0L4tpreS754, _M0L3valS2798);
      _M0L6_2atmpS2796 = -_M0L6_2atmpS2797;
      _M0L6_2atmpS2795 = _M0L2dtS751 * _M0L6_2atmpS2796;
      _M0L6_2atmpS2794 = _M0L6_2atmpS2795 * _M0L8inv__tauS747;
      _M0L6_2atmpS2792 = _M0L6_2atmpS2793 + _M0L6_2atmpS2794;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS754, _M0L3valS2791, _M0L6_2atmpS2792);
      _M0L3valS2801 = _M0L1jS753->$0;
      _M0L6_2atmpS2800 = _M0L3valS2801 + 1;
      _M0L1jS753->$0 = _M0L6_2atmpS2800;
      continue;
    }
    break;
  }
  _M0L1iS749->$0 = 0;
  while (1) {
    int32_t _M0L3valS2802 = _M0L1iS749->$0;
    if (_M0L3valS2802 < _M0L7n__postS745) {
      int32_t _M0L3valS2803 = _M0L1iS749->$0;
      int32_t _M0L3valS2809;
      int32_t _M0L6_2atmpS2808;
      #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS746, _M0L3valS2803)) {
        int32_t _M0L3valS2804 = _M0L1iS749->$0;
        int32_t _M0L3valS2807 = _M0L1iS749->$0;
        float _M0L6_2atmpS2806;
        float _M0L6_2atmpS2805;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2806
        = _M0MPC15array5Array2atGfE(_M0L5tpostS750, _M0L3valS2807);
        _M0L6_2atmpS2805 = _M0L6_2atmpS2806 + 0x1p+0f;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS750, _M0L3valS2804, _M0L6_2atmpS2805);
      }
      _M0L3valS2809 = _M0L1iS749->$0;
      _M0L6_2atmpS2808 = _M0L3valS2809 + 1;
      _M0L1iS749->$0 = _M0L6_2atmpS2808;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS749);
    }
    break;
  }
  _M0L1jS753->$0 = 0;
  while (1) {
    int32_t _M0L3valS2810 = _M0L1jS753->$0;
    if (_M0L3valS2810 < _M0L6n__preS743) {
      int32_t _M0L3valS2811 = _M0L1jS753->$0;
      int32_t _M0L3valS2817;
      int32_t _M0L6_2atmpS2816;
      #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS744, _M0L3valS2811)) {
        int32_t _M0L3valS2812 = _M0L1jS753->$0;
        int32_t _M0L3valS2815 = _M0L1jS753->$0;
        float _M0L6_2atmpS2814;
        float _M0L6_2atmpS2813;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2814
        = _M0MPC15array5Array2atGfE(_M0L4tpreS754, _M0L3valS2815);
        _M0L6_2atmpS2813 = _M0L6_2atmpS2814 + 0x1p+0f;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS754, _M0L3valS2812, _M0L6_2atmpS2813);
      }
      _M0L3valS2817 = _M0L1jS753->$0;
      _M0L6_2atmpS2816 = _M0L3valS2817 + 1;
      _M0L1jS753->$0 = _M0L6_2atmpS2816;
      continue;
    }
    break;
  }
  _M0L1jS753->$0 = 0;
  while (1) {
    int32_t _M0L3valS2818 = _M0L1jS753->$0;
    if (_M0L3valS2818 < _M0L6n__preS743) {
      int32_t _M0L3valS2819 = _M0L1jS753->$0;
      int32_t _M0L3valS2837;
      int32_t _M0L6_2atmpS2836;
      #line 493 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS744, _M0L3valS2819)) {
        int32_t _M0L3valS2835 = _M0L1jS753->$0;
        int32_t _M0L5startS758;
        int32_t _M0L3valS2834;
        int32_t _M0L6_2atmpS2833;
        int32_t _M0L3endS760;
        struct _M0TPB8MutLocalGiE* _M0L1sS761;
        #line 494 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS758
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS759, _M0L3valS2835);
        _M0L3valS2834 = _M0L1jS753->$0;
        _M0L6_2atmpS2833 = _M0L3valS2834 + 1;
        #line 495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS760
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS759, _M0L6_2atmpS2833);
        _M0L1sS761
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS761)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS761->$0 = _M0L5startS758;
        while (1) {
          int32_t _M0L3valS2820 = _M0L1sS761->$0;
          if (_M0L3valS2820 < _M0L3endS760) {
            int32_t _M0L3valS2832 = _M0L1sS761->$0;
            int32_t _M0L9post__idxS762;
            int32_t _M0L3valS2831;
            float _M0L6_2atmpS2829;
            float _M0L6_2atmpS2830;
            float _M0L5ratioS764;
            float _M0L3lnxS765;
            float _M0L1xS766;
            float _M0L1aS2827;
            float _M0L6_2atmpS2828;
            float _M0L2dwS767;
            int32_t _M0L3valS2821;
            int32_t _M0L3valS2824;
            float _M0L6_2atmpS2823;
            float _M0L6_2atmpS2822;
            int32_t _M0L3valS2826;
            int32_t _M0L6_2atmpS2825;
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS762
            = _M0MPC15array5Array2atGiE(_M0L6colptrS763, _M0L3valS2832);
            _M0L3valS2831 = _M0L1jS753->$0;
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2829
            = _M0MPC15array5Array2atGfE(_M0L4tpreS754, _M0L3valS2831);
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2830
            = _M0MPC15array5Array2atGfE(_M0L5tpostS750, _M0L9post__idxS762);
            _M0L5ratioS764 = _M0L6_2atmpS2829 / _M0L6_2atmpS2830;
            #line 500 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L3lnxS765 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS764);
            _M0L1xS766 = _M0L3lnxS765 * _M0L3lnxS765;
            _M0L1aS2827 = _M0L5paramS748->$0;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2828
            = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS766);
            _M0L2dwS767 = _M0L1aS2827 * _M0L6_2atmpS2828;
            _M0L3valS2821 = _M0L1sS761->$0;
            _M0L3valS2824 = _M0L1sS761->$0;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2823
            = _M0MPC15array5Array2atGfE(_M0L1wS768, _M0L3valS2824);
            _M0L6_2atmpS2822 = _M0L6_2atmpS2823 + _M0L2dwS767;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS768, _M0L3valS2821, _M0L6_2atmpS2822);
            _M0L3valS2826 = _M0L1sS761->$0;
            _M0L6_2atmpS2825 = _M0L3valS2826 + 1;
            _M0L1sS761->$0 = _M0L6_2atmpS2825;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS761);
          }
          break;
        }
      }
      _M0L3valS2837 = _M0L1jS753->$0;
      _M0L6_2atmpS2836 = _M0L3valS2837 + 1;
      _M0L1jS753->$0 = _M0L6_2atmpS2836;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS753);
    }
    break;
  }
  #line 511 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS771 = _M0MPC15array5Array6lengthGfE(_M0L1wS768);
  _M0L2s2S772
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S772)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S772->$0 = 0;
  while (1) {
    int32_t _M0L3valS2838 = _M0L2s2S772->$0;
    if (_M0L3valS2838 < _M0L3nnzS771) {
      int32_t _M0L3valS2850 = _M0L2s2S772->$0;
      int32_t _M0L9post__idxS773;
      int32_t _M0L3valS2849;
      int32_t _M0L6_2atmpS2848;
      #line 514 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS773
      = _M0MPC15array5Array2atGiE(_M0L6colptrS763, _M0L3valS2850);
      #line 515 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS746, _M0L9post__idxS773)
      ) {
        int32_t _M0L3valS2847 = _M0L2s2S772->$0;
        int32_t _M0L6j__preS774;
        float _M0L6_2atmpS2845;
        float _M0L6_2atmpS2846;
        float _M0L5ratioS775;
        float _M0L3lnxS776;
        float _M0L1xS777;
        float _M0L1aS2843;
        float _M0L6_2atmpS2844;
        float _M0L2dwS778;
        int32_t _M0L3valS2839;
        int32_t _M0L3valS2842;
        float _M0L6_2atmpS2841;
        float _M0L6_2atmpS2840;
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS774
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS759, _M0L3valS2847);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2845
        = _M0MPC15array5Array2atGfE(_M0L4tpreS754, _M0L6j__preS774);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2846
        = _M0MPC15array5Array2atGfE(_M0L5tpostS750, _M0L9post__idxS773);
        _M0L5ratioS775 = _M0L6_2atmpS2845 / _M0L6_2atmpS2846;
        #line 520 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3lnxS776 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS775);
        _M0L1xS777 = _M0L3lnxS776 * _M0L3lnxS776;
        _M0L1aS2843 = _M0L5paramS748->$0;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2844
        = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS777);
        _M0L2dwS778 = _M0L1aS2843 * _M0L6_2atmpS2844;
        _M0L3valS2839 = _M0L2s2S772->$0;
        _M0L3valS2842 = _M0L2s2S772->$0;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2841
        = _M0MPC15array5Array2atGfE(_M0L1wS768, _M0L3valS2842);
        _M0L6_2atmpS2840 = _M0L6_2atmpS2841 + _M0L2dwS778;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS768, _M0L3valS2839, _M0L6_2atmpS2840);
      }
      _M0L3valS2849 = _M0L2s2S772->$0;
      _M0L6_2atmpS2848 = _M0L3valS2849 + 1;
      _M0L2s2S772->$0 = _M0L6_2atmpS2848;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S772);
    }
    break;
  }
  _M0L2s3S780
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S780)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S780->$0 = 0;
  while (1) {
    int32_t _M0L3valS2851 = _M0L2s3S780->$0;
    if (_M0L3valS2851 < _M0L3nnzS771) {
      int32_t _M0L3valS2854 = _M0L2s3S780->$0;
      float _M0L6_2atmpS2852;
      float _M0L6w__minS2853;
      int32_t _M0L3valS2863;
      int32_t _M0L6_2atmpS2862;
      #line 530 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2852 = _M0MPC15array5Array2atGfE(_M0L1wS768, _M0L3valS2854);
      _M0L6w__minS2853 = _M0L5paramS748->$3;
      if (_M0L6_2atmpS2852 < _M0L6w__minS2853) {
        int32_t _M0L3valS2855 = _M0L2s3S780->$0;
        float _M0L6w__minS2856 = _M0L5paramS748->$3;
        #line 531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS768, _M0L3valS2855, _M0L6w__minS2856);
      } else {
        int32_t _M0L3valS2859 = _M0L2s3S780->$0;
        float _M0L6_2atmpS2857;
        float _M0L6w__maxS2858;
        #line 532 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2857
        = _M0MPC15array5Array2atGfE(_M0L1wS768, _M0L3valS2859);
        _M0L6w__maxS2858 = _M0L5paramS748->$2;
        if (_M0L6_2atmpS2857 > _M0L6w__maxS2858) {
          int32_t _M0L3valS2860 = _M0L2s3S780->$0;
          float _M0L6w__maxS2861 = _M0L5paramS748->$2;
          #line 533 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS768, _M0L3valS2860, _M0L6w__maxS2861);
        }
      }
      _M0L3valS2863 = _M0L2s3S780->$0;
      _M0L6_2atmpS2862 = _M0L3valS2863 + 1;
      _M0L2s3S780->$0 = _M0L6_2atmpS2862;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S780);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE* _M0L6rowptrS737,
  int32_t _M0L1sS741
) {
  int32_t _M0L6_2atmpS2777;
  int32_t _M0L1nS736;
  struct _M0TPB8MutLocalGiE* _M0L2loS738;
  struct _M0TPB8MutLocalGiE* _M0L2hiS739;
  int32_t _result_5305;
  #line 542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 543 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2777 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS737);
  _M0L1nS736 = _M0L6_2atmpS2777 - 1;
  _M0L2loS738
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2loS738)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2loS738->$0 = 0;
  _M0L2hiS739
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2hiS739)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2hiS739->$0 = _M0L1nS736;
  while (1) {
    int32_t _M0L3valS2769 = _M0L2loS738->$0;
    int32_t _M0L3valS2770 = _M0L2hiS739->$0;
    if (_M0L3valS2769 < _M0L3valS2770) {
      int32_t _M0L3valS2775 = _M0L2loS738->$0;
      int32_t _M0L3valS2776 = _M0L2hiS739->$0;
      int32_t _M0L6_2atmpS2774 = _M0L3valS2775 + _M0L3valS2776;
      int32_t _M0L6_2atmpS2773 = _M0L6_2atmpS2774 + 1;
      int32_t _M0L3midS740 = _M0L6_2atmpS2773 / 2;
      int32_t _M0L6_2atmpS2771;
      #line 548 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2771
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS737, _M0L3midS740);
      if (_M0L6_2atmpS2771 <= _M0L1sS741) {
        _M0L2loS738->$0 = _M0L3midS740;
      } else {
        int32_t _M0L6_2atmpS2772 = _M0L3midS740 - 1;
        _M0L2hiS739->$0 = _M0L6_2atmpS2772;
      }
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2hiS739);
    }
    break;
  }
  _result_5305 = _M0L2loS738->$0;
  moonbit_decref_cycle_free(_M0L2loS738);
  return _result_5305;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS733) {
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 428 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS733)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS2768 = -_M0L1xS733;
    float _M0L3argS734 = _M0L6_2atmpS2768 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS2766 = 0x1p+0f - _M0L1xS733;
    float _M0L6_2atmpS2767;
    float _M0L1vS735;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS2767 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS734);
    _M0L1vS735 = _M0L6_2atmpS2766 * _M0L6_2atmpS2767;
    #line 433 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS735)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS735;
    }
  }
}

int32_t _M0FP26RiantR8snn__mbt19stimulate__balanced(
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1sS700,
  float _M0L4timeS698,
  float _M0L2dtS709
) {
  int32_t _M0L1nS699;
  struct _M0TP26RiantR8snn__mbt17BalancedParameter* _M0L5paramS701;
  float _M0L3kIES702;
  float _M0L4betaS703;
  float _M0L3tauS704;
  float _M0L2r0S705;
  float _M0L1wS706;
  float _M0L3wIES707;
  float _M0L6_2atmpS2765;
  float _M0L11inh__lambdaS708;
  int32_t _M0L7_2abindS710;
  int32_t _M0L1kS711;
  float _M0L6_2atmpS2764;
  float _M0L2ccS715;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
  _M0L1nS699 = _M0L1sS700->$1;
  _M0L5paramS701 = _M0L1sS700->$0;
  _M0L3kIES702 = _M0L5paramS701->$0;
  _M0L4betaS703 = _M0L5paramS701->$1;
  _M0L3tauS704 = _M0L5paramS701->$2;
  _M0L2r0S705 = _M0L5paramS701->$3;
  _M0L1wS706 = _M0L5paramS701->$4;
  _M0L3wIES707 = _M0L5paramS701->$5;
  _M0L6_2atmpS2765 = _M0L2r0S705 * _M0L3kIES702;
  _M0L11inh__lambdaS708 = _M0L6_2atmpS2765 * _M0L2dtS709;
  _M0L7_2abindS710 = 0;
  _M0L1kS711 = _M0L7_2abindS710;
  while (1) {
    if (_M0L1kS711 < _M0L1nS699) {
      struct _M0TPB5ArrayGbE* _M0L4fireS2678 = _M0L1sS700->$4;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2686;
      int32_t _M0L1mS714;
      int32_t _M0L6_2atmpS2677;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2678, _M0L1kS711, 0);
      if (_M0L11inh__lambdaS708 <= 0x0p+0f) {
        goto join_712;
      }
      _M0L3rngS2686 = _M0L1sS700->$7;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0L1mS714
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2686, _M0L11inh__lambdaS708);
      if (_M0L1mS714 > 0) {
        struct _M0TPB5ArrayGfE* _M0L2giS2679 = _M0L1sS700->$3;
        struct _M0TPB5ArrayGfE* _M0L2giS2685 = _M0L1sS700->$3;
        float _M0L6_2atmpS2681;
        float _M0L6_2atmpS2684;
        float _M0L6_2atmpS2683;
        float _M0L6_2atmpS2682;
        float _M0L6_2atmpS2680;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS2681
        = _M0MPC15array5Array2atGfE(_M0L2giS2685, _M0L1kS711);
        _M0L6_2atmpS2684 = (float)_M0L1mS714;
        _M0L6_2atmpS2683 = _M0L1wS706 * _M0L6_2atmpS2684;
        _M0L6_2atmpS2682 = _M0L6_2atmpS2683 * _M0L3wIES707;
        _M0L6_2atmpS2680 = _M0L6_2atmpS2681 + _M0L6_2atmpS2682;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L2giS2679, _M0L1kS711, _M0L6_2atmpS2680);
      }
      goto join_712;
      goto joinlet_5307;
      join_712:;
      _M0L6_2atmpS2677 = _M0L1kS711 + 1;
      _M0L1kS711 = _M0L6_2atmpS2677;
      continue;
      joinlet_5307:;
    }
    break;
  }
  _M0L6_2atmpS2764 = _M0L2dtS709 / _M0L3tauS704;
  _M0L2ccS715 = 0x1p+0f - _M0L6_2atmpS2764;
  if (_M0L5paramS701->$6) {
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2724 = _M0L1sS700->$7;
    double _M0L6_2atmpS2723;
    float _M0L6_2atmpS2722;
    float _M0L2reS716;
    struct _M0TPB5ArrayGfE* _M0L5noiseS2687;
    struct _M0TPB5ArrayGfE* _M0L5noiseS2692;
    float _M0L6_2atmpS2691;
    float _M0L6_2atmpS2690;
    float _M0L6_2atmpS2689;
    float _M0L6_2atmpS2688;
    struct _M0TPB5ArrayGfE* _M0L5noiseS2721;
    float _M0L6_2atmpS2720;
    float _M0L6_2atmpS2719;
    struct _M0TPB8MutLocalGfE* _M0L2nbS717;
    float _M0L3valS2693;
    float _M0L3valS2694;
    float _M0L6_2atmpS2717;
    float _M0L3valS2718;
    float _M0L6_2atmpS2714;
    struct _M0TPB5ArrayGfE* _M0L1rS2716;
    float _M0L6_2atmpS2715;
    float _M0L6_2atmpS2713;
    struct _M0TPB8MutLocalGfE* _M0L5erateS718;
    float _M0L3valS2695;
    struct _M0TPB5ArrayGfE* _M0L1rS2696;
    struct _M0TPB5ArrayGfE* _M0L1rS2703;
    float _M0L6_2atmpS2698;
    float _M0L3valS2702;
    float _M0L6_2atmpS2701;
    float _M0L6_2atmpS2700;
    float _M0L6_2atmpS2699;
    float _M0L6_2atmpS2697;
    float _M0L3valS2712;
    float _M0L11exc__lambdaS719;
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2711;
    int32_t _M0L1mS720;
    #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS2723 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS2724);
    _M0L6_2atmpS2722 = (float)_M0L6_2atmpS2723;
    _M0L2reS716 = _M0L6_2atmpS2722 - 0x1p-1f;
    _M0L5noiseS2687 = _M0L1sS700->$6;
    _M0L5noiseS2692 = _M0L1sS700->$6;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS2691 = _M0MPC15array5Array2atGfE(_M0L5noiseS2692, 0);
    _M0L6_2atmpS2690 = _M0L6_2atmpS2691 - _M0L2reS716;
    _M0L6_2atmpS2689 = _M0L6_2atmpS2690 * _M0L2ccS715;
    _M0L6_2atmpS2688 = _M0L6_2atmpS2689 + _M0L2reS716;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L5noiseS2687, 0, _M0L6_2atmpS2688);
    _M0L5noiseS2721 = _M0L1sS700->$6;
    #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS2720 = _M0MPC15array5Array2atGfE(_M0L5noiseS2721, 0);
    _M0L6_2atmpS2719 = _M0L6_2atmpS2720 * _M0L4betaS703;
    _M0L2nbS717
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L2nbS717)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L2nbS717->$0 = _M0L6_2atmpS2719;
    _M0L3valS2693 = _M0L2nbS717->$0;
    if (_M0L3valS2693 > 0x1p+0f) {
      _M0L2nbS717->$0 = 0x1p+0f;
    }
    _M0L3valS2694 = _M0L2nbS717->$0;
    if (_M0L3valS2694 < 0x0p+0f) {
      _M0L2nbS717->$0 = 0x0p+0f;
    }
    _M0L6_2atmpS2717 = _M0L2r0S705 / 0x1p+1f;
    _M0L3valS2718 = _M0L2nbS717->$0;
    moonbit_decref_cycle_free(_M0L2nbS717);
    _M0L6_2atmpS2714 = _M0L6_2atmpS2717 * _M0L3valS2718;
    _M0L1rS2716 = _M0L1sS700->$5;
    #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS2715 = _M0MPC15array5Array2atGfE(_M0L1rS2716, 0);
    _M0L6_2atmpS2713 = _M0L6_2atmpS2714 + _M0L6_2atmpS2715;
    _M0L5erateS718
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L5erateS718)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L5erateS718->$0 = _M0L6_2atmpS2713;
    _M0L3valS2695 = _M0L5erateS718->$0;
    if (_M0L3valS2695 < 0x0p+0f) {
      _M0L5erateS718->$0 = 0x0p+0f;
    }
    _M0L1rS2696 = _M0L1sS700->$5;
    _M0L1rS2703 = _M0L1sS700->$5;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS2698 = _M0MPC15array5Array2atGfE(_M0L1rS2703, 0);
    _M0L3valS2702 = _M0L5erateS718->$0;
    _M0L6_2atmpS2701 = _M0L2r0S705 - _M0L3valS2702;
    _M0L6_2atmpS2700 = _M0L6_2atmpS2701 / 0x1.9p+8f;
    _M0L6_2atmpS2699 = _M0L6_2atmpS2700 * _M0L2dtS709;
    _M0L6_2atmpS2697 = _M0L6_2atmpS2698 + _M0L6_2atmpS2699;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L1rS2696, 0, _M0L6_2atmpS2697);
    _M0L3valS2712 = _M0L5erateS718->$0;
    moonbit_decref_cycle_free(_M0L5erateS718);
    _M0L11exc__lambdaS719 = _M0L3valS2712 * _M0L2dtS709;
    _M0L3rngS2711 = _M0L1sS700->$7;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L1mS720
    = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2711, _M0L11exc__lambdaS719);
    if (_M0L1mS720 > 0) {
      float _M0L6_2atmpS2710 = (float)_M0L1mS720;
      float _M0L3addS721 = _M0L1wS706 * _M0L6_2atmpS2710;
      int32_t _M0L7_2abindS722 = 0;
      int32_t _M0L1iS723 = _M0L7_2abindS722;
      while (1) {
        if (_M0L1iS723 < _M0L1nS699) {
          struct _M0TPB5ArrayGfE* _M0L2geS2704 = _M0L1sS700->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS2707 = _M0L1sS700->$2;
          float _M0L6_2atmpS2706;
          float _M0L6_2atmpS2705;
          struct _M0TPB5ArrayGbE* _M0L4fireS2708;
          int32_t _M0L6_2atmpS2709;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS2706
          = _M0MPC15array5Array2atGfE(_M0L2geS2707, _M0L1iS723);
          _M0L6_2atmpS2705 = _M0L6_2atmpS2706 + _M0L3addS721;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS2704, _M0L1iS723, _M0L6_2atmpS2705);
          _M0L4fireS2708 = _M0L1sS700->$4;
          #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS2708, _M0L1iS723, 1);
          _M0L6_2atmpS2709 = _M0L1iS723 + 1;
          _M0L1iS723 = _M0L6_2atmpS2709;
          continue;
        }
        break;
      }
    }
  } else {
    int32_t _M0L7_2abindS725 = 0;
    int32_t _M0L1iS726 = _M0L7_2abindS725;
    while (1) {
      if (_M0L1iS726 < _M0L1nS699) {
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2762 = _M0L1sS700->$7;
        double _M0L6_2atmpS2761;
        float _M0L6_2atmpS2760;
        float _M0L2reS727;
        struct _M0TPB5ArrayGfE* _M0L5noiseS2725;
        struct _M0TPB5ArrayGfE* _M0L5noiseS2730;
        float _M0L6_2atmpS2729;
        float _M0L6_2atmpS2728;
        float _M0L6_2atmpS2727;
        float _M0L6_2atmpS2726;
        struct _M0TPB5ArrayGfE* _M0L5noiseS2759;
        float _M0L6_2atmpS2758;
        float _M0L6_2atmpS2757;
        struct _M0TPB8MutLocalGfE* _M0L2nbS728;
        float _M0L3valS2731;
        float _M0L3valS2732;
        float _M0L6_2atmpS2755;
        float _M0L3valS2756;
        float _M0L6_2atmpS2752;
        struct _M0TPB5ArrayGfE* _M0L1rS2754;
        float _M0L6_2atmpS2753;
        float _M0L6_2atmpS2751;
        struct _M0TPB8MutLocalGfE* _M0L5erateS729;
        float _M0L3valS2733;
        struct _M0TPB5ArrayGfE* _M0L1rS2734;
        struct _M0TPB5ArrayGfE* _M0L1rS2741;
        float _M0L6_2atmpS2736;
        float _M0L3valS2740;
        float _M0L6_2atmpS2739;
        float _M0L6_2atmpS2738;
        float _M0L6_2atmpS2737;
        float _M0L6_2atmpS2735;
        float _M0L3valS2750;
        float _M0L11exc__lambdaS730;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2749;
        int32_t _M0L1mS731;
        int32_t _M0L6_2atmpS2763;
        #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS2761 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS2762);
        _M0L6_2atmpS2760 = (float)_M0L6_2atmpS2761;
        _M0L2reS727 = _M0L6_2atmpS2760 - 0x1p-1f;
        _M0L5noiseS2725 = _M0L1sS700->$6;
        _M0L5noiseS2730 = _M0L1sS700->$6;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS2729
        = _M0MPC15array5Array2atGfE(_M0L5noiseS2730, _M0L1iS726);
        _M0L6_2atmpS2728 = _M0L6_2atmpS2729 - _M0L2reS727;
        _M0L6_2atmpS2727 = _M0L6_2atmpS2728 * _M0L2ccS715;
        _M0L6_2atmpS2726 = _M0L6_2atmpS2727 + _M0L2reS727;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L5noiseS2725, _M0L1iS726, _M0L6_2atmpS2726);
        _M0L5noiseS2759 = _M0L1sS700->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS2758
        = _M0MPC15array5Array2atGfE(_M0L5noiseS2759, _M0L1iS726);
        _M0L6_2atmpS2757 = _M0L6_2atmpS2758 * _M0L4betaS703;
        _M0L2nbS728
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L2nbS728)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2nbS728->$0 = _M0L6_2atmpS2757;
        _M0L3valS2731 = _M0L2nbS728->$0;
        if (_M0L3valS2731 > 0x1p+0f) {
          _M0L2nbS728->$0 = 0x1p+0f;
        }
        _M0L3valS2732 = _M0L2nbS728->$0;
        if (_M0L3valS2732 < 0x0p+0f) {
          _M0L2nbS728->$0 = 0x0p+0f;
        }
        _M0L6_2atmpS2755 = _M0L2r0S705 / 0x1p+1f;
        _M0L3valS2756 = _M0L2nbS728->$0;
        moonbit_decref_cycle_free(_M0L2nbS728);
        _M0L6_2atmpS2752 = _M0L6_2atmpS2755 * _M0L3valS2756;
        _M0L1rS2754 = _M0L1sS700->$5;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS2753 = _M0MPC15array5Array2atGfE(_M0L1rS2754, _M0L1iS726);
        _M0L6_2atmpS2751 = _M0L6_2atmpS2752 + _M0L6_2atmpS2753;
        _M0L5erateS729
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L5erateS729)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L5erateS729->$0 = _M0L6_2atmpS2751;
        _M0L3valS2733 = _M0L5erateS729->$0;
        if (_M0L3valS2733 < 0x0p+0f) {
          _M0L5erateS729->$0 = 0x0p+0f;
        }
        _M0L1rS2734 = _M0L1sS700->$5;
        _M0L1rS2741 = _M0L1sS700->$5;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS2736 = _M0MPC15array5Array2atGfE(_M0L1rS2741, _M0L1iS726);
        _M0L3valS2740 = _M0L5erateS729->$0;
        _M0L6_2atmpS2739 = _M0L2r0S705 - _M0L3valS2740;
        _M0L6_2atmpS2738 = _M0L6_2atmpS2739 / 0x1.9p+8f;
        _M0L6_2atmpS2737 = _M0L6_2atmpS2738 * _M0L2dtS709;
        _M0L6_2atmpS2735 = _M0L6_2atmpS2736 + _M0L6_2atmpS2737;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L1rS2734, _M0L1iS726, _M0L6_2atmpS2735);
        _M0L3valS2750 = _M0L5erateS729->$0;
        moonbit_decref_cycle_free(_M0L5erateS729);
        _M0L11exc__lambdaS730 = _M0L3valS2750 * _M0L2dtS709;
        _M0L3rngS2749 = _M0L1sS700->$7;
        #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L1mS731
        = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2749, _M0L11exc__lambdaS730);
        if (_M0L1mS731 > 0) {
          struct _M0TPB5ArrayGfE* _M0L2geS2742 = _M0L1sS700->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS2747 = _M0L1sS700->$2;
          float _M0L6_2atmpS2744;
          float _M0L6_2atmpS2746;
          float _M0L6_2atmpS2745;
          float _M0L6_2atmpS2743;
          struct _M0TPB5ArrayGbE* _M0L4fireS2748;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS2744
          = _M0MPC15array5Array2atGfE(_M0L2geS2747, _M0L1iS726);
          _M0L6_2atmpS2746 = (float)_M0L1mS731;
          _M0L6_2atmpS2745 = _M0L1wS706 * _M0L6_2atmpS2746;
          _M0L6_2atmpS2743 = _M0L6_2atmpS2744 + _M0L6_2atmpS2745;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS2742, _M0L1iS726, _M0L6_2atmpS2743);
          _M0L4fireS2748 = _M0L1sS700->$4;
          #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS2748, _M0L1iS726, 1);
        }
        _M0L6_2atmpS2763 = _M0L1iS726 + 1;
        _M0L1iS726 = _M0L6_2atmpS2763;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS696
) {
  struct _M0TUmmmmE* _M0L1sS695;
  uint64_t _M0L6_2atmpS2676;
  struct _M0TUmmmmE* _M0L1tS697;
  uint64_t _M0L6_2atmpS2672;
  uint64_t _M0L6_2atmpS2673;
  uint64_t _M0L6_2atmpS2674;
  uint64_t _M0L6_2atmpS2675;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_5310;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS695 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS696);
  _M0L6_2atmpS2676 = _M0L1sS695->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS697 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2676);
  _M0L6_2atmpS2672 = _M0L1sS695->$0;
  _M0L6_2atmpS2673 = _M0L1sS695->$1;
  _M0L6_2atmpS2674 = _M0L1sS695->$2;
  moonbit_decref_cycle_free(_M0L1sS695);
  _M0L6_2atmpS2675 = _M0L1tS697->$0;
  moonbit_decref_cycle_free(_M0L1tS697);
  _block_5310
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_5310)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5310->$0 = _M0L6_2atmpS2672;
  _block_5310->$1 = _M0L6_2atmpS2673;
  _block_5310->$2 = _M0L6_2atmpS2674;
  _block_5310->$3 = _M0L6_2atmpS2675;
  return _block_5310;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS687) {
  uint64_t _M0L2s1S686;
  uint64_t _M0L2z1S688;
  uint64_t _M0L2s2S689;
  uint64_t _M0L2z2S690;
  uint64_t _M0L2s3S691;
  uint64_t _M0L2z3S692;
  uint64_t _M0L2s4S693;
  uint64_t _M0L2z4S694;
  struct _M0TUmmmmE* _block_5311;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S686 = _M0L4seedS687 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S688 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S686);
  _M0L2s2S689 = _M0L2s1S686 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S690 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S689);
  _M0L2s3S691 = _M0L2s2S689 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S692 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S691);
  _M0L2s4S693 = _M0L2s3S691 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S694 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S693);
  _block_5311 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_5311)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5311->$0 = _M0L2z1S688;
  _block_5311->$1 = _M0L2z2S690;
  _block_5311->$2 = _M0L2z3S692;
  _block_5311->$3 = _M0L2z4S694;
  return _block_5311;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS684) {
  uint64_t _M0L6_2atmpS2671;
  uint64_t _M0L6_2atmpS2670;
  uint64_t _M0L1zS683;
  uint64_t _M0L6_2atmpS2669;
  uint64_t _M0L6_2atmpS2668;
  uint64_t _M0L1zS685;
  uint64_t _M0L6_2atmpS2667;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2671 = _M0L1zS684 >> 30;
  _M0L6_2atmpS2670 = _M0L1zS684 ^ _M0L6_2atmpS2671;
  _M0L1zS683 = _M0L6_2atmpS2670 * 13787848793156543929ull;
  _M0L6_2atmpS2669 = _M0L1zS683 >> 27;
  _M0L6_2atmpS2668 = _M0L1zS683 ^ _M0L6_2atmpS2669;
  _M0L1zS685 = _M0L6_2atmpS2668 * 10723151780598845931ull;
  _M0L6_2atmpS2667 = _M0L1zS685 >> 31;
  return _M0L1zS685 ^ _M0L6_2atmpS2667;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS671
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2651;
  int32_t _M0L6_2atmpS2650;
  float _M0L12noise__sigmaS2652;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS2651 = _M0L1sS671->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS2650 = _M0MPC15array5Array2atGbE(_M0L6activeS2651, 0);
  if (!_M0L6_2atmpS2650) {
    return 0;
  }
  _M0L12noise__sigmaS2652 = _M0L1sS671->$4;
  if (_M0L12noise__sigmaS2652 <= 0x0p+0f) {
    int32_t _M0L7_2abindS672 = 0;
    int32_t _M0L7_2abindS673 = _M0L1sS671->$3;
    int32_t _M0L1kS674 = _M0L7_2abindS672;
    while (1) {
      if (_M0L1kS674 < _M0L7_2abindS673) {
        struct _M0TPB5ArrayGfE* _M0L1iS2653 = _M0L1sS671->$2;
        float _M0L7i__baseS2654 = _M0L1sS671->$0;
        int32_t _M0L6_2atmpS2655;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS2653, _M0L1kS674, _M0L7i__baseS2654);
        _M0L6_2atmpS2655 = _M0L1kS674 + 1;
        _M0L1kS674 = _M0L6_2atmpS2655;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS676 = _M0L1sS671->$4;
    struct _M0TPB8MutLocalGiE* _M0L1kS677 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS677)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS677->$0 = 0;
    while (1) {
      int32_t _M0L3valS2656 = _M0L1kS677->$0;
      int32_t _M0L1nS2657 = _M0L1sS671->$3;
      if (_M0L3valS2656 < _M0L1nS2657) {
        double _M0L2z1S679;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2666 = _M0L1sS671->$5;
        struct _M0TUddE* _M0L7_2abindS680;
        double _M0L5_2az1S681;
        struct _M0TPB5ArrayGfE* _M0L1iS2658;
        int32_t _M0L3valS2659;
        float _M0L7i__baseS2661;
        float _M0L6_2atmpS2663;
        float _M0L6_2atmpS2662;
        float _M0L6_2atmpS2660;
        int32_t _M0L3valS2665;
        int32_t _M0L6_2atmpS2664;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS680 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS2666);
        _M0L5_2az1S681 = _M0L7_2abindS680->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS680);
        _M0L2z1S679 = _M0L5_2az1S681;
        goto join_678;
        goto joinlet_5314;
        join_678:;
        _M0L1iS2658 = _M0L1sS671->$2;
        _M0L3valS2659 = _M0L1kS677->$0;
        _M0L7i__baseS2661 = _M0L1sS671->$0;
        _M0L6_2atmpS2663 = (float)_M0L2z1S679;
        _M0L6_2atmpS2662 = _M0L5sigmaS676 * _M0L6_2atmpS2663;
        _M0L6_2atmpS2660 = _M0L7i__baseS2661 + _M0L6_2atmpS2662;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS2658, _M0L3valS2659, _M0L6_2atmpS2660);
        _M0L3valS2665 = _M0L1kS677->$0;
        _M0L6_2atmpS2664 = _M0L3valS2665 + 1;
        _M0L1kS677->$0 = _M0L6_2atmpS2664;
        joinlet_5314:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS677);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stimulate__current__if(
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1sS658
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2635;
  int32_t _M0L6_2atmpS2634;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS659;
  int32_t _M0L1nS660;
  float _M0L12noise__sigmaS2636;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS2635 = _M0L1sS658->$1;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS2634 = _M0MPC15array5Array2atGbE(_M0L6activeS2635, 0);
  if (!_M0L6_2atmpS2634) {
    return 0;
  }
  _M0L3popS659 = _M0L1sS658->$2;
  _M0L1nS660 = _M0L3popS659->$2;
  _M0L12noise__sigmaS2636 = _M0L1sS658->$3;
  if (_M0L12noise__sigmaS2636 <= 0x0p+0f) {
    int32_t _M0L7_2abindS661 = 0;
    int32_t _M0L1iS662 = _M0L7_2abindS661;
    while (1) {
      if (_M0L1iS662 < _M0L1nS660) {
        struct _M0TPB5ArrayGfE* _M0L1iS2637 = _M0L3popS659->$7;
        float _M0L7i__baseS2638 = _M0L1sS658->$0;
        int32_t _M0L6_2atmpS2639;
        #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS2637, _M0L1iS662, _M0L7i__baseS2638);
        _M0L6_2atmpS2639 = _M0L1iS662 + 1;
        _M0L1iS662 = _M0L6_2atmpS2639;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS664 = _M0L1sS658->$3;
    struct _M0TPB8MutLocalGiE* _M0L1kS665 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS665)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS665->$0 = 0;
    while (1) {
      int32_t _M0L3valS2640 = _M0L1kS665->$0;
      if (_M0L3valS2640 < _M0L1nS660) {
        double _M0L2z1S667;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2649 = _M0L1sS658->$4;
        struct _M0TUddE* _M0L7_2abindS668;
        double _M0L5_2az1S669;
        struct _M0TPB5ArrayGfE* _M0L1iS2641;
        int32_t _M0L3valS2642;
        float _M0L7i__baseS2644;
        float _M0L6_2atmpS2646;
        float _M0L6_2atmpS2645;
        float _M0L6_2atmpS2643;
        int32_t _M0L3valS2648;
        int32_t _M0L6_2atmpS2647;
        #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS668 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS2649);
        _M0L5_2az1S669 = _M0L7_2abindS668->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS668);
        _M0L2z1S667 = _M0L5_2az1S669;
        goto join_666;
        goto joinlet_5317;
        join_666:;
        _M0L1iS2641 = _M0L3popS659->$7;
        _M0L3valS2642 = _M0L1kS665->$0;
        _M0L7i__baseS2644 = _M0L1sS658->$0;
        _M0L6_2atmpS2646 = (float)_M0L2z1S667;
        _M0L6_2atmpS2645 = _M0L5sigmaS664 * _M0L6_2atmpS2646;
        _M0L6_2atmpS2643 = _M0L7i__baseS2644 + _M0L6_2atmpS2645;
        #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS2641, _M0L3valS2642, _M0L6_2atmpS2643);
        _M0L3valS2648 = _M0L1kS665->$0;
        _M0L6_2atmpS2647 = _M0L3valS2648 + 1;
        _M0L1kS665->$0 = _M0L6_2atmpS2647;
        joinlet_5317:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS665);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS647,
  float _M0L4timeS657,
  float _M0L2dtS649
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2621;
  struct _M0TPB5ArrayGbE* _M0L6activeS2620;
  int32_t _M0L6_2atmpS2619;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2633;
  float _M0L4rateS2632;
  float _M0L6lambdaS648;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS650;
  int32_t _M0L7_2abindS651;
  int32_t* _M0L7_2abindS652;
  int32_t _M0L2__S653;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS2621 = _M0L1sS647->$0;
  _M0L6activeS2620 = _M0L5paramS2621->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2619 = _M0MPC15array5Array2atGbE(_M0L6activeS2620, 0);
  if (!_M0L6_2atmpS2619) {
    return 0;
  }
  _M0L5paramS2633 = _M0L1sS647->$0;
  _M0L4rateS2632 = _M0L5paramS2633->$0;
  _M0L6lambdaS648 = _M0L4rateS2632 * _M0L2dtS649;
  if (_M0L6lambdaS648 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS650 = _M0L1sS647->$1;
  _M0L7_2abindS651 = _M0L7_2abindS650->$1;
  _M0L7_2abindS652 = _M0L7_2abindS650->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS652);
  _M0L2__S653 = 0;
  while (1) {
    if (_M0L2__S653 < _M0L7_2abindS651) {
      int32_t _M0L1nS654 = (int32_t)_M0L7_2abindS652[_M0L2__S653];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2630 = _M0L1sS647->$3;
      int32_t _M0L1kS655;
      int32_t _M0L6_2atmpS2631;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS655
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2630, _M0L6lambdaS648);
      if (_M0L1kS655 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS2622 = _M0L1sS647->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS2629 = _M0L1sS647->$2;
        float _M0L6_2atmpS2624;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2628;
        float _M0L2muS2626;
        float _M0L6_2atmpS2627;
        float _M0L6_2atmpS2625;
        float _M0L6_2atmpS2623;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS2624 = _M0MPC15array5Array2atGfE(_M0L1gS2629, _M0L1nS654);
        _M0L5paramS2628 = _M0L1sS647->$0;
        _M0L2muS2626 = _M0L5paramS2628->$1;
        _M0L6_2atmpS2627 = (float)_M0L1kS655;
        _M0L6_2atmpS2625 = _M0L2muS2626 * _M0L6_2atmpS2627;
        _M0L6_2atmpS2623 = _M0L6_2atmpS2624 + _M0L6_2atmpS2625;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS2622, _M0L1nS654, _M0L6_2atmpS2623);
      }
      _M0L6_2atmpS2631 = _M0L2__S653 + 1;
      _M0L2__S653 = _M0L6_2atmpS2631;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS652);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS630,
  float _M0L4timeS628,
  float _M0L2dtS633
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2618;
  int32_t _M0L6n__preS629;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2617;
  int32_t _M0L7n__postS631;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2616;
  float _M0L4rateS2615;
  float _M0L6lambdaS632;
  int32_t _M0L7_2abindS634;
  int32_t _M0L1iS635;
  moonbit_string_t _M0L3symS2612;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS637;
  int32_t _M0L7_2abindS638;
  int32_t _M0L1iS639;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS2618 = _M0L1sS630->$0;
  _M0L6n__preS629 = _M0L5paramS2618->$1;
  _M0L4postS2617 = _M0L1sS630->$1;
  _M0L7n__postS631 = _M0L4postS2617->$2;
  _M0L5paramS2616 = _M0L1sS630->$0;
  _M0L4rateS2615 = _M0L5paramS2616->$0;
  _M0L6lambdaS632 = _M0L4rateS2615 * _M0L2dtS633;
  _M0L7_2abindS634 = 0;
  _M0L1iS635 = _M0L7_2abindS634;
  while (1) {
    if (_M0L1iS635 < _M0L6n__preS629) {
      struct _M0TPB5ArrayGbE* _M0L4fireS2597 = _M0L1sS630->$3;
      int32_t _M0L6_2atmpS2598;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2597, _M0L1iS635, 0);
      _M0L6_2atmpS2598 = _M0L1iS635 + 1;
      _M0L1iS635 = _M0L6_2atmpS2598;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS632 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS2612 = _M0L1sS630->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS2612 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2612)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2612, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2612) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2613 = _M0L1sS630->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS4921 = _M0L4postS2613->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS4921);
    _M0L9g__targetS637 = _M0L8_2afieldS4921;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2614 = _M0L1sS630->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS4922 = _M0L4postS2614->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS4922);
    _M0L9g__targetS637 = _M0L8_2afieldS4922;
  }
  _M0L7_2abindS638 = 0;
  _M0L1iS639 = _M0L7_2abindS638;
  while (1) {
    if (_M0L1iS639 < _M0L6n__preS629) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2602 =
        _M0L1sS630->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS2601 = _M0L5paramS2602->$2;
      int32_t _M0L6_2atmpS2600;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2611;
      int32_t _M0L1kS642;
      int32_t _M0L6_2atmpS2599;
      moonbit_incref_cycle_free(_M0L6activeS2601);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS2600
      = _M0MPC15array5Array2atGbE(_M0L6activeS2601, _M0L1iS639);
      moonbit_decref_cycle_free(_M0L6activeS2601);
      if (!_M0L6_2atmpS2600) {
        goto join_640;
      }
      _M0L3rngS2611 = _M0L1sS630->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS642
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2611, _M0L6lambdaS632);
      if (_M0L1kS642 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2603 = _M0L1sS630->$3;
        int32_t _M0L7_2abindS643;
        int32_t _M0L1jS644;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2603, _M0L1iS639, 1);
        _M0L7_2abindS643 = 0;
        _M0L1jS644 = _M0L7_2abindS643;
        while (1) {
          if (_M0L1jS644 < _M0L7n__postS631) {
            int32_t _M0L6_2atmpS2609 = _M0L1jS644 * _M0L6n__preS629;
            int32_t _M0L3idxS645 = _M0L6_2atmpS2609 + _M0L1iS639;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS2604 = _M0L1sS630->$5;
            int32_t _M0L6_2atmpS2610;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS2604, _M0L3idxS645)
            ) {
              float _M0L6_2atmpS2606;
              struct _M0TPB5ArrayGfE* _M0L7weightsS2608;
              float _M0L6_2atmpS2607;
              float _M0L6_2atmpS2605;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS2606
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS637, _M0L1jS644);
              _M0L7weightsS2608 = _M0L1sS630->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS2607
              = _M0MPC15array5Array2atGfE(_M0L7weightsS2608, _M0L3idxS645);
              _M0L6_2atmpS2605 = _M0L6_2atmpS2606 + _M0L6_2atmpS2607;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS637, _M0L1jS644, _M0L6_2atmpS2605);
            }
            _M0L6_2atmpS2610 = _M0L1jS644 + 1;
            _M0L1jS644 = _M0L6_2atmpS2610;
            continue;
          }
          break;
        }
      }
      goto join_640;
      goto joinlet_5321;
      join_640:;
      _M0L6_2atmpS2599 = _M0L1iS639 + 1;
      _M0L1iS639 = _M0L6_2atmpS2599;
      continue;
      joinlet_5321:;
    } else {
      moonbit_decref_cycle_free(_M0L9g__targetS637);
    }
    break;
  }
  return 0;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS623
) {
  double _M0L2u1S622;
  double _M0L8u1__safeS624;
  double _M0L2u2S625;
  double _M0L6_2atmpS2596;
  double _M0L6_2atmpS2595;
  double _M0L1rS626;
  double _M0L5thetaS627;
  double _M0L6_2atmpS2594;
  double _M0L6_2atmpS2591;
  double _M0L6_2atmpS2593;
  double _M0L6_2atmpS2592;
  struct _M0TUddE* _block_5323;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S622 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS623);
  if (_M0L2u1S622 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS624 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS624 = _M0L2u1S622;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S625 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS623);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2596 = _M0FPC14math2ln(_M0L8u1__safeS624);
  _M0L6_2atmpS2595 = -0x1p+1 * _M0L6_2atmpS2596;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS626 = sqrt(_M0L6_2atmpS2595);
  _M0L5thetaS627 = 0x1.921fb54442d18p+2 * _M0L2u2S625;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2594 = _M0FPC14math3cos(_M0L5thetaS627);
  _M0L6_2atmpS2591 = _M0L1rS626 * _M0L6_2atmpS2594;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2593 = _M0FPC14math3sin(_M0L5thetaS627);
  _M0L6_2atmpS2592 = _M0L1rS626 * _M0L6_2atmpS2593;
  _block_5323 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_5323)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5323->$0 = _M0L6_2atmpS2591;
  _block_5323->$1 = _M0L6_2atmpS2592;
  return _block_5323;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS620,
  float _M0L6lambdaS614
) {
  float _M0L6_2atmpS2590;
  float _M0L6_2atmpS2589;
  double _M0L1lS615;
  struct _M0TPB8MutLocalGdE* _M0L1pS616;
  struct _M0TPB8MutLocalGiE* _M0L1kS617;
  float _M0L6_2atmpS2588;
  int32_t _M0L8ten__lamS619;
  int32_t _M0L3capS618;
  int32_t _M0L3valS2587;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS614 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS2590 = -_M0L6lambdaS614;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2589 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2590);
  _M0L1lS615 = (double)_M0L6_2atmpS2589;
  _M0L1pS616
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS616)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS616->$0 = 0x1p+0;
  _M0L1kS617
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS617)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS617->$0 = 0;
  _M0L6_2atmpS2588 = _M0L6lambdaS614 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS619 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2588);
  if (_M0L8ten__lamS619 > 100) {
    _M0L3capS618 = _M0L8ten__lamS619;
  } else {
    _M0L3capS618 = 100;
  }
  while (1) {
    int32_t _M0L3valS2579 = _M0L1kS617->$0;
    int32_t _M0L6_2atmpS2578 = _M0L3valS2579 + 1;
    double _M0L3valS2581;
    double _M0L6_2atmpS2582;
    double _M0L6_2atmpS2580;
    double _M0L3valS2583;
    int32_t _M0L3valS2585;
    _M0L1kS617->$0 = _M0L6_2atmpS2578;
    _M0L3valS2581 = _M0L1pS616->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS2582 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS620);
    _M0L6_2atmpS2580 = _M0L3valS2581 * _M0L6_2atmpS2582;
    _M0L1pS616->$0 = _M0L6_2atmpS2580;
    _M0L3valS2583 = _M0L1pS616->$0;
    if (_M0L3valS2583 < _M0L1lS615) {
      int32_t _M0L3valS2584;
      moonbit_decref_cycle_free(_M0L1pS616);
      _M0L3valS2584 = _M0L1kS617->$0;
      moonbit_decref_cycle_free(_M0L1kS617);
      return _M0L3valS2584 - 1;
    }
    _M0L3valS2585 = _M0L1kS617->$0;
    if (_M0L3valS2585 > _M0L3capS618) {
      int32_t _M0L3valS2586;
      moonbit_decref_cycle_free(_M0L1pS616);
      _M0L3valS2586 = _M0L1kS617->$0;
      moonbit_decref_cycle_free(_M0L1kS617);
      return _M0L3valS2586 - 1;
    }
    continue;
    break;
  }
  _M0L3valS2587 = _M0L1kS617->$0;
  moonbit_decref_cycle_free(_M0L1kS617);
  return _M0L3valS2587 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS612
) {
  uint64_t _M0L1uS611;
  uint64_t _M0L4bitsS613;
  double _M0L6_2atmpS2577;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS611 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS612);
  _M0L4bitsS613 = _M0L1uS611 >> 11;
  _M0L6_2atmpS2577 = (double)_M0L4bitsS613;
  return _M0L6_2atmpS2577 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS605,
  float _M0L1tS607,
  float _M0L1wS609
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS604;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS604
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS604)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS604->$0 = 0;
  while (1) {
    int32_t _M0L3valS2539 = _M0L1iS604->$0;
    int32_t _M0L1nS2540 = _M0L1sS605->$0;
    if (_M0L3valS2539 < _M0L1nS2540) {
      struct _M0TPB5ArrayGbE* _M0L4fireS2541 = _M0L1sS605->$4;
      int32_t _M0L3valS2542 = _M0L1iS604->$0;
      int32_t _M0L3valS2544;
      int32_t _M0L6_2atmpS2543;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2541, _M0L3valS2542, 0);
      _M0L3valS2544 = _M0L1iS604->$0;
      _M0L6_2atmpS2543 = _M0L3valS2544 + 1;
      _M0L1iS604->$0 = _M0L6_2atmpS2543;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS604);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS2548 = _M0L1sS605->$3;
    int32_t _M0L6_2atmpS2547;
    int32_t _if__result_5327;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS2547 = _M0MPC15array5Array2atGiE(_M0L11next__indexS2548, 0);
    if (_M0L6_2atmpS2547 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS2546 = _M0L1sS605->$2;
      float _M0L6_2atmpS2545;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2545 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS2546, 0);
      _if__result_5327 = _M0L6_2atmpS2545 <= _M0L1tS607;
    } else {
      _if__result_5327 = 0;
    }
    if (_if__result_5327) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS2576 =
        _M0L1sS605->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS2573 = _M0L5paramS2576->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS2575 = _M0L1sS605->$3;
      int32_t _M0L6_2atmpS2574;
      int32_t _M0L1jS608;
      struct _M0TPB5ArrayGbE* _M0L4fireS2549;
      struct _M0TPB5ArrayGfE* _M0L1gS2550;
      struct _M0TPB5ArrayGfE* _M0L1gS2553;
      float _M0L6_2atmpS2552;
      float _M0L6_2atmpS2551;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS2559;
      int32_t _M0L6_2atmpS2558;
      int32_t _M0L6_2atmpS2554;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS2557;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS2556;
      int32_t _M0L6_2atmpS2555;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2574 = _M0MPC15array5Array2atGiE(_M0L11next__indexS2575, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS608
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS2573, _M0L6_2atmpS2574);
      _M0L4fireS2549 = _M0L1sS605->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2549, _M0L1jS608, 1);
      _M0L1gS2550 = _M0L1sS605->$5;
      _M0L1gS2553 = _M0L1sS605->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2552 = _M0MPC15array5Array2atGfE(_M0L1gS2553, _M0L1jS608);
      _M0L6_2atmpS2551 = _M0L6_2atmpS2552 + _M0L1wS609;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS2550, _M0L1jS608, _M0L6_2atmpS2551);
      _M0L11next__indexS2559 = _M0L1sS605->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2558 = _M0MPC15array5Array2atGiE(_M0L11next__indexS2559, 0);
      _M0L6_2atmpS2554 = _M0L6_2atmpS2558 + 1;
      _M0L5paramS2557 = _M0L1sS605->$1;
      _M0L10spiketimesS2556 = _M0L5paramS2557->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2555 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS2556);
      if (_M0L6_2atmpS2554 < _M0L6_2atmpS2555) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS2560 = _M0L1sS605->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS2563 = _M0L1sS605->$3;
        int32_t _M0L6_2atmpS2562;
        int32_t _M0L6_2atmpS2561;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS2564;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS2569;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS2566;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS2568;
        int32_t _M0L6_2atmpS2567;
        float _M0L6_2atmpS2565;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS2562
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS2563, 0);
        _M0L6_2atmpS2561 = _M0L6_2atmpS2562 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS2560, 0, _M0L6_2atmpS2561);
        _M0L11next__spikeS2564 = _M0L1sS605->$2;
        _M0L5paramS2569 = _M0L1sS605->$1;
        _M0L10spiketimesS2566 = _M0L5paramS2569->$0;
        _M0L11next__indexS2568 = _M0L1sS605->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS2567
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS2568, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS2565
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS2566, _M0L6_2atmpS2567);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS2564, 0, _M0L6_2atmpS2565);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS2570 = _M0L1sS605->$2;
        float _M0L6_2atmpS2571 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS2572;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS2570, 0, _M0L6_2atmpS2571);
        _M0L11next__indexS2572 = _M0L1sS605->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS2572, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS591,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS589,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS593,
  float _M0L6t__nowS588,
  float _M0L2dtS598
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2452;
  int32_t _M0L6_2atmpS2451;
  int32_t _if__result_5328;
  int32_t _M0L6n__preS590;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2454;
  int32_t _M0L6_2atmpS2453;
  float _M0L11u__baselineS592;
  float _M0L6tau__fS2538;
  float _M0L11inv__tau__fS594;
  float _M0L6tau__dS2537;
  float _M0L11inv__tau__dS595;
  struct _M0TPB8MutLocalGiE* _M0L1jS596;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2452 = _M0L4varsS589->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2451 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2452);
  if (_M0L6_2atmpS2451 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2450 = _M0L4varsS589->$6;
    int32_t _M0L6_2atmpS2449;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2449 = _M0MPC15array5Array2atGbE(_M0L6activeS2450, 0);
    _if__result_5328 = !_M0L6_2atmpS2449;
  } else {
    _if__result_5328 = 0;
  }
  if (_if__result_5328) {
    return 0;
  }
  _M0L6n__preS590 = _M0L4varsS589->$0;
  _M0L3rhoS2454 = _M0L3synS591->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2453 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2454);
  if (_M0L6_2atmpS2453 == 0) {
    return 0;
  }
  _M0L11u__baselineS592 = _M0L5paramS593->$0;
  _M0L6tau__fS2538 = _M0L5paramS593->$1;
  _M0L11inv__tau__fS594 = 0x1p+0f / _M0L6tau__fS2538;
  _M0L6tau__dS2537 = _M0L5paramS593->$2;
  _M0L11inv__tau__dS595 = 0x1p+0f / _M0L6tau__dS2537;
  _M0L1jS596
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS596)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS596->$0 = 0;
  while (1) {
    int32_t _M0L3valS2455 = _M0L1jS596->$0;
    if (_M0L3valS2455 < _M0L6n__preS590) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2458 = _M0L3synS591->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2456 = _M0L3preS2458->$5;
      int32_t _M0L3valS2457 = _M0L1jS596->$0;
      int32_t _M0L3valS2485;
      int32_t _M0L6_2atmpS2484;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2456, _M0L3valS2457)) {
        struct _M0TPB5ArrayGfE* _M0L1uS2459 = _M0L4varsS589->$2;
        int32_t _M0L3valS2460 = _M0L1jS596->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS2468 = _M0L4varsS589->$2;
        int32_t _M0L3valS2469 = _M0L1jS596->$0;
        float _M0L6_2atmpS2462;
        struct _M0TPB5ArrayGfE* _M0L1uS2466;
        int32_t _M0L3valS2467;
        float _M0L6_2atmpS2465;
        float _M0L6_2atmpS2464;
        float _M0L6_2atmpS2463;
        float _M0L6_2atmpS2461;
        struct _M0TPB5ArrayGfE* _M0L1xS2470;
        int32_t _M0L3valS2471;
        struct _M0TPB5ArrayGfE* _M0L1xS2482;
        int32_t _M0L3valS2483;
        float _M0L6_2atmpS2473;
        struct _M0TPB5ArrayGfE* _M0L1uS2480;
        int32_t _M0L3valS2481;
        float _M0L6_2atmpS2479;
        float _M0L6_2atmpS2475;
        struct _M0TPB5ArrayGfE* _M0L1xS2477;
        int32_t _M0L3valS2478;
        float _M0L6_2atmpS2476;
        float _M0L6_2atmpS2474;
        float _M0L6_2atmpS2472;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2462
        = _M0MPC15array5Array2atGfE(_M0L1uS2468, _M0L3valS2469);
        _M0L1uS2466 = _M0L4varsS589->$2;
        _M0L3valS2467 = _M0L1jS596->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2465
        = _M0MPC15array5Array2atGfE(_M0L1uS2466, _M0L3valS2467);
        _M0L6_2atmpS2464 = 0x1p+0f - _M0L6_2atmpS2465;
        _M0L6_2atmpS2463 = _M0L11u__baselineS592 * _M0L6_2atmpS2464;
        _M0L6_2atmpS2461 = _M0L6_2atmpS2462 + _M0L6_2atmpS2463;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2459, _M0L3valS2460, _M0L6_2atmpS2461);
        _M0L1xS2470 = _M0L4varsS589->$3;
        _M0L3valS2471 = _M0L1jS596->$0;
        _M0L1xS2482 = _M0L4varsS589->$3;
        _M0L3valS2483 = _M0L1jS596->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2473
        = _M0MPC15array5Array2atGfE(_M0L1xS2482, _M0L3valS2483);
        _M0L1uS2480 = _M0L4varsS589->$2;
        _M0L3valS2481 = _M0L1jS596->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2479
        = _M0MPC15array5Array2atGfE(_M0L1uS2480, _M0L3valS2481);
        _M0L6_2atmpS2475 = -_M0L6_2atmpS2479;
        _M0L1xS2477 = _M0L4varsS589->$3;
        _M0L3valS2478 = _M0L1jS596->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2476
        = _M0MPC15array5Array2atGfE(_M0L1xS2477, _M0L3valS2478);
        _M0L6_2atmpS2474 = _M0L6_2atmpS2475 * _M0L6_2atmpS2476;
        _M0L6_2atmpS2472 = _M0L6_2atmpS2473 + _M0L6_2atmpS2474;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2470, _M0L3valS2471, _M0L6_2atmpS2472);
      }
      _M0L3valS2485 = _M0L1jS596->$0;
      _M0L6_2atmpS2484 = _M0L3valS2485 + 1;
      _M0L1jS596->$0 = _M0L6_2atmpS2484;
      continue;
    }
    break;
  }
  _M0L1jS596->$0 = 0;
  while (1) {
    int32_t _M0L3valS2486 = _M0L1jS596->$0;
    if (_M0L3valS2486 < _M0L6n__preS590) {
      struct _M0TPB5ArrayGfE* _M0L1uS2487 = _M0L4varsS589->$2;
      int32_t _M0L3valS2488 = _M0L1jS596->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS2497 = _M0L4varsS589->$2;
      int32_t _M0L3valS2498 = _M0L1jS596->$0;
      float _M0L6_2atmpS2490;
      struct _M0TPB5ArrayGfE* _M0L1uS2495;
      int32_t _M0L3valS2496;
      float _M0L6_2atmpS2494;
      float _M0L6_2atmpS2493;
      float _M0L6_2atmpS2492;
      float _M0L6_2atmpS2491;
      float _M0L6_2atmpS2489;
      struct _M0TPB5ArrayGfE* _M0L1xS2499;
      int32_t _M0L3valS2500;
      struct _M0TPB5ArrayGfE* _M0L1xS2509;
      int32_t _M0L3valS2510;
      float _M0L6_2atmpS2502;
      struct _M0TPB5ArrayGfE* _M0L1xS2507;
      int32_t _M0L3valS2508;
      float _M0L6_2atmpS2506;
      float _M0L6_2atmpS2505;
      float _M0L6_2atmpS2504;
      float _M0L6_2atmpS2503;
      float _M0L6_2atmpS2501;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS2511;
      int32_t _M0L3valS2512;
      struct _M0TPB5ArrayGfE* _M0L1uS2518;
      int32_t _M0L3valS2519;
      float _M0L6_2atmpS2514;
      struct _M0TPB5ArrayGfE* _M0L1xS2516;
      int32_t _M0L3valS2517;
      float _M0L6_2atmpS2515;
      float _M0L6_2atmpS2513;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2536;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2534;
      int32_t _M0L3valS2535;
      int32_t _M0L5startS599;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2533;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2530;
      int32_t _M0L3valS2532;
      int32_t _M0L6_2atmpS2531;
      int32_t _M0L3endS600;
      struct _M0TPB8MutLocalGiE* _M0L1sS601;
      int32_t _M0L3valS2529;
      int32_t _M0L6_2atmpS2528;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2490
      = _M0MPC15array5Array2atGfE(_M0L1uS2497, _M0L3valS2498);
      _M0L1uS2495 = _M0L4varsS589->$2;
      _M0L3valS2496 = _M0L1jS596->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2494
      = _M0MPC15array5Array2atGfE(_M0L1uS2495, _M0L3valS2496);
      _M0L6_2atmpS2493 = _M0L11u__baselineS592 - _M0L6_2atmpS2494;
      _M0L6_2atmpS2492 = _M0L2dtS598 * _M0L6_2atmpS2493;
      _M0L6_2atmpS2491 = _M0L6_2atmpS2492 * _M0L11inv__tau__fS594;
      _M0L6_2atmpS2489 = _M0L6_2atmpS2490 + _M0L6_2atmpS2491;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS2487, _M0L3valS2488, _M0L6_2atmpS2489);
      _M0L1xS2499 = _M0L4varsS589->$3;
      _M0L3valS2500 = _M0L1jS596->$0;
      _M0L1xS2509 = _M0L4varsS589->$3;
      _M0L3valS2510 = _M0L1jS596->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2502
      = _M0MPC15array5Array2atGfE(_M0L1xS2509, _M0L3valS2510);
      _M0L1xS2507 = _M0L4varsS589->$3;
      _M0L3valS2508 = _M0L1jS596->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2506
      = _M0MPC15array5Array2atGfE(_M0L1xS2507, _M0L3valS2508);
      _M0L6_2atmpS2505 = 0x1p+0f - _M0L6_2atmpS2506;
      _M0L6_2atmpS2504 = _M0L2dtS598 * _M0L6_2atmpS2505;
      _M0L6_2atmpS2503 = _M0L6_2atmpS2504 * _M0L11inv__tau__dS595;
      _M0L6_2atmpS2501 = _M0L6_2atmpS2502 + _M0L6_2atmpS2503;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS2499, _M0L3valS2500, _M0L6_2atmpS2501);
      _M0L8rho__preS2511 = _M0L4varsS589->$4;
      _M0L3valS2512 = _M0L1jS596->$0;
      _M0L1uS2518 = _M0L4varsS589->$2;
      _M0L3valS2519 = _M0L1jS596->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2514
      = _M0MPC15array5Array2atGfE(_M0L1uS2518, _M0L3valS2519);
      _M0L1xS2516 = _M0L4varsS589->$3;
      _M0L3valS2517 = _M0L1jS596->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2515
      = _M0MPC15array5Array2atGfE(_M0L1xS2516, _M0L3valS2517);
      _M0L6_2atmpS2513 = _M0L6_2atmpS2514 * _M0L6_2atmpS2515;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS2511, _M0L3valS2512, _M0L6_2atmpS2513);
      _M0L6matrixS2536 = _M0L3synS591->$4;
      _M0L6rowptrS2534 = _M0L6matrixS2536->$2;
      _M0L3valS2535 = _M0L1jS596->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS599
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2534, _M0L3valS2535);
      _M0L6matrixS2533 = _M0L3synS591->$4;
      _M0L6rowptrS2530 = _M0L6matrixS2533->$2;
      _M0L3valS2532 = _M0L1jS596->$0;
      _M0L6_2atmpS2531 = _M0L3valS2532 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS600
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2530, _M0L6_2atmpS2531);
      _M0L1sS601
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS601)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS601->$0 = _M0L5startS599;
      while (1) {
        int32_t _M0L3valS2520 = _M0L1sS601->$0;
        if (_M0L3valS2520 < _M0L3endS600) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS2521 = _M0L3synS591->$6;
          int32_t _M0L3valS2522 = _M0L1sS601->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS2524 = _M0L4varsS589->$4;
          int32_t _M0L3valS2525 = _M0L1jS596->$0;
          float _M0L6_2atmpS2523;
          int32_t _M0L3valS2527;
          int32_t _M0L6_2atmpS2526;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS2523
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS2524, _M0L3valS2525);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS2521, _M0L3valS2522, _M0L6_2atmpS2523);
          _M0L3valS2527 = _M0L1sS601->$0;
          _M0L6_2atmpS2526 = _M0L3valS2527 + 1;
          _M0L1sS601->$0 = _M0L6_2atmpS2526;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS601);
        }
        break;
      }
      _M0L3valS2529 = _M0L1jS596->$0;
      _M0L6_2atmpS2528 = _M0L3valS2529 + 1;
      _M0L1jS596->$0 = _M0L6_2atmpS2528;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS596);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23markram__stp__step__het(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS572,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS570,
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS575,
  float _M0L6t__nowS579
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2360;
  int32_t _M0L6_2atmpS2359;
  int32_t _if__result_5332;
  int32_t _M0L6n__preS571;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2362;
  int32_t _M0L6_2atmpS2361;
  struct _M0TPB8MutLocalGiE* _M0L1jS573;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2360 = _M0L4varsS570->$6;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2359 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2360);
  if (_M0L6_2atmpS2359 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2358 = _M0L4varsS570->$6;
    int32_t _M0L6_2atmpS2357;
    #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2357 = _M0MPC15array5Array2atGbE(_M0L6activeS2358, 0);
    _if__result_5332 = !_M0L6_2atmpS2357;
  } else {
    _if__result_5332 = 0;
  }
  if (_if__result_5332) {
    return 0;
  }
  _M0L6n__preS571 = _M0L4varsS570->$0;
  _M0L3rhoS2362 = _M0L3synS572->$6;
  #line 357 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2361 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2362);
  if (_M0L6_2atmpS2361 == 0) {
    return 0;
  }
  _M0L1jS573
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS573)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS573->$0 = 0;
  while (1) {
    int32_t _M0L3valS2363 = _M0L1jS573->$0;
    if (_M0L3valS2363 < _M0L6n__preS571) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2366 = _M0L3synS572->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2364 = _M0L3preS2366->$5;
      int32_t _M0L3valS2365 = _M0L1jS573->$0;
      int32_t _M0L3valS2448;
      int32_t _M0L6_2atmpS2447;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2364, _M0L3valS2365)) {
        struct _M0TPB5ArrayGfE* _M0L6tau__dS2445 = _M0L5paramS575->$0;
        int32_t _M0L3valS2446 = _M0L1jS573->$0;
        float _M0L9tau__d__jS574;
        struct _M0TPB5ArrayGfE* _M0L6tau__fS2443;
        int32_t _M0L3valS2444;
        float _M0L9tau__f__jS576;
        struct _M0TPB5ArrayGfE* _M0L1uS2441;
        int32_t _M0L3valS2442;
        float _M0L14u__baseline__jS577;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2439;
        int32_t _M0L3valS2440;
        float _M0L6_2atmpS2438;
        float _M0L7dt__preS578;
        float _M0L7dt__preS580;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2367;
        int32_t _M0L3valS2368;
        float _M0L6_2atmpS2437;
        float _M0L6arg__fS581;
        struct _M0TPB5ArrayGfE* _M0L1uS2369;
        int32_t _M0L3valS2370;
        struct _M0TPB5ArrayGfE* _M0L1uS2376;
        int32_t _M0L3valS2377;
        float _M0L6_2atmpS2375;
        float _M0L6_2atmpS2373;
        float _M0L6_2atmpS2374;
        float _M0L6_2atmpS2372;
        float _M0L6_2atmpS2371;
        float _M0L6_2atmpS2436;
        float _M0L6arg__dS582;
        struct _M0TPB5ArrayGfE* _M0L1xS2378;
        int32_t _M0L3valS2379;
        struct _M0TPB5ArrayGfE* _M0L1xS2385;
        int32_t _M0L3valS2386;
        float _M0L6_2atmpS2384;
        float _M0L6_2atmpS2382;
        float _M0L6_2atmpS2383;
        float _M0L6_2atmpS2381;
        float _M0L6_2atmpS2380;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS2387;
        int32_t _M0L3valS2388;
        struct _M0TPB5ArrayGfE* _M0L1uS2394;
        int32_t _M0L3valS2395;
        float _M0L6_2atmpS2390;
        struct _M0TPB5ArrayGfE* _M0L1xS2392;
        int32_t _M0L3valS2393;
        float _M0L6_2atmpS2391;
        float _M0L6_2atmpS2389;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2435;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2433;
        int32_t _M0L3valS2434;
        int32_t _M0L5startS583;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2432;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2429;
        int32_t _M0L3valS2431;
        int32_t _M0L6_2atmpS2430;
        int32_t _M0L3endS584;
        struct _M0TPB8MutLocalGiE* _M0L1sS585;
        struct _M0TPB5ArrayGfE* _M0L1uS2404;
        int32_t _M0L3valS2405;
        struct _M0TPB5ArrayGfE* _M0L1uS2413;
        int32_t _M0L3valS2414;
        float _M0L6_2atmpS2407;
        struct _M0TPB5ArrayGfE* _M0L1uS2411;
        int32_t _M0L3valS2412;
        float _M0L6_2atmpS2410;
        float _M0L6_2atmpS2409;
        float _M0L6_2atmpS2408;
        float _M0L6_2atmpS2406;
        struct _M0TPB5ArrayGfE* _M0L1xS2415;
        int32_t _M0L3valS2416;
        struct _M0TPB5ArrayGfE* _M0L1xS2427;
        int32_t _M0L3valS2428;
        float _M0L6_2atmpS2418;
        struct _M0TPB5ArrayGfE* _M0L1uS2425;
        int32_t _M0L3valS2426;
        float _M0L6_2atmpS2424;
        float _M0L6_2atmpS2420;
        struct _M0TPB5ArrayGfE* _M0L1xS2422;
        int32_t _M0L3valS2423;
        float _M0L6_2atmpS2421;
        float _M0L6_2atmpS2419;
        float _M0L6_2atmpS2417;
        #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__d__jS574
        = _M0MPC15array5Array2atGfE(_M0L6tau__dS2445, _M0L3valS2446);
        _M0L6tau__fS2443 = _M0L5paramS575->$1;
        _M0L3valS2444 = _M0L1jS573->$0;
        #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__f__jS576
        = _M0MPC15array5Array2atGfE(_M0L6tau__fS2443, _M0L3valS2444);
        _M0L1uS2441 = _M0L5paramS575->$2;
        _M0L3valS2442 = _M0L1jS573->$0;
        #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L14u__baseline__jS577
        = _M0MPC15array5Array2atGfE(_M0L1uS2441, _M0L3valS2442);
        _M0L11last__spikeS2439 = _M0L4varsS570->$5;
        _M0L3valS2440 = _M0L1jS573->$0;
        #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2438
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS2439, _M0L3valS2440);
        _M0L7dt__preS578 = _M0L6t__nowS579 - _M0L6_2atmpS2438;
        if (_M0L7dt__preS578 < 0x0p+0f) {
          _M0L7dt__preS580 = 0x0p+0f;
        } else {
          _M0L7dt__preS580 = _M0L7dt__preS578;
        }
        _M0L11last__spikeS2367 = _M0L4varsS570->$5;
        _M0L3valS2368 = _M0L1jS573->$0;
        #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS2367, _M0L3valS2368, _M0L6t__nowS579);
        _M0L6_2atmpS2437 = -_M0L7dt__preS580;
        _M0L6arg__fS581 = _M0L6_2atmpS2437 / _M0L9tau__f__jS576;
        _M0L1uS2369 = _M0L4varsS570->$2;
        _M0L3valS2370 = _M0L1jS573->$0;
        _M0L1uS2376 = _M0L4varsS570->$2;
        _M0L3valS2377 = _M0L1jS573->$0;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2375
        = _M0MPC15array5Array2atGfE(_M0L1uS2376, _M0L3valS2377);
        _M0L6_2atmpS2373 = _M0L14u__baseline__jS577 - _M0L6_2atmpS2375;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2374 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS581);
        _M0L6_2atmpS2372 = _M0L6_2atmpS2373 * _M0L6_2atmpS2374;
        _M0L6_2atmpS2371 = _M0L14u__baseline__jS577 - _M0L6_2atmpS2372;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2369, _M0L3valS2370, _M0L6_2atmpS2371);
        _M0L6_2atmpS2436 = -_M0L7dt__preS580;
        _M0L6arg__dS582 = _M0L6_2atmpS2436 / _M0L9tau__d__jS574;
        _M0L1xS2378 = _M0L4varsS570->$3;
        _M0L3valS2379 = _M0L1jS573->$0;
        _M0L1xS2385 = _M0L4varsS570->$3;
        _M0L3valS2386 = _M0L1jS573->$0;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2384
        = _M0MPC15array5Array2atGfE(_M0L1xS2385, _M0L3valS2386);
        _M0L6_2atmpS2382 = 0x1p+0f - _M0L6_2atmpS2384;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2383 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS582);
        _M0L6_2atmpS2381 = _M0L6_2atmpS2382 * _M0L6_2atmpS2383;
        _M0L6_2atmpS2380 = 0x1p+0f - _M0L6_2atmpS2381;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2378, _M0L3valS2379, _M0L6_2atmpS2380);
        _M0L8rho__preS2387 = _M0L4varsS570->$4;
        _M0L3valS2388 = _M0L1jS573->$0;
        _M0L1uS2394 = _M0L4varsS570->$2;
        _M0L3valS2395 = _M0L1jS573->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2390
        = _M0MPC15array5Array2atGfE(_M0L1uS2394, _M0L3valS2395);
        _M0L1xS2392 = _M0L4varsS570->$3;
        _M0L3valS2393 = _M0L1jS573->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2391
        = _M0MPC15array5Array2atGfE(_M0L1xS2392, _M0L3valS2393);
        _M0L6_2atmpS2389 = _M0L6_2atmpS2390 * _M0L6_2atmpS2391;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS2387, _M0L3valS2388, _M0L6_2atmpS2389);
        _M0L6matrixS2435 = _M0L3synS572->$4;
        _M0L6rowptrS2433 = _M0L6matrixS2435->$2;
        _M0L3valS2434 = _M0L1jS573->$0;
        #line 374 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS583
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2433, _M0L3valS2434);
        _M0L6matrixS2432 = _M0L3synS572->$4;
        _M0L6rowptrS2429 = _M0L6matrixS2432->$2;
        _M0L3valS2431 = _M0L1jS573->$0;
        _M0L6_2atmpS2430 = _M0L3valS2431 + 1;
        #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS584
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2429, _M0L6_2atmpS2430);
        _M0L1sS585
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS585)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS585->$0 = _M0L5startS583;
        while (1) {
          int32_t _M0L3valS2396 = _M0L1sS585->$0;
          if (_M0L3valS2396 < _M0L3endS584) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS2397 = _M0L3synS572->$6;
            int32_t _M0L3valS2398 = _M0L1sS585->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS2400 = _M0L4varsS570->$4;
            int32_t _M0L3valS2401 = _M0L1jS573->$0;
            float _M0L6_2atmpS2399;
            int32_t _M0L3valS2403;
            int32_t _M0L6_2atmpS2402;
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS2399
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS2400, _M0L3valS2401);
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS2397, _M0L3valS2398, _M0L6_2atmpS2399);
            _M0L3valS2403 = _M0L1sS585->$0;
            _M0L6_2atmpS2402 = _M0L3valS2403 + 1;
            _M0L1sS585->$0 = _M0L6_2atmpS2402;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS585);
          }
          break;
        }
        _M0L1uS2404 = _M0L4varsS570->$2;
        _M0L3valS2405 = _M0L1jS573->$0;
        _M0L1uS2413 = _M0L4varsS570->$2;
        _M0L3valS2414 = _M0L1jS573->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2407
        = _M0MPC15array5Array2atGfE(_M0L1uS2413, _M0L3valS2414);
        _M0L1uS2411 = _M0L4varsS570->$2;
        _M0L3valS2412 = _M0L1jS573->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2410
        = _M0MPC15array5Array2atGfE(_M0L1uS2411, _M0L3valS2412);
        _M0L6_2atmpS2409 = 0x1p+0f - _M0L6_2atmpS2410;
        _M0L6_2atmpS2408 = _M0L14u__baseline__jS577 * _M0L6_2atmpS2409;
        _M0L6_2atmpS2406 = _M0L6_2atmpS2407 + _M0L6_2atmpS2408;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2404, _M0L3valS2405, _M0L6_2atmpS2406);
        _M0L1xS2415 = _M0L4varsS570->$3;
        _M0L3valS2416 = _M0L1jS573->$0;
        _M0L1xS2427 = _M0L4varsS570->$3;
        _M0L3valS2428 = _M0L1jS573->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2418
        = _M0MPC15array5Array2atGfE(_M0L1xS2427, _M0L3valS2428);
        _M0L1uS2425 = _M0L4varsS570->$2;
        _M0L3valS2426 = _M0L1jS573->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2424
        = _M0MPC15array5Array2atGfE(_M0L1uS2425, _M0L3valS2426);
        _M0L6_2atmpS2420 = -_M0L6_2atmpS2424;
        _M0L1xS2422 = _M0L4varsS570->$3;
        _M0L3valS2423 = _M0L1jS573->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2421
        = _M0MPC15array5Array2atGfE(_M0L1xS2422, _M0L3valS2423);
        _M0L6_2atmpS2419 = _M0L6_2atmpS2420 * _M0L6_2atmpS2421;
        _M0L6_2atmpS2417 = _M0L6_2atmpS2418 + _M0L6_2atmpS2419;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2415, _M0L3valS2416, _M0L6_2atmpS2417);
      }
      _M0L3valS2448 = _M0L1jS573->$0;
      _M0L6_2atmpS2447 = _M0L3valS2448 + 1;
      _M0L1jS573->$0 = _M0L6_2atmpS2447;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS573);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18markram__stp__step(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS554,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS552,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS556,
  float _M0L6t__nowS561
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2274;
  int32_t _M0L6_2atmpS2273;
  int32_t _if__result_5335;
  int32_t _M0L6n__preS553;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2276;
  int32_t _M0L6_2atmpS2275;
  float _M0L6tau__fS555;
  float _M0L6tau__dS557;
  float _M0L11u__baselineS558;
  struct _M0TPB8MutLocalGiE* _M0L1jS559;
  #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2274 = _M0L4varsS552->$6;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2273 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2274);
  if (_M0L6_2atmpS2273 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2272 = _M0L4varsS552->$6;
    int32_t _M0L6_2atmpS2271;
    #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2271 = _M0MPC15array5Array2atGbE(_M0L6activeS2272, 0);
    _if__result_5335 = !_M0L6_2atmpS2271;
  } else {
    _if__result_5335 = 0;
  }
  if (_if__result_5335) {
    return 0;
  }
  _M0L6n__preS553 = _M0L4varsS552->$0;
  _M0L3rhoS2276 = _M0L3synS554->$6;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2275 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2276);
  if (_M0L6_2atmpS2275 == 0) {
    return 0;
  }
  _M0L6tau__fS555 = _M0L5paramS556->$1;
  _M0L6tau__dS557 = _M0L5paramS556->$0;
  _M0L11u__baselineS558 = _M0L5paramS556->$2;
  _M0L1jS559
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS559)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS559->$0 = 0;
  while (1) {
    int32_t _M0L3valS2277 = _M0L1jS559->$0;
    if (_M0L3valS2277 < _M0L6n__preS553) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2280 = _M0L3synS554->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2278 = _M0L3preS2280->$5;
      int32_t _M0L3valS2279 = _M0L1jS559->$0;
      int32_t _M0L3valS2356;
      int32_t _M0L6_2atmpS2355;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2278, _M0L3valS2279)) {
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2353 = _M0L4varsS552->$5;
        int32_t _M0L3valS2354 = _M0L1jS559->$0;
        float _M0L6_2atmpS2352;
        float _M0L7dt__preS560;
        float _M0L7dt__preS562;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2281;
        int32_t _M0L3valS2282;
        float _M0L6_2atmpS2351;
        float _M0L6arg__fS563;
        struct _M0TPB5ArrayGfE* _M0L1uS2283;
        int32_t _M0L3valS2284;
        struct _M0TPB5ArrayGfE* _M0L1uS2290;
        int32_t _M0L3valS2291;
        float _M0L6_2atmpS2289;
        float _M0L6_2atmpS2287;
        float _M0L6_2atmpS2288;
        float _M0L6_2atmpS2286;
        float _M0L6_2atmpS2285;
        float _M0L6_2atmpS2350;
        float _M0L6arg__dS564;
        struct _M0TPB5ArrayGfE* _M0L1xS2292;
        int32_t _M0L3valS2293;
        struct _M0TPB5ArrayGfE* _M0L1xS2299;
        int32_t _M0L3valS2300;
        float _M0L6_2atmpS2298;
        float _M0L6_2atmpS2296;
        float _M0L6_2atmpS2297;
        float _M0L6_2atmpS2295;
        float _M0L6_2atmpS2294;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS2301;
        int32_t _M0L3valS2302;
        struct _M0TPB5ArrayGfE* _M0L1uS2308;
        int32_t _M0L3valS2309;
        float _M0L6_2atmpS2304;
        struct _M0TPB5ArrayGfE* _M0L1xS2306;
        int32_t _M0L3valS2307;
        float _M0L6_2atmpS2305;
        float _M0L6_2atmpS2303;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2349;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2347;
        int32_t _M0L3valS2348;
        int32_t _M0L5startS565;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2346;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2343;
        int32_t _M0L3valS2345;
        int32_t _M0L6_2atmpS2344;
        int32_t _M0L3endS566;
        struct _M0TPB8MutLocalGiE* _M0L1sS567;
        struct _M0TPB5ArrayGfE* _M0L1uS2318;
        int32_t _M0L3valS2319;
        struct _M0TPB5ArrayGfE* _M0L1uS2327;
        int32_t _M0L3valS2328;
        float _M0L6_2atmpS2321;
        struct _M0TPB5ArrayGfE* _M0L1uS2325;
        int32_t _M0L3valS2326;
        float _M0L6_2atmpS2324;
        float _M0L6_2atmpS2323;
        float _M0L6_2atmpS2322;
        float _M0L6_2atmpS2320;
        struct _M0TPB5ArrayGfE* _M0L1xS2329;
        int32_t _M0L3valS2330;
        struct _M0TPB5ArrayGfE* _M0L1xS2341;
        int32_t _M0L3valS2342;
        float _M0L6_2atmpS2332;
        struct _M0TPB5ArrayGfE* _M0L1uS2339;
        int32_t _M0L3valS2340;
        float _M0L6_2atmpS2338;
        float _M0L6_2atmpS2334;
        struct _M0TPB5ArrayGfE* _M0L1xS2336;
        int32_t _M0L3valS2337;
        float _M0L6_2atmpS2335;
        float _M0L6_2atmpS2333;
        float _M0L6_2atmpS2331;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2352
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS2353, _M0L3valS2354);
        _M0L7dt__preS560 = _M0L6t__nowS561 - _M0L6_2atmpS2352;
        if (_M0L7dt__preS560 < 0x0p+0f) {
          _M0L7dt__preS562 = 0x0p+0f;
        } else {
          _M0L7dt__preS562 = _M0L7dt__preS560;
        }
        _M0L11last__spikeS2281 = _M0L4varsS552->$5;
        _M0L3valS2282 = _M0L1jS559->$0;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS2281, _M0L3valS2282, _M0L6t__nowS561);
        _M0L6_2atmpS2351 = -_M0L7dt__preS562;
        _M0L6arg__fS563 = _M0L6_2atmpS2351 / _M0L6tau__fS555;
        _M0L1uS2283 = _M0L4varsS552->$2;
        _M0L3valS2284 = _M0L1jS559->$0;
        _M0L1uS2290 = _M0L4varsS552->$2;
        _M0L3valS2291 = _M0L1jS559->$0;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2289
        = _M0MPC15array5Array2atGfE(_M0L1uS2290, _M0L3valS2291);
        _M0L6_2atmpS2287 = _M0L11u__baselineS558 - _M0L6_2atmpS2289;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2288 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS563);
        _M0L6_2atmpS2286 = _M0L6_2atmpS2287 * _M0L6_2atmpS2288;
        _M0L6_2atmpS2285 = _M0L11u__baselineS558 - _M0L6_2atmpS2286;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2283, _M0L3valS2284, _M0L6_2atmpS2285);
        _M0L6_2atmpS2350 = -_M0L7dt__preS562;
        _M0L6arg__dS564 = _M0L6_2atmpS2350 / _M0L6tau__dS557;
        _M0L1xS2292 = _M0L4varsS552->$3;
        _M0L3valS2293 = _M0L1jS559->$0;
        _M0L1xS2299 = _M0L4varsS552->$3;
        _M0L3valS2300 = _M0L1jS559->$0;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2298
        = _M0MPC15array5Array2atGfE(_M0L1xS2299, _M0L3valS2300);
        _M0L6_2atmpS2296 = 0x1p+0f - _M0L6_2atmpS2298;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2297 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS564);
        _M0L6_2atmpS2295 = _M0L6_2atmpS2296 * _M0L6_2atmpS2297;
        _M0L6_2atmpS2294 = 0x1p+0f - _M0L6_2atmpS2295;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2292, _M0L3valS2293, _M0L6_2atmpS2294);
        _M0L8rho__preS2301 = _M0L4varsS552->$4;
        _M0L3valS2302 = _M0L1jS559->$0;
        _M0L1uS2308 = _M0L4varsS552->$2;
        _M0L3valS2309 = _M0L1jS559->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2304
        = _M0MPC15array5Array2atGfE(_M0L1uS2308, _M0L3valS2309);
        _M0L1xS2306 = _M0L4varsS552->$3;
        _M0L3valS2307 = _M0L1jS559->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2305
        = _M0MPC15array5Array2atGfE(_M0L1xS2306, _M0L3valS2307);
        _M0L6_2atmpS2303 = _M0L6_2atmpS2304 * _M0L6_2atmpS2305;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS2301, _M0L3valS2302, _M0L6_2atmpS2303);
        _M0L6matrixS2349 = _M0L3synS554->$4;
        _M0L6rowptrS2347 = _M0L6matrixS2349->$2;
        _M0L3valS2348 = _M0L1jS559->$0;
        #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS565
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2347, _M0L3valS2348);
        _M0L6matrixS2346 = _M0L3synS554->$4;
        _M0L6rowptrS2343 = _M0L6matrixS2346->$2;
        _M0L3valS2345 = _M0L1jS559->$0;
        _M0L6_2atmpS2344 = _M0L3valS2345 + 1;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS566
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2343, _M0L6_2atmpS2344);
        _M0L1sS567
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS567)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS567->$0 = _M0L5startS565;
        while (1) {
          int32_t _M0L3valS2310 = _M0L1sS567->$0;
          if (_M0L3valS2310 < _M0L3endS566) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS2311 = _M0L3synS554->$6;
            int32_t _M0L3valS2312 = _M0L1sS567->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS2314 = _M0L4varsS552->$4;
            int32_t _M0L3valS2315 = _M0L1jS559->$0;
            float _M0L6_2atmpS2313;
            int32_t _M0L3valS2317;
            int32_t _M0L6_2atmpS2316;
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS2313
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS2314, _M0L3valS2315);
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS2311, _M0L3valS2312, _M0L6_2atmpS2313);
            _M0L3valS2317 = _M0L1sS567->$0;
            _M0L6_2atmpS2316 = _M0L3valS2317 + 1;
            _M0L1sS567->$0 = _M0L6_2atmpS2316;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS567);
          }
          break;
        }
        _M0L1uS2318 = _M0L4varsS552->$2;
        _M0L3valS2319 = _M0L1jS559->$0;
        _M0L1uS2327 = _M0L4varsS552->$2;
        _M0L3valS2328 = _M0L1jS559->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2321
        = _M0MPC15array5Array2atGfE(_M0L1uS2327, _M0L3valS2328);
        _M0L1uS2325 = _M0L4varsS552->$2;
        _M0L3valS2326 = _M0L1jS559->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2324
        = _M0MPC15array5Array2atGfE(_M0L1uS2325, _M0L3valS2326);
        _M0L6_2atmpS2323 = 0x1p+0f - _M0L6_2atmpS2324;
        _M0L6_2atmpS2322 = _M0L11u__baselineS558 * _M0L6_2atmpS2323;
        _M0L6_2atmpS2320 = _M0L6_2atmpS2321 + _M0L6_2atmpS2322;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2318, _M0L3valS2319, _M0L6_2atmpS2320);
        _M0L1xS2329 = _M0L4varsS552->$3;
        _M0L3valS2330 = _M0L1jS559->$0;
        _M0L1xS2341 = _M0L4varsS552->$3;
        _M0L3valS2342 = _M0L1jS559->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2332
        = _M0MPC15array5Array2atGfE(_M0L1xS2341, _M0L3valS2342);
        _M0L1uS2339 = _M0L4varsS552->$2;
        _M0L3valS2340 = _M0L1jS559->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2338
        = _M0MPC15array5Array2atGfE(_M0L1uS2339, _M0L3valS2340);
        _M0L6_2atmpS2334 = -_M0L6_2atmpS2338;
        _M0L1xS2336 = _M0L4varsS552->$3;
        _M0L3valS2337 = _M0L1jS559->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2335
        = _M0MPC15array5Array2atGfE(_M0L1xS2336, _M0L3valS2337);
        _M0L6_2atmpS2333 = _M0L6_2atmpS2334 * _M0L6_2atmpS2335;
        _M0L6_2atmpS2331 = _M0L6_2atmpS2332 + _M0L6_2atmpS2333;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2329, _M0L3valS2330, _M0L6_2atmpS2331);
      }
      _M0L3valS2356 = _M0L1jS559->$0;
      _M0L6_2atmpS2355 = _M0L3valS2356 + 1;
      _M0L1jS559->$0 = _M0L6_2atmpS2355;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS559);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS550,
  float _M0L2dtS551
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2263;
  struct _M0TPB5ArrayGfE* _M0L1tS2266;
  float _M0L6_2atmpS2265;
  float _M0L6_2atmpS2264;
  struct _M0TPB5ArrayGiE* _M0L2ttS2267;
  struct _M0TPB5ArrayGiE* _M0L2ttS2270;
  int32_t _M0L6_2atmpS2269;
  int32_t _M0L6_2atmpS2268;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2263 = _M0L1tS550->$0;
  _M0L1tS2266 = _M0L1tS550->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2265 = _M0MPC15array5Array2atGfE(_M0L1tS2266, 0);
  _M0L6_2atmpS2264 = _M0L6_2atmpS2265 + _M0L2dtS551;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS2263, 0, _M0L6_2atmpS2264);
  _M0L2ttS2267 = _M0L1tS550->$1;
  _M0L2ttS2270 = _M0L1tS550->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2269 = _M0MPC15array5Array2atGiE(_M0L2ttS2270, 0);
  _M0L6_2atmpS2268 = _M0L6_2atmpS2269 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS2267, 0, _M0L6_2atmpS2268);
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS549
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2262;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2262 = _M0L1tS549->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS2262, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS2261;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2258;
  int32_t* _M0L6_2atmpS2260;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2259;
  struct _M0TP26RiantR8snn__mbt4Time* _block_5338;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2261 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2261[0] = 0x0p+0f;
  _M0L6_2atmpS2258
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2258)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _M0L6_2atmpS2258->$0 = _M0L6_2atmpS2261;
  _M0L6_2atmpS2258->$1 = 1;
  _M0L6_2atmpS2260 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS2260[0] = 0;
  _M0L6_2atmpS2259
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2259)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _M0L6_2atmpS2259->$0 = _M0L6_2atmpS2260;
  _M0L6_2atmpS2259->$1 = 1;
  _block_5338
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_5338)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 95, 0);
  _block_5338->$0 = _M0L6_2atmpS2258;
  _block_5338->$1 = _M0L6_2atmpS2259;
  _block_5338->$2 = 0x1p-3f;
  return _block_5338;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS547
) {
  uint32_t _M0L1uS546;
  uint32_t _M0L4bitsS548;
  double _M0L6_2atmpS2257;
  double _M0L6_2atmpS2256;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS546 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS547);
  _M0L4bitsS548 = _M0L1uS546 >> 8;
  _M0L6_2atmpS2257 = (double)_M0L4bitsS548;
  _M0L6_2atmpS2256 = _M0L6_2atmpS2257 * 0x1p-24;
  return (float)_M0L6_2atmpS2256;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS545
) {
  uint64_t _M0L1uS544;
  uint64_t _M0L6_2atmpS2255;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS544 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS545);
  _M0L6_2atmpS2255 = _M0L1uS544 >> 32;
  return (uint32_t)_M0L6_2atmpS2255;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS537
) {
  uint64_t _M0L2s0S536;
  uint64_t _M0L2s1S538;
  uint64_t _M0L2s2S539;
  uint64_t _M0L2s3S540;
  uint64_t _M0L3tmpS541;
  uint64_t _M0L6_2atmpS2254;
  uint64_t _M0L3resS542;
  uint64_t _M0L1tS543;
  uint64_t _M0L6_2atmpS2244;
  uint64_t _M0L6_2atmpS2245;
  uint64_t _M0L2s2S2247;
  uint64_t _M0L6_2atmpS2246;
  uint64_t _M0L2s3S2249;
  uint64_t _M0L6_2atmpS2248;
  uint64_t _M0L2s2S2251;
  uint64_t _M0L6_2atmpS2250;
  uint64_t _M0L2s3S2253;
  uint64_t _M0L6_2atmpS2252;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S536 = _M0L1rS537->$0;
  _M0L2s1S538 = _M0L1rS537->$1;
  _M0L2s2S539 = _M0L1rS537->$2;
  _M0L2s3S540 = _M0L1rS537->$3;
  _M0L3tmpS541 = _M0L2s0S536 + _M0L2s3S540;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2254 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS541, 23);
  _M0L3resS542 = _M0L6_2atmpS2254 + _M0L2s0S536;
  _M0L1tS543 = _M0L2s1S538 << 17;
  _M0L6_2atmpS2244 = _M0L2s2S539 ^ _M0L2s0S536;
  _M0L1rS537->$2 = _M0L6_2atmpS2244;
  _M0L6_2atmpS2245 = _M0L2s3S540 ^ _M0L2s1S538;
  _M0L1rS537->$3 = _M0L6_2atmpS2245;
  _M0L2s2S2247 = _M0L1rS537->$2;
  _M0L6_2atmpS2246 = _M0L2s1S538 ^ _M0L2s2S2247;
  _M0L1rS537->$1 = _M0L6_2atmpS2246;
  _M0L2s3S2249 = _M0L1rS537->$3;
  _M0L6_2atmpS2248 = _M0L2s0S536 ^ _M0L2s3S2249;
  _M0L1rS537->$0 = _M0L6_2atmpS2248;
  _M0L2s2S2251 = _M0L1rS537->$2;
  _M0L6_2atmpS2250 = _M0L2s2S2251 ^ _M0L1tS543;
  _M0L1rS537->$2 = _M0L6_2atmpS2250;
  _M0L2s3S2253 = _M0L1rS537->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2252 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2253, 45);
  _M0L1rS537->$3 = _M0L6_2atmpS2252;
  return _M0L3resS542;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS534, int32_t _M0L1kS535) {
  uint64_t _M0L6_2atmpS2241;
  int32_t _M0L6_2atmpS2243;
  uint64_t _M0L6_2atmpS2242;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2241 = _M0L1xS534 << (_M0L1kS535 & 63);
  _M0L6_2atmpS2243 = 64 - _M0L1kS535;
  _M0L6_2atmpS2242 = _M0L1xS534 >> (_M0L6_2atmpS2243 & 63);
  return _M0L6_2atmpS2241 | _M0L6_2atmpS2242;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS527
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS2240;
  int32_t _M0L1nS526;
  struct _M0TPB8MutLocalGiE* _M0L5countS528;
  struct _M0TPB8MutLocalGfE* _M0L4prevS529;
  int32_t _M0L7_2abindS530;
  int32_t _M0L1iS531;
  int32_t _result_5340;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS2240 = _M0L1mS527->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS526 = _M0MPC15array5Array6lengthGfE(_M0L4dataS2240);
  if (_M0L1nS526 == 0) {
    return 0;
  }
  _M0L5countS528
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS528)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS528->$0 = 0;
  _M0L4prevS529
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS529)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS529->$0 = 0x0p+0f;
  _M0L7_2abindS530 = 0;
  _M0L1iS531 = _M0L7_2abindS530;
  while (1) {
    if (_M0L1iS531 < _M0L1nS526) {
      struct _M0TPB5ArrayGfE* _M0L4dataS2238 = _M0L1mS527->$2;
      float _M0L3curS532;
      float _M0L3valS2235;
      int32_t _M0L6_2atmpS2239;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS532 = _M0MPC15array5Array2atGfE(_M0L4dataS2238, _M0L1iS531);
      _M0L3valS2235 = _M0L4prevS529->$0;
      if (_M0L3valS2235 < 0x1p-1f && _M0L3curS532 >= 0x1p-1f) {
        int32_t _M0L3valS2237 = _M0L5countS528->$0;
        int32_t _M0L6_2atmpS2236 = _M0L3valS2237 + 1;
        _M0L5countS528->$0 = _M0L6_2atmpS2236;
      }
      _M0L4prevS529->$0 = _M0L3curS532;
      _M0L6_2atmpS2239 = _M0L1iS531 + 1;
      _M0L1iS531 = _M0L6_2atmpS2239;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS529);
    }
    break;
  }
  _result_5340 = _M0L5countS528->$0;
  moonbit_decref_cycle_free(_M0L5countS528);
  return _result_5340;
}

double _M0FPC14math2ln(double _M0L1xS512) {
  struct _M0TUdiE* _M0L7_2abindS513;
  double _M0L5_2af1S514;
  int32_t _M0L5_2akiS515;
  double _M0L1fS517;
  double _M0L1kS518;
  double _M0L6_2atmpS2228;
  double _M0L1sS519;
  double _M0L2s2S520;
  double _M0L2s4S521;
  double _M0L6_2atmpS2227;
  double _M0L6_2atmpS2226;
  double _M0L6_2atmpS2225;
  double _M0L6_2atmpS2224;
  double _M0L6_2atmpS2223;
  double _M0L6_2atmpS2222;
  double _M0L2t1S522;
  double _M0L6_2atmpS2221;
  double _M0L6_2atmpS2220;
  double _M0L6_2atmpS2219;
  double _M0L6_2atmpS2218;
  double _M0L2t2S523;
  double _M0L1rS524;
  double _M0L6_2atmpS2217;
  double _M0L4hfsqS525;
  double _M0L6_2atmpS2210;
  double _M0L6_2atmpS2216;
  double _M0L6_2atmpS2214;
  double _M0L6_2atmpS2215;
  double _M0L6_2atmpS2213;
  double _M0L6_2atmpS2212;
  double _M0L6_2atmpS2211;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS512 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS512)
      || _M0MPC16double6Double7is__inf(_M0L1xS512)
    ) {
      return _M0L1xS512;
    } else if (_M0L1xS512 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS513 = _M0FPC14math5frexp(_M0L1xS512);
  _M0L5_2af1S514 = _M0L7_2abindS513->$0;
  _M0L5_2akiS515 = _M0L7_2abindS513->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS513);
  if (_M0L5_2af1S514 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2232 = _M0L5_2af1S514 * 0x1p+1;
    double _M0L6_2atmpS2229 = _M0L6_2atmpS2232 - 0x1p+0;
    int32_t _M0L6_2atmpS2231 = _M0L5_2akiS515 - 1;
    double _M0L6_2atmpS2230 = (double)_M0L6_2atmpS2231;
    _M0L1fS517 = _M0L6_2atmpS2229;
    _M0L1kS518 = _M0L6_2atmpS2230;
    goto join_516;
  } else {
    double _M0L6_2atmpS2233 = _M0L5_2af1S514 - 0x1p+0;
    double _M0L6_2atmpS2234 = (double)_M0L5_2akiS515;
    _M0L1fS517 = _M0L6_2atmpS2233;
    _M0L1kS518 = _M0L6_2atmpS2234;
    goto join_516;
  }
  join_516:;
  _M0L6_2atmpS2228 = 0x1p+1 + _M0L1fS517;
  _M0L1sS519 = _M0L1fS517 / _M0L6_2atmpS2228;
  _M0L2s2S520 = _M0L1sS519 * _M0L1sS519;
  _M0L2s4S521 = _M0L2s2S520 * _M0L2s2S520;
  _M0L6_2atmpS2227 = _M0L2s4S521 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2226 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2227;
  _M0L6_2atmpS2225 = _M0L2s4S521 * _M0L6_2atmpS2226;
  _M0L6_2atmpS2224 = 0x1.2492494229359p-2 + _M0L6_2atmpS2225;
  _M0L6_2atmpS2223 = _M0L2s4S521 * _M0L6_2atmpS2224;
  _M0L6_2atmpS2222 = 0x1.5555555555593p-1 + _M0L6_2atmpS2223;
  _M0L2t1S522 = _M0L2s2S520 * _M0L6_2atmpS2222;
  _M0L6_2atmpS2221 = _M0L2s4S521 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2220 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2221;
  _M0L6_2atmpS2219 = _M0L2s4S521 * _M0L6_2atmpS2220;
  _M0L6_2atmpS2218 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2219;
  _M0L2t2S523 = _M0L2s4S521 * _M0L6_2atmpS2218;
  _M0L1rS524 = _M0L2t1S522 + _M0L2t2S523;
  _M0L6_2atmpS2217 = 0x1p-1 * _M0L1fS517;
  _M0L4hfsqS525 = _M0L6_2atmpS2217 * _M0L1fS517;
  _M0L6_2atmpS2210 = _M0L1kS518 * 0x1.62e42feep-1;
  _M0L6_2atmpS2216 = _M0L4hfsqS525 + _M0L1rS524;
  _M0L6_2atmpS2214 = _M0L1sS519 * _M0L6_2atmpS2216;
  _M0L6_2atmpS2215 = _M0L1kS518 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2213 = _M0L6_2atmpS2214 + _M0L6_2atmpS2215;
  _M0L6_2atmpS2212 = _M0L4hfsqS525 - _M0L6_2atmpS2213;
  _M0L6_2atmpS2211 = _M0L6_2atmpS2212 - _M0L1fS517;
  return _M0L6_2atmpS2210 - _M0L6_2atmpS2211;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS505) {
  struct _M0TUdiE* _M0L7_2abindS506;
  double _M0L10_2anorm__fS507;
  int32_t _M0L6_2aexpS508;
  uint64_t _M0L1uS509;
  uint64_t _M0L6_2atmpS2209;
  uint64_t _M0L6_2atmpS2208;
  int32_t _M0L6_2atmpS2207;
  int32_t _M0L6_2atmpS2206;
  int32_t _M0L3expS510;
  uint64_t _M0L6_2atmpS2205;
  uint64_t _M0L6_2atmpS2204;
  uint64_t _M0L6_2atmpS2203;
  double _M0L4fracS511;
  struct _M0TUdiE* _block_5343;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS505 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS505)
    || _M0MPC16double6Double7is__nan(_M0L1fS505)
  ) {
    struct _M0TUdiE* _block_5342 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_5342)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_5342->$0 = _M0L1fS505;
    _block_5342->$1 = 0;
    return _block_5342;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS506 = _M0FPC14math9normalize(_M0L1fS505);
  _M0L10_2anorm__fS507 = _M0L7_2abindS506->$0;
  _M0L6_2aexpS508 = _M0L7_2abindS506->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS506);
  _M0L1uS509 = *(int64_t*)&_M0L10_2anorm__fS507;
  _M0L6_2atmpS2209 = _M0L1uS509 >> 52;
  _M0L6_2atmpS2208 = _M0L6_2atmpS2209 & 2047ull;
  _M0L6_2atmpS2207 = (int32_t)_M0L6_2atmpS2208;
  _M0L6_2atmpS2206 = _M0L6_2aexpS508 + _M0L6_2atmpS2207;
  _M0L3expS510 = _M0L6_2atmpS2206 - 1022;
  _M0L6_2atmpS2205 = ~9218868437227405312ull;
  _M0L6_2atmpS2204 = _M0L1uS509 & _M0L6_2atmpS2205;
  _M0L6_2atmpS2203 = _M0L6_2atmpS2204 | 4602678819172646912ull;
  _M0L4fracS511 = *(double*)&_M0L6_2atmpS2203;
  _block_5343 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_5343)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5343->$0 = _M0L4fracS511;
  _block_5343->$1 = _M0L3expS510;
  return _block_5343;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS504) {
  double _M0L6_2atmpS2200;
  struct _M0TUdiE* _block_5345;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2200 = fabs(_M0L1fS504);
  if (_M0L6_2atmpS2200 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2202 = (double)4503599627370496ll;
    double _M0L6_2atmpS2201 = _M0L1fS504 * _M0L6_2atmpS2202;
    struct _M0TUdiE* _block_5344 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_5344)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_5344->$0 = _M0L6_2atmpS2201;
    _block_5344->$1 = -52;
    return _block_5344;
  }
  _block_5345 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_5345)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5345->$0 = _M0L1fS504;
  _block_5345->$1 = 0;
  return _block_5345;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS503) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS503 != _M0L4selfS503;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS502) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS502 != _M0L4selfS502) {
    return 0;
  } else if (_M0L4selfS502 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS502 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS502;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS483,
  float _M0L4elemS485
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS482;
  int32_t _M0L1iS484;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS482 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS483);
  _M0L1iS484 = 0;
  while (1) {
    if (_M0L1iS484 < _M0L3lenS483) {
      float* _M0L3bufS2192 = _M0L3arrS482->$0;
      int32_t _M0L6_2atmpS2193;
      _M0L3bufS2192[_M0L1iS484] = _M0L4elemS485;
      _M0L6_2atmpS2193 = _M0L1iS484 + 1;
      _M0L1iS484 = _M0L6_2atmpS2193;
      continue;
    }
    break;
  }
  return _M0L3arrS482;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS488,
  int32_t _M0L4elemS490
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS487;
  int32_t _M0L1iS489;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS487 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS488);
  _M0L1iS489 = 0;
  while (1) {
    if (_M0L1iS489 < _M0L3lenS488) {
      uint8_t* _M0L3bufS2194 = _M0L3arrS487->$0;
      int32_t _M0L6_2atmpS2195;
      _M0L3bufS2194[_M0L1iS489] = _M0L4elemS490;
      _M0L6_2atmpS2195 = _M0L1iS489 + 1;
      _M0L1iS489 = _M0L6_2atmpS2195;
      continue;
    }
    break;
  }
  return _M0L3arrS487;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS493,
  int32_t _M0L4elemS495
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS492;
  int32_t _M0L1iS494;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS492 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS493);
  _M0L1iS494 = 0;
  while (1) {
    if (_M0L1iS494 < _M0L3lenS493) {
      int32_t* _M0L3bufS2196 = _M0L3arrS492->$0;
      int32_t _M0L6_2atmpS2197;
      _M0L3bufS2196[_M0L1iS494] = _M0L4elemS495;
      _M0L6_2atmpS2197 = _M0L1iS494 + 1;
      _M0L1iS494 = _M0L6_2atmpS2197;
      continue;
    }
    break;
  }
  return _M0L3arrS492;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS498,
  struct _M0TPB5ArrayGfE* _M0L4elemS500
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS497;
  int32_t _M0L1iS499;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS497
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS498);
  _M0L1iS499 = 0;
  while (1) {
    if (_M0L1iS499 < _M0L3lenS498) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2198 = _M0L3arrS497->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS4923 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2198[_M0L1iS499];
      int32_t _M0L6_2atmpS2199;
      moonbit_incref_cycle_free(_M0L4elemS500);
      if (_M0L6_2aoldS4923) {
        moonbit_decref_cycle_free(_M0L6_2aoldS4923);
      }
      _M0L3bufS2198[_M0L1iS499] = _M0L4elemS500;
      _M0L6_2atmpS2199 = _M0L1iS499 + 1;
      _M0L1iS499 = _M0L6_2atmpS2199;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS500);
    }
    break;
  }
  return _M0L3arrS497;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS467,
  int32_t _M0L5indexS468,
  float _M0L5valueS469
) {
  int32_t _M0L3lenS466;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS466 = _M0L4selfS467->$1;
  if (_M0L5indexS468 >= 0 && _M0L5indexS468 < _M0L3lenS466) {
    float* _M0L6_2atmpS2188;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2188 = _M0MPC15array5Array6bufferGfE(_M0L4selfS467);
    _M0L6_2atmpS2188[_M0L5indexS468] = _M0L5valueS469;
    moonbit_decref_cycle_free(_M0L6_2atmpS2188);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS471,
  int32_t _M0L5indexS472,
  struct _M0TPB5ArrayGfE* _M0L5valueS473
) {
  int32_t _M0L3lenS470;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS470 = _M0L4selfS471->$1;
  if (_M0L5indexS472 >= 0 && _M0L5indexS472 < _M0L3lenS470) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2189;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS4924;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2189
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS471);
    _M0L6_2aoldS4924
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2189[_M0L5indexS472];
    if (_M0L6_2aoldS4924) {
      moonbit_decref_cycle_free(_M0L6_2aoldS4924);
    }
    _M0L6_2atmpS2189[_M0L5indexS472] = _M0L5valueS473;
    moonbit_decref_cycle_free(_M0L6_2atmpS2189);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS473);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS475,
  int32_t _M0L5indexS476,
  int32_t _M0L5valueS477
) {
  int32_t _M0L3lenS474;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS474 = _M0L4selfS475->$1;
  if (_M0L5indexS476 >= 0 && _M0L5indexS476 < _M0L3lenS474) {
    int32_t* _M0L6_2atmpS2190;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2190 = _M0MPC15array5Array6bufferGiE(_M0L4selfS475);
    _M0L6_2atmpS2190[_M0L5indexS476] = _M0L5valueS477;
    moonbit_decref_cycle_free(_M0L6_2atmpS2190);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS479,
  int32_t _M0L5indexS480,
  int32_t _M0L5valueS481
) {
  int32_t _M0L3lenS478;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS478 = _M0L4selfS479->$1;
  if (_M0L5indexS480 >= 0 && _M0L5indexS480 < _M0L3lenS478) {
    uint8_t* _M0L6_2atmpS2191;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2191 = _M0MPC15array5Array6bufferGbE(_M0L4selfS479);
    _M0L6_2atmpS2191[_M0L5indexS480] = _M0L5valueS481;
    moonbit_decref_cycle_free(_M0L6_2atmpS2191);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS459) {
  int32_t _M0L3lenS458;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS458 = _M0L4selfS459->$1;
  if (_M0L3lenS458 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS460 = _M0L3lenS458 - 1;
    float* _M0L3bufS2186 = _M0L4selfS459->$0;
    float _M0L1vS461 = (float)_M0L3bufS2186[_M0L5indexS460];
    void* _block_5350;
    _M0L4selfS459->$1 = _M0L5indexS460;
    _block_5350
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_5350)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_5350)->$0 = _M0L1vS461;
    return _block_5350;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS463) {
  int32_t _M0L3lenS462;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS462 = _M0L4selfS463->$1;
  if (_M0L3lenS462 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS464 = _M0L3lenS462 - 1;
    int32_t* _M0L3bufS2187 = _M0L4selfS463->$0;
    int32_t _M0L1vS465 = (int32_t)_M0L3bufS2187[_M0L5indexS464];
    _M0L4selfS463->$1 = _M0L5indexS464;
    return (int64_t)_M0L1vS465;
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS441,
  int32_t _M0L5indexS442
) {
  int32_t _M0L3lenS440;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS440 = _M0L4selfS441->$1;
  if (_M0L5indexS442 >= 0 && _M0L5indexS442 < _M0L3lenS440) {
    moonbit_string_t* _M0L6_2atmpS2180;
    moonbit_string_t _M0L6_2atmpS4925;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2180 = _M0MPC15array5Array6bufferGsE(_M0L4selfS441);
    _M0L6_2atmpS4925 = (moonbit_string_t)_M0L6_2atmpS2180[_M0L5indexS442];
    moonbit_incref_cycle_free(_M0L6_2atmpS4925);
    moonbit_decref_cycle_free(_M0L6_2atmpS2180);
    return _M0L6_2atmpS4925;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS444,
  int32_t _M0L5indexS445
) {
  int32_t _M0L3lenS443;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS443 = _M0L4selfS444->$1;
  if (_M0L5indexS445 >= 0 && _M0L5indexS445 < _M0L3lenS443) {
    float* _M0L6_2atmpS2181;
    float _result_5351;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2181 = _M0MPC15array5Array6bufferGfE(_M0L4selfS444);
    _result_5351 = (float)_M0L6_2atmpS2181[_M0L5indexS445];
    moonbit_decref_cycle_free(_M0L6_2atmpS2181);
    return _result_5351;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS447,
  int32_t _M0L5indexS448
) {
  int32_t _M0L3lenS446;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS446 = _M0L4selfS447->$1;
  if (_M0L5indexS448 >= 0 && _M0L5indexS448 < _M0L3lenS446) {
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS2182;
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L6_2atmpS4926;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2182
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L4selfS447);
    _M0L6_2atmpS4926
    = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L6_2atmpS2182[
        _M0L5indexS448
      ];
    if (_M0L6_2atmpS4926) {
      moonbit_incref_cycle_free(_M0L6_2atmpS4926);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2182);
    return _M0L6_2atmpS4926;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS450,
  int32_t _M0L5indexS451
) {
  int32_t _M0L3lenS449;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS449 = _M0L4selfS450->$1;
  if (_M0L5indexS451 >= 0 && _M0L5indexS451 < _M0L3lenS449) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2183;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS4927;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2183
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS450);
    _M0L6_2atmpS4927
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2183[_M0L5indexS451];
    if (_M0L6_2atmpS4927) {
      moonbit_incref_cycle_free(_M0L6_2atmpS4927);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2183);
    return _M0L6_2atmpS4927;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS453,
  int32_t _M0L5indexS454
) {
  int32_t _M0L3lenS452;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS452 = _M0L4selfS453->$1;
  if (_M0L5indexS454 >= 0 && _M0L5indexS454 < _M0L3lenS452) {
    int32_t* _M0L6_2atmpS2184;
    int32_t _result_5352;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2184 = _M0MPC15array5Array6bufferGiE(_M0L4selfS453);
    _result_5352 = (int32_t)_M0L6_2atmpS2184[_M0L5indexS454];
    moonbit_decref_cycle_free(_M0L6_2atmpS2184);
    return _result_5352;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS456,
  int32_t _M0L5indexS457
) {
  int32_t _M0L3lenS455;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS455 = _M0L4selfS456->$1;
  if (_M0L5indexS457 >= 0 && _M0L5indexS457 < _M0L3lenS455) {
    uint8_t* _M0L6_2atmpS2185;
    int32_t _result_5353;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2185 = _M0MPC15array5Array6bufferGbE(_M0L4selfS456);
    _result_5353 = (int32_t)_M0L6_2atmpS2185[_M0L5indexS457];
    moonbit_decref_cycle_free(_M0L6_2atmpS2185);
    return _result_5353;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS439) {
  moonbit_string_t _M0L6_2atmpS2179;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2179 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS439);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2179);
  moonbit_decref_cycle_free(_M0L6_2atmpS2179);
  return 0;
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS438) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS438 > _M0FPB18double__max__value
         || _M0L4selfS438 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS437) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS437 != _M0L4selfS437;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS433
) {
  float* _M0L6_2atmpS2175;
  struct _M0TPB5ArrayGfE* _block_5354;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2175 = (float*)moonbit_make_float_array_raw(_M0L3lenS433);
  _block_5354
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_5354)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _block_5354->$0 = _M0L6_2atmpS2175;
  _block_5354->$1 = _M0L3lenS433;
  return _block_5354;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS434
) {
  uint8_t* _M0L6_2atmpS2176;
  struct _M0TPB5ArrayGbE* _block_5355;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2176 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS434);
  _block_5355
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_5355)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 99, 0);
  _block_5355->$0 = _M0L6_2atmpS2176;
  _block_5355->$1 = _M0L3lenS434;
  return _block_5355;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS435
) {
  int32_t* _M0L6_2atmpS2177;
  struct _M0TPB5ArrayGiE* _block_5356;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2177 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS435);
  _block_5356
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_5356)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _block_5356->$0 = _M0L6_2atmpS2177;
  _block_5356->$1 = _M0L3lenS435;
  return _block_5356;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS436
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS2178;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_5357;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2178
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS436, 0);
  _block_5357
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_5357)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 102, 0);
  _block_5357->$0 = _M0L6_2atmpS2178;
  _block_5357->$1 = _M0L3lenS436;
  return _block_5357;
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS432) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS432, 10);
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS420,
  moonbit_string_t _M0L5valueS422
) {
  int32_t _M0L3lenS2147;
  moonbit_string_t* _M0L6_2atmpS2149;
  int32_t _M0L6_2atmpS2148;
  int32_t _M0L6lengthS421;
  moonbit_string_t* _M0L3bufS2152;
  moonbit_string_t _M0L6_2aoldS4928;
  int32_t _M0L6_2atmpS2153;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2147 = _M0L4selfS420->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2149 = _M0MPC15array5Array6bufferGsE(_M0L4selfS420);
  _M0L6_2atmpS2148 = Moonbit_array_length(_M0L6_2atmpS2149);
  moonbit_decref_cycle_free(_M0L6_2atmpS2149);
  if (_M0L3lenS2147 == _M0L6_2atmpS2148) {
    int32_t _M0L3lenS2151 = _M0L4selfS420->$1;
    int32_t _M0L6_2atmpS2150 = _M0L3lenS2151 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS420, _M0L6_2atmpS2150);
  }
  _M0L6lengthS421 = _M0L4selfS420->$1;
  _M0L3bufS2152 = _M0L4selfS420->$0;
  _M0L6_2aoldS4928 = (moonbit_string_t)_M0L3bufS2152[_M0L6lengthS421];
  moonbit_decref_cycle_free(_M0L6_2aoldS4928);
  _M0L3bufS2152[_M0L6lengthS421] = _M0L5valueS422;
  _M0L6_2atmpS2153 = _M0L6lengthS421 + 1;
  _M0L4selfS420->$1 = _M0L6_2atmpS2153;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS423,
  struct _M0TUsiE* _M0L5valueS425
) {
  int32_t _M0L3lenS2154;
  struct _M0TUsiE** _M0L6_2atmpS2156;
  int32_t _M0L6_2atmpS2155;
  int32_t _M0L6lengthS424;
  struct _M0TUsiE** _M0L3bufS2159;
  struct _M0TUsiE* _M0L6_2aoldS4929;
  int32_t _M0L6_2atmpS2160;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2154 = _M0L4selfS423->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2156 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS423);
  _M0L6_2atmpS2155 = Moonbit_array_length(_M0L6_2atmpS2156);
  moonbit_decref_cycle_free(_M0L6_2atmpS2156);
  if (_M0L3lenS2154 == _M0L6_2atmpS2155) {
    int32_t _M0L3lenS2158 = _M0L4selfS423->$1;
    int32_t _M0L6_2atmpS2157 = _M0L3lenS2158 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS423, _M0L6_2atmpS2157);
  }
  _M0L6lengthS424 = _M0L4selfS423->$1;
  _M0L3bufS2159 = _M0L4selfS423->$0;
  _M0L6_2aoldS4929 = (struct _M0TUsiE*)_M0L3bufS2159[_M0L6lengthS424];
  if (_M0L6_2aoldS4929) {
    moonbit_decref_cycle_free(_M0L6_2aoldS4929);
  }
  _M0L3bufS2159[_M0L6lengthS424] = _M0L5valueS425;
  _M0L6_2atmpS2160 = _M0L6lengthS424 + 1;
  _M0L4selfS423->$1 = _M0L6_2atmpS2160;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS426,
  int32_t _M0L5valueS428
) {
  int32_t _M0L3lenS2161;
  int32_t* _M0L6_2atmpS2163;
  int32_t _M0L6_2atmpS2162;
  int32_t _M0L6lengthS427;
  int32_t* _M0L3bufS2166;
  int32_t _M0L6_2atmpS2167;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2161 = _M0L4selfS426->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2163 = _M0MPC15array5Array6bufferGiE(_M0L4selfS426);
  _M0L6_2atmpS2162 = Moonbit_array_length(_M0L6_2atmpS2163);
  moonbit_decref_cycle_free(_M0L6_2atmpS2163);
  if (_M0L3lenS2161 == _M0L6_2atmpS2162) {
    int32_t _M0L3lenS2165 = _M0L4selfS426->$1;
    int32_t _M0L6_2atmpS2164 = _M0L3lenS2165 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS426, _M0L6_2atmpS2164);
  }
  _M0L6lengthS427 = _M0L4selfS426->$1;
  _M0L3bufS2166 = _M0L4selfS426->$0;
  _M0L3bufS2166[_M0L6lengthS427] = _M0L5valueS428;
  _M0L6_2atmpS2167 = _M0L6lengthS427 + 1;
  _M0L4selfS426->$1 = _M0L6_2atmpS2167;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS429,
  float _M0L5valueS431
) {
  int32_t _M0L3lenS2168;
  float* _M0L6_2atmpS2170;
  int32_t _M0L6_2atmpS2169;
  int32_t _M0L6lengthS430;
  float* _M0L3bufS2173;
  int32_t _M0L6_2atmpS2174;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2168 = _M0L4selfS429->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2170 = _M0MPC15array5Array6bufferGfE(_M0L4selfS429);
  _M0L6_2atmpS2169 = Moonbit_array_length(_M0L6_2atmpS2170);
  moonbit_decref_cycle_free(_M0L6_2atmpS2170);
  if (_M0L3lenS2168 == _M0L6_2atmpS2169) {
    int32_t _M0L3lenS2172 = _M0L4selfS429->$1;
    int32_t _M0L6_2atmpS2171 = _M0L3lenS2172 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS429, _M0L6_2atmpS2171);
  }
  _M0L6lengthS430 = _M0L4selfS429->$1;
  _M0L3bufS2173 = _M0L4selfS429->$0;
  _M0L3bufS2173[_M0L6lengthS430] = _M0L5valueS431;
  _M0L6_2atmpS2174 = _M0L6lengthS430 + 1;
  _M0L4selfS429->$1 = _M0L6_2atmpS2174;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS405,
  int32_t _M0L8requiredS407
) {
  int32_t _M0L8old__capS404;
  int32_t _M0L3lenS2143;
  int32_t _M0L8new__capS406;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS404 = _M0MPC15array5Array8capacityGsE(_M0L4selfS405);
  _M0L3lenS2143 = _M0L4selfS405->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS406
  = _M0FPB23array__growth__capacity(_M0L8old__capS404, _M0L3lenS2143, _M0L8requiredS407);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS405, _M0L8new__capS406);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS409,
  int32_t _M0L8requiredS411
) {
  int32_t _M0L8old__capS408;
  int32_t _M0L3lenS2144;
  int32_t _M0L8new__capS410;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS408 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS409);
  _M0L3lenS2144 = _M0L4selfS409->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS410
  = _M0FPB23array__growth__capacity(_M0L8old__capS408, _M0L3lenS2144, _M0L8requiredS411);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS409, _M0L8new__capS410);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS413,
  int32_t _M0L8requiredS415
) {
  int32_t _M0L8old__capS412;
  int32_t _M0L3lenS2145;
  int32_t _M0L8new__capS414;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS412 = _M0MPC15array5Array8capacityGiE(_M0L4selfS413);
  _M0L3lenS2145 = _M0L4selfS413->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS414
  = _M0FPB23array__growth__capacity(_M0L8old__capS412, _M0L3lenS2145, _M0L8requiredS415);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS413, _M0L8new__capS414);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS417,
  int32_t _M0L8requiredS419
) {
  int32_t _M0L8old__capS416;
  int32_t _M0L3lenS2146;
  int32_t _M0L8new__capS418;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS416 = _M0MPC15array5Array8capacityGfE(_M0L4selfS417);
  _M0L3lenS2146 = _M0L4selfS417->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS418
  = _M0FPB23array__growth__capacity(_M0L8old__capS416, _M0L3lenS2146, _M0L8requiredS419);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS417, _M0L8new__capS418);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS381,
  int32_t _M0L13new__capacityS384
) {
  moonbit_string_t* _M0L8old__bufS380;
  int32_t _M0L3lenS382;
  int32_t _M0L9copy__lenS383;
  moonbit_string_t* _M0L8new__bufS385;
  moonbit_string_t* _M0L6_2aoldS4930;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS380 = _M0L4selfS381->$0;
  _M0L3lenS382 = _M0L4selfS381->$1;
  if (_M0L3lenS382 < _M0L13new__capacityS384) {
    _M0L9copy__lenS383 = _M0L3lenS382;
  } else {
    _M0L9copy__lenS383 = _M0L13new__capacityS384;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS380);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS385
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS380, _M0L13new__capacityS384, _M0L9copy__lenS383, 0, 0);
  _M0L6_2aoldS4930 = _M0L4selfS381->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS4930);
  _M0L4selfS381->$0 = _M0L8new__bufS385;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS387,
  int32_t _M0L13new__capacityS390
) {
  struct _M0TUsiE** _M0L8old__bufS386;
  int32_t _M0L3lenS388;
  int32_t _M0L9copy__lenS389;
  struct _M0TUsiE** _M0L8new__bufS391;
  struct _M0TUsiE** _M0L6_2aoldS4931;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS386 = _M0L4selfS387->$0;
  _M0L3lenS388 = _M0L4selfS387->$1;
  if (_M0L3lenS388 < _M0L13new__capacityS390) {
    _M0L9copy__lenS389 = _M0L3lenS388;
  } else {
    _M0L9copy__lenS389 = _M0L13new__capacityS390;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS386);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS391
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS386, _M0L13new__capacityS390, _M0L9copy__lenS389, 0, 0);
  _M0L6_2aoldS4931 = _M0L4selfS387->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS4931);
  _M0L4selfS387->$0 = _M0L8new__bufS391;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS393,
  int32_t _M0L13new__capacityS396
) {
  int32_t* _M0L8old__bufS392;
  int32_t _M0L3lenS394;
  int32_t _M0L9copy__lenS395;
  int32_t* _M0L8new__bufS397;
  int32_t* _M0L6_2aoldS4932;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS392 = _M0L4selfS393->$0;
  _M0L3lenS394 = _M0L4selfS393->$1;
  if (_M0L3lenS394 < _M0L13new__capacityS396) {
    _M0L9copy__lenS395 = _M0L3lenS394;
  } else {
    _M0L9copy__lenS395 = _M0L13new__capacityS396;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS392);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS397
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS392, _M0L13new__capacityS396, _M0L9copy__lenS395, 0, 0);
  _M0L6_2aoldS4932 = _M0L4selfS393->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS4932);
  _M0L4selfS393->$0 = _M0L8new__bufS397;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS399,
  int32_t _M0L13new__capacityS402
) {
  float* _M0L8old__bufS398;
  int32_t _M0L3lenS400;
  int32_t _M0L9copy__lenS401;
  float* _M0L8new__bufS403;
  float* _M0L6_2aoldS4933;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS398 = _M0L4selfS399->$0;
  _M0L3lenS400 = _M0L4selfS399->$1;
  if (_M0L3lenS400 < _M0L13new__capacityS402) {
    _M0L9copy__lenS401 = _M0L3lenS400;
  } else {
    _M0L9copy__lenS401 = _M0L13new__capacityS402;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS398);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS403
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS398, _M0L13new__capacityS402, _M0L9copy__lenS401, 0, 0);
  _M0L6_2aoldS4933 = _M0L4selfS399->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS4933);
  _M0L4selfS399->$0 = _M0L8new__bufS403;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS376
) {
  moonbit_string_t* _M0L6_2atmpS2139;
  int32_t _result_5358;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2139 = _M0MPC15array5Array6bufferGsE(_M0L4selfS376);
  _result_5358 = Moonbit_array_length(_M0L6_2atmpS2139);
  moonbit_decref_cycle_free(_M0L6_2atmpS2139);
  return _result_5358;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS377
) {
  struct _M0TUsiE** _M0L6_2atmpS2140;
  int32_t _result_5359;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2140 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS377);
  _result_5359 = Moonbit_array_length(_M0L6_2atmpS2140);
  moonbit_decref_cycle_free(_M0L6_2atmpS2140);
  return _result_5359;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS378
) {
  int32_t* _M0L6_2atmpS2141;
  int32_t _result_5360;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2141 = _M0MPC15array5Array6bufferGiE(_M0L4selfS378);
  _result_5360 = Moonbit_array_length(_M0L6_2atmpS2141);
  moonbit_decref_cycle_free(_M0L6_2atmpS2141);
  return _result_5360;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS379
) {
  float* _M0L6_2atmpS2142;
  int32_t _result_5361;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2142 = _M0MPC15array5Array6bufferGfE(_M0L4selfS379);
  _result_5361 = Moonbit_array_length(_M0L6_2atmpS2142);
  moonbit_decref_cycle_free(_M0L6_2atmpS2142);
  return _result_5361;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS372,
  int32_t _M0L3lenS370,
  int32_t _M0L8requiredS369
) {
  int32_t _M0L5startS371;
  int32_t _M0L5spaceS373;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS369 < _M0L3lenS370) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  if (_M0L7currentS372 == 0) {
    _M0L5startS371 = 8;
  } else {
    _M0L5startS371 = _M0L7currentS372;
  }
  _M0L5spaceS373 = _M0L5startS371;
  while (1) {
    if (_M0L5spaceS373 < _M0L8requiredS369) {
      int32_t _M0L4nextS374 = _M0L5spaceS373 * 2;
      if (_M0L4nextS374 <= _M0L5spaceS373) {
        return _M0L8requiredS369;
      }
      _M0L5spaceS373 = _M0L4nextS374;
      continue;
    } else {
      return _M0L5spaceS373;
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

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS368) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS368->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS359) {
  float* _M0L8_2afieldS4934;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS4934 = _M0L4selfS359->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS4934);
  return _M0L8_2afieldS4934;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS360
) {
  moonbit_string_t* _M0L8_2afieldS4935;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS4935 = _M0L4selfS360->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS4935);
  return _M0L8_2afieldS4935;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS361
) {
  struct _M0TUsiE** _M0L8_2afieldS4936;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS4936 = _M0L4selfS361->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS4936);
  return _M0L8_2afieldS4936;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS362
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L8_2afieldS4937;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS4937 = _M0L4selfS362->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS4937);
  return _M0L8_2afieldS4937;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS363
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS4938;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS4938 = _M0L4selfS363->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS4938);
  return _M0L8_2afieldS4938;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS364) {
  int32_t* _M0L8_2afieldS4939;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS4939 = _M0L4selfS364->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS4939);
  return _M0L8_2afieldS4939;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS365) {
  uint8_t* _M0L8_2afieldS4940;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS4940 = _M0L4selfS365->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS4940);
  return _M0L8_2afieldS4940;
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
  int32_t _M0L3endS2137;
  int32_t _M0L5startS2138;
  int32_t _M0L8str__lenS354;
  int32_t _M0L3lenS2136;
  int32_t _M0L8requiredS356;
  uint16_t* _M0L4dataS2129;
  int32_t _M0L6_2atmpS2128;
  int32_t _if__result_5363;
  uint16_t* _M0L4dataS2130;
  int32_t _M0L3lenS2131;
  moonbit_string_t _M0L6_2atmpS2132;
  int32_t _M0L6_2atmpS2133;
  int32_t _M0L3lenS2135;
  int32_t _M0L6_2atmpS2134;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS2137 = _M0L3strS355.$2;
  _M0L5startS2138 = _M0L3strS355.$1;
  _M0L8str__lenS354 = _M0L3endS2137 - _M0L5startS2138;
  if (_M0L8str__lenS354 == 0) {
    return 0;
  }
  _M0L3lenS2136 = _M0L4selfS357->$1;
  _M0L8requiredS356 = _M0L3lenS2136 + _M0L8str__lenS354;
  _M0L4dataS2129 = _M0L4selfS357->$0;
  _M0L6_2atmpS2128 = Moonbit_array_length(_M0L4dataS2129);
  if (_M0L8requiredS356 > _M0L6_2atmpS2128) {
    _if__result_5363 = 1;
  } else {
    int32_t _M0L3lenS2127 = _M0L4selfS357->$1;
    _if__result_5363 = _M0L8requiredS356 < _M0L3lenS2127;
  }
  if (_if__result_5363) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS357, _M0L8requiredS356);
  }
  _M0L4dataS2130 = _M0L4selfS357->$0;
  _M0L3lenS2131 = _M0L4selfS357->$1;
  moonbit_incref_cycle_free(_M0L4dataS2130);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2132 = _M0MPC16string10StringView4data(_M0L3strS355);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2133 = _M0MPC16string10StringView13start__offset(_M0L3strS355);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2130, _M0L3lenS2131, _M0L6_2atmpS2132, _M0L6_2atmpS2133, _M0L8str__lenS354);
  moonbit_decref_cycle_free(_M0L4dataS2130);
  moonbit_decref_cycle_free(_M0L6_2atmpS2132);
  _M0L3lenS2135 = _M0L4selfS357->$1;
  _M0L6_2atmpS2134 = _M0L3lenS2135 + _M0L8str__lenS354;
  _M0L4selfS357->$1 = _M0L6_2atmpS2134;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS351,
  int32_t _M0L5startS349,
  int32_t _M0L3endS350
) {
  int32_t _if__result_5364;
  int32_t _M0L3lenS352;
  int32_t _M0L6_2atmpS2126;
  moonbit_bytes_t _M0L5bytesS353;
  moonbit_bytes_t _M0L6_2atmpS2125;
  moonbit_string_t _result_5365;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS349 == 0) {
    int32_t _M0L6_2atmpS2124 = Moonbit_array_length(_M0L3strS351);
    _if__result_5364 = _M0L3endS350 == _M0L6_2atmpS2124;
  } else {
    _if__result_5364 = 0;
  }
  if (_if__result_5364) {
    moonbit_incref_cycle_free(_M0L3strS351);
    return _M0L3strS351;
  }
  _M0L3lenS352 = _M0L3endS350 - _M0L5startS349;
  _M0L6_2atmpS2126 = _M0L3lenS352 * 2;
  _M0L5bytesS353 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS2126, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS353, 0, _M0L3strS351, _M0L5startS349, _M0L3lenS352);
  _M0L6_2atmpS2125 = _M0L5bytesS353;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_5365
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS2125, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS2125);
  return _result_5365;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS344,
  int32_t _M0L6offsetS348,
  int64_t _M0L6lengthS346
) {
  int32_t _M0L3lenS343;
  int32_t _M0L6lengthS345;
  int32_t _if__result_5366;
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
      int32_t _M0L6_2atmpS2123 = _M0L6offsetS348 + _M0L6lengthS345;
      _if__result_5366 = _M0L6_2atmpS2123 <= _M0L3lenS343;
    } else {
      _if__result_5366 = 0;
    }
  } else {
    _if__result_5366 = 0;
  }
  if (_if__result_5366) {
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
  int32_t _M0L6_2atmpS2122;
  int32_t _M0L6_2atmpS2121;
  int32_t _M0L2e1S329;
  int32_t _M0L6_2atmpS2120;
  int32_t _M0L2e2S332;
  int32_t _M0L4len1S334;
  int32_t _M0L4len2S336;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS2122 = _M0L6lengthS331 * 2;
  _M0L6_2atmpS2121 = _M0L13bytes__offsetS330 + _M0L6_2atmpS2122;
  _M0L2e1S329 = _M0L6_2atmpS2121 - 1;
  _M0L6_2atmpS2120 = _M0L11str__offsetS333 + _M0L6lengthS331;
  _M0L2e2S332 = _M0L6_2atmpS2120 - 1;
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
        int32_t _M0L6_2atmpS2117 = _M0L3strS337[_M0L1iS339];
        int32_t _M0L6_2atmpS2116 = (int32_t)_M0L6_2atmpS2117;
        uint32_t _M0L1cS341 = *(uint32_t*)&_M0L6_2atmpS2116;
        uint32_t _M0L6_2atmpS2112 = _M0L1cS341 & 255u;
        int32_t _M0L6_2atmpS2111;
        int32_t _M0L6_2atmpS2113;
        uint32_t _M0L6_2atmpS2115;
        int32_t _M0L6_2atmpS2114;
        int32_t _M0L6_2atmpS2118;
        int32_t _M0L6_2atmpS2119;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2111 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2112);
        if (
          _M0L1jS340 < 0 || _M0L1jS340 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L1jS340] = _M0L6_2atmpS2111;
        _M0L6_2atmpS2113 = _M0L1jS340 + 1;
        _M0L6_2atmpS2115 = _M0L1cS341 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2114 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2115);
        if (
          _M0L6_2atmpS2113 < 0
          || _M0L6_2atmpS2113 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L6_2atmpS2113] = _M0L6_2atmpS2114;
        _M0L6_2atmpS2118 = _M0L1iS339 + 1;
        _M0L6_2atmpS2119 = _M0L1jS340 + 2;
        _M0L1iS339 = _M0L6_2atmpS2118;
        _M0L1jS340 = _M0L6_2atmpS2119;
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
  int32_t _M0L6_2atmpS2110;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2110 = *(int32_t*)&_M0L4selfS328;
  return _M0L6_2atmpS2110 & 0xff;
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
    int32_t _M0L6_2atmpS2109 = -_M0L4selfS312;
    _M0L3numS314 = *(uint32_t*)&_M0L6_2atmpS2109;
  } else {
    _M0L3numS314 = *(uint32_t*)&_M0L4selfS312;
  }
  switch (_M0L5radixS311) {
    case 10: {
      int32_t _M0L10digit__lenS316;
      int32_t _M0L6_2atmpS2106;
      int32_t _M0L10total__lenS317;
      uint16_t* _M0L6bufferS318;
      int32_t _M0L12digit__startS319;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS316 = _M0FPB12dec__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS2106 = 1;
      } else {
        _M0L6_2atmpS2106 = 0;
      }
      _M0L10total__lenS317 = _M0L10digit__lenS316 + _M0L6_2atmpS2106;
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
      int32_t _M0L6_2atmpS2107;
      int32_t _M0L10total__lenS321;
      uint16_t* _M0L6bufferS322;
      int32_t _M0L12digit__startS323;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS320 = _M0FPB12hex__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS2107 = 1;
      } else {
        _M0L6_2atmpS2107 = 0;
      }
      _M0L10total__lenS321 = _M0L10digit__lenS320 + _M0L6_2atmpS2107;
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
      int32_t _M0L6_2atmpS2108;
      int32_t _M0L10total__lenS325;
      uint16_t* _M0L6bufferS326;
      int32_t _M0L12digit__startS327;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS324
      = _M0FPB14radix__count32(_M0L3numS314, _M0L5radixS311);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS2108 = 1;
      } else {
        _M0L6_2atmpS2108 = 0;
      }
      _M0L10total__lenS325 = _M0L10digit__lenS324 + _M0L6_2atmpS2108;
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
      uint32_t _M0L6_2atmpS2104 = _M0L3numS308 / _M0L4baseS306;
      int32_t _M0L6_2atmpS2105 = _M0L5countS309 + 1;
      _M0L3numS308 = _M0L6_2atmpS2104;
      _M0L5countS309 = _M0L6_2atmpS2105;
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
    int32_t _M0L6_2atmpS2103;
    int32_t _M0L6_2atmpS2102;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS304 = moonbit_clz32(_M0L5valueS303);
    _M0L6_2atmpS2103 = 31 - _M0L14leading__zerosS304;
    _M0L6_2atmpS2102 = _M0L6_2atmpS2103 / 4;
    return _M0L6_2atmpS2102 + 1;
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
  int32_t _M0L6_2atmpS2101;
  uint32_t _M0L3numS278;
  int32_t _M0L6offsetS279;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2101 = _M0L10total__lenS301 - _M0L12digit__startS289;
  _M0L3numS278 = _M0L3numS300;
  _M0L6offsetS279 = _M0L6_2atmpS2101;
  while (1) {
    if (_M0L3numS278 >= 10000u) {
      uint32_t _M0L1tS280 = _M0L3numS278 / 10000u;
      uint32_t _M0L6_2atmpS2078 = _M0L3numS278 % 10000u;
      int32_t _M0L1rS281 = *(int32_t*)&_M0L6_2atmpS2078;
      int32_t _M0L2d1S282 = _M0L1rS281 / 100;
      int32_t _M0L2d2S283 = _M0L1rS281 % 100;
      int32_t _M0L6_2atmpS2077 = _M0L2d1S282 / 10;
      int32_t _M0L6_2atmpS2076 = 48 + _M0L6_2atmpS2077;
      int32_t _M0L6d1__hiS284 = (uint16_t)_M0L6_2atmpS2076;
      int32_t _M0L6_2atmpS2075 = _M0L2d1S282 % 10;
      int32_t _M0L6_2atmpS2074 = 48 + _M0L6_2atmpS2075;
      int32_t _M0L6d1__loS285 = (uint16_t)_M0L6_2atmpS2074;
      int32_t _M0L6_2atmpS2073 = _M0L2d2S283 / 10;
      int32_t _M0L6_2atmpS2072 = 48 + _M0L6_2atmpS2073;
      int32_t _M0L6d2__hiS286 = (uint16_t)_M0L6_2atmpS2072;
      int32_t _M0L6_2atmpS2071 = _M0L2d2S283 % 10;
      int32_t _M0L6_2atmpS2070 = 48 + _M0L6_2atmpS2071;
      int32_t _M0L6d2__loS287 = (uint16_t)_M0L6_2atmpS2070;
      int32_t _M0L6_2atmpS2062 = _M0L12digit__startS289 + _M0L6offsetS279;
      int32_t _M0L6_2atmpS2061 = _M0L6_2atmpS2062 - 4;
      int32_t _M0L6_2atmpS2064;
      int32_t _M0L6_2atmpS2063;
      int32_t _M0L6_2atmpS2066;
      int32_t _M0L6_2atmpS2065;
      int32_t _M0L6_2atmpS2068;
      int32_t _M0L6_2atmpS2067;
      int32_t _M0L6_2atmpS2069;
      _M0L6bufferS288[_M0L6_2atmpS2061] = _M0L6d1__hiS284;
      _M0L6_2atmpS2064 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS2063 = _M0L6_2atmpS2064 - 3;
      _M0L6bufferS288[_M0L6_2atmpS2063] = _M0L6d1__loS285;
      _M0L6_2atmpS2066 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS2065 = _M0L6_2atmpS2066 - 2;
      _M0L6bufferS288[_M0L6_2atmpS2065] = _M0L6d2__hiS286;
      _M0L6_2atmpS2068 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS2067 = _M0L6_2atmpS2068 - 1;
      _M0L6bufferS288[_M0L6_2atmpS2067] = _M0L6d2__loS287;
      _M0L6_2atmpS2069 = _M0L6offsetS279 - 4;
      _M0L3numS278 = _M0L1tS280;
      _M0L6offsetS279 = _M0L6_2atmpS2069;
      continue;
    } else {
      int32_t _M0L6_2atmpS2100 = *(int32_t*)&_M0L3numS278;
      int32_t _M0L9remainingS291 = _M0L6_2atmpS2100;
      int32_t _M0L6offsetS292 = _M0L6offsetS279;
      while (1) {
        if (_M0L9remainingS291 >= 100) {
          int32_t _M0L1tS293 = _M0L9remainingS291 / 100;
          int32_t _M0L1dS294 = _M0L9remainingS291 % 100;
          int32_t _M0L6_2atmpS2087 = _M0L1dS294 / 10;
          int32_t _M0L6_2atmpS2086 = 48 + _M0L6_2atmpS2087;
          int32_t _M0L5d__hiS295 = (uint16_t)_M0L6_2atmpS2086;
          int32_t _M0L6_2atmpS2085 = _M0L1dS294 % 10;
          int32_t _M0L6_2atmpS2084 = 48 + _M0L6_2atmpS2085;
          int32_t _M0L5d__loS296 = (uint16_t)_M0L6_2atmpS2084;
          int32_t _M0L6_2atmpS2080 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS2079 = _M0L6_2atmpS2080 - 2;
          int32_t _M0L6_2atmpS2082;
          int32_t _M0L6_2atmpS2081;
          int32_t _M0L6_2atmpS2083;
          _M0L6bufferS288[_M0L6_2atmpS2079] = _M0L5d__hiS295;
          _M0L6_2atmpS2082 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS2081 = _M0L6_2atmpS2082 - 1;
          _M0L6bufferS288[_M0L6_2atmpS2081] = _M0L5d__loS296;
          _M0L6_2atmpS2083 = _M0L6offsetS292 - 2;
          _M0L9remainingS291 = _M0L1tS293;
          _M0L6offsetS292 = _M0L6_2atmpS2083;
          continue;
        } else if (_M0L9remainingS291 >= 10) {
          int32_t _M0L6_2atmpS2095 = _M0L9remainingS291 / 10;
          int32_t _M0L6_2atmpS2094 = 48 + _M0L6_2atmpS2095;
          int32_t _M0L5d__hiS298 = (uint16_t)_M0L6_2atmpS2094;
          int32_t _M0L6_2atmpS2093 = _M0L9remainingS291 % 10;
          int32_t _M0L6_2atmpS2092 = 48 + _M0L6_2atmpS2093;
          int32_t _M0L5d__loS299 = (uint16_t)_M0L6_2atmpS2092;
          int32_t _M0L6_2atmpS2089 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS2088 = _M0L6_2atmpS2089 - 2;
          int32_t _M0L6_2atmpS2091;
          int32_t _M0L6_2atmpS2090;
          _M0L6bufferS288[_M0L6_2atmpS2088] = _M0L5d__hiS298;
          _M0L6_2atmpS2091 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS2090 = _M0L6_2atmpS2091 - 1;
          _M0L6bufferS288[_M0L6_2atmpS2090] = _M0L5d__loS299;
        } else {
          int32_t _M0L6_2atmpS2099 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS2096 = _M0L6_2atmpS2099 - 1;
          int32_t _M0L6_2atmpS2098 = 48 + _M0L9remainingS291;
          int32_t _M0L6_2atmpS2097 = (uint16_t)_M0L6_2atmpS2098;
          _M0L6bufferS288[_M0L6_2atmpS2096] = _M0L6_2atmpS2097;
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
  int32_t _M0L6_2atmpS2046;
  int32_t _M0L6_2atmpS2045;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS261 = *(uint32_t*)&_M0L5radixS262;
  _M0L6_2atmpS2046 = _M0L5radixS262 - 1;
  _M0L6_2atmpS2045 = _M0L5radixS262 & _M0L6_2atmpS2046;
  if (_M0L6_2atmpS2045 == 0) {
    int32_t _M0L5shiftS263;
    uint32_t _M0L4maskS264;
    int32_t _M0L6_2atmpS2053;
    int32_t _M0L6offsetS265;
    uint32_t _M0L1nS266;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS263 = moonbit_ctz32(_M0L5radixS262);
    _M0L4maskS264 = _M0L4baseS261 - 1u;
    _M0L6_2atmpS2053 = _M0L10total__lenS271 - _M0L12digit__startS269;
    _M0L6offsetS265 = _M0L6_2atmpS2053;
    _M0L1nS266 = _M0L3numS272;
    while (1) {
      if (_M0L1nS266 > 0u) {
        uint32_t _M0L6_2atmpS2052 = _M0L1nS266 & _M0L4maskS264;
        int32_t _M0L5digitS267 = *(int32_t*)&_M0L6_2atmpS2052;
        int32_t _M0L6_2atmpS2049 = _M0L12digit__startS269 + _M0L6offsetS265;
        int32_t _M0L6_2atmpS2047 = _M0L6_2atmpS2049 - 1;
        int32_t _M0L6_2atmpS2048 =
          ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L5digitS267];
        int32_t _M0L6_2atmpS2050;
        uint32_t _M0L6_2atmpS2051;
        _M0L6bufferS268[_M0L6_2atmpS2047] = _M0L6_2atmpS2048;
        _M0L6_2atmpS2050 = _M0L6offsetS265 - 1;
        _M0L6_2atmpS2051 = _M0L1nS266 >> (_M0L5shiftS263 & 31);
        _M0L6offsetS265 = _M0L6_2atmpS2050;
        _M0L1nS266 = _M0L6_2atmpS2051;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2060 = _M0L10total__lenS271 - _M0L12digit__startS269;
    int32_t _M0L6offsetS273 = _M0L6_2atmpS2060;
    uint32_t _M0L1nS274 = _M0L3numS272;
    while (1) {
      if (_M0L1nS274 > 0u) {
        uint32_t _M0L1qS275 = _M0L1nS274 / _M0L4baseS261;
        uint32_t _M0L6_2atmpS2059 = _M0L1qS275 * _M0L4baseS261;
        uint32_t _M0L6_2atmpS2058 = _M0L1nS274 - _M0L6_2atmpS2059;
        int32_t _M0L5digitS276 = *(int32_t*)&_M0L6_2atmpS2058;
        int32_t _M0L6_2atmpS2056 = _M0L12digit__startS269 + _M0L6offsetS273;
        int32_t _M0L6_2atmpS2054 = _M0L6_2atmpS2056 - 1;
        int32_t _M0L6_2atmpS2055 =
          ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L5digitS276];
        int32_t _M0L6_2atmpS2057;
        _M0L6bufferS268[_M0L6_2atmpS2054] = _M0L6_2atmpS2055;
        _M0L6_2atmpS2057 = _M0L6offsetS273 - 1;
        _M0L6offsetS273 = _M0L6_2atmpS2057;
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
  int32_t _M0L6_2atmpS2044;
  int32_t _M0L6offsetS250;
  uint32_t _M0L1nS251;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2044 = _M0L10total__lenS259 - _M0L12digit__startS256;
  _M0L6offsetS250 = _M0L6_2atmpS2044;
  _M0L1nS251 = _M0L3numS260;
  while (1) {
    if (_M0L6offsetS250 >= 2) {
      uint32_t _M0L6_2atmpS2041 = _M0L1nS251 & 255u;
      int32_t _M0L9byte__valS252 = *(int32_t*)&_M0L6_2atmpS2041;
      int32_t _M0L2hiS253 = _M0L9byte__valS252 / 16;
      int32_t _M0L2loS254 = _M0L9byte__valS252 % 16;
      int32_t _M0L6_2atmpS2035 = _M0L12digit__startS256 + _M0L6offsetS250;
      int32_t _M0L6_2atmpS2033 = _M0L6_2atmpS2035 - 2;
      int32_t _M0L6_2atmpS2034 =
        ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L2hiS253];
      int32_t _M0L6_2atmpS2038;
      int32_t _M0L6_2atmpS2036;
      int32_t _M0L6_2atmpS2037;
      int32_t _M0L6_2atmpS2039;
      uint32_t _M0L6_2atmpS2040;
      _M0L6bufferS255[_M0L6_2atmpS2033] = _M0L6_2atmpS2034;
      _M0L6_2atmpS2038 = _M0L12digit__startS256 + _M0L6offsetS250;
      _M0L6_2atmpS2036 = _M0L6_2atmpS2038 - 1;
      _M0L6_2atmpS2037
      = ((moonbit_string_t)moonbit_string_literal_15.data)[
        _M0L2loS254
      ];
      _M0L6bufferS255[_M0L6_2atmpS2036] = _M0L6_2atmpS2037;
      _M0L6_2atmpS2039 = _M0L6offsetS250 - 2;
      _M0L6_2atmpS2040 = _M0L1nS251 >> 8;
      _M0L6offsetS250 = _M0L6_2atmpS2039;
      _M0L1nS251 = _M0L6_2atmpS2040;
      continue;
    } else if (_M0L6offsetS250 == 1) {
      uint32_t _M0L6_2atmpS2043 = _M0L1nS251 & 15u;
      int32_t _M0L6nibbleS258 = *(int32_t*)&_M0L6_2atmpS2043;
      int32_t _M0L6_2atmpS2042 =
        ((moonbit_string_t)moonbit_string_literal_15.data)[_M0L6nibbleS258];
      _M0L6bufferS255[_M0L12digit__startS256] = _M0L6_2atmpS2042;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS249
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS248;
  struct _M0TPB6Logger _M0L6_2atmpS2032;
  moonbit_string_t _result_5374;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS248);
  _M0L6_2atmpS2032
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS248
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS249, _M0L6_2atmpS2032);
  if (_M0L6_2atmpS2032.$1) {
    moonbit_decref(_M0L6_2atmpS2032.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_5374 = _M0MPB13StringBuilder10to__string(_M0L6loggerS248);
  moonbit_decref_cycle_free(_M0L6loggerS248);
  return _result_5374;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS245,
  struct _M0TPB6Logger _M0L6loggerS244
) {
  moonbit_string_t _M0L6_2atmpS2030;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2030 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS245);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS244.$0->$method_0(_M0L6loggerS244.$1, _M0L6_2atmpS2030);
  moonbit_decref_cycle_free(_M0L6_2atmpS2030);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS247,
  struct _M0TPB6Logger _M0L6loggerS246
) {
  moonbit_string_t _M0L6_2atmpS2031;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2031 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS247);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS246.$0->$method_0(_M0L6loggerS246.$1, _M0L6_2atmpS2031);
  moonbit_decref_cycle_free(_M0L6_2atmpS2031);
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
  moonbit_string_t _M0L8_2afieldS4941;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS4941 = _M0L4selfS242.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS4941);
  return _M0L8_2afieldS4941;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS238,
  moonbit_string_t _M0L5valueS239,
  int32_t _M0L5startS240,
  int32_t _M0L3lenS241
) {
  int32_t _M0L6_2atmpS2029;
  int64_t _M0L6_2atmpS2028;
  struct _M0TPC16string10StringView _M0L6_2atmpS2027;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2029 = _M0L5startS240 + _M0L3lenS241;
  _M0L6_2atmpS2028 = (int64_t)_M0L6_2atmpS2029;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2027
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS239, _M0L5startS240, _M0L6_2atmpS2028);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS238, _M0L6_2atmpS2027);
  moonbit_decref_cycle_free(_M0L6_2atmpS2027.$0);
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
  int32_t _M0L6_2atmpS2011;
  int32_t _if__result_5375;
  int32_t _M0L6_2atmpS2019;
  int32_t _if__result_5376;
  int32_t _M0L6_2atmpS2021;
  int32_t _M0L6_2atmpS2022;
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
  _M0L6_2atmpS2011 = _M0Lm2loS232;
  if (_M0L6_2atmpS2011 > 0) {
    int32_t _M0L6_2atmpS2010 = _M0Lm2loS232;
    if (_M0L6_2atmpS2010 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS2009 = _M0Lm2loS232;
      int32_t _M0L6_2atmpS2008 = _M0L4selfS231[_M0L6_2atmpS2009];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2008)) {
        int32_t _M0L6_2atmpS2007 = _M0Lm2loS232;
        int32_t _M0L6_2atmpS2006 = _M0L6_2atmpS2007 - 1;
        int32_t _M0L6_2atmpS2005 = _M0L4selfS231[_M0L6_2atmpS2006];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_5375
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2005);
      } else {
        _if__result_5375 = 0;
      }
    } else {
      _if__result_5375 = 0;
    }
  } else {
    _if__result_5375 = 0;
  }
  if (_if__result_5375) {
    int32_t _M0L6_2atmpS2012 = _M0Lm2loS232;
    _M0Lm2loS232 = _M0L6_2atmpS2012 + 1;
  }
  _M0L6_2atmpS2019 = _M0Lm2hiS234;
  if (_M0L6_2atmpS2019 > 0) {
    int32_t _M0L6_2atmpS2018 = _M0Lm2hiS234;
    if (_M0L6_2atmpS2018 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS2017 = _M0Lm2hiS234;
      int32_t _M0L6_2atmpS2016 = _M0L4selfS231[_M0L6_2atmpS2017];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2016)) {
        int32_t _M0L6_2atmpS2015 = _M0Lm2hiS234;
        int32_t _M0L6_2atmpS2014 = _M0L6_2atmpS2015 - 1;
        int32_t _M0L6_2atmpS2013 = _M0L4selfS231[_M0L6_2atmpS2014];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_5376
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2013);
      } else {
        _if__result_5376 = 0;
      }
    } else {
      _if__result_5376 = 0;
    }
  } else {
    _if__result_5376 = 0;
  }
  if (_if__result_5376) {
    int32_t _M0L6_2atmpS2020 = _M0Lm2hiS234;
    _M0Lm2hiS234 = _M0L6_2atmpS2020 - 1;
  }
  _M0L6_2atmpS2021 = _M0Lm2loS232;
  _M0L6_2atmpS2022 = _M0Lm2hiS234;
  if (_M0L6_2atmpS2021 >= _M0L6_2atmpS2022) {
    int32_t _M0L6_2atmpS2023 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS2024 = _M0Lm2loS232;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS2023,
                                                 .$2 = _M0L6_2atmpS2024};
  } else {
    int32_t _M0L6_2atmpS2025 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS2026 = _M0Lm2hiS234;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS2025,
                                                 .$2 = _M0L6_2atmpS2026};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS229,
  struct _M0TPB4Show _M0L4showS228
) {
  struct _M0TPB6Logger _M0L6_2atmpS2004;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS229);
  _M0L6_2atmpS2004
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS229
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS228.$0->$method_0(_M0L4showS228.$1, _M0L6_2atmpS2004);
  if (_M0L6_2atmpS2004.$1) {
    moonbit_decref(_M0L6_2atmpS2004.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS227,
  struct _M0TPB4Show _M0L4showS226
) {
  struct _M0TPB6Logger _M0L6_2atmpS2003;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS2003
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS227
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS226.$0->$method_0(_M0L4showS226.$1, _M0L6_2atmpS2003);
  if (_M0L6_2atmpS2003.$1) {
    moonbit_decref(_M0L6_2atmpS2003.$1);
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
  int32_t _M0L6_2atmpS2002;
  struct _M0TPC16string10StringView _M0L6_2atmpS2000;
  struct _M0TPB6Logger _M0L6_2atmpS2001;
  moonbit_string_t _result_5377;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS223 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS2002 = Moonbit_array_length(_M0L4selfS224);
  moonbit_incref_cycle_free(_M0L4selfS224);
  _M0L6_2atmpS2000
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS224, .$1 = 0, .$2 = _M0L6_2atmpS2002
  };
  moonbit_incref_cycle_free(_M0L3bufS223);
  _M0L6_2atmpS2001
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS223
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS2000, _M0L6_2atmpS2001, _M0L5quoteS225);
  moonbit_decref_cycle_free(_M0L6_2atmpS2000.$0);
  if (_M0L6_2atmpS2001.$1) {
    moonbit_decref(_M0L6_2atmpS2001.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_5377 = _M0MPB13StringBuilder10to__string(_M0L3bufS223);
  moonbit_decref_cycle_free(_M0L3bufS223);
  return _result_5377;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS215,
  struct _M0TPB6Logger _M0L6loggerS213,
  int32_t _M0L5quoteS212
) {
  int32_t _M0L3endS1998;
  int32_t _M0L5startS1999;
  int32_t _M0L3lenS214;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS216;
  int32_t _M0L1iS217;
  int32_t _M0L3segS218;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS212) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 34);
  }
  _M0L3endS1998 = _M0L4selfS215.$2;
  _M0L5startS1999 = _M0L4selfS215.$1;
  _M0L3lenS214 = _M0L3endS1998 - _M0L5startS1999;
  moonbit_incref_cycle_free(_M0L4selfS215.$0);
  if (_M0L6loggerS213.$1) {
    moonbit_incref(_M0L6loggerS213.$1);
  }
  _M0L6_2aenvS216
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS216)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 112, 0);
  _M0L6_2aenvS216->$0 = _M0L4selfS215;
  _M0L6_2aenvS216->$1 = _M0L6loggerS213;
  _M0L1iS217 = 0;
  _M0L3segS218 = 0;
  _2afor_219:;
  while (1) {
    moonbit_string_t _M0L3strS1995;
    int32_t _M0L5startS1997;
    int32_t _M0L6_2atmpS1996;
    int32_t _M0L4codeS220;
    int32_t _M0L1cS222;
    int32_t _M0L6_2atmpS1979;
    int32_t _M0L6_2atmpS1980;
    int32_t _M0L6_2atmpS1981;
    if (_M0L1iS217 >= _M0L3lenS214) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
      moonbit_decref_cycle_free(_M0L6_2aenvS216);
      break;
    }
    _M0L3strS1995 = _M0L4selfS215.$0;
    _M0L5startS1997 = _M0L4selfS215.$1;
    _M0L6_2atmpS1996 = _M0L5startS1997 + _M0L1iS217;
    _M0L4codeS220 = _M0L3strS1995[_M0L6_2atmpS1996];
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
        int32_t _M0L6_2atmpS1982;
        int32_t _M0L6_2atmpS1983;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_16.data);
        _M0L6_2atmpS1982 = _M0L1iS217 + 1;
        _M0L6_2atmpS1983 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1982;
        _M0L3segS218 = _M0L6_2atmpS1983;
        goto _2afor_219;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1984;
        int32_t _M0L6_2atmpS1985;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_17.data);
        _M0L6_2atmpS1984 = _M0L1iS217 + 1;
        _M0L6_2atmpS1985 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1984;
        _M0L3segS218 = _M0L6_2atmpS1985;
        goto _2afor_219;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1986;
        int32_t _M0L6_2atmpS1987;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1986 = _M0L1iS217 + 1;
        _M0L6_2atmpS1987 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1986;
        _M0L3segS218 = _M0L6_2atmpS1987;
        goto _2afor_219;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1988;
        int32_t _M0L6_2atmpS1989;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1988 = _M0L1iS217 + 1;
        _M0L6_2atmpS1989 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1988;
        _M0L3segS218 = _M0L6_2atmpS1989;
        goto _2afor_219;
        break;
      }
      default: {
        if (_M0L4codeS220 < 32) {
          int32_t _M0L6_2atmpS1991;
          moonbit_string_t _M0L6_2atmpS1990;
          int32_t _M0L6_2atmpS1992;
          int32_t _M0L6_2atmpS1993;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_20.data);
          _M0L6_2atmpS1991 = _M0L4codeS220 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1990 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1991);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, _M0L6_2atmpS1990);
          moonbit_decref_cycle_free(_M0L6_2atmpS1990);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1992 = _M0L1iS217 + 1;
          _M0L6_2atmpS1993 = _M0L1iS217 + 1;
          _M0L1iS217 = _M0L6_2atmpS1992;
          _M0L3segS218 = _M0L6_2atmpS1993;
          goto _2afor_219;
        } else {
          int32_t _M0L6_2atmpS1994 = _M0L1iS217 + 1;
          int32_t _tmp_5380 = _M0L3segS218;
          _M0L1iS217 = _M0L6_2atmpS1994;
          _M0L3segS218 = _tmp_5380;
          goto _2afor_219;
        }
        break;
      }
    }
    goto joinlet_5379;
    join_221:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1979 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS222);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, _M0L6_2atmpS1979);
    _M0L6_2atmpS1980 = _M0L1iS217 + 1;
    _M0L6_2atmpS1981 = _M0L1iS217 + 1;
    _M0L1iS217 = _M0L6_2atmpS1980;
    _M0L3segS218 = _M0L6_2atmpS1981;
    continue;
    joinlet_5379:;
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
    int64_t _M0L6_2atmpS1978 = (int64_t)_M0L1iS210;
    struct _M0TPC16string10StringView _M0L6_2atmpS1977;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1977
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS209, _M0L3segS211, _M0L6_2atmpS1978);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS207.$0->$method_2(_M0L6loggerS207.$1, _M0L6_2atmpS1977);
    moonbit_decref_cycle_free(_M0L6_2atmpS1977.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS198,
  int32_t _M0L5startS200,
  int64_t _M0L3endS202
) {
  int32_t _M0L3endS1975;
  int32_t _M0L5startS1976;
  int32_t _M0L3lenS197;
  int32_t _M0Lm2loS199;
  int32_t _M0Lm2hiS201;
  moonbit_string_t _M0L3strS205;
  int32_t _M0L4baseS206;
  int32_t _M0L6_2atmpS1953;
  int32_t _if__result_5381;
  int32_t _M0L6_2atmpS1963;
  int32_t _if__result_5382;
  int32_t _M0L6_2atmpS1965;
  int32_t _M0L6_2atmpS1966;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1975 = _M0L4selfS198.$2;
  _M0L5startS1976 = _M0L4selfS198.$1;
  _M0L3lenS197 = _M0L3endS1975 - _M0L5startS1976;
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
  _M0L6_2atmpS1953 = _M0Lm2loS199;
  if (_M0L6_2atmpS1953 > 0) {
    int32_t _M0L6_2atmpS1952 = _M0Lm2loS199;
    if (_M0L6_2atmpS1952 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1951 = _M0Lm2loS199;
      int32_t _M0L6_2atmpS1950 = _M0L4baseS206 + _M0L6_2atmpS1951;
      int32_t _M0L6_2atmpS1949 = _M0L3strS205[_M0L6_2atmpS1950];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1949)) {
        int32_t _M0L6_2atmpS1948 = _M0Lm2loS199;
        int32_t _M0L6_2atmpS1947 = _M0L4baseS206 + _M0L6_2atmpS1948;
        int32_t _M0L6_2atmpS1946 = _M0L6_2atmpS1947 - 1;
        int32_t _M0L6_2atmpS1945 = _M0L3strS205[_M0L6_2atmpS1946];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_5381
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1945);
      } else {
        _if__result_5381 = 0;
      }
    } else {
      _if__result_5381 = 0;
    }
  } else {
    _if__result_5381 = 0;
  }
  if (_if__result_5381) {
    int32_t _M0L6_2atmpS1954 = _M0Lm2loS199;
    _M0Lm2loS199 = _M0L6_2atmpS1954 + 1;
  }
  _M0L6_2atmpS1963 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1963 > 0) {
    int32_t _M0L6_2atmpS1962 = _M0Lm2hiS201;
    if (_M0L6_2atmpS1962 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1961 = _M0Lm2hiS201;
      int32_t _M0L6_2atmpS1960 = _M0L4baseS206 + _M0L6_2atmpS1961;
      int32_t _M0L6_2atmpS1959 = _M0L3strS205[_M0L6_2atmpS1960];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1959)) {
        int32_t _M0L6_2atmpS1958 = _M0Lm2hiS201;
        int32_t _M0L6_2atmpS1957 = _M0L4baseS206 + _M0L6_2atmpS1958;
        int32_t _M0L6_2atmpS1956 = _M0L6_2atmpS1957 - 1;
        int32_t _M0L6_2atmpS1955 = _M0L3strS205[_M0L6_2atmpS1956];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_5382
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1955);
      } else {
        _if__result_5382 = 0;
      }
    } else {
      _if__result_5382 = 0;
    }
  } else {
    _if__result_5382 = 0;
  }
  if (_if__result_5382) {
    int32_t _M0L6_2atmpS1964 = _M0Lm2hiS201;
    _M0Lm2hiS201 = _M0L6_2atmpS1964 - 1;
  }
  _M0L6_2atmpS1965 = _M0Lm2loS199;
  _M0L6_2atmpS1966 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1965 >= _M0L6_2atmpS1966) {
    int32_t _M0L6_2atmpS1970 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1967 = _M0L4baseS206 + _M0L6_2atmpS1970;
    int32_t _M0L6_2atmpS1969 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1968 = _M0L4baseS206 + _M0L6_2atmpS1969;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1967,
                                                 .$2 = _M0L6_2atmpS1968};
  } else {
    int32_t _M0L6_2atmpS1974 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1971 = _M0L4baseS206 + _M0L6_2atmpS1974;
    int32_t _M0L6_2atmpS1973 = _M0Lm2hiS201;
    int32_t _M0L6_2atmpS1972 = _M0L4baseS206 + _M0L6_2atmpS1973;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1971,
                                                 .$2 = _M0L6_2atmpS1972};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS196) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS195;
  int32_t _M0L6_2atmpS1942;
  int32_t _M0L6_2atmpS1941;
  int32_t _M0L6_2atmpS1944;
  int32_t _M0L6_2atmpS1943;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1940;
  moonbit_string_t _result_5383;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS195 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1942 = _M0IPC14byte4BytePB3Div3div(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1941
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1942);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1941);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1944 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1943
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1944);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1943);
  _M0L6_2atmpS1940 = _M0L7_2aselfS195;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_5383 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1940);
  moonbit_decref_cycle_free(_M0L6_2atmpS1940);
  return _result_5383;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS194) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS194 < 10) {
    int32_t _M0L6_2atmpS1937;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1937 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1937);
  } else {
    int32_t _M0L6_2atmpS1939;
    int32_t _M0L6_2atmpS1938;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1939 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1938 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1939, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1938);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS192,
  int32_t _M0L4thatS193
) {
  int32_t _M0L6_2atmpS1935;
  int32_t _M0L6_2atmpS1936;
  int32_t _M0L6_2atmpS1934;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1935 = (int32_t)_M0L4selfS192;
  _M0L6_2atmpS1936 = (int32_t)_M0L4thatS193;
  _M0L6_2atmpS1934 = _M0L6_2atmpS1935 - _M0L6_2atmpS1936;
  return _M0L6_2atmpS1934 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS190,
  int32_t _M0L4thatS191
) {
  int32_t _M0L6_2atmpS1932;
  int32_t _M0L6_2atmpS1933;
  int32_t _M0L6_2atmpS1931;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1932 = (int32_t)_M0L4selfS190;
  _M0L6_2atmpS1933 = (int32_t)_M0L4thatS191;
  _M0L6_2atmpS1931 = _M0L6_2atmpS1932 % _M0L6_2atmpS1933;
  return _M0L6_2atmpS1931 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS188,
  int32_t _M0L4thatS189
) {
  int32_t _M0L6_2atmpS1929;
  int32_t _M0L6_2atmpS1930;
  int32_t _M0L6_2atmpS1928;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1929 = (int32_t)_M0L4selfS188;
  _M0L6_2atmpS1930 = (int32_t)_M0L4thatS189;
  _M0L6_2atmpS1928 = _M0L6_2atmpS1929 / _M0L6_2atmpS1930;
  return _M0L6_2atmpS1928 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS186,
  int32_t _M0L4thatS187
) {
  int32_t _M0L6_2atmpS1926;
  int32_t _M0L6_2atmpS1927;
  int32_t _M0L6_2atmpS1925;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1926 = (int32_t)_M0L4selfS186;
  _M0L6_2atmpS1927 = (int32_t)_M0L4thatS187;
  _M0L6_2atmpS1925 = _M0L6_2atmpS1926 + _M0L6_2atmpS1927;
  return _M0L6_2atmpS1925 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS185) {
  int32_t _M0L6_2atmpS1924;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1924 = (int32_t)_M0L4selfS185;
  return _M0L6_2atmpS1924;
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
  int32_t _M0L3lenS1923;
  int32_t _M0L8requiredS181;
  uint16_t* _M0L4dataS1918;
  int32_t _M0L6_2atmpS1917;
  int32_t _if__result_5384;
  uint16_t* _M0L4dataS1919;
  int32_t _M0L3lenS1920;
  int32_t _M0L3lenS1922;
  int32_t _M0L6_2atmpS1921;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS179 = Moonbit_array_length(_M0L3strS180);
  if (_M0L8str__lenS179 == 0) {
    return 0;
  }
  _M0L3lenS1923 = _M0L4selfS182->$1;
  _M0L8requiredS181 = _M0L3lenS1923 + _M0L8str__lenS179;
  _M0L4dataS1918 = _M0L4selfS182->$0;
  _M0L6_2atmpS1917 = Moonbit_array_length(_M0L4dataS1918);
  if (_M0L8requiredS181 > _M0L6_2atmpS1917) {
    _if__result_5384 = 1;
  } else {
    int32_t _M0L3lenS1916 = _M0L4selfS182->$1;
    _if__result_5384 = _M0L8requiredS181 < _M0L3lenS1916;
  }
  if (_if__result_5384) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS182, _M0L8requiredS181);
  }
  _M0L4dataS1919 = _M0L4selfS182->$0;
  _M0L3lenS1920 = _M0L4selfS182->$1;
  moonbit_incref_cycle_free(_M0L4dataS1919);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1919, _M0L3lenS1920, _M0L3strS180, 0, _M0L8str__lenS179);
  moonbit_decref_cycle_free(_M0L4dataS1919);
  _M0L3lenS1922 = _M0L4selfS182->$1;
  _M0L6_2atmpS1921 = _M0L3lenS1922 + _M0L8str__lenS179;
  _M0L4selfS182->$1 = _M0L6_2atmpS1921;
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
      int32_t _M0L6_2atmpS1913 = _M0L3strS176[_M0L1iS173];
      int32_t _M0L6_2atmpS1914;
      int32_t _M0L6_2atmpS1915;
      _M0L4selfS175[_M0L1jS174] = _M0L6_2atmpS1913;
      _M0L6_2atmpS1914 = _M0L1iS173 + 1;
      _M0L6_2atmpS1915 = _M0L1jS174 + 1;
      _M0L1iS173 = _M0L6_2atmpS1914;
      _M0L1jS174 = _M0L6_2atmpS1915;
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
    int32_t _M0L3lenS1884 = _M0L4selfS168->$1;
    uint16_t* _M0L4dataS1886 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1885 = Moonbit_array_length(_M0L4dataS1886);
    uint16_t* _M0L4dataS1889;
    int32_t _M0L3lenS1890;
    int32_t _M0L6_2atmpS1891;
    int32_t _M0L3lenS1893;
    int32_t _M0L6_2atmpS1892;
    if (_M0L3lenS1884 >= _M0L6_2atmpS1885) {
      int32_t _M0L3lenS1888 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1887 = _M0L3lenS1888 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1887);
    }
    _M0L4dataS1889 = _M0L4selfS168->$0;
    _M0L3lenS1890 = _M0L4selfS168->$1;
    moonbit_incref_cycle_free(_M0L4dataS1889);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1891 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS166);
    if (
      _M0L3lenS1890 < 0
      || _M0L3lenS1890 >= Moonbit_array_length(_M0L4dataS1889)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1889[_M0L3lenS1890] = _M0L6_2atmpS1891;
    moonbit_decref_cycle_free(_M0L4dataS1889);
    _M0L3lenS1893 = _M0L4selfS168->$1;
    _M0L6_2atmpS1892 = _M0L3lenS1893 + 1;
    _M0L4selfS168->$1 = _M0L6_2atmpS1892;
  } else if (_M0L4codeS166 <= 1114111u) {
    uint16_t* _M0L4dataS1897 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1895 = Moonbit_array_length(_M0L4dataS1897);
    int32_t _M0L3lenS1896 = _M0L4selfS168->$1;
    int32_t _M0L6_2atmpS1894 = _M0L6_2atmpS1895 - _M0L3lenS1896;
    uint32_t _M0L4codeS169;
    uint16_t* _M0L4dataS1900;
    int32_t _M0L3lenS1901;
    uint32_t _M0L6_2atmpS1904;
    uint32_t _M0L6_2atmpS1903;
    int32_t _M0L6_2atmpS1902;
    uint16_t* _M0L4dataS1905;
    int32_t _M0L3lenS1910;
    int32_t _M0L6_2atmpS1906;
    uint32_t _M0L6_2atmpS1909;
    uint32_t _M0L6_2atmpS1908;
    int32_t _M0L6_2atmpS1907;
    int32_t _M0L3lenS1912;
    int32_t _M0L6_2atmpS1911;
    if (_M0L6_2atmpS1894 < 2) {
      int32_t _M0L3lenS1899 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1898 = _M0L3lenS1899 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1898);
    }
    _M0L4codeS169 = _M0L4codeS166 - 65536u;
    _M0L4dataS1900 = _M0L4selfS168->$0;
    _M0L3lenS1901 = _M0L4selfS168->$1;
    _M0L6_2atmpS1904 = _M0L4codeS169 >> 10;
    _M0L6_2atmpS1903 = 55296u + _M0L6_2atmpS1904;
    moonbit_incref_cycle_free(_M0L4dataS1900);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1902 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1903);
    if (
      _M0L3lenS1901 < 0
      || _M0L3lenS1901 >= Moonbit_array_length(_M0L4dataS1900)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1900[_M0L3lenS1901] = _M0L6_2atmpS1902;
    moonbit_decref_cycle_free(_M0L4dataS1900);
    _M0L4dataS1905 = _M0L4selfS168->$0;
    _M0L3lenS1910 = _M0L4selfS168->$1;
    _M0L6_2atmpS1906 = _M0L3lenS1910 + 1;
    _M0L6_2atmpS1909 = _M0L4codeS169 & 1023u;
    _M0L6_2atmpS1908 = 56320u + _M0L6_2atmpS1909;
    moonbit_incref_cycle_free(_M0L4dataS1905);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1907 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1908);
    if (
      _M0L6_2atmpS1906 < 0
      || _M0L6_2atmpS1906 >= Moonbit_array_length(_M0L4dataS1905)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1905[_M0L6_2atmpS1906] = _M0L6_2atmpS1907;
    moonbit_decref_cycle_free(_M0L4dataS1905);
    _M0L3lenS1912 = _M0L4selfS168->$1;
    _M0L6_2atmpS1911 = _M0L3lenS1912 + 2;
    _M0L4selfS168->$1 = _M0L6_2atmpS1911;
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
  uint16_t* _M0L4dataS1883;
  int32_t _M0L6_2atmpS1881;
  int32_t _M0L3lenS1882;
  int32_t _M0L13new__capacityS162;
  uint16_t* _M0L4dataS1878;
  int32_t _M0L6_2atmpS1879;
  int32_t _M0L3lenS1880;
  uint16_t* _M0L9new__dataS165;
  uint16_t* _M0L6_2aoldS4942;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1883 = _M0L4selfS163->$0;
  _M0L6_2atmpS1881 = Moonbit_array_length(_M0L4dataS1883);
  _M0L3lenS1882 = _M0L4selfS163->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS162
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1881, _M0L3lenS1882, _M0L8requiredS164);
  _M0L4dataS1878 = _M0L4selfS163->$0;
  moonbit_incref_cycle_free(_M0L4dataS1878);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1879 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1880 = _M0L4selfS163->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS165
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1878, _M0L13new__capacityS162, _M0L6_2atmpS1879, _M0L3lenS1880, 0, 0);
  _M0L6_2aoldS4942 = _M0L4selfS163->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS4942);
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
  int32_t _M0L6_2atmpS1877;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1877 = *(int32_t*)&_M0L4selfS155;
  return (uint16_t)_M0L6_2atmpS1877;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS154) {
  int32_t _M0L6_2atmpS1876;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1876 = _M0L4selfS154;
  return *(uint32_t*)&_M0L6_2atmpS1876;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS152
) {
  int32_t _M0L3lenS1867;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1867 = _M0L4selfS152->$1;
  if (_M0L3lenS1867 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1868 = _M0L4selfS152->$1;
    uint16_t* _M0L4dataS1870 = _M0L4selfS152->$0;
    int32_t _M0L6_2atmpS1869 = Moonbit_array_length(_M0L4dataS1870);
    if (_M0L3lenS1868 == _M0L6_2atmpS1869) {
      uint16_t* _M0L4dataS1871 = _M0L4selfS152->$0;
      moonbit_incref_cycle_free(_M0L4dataS1871);
      return _M0L4dataS1871;
    } else {
      uint16_t* _M0L4dataS1872 = _M0L4selfS152->$0;
      int32_t _M0L3lenS1873 = _M0L4selfS152->$1;
      int32_t _M0L6_2atmpS1874;
      int32_t _M0L3lenS1875;
      uint16_t* _M0L4dataS153;
      moonbit_incref_cycle_free(_M0L4dataS1872);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1874 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1875 = _M0L4selfS152->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS153
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1872, _M0L3lenS1873, _M0L6_2atmpS1874, _M0L3lenS1875, 0, 0);
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
  int32_t _if__result_5387;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS145 >= 0) {
    if (_M0L3lenS146 >= 0) {
      if (_M0L11src__offsetS147 >= 0) {
        if (_M0L11dst__offsetS148 >= 0) {
          int32_t _M0L6_2atmpS1863 = _M0L11src__offsetS147 + _M0L3lenS146;
          int32_t _M0L6_2atmpS1864 = Moonbit_array_length(_M0L3srcS149);
          if (_M0L6_2atmpS1863 <= _M0L6_2atmpS1864) {
            int32_t _M0L6_2atmpS1862 = _M0L11dst__offsetS148 + _M0L3lenS146;
            _if__result_5387 = _M0L6_2atmpS1862 <= _M0L13allocate__lenS145;
          } else {
            _if__result_5387 = 0;
          }
        } else {
          _if__result_5387 = 0;
        }
      } else {
        _if__result_5387 = 0;
      }
    } else {
      _if__result_5387 = 0;
    }
  } else {
    _if__result_5387 = 0;
  }
  if (_if__result_5387) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS149, _M0L13allocate__lenS145, _M0L4initS150, _M0L11src__offsetS147, _M0L11dst__offsetS148, _M0L3lenS146);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS151;
    int32_t _M0L6_2atmpS1866;
    moonbit_string_t _M0L6_2atmpS1865;
    uint16_t* _result_5388;
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
    _M0L6_2atmpS1866 = Moonbit_array_length(_M0L3srcS149);
    moonbit_decref_cycle_free(_M0L3srcS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L6_2atmpS1866);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1865
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS151);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS151);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_5388 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1865);
    moonbit_decref_cycle_free(_M0L6_2atmpS1865);
    return _result_5388;
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
  struct _M0TPB13StringBuilder* _block_5389;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS136 < 1) {
    _M0L7initialS135 = 1;
  } else {
    int32_t _M0L6_2atmpS1861 = _M0L10size__hintS136 + 1;
    _M0L7initialS135 = _M0L6_2atmpS1861 / 2;
  }
  _M0L4dataS137 = (uint16_t*)moonbit_make_string(_M0L7initialS135, 0);
  _block_5389
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_5389)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 117, 0);
  _block_5389->$0 = _M0L4dataS137;
  _block_5389->$1 = 0;
  return _block_5389;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS134) {
  int32_t _M0L6_2atmpS1860;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1860 = (int32_t)_M0L4selfS134;
  return _M0L6_2atmpS1860;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS114,
  int32_t _M0L13allocate__lenS110,
  int32_t _M0L3lenS111,
  int32_t _M0L11src__offsetS112,
  int32_t _M0L11dst__offsetS113
) {
  int32_t _if__result_5390;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS110 >= 0) {
    if (_M0L3lenS111 >= 0) {
      if (_M0L11src__offsetS112 >= 0) {
        if (_M0L11dst__offsetS113 >= 0) {
          int32_t _M0L6_2atmpS1841 = _M0L11src__offsetS112 + _M0L3lenS111;
          int32_t _M0L6_2atmpS1842;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1842
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS114);
          if (_M0L6_2atmpS1841 <= _M0L6_2atmpS1842) {
            int32_t _M0L6_2atmpS1840 = _M0L11dst__offsetS113 + _M0L3lenS111;
            _if__result_5390 = _M0L6_2atmpS1840 <= _M0L13allocate__lenS110;
          } else {
            _if__result_5390 = 0;
          }
        } else {
          _if__result_5390 = 0;
        }
      } else {
        _if__result_5390 = 0;
      }
    } else {
      _if__result_5390 = 0;
    }
  } else {
    _if__result_5390 = 0;
  }
  if (_if__result_5390) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS110, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS114, _M0L11src__offsetS112, _M0L11dst__offsetS113, _M0L3lenS111);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS115;
    int32_t _M0L6_2atmpS1844;
    moonbit_string_t _M0L6_2atmpS1843;
    moonbit_string_t* _result_5391;
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
    _M0L6_2atmpS1844 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS114);
    moonbit_decref_cycle_free(_M0L3srcS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L6_2atmpS1844);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1843
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS115);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS115);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5391
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1843);
    moonbit_decref_cycle_free(_M0L6_2atmpS1843);
    return _result_5391;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS120,
  int32_t _M0L13allocate__lenS116,
  int32_t _M0L3lenS117,
  int32_t _M0L11src__offsetS118,
  int32_t _M0L11dst__offsetS119
) {
  int32_t _if__result_5392;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS116 >= 0) {
    if (_M0L3lenS117 >= 0) {
      if (_M0L11src__offsetS118 >= 0) {
        if (_M0L11dst__offsetS119 >= 0) {
          int32_t _M0L6_2atmpS1846 = _M0L11src__offsetS118 + _M0L3lenS117;
          int32_t _M0L6_2atmpS1847;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1847
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS120);
          if (_M0L6_2atmpS1846 <= _M0L6_2atmpS1847) {
            int32_t _M0L6_2atmpS1845 = _M0L11dst__offsetS119 + _M0L3lenS117;
            _if__result_5392 = _M0L6_2atmpS1845 <= _M0L13allocate__lenS116;
          } else {
            _if__result_5392 = 0;
          }
        } else {
          _if__result_5392 = 0;
        }
      } else {
        _if__result_5392 = 0;
      }
    } else {
      _if__result_5392 = 0;
    }
  } else {
    _if__result_5392 = 0;
  }
  if (_if__result_5392) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS116, 0, _M0L3srcS120, _M0L11src__offsetS118, _M0L11dst__offsetS119, _M0L3lenS117);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS121;
    int32_t _M0L6_2atmpS1849;
    moonbit_string_t _M0L6_2atmpS1848;
    struct _M0TUsiE** _result_5393;
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
    _M0L6_2atmpS1849 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS120);
    moonbit_decref_cycle_free(_M0L3srcS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L6_2atmpS1849);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1848
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS121);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS121);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5393
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1848);
    moonbit_decref_cycle_free(_M0L6_2atmpS1848);
    return _result_5393;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS126,
  int32_t _M0L13allocate__lenS122,
  int32_t _M0L3lenS123,
  int32_t _M0L11src__offsetS124,
  int32_t _M0L11dst__offsetS125
) {
  int32_t _if__result_5394;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS122 >= 0) {
    if (_M0L3lenS123 >= 0) {
      if (_M0L11src__offsetS124 >= 0) {
        if (_M0L11dst__offsetS125 >= 0) {
          int32_t _M0L6_2atmpS1851 = _M0L11src__offsetS124 + _M0L3lenS123;
          int32_t _M0L6_2atmpS1852;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1852
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS126);
          if (_M0L6_2atmpS1851 <= _M0L6_2atmpS1852) {
            int32_t _M0L6_2atmpS1850 = _M0L11dst__offsetS125 + _M0L3lenS123;
            _if__result_5394 = _M0L6_2atmpS1850 <= _M0L13allocate__lenS122;
          } else {
            _if__result_5394 = 0;
          }
        } else {
          _if__result_5394 = 0;
        }
      } else {
        _if__result_5394 = 0;
      }
    } else {
      _if__result_5394 = 0;
    }
  } else {
    _if__result_5394 = 0;
  }
  if (_if__result_5394) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS126, _M0L13allocate__lenS122, _M0L11src__offsetS124, _M0L11dst__offsetS125, _M0L3lenS123);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS127;
    int32_t _M0L6_2atmpS1854;
    moonbit_string_t _M0L6_2atmpS1853;
    int32_t* _result_5395;
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
    _M0L6_2atmpS1854 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS126);
    moonbit_decref_cycle_free(_M0L3srcS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L6_2atmpS1854);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1853
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS127);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS127);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5395
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1853);
    moonbit_decref_cycle_free(_M0L6_2atmpS1853);
    return _result_5395;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS132,
  int32_t _M0L13allocate__lenS128,
  int32_t _M0L3lenS129,
  int32_t _M0L11src__offsetS130,
  int32_t _M0L11dst__offsetS131
) {
  int32_t _if__result_5396;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS128 >= 0) {
    if (_M0L3lenS129 >= 0) {
      if (_M0L11src__offsetS130 >= 0) {
        if (_M0L11dst__offsetS131 >= 0) {
          int32_t _M0L6_2atmpS1856 = _M0L11src__offsetS130 + _M0L3lenS129;
          int32_t _M0L6_2atmpS1857;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1857
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS132);
          if (_M0L6_2atmpS1856 <= _M0L6_2atmpS1857) {
            int32_t _M0L6_2atmpS1855 = _M0L11dst__offsetS131 + _M0L3lenS129;
            _if__result_5396 = _M0L6_2atmpS1855 <= _M0L13allocate__lenS128;
          } else {
            _if__result_5396 = 0;
          }
        } else {
          _if__result_5396 = 0;
        }
      } else {
        _if__result_5396 = 0;
      }
    } else {
      _if__result_5396 = 0;
    }
  } else {
    _if__result_5396 = 0;
  }
  if (_if__result_5396) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS132, _M0L13allocate__lenS128, _M0L11src__offsetS130, _M0L11dst__offsetS131, _M0L3lenS129);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS133;
    int32_t _M0L6_2atmpS1859;
    moonbit_string_t _M0L6_2atmpS1858;
    float* _result_5397;
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
    _M0L6_2atmpS1859 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS132);
    moonbit_decref_cycle_free(_M0L3srcS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L6_2atmpS1859);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1858
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS133);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS133);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5397
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1858);
    moonbit_decref_cycle_free(_M0L6_2atmpS1858);
    return _result_5397;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS107,
  moonbit_string_t _M0L3objS106
) {
  struct _M0TPB6Logger _M0L6_2atmpS1838;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS107);
  _M0L6_2atmpS1838
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS107
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS106, _M0L6_2atmpS1838);
  if (_M0L6_2atmpS1838.$1) {
    moonbit_decref(_M0L6_2atmpS1838.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS109,
  int32_t _M0L3objS108
) {
  struct _M0TPB6Logger _M0L6_2atmpS1839;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS109);
  _M0L6_2atmpS1839
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS109
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS108, _M0L6_2atmpS1839);
  if (_M0L6_2atmpS1839.$1) {
    moonbit_decref(_M0L6_2atmpS1839.$1);
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS97,
  int32_t _M0L13allocate__lenS95,
  int32_t _M0L11src__offsetS98,
  int32_t _M0L11dst__offsetS96,
  int32_t _M0L9blit__lenS99
) {
  int32_t* _M0L3dstS94;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS94
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS95);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS94, _M0L11dst__offsetS96, _M0L3srcS97, _M0L11src__offsetS98, _M0L9blit__lenS99);
  moonbit_decref_cycle_free(_M0L3srcS97);
  return _M0L3dstS94;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS103,
  int32_t _M0L13allocate__lenS101,
  int32_t _M0L11src__offsetS104,
  int32_t _M0L11dst__offsetS102,
  int32_t _M0L9blit__lenS105
) {
  float* _M0L3dstS100;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS100
  = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS101);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS100, _M0L11dst__offsetS102, _M0L3srcS103, _M0L11src__offsetS104, _M0L9blit__lenS105);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS72,
  int32_t _M0L11dst__offsetS73,
  int32_t* _M0L3srcS74,
  int32_t _M0L11src__offsetS75,
  int32_t _M0L3lenS76
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS74);
  moonbit_incref_cycle_free(_M0L3dstS72);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS72, _M0L11dst__offsetS73, _M0L3srcS74, _M0L11src__offsetS75, _M0L3lenS76, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS77,
  int32_t _M0L11dst__offsetS78,
  float* _M0L3srcS79,
  int32_t _M0L11src__offsetS80,
  int32_t _M0L3lenS81
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS79);
  moonbit_incref_cycle_free(_M0L3dstS77);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS77, _M0L11dst__offsetS78, _M0L3srcS79, _M0L11src__offsetS80, _M0L3lenS81, sizeof(float));
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
        int32_t _M0L6_2atmpS1793 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS1795 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS1794;
        int32_t _M0L6_2atmpS1796;
        if (
          _M0L6_2atmpS1795 < 0
          || _M0L6_2atmpS1795 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1794 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1795];
        if (
          _M0L6_2atmpS1793 < 0
          || _M0L6_2atmpS1793 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1793] = _M0L6_2atmpS1794;
        _M0L6_2atmpS1796 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS1796;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1801 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS1801;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS1797 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS1799 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS1798;
        int32_t _M0L6_2atmpS1800;
        if (
          _M0L6_2atmpS1799 < 0
          || _M0L6_2atmpS1799 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1798 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1799];
        if (
          _M0L6_2atmpS1797 < 0
          || _M0L6_2atmpS1797 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1797] = _M0L6_2atmpS1798;
        _M0L6_2atmpS1800 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS1800;
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
        int32_t _M0L6_2atmpS1802 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1804 = _M0L11src__offsetS29 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS1803;
        moonbit_string_t _M0L6_2aoldS4943;
        int32_t _M0L6_2atmpS1805;
        if (
          _M0L6_2atmpS1804 < 0
          || _M0L6_2atmpS1804 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1803 = (moonbit_string_t)_M0L3srcS27[_M0L6_2atmpS1804];
        if (
          _M0L6_2atmpS1802 < 0
          || _M0L6_2atmpS1802 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS4943 = (moonbit_string_t)_M0L3dstS26[_M0L6_2atmpS1802];
        moonbit_incref_cycle_free(_M0L6_2atmpS1803);
        moonbit_decref_cycle_free(_M0L6_2aoldS4943);
        _M0L3dstS26[_M0L6_2atmpS1802] = _M0L6_2atmpS1803;
        _M0L6_2atmpS1805 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1805;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1810 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1810;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1806 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1808 = _M0L11src__offsetS29 + _M0L1iS33;
        moonbit_string_t _M0L6_2atmpS1807;
        moonbit_string_t _M0L6_2aoldS4944;
        int32_t _M0L6_2atmpS1809;
        if (
          _M0L6_2atmpS1808 < 0
          || _M0L6_2atmpS1808 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1807 = (moonbit_string_t)_M0L3srcS27[_M0L6_2atmpS1808];
        if (
          _M0L6_2atmpS1806 < 0
          || _M0L6_2atmpS1806 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS4944 = (moonbit_string_t)_M0L3dstS26[_M0L6_2atmpS1806];
        moonbit_incref_cycle_free(_M0L6_2atmpS1807);
        moonbit_decref_cycle_free(_M0L6_2aoldS4944);
        _M0L3dstS26[_M0L6_2atmpS1806] = _M0L6_2atmpS1807;
        _M0L6_2atmpS1809 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1809;
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
        int32_t _M0L6_2atmpS1811 = _M0L11dst__offsetS37 + _M0L1iS39;
        int32_t _M0L6_2atmpS1813 = _M0L11src__offsetS38 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS1812;
        struct _M0TUsiE* _M0L6_2aoldS4945;
        int32_t _M0L6_2atmpS1814;
        if (
          _M0L6_2atmpS1813 < 0
          || _M0L6_2atmpS1813 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1812 = (struct _M0TUsiE*)_M0L3srcS36[_M0L6_2atmpS1813];
        if (
          _M0L6_2atmpS1811 < 0
          || _M0L6_2atmpS1811 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS4945 = (struct _M0TUsiE*)_M0L3dstS35[_M0L6_2atmpS1811];
        if (_M0L6_2atmpS1812) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1812);
        }
        if (_M0L6_2aoldS4945) {
          moonbit_decref_cycle_free(_M0L6_2aoldS4945);
        }
        _M0L3dstS35[_M0L6_2atmpS1811] = _M0L6_2atmpS1812;
        _M0L6_2atmpS1814 = _M0L1iS39 + 1;
        _M0L1iS39 = _M0L6_2atmpS1814;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1819 = _M0L3lenS40 - 1;
    int32_t _M0L1iS42 = _M0L6_2atmpS1819;
    while (1) {
      if (_M0L1iS42 >= 0) {
        int32_t _M0L6_2atmpS1815 = _M0L11dst__offsetS37 + _M0L1iS42;
        int32_t _M0L6_2atmpS1817 = _M0L11src__offsetS38 + _M0L1iS42;
        struct _M0TUsiE* _M0L6_2atmpS1816;
        struct _M0TUsiE* _M0L6_2aoldS4946;
        int32_t _M0L6_2atmpS1818;
        if (
          _M0L6_2atmpS1817 < 0
          || _M0L6_2atmpS1817 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1816 = (struct _M0TUsiE*)_M0L3srcS36[_M0L6_2atmpS1817];
        if (
          _M0L6_2atmpS1815 < 0
          || _M0L6_2atmpS1815 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS4946 = (struct _M0TUsiE*)_M0L3dstS35[_M0L6_2atmpS1815];
        if (_M0L6_2atmpS1816) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1816);
        }
        if (_M0L6_2aoldS4946) {
          moonbit_decref_cycle_free(_M0L6_2aoldS4946);
        }
        _M0L3dstS35[_M0L6_2atmpS1815] = _M0L6_2atmpS1816;
        _M0L6_2atmpS1818 = _M0L1iS42 - 1;
        _M0L1iS42 = _M0L6_2atmpS1818;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS44,
  int32_t _M0L11dst__offsetS46,
  int32_t* _M0L3srcS45,
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
        int32_t _M0L6_2atmpS1820 = _M0L11dst__offsetS46 + _M0L1iS48;
        int32_t _M0L6_2atmpS1822 = _M0L11src__offsetS47 + _M0L1iS48;
        int32_t _M0L6_2atmpS1821;
        int32_t _M0L6_2atmpS1823;
        if (
          _M0L6_2atmpS1822 < 0
          || _M0L6_2atmpS1822 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1821 = (int32_t)_M0L3srcS45[_M0L6_2atmpS1822];
        if (
          _M0L6_2atmpS1820 < 0
          || _M0L6_2atmpS1820 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS44[_M0L6_2atmpS1820] = _M0L6_2atmpS1821;
        _M0L6_2atmpS1823 = _M0L1iS48 + 1;
        _M0L1iS48 = _M0L6_2atmpS1823;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1828 = _M0L3lenS49 - 1;
    int32_t _M0L1iS51 = _M0L6_2atmpS1828;
    while (1) {
      if (_M0L1iS51 >= 0) {
        int32_t _M0L6_2atmpS1824 = _M0L11dst__offsetS46 + _M0L1iS51;
        int32_t _M0L6_2atmpS1826 = _M0L11src__offsetS47 + _M0L1iS51;
        int32_t _M0L6_2atmpS1825;
        int32_t _M0L6_2atmpS1827;
        if (
          _M0L6_2atmpS1826 < 0
          || _M0L6_2atmpS1826 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1825 = (int32_t)_M0L3srcS45[_M0L6_2atmpS1826];
        if (
          _M0L6_2atmpS1824 < 0
          || _M0L6_2atmpS1824 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS44[_M0L6_2atmpS1824] = _M0L6_2atmpS1825;
        _M0L6_2atmpS1827 = _M0L1iS51 - 1;
        _M0L1iS51 = _M0L6_2atmpS1827;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS53,
  int32_t _M0L11dst__offsetS55,
  float* _M0L3srcS54,
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
        int32_t _M0L6_2atmpS1829 = _M0L11dst__offsetS55 + _M0L1iS57;
        int32_t _M0L6_2atmpS1831 = _M0L11src__offsetS56 + _M0L1iS57;
        float _M0L6_2atmpS1830;
        int32_t _M0L6_2atmpS1832;
        if (
          _M0L6_2atmpS1831 < 0
          || _M0L6_2atmpS1831 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1830 = (float)_M0L3srcS54[_M0L6_2atmpS1831];
        if (
          _M0L6_2atmpS1829 < 0
          || _M0L6_2atmpS1829 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS53[_M0L6_2atmpS1829] = _M0L6_2atmpS1830;
        _M0L6_2atmpS1832 = _M0L1iS57 + 1;
        _M0L1iS57 = _M0L6_2atmpS1832;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS54);
        moonbit_decref_cycle_free(_M0L3dstS53);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1837 = _M0L3lenS58 - 1;
    int32_t _M0L1iS60 = _M0L6_2atmpS1837;
    while (1) {
      if (_M0L1iS60 >= 0) {
        int32_t _M0L6_2atmpS1833 = _M0L11dst__offsetS55 + _M0L1iS60;
        int32_t _M0L6_2atmpS1835 = _M0L11src__offsetS56 + _M0L1iS60;
        float _M0L6_2atmpS1834;
        int32_t _M0L6_2atmpS1836;
        if (
          _M0L6_2atmpS1835 < 0
          || _M0L6_2atmpS1835 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1834 = (float)_M0L3srcS54[_M0L6_2atmpS1835];
        if (
          _M0L6_2atmpS1833 < 0
          || _M0L6_2atmpS1833 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS53[_M0L6_2atmpS1833] = _M0L6_2atmpS1834;
        _M0L6_2atmpS1836 = _M0L1iS60 - 1;
        _M0L1iS60 = _M0L6_2atmpS1836;
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS15) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS16) {
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1760) {
  switch (Moonbit_object_tag(_M0L4_2aeS1760)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_30.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_31.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1760);
      break;
    }
    
    case 3: {
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
  void* _M0L11_2aobj__ptrS1788,
  struct _M0TPB4Show _M0L8_2aparamS1787
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1786 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1788;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1786, _M0L8_2aparamS1787);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1785,
  struct _M0TPB4Show _M0L8_2aparamS1784
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1783 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1785;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1783, _M0L8_2aparamS1784);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1782,
  int32_t _M0L8_2aparamS1781
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1780 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1782;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1780, _M0L8_2aparamS1781);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1779,
  struct _M0TPC16string10StringView _M0L8_2aparamS1778
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1777 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1779;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1777, _M0L8_2aparamS1778);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1776,
  moonbit_string_t _M0L8_2aparamS1773,
  int32_t _M0L8_2aparamS1774,
  int32_t _M0L8_2aparamS1775
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1772 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1776;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1772, _M0L8_2aparamS1773, _M0L8_2aparamS1774, _M0L8_2aparamS1775);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1771,
  moonbit_string_t _M0L8_2aparamS1770
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1769 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1771;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1769, _M0L8_2aparamS1770);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_5408 = 9218868437227405311ll;
  int64_t _tmp_5409;
  int64_t _tmp_5410;
  int64_t _tmp_5411;
  int64_t _tmp_5412;
  _M0FPB18double__max__value = *(double*)&_tmp_5408;
  _tmp_5409 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_5409;
  _tmp_5410 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_5410;
  _tmp_5411 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_5411;
  _tmp_5412 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_5412;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1792;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1753;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1754;
  int32_t _M0L7_2abindS1755;
  struct _M0TUsiE** _M0L7_2abindS1756;
  int32_t _M0L6_2acntS5123;
  int32_t _M0L2__S1757;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1792
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1753
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1753)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 120, 0);
  _M0L12async__testsS1753->$0 = _M0L6_2atmpS1792;
  _M0L12async__testsS1753->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1754
  = _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1755 = _M0L7_2abindS1754->$1;
  _M0L7_2abindS1756 = _M0L7_2abindS1754->$0;
  _M0L6_2acntS5123
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1754));
  if (_M0L6_2acntS5123 > 1) {
    int32_t _M0L11_2anew__cntS5124 = _M0L6_2acntS5123 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1754), _M0L11_2anew__cntS5124);
    moonbit_incref_cycle_free(_M0L7_2abindS1756);
  } else if (_M0L6_2acntS5123 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1754);
  }
  _M0L2__S1757 = 0;
  while (1) {
    if (_M0L2__S1757 < _M0L7_2abindS1755) {
      struct _M0TUsiE* _M0L3argS1758 =
        (struct _M0TUsiE*)_M0L7_2abindS1756[_M0L2__S1757];
      moonbit_string_t _M0L6_2atmpS1789 = _M0L3argS1758->$0;
      int32_t _M0L6_2atmpS1790 = _M0L3argS1758->$1;
      int32_t _M0L6_2atmpS1791;
      moonbit_incref_cycle_free(_M0L6_2atmpS1789);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1753, _M0L6_2atmpS1789, _M0L6_2atmpS1790);
      moonbit_decref_cycle_free(_M0L6_2atmpS1789);
      _M0L6_2atmpS1791 = _M0L2__S1757 + 1;
      _M0L2__S1757 = _M0L6_2atmpS1791;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1756);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\ei_inhibition\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30ei__inhibition__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1753);
  moonbit_decref_cycle_free(_M0L12async__testsS1753);
  moonbit_flush_cycles();
  return 0;
}