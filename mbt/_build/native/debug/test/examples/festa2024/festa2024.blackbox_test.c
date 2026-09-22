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

struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__;

struct _M0TP26RiantR8snn__mbt11HHParameter;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__;

struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070;

struct _M0TP26RiantR8snn__mbt2IZ;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel;

struct _M0TP26RiantR8snn__mbt17BalancedParameter;

struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025;

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus;

struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat;

struct _M0TUddE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25festa2024__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25festa2024__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TP26RiantR8snn__mbt13STDPVariables;

struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet;

struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric;

struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet;

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065;

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

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

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

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25festa2024__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__ {
  struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25festa2024__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2077(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2070(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2065(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2042(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2035(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25festa2024__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*
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

struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0MP26RiantR8snn__mbt9STDPEntry3new(
  int32_t,
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0MP26RiantR8snn__mbt13STDPVariables3new(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
);

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

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  int32_t
);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[116]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 115, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 102, 101, 115, 116, 97, 50, 48, 
    50, 52, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 
    107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[114]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 113, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 102, 101, 115, 116, 97, 50, 48, 
    50, 52, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 
    115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_34 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

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

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2077$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2077
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[135] =
  {
    sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065)
    / 4, 1,
    offsetof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065, $1)
    / 4
    * 2,
    sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070)
    / 4, 1,
    offsetof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TP26RiantR8snn__mbt9STDPEntry) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $5) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt13STDPVariables) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $4) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $3) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS5693
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS2098,
  moonbit_string_t _M0L8filenameS2067,
  int32_t _M0L5indexS2069
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065* _closure_5904;
  struct _M0TWEu* _M0L13handle__startS2065;
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070* _closure_5905;
  struct _M0TWssbEu* _M0L14handle__resultS2070;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS2077;
  void* _M0L11_2atry__errS2092;
  struct moonbit_result_0 _tmp_5907;
  int32_t _handle__error__result_5908;
  int32_t _M0L6_2atmpS5681;
  void* _M0L3errS2093;
  moonbit_string_t _M0L4nameS2095;
  struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS2096;
  moonbit_string_t _M0L7_2anameS2097;
  int32_t _M0L6_2acntS5726;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS2067);
  _closure_5904
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065*)moonbit_malloc(sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065));
  Moonbit_object_header(_closure_5904)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_5904->code
  = &_M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2065;
  _closure_5904->$0 = _M0L5indexS2069;
  _closure_5904->$1 = _M0L8filenameS2067;
  _M0L13handle__startS2065 = (struct _M0TWEu*)_closure_5904;
  moonbit_incref_cycle_free(_M0L8filenameS2067);
  _closure_5905
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070*)moonbit_malloc(sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070));
  Moonbit_object_header(_closure_5905)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_5905->code
  = &_M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2070;
  _closure_5905->$0 = _M0L5indexS2069;
  _closure_5905->$1 = _M0L8filenameS2067;
  _M0L14handle__resultS2070 = (struct _M0TWssbEu*)_closure_5905;
  _M0L17error__to__stringS2077
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2077$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _tmp_5907
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS2098, _M0L8filenameS2067, _M0L5indexS2069, _M0L13handle__startS2065, _M0L14handle__resultS2070, _M0L17error__to__stringS2077);
  if (_tmp_5907.tag) {
    int32_t const _M0L5_2aokS5690 = _tmp_5907.data.ok;
    _handle__error__result_5908 = _M0L5_2aokS5690;
  } else {
    void* const _M0L6_2aerrS5691 = _tmp_5907.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
    moonbit_decref_cycle_free(_M0L13handle__startS2065);
    _M0L11_2atry__errS2092 = _M0L6_2aerrS5691;
    goto join_2091;
  }
  if (_handle__error__result_5908) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
    moonbit_decref_cycle_free(_M0L13handle__startS2065);
    _M0L6_2atmpS5681 = 1;
  } else {
    struct moonbit_result_0 _tmp_5909;
    int32_t _handle__error__result_5910;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
    _tmp_5909
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS2098, _M0L8filenameS2067, _M0L5indexS2069, _M0L13handle__startS2065, _M0L14handle__resultS2070, _M0L17error__to__stringS2077);
    if (_tmp_5909.tag) {
      int32_t const _M0L5_2aokS5688 = _tmp_5909.data.ok;
      _handle__error__result_5910 = _M0L5_2aokS5688;
    } else {
      void* const _M0L6_2aerrS5689 = _tmp_5909.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
      moonbit_decref_cycle_free(_M0L13handle__startS2065);
      _M0L11_2atry__errS2092 = _M0L6_2aerrS5689;
      goto join_2091;
    }
    if (_handle__error__result_5910) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
      moonbit_decref_cycle_free(_M0L13handle__startS2065);
      _M0L6_2atmpS5681 = 1;
    } else {
      struct moonbit_result_0 _tmp_5911;
      int32_t _handle__error__result_5912;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _tmp_5911
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS2098, _M0L8filenameS2067, _M0L5indexS2069, _M0L13handle__startS2065, _M0L14handle__resultS2070, _M0L17error__to__stringS2077);
      if (_tmp_5911.tag) {
        int32_t const _M0L5_2aokS5686 = _tmp_5911.data.ok;
        _handle__error__result_5912 = _M0L5_2aokS5686;
      } else {
        void* const _M0L6_2aerrS5687 = _tmp_5911.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
        moonbit_decref_cycle_free(_M0L13handle__startS2065);
        _M0L11_2atry__errS2092 = _M0L6_2aerrS5687;
        goto join_2091;
      }
      if (_handle__error__result_5912) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
        moonbit_decref_cycle_free(_M0L13handle__startS2065);
        _M0L6_2atmpS5681 = 1;
      } else {
        struct moonbit_result_0 _tmp_5913;
        int32_t _handle__error__result_5914;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
        _tmp_5913
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS2098, _M0L8filenameS2067, _M0L5indexS2069, _M0L13handle__startS2065, _M0L14handle__resultS2070, _M0L17error__to__stringS2077);
        if (_tmp_5913.tag) {
          int32_t const _M0L5_2aokS5684 = _tmp_5913.data.ok;
          _handle__error__result_5914 = _M0L5_2aokS5684;
        } else {
          void* const _M0L6_2aerrS5685 = _tmp_5913.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
          moonbit_decref_cycle_free(_M0L13handle__startS2065);
          _M0L11_2atry__errS2092 = _M0L6_2aerrS5685;
          goto join_2091;
        }
        if (_handle__error__result_5914) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
          moonbit_decref_cycle_free(_M0L13handle__startS2065);
          _M0L6_2atmpS5681 = 1;
        } else {
          struct moonbit_result_0 _tmp_5915;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
          _tmp_5915
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS2098, _M0L8filenameS2067, _M0L5indexS2069, _M0L13handle__startS2065, _M0L14handle__resultS2070, _M0L17error__to__stringS2077);
          moonbit_decref_cycle_free(_M0L13handle__startS2065);
          moonbit_decref_cycle_free(_M0L17error__to__stringS2077);
          if (_tmp_5915.tag) {
            int32_t const _M0L5_2aokS5682 = _tmp_5915.data.ok;
            _M0L6_2atmpS5681 = _M0L5_2aokS5682;
          } else {
            void* const _M0L6_2aerrS5683 = _tmp_5915.data.err;
            _M0L11_2atry__errS2092 = _M0L6_2aerrS5683;
            goto join_2091;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS5681) {
    void* _M0L128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5692 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5692)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5692)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS2092
    = _M0L128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5692;
    goto join_2091;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS2070);
  }
  goto joinlet_5906;
  join_2091:;
  _M0L3errS2093 = _M0L11_2atry__errS2092;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS2096
  = (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS2093;
  _M0L7_2anameS2097 = _M0L36_2aMoonBitTestDriverInternalSkipTestS2096->$0;
  _M0L6_2acntS5726
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS2096));
  if (_M0L6_2acntS5726 > 1) {
    int32_t _M0L11_2anew__cntS5727 = _M0L6_2acntS5726 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS2096), _M0L11_2anew__cntS5727);
    moonbit_incref_cycle_free(_M0L7_2anameS2097);
  } else if (_M0L6_2acntS5726 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS2096);
  }
  _M0L4nameS2095 = _M0L7_2anameS2097;
  goto join_2094;
  goto joinlet_5916;
  join_2094:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2070(_M0L14handle__resultS2070, _M0L4nameS2095, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS2070);
  moonbit_decref_cycle_free(_M0L4nameS2095);
  joinlet_5916:;
  joinlet_5906:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2077(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS5680,
  void* _M0L3errS2078
) {
  void* _M0L1eS2080;
  moonbit_string_t _M0L1eS2082;
  moonbit_string_t _result_5919;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS2078)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS2083 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS2078;
      moonbit_string_t _M0L4_2aeS2084 = _M0L10_2aFailureS2083->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2084);
      _M0L1eS2082 = _M0L4_2aeS2084;
      goto join_2081;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS2085 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS2078;
      moonbit_string_t _M0L4_2aeS2086 = _M0L15_2aInspectErrorS2085->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2086);
      _M0L1eS2082 = _M0L4_2aeS2086;
      goto join_2081;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS2087 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS2078;
      moonbit_string_t _M0L4_2aeS2088 = _M0L16_2aSnapshotErrorS2087->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2088);
      _M0L1eS2082 = _M0L4_2aeS2088;
      goto join_2081;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS2089 =
        (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS2078;
      moonbit_string_t _M0L4_2aeS2090 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS2089->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2090);
      _M0L1eS2082 = _M0L4_2aeS2090;
      goto join_2081;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS2078);
      _M0L1eS2080 = _M0L3errS2078;
      goto join_2079;
      break;
    }
  }
  join_2081:;
  return _M0L1eS2082;
  join_2079:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _result_5919 = _M0FP15Error10to__string(_M0L1eS2080);
  moonbit_decref_cycle_free(_M0L1eS2080);
  return _result_5919;
}

int32_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2070(
  struct _M0TWssbEu* _M0L6_2aenvS5677,
  moonbit_string_t _M0L10__testnameS2071,
  moonbit_string_t _M0L7messageS2072,
  int32_t _M0L7skippedS2073
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070* _M0L14_2acasted__envS5678;
  moonbit_string_t _M0L8filenameS2067;
  int32_t _M0L5indexS2069;
  moonbit_string_t _M0L10file__nameS2074;
  moonbit_string_t _M0L7messageS2075;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS2076;
  moonbit_string_t _M0L6_2atmpS5679;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5678
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2070*)_M0L6_2aenvS5677;
  _M0L8filenameS2067 = _M0L14_2acasted__envS5678->$1;
  _M0L5indexS2069 = _M0L14_2acasted__envS5678->$0;
  if (!_M0L7skippedS2073 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS2074
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS2067, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS2075
  = _M0MPC16string6String14escape_2einner(_M0L7messageS2072, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS2076
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2076, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2076, _M0L10file__nameS2074);
  moonbit_decref_cycle_free(_M0L10file__nameS2074);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2076, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS2076, _M0L5indexS2069);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2076, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2076, _M0L7messageS2075);
  moonbit_decref_cycle_free(_M0L7messageS2075);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2076, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5679
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS2076);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS2076);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5679);
  moonbit_decref_cycle_free(_M0L6_2atmpS5679);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2065(
  struct _M0TWEu* _M0L6_2aenvS5674
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065* _M0L14_2acasted__envS5675;
  moonbit_string_t _M0L8filenameS2067;
  int32_t _M0L5indexS2069;
  moonbit_string_t _M0L10file__nameS2066;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS2068;
  moonbit_string_t _M0L6_2atmpS5676;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5675
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2ffesta2024__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2065*)_M0L6_2aenvS5674;
  _M0L8filenameS2067 = _M0L14_2acasted__envS5675->$1;
  _M0L5indexS2069 = _M0L14_2acasted__envS5675->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS2066
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS2067, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS2068
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2068, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2068, _M0L10file__nameS2066);
  moonbit_decref_cycle_free(_M0L10file__nameS2066);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2068, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS2068, _M0L5indexS2069);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2068, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5676
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS2068);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS2068);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5676);
  moonbit_decref_cycle_free(_M0L6_2atmpS5676);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S2035;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS2042;
  struct _M0TUsiE** _M0L6_2atmpS5673;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS2049;
  moonbit_string_t* _M0L9cli__argsS2050;
  moonbit_string_t _M0L6_2atmpS5672;
  moonbit_string_t _M0L6_2atmpS5671;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS2051;
  int32_t _M0L7_2abindS2052;
  moonbit_string_t* _M0L7_2abindS2053;
  int32_t _M0L6_2acntS5728;
  int32_t _M0L2__S2054;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S2035 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS2042 = 0;
  _M0L6_2atmpS5673 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS2049
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS2049)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS2049->$0 = _M0L6_2atmpS5673;
  _M0L16file__and__indexS2049->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS2050
  = _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS2050)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS5672 = (moonbit_string_t)_M0L9cli__argsS2050[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS5672);
  moonbit_decref_cycle_free(_M0L9cli__argsS2050);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5671
  = _M0MP46RiantR8snn__mbt8examples25festa2024__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS5672);
  moonbit_decref_cycle_free(_M0L6_2atmpS5672);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS2051
  = _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2042(_M0L51moonbit__test__driver__internal__split__mbt__stringS2042, _M0L6_2atmpS5671, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS5671);
  _M0L7_2abindS2052 = _M0L10test__argsS2051->$1;
  _M0L7_2abindS2053 = _M0L10test__argsS2051->$0;
  _M0L6_2acntS5728
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS2051));
  if (_M0L6_2acntS5728 > 1) {
    int32_t _M0L11_2anew__cntS5729 = _M0L6_2acntS5728 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS2051), _M0L11_2anew__cntS5729);
    moonbit_incref_cycle_free(_M0L7_2abindS2053);
  } else if (_M0L6_2acntS5728 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS2051);
  }
  _M0L2__S2054 = 0;
  while (1) {
    if (_M0L2__S2054 < _M0L7_2abindS2052) {
      moonbit_string_t _M0L3argS2055 =
        (moonbit_string_t)_M0L7_2abindS2053[_M0L2__S2054];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS2056;
      moonbit_string_t _M0L4fileS2057;
      moonbit_string_t _M0L5rangeS2058;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS2059;
      moonbit_string_t _M0L6_2atmpS5669;
      int32_t _M0L5startS2060;
      moonbit_string_t _M0L6_2atmpS5668;
      int32_t _M0L3endS2061;
      int32_t _M0L1iS2062;
      int32_t _M0L6_2atmpS5670;
      moonbit_incref_cycle_free(_M0L3argS2055);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS2056
      = _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2042(_M0L51moonbit__test__driver__internal__split__mbt__stringS2042, _M0L3argS2055, 58);
      moonbit_decref_cycle_free(_M0L3argS2055);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS2057
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS2056, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS2058
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS2056, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS2056);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS2059
      = _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2042(_M0L51moonbit__test__driver__internal__split__mbt__stringS2042, _M0L5rangeS2058, 45);
      moonbit_decref_cycle_free(_M0L5rangeS2058);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5669
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS2059, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS2060
      = _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2035(_M0L45moonbit__test__driver__internal__parse__int__S2035, _M0L6_2atmpS5669);
      moonbit_decref_cycle_free(_M0L6_2atmpS5669);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5668
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS2059, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS2059);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS2061
      = _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2035(_M0L45moonbit__test__driver__internal__parse__int__S2035, _M0L6_2atmpS5668);
      moonbit_decref_cycle_free(_M0L6_2atmpS5668);
      _M0L1iS2062 = _M0L5startS2060;
      while (1) {
        if (_M0L1iS2062 < _M0L3endS2061) {
          struct _M0TUsiE* _M0L8_2atupleS5666;
          int32_t _M0L6_2atmpS5667;
          moonbit_incref_cycle_free(_M0L4fileS2057);
          _M0L8_2atupleS5666
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS5666)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS5666->$0 = _M0L4fileS2057;
          _M0L8_2atupleS5666->$1 = _M0L1iS2062;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS2049, _M0L8_2atupleS5666);
          _M0L6_2atmpS5667 = _M0L1iS2062 + 1;
          _M0L1iS2062 = _M0L6_2atmpS5667;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS2057);
        }
        break;
      }
      _M0L6_2atmpS5670 = _M0L2__S2054 + 1;
      _M0L2__S2054 = _M0L6_2atmpS5670;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS2053);
    }
    break;
  }
  return _M0L16file__and__indexS2049;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2042(
  int32_t _M0L6_2aenvS5647,
  moonbit_string_t _M0L1sS2043,
  int32_t _M0L3sepS2044
) {
  moonbit_string_t* _M0L6_2atmpS5665;
  struct _M0TPB5ArrayGsE* _M0L3resS2045;
  struct _M0TPB8MutLocalGiE* _M0L1iS2046;
  struct _M0TPB8MutLocalGiE* _M0L5startS2047;
  int32_t _M0L3valS5660;
  int32_t _M0L6_2atmpS5661;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5665 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS2045
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS2045)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS2045->$0 = _M0L6_2atmpS5665;
  _M0L3resS2045->$1 = 0;
  _M0L1iS2046
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS2046)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS2046->$0 = 0;
  _M0L5startS2047
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS2047)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS2047->$0 = 0;
  while (1) {
    int32_t _M0L3valS5648 = _M0L1iS2046->$0;
    int32_t _M0L6_2atmpS5649 = Moonbit_array_length(_M0L1sS2043);
    if (_M0L3valS5648 < _M0L6_2atmpS5649) {
      int32_t _M0L3valS5652 = _M0L1iS2046->$0;
      int32_t _M0L6_2atmpS5651;
      int32_t _M0L6_2atmpS5650;
      int32_t _M0L3valS5659;
      int32_t _M0L6_2atmpS5658;
      if (
        _M0L3valS5652 < 0
        || _M0L3valS5652 >= Moonbit_array_length(_M0L1sS2043)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5651 = _M0L1sS2043[_M0L3valS5652];
      _M0L6_2atmpS5650 = _M0L6_2atmpS5651;
      if (_M0L6_2atmpS5650 == _M0L3sepS2044) {
        int32_t _M0L3valS5654 = _M0L5startS2047->$0;
        int32_t _M0L3valS5655 = _M0L1iS2046->$0;
        moonbit_string_t _M0L6_2atmpS5653;
        int32_t _M0L3valS5657;
        int32_t _M0L6_2atmpS5656;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS5653
        = _M0MPC16string6String17unsafe__substring(_M0L1sS2043, _M0L3valS5654, _M0L3valS5655);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS2045, _M0L6_2atmpS5653);
        _M0L3valS5657 = _M0L1iS2046->$0;
        _M0L6_2atmpS5656 = _M0L3valS5657 + 1;
        _M0L5startS2047->$0 = _M0L6_2atmpS5656;
      }
      _M0L3valS5659 = _M0L1iS2046->$0;
      _M0L6_2atmpS5658 = _M0L3valS5659 + 1;
      _M0L1iS2046->$0 = _M0L6_2atmpS5658;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS2046);
    }
    break;
  }
  _M0L3valS5660 = _M0L5startS2047->$0;
  _M0L6_2atmpS5661 = Moonbit_array_length(_M0L1sS2043);
  if (_M0L3valS5660 < _M0L6_2atmpS5661) {
    int32_t _M0L3valS5663 = _M0L5startS2047->$0;
    int32_t _M0L6_2atmpS5664;
    moonbit_string_t _M0L6_2atmpS5662;
    moonbit_decref_cycle_free(_M0L5startS2047);
    _M0L6_2atmpS5664 = Moonbit_array_length(_M0L1sS2043);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS5662
    = _M0MPC16string6String17unsafe__substring(_M0L1sS2043, _M0L3valS5663, _M0L6_2atmpS5664);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS2045, _M0L6_2atmpS5662);
  } else {
    moonbit_decref_cycle_free(_M0L5startS2047);
  }
  return _M0L3resS2045;
}

int32_t _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2035(
  int32_t _M0L6_2aenvS5640,
  moonbit_string_t _M0L1sS2036
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS2037;
  int32_t _M0L3lenS2038;
  int32_t _M0L7_2abindS2039;
  int32_t _M0L1iS2040;
  int32_t _result_5924;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS2037
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS2037)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS2037->$0 = 0;
  _M0L3lenS2038 = Moonbit_array_length(_M0L1sS2036);
  _M0L7_2abindS2039 = 0;
  _M0L1iS2040 = _M0L7_2abindS2039;
  while (1) {
    if (_M0L1iS2040 < _M0L3lenS2038) {
      int32_t _M0L3valS5645 = _M0L3resS2037->$0;
      int32_t _M0L6_2atmpS5642 = _M0L3valS5645 * 10;
      int32_t _M0L6_2atmpS5644;
      int32_t _M0L6_2atmpS5643;
      int32_t _M0L6_2atmpS5641;
      int32_t _M0L6_2atmpS5646;
      if (
        _M0L1iS2040 < 0 || _M0L1iS2040 >= Moonbit_array_length(_M0L1sS2036)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5644 = _M0L1sS2036[_M0L1iS2040];
      _M0L6_2atmpS5643 = _M0L6_2atmpS5644 - 48;
      _M0L6_2atmpS5641 = _M0L6_2atmpS5642 + _M0L6_2atmpS5643;
      _M0L3resS2037->$0 = _M0L6_2atmpS5641;
      _M0L6_2atmpS5646 = _M0L1iS2040 + 1;
      _M0L1iS2040 = _M0L6_2atmpS5646;
      continue;
    }
    break;
  }
  _result_5924 = _M0L3resS2037->$0;
  moonbit_decref_cycle_free(_M0L3resS2037);
  return _result_5924;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25festa2024__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS2034
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS2034);
  return _M0L4selfS2034;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2004,
  moonbit_string_t _M0L12_2adiscard__S2005,
  int32_t _M0L12_2adiscard__S2006,
  struct _M0TWEu* _M0L12_2adiscard__S2007,
  struct _M0TWssbEu* _M0L12_2adiscard__S2008,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2009
) {
  struct moonbit_result_0 _result_5925;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _result_5925.tag = 1;
  _result_5925.data.ok = 0;
  return _result_5925;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2010,
  moonbit_string_t _M0L12_2adiscard__S2011,
  int32_t _M0L12_2adiscard__S2012,
  struct _M0TWEu* _M0L12_2adiscard__S2013,
  struct _M0TWssbEu* _M0L12_2adiscard__S2014,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2015
) {
  struct moonbit_result_0 _result_5926;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _result_5926.tag = 1;
  _result_5926.data.ok = 0;
  return _result_5926;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2016,
  moonbit_string_t _M0L12_2adiscard__S2017,
  int32_t _M0L12_2adiscard__S2018,
  struct _M0TWEu* _M0L12_2adiscard__S2019,
  struct _M0TWssbEu* _M0L12_2adiscard__S2020,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2021
) {
  struct moonbit_result_0 _result_5927;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _result_5927.tag = 1;
  _result_5927.data.ok = 0;
  return _result_5927;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2022,
  moonbit_string_t _M0L12_2adiscard__S2023,
  int32_t _M0L12_2adiscard__S2024,
  struct _M0TWEu* _M0L12_2adiscard__S2025,
  struct _M0TWssbEu* _M0L12_2adiscard__S2026,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2027
) {
  struct moonbit_result_0 _result_5928;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _result_5928.tag = 1;
  _result_5928.data.ok = 0;
  return _result_5928;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2028,
  moonbit_string_t _M0L12_2adiscard__S2029,
  int32_t _M0L12_2adiscard__S2030,
  struct _M0TWEu* _M0L12_2adiscard__S2031,
  struct _M0TWssbEu* _M0L12_2adiscard__S2032,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2033
) {
  struct moonbit_result_0 _result_5929;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _result_5929.tag = 1;
  _result_5929.data.ok = 0;
  return _result_5929;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2003
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1876,
  float _M0L2dtS1881
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2abindS1875;
  int32_t _M0L7_2abindS1877;
  void** _M0L7_2abindS1878;
  int32_t _M0L2__S1879;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1883;
  int32_t _M0L7_2abindS1884;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1885;
  int32_t _M0L2__S1886;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2abindS1889;
  int32_t _M0L7_2abindS1890;
  void** _M0L7_2abindS1891;
  int32_t _M0L2__S1892;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1910;
  int32_t _M0L7_2abindS1911;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1912;
  int32_t _M0L2__S1913;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2abindS1916;
  int32_t _M0L7_2abindS1917;
  void** _M0L7_2abindS1918;
  int32_t _M0L2__S1919;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L7_2abindS1962;
  int32_t _M0L7_2abindS1963;
  void** _M0L7_2abindS1964;
  int32_t _M0L2__S1965;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1968;
  int32_t _M0L7_2abindS1969;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1970;
  int32_t _M0L2__S1971;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5639;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L7_2abindS1875 = _M0L1mS1876->$2;
  _M0L7_2abindS1877 = _M0L7_2abindS1875->$1;
  _M0L7_2abindS1878 = _M0L7_2abindS1875->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1878);
  _M0L2__S1879 = 0;
  while (1) {
    if (_M0L2__S1879 < _M0L7_2abindS1877) {
      void* _M0L1sS1880 = (void*)_M0L7_2abindS1878[_M0L2__S1879];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5448 = _M0L1mS1876->$3;
      int32_t _M0L6_2atmpS5449;
      moonbit_incref_cycle_free(_M0L1sS1880);
      #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14stimulate__any(_M0L1sS1880, _M0L4timeS5448, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L1sS1880);
      _M0L6_2atmpS5449 = _M0L2__S1879 + 1;
      _M0L2__S1879 = _M0L6_2atmpS5449;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1878);
    }
    break;
  }
  _M0L7_2abindS1883 = _M0L1mS1876->$1;
  _M0L7_2abindS1884 = _M0L7_2abindS1883->$1;
  _M0L7_2abindS1885 = _M0L7_2abindS1883->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1885);
  _M0L2__S1886 = 0;
  while (1) {
    if (_M0L2__S1886 < _M0L7_2abindS1884) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1887 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1885[
          _M0L2__S1886
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5451 = _M0L1mS1876->$3;
      float _M0L6_2atmpS5450;
      int32_t _M0L6_2atmpS5452;
      moonbit_incref_cycle_free(_M0L1cS1887);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5450 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5451);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1887, _M0L6_2atmpS5450);
      moonbit_decref_cycle_free(_M0L1cS1887);
      _M0L6_2atmpS5452 = _M0L2__S1886 + 1;
      _M0L2__S1886 = _M0L6_2atmpS5452;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1885);
    }
    break;
  }
  _M0L7_2abindS1889 = _M0L1mS1876->$6;
  _M0L7_2abindS1890 = _M0L7_2abindS1889->$1;
  _M0L7_2abindS1891 = _M0L7_2abindS1889->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1891);
  _M0L2__S1892 = 0;
  while (1) {
    if (_M0L2__S1892 < _M0L7_2abindS1890) {
      void* _M0L5entryS1893 = (void*)_M0L7_2abindS1891[_M0L2__S1892];
      struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L1eS1895;
      struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L1eS1898;
      struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L1eS1901;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5469;
      int32_t _M0L11conn__indexS5470;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1902;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5465;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS5466;
      int32_t _M0L6_2acntS5734;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5468;
      float _M0L6_2atmpS5467;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5463;
      int32_t _M0L11conn__indexS5464;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1899;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5459;
      struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS5460;
      int32_t _M0L6_2acntS5732;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5462;
      float _M0L6_2atmpS5461;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5457;
      int32_t _M0L11conn__indexS5458;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1896;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5453;
      struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS5454;
      int32_t _M0L6_2acntS5730;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5456;
      float _M0L6_2atmpS5455;
      int32_t _M0L6_2atmpS5471;
      switch (Moonbit_object_tag(_M0L5entryS1893)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__* _M0L15_2aMarkramSTP__S1903 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__*)_M0L5entryS1893;
          struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L4_2aeS1904 =
            _M0L15_2aMarkramSTP__S1903->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1904);
          _M0L1eS1901 = _M0L4_2aeS1904;
          goto join_1900;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__* _M0L18_2aMarkramSTPHet__S1905 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__*)_M0L5entryS1893;
          struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L4_2aeS1906 =
            _M0L18_2aMarkramSTPHet__S1905->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1906);
          _M0L1eS1898 = _M0L4_2aeS1906;
          goto join_1897;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__* _M0L23_2aMarkramSTPTimestep__S1907 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__*)_M0L5entryS1893;
          struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L4_2aeS1908 =
            _M0L23_2aMarkramSTPTimestep__S1907->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1908);
          _M0L1eS1895 = _M0L4_2aeS1908;
          goto join_1894;
          break;
        }
      }
      goto joinlet_5935;
      join_1900:;
      _M0L5connsS5469 = _M0L1mS1876->$1;
      _M0L11conn__indexS5470 = _M0L1eS1901->$0;
      #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1902
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5469, _M0L11conn__indexS5470);
      _M0L4varsS5465 = _M0L1eS1901->$1;
      _M0L5paramS5466 = _M0L1eS1901->$2;
      _M0L6_2acntS5734 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1901));
      if (_M0L6_2acntS5734 > 1) {
        int32_t _M0L11_2anew__cntS5735 = _M0L6_2acntS5734 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1901), _M0L11_2anew__cntS5735);
        moonbit_incref_cycle_free(_M0L5paramS5466);
        moonbit_incref_cycle_free(_M0L4varsS5465);
      } else if (_M0L6_2acntS5734 == 1) {
        #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1901);
      }
      _M0L4timeS5468 = _M0L1mS1876->$3;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5467 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5468);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt18markram__stp__step(_M0L3synS1902, _M0L4varsS5465, _M0L5paramS5466, _M0L6_2atmpS5467);
      moonbit_decref_cycle_free(_M0L3synS1902);
      moonbit_decref_cycle_free(_M0L4varsS5465);
      moonbit_decref_cycle_free(_M0L5paramS5466);
      joinlet_5935:;
      goto joinlet_5934;
      join_1897:;
      _M0L5connsS5463 = _M0L1mS1876->$1;
      _M0L11conn__indexS5464 = _M0L1eS1898->$0;
      #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1899
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5463, _M0L11conn__indexS5464);
      _M0L4varsS5459 = _M0L1eS1898->$1;
      _M0L5paramS5460 = _M0L1eS1898->$2;
      _M0L6_2acntS5732 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1898));
      if (_M0L6_2acntS5732 > 1) {
        int32_t _M0L11_2anew__cntS5733 = _M0L6_2acntS5732 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1898), _M0L11_2anew__cntS5733);
        moonbit_incref_cycle_free(_M0L5paramS5460);
        moonbit_incref_cycle_free(_M0L4varsS5459);
      } else if (_M0L6_2acntS5732 == 1) {
        #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1898);
      }
      _M0L4timeS5462 = _M0L1mS1876->$3;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5461 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5462);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt23markram__stp__step__het(_M0L3synS1899, _M0L4varsS5459, _M0L5paramS5460, _M0L6_2atmpS5461);
      moonbit_decref_cycle_free(_M0L3synS1899);
      moonbit_decref_cycle_free(_M0L4varsS5459);
      moonbit_decref_cycle_free(_M0L5paramS5460);
      joinlet_5934:;
      goto joinlet_5933;
      join_1894:;
      _M0L5connsS5457 = _M0L1mS1876->$1;
      _M0L11conn__indexS5458 = _M0L1eS1895->$0;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1896
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5457, _M0L11conn__indexS5458);
      _M0L4varsS5453 = _M0L1eS1895->$1;
      _M0L5paramS5454 = _M0L1eS1895->$2;
      _M0L6_2acntS5730 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1895));
      if (_M0L6_2acntS5730 > 1) {
        int32_t _M0L11_2anew__cntS5731 = _M0L6_2acntS5730 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1895), _M0L11_2anew__cntS5731);
        moonbit_incref_cycle_free(_M0L5paramS5454);
        moonbit_incref_cycle_free(_M0L4varsS5453);
      } else if (_M0L6_2acntS5730 == 1) {
        #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1895);
      }
      _M0L4timeS5456 = _M0L1mS1876->$3;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5455 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5456);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(_M0L3synS1896, _M0L4varsS5453, _M0L5paramS5454, _M0L6_2atmpS5455, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L3synS1896);
      moonbit_decref_cycle_free(_M0L4varsS5453);
      moonbit_decref_cycle_free(_M0L5paramS5454);
      joinlet_5933:;
      _M0L6_2atmpS5471 = _M0L2__S1892 + 1;
      _M0L2__S1892 = _M0L6_2atmpS5471;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1891);
    }
    break;
  }
  _M0L7_2abindS1910 = _M0L1mS1876->$1;
  _M0L7_2abindS1911 = _M0L7_2abindS1910->$1;
  _M0L7_2abindS1912 = _M0L7_2abindS1910->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1912);
  _M0L2__S1913 = 0;
  while (1) {
    if (_M0L2__S1913 < _M0L7_2abindS1911) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1914 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1912[
          _M0L2__S1913
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5473 = _M0L1mS1876->$3;
      float _M0L6_2atmpS5472;
      int32_t _M0L6_2atmpS5474;
      moonbit_incref_cycle_free(_M0L1cS1914);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5472 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5473);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1914, _M0L6_2atmpS5472);
      moonbit_decref_cycle_free(_M0L1cS1914);
      _M0L6_2atmpS5474 = _M0L2__S1913 + 1;
      _M0L2__S1913 = _M0L6_2atmpS5474;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1912);
    }
    break;
  }
  _M0L7_2abindS1916 = _M0L1mS1876->$5;
  _M0L7_2abindS1917 = _M0L7_2abindS1916->$1;
  _M0L7_2abindS1918 = _M0L7_2abindS1916->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1918);
  _M0L2__S1919 = 0;
  while (1) {
    if (_M0L2__S1919 < _M0L7_2abindS1917) {
      void* _M0L5entryS1920 = (void*)_M0L7_2abindS1918[_M0L2__S1919];
      struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L1eS1922;
      struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L1eS1925;
      struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L1eS1928;
      struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L1eS1931;
      struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L1eS1934;
      struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L1eS1937;
      struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L1eS1940;
      struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L1eS1943;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5632;
      int32_t _M0L11conn__indexS5633;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1944;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5627;
      struct _M0TPB5ArrayGfE* _M0L4valsS5614;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5626;
      struct _M0TPB5ArrayGbE* _M0L4fireS5615;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5625;
      struct _M0TPB5ArrayGbE* _M0L4fireS5616;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5624;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5617;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5623;
      int32_t _M0L6_2acntS5883;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5618;
      int32_t _M0L6_2acntS5894;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5619;
      struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS5620;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5622;
      float _M0L6_2atmpS5621;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5628;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5631;
      int32_t _M0L6_2acntS5898;
      float _M0L6_2atmpS5630;
      float _M0L6_2atmpS5629;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5612;
      int32_t _M0L11conn__indexS5613;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1941;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5607;
      struct _M0TPB5ArrayGfE* _M0L4valsS5595;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5606;
      struct _M0TPB5ArrayGbE* _M0L4fireS5596;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5605;
      struct _M0TPB5ArrayGbE* _M0L4fireS5597;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5604;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5598;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5603;
      int32_t _M0L6_2acntS5863;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5599;
      int32_t _M0L6_2acntS5874;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5600;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5601;
      struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS5602;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5608;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5611;
      int32_t _M0L6_2acntS5878;
      float _M0L6_2atmpS5610;
      float _M0L6_2atmpS5609;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5593;
      int32_t _M0L11conn__indexS5594;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1938;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5588;
      struct _M0TPB5ArrayGfE* _M0L4valsS5577;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5587;
      struct _M0TPB5ArrayGbE* _M0L4fireS5578;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5586;
      struct _M0TPB5ArrayGbE* _M0L4fireS5579;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5585;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5580;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5584;
      int32_t _M0L6_2acntS5844;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5581;
      int32_t _M0L6_2acntS5855;
      struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS5582;
      struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS5583;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5589;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5592;
      int32_t _M0L6_2acntS5859;
      float _M0L6_2atmpS5591;
      float _M0L6_2atmpS5590;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5575;
      int32_t _M0L11conn__indexS5576;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1935;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5570;
      struct _M0TPB5ArrayGfE* _M0L4valsS5557;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5569;
      struct _M0TPB5ArrayGbE* _M0L4fireS5558;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5568;
      struct _M0TPB5ArrayGbE* _M0L4fireS5559;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5567;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5560;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5566;
      int32_t _M0L6_2acntS5825;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5561;
      int32_t _M0L6_2acntS5836;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5562;
      struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS5563;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5565;
      float _M0L6_2atmpS5564;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5571;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5574;
      int32_t _M0L6_2acntS5840;
      float _M0L6_2atmpS5573;
      float _M0L6_2atmpS5572;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5555;
      int32_t _M0L11conn__indexS5556;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1932;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5550;
      struct _M0TPB5ArrayGfE* _M0L4valsS5537;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5549;
      struct _M0TPB5ArrayGbE* _M0L4fireS5538;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5548;
      struct _M0TPB5ArrayGbE* _M0L4fireS5539;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5547;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5540;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5546;
      int32_t _M0L6_2acntS5806;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5541;
      int32_t _M0L6_2acntS5817;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS5542;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS5543;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5545;
      float _M0L6_2atmpS5544;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5551;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5554;
      int32_t _M0L6_2acntS5821;
      float _M0L6_2atmpS5553;
      float _M0L6_2atmpS5552;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5535;
      int32_t _M0L11conn__indexS5536;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1929;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5530;
      struct _M0TPB5ArrayGfE* _M0L4valsS5515;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5529;
      struct _M0TPB5ArrayGbE* _M0L4fireS5516;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5528;
      struct _M0TPB5ArrayGbE* _M0L4fireS5517;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5527;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5518;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5526;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5519;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5525;
      int32_t _M0L6_2acntS5774;
      struct _M0TPB5ArrayGfE* _M0L1vS5520;
      int32_t _M0L6_2acntS5785;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS5521;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS5522;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5524;
      float _M0L6_2atmpS5523;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5531;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5534;
      int32_t _M0L6_2acntS5802;
      float _M0L6_2atmpS5533;
      float _M0L6_2atmpS5532;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5513;
      int32_t _M0L11conn__indexS5514;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1926;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5508;
      struct _M0TPB5ArrayGfE* _M0L4valsS5495;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5507;
      struct _M0TPB5ArrayGbE* _M0L4fireS5496;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5506;
      struct _M0TPB5ArrayGbE* _M0L4fireS5497;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5505;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5498;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5504;
      int32_t _M0L6_2acntS5755;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5499;
      int32_t _M0L6_2acntS5766;
      struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS5500;
      struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS5501;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5503;
      float _M0L6_2atmpS5502;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5509;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5512;
      int32_t _M0L6_2acntS5770;
      float _M0L6_2atmpS5511;
      float _M0L6_2atmpS5510;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5493;
      int32_t _M0L11conn__indexS5494;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1923;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5488;
      struct _M0TPB5ArrayGfE* _M0L4valsS5475;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5487;
      struct _M0TPB5ArrayGbE* _M0L4fireS5476;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5486;
      struct _M0TPB5ArrayGbE* _M0L4fireS5477;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5485;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5478;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5484;
      int32_t _M0L6_2acntS5736;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5479;
      int32_t _M0L6_2acntS5747;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS5480;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS5481;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5483;
      float _M0L6_2atmpS5482;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5489;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5492;
      int32_t _M0L6_2acntS5751;
      float _M0L6_2atmpS5491;
      float _M0L6_2atmpS5490;
      int32_t _M0L6_2atmpS5634;
      switch (Moonbit_object_tag(_M0L5entryS1920)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__* _M0L13_2aGerstner__S1945 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L5entryS1920;
          struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L4_2aeS1946 =
            _M0L13_2aGerstner__S1945->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1946);
          _M0L1eS1943 = _M0L4_2aeS1946;
          goto join_1942;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__* _M0L15_2aMexicanHat__S1947 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__*)_M0L5entryS1920;
          struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L4_2aeS1948 =
            _M0L15_2aMexicanHat__S1947->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1948);
          _M0L1eS1940 = _M0L4_2aeS1948;
          goto join_1939;
          break;
        }
        
        case 2: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__* _M0L18_2aAntiSymmetric__S1949 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__*)_M0L5entryS1920;
          struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L4_2aeS1950 =
            _M0L18_2aAntiSymmetric__S1949->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1950);
          _M0L1eS1937 = _M0L4_2aeS1950;
          goto join_1936;
          break;
        }
        
        case 3: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__* _M0L19_2aConfavreux2025__S1951 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__*)_M0L5entryS1920;
          struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L4_2aeS1952 =
            _M0L19_2aConfavreux2025__S1951->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1952);
          _M0L1eS1934 = _M0L4_2aeS1952;
          goto join_1933;
          break;
        }
        
        case 4: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__* _M0L14_2aIstdpRate__S1953 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__*)_M0L5entryS1920;
          struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L4_2aeS1954 =
            _M0L14_2aIstdpRate__S1953->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1954);
          _M0L1eS1931 = _M0L4_2aeS1954;
          goto join_1930;
          break;
        }
        
        case 5: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__* _M0L19_2aIstdpPotential__S1955 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__*)_M0L5entryS1920;
          struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L4_2aeS1956 =
            _M0L19_2aIstdpPotential__S1955->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1956);
          _M0L1eS1928 = _M0L4_2aeS1956;
          goto join_1927;
          break;
        }
        
        case 6: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__* _M0L14_2aSymmetric__S1957 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__*)_M0L5entryS1920;
          struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L4_2aeS1958 =
            _M0L14_2aSymmetric__S1957->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1958);
          _M0L1eS1925 = _M0L4_2aeS1958;
          goto join_1924;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__* _M0L17_2aCaPlasticity__S1959 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__*)_M0L5entryS1920;
          struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L4_2aeS1960 =
            _M0L17_2aCaPlasticity__S1959->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1960);
          _M0L1eS1922 = _M0L4_2aeS1960;
          goto join_1921;
          break;
        }
      }
      goto joinlet_5945;
      join_1942:;
      _M0L5connsS5632 = _M0L1mS1876->$1;
      _M0L11conn__indexS5633 = _M0L1eS1943->$0;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1944
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5632, _M0L11conn__indexS5633);
      _M0L6matrixS5627 = _M0L3synS1944->$4;
      _M0L4valsS5614 = _M0L6matrixS5627->$4;
      _M0L3preS5626 = _M0L3synS1944->$0;
      _M0L4fireS5615 = _M0L3preS5626->$5;
      _M0L4postS5625 = _M0L3synS1944->$1;
      _M0L4fireS5616 = _M0L4postS5625->$5;
      _M0L6matrixS5624 = _M0L3synS1944->$4;
      _M0L6colptrS5617 = _M0L6matrixS5624->$3;
      _M0L6matrixS5623 = _M0L3synS1944->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5617);
      moonbit_incref_cycle_free(_M0L4fireS5616);
      moonbit_incref_cycle_free(_M0L4fireS5615);
      moonbit_incref_cycle_free(_M0L4valsS5614);
      _M0L6_2acntS5883
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1944));
      if (_M0L6_2acntS5883 > 1) {
        int32_t _M0L11_2anew__cntS5893 = _M0L6_2acntS5883 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1944), _M0L11_2anew__cntS5893);
        moonbit_incref_cycle_free(_M0L6matrixS5623);
      } else if (_M0L6_2acntS5883 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5892 = _M0L3synS1944->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5891;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5890;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5889;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5888;
        moonbit_string_t _M0L8_2afieldS5887;
        moonbit_string_t _M0L8_2afieldS5886;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5885;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5884;
        moonbit_decref_cycle_free(_M0L8_2afieldS5892);
        _M0L8_2afieldS5891 = _M0L3synS1944->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5891);
        _M0L8_2afieldS5890 = _M0L3synS1944->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5890);
        _M0L8_2afieldS5889 = _M0L3synS1944->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5889);
        _M0L8_2afieldS5888 = _M0L3synS1944->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5888);
        _M0L8_2afieldS5887 = _M0L3synS1944->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5887);
        _M0L8_2afieldS5886 = _M0L3synS1944->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5886);
        _M0L8_2afieldS5885 = _M0L3synS1944->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5885);
        _M0L8_2afieldS5884 = _M0L3synS1944->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5884);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1944);
      }
      _M0L6rowptrS5618 = _M0L6matrixS5623->$2;
      _M0L6_2acntS5894
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5623));
      if (_M0L6_2acntS5894 > 1) {
        int32_t _M0L11_2anew__cntS5897 = _M0L6_2acntS5894 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5623), _M0L11_2anew__cntS5897);
        moonbit_incref_cycle_free(_M0L6rowptrS5618);
      } else if (_M0L6_2acntS5894 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5896 = _M0L6matrixS5623->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5895;
        moonbit_decref_cycle_free(_M0L8_2afieldS5896);
        _M0L8_2afieldS5895 = _M0L6matrixS5623->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5895);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5623);
      }
      _M0L4varsS5619 = _M0L1eS1943->$3;
      _M0L5paramS5620 = _M0L1eS1943->$4;
      _M0L6t__nowS5622 = _M0L1eS1943->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5622);
      moonbit_incref_cycle_free(_M0L5paramS5620);
      moonbit_incref_cycle_free(_M0L4varsS5619);
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5621 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5622, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5622);
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt10stdp__step(_M0L4valsS5614, _M0L4fireS5615, _M0L4fireS5616, _M0L6colptrS5617, _M0L6rowptrS5618, _M0L4varsS5619, _M0L5paramS5620, _M0L6_2atmpS5621, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L4valsS5614);
      moonbit_decref_cycle_free(_M0L4fireS5615);
      moonbit_decref_cycle_free(_M0L4fireS5616);
      moonbit_decref_cycle_free(_M0L6colptrS5617);
      moonbit_decref_cycle_free(_M0L6rowptrS5618);
      moonbit_decref_cycle_free(_M0L4varsS5619);
      moonbit_decref_cycle_free(_M0L5paramS5620);
      _M0L6t__nowS5628 = _M0L1eS1943->$5;
      _M0L6t__nowS5631 = _M0L1eS1943->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5628);
      _M0L6_2acntS5898 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1943));
      if (_M0L6_2acntS5898 > 1) {
        int32_t _M0L11_2anew__cntS5901 = _M0L6_2acntS5898 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1943), _M0L11_2anew__cntS5901);
        moonbit_incref_cycle_free(_M0L6t__nowS5631);
      } else if (_M0L6_2acntS5898 == 1) {
        struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L8_2afieldS5900 =
          _M0L1eS1943->$4;
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5899;
        moonbit_decref_cycle_free(_M0L8_2afieldS5900);
        _M0L8_2afieldS5899 = _M0L1eS1943->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5899);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1943);
      }
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5630 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5631, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5631);
      _M0L6_2atmpS5629 = _M0L6_2atmpS5630 + _M0L2dtS1881;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5628, 0, _M0L6_2atmpS5629);
      moonbit_decref_cycle_free(_M0L6t__nowS5628);
      joinlet_5945:;
      goto joinlet_5944;
      join_1939:;
      _M0L5connsS5612 = _M0L1mS1876->$1;
      _M0L11conn__indexS5613 = _M0L1eS1940->$0;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1941
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5612, _M0L11conn__indexS5613);
      _M0L6matrixS5607 = _M0L3synS1941->$4;
      _M0L4valsS5595 = _M0L6matrixS5607->$4;
      _M0L3preS5606 = _M0L3synS1941->$0;
      _M0L4fireS5596 = _M0L3preS5606->$5;
      _M0L4postS5605 = _M0L3synS1941->$1;
      _M0L4fireS5597 = _M0L4postS5605->$5;
      _M0L6matrixS5604 = _M0L3synS1941->$4;
      _M0L6colptrS5598 = _M0L6matrixS5604->$3;
      _M0L6matrixS5603 = _M0L3synS1941->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5598);
      moonbit_incref_cycle_free(_M0L4fireS5597);
      moonbit_incref_cycle_free(_M0L4fireS5596);
      moonbit_incref_cycle_free(_M0L4valsS5595);
      _M0L6_2acntS5863
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1941));
      if (_M0L6_2acntS5863 > 1) {
        int32_t _M0L11_2anew__cntS5873 = _M0L6_2acntS5863 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1941), _M0L11_2anew__cntS5873);
        moonbit_incref_cycle_free(_M0L6matrixS5603);
      } else if (_M0L6_2acntS5863 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5872 = _M0L3synS1941->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5871;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5870;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5869;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5868;
        moonbit_string_t _M0L8_2afieldS5867;
        moonbit_string_t _M0L8_2afieldS5866;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5865;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5864;
        moonbit_decref_cycle_free(_M0L8_2afieldS5872);
        _M0L8_2afieldS5871 = _M0L3synS1941->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5871);
        _M0L8_2afieldS5870 = _M0L3synS1941->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5870);
        _M0L8_2afieldS5869 = _M0L3synS1941->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5869);
        _M0L8_2afieldS5868 = _M0L3synS1941->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5868);
        _M0L8_2afieldS5867 = _M0L3synS1941->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5867);
        _M0L8_2afieldS5866 = _M0L3synS1941->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5866);
        _M0L8_2afieldS5865 = _M0L3synS1941->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5865);
        _M0L8_2afieldS5864 = _M0L3synS1941->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5864);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1941);
      }
      _M0L6rowptrS5599 = _M0L6matrixS5603->$2;
      _M0L6_2acntS5874
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5603));
      if (_M0L6_2acntS5874 > 1) {
        int32_t _M0L11_2anew__cntS5877 = _M0L6_2acntS5874 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5603), _M0L11_2anew__cntS5877);
        moonbit_incref_cycle_free(_M0L6rowptrS5599);
      } else if (_M0L6_2acntS5874 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5876 = _M0L6matrixS5603->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5875;
        moonbit_decref_cycle_free(_M0L8_2afieldS5876);
        _M0L8_2afieldS5875 = _M0L6matrixS5603->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5875);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5603);
      }
      _M0L4tpreS5600 = _M0L1eS1940->$4;
      _M0L5tpostS5601 = _M0L1eS1940->$5;
      _M0L5paramS5602 = _M0L1eS1940->$3;
      moonbit_incref_cycle_free(_M0L5paramS5602);
      moonbit_incref_cycle_free(_M0L5tpostS5601);
      moonbit_incref_cycle_free(_M0L4tpreS5600);
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(_M0L4valsS5595, _M0L4fireS5596, _M0L4fireS5597, _M0L6colptrS5598, _M0L6rowptrS5599, _M0L4tpreS5600, _M0L5tpostS5601, _M0L5paramS5602, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L4valsS5595);
      moonbit_decref_cycle_free(_M0L4fireS5596);
      moonbit_decref_cycle_free(_M0L4fireS5597);
      moonbit_decref_cycle_free(_M0L6colptrS5598);
      moonbit_decref_cycle_free(_M0L6rowptrS5599);
      moonbit_decref_cycle_free(_M0L4tpreS5600);
      moonbit_decref_cycle_free(_M0L5tpostS5601);
      moonbit_decref_cycle_free(_M0L5paramS5602);
      _M0L6t__nowS5608 = _M0L1eS1940->$6;
      _M0L6t__nowS5611 = _M0L1eS1940->$6;
      moonbit_incref_cycle_free(_M0L6t__nowS5608);
      _M0L6_2acntS5878 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1940));
      if (_M0L6_2acntS5878 > 1) {
        int32_t _M0L11_2anew__cntS5882 = _M0L6_2acntS5878 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1940), _M0L11_2anew__cntS5882);
        moonbit_incref_cycle_free(_M0L6t__nowS5611);
      } else if (_M0L6_2acntS5878 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5881 = _M0L1eS1940->$5;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5880;
        struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L8_2afieldS5879;
        moonbit_decref_cycle_free(_M0L8_2afieldS5881);
        _M0L8_2afieldS5880 = _M0L1eS1940->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5880);
        _M0L8_2afieldS5879 = _M0L1eS1940->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5879);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1940);
      }
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5610 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5611, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5611);
      _M0L6_2atmpS5609 = _M0L6_2atmpS5610 + _M0L2dtS1881;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5608, 0, _M0L6_2atmpS5609);
      moonbit_decref_cycle_free(_M0L6t__nowS5608);
      joinlet_5944:;
      goto joinlet_5943;
      join_1936:;
      _M0L5connsS5593 = _M0L1mS1876->$1;
      _M0L11conn__indexS5594 = _M0L1eS1937->$0;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1938
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5593, _M0L11conn__indexS5594);
      _M0L6matrixS5588 = _M0L3synS1938->$4;
      _M0L4valsS5577 = _M0L6matrixS5588->$4;
      _M0L3preS5587 = _M0L3synS1938->$0;
      _M0L4fireS5578 = _M0L3preS5587->$5;
      _M0L4postS5586 = _M0L3synS1938->$1;
      _M0L4fireS5579 = _M0L4postS5586->$5;
      _M0L6matrixS5585 = _M0L3synS1938->$4;
      _M0L6colptrS5580 = _M0L6matrixS5585->$3;
      _M0L6matrixS5584 = _M0L3synS1938->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5580);
      moonbit_incref_cycle_free(_M0L4fireS5579);
      moonbit_incref_cycle_free(_M0L4fireS5578);
      moonbit_incref_cycle_free(_M0L4valsS5577);
      _M0L6_2acntS5844
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1938));
      if (_M0L6_2acntS5844 > 1) {
        int32_t _M0L11_2anew__cntS5854 = _M0L6_2acntS5844 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1938), _M0L11_2anew__cntS5854);
        moonbit_incref_cycle_free(_M0L6matrixS5584);
      } else if (_M0L6_2acntS5844 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5853 = _M0L3synS1938->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5852;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5851;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5850;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5849;
        moonbit_string_t _M0L8_2afieldS5848;
        moonbit_string_t _M0L8_2afieldS5847;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5846;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5845;
        moonbit_decref_cycle_free(_M0L8_2afieldS5853);
        _M0L8_2afieldS5852 = _M0L3synS1938->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5852);
        _M0L8_2afieldS5851 = _M0L3synS1938->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5851);
        _M0L8_2afieldS5850 = _M0L3synS1938->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5850);
        _M0L8_2afieldS5849 = _M0L3synS1938->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5849);
        _M0L8_2afieldS5848 = _M0L3synS1938->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5848);
        _M0L8_2afieldS5847 = _M0L3synS1938->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5847);
        _M0L8_2afieldS5846 = _M0L3synS1938->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5846);
        _M0L8_2afieldS5845 = _M0L3synS1938->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5845);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1938);
      }
      _M0L6rowptrS5581 = _M0L6matrixS5584->$2;
      _M0L6_2acntS5855
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5584));
      if (_M0L6_2acntS5855 > 1) {
        int32_t _M0L11_2anew__cntS5858 = _M0L6_2acntS5855 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5584), _M0L11_2anew__cntS5858);
        moonbit_incref_cycle_free(_M0L6rowptrS5581);
      } else if (_M0L6_2acntS5855 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5857 = _M0L6matrixS5584->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5856;
        moonbit_decref_cycle_free(_M0L8_2afieldS5857);
        _M0L8_2afieldS5856 = _M0L6matrixS5584->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5856);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5584);
      }
      _M0L4varsS5582 = _M0L1eS1937->$4;
      _M0L5paramS5583 = _M0L1eS1937->$3;
      moonbit_incref_cycle_free(_M0L5paramS5583);
      moonbit_incref_cycle_free(_M0L4varsS5582);
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(_M0L4valsS5577, _M0L4fireS5578, _M0L4fireS5579, _M0L6colptrS5580, _M0L6rowptrS5581, _M0L4varsS5582, _M0L5paramS5583, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L4valsS5577);
      moonbit_decref_cycle_free(_M0L4fireS5578);
      moonbit_decref_cycle_free(_M0L4fireS5579);
      moonbit_decref_cycle_free(_M0L6colptrS5580);
      moonbit_decref_cycle_free(_M0L6rowptrS5581);
      moonbit_decref_cycle_free(_M0L4varsS5582);
      moonbit_decref_cycle_free(_M0L5paramS5583);
      _M0L6t__nowS5589 = _M0L1eS1937->$5;
      _M0L6t__nowS5592 = _M0L1eS1937->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5589);
      _M0L6_2acntS5859 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1937));
      if (_M0L6_2acntS5859 > 1) {
        int32_t _M0L11_2anew__cntS5862 = _M0L6_2acntS5859 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1937), _M0L11_2anew__cntS5862);
        moonbit_incref_cycle_free(_M0L6t__nowS5592);
      } else if (_M0L6_2acntS5859 == 1) {
        struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L8_2afieldS5861 =
          _M0L1eS1937->$4;
        struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L8_2afieldS5860;
        moonbit_decref_cycle_free(_M0L8_2afieldS5861);
        _M0L8_2afieldS5860 = _M0L1eS1937->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5860);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1937);
      }
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5591 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5592, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5592);
      _M0L6_2atmpS5590 = _M0L6_2atmpS5591 + _M0L2dtS1881;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5589, 0, _M0L6_2atmpS5590);
      moonbit_decref_cycle_free(_M0L6t__nowS5589);
      joinlet_5943:;
      goto joinlet_5942;
      join_1933:;
      _M0L5connsS5575 = _M0L1mS1876->$1;
      _M0L11conn__indexS5576 = _M0L1eS1934->$0;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1935
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5575, _M0L11conn__indexS5576);
      _M0L6matrixS5570 = _M0L3synS1935->$4;
      _M0L4valsS5557 = _M0L6matrixS5570->$4;
      _M0L3preS5569 = _M0L3synS1935->$0;
      _M0L4fireS5558 = _M0L3preS5569->$5;
      _M0L4postS5568 = _M0L3synS1935->$1;
      _M0L4fireS5559 = _M0L4postS5568->$5;
      _M0L6matrixS5567 = _M0L3synS1935->$4;
      _M0L6colptrS5560 = _M0L6matrixS5567->$3;
      _M0L6matrixS5566 = _M0L3synS1935->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5560);
      moonbit_incref_cycle_free(_M0L4fireS5559);
      moonbit_incref_cycle_free(_M0L4fireS5558);
      moonbit_incref_cycle_free(_M0L4valsS5557);
      _M0L6_2acntS5825
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1935));
      if (_M0L6_2acntS5825 > 1) {
        int32_t _M0L11_2anew__cntS5835 = _M0L6_2acntS5825 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1935), _M0L11_2anew__cntS5835);
        moonbit_incref_cycle_free(_M0L6matrixS5566);
      } else if (_M0L6_2acntS5825 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5834 = _M0L3synS1935->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5833;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5832;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5831;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5830;
        moonbit_string_t _M0L8_2afieldS5829;
        moonbit_string_t _M0L8_2afieldS5828;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5827;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5826;
        moonbit_decref_cycle_free(_M0L8_2afieldS5834);
        _M0L8_2afieldS5833 = _M0L3synS1935->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5833);
        _M0L8_2afieldS5832 = _M0L3synS1935->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5832);
        _M0L8_2afieldS5831 = _M0L3synS1935->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5831);
        _M0L8_2afieldS5830 = _M0L3synS1935->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5830);
        _M0L8_2afieldS5829 = _M0L3synS1935->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5829);
        _M0L8_2afieldS5828 = _M0L3synS1935->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5828);
        _M0L8_2afieldS5827 = _M0L3synS1935->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5827);
        _M0L8_2afieldS5826 = _M0L3synS1935->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5826);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1935);
      }
      _M0L6rowptrS5561 = _M0L6matrixS5566->$2;
      _M0L6_2acntS5836
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5566));
      if (_M0L6_2acntS5836 > 1) {
        int32_t _M0L11_2anew__cntS5839 = _M0L6_2acntS5836 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5566), _M0L11_2anew__cntS5839);
        moonbit_incref_cycle_free(_M0L6rowptrS5561);
      } else if (_M0L6_2acntS5836 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5838 = _M0L6matrixS5566->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5837;
        moonbit_decref_cycle_free(_M0L8_2afieldS5838);
        _M0L8_2afieldS5837 = _M0L6matrixS5566->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5837);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5566);
      }
      _M0L4varsS5562 = _M0L1eS1934->$4;
      _M0L5paramS5563 = _M0L1eS1934->$3;
      _M0L6t__nowS5565 = _M0L1eS1934->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5565);
      moonbit_incref_cycle_free(_M0L5paramS5563);
      moonbit_incref_cycle_free(_M0L4varsS5562);
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5564 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5565, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5565);
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22stdp__confavreux__step(_M0L4valsS5557, _M0L4fireS5558, _M0L4fireS5559, _M0L6colptrS5560, _M0L6rowptrS5561, _M0L4varsS5562, _M0L5paramS5563, _M0L6_2atmpS5564, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L4valsS5557);
      moonbit_decref_cycle_free(_M0L4fireS5558);
      moonbit_decref_cycle_free(_M0L4fireS5559);
      moonbit_decref_cycle_free(_M0L6colptrS5560);
      moonbit_decref_cycle_free(_M0L6rowptrS5561);
      moonbit_decref_cycle_free(_M0L4varsS5562);
      moonbit_decref_cycle_free(_M0L5paramS5563);
      _M0L6t__nowS5571 = _M0L1eS1934->$5;
      _M0L6t__nowS5574 = _M0L1eS1934->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5571);
      _M0L6_2acntS5840 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1934));
      if (_M0L6_2acntS5840 > 1) {
        int32_t _M0L11_2anew__cntS5843 = _M0L6_2acntS5840 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1934), _M0L11_2anew__cntS5843);
        moonbit_incref_cycle_free(_M0L6t__nowS5574);
      } else if (_M0L6_2acntS5840 == 1) {
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5842 =
          _M0L1eS1934->$4;
        struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L8_2afieldS5841;
        moonbit_decref_cycle_free(_M0L8_2afieldS5842);
        _M0L8_2afieldS5841 = _M0L1eS1934->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5841);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1934);
      }
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5573 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5574, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5574);
      _M0L6_2atmpS5572 = _M0L6_2atmpS5573 + _M0L2dtS1881;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5571, 0, _M0L6_2atmpS5572);
      moonbit_decref_cycle_free(_M0L6t__nowS5571);
      joinlet_5942:;
      goto joinlet_5941;
      join_1930:;
      _M0L5connsS5555 = _M0L1mS1876->$1;
      _M0L11conn__indexS5556 = _M0L1eS1931->$0;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1932
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5555, _M0L11conn__indexS5556);
      _M0L6matrixS5550 = _M0L3synS1932->$4;
      _M0L4valsS5537 = _M0L6matrixS5550->$4;
      _M0L3preS5549 = _M0L3synS1932->$0;
      _M0L4fireS5538 = _M0L3preS5549->$5;
      _M0L4postS5548 = _M0L3synS1932->$1;
      _M0L4fireS5539 = _M0L4postS5548->$5;
      _M0L6matrixS5547 = _M0L3synS1932->$4;
      _M0L6colptrS5540 = _M0L6matrixS5547->$3;
      _M0L6matrixS5546 = _M0L3synS1932->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5540);
      moonbit_incref_cycle_free(_M0L4fireS5539);
      moonbit_incref_cycle_free(_M0L4fireS5538);
      moonbit_incref_cycle_free(_M0L4valsS5537);
      _M0L6_2acntS5806
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1932));
      if (_M0L6_2acntS5806 > 1) {
        int32_t _M0L11_2anew__cntS5816 = _M0L6_2acntS5806 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1932), _M0L11_2anew__cntS5816);
        moonbit_incref_cycle_free(_M0L6matrixS5546);
      } else if (_M0L6_2acntS5806 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5815 = _M0L3synS1932->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5814;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5813;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5812;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5811;
        moonbit_string_t _M0L8_2afieldS5810;
        moonbit_string_t _M0L8_2afieldS5809;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5808;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5807;
        moonbit_decref_cycle_free(_M0L8_2afieldS5815);
        _M0L8_2afieldS5814 = _M0L3synS1932->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5814);
        _M0L8_2afieldS5813 = _M0L3synS1932->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5813);
        _M0L8_2afieldS5812 = _M0L3synS1932->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5812);
        _M0L8_2afieldS5811 = _M0L3synS1932->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5811);
        _M0L8_2afieldS5810 = _M0L3synS1932->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5810);
        _M0L8_2afieldS5809 = _M0L3synS1932->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5809);
        _M0L8_2afieldS5808 = _M0L3synS1932->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5808);
        _M0L8_2afieldS5807 = _M0L3synS1932->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5807);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1932);
      }
      _M0L6rowptrS5541 = _M0L6matrixS5546->$2;
      _M0L6_2acntS5817
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5546));
      if (_M0L6_2acntS5817 > 1) {
        int32_t _M0L11_2anew__cntS5820 = _M0L6_2acntS5817 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5546), _M0L11_2anew__cntS5820);
        moonbit_incref_cycle_free(_M0L6rowptrS5541);
      } else if (_M0L6_2acntS5817 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5819 = _M0L6matrixS5546->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5818;
        moonbit_decref_cycle_free(_M0L8_2afieldS5819);
        _M0L8_2afieldS5818 = _M0L6matrixS5546->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5818);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5546);
      }
      _M0L4varsS5542 = _M0L1eS1931->$4;
      _M0L5paramS5543 = _M0L1eS1931->$3;
      _M0L6t__nowS5545 = _M0L1eS1931->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5545);
      moonbit_incref_cycle_free(_M0L5paramS5543);
      moonbit_incref_cycle_free(_M0L4varsS5542);
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5544 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5545, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5545);
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS5537, _M0L4fireS5538, _M0L4fireS5539, _M0L6colptrS5540, _M0L6rowptrS5541, _M0L4varsS5542, _M0L5paramS5543, _M0L6_2atmpS5544, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L4valsS5537);
      moonbit_decref_cycle_free(_M0L4fireS5538);
      moonbit_decref_cycle_free(_M0L4fireS5539);
      moonbit_decref_cycle_free(_M0L6colptrS5540);
      moonbit_decref_cycle_free(_M0L6rowptrS5541);
      moonbit_decref_cycle_free(_M0L4varsS5542);
      moonbit_decref_cycle_free(_M0L5paramS5543);
      _M0L6t__nowS5551 = _M0L1eS1931->$5;
      _M0L6t__nowS5554 = _M0L1eS1931->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5551);
      _M0L6_2acntS5821 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1931));
      if (_M0L6_2acntS5821 > 1) {
        int32_t _M0L11_2anew__cntS5824 = _M0L6_2acntS5821 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1931), _M0L11_2anew__cntS5824);
        moonbit_incref_cycle_free(_M0L6t__nowS5554);
      } else if (_M0L6_2acntS5821 == 1) {
        struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L8_2afieldS5823 =
          _M0L1eS1931->$4;
        struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L8_2afieldS5822;
        moonbit_decref_cycle_free(_M0L8_2afieldS5823);
        _M0L8_2afieldS5822 = _M0L1eS1931->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5822);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1931);
      }
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5553 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5554, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5554);
      _M0L6_2atmpS5552 = _M0L6_2atmpS5553 + _M0L2dtS1881;
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5551, 0, _M0L6_2atmpS5552);
      moonbit_decref_cycle_free(_M0L6t__nowS5551);
      joinlet_5941:;
      goto joinlet_5940;
      join_1927:;
      _M0L5connsS5535 = _M0L1mS1876->$1;
      _M0L11conn__indexS5536 = _M0L1eS1928->$0;
      #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1929
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5535, _M0L11conn__indexS5536);
      _M0L6matrixS5530 = _M0L3synS1929->$4;
      _M0L4valsS5515 = _M0L6matrixS5530->$4;
      _M0L3preS5529 = _M0L3synS1929->$0;
      _M0L4fireS5516 = _M0L3preS5529->$5;
      _M0L4postS5528 = _M0L3synS1929->$1;
      _M0L4fireS5517 = _M0L4postS5528->$5;
      _M0L6matrixS5527 = _M0L3synS1929->$4;
      _M0L6colptrS5518 = _M0L6matrixS5527->$3;
      _M0L6matrixS5526 = _M0L3synS1929->$4;
      _M0L6rowptrS5519 = _M0L6matrixS5526->$2;
      _M0L4postS5525 = _M0L3synS1929->$1;
      moonbit_incref_cycle_free(_M0L6rowptrS5519);
      moonbit_incref_cycle_free(_M0L6colptrS5518);
      moonbit_incref_cycle_free(_M0L4fireS5517);
      moonbit_incref_cycle_free(_M0L4fireS5516);
      moonbit_incref_cycle_free(_M0L4valsS5515);
      _M0L6_2acntS5774
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1929));
      if (_M0L6_2acntS5774 > 1) {
        int32_t _M0L11_2anew__cntS5784 = _M0L6_2acntS5774 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1929), _M0L11_2anew__cntS5784);
        moonbit_incref_cycle_free(_M0L4postS5525);
      } else if (_M0L6_2acntS5774 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5783 = _M0L3synS1929->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5782;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5781;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5780;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5779;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L8_2afieldS5778;
        moonbit_string_t _M0L8_2afieldS5777;
        moonbit_string_t _M0L8_2afieldS5776;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5775;
        moonbit_decref_cycle_free(_M0L8_2afieldS5783);
        _M0L8_2afieldS5782 = _M0L3synS1929->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5782);
        _M0L8_2afieldS5781 = _M0L3synS1929->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5781);
        _M0L8_2afieldS5780 = _M0L3synS1929->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5780);
        _M0L8_2afieldS5779 = _M0L3synS1929->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5779);
        _M0L8_2afieldS5778 = _M0L3synS1929->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5778);
        _M0L8_2afieldS5777 = _M0L3synS1929->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5777);
        _M0L8_2afieldS5776 = _M0L3synS1929->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5776);
        _M0L8_2afieldS5775 = _M0L3synS1929->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5775);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1929);
      }
      _M0L1vS5520 = _M0L4postS5525->$3;
      _M0L6_2acntS5785
      = Moonbit_rc_count(Moonbit_object_header(_M0L4postS5525));
      if (_M0L6_2acntS5785 > 1) {
        int32_t _M0L11_2anew__cntS5801 = _M0L6_2acntS5785 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L4postS5525), _M0L11_2anew__cntS5801);
        moonbit_incref_cycle_free(_M0L1vS5520);
      } else if (_M0L6_2acntS5785 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5800 = _M0L4postS5525->$16;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5799;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5798;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5797;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5796;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5795;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5794;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5793;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5792;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5791;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5790;
        struct _M0TPB5ArrayGbE* _M0L8_2afieldS5789;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5788;
        struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS5787;
        struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS5786;
        moonbit_decref_cycle_free(_M0L8_2afieldS5800);
        _M0L8_2afieldS5799 = _M0L4postS5525->$15;
        moonbit_decref_cycle_free(_M0L8_2afieldS5799);
        _M0L8_2afieldS5798 = _M0L4postS5525->$14;
        moonbit_decref_cycle_free(_M0L8_2afieldS5798);
        _M0L8_2afieldS5797 = _M0L4postS5525->$13;
        moonbit_decref_cycle_free(_M0L8_2afieldS5797);
        _M0L8_2afieldS5796 = _M0L4postS5525->$12;
        moonbit_decref_cycle_free(_M0L8_2afieldS5796);
        _M0L8_2afieldS5795 = _M0L4postS5525->$11;
        moonbit_decref_cycle_free(_M0L8_2afieldS5795);
        _M0L8_2afieldS5794 = _M0L4postS5525->$10;
        moonbit_decref_cycle_free(_M0L8_2afieldS5794);
        _M0L8_2afieldS5793 = _M0L4postS5525->$9;
        moonbit_decref_cycle_free(_M0L8_2afieldS5793);
        _M0L8_2afieldS5792 = _M0L4postS5525->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5792);
        _M0L8_2afieldS5791 = _M0L4postS5525->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5791);
        _M0L8_2afieldS5790 = _M0L4postS5525->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5790);
        _M0L8_2afieldS5789 = _M0L4postS5525->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5789);
        _M0L8_2afieldS5788 = _M0L4postS5525->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5788);
        _M0L8_2afieldS5787 = _M0L4postS5525->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5787);
        _M0L8_2afieldS5786 = _M0L4postS5525->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5786);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L4postS5525);
      }
      _M0L4varsS5521 = _M0L1eS1928->$4;
      _M0L5paramS5522 = _M0L1eS1928->$3;
      _M0L6t__nowS5524 = _M0L1eS1928->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5524);
      moonbit_incref_cycle_free(_M0L5paramS5522);
      moonbit_incref_cycle_free(_M0L4varsS5521);
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5523 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5524, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5524);
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS5515, _M0L4fireS5516, _M0L4fireS5517, _M0L6colptrS5518, _M0L6rowptrS5519, _M0L1vS5520, _M0L4varsS5521, _M0L5paramS5522, _M0L6_2atmpS5523, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L4valsS5515);
      moonbit_decref_cycle_free(_M0L4fireS5516);
      moonbit_decref_cycle_free(_M0L4fireS5517);
      moonbit_decref_cycle_free(_M0L6colptrS5518);
      moonbit_decref_cycle_free(_M0L6rowptrS5519);
      moonbit_decref_cycle_free(_M0L1vS5520);
      moonbit_decref_cycle_free(_M0L4varsS5521);
      moonbit_decref_cycle_free(_M0L5paramS5522);
      _M0L6t__nowS5531 = _M0L1eS1928->$5;
      _M0L6t__nowS5534 = _M0L1eS1928->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5531);
      _M0L6_2acntS5802 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1928));
      if (_M0L6_2acntS5802 > 1) {
        int32_t _M0L11_2anew__cntS5805 = _M0L6_2acntS5802 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1928), _M0L11_2anew__cntS5805);
        moonbit_incref_cycle_free(_M0L6t__nowS5534);
      } else if (_M0L6_2acntS5802 == 1) {
        struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L8_2afieldS5804 =
          _M0L1eS1928->$4;
        struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L8_2afieldS5803;
        moonbit_decref_cycle_free(_M0L8_2afieldS5804);
        _M0L8_2afieldS5803 = _M0L1eS1928->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5803);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1928);
      }
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5533 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5534, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5534);
      _M0L6_2atmpS5532 = _M0L6_2atmpS5533 + _M0L2dtS1881;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5531, 0, _M0L6_2atmpS5532);
      moonbit_decref_cycle_free(_M0L6t__nowS5531);
      joinlet_5940:;
      goto joinlet_5939;
      join_1924:;
      _M0L5connsS5513 = _M0L1mS1876->$1;
      _M0L11conn__indexS5514 = _M0L1eS1925->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1926
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5513, _M0L11conn__indexS5514);
      _M0L6matrixS5508 = _M0L3synS1926->$4;
      _M0L4valsS5495 = _M0L6matrixS5508->$4;
      _M0L3preS5507 = _M0L3synS1926->$0;
      _M0L4fireS5496 = _M0L3preS5507->$5;
      _M0L4postS5506 = _M0L3synS1926->$1;
      _M0L4fireS5497 = _M0L4postS5506->$5;
      _M0L6matrixS5505 = _M0L3synS1926->$4;
      _M0L6colptrS5498 = _M0L6matrixS5505->$3;
      _M0L6matrixS5504 = _M0L3synS1926->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5498);
      moonbit_incref_cycle_free(_M0L4fireS5497);
      moonbit_incref_cycle_free(_M0L4fireS5496);
      moonbit_incref_cycle_free(_M0L4valsS5495);
      _M0L6_2acntS5755
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1926));
      if (_M0L6_2acntS5755 > 1) {
        int32_t _M0L11_2anew__cntS5765 = _M0L6_2acntS5755 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1926), _M0L11_2anew__cntS5765);
        moonbit_incref_cycle_free(_M0L6matrixS5504);
      } else if (_M0L6_2acntS5755 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5764 = _M0L3synS1926->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5763;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5762;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5761;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5760;
        moonbit_string_t _M0L8_2afieldS5759;
        moonbit_string_t _M0L8_2afieldS5758;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5757;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5756;
        moonbit_decref_cycle_free(_M0L8_2afieldS5764);
        _M0L8_2afieldS5763 = _M0L3synS1926->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5763);
        _M0L8_2afieldS5762 = _M0L3synS1926->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5762);
        _M0L8_2afieldS5761 = _M0L3synS1926->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5761);
        _M0L8_2afieldS5760 = _M0L3synS1926->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5760);
        _M0L8_2afieldS5759 = _M0L3synS1926->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5759);
        _M0L8_2afieldS5758 = _M0L3synS1926->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5758);
        _M0L8_2afieldS5757 = _M0L3synS1926->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5757);
        _M0L8_2afieldS5756 = _M0L3synS1926->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5756);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1926);
      }
      _M0L6rowptrS5499 = _M0L6matrixS5504->$2;
      _M0L6_2acntS5766
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5504));
      if (_M0L6_2acntS5766 > 1) {
        int32_t _M0L11_2anew__cntS5769 = _M0L6_2acntS5766 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5504), _M0L11_2anew__cntS5769);
        moonbit_incref_cycle_free(_M0L6rowptrS5499);
      } else if (_M0L6_2acntS5766 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5768 = _M0L6matrixS5504->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5767;
        moonbit_decref_cycle_free(_M0L8_2afieldS5768);
        _M0L8_2afieldS5767 = _M0L6matrixS5504->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5767);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5504);
      }
      _M0L4varsS5500 = _M0L1eS1925->$4;
      _M0L5paramS5501 = _M0L1eS1925->$3;
      _M0L6t__nowS5503 = _M0L1eS1925->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5503);
      moonbit_incref_cycle_free(_M0L5paramS5501);
      moonbit_incref_cycle_free(_M0L4varsS5500);
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5502 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5503, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5503);
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt21stdp__symmetric__step(_M0L4valsS5495, _M0L4fireS5496, _M0L4fireS5497, _M0L6colptrS5498, _M0L6rowptrS5499, _M0L4varsS5500, _M0L5paramS5501, _M0L6_2atmpS5502, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L4valsS5495);
      moonbit_decref_cycle_free(_M0L4fireS5496);
      moonbit_decref_cycle_free(_M0L4fireS5497);
      moonbit_decref_cycle_free(_M0L6colptrS5498);
      moonbit_decref_cycle_free(_M0L6rowptrS5499);
      moonbit_decref_cycle_free(_M0L4varsS5500);
      moonbit_decref_cycle_free(_M0L5paramS5501);
      _M0L6t__nowS5509 = _M0L1eS1925->$5;
      _M0L6t__nowS5512 = _M0L1eS1925->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5509);
      _M0L6_2acntS5770 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1925));
      if (_M0L6_2acntS5770 > 1) {
        int32_t _M0L11_2anew__cntS5773 = _M0L6_2acntS5770 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1925), _M0L11_2anew__cntS5773);
        moonbit_incref_cycle_free(_M0L6t__nowS5512);
      } else if (_M0L6_2acntS5770 == 1) {
        struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L8_2afieldS5772 =
          _M0L1eS1925->$4;
        struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L8_2afieldS5771;
        moonbit_decref_cycle_free(_M0L8_2afieldS5772);
        _M0L8_2afieldS5771 = _M0L1eS1925->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5771);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1925);
      }
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5511 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5512, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5512);
      _M0L6_2atmpS5510 = _M0L6_2atmpS5511 + _M0L2dtS1881;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5509, 0, _M0L6_2atmpS5510);
      moonbit_decref_cycle_free(_M0L6t__nowS5509);
      joinlet_5939:;
      goto joinlet_5938;
      join_1921:;
      _M0L5connsS5493 = _M0L1mS1876->$1;
      _M0L11conn__indexS5494 = _M0L1eS1922->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1923
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5493, _M0L11conn__indexS5494);
      _M0L6matrixS5488 = _M0L3synS1923->$4;
      _M0L4valsS5475 = _M0L6matrixS5488->$4;
      _M0L3preS5487 = _M0L3synS1923->$0;
      _M0L4fireS5476 = _M0L3preS5487->$5;
      _M0L4postS5486 = _M0L3synS1923->$1;
      _M0L4fireS5477 = _M0L4postS5486->$5;
      _M0L6matrixS5485 = _M0L3synS1923->$4;
      _M0L6colptrS5478 = _M0L6matrixS5485->$3;
      _M0L6matrixS5484 = _M0L3synS1923->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5478);
      moonbit_incref_cycle_free(_M0L4fireS5477);
      moonbit_incref_cycle_free(_M0L4fireS5476);
      moonbit_incref_cycle_free(_M0L4valsS5475);
      _M0L6_2acntS5736
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1923));
      if (_M0L6_2acntS5736 > 1) {
        int32_t _M0L11_2anew__cntS5746 = _M0L6_2acntS5736 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1923), _M0L11_2anew__cntS5746);
        moonbit_incref_cycle_free(_M0L6matrixS5484);
      } else if (_M0L6_2acntS5736 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5745 = _M0L3synS1923->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5744;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5743;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5742;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5741;
        moonbit_string_t _M0L8_2afieldS5740;
        moonbit_string_t _M0L8_2afieldS5739;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5738;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5737;
        moonbit_decref_cycle_free(_M0L8_2afieldS5745);
        _M0L8_2afieldS5744 = _M0L3synS1923->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5744);
        _M0L8_2afieldS5743 = _M0L3synS1923->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5743);
        _M0L8_2afieldS5742 = _M0L3synS1923->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5742);
        _M0L8_2afieldS5741 = _M0L3synS1923->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5741);
        _M0L8_2afieldS5740 = _M0L3synS1923->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5740);
        _M0L8_2afieldS5739 = _M0L3synS1923->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5739);
        _M0L8_2afieldS5738 = _M0L3synS1923->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5738);
        _M0L8_2afieldS5737 = _M0L3synS1923->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5737);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1923);
      }
      _M0L6rowptrS5479 = _M0L6matrixS5484->$2;
      _M0L6_2acntS5747
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5484));
      if (_M0L6_2acntS5747 > 1) {
        int32_t _M0L11_2anew__cntS5750 = _M0L6_2acntS5747 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5484), _M0L11_2anew__cntS5750);
        moonbit_incref_cycle_free(_M0L6rowptrS5479);
      } else if (_M0L6_2acntS5747 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5749 = _M0L6matrixS5484->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5748;
        moonbit_decref_cycle_free(_M0L8_2afieldS5749);
        _M0L8_2afieldS5748 = _M0L6matrixS5484->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5748);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5484);
      }
      _M0L4varsS5480 = _M0L1eS1922->$4;
      _M0L5paramS5481 = _M0L1eS1922->$3;
      _M0L6t__nowS5483 = _M0L1eS1922->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5483);
      moonbit_incref_cycle_free(_M0L5paramS5481);
      moonbit_incref_cycle_free(_M0L4varsS5480);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5482 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5483, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5483);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt20ca__plasticity__step(_M0L4valsS5475, _M0L4fireS5476, _M0L4fireS5477, _M0L6colptrS5478, _M0L6rowptrS5479, _M0L4varsS5480, _M0L5paramS5481, _M0L6_2atmpS5482, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L4valsS5475);
      moonbit_decref_cycle_free(_M0L4fireS5476);
      moonbit_decref_cycle_free(_M0L4fireS5477);
      moonbit_decref_cycle_free(_M0L6colptrS5478);
      moonbit_decref_cycle_free(_M0L6rowptrS5479);
      moonbit_decref_cycle_free(_M0L4varsS5480);
      moonbit_decref_cycle_free(_M0L5paramS5481);
      _M0L6t__nowS5489 = _M0L1eS1922->$5;
      _M0L6t__nowS5492 = _M0L1eS1922->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5489);
      _M0L6_2acntS5751 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1922));
      if (_M0L6_2acntS5751 > 1) {
        int32_t _M0L11_2anew__cntS5754 = _M0L6_2acntS5751 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1922), _M0L11_2anew__cntS5754);
        moonbit_incref_cycle_free(_M0L6t__nowS5492);
      } else if (_M0L6_2acntS5751 == 1) {
        struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L8_2afieldS5753 =
          _M0L1eS1922->$4;
        struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L8_2afieldS5752;
        moonbit_decref_cycle_free(_M0L8_2afieldS5753);
        _M0L8_2afieldS5752 = _M0L1eS1922->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5752);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1922);
      }
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5491 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5492, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5492);
      _M0L6_2atmpS5490 = _M0L6_2atmpS5491 + _M0L2dtS1881;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5489, 0, _M0L6_2atmpS5490);
      moonbit_decref_cycle_free(_M0L6t__nowS5489);
      joinlet_5938:;
      _M0L6_2atmpS5634 = _M0L2__S1919 + 1;
      _M0L2__S1919 = _M0L6_2atmpS5634;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1918);
    }
    break;
  }
  _M0L7_2abindS1962 = _M0L1mS1876->$0;
  _M0L7_2abindS1963 = _M0L7_2abindS1962->$1;
  _M0L7_2abindS1964 = _M0L7_2abindS1962->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1964);
  _M0L2__S1965 = 0;
  while (1) {
    if (_M0L2__S1965 < _M0L7_2abindS1963) {
      void* _M0L1pS1966 = (void*)_M0L7_2abindS1964[_M0L2__S1965];
      int32_t _M0L6_2atmpS5635;
      moonbit_incref_cycle_free(_M0L1pS1966);
      #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14integrate__any(_M0L1pS1966, _M0L2dtS1881);
      moonbit_decref_cycle_free(_M0L1pS1966);
      _M0L6_2atmpS5635 = _M0L2__S1965 + 1;
      _M0L2__S1965 = _M0L6_2atmpS5635;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1964);
    }
    break;
  }
  _M0L7_2abindS1968 = _M0L1mS1876->$4;
  _M0L7_2abindS1969 = _M0L7_2abindS1968->$1;
  _M0L7_2abindS1970 = _M0L7_2abindS1968->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1970);
  _M0L2__S1971 = 0;
  while (1) {
    if (_M0L2__S1971 < _M0L7_2abindS1969) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L3monS1972 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1970[
          _M0L2__S1971
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5637 = _M0L1mS1876->$3;
      float _M0L6_2atmpS5636;
      int32_t _M0L6_2atmpS5638;
      moonbit_incref_cycle_free(_M0L3monS1972);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5636 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5637);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L3monS1972, _M0L6_2atmpS5636);
      moonbit_decref_cycle_free(_M0L3monS1972);
      _M0L6_2atmpS5638 = _M0L2__S1971 + 1;
      _M0L2__S1971 = _M0L6_2atmpS5638;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1970);
    }
    break;
  }
  _M0L4timeS5639 = _M0L1mS1876->$3;
  #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS5639, _M0L2dtS1881);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1873,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1874,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L11stims_2eoptS1862,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L14monitors_2eoptS1865,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L10stdp_2eoptS1868,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L9stp_2eoptS1871
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1861;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1864;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1867;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1870;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _result_5948;
  if (_M0L11stims_2eoptS1862 == 0) {
    void** _M0L6_2atmpS5447 = (void**)moonbit_empty_ref_array;
    _M0L5stimsS1861
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
    Moonbit_object_header(_M0L5stimsS1861)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _M0L5stimsS1861->$0 = _M0L6_2atmpS5447;
    _M0L5stimsS1861->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2aSomeS1863 =
      _M0L11stims_2eoptS1862;
    if (_M0L7_2aSomeS1863) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1863);
    }
    _M0L5stimsS1861 = _M0L7_2aSomeS1863;
  }
  if (_M0L14monitors_2eoptS1865 == 0) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS5446 =
      (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_empty_ref_array;
    _M0L8monitorsS1864
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
    Moonbit_object_header(_M0L8monitorsS1864)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _M0L8monitorsS1864->$0 = _M0L6_2atmpS5446;
    _M0L8monitorsS1864->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2aSomeS1866 =
      _M0L14monitors_2eoptS1865;
    if (_M0L7_2aSomeS1866) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1866);
    }
    _M0L8monitorsS1864 = _M0L7_2aSomeS1866;
  }
  if (_M0L10stdp_2eoptS1868 == 0) {
    void** _M0L6_2atmpS5445 = (void**)moonbit_empty_ref_array;
    _M0L4stdpS1867
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
    Moonbit_object_header(_M0L4stdpS1867)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
    _M0L4stdpS1867->$0 = _M0L6_2atmpS5445;
    _M0L4stdpS1867->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2aSomeS1869 =
      _M0L10stdp_2eoptS1868;
    if (_M0L7_2aSomeS1869) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1869);
    }
    _M0L4stdpS1867 = _M0L7_2aSomeS1869;
  }
  if (_M0L9stp_2eoptS1871 == 0) {
    void** _M0L6_2atmpS5444 = (void**)moonbit_empty_ref_array;
    _M0L3stpS1870
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE));
    Moonbit_object_header(_M0L3stpS1870)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
    _M0L3stpS1870->$0 = _M0L6_2atmpS5444;
    _M0L3stpS1870->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2aSomeS1872 =
      _M0L9stp_2eoptS1871;
    if (_M0L7_2aSomeS1872) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1872);
    }
    _M0L3stpS1870 = _M0L7_2aSomeS1872;
  }
  _result_5948
  = _M0FP26RiantR8snn__mbt15compose_2einner(_M0L4popsS1873, _M0L5connsS1874, _M0L5stimsS1861, _M0L8monitorsS1864, _M0L4stdpS1867, _M0L3stpS1870);
  moonbit_decref_cycle_free(_M0L5stimsS1861);
  moonbit_decref_cycle_free(_M0L8monitorsS1864);
  moonbit_decref_cycle_free(_M0L4stdpS1867);
  moonbit_decref_cycle_free(_M0L3stpS1870);
  return _result_5948;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1855,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1856,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1857,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1858,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1859,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1860
) {
  struct _M0TP26RiantR8snn__mbt4Time* _M0L6_2atmpS5443;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _block_5949;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5443 = _M0MP26RiantR8snn__mbt4Time3new();
  moonbit_incref_cycle_free(_M0L4popsS1855);
  moonbit_incref_cycle_free(_M0L5connsS1856);
  moonbit_incref_cycle_free(_M0L5stimsS1857);
  moonbit_incref_cycle_free(_M0L8monitorsS1858);
  moonbit_incref_cycle_free(_M0L4stdpS1859);
  moonbit_incref_cycle_free(_M0L3stpS1860);
  _block_5949
  = (struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel));
  Moonbit_object_header(_block_5949)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_5949->$0 = _M0L4popsS1855;
  _block_5949->$1 = _M0L5connsS1856;
  _block_5949->$2 = _M0L5stimsS1857;
  _block_5949->$3 = _M0L6_2atmpS5443;
  _block_5949->$4 = _M0L8monitorsS1858;
  _block_5949->$5 = _M0L4stdpS1859;
  _block_5949->$6 = _M0L3stpS1860;
  return _block_5949;
}

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void* _M0L1sS1841,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS1829,
  float _M0L2dtS1836
) {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1xS1827;
  float _M0L1wS1828;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1xS1831;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1xS1833;
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1xS1835;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1xS1838;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1xS1840;
  float _M0L6_2atmpS5442;
  float _M0L6_2atmpS5441;
  float _M0L6_2atmpS5440;
  float _M0L6_2atmpS5439;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  switch (Moonbit_object_tag(_M0L1sS1841)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__* _M0L14_2aPoissonIF__S1842 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L1sS1841;
      struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L4_2axS1843 =
        _M0L14_2aPoissonIF__S1842->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1843);
      _M0L1xS1840 = _M0L4_2axS1843;
      goto join_1839;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__* _M0L17_2aPoissonLayer__S1844 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__*)_M0L1sS1841;
      struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L4_2axS1845 =
        _M0L17_2aPoissonLayer__S1844->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1845);
      _M0L1xS1838 = _M0L4_2axS1845;
      goto join_1837;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__* _M0L15_2aBalancedIF__S1846 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__*)_M0L1sS1841;
      struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L4_2axS1847 =
        _M0L15_2aBalancedIF__S1846->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1847);
      _M0L1xS1835 = _M0L4_2axS1847;
      goto join_1834;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__* _M0L14_2aCurrentIF__S1848 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__*)_M0L1sS1841;
      struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L4_2axS1849 =
        _M0L14_2aCurrentIF__S1848->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1849);
      _M0L1xS1833 = _M0L4_2axS1849;
      goto join_1832;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__* _M0L15_2aCurrentArr__S1850 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__*)_M0L1sS1841;
      struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L4_2axS1851 =
        _M0L15_2aCurrentArr__S1850->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1851);
      _M0L1xS1831 = _M0L4_2axS1851;
      goto join_1830;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__* _M0L14_2aTimedStim__S1852 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__*)_M0L1sS1841;
      struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L4_2axS1853 =
        _M0L14_2aTimedStim__S1852->$0;
      float _M0L4_2awS1854 = _M0L14_2aTimedStim__S1852->$1;
      moonbit_incref_cycle_free(_M0L4_2axS1853);
      _M0L1xS1827 = _M0L4_2axS1853;
      _M0L1wS1828 = _M0L4_2awS1854;
      goto join_1826;
      break;
    }
  }
  goto joinlet_5955;
  join_1839:;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5442 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1829);
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt13stimulate__if(_M0L1xS1840, _M0L6_2atmpS5442, _M0L2dtS1836);
  moonbit_decref_cycle_free(_M0L1xS1840);
  joinlet_5955:;
  goto joinlet_5954;
  join_1837:;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5441 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1829);
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt16stimulate__layer(_M0L1xS1838, _M0L6_2atmpS5441, _M0L2dtS1836);
  moonbit_decref_cycle_free(_M0L1xS1838);
  joinlet_5954:;
  goto joinlet_5953;
  join_1834:;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5440 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1829);
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt19stimulate__balanced(_M0L1xS1835, _M0L6_2atmpS5440, _M0L2dtS1836);
  moonbit_decref_cycle_free(_M0L1xS1835);
  joinlet_5953:;
  goto joinlet_5952;
  join_1832:;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt22stimulate__current__if(_M0L1xS1833);
  moonbit_decref_cycle_free(_M0L1xS1833);
  joinlet_5952:;
  goto joinlet_5951;
  join_1830:;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt25stimulate__current__array(_M0L1xS1831);
  moonbit_decref_cycle_free(_M0L1xS1831);
  joinlet_5951:;
  goto joinlet_5950;
  join_1826:;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5439 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1829);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L1xS1827, _M0L6_2atmpS5439, _M0L1wS1828);
  moonbit_decref_cycle_free(_M0L1xS1827);
  joinlet_5950:;
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1819,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1820,
  moonbit_string_t _M0L3symS1825,
  float _M0L2muS1821,
  float _M0L5sigmaS1822,
  float _M0L1pS1823,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1824
) {
  int32_t _M0L1nS5437;
  int32_t _M0L1nS5438;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1818;
  float* _M0L6_2atmpS5436;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5427;
  float* _M0L6_2atmpS5435;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5428;
  float* _M0L6_2atmpS5434;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5429;
  int32_t* _M0L6_2atmpS5433;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS5430;
  float* _M0L6_2atmpS5432;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5431;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_5956;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS5437 = _M0L3preS1819->$2;
  _M0L1nS5438 = _M0L4postS1820->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1818
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS5437, _M0L1nS5438, _M0L2muS1821, _M0L5sigmaS1822, _M0L1pS1823, _M0L3rngS1824);
  _M0L6_2atmpS5436 = moonbit_empty_float_array;
  _M0L6_2atmpS5427
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5427)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5427->$0 = _M0L6_2atmpS5436;
  _M0L6_2atmpS5427->$1 = 0;
  _M0L6_2atmpS5435 = moonbit_empty_float_array;
  _M0L6_2atmpS5428
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5428)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5428->$0 = _M0L6_2atmpS5435;
  _M0L6_2atmpS5428->$1 = 0;
  _M0L6_2atmpS5434 = moonbit_empty_float_array;
  _M0L6_2atmpS5429
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5429)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5429->$0 = _M0L6_2atmpS5434;
  _M0L6_2atmpS5429->$1 = 0;
  _M0L6_2atmpS5433 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS5430
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS5430)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS5430->$0 = _M0L6_2atmpS5433;
  _M0L6_2atmpS5430->$1 = 0;
  _M0L6_2atmpS5432 = moonbit_empty_float_array;
  _M0L6_2atmpS5431
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5431)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5431->$0 = _M0L6_2atmpS5432;
  _M0L6_2atmpS5431->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1819);
  moonbit_incref_cycle_free(_M0L4postS1820);
  moonbit_incref_cycle_free(_M0L3symS1825);
  _block_5956
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_5956)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _block_5956->$0 = _M0L3preS1819;
  _block_5956->$1 = _M0L4postS1820;
  _block_5956->$2 = _M0L3symS1825;
  _block_5956->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_5956->$4 = _M0L6matrixS1818;
  _block_5956->$5 = _M0L6_2atmpS5427;
  _block_5956->$6 = _M0L6_2atmpS5428;
  _block_5956->$7 = _M0L6_2atmpS5429;
  _block_5956->$8 = _M0L6_2atmpS5430;
  _block_5956->$9 = _M0L6_2atmpS5431;
  return _block_5956;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1814,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1791,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1793,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1810,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1804,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1801,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1797,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1795,
  float _M0L6t__nowS1789,
  float _M0L2dtS1798
) {
  int32_t _M0L6n__preS1790;
  int32_t _M0L7n__postS1792;
  float _M0L6tau__yS5426;
  float _M0L11inv__tau__yS1794;
  struct _M0TPB8MutLocalGiE* _M0L1jS1796;
  struct _M0TPB8MutLocalGiE* _M0L1iS1800;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1790 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1791);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1792 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1793);
  _M0L6tau__yS5426 = _M0L5paramS1795->$2;
  _M0L11inv__tau__yS1794 = 0x1p+0f / _M0L6tau__yS5426;
  _M0L1jS1796
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1796)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1796->$0 = 0;
  while (1) {
    int32_t _M0L3valS5343 = _M0L1jS1796->$0;
    if (_M0L3valS5343 < _M0L6n__preS1790) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5344 = _M0L4varsS1797->$0;
      int32_t _M0L3valS5345 = _M0L1jS1796->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5354 = _M0L4varsS1797->$0;
      int32_t _M0L3valS5355 = _M0L1jS1796->$0;
      float _M0L6_2atmpS5347;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5352;
      int32_t _M0L3valS5353;
      float _M0L6_2atmpS5351;
      float _M0L6_2atmpS5350;
      float _M0L6_2atmpS5349;
      float _M0L6_2atmpS5348;
      float _M0L6_2atmpS5346;
      int32_t _M0L3valS5356;
      int32_t _M0L3valS5364;
      int32_t _M0L6_2atmpS5363;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5347
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5354, _M0L3valS5355);
      _M0L4tpreS5352 = _M0L4varsS1797->$0;
      _M0L3valS5353 = _M0L1jS1796->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5351
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5352, _M0L3valS5353);
      _M0L6_2atmpS5350 = -_M0L6_2atmpS5351;
      _M0L6_2atmpS5349 = _M0L2dtS1798 * _M0L6_2atmpS5350;
      _M0L6_2atmpS5348 = _M0L6_2atmpS5349 * _M0L11inv__tau__yS1794;
      _M0L6_2atmpS5346 = _M0L6_2atmpS5347 + _M0L6_2atmpS5348;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5344, _M0L3valS5345, _M0L6_2atmpS5346);
      _M0L3valS5356 = _M0L1jS1796->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1791, _M0L3valS5356)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5357 = _M0L4varsS1797->$0;
        int32_t _M0L3valS5358 = _M0L1jS1796->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5361 = _M0L4varsS1797->$0;
        int32_t _M0L3valS5362 = _M0L1jS1796->$0;
        float _M0L6_2atmpS5360;
        float _M0L6_2atmpS5359;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5360
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5361, _M0L3valS5362);
        _M0L6_2atmpS5359 = _M0L6_2atmpS5360 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5357, _M0L3valS5358, _M0L6_2atmpS5359);
      }
      _M0L3valS5364 = _M0L1jS1796->$0;
      _M0L6_2atmpS5363 = _M0L3valS5364 + 1;
      _M0L1jS1796->$0 = _M0L6_2atmpS5363;
      continue;
    }
    break;
  }
  _M0L1iS1800
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1800)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1800->$0 = 0;
  while (1) {
    int32_t _M0L3valS5365 = _M0L1iS1800->$0;
    if (_M0L3valS5365 < _M0L7n__postS1792) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5366 = _M0L4varsS1797->$1;
      int32_t _M0L3valS5367 = _M0L1iS1800->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5379 = _M0L4varsS1797->$1;
      int32_t _M0L3valS5380 = _M0L1iS1800->$0;
      float _M0L6_2atmpS5369;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5377;
      int32_t _M0L3valS5378;
      float _M0L6_2atmpS5374;
      int32_t _M0L3valS5376;
      float _M0L6_2atmpS5375;
      float _M0L6_2atmpS5373;
      float _M0L6_2atmpS5372;
      float _M0L6_2atmpS5371;
      float _M0L6_2atmpS5370;
      float _M0L6_2atmpS5368;
      int32_t _M0L3valS5381;
      int32_t _M0L3valS5389;
      int32_t _M0L6_2atmpS5388;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5369
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5379, _M0L3valS5380);
      _M0L5tpostS5377 = _M0L4varsS1797->$1;
      _M0L3valS5378 = _M0L1iS1800->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5374
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5377, _M0L3valS5378);
      _M0L3valS5376 = _M0L1iS1800->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5375
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1801, _M0L3valS5376);
      _M0L6_2atmpS5373 = _M0L6_2atmpS5374 - _M0L6_2atmpS5375;
      _M0L6_2atmpS5372 = -_M0L6_2atmpS5373;
      _M0L6_2atmpS5371 = _M0L2dtS1798 * _M0L6_2atmpS5372;
      _M0L6_2atmpS5370 = _M0L6_2atmpS5371 * _M0L11inv__tau__yS1794;
      _M0L6_2atmpS5368 = _M0L6_2atmpS5369 + _M0L6_2atmpS5370;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5366, _M0L3valS5367, _M0L6_2atmpS5368);
      _M0L3valS5381 = _M0L1iS1800->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1793, _M0L3valS5381)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5382 = _M0L4varsS1797->$1;
        int32_t _M0L3valS5383 = _M0L1iS1800->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5386 = _M0L4varsS1797->$1;
        int32_t _M0L3valS5387 = _M0L1iS1800->$0;
        float _M0L6_2atmpS5385;
        float _M0L6_2atmpS5384;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5385
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5386, _M0L3valS5387);
        _M0L6_2atmpS5384 = _M0L6_2atmpS5385 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5382, _M0L3valS5383, _M0L6_2atmpS5384);
      }
      _M0L3valS5389 = _M0L1iS1800->$0;
      _M0L6_2atmpS5388 = _M0L3valS5389 + 1;
      _M0L1iS1800->$0 = _M0L6_2atmpS5388;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1800);
    }
    break;
  }
  _M0L1jS1796->$0 = 0;
  while (1) {
    int32_t _M0L3valS5390 = _M0L1jS1796->$0;
    if (_M0L3valS5390 < _M0L6n__preS1790) {
      int32_t _M0L3valS5425 = _M0L1jS1796->$0;
      int32_t _M0L5startS1803;
      int32_t _M0L3valS5424;
      int32_t _M0L6_2atmpS5423;
      int32_t _M0L3endS1805;
      int32_t _M0L3valS5422;
      int32_t _M0L10pre__firedS1806;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5420;
      int32_t _M0L3valS5421;
      float _M0L7tpre__jS1807;
      struct _M0TPB8MutLocalGiE* _M0L1sS1808;
      int32_t _M0L3valS5419;
      int32_t _M0L6_2atmpS5418;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1803
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1804, _M0L3valS5425);
      _M0L3valS5424 = _M0L1jS1796->$0;
      _M0L6_2atmpS5423 = _M0L3valS5424 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1805
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1804, _M0L6_2atmpS5423);
      _M0L3valS5422 = _M0L1jS1796->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1806
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1791, _M0L3valS5422);
      _M0L4tpreS5420 = _M0L4varsS1797->$0;
      _M0L3valS5421 = _M0L1jS1796->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1807
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5420, _M0L3valS5421);
      _M0L1sS1808
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1808)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1808->$0 = _M0L5startS1803;
      while (1) {
        int32_t _M0L3valS5391 = _M0L1sS1808->$0;
        if (_M0L3valS5391 < _M0L3endS1805) {
          int32_t _M0L3valS5417 = _M0L1sS1808->$0;
          int32_t _M0L9post__idxS1809;
          int32_t _M0L11post__firedS1811;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5416;
          float _M0L8tpost__iS1812;
          int32_t _M0L3valS5406;
          float _M0L6_2atmpS5404;
          float _M0L6w__minS5405;
          int32_t _M0L3valS5411;
          float _M0L6_2atmpS5409;
          float _M0L6w__maxS5410;
          int32_t _M0L3valS5415;
          int32_t _M0L6_2atmpS5414;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1809
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1810, _M0L3valS5417);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1811
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1793, _M0L9post__idxS1809);
          _M0L5tpostS5416 = _M0L4varsS1797->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1812
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5416, _M0L9post__idxS1809);
          if (_M0L10pre__firedS1806) {
            float _M0L3etaS5396 = _M0L5paramS1795->$0;
            float _M0L2v0S5398 = _M0L5paramS1795->$1;
            float _M0L6_2atmpS5397 = _M0L8tpost__iS1812 - _M0L2v0S5398;
            float _M0L2dwS1813 = _M0L3etaS5396 * _M0L6_2atmpS5397;
            int32_t _M0L3valS5392 = _M0L1sS1808->$0;
            int32_t _M0L3valS5395 = _M0L1sS1808->$0;
            float _M0L6_2atmpS5394;
            float _M0L6_2atmpS5393;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5394
            = _M0MPC15array5Array2atGfE(_M0L1wS1814, _M0L3valS5395);
            _M0L6_2atmpS5393 = _M0L6_2atmpS5394 + _M0L2dwS1813;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1814, _M0L3valS5392, _M0L6_2atmpS5393);
          }
          if (_M0L11post__firedS1811) {
            float _M0L3etaS5403 = _M0L5paramS1795->$0;
            float _M0L2dwS1815 = _M0L3etaS5403 * _M0L7tpre__jS1807;
            int32_t _M0L3valS5399 = _M0L1sS1808->$0;
            int32_t _M0L3valS5402 = _M0L1sS1808->$0;
            float _M0L6_2atmpS5401;
            float _M0L6_2atmpS5400;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5401
            = _M0MPC15array5Array2atGfE(_M0L1wS1814, _M0L3valS5402);
            _M0L6_2atmpS5400 = _M0L6_2atmpS5401 + _M0L2dwS1815;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1814, _M0L3valS5399, _M0L6_2atmpS5400);
          }
          _M0L3valS5406 = _M0L1sS1808->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5404
          = _M0MPC15array5Array2atGfE(_M0L1wS1814, _M0L3valS5406);
          _M0L6w__minS5405 = _M0L5paramS1795->$4;
          if (_M0L6_2atmpS5404 < _M0L6w__minS5405) {
            int32_t _M0L3valS5407 = _M0L1sS1808->$0;
            float _M0L6w__minS5408 = _M0L5paramS1795->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1814, _M0L3valS5407, _M0L6w__minS5408);
          }
          _M0L3valS5411 = _M0L1sS1808->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5409
          = _M0MPC15array5Array2atGfE(_M0L1wS1814, _M0L3valS5411);
          _M0L6w__maxS5410 = _M0L5paramS1795->$3;
          if (_M0L6_2atmpS5409 > _M0L6w__maxS5410) {
            int32_t _M0L3valS5412 = _M0L1sS1808->$0;
            float _M0L6w__maxS5413 = _M0L5paramS1795->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1814, _M0L3valS5412, _M0L6w__maxS5413);
          }
          _M0L3valS5415 = _M0L1sS1808->$0;
          _M0L6_2atmpS5414 = _M0L3valS5415 + 1;
          _M0L1sS1808->$0 = _M0L6_2atmpS5414;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1808);
        }
        break;
      }
      _M0L3valS5419 = _M0L1jS1796->$0;
      _M0L6_2atmpS5418 = _M0L3valS5419 + 1;
      _M0L1jS1796->$0 = _M0L6_2atmpS5418;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1796);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1785,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1763,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1765,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1781,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1775,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1769,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1767,
  float _M0L6t__nowS1761,
  float _M0L2dtS1770
) {
  int32_t _M0L6n__preS1762;
  int32_t _M0L7n__postS1764;
  float _M0L6tau__yS5342;
  float _M0L11inv__tau__yS1766;
  struct _M0TPB8MutLocalGiE* _M0L1jS1768;
  struct _M0TPB8MutLocalGiE* _M0L1iS1772;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1762 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1763);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1764 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1765);
  _M0L6tau__yS5342 = _M0L5paramS1767->$2;
  _M0L11inv__tau__yS1766 = 0x1p+0f / _M0L6tau__yS5342;
  _M0L1jS1768
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1768)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1768->$0 = 0;
  while (1) {
    int32_t _M0L3valS5259 = _M0L1jS1768->$0;
    if (_M0L3valS5259 < _M0L6n__preS1762) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5260 = _M0L4varsS1769->$0;
      int32_t _M0L3valS5261 = _M0L1jS1768->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5270 = _M0L4varsS1769->$0;
      int32_t _M0L3valS5271 = _M0L1jS1768->$0;
      float _M0L6_2atmpS5263;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5268;
      int32_t _M0L3valS5269;
      float _M0L6_2atmpS5267;
      float _M0L6_2atmpS5266;
      float _M0L6_2atmpS5265;
      float _M0L6_2atmpS5264;
      float _M0L6_2atmpS5262;
      int32_t _M0L3valS5272;
      int32_t _M0L3valS5280;
      int32_t _M0L6_2atmpS5279;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5263
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5270, _M0L3valS5271);
      _M0L4tpreS5268 = _M0L4varsS1769->$0;
      _M0L3valS5269 = _M0L1jS1768->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5267
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5268, _M0L3valS5269);
      _M0L6_2atmpS5266 = -_M0L6_2atmpS5267;
      _M0L6_2atmpS5265 = _M0L2dtS1770 * _M0L6_2atmpS5266;
      _M0L6_2atmpS5264 = _M0L6_2atmpS5265 * _M0L11inv__tau__yS1766;
      _M0L6_2atmpS5262 = _M0L6_2atmpS5263 + _M0L6_2atmpS5264;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5260, _M0L3valS5261, _M0L6_2atmpS5262);
      _M0L3valS5272 = _M0L1jS1768->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1763, _M0L3valS5272)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5273 = _M0L4varsS1769->$0;
        int32_t _M0L3valS5274 = _M0L1jS1768->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5277 = _M0L4varsS1769->$0;
        int32_t _M0L3valS5278 = _M0L1jS1768->$0;
        float _M0L6_2atmpS5276;
        float _M0L6_2atmpS5275;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5276
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5277, _M0L3valS5278);
        _M0L6_2atmpS5275 = _M0L6_2atmpS5276 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5273, _M0L3valS5274, _M0L6_2atmpS5275);
      }
      _M0L3valS5280 = _M0L1jS1768->$0;
      _M0L6_2atmpS5279 = _M0L3valS5280 + 1;
      _M0L1jS1768->$0 = _M0L6_2atmpS5279;
      continue;
    }
    break;
  }
  _M0L1iS1772
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1772)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1772->$0 = 0;
  while (1) {
    int32_t _M0L3valS5281 = _M0L1iS1772->$0;
    if (_M0L3valS5281 < _M0L7n__postS1764) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5282 = _M0L4varsS1769->$1;
      int32_t _M0L3valS5283 = _M0L1iS1772->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5292 = _M0L4varsS1769->$1;
      int32_t _M0L3valS5293 = _M0L1iS1772->$0;
      float _M0L6_2atmpS5285;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5290;
      int32_t _M0L3valS5291;
      float _M0L6_2atmpS5289;
      float _M0L6_2atmpS5288;
      float _M0L6_2atmpS5287;
      float _M0L6_2atmpS5286;
      float _M0L6_2atmpS5284;
      int32_t _M0L3valS5294;
      int32_t _M0L3valS5302;
      int32_t _M0L6_2atmpS5301;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5285
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5292, _M0L3valS5293);
      _M0L5tpostS5290 = _M0L4varsS1769->$1;
      _M0L3valS5291 = _M0L1iS1772->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5289
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5290, _M0L3valS5291);
      _M0L6_2atmpS5288 = -_M0L6_2atmpS5289;
      _M0L6_2atmpS5287 = _M0L2dtS1770 * _M0L6_2atmpS5288;
      _M0L6_2atmpS5286 = _M0L6_2atmpS5287 * _M0L11inv__tau__yS1766;
      _M0L6_2atmpS5284 = _M0L6_2atmpS5285 + _M0L6_2atmpS5286;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5282, _M0L3valS5283, _M0L6_2atmpS5284);
      _M0L3valS5294 = _M0L1iS1772->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1765, _M0L3valS5294)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5295 = _M0L4varsS1769->$1;
        int32_t _M0L3valS5296 = _M0L1iS1772->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5299 = _M0L4varsS1769->$1;
        int32_t _M0L3valS5300 = _M0L1iS1772->$0;
        float _M0L6_2atmpS5298;
        float _M0L6_2atmpS5297;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5298
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5299, _M0L3valS5300);
        _M0L6_2atmpS5297 = _M0L6_2atmpS5298 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5295, _M0L3valS5296, _M0L6_2atmpS5297);
      }
      _M0L3valS5302 = _M0L1iS1772->$0;
      _M0L6_2atmpS5301 = _M0L3valS5302 + 1;
      _M0L1iS1772->$0 = _M0L6_2atmpS5301;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1772);
    }
    break;
  }
  _M0L1jS1768->$0 = 0;
  while (1) {
    int32_t _M0L3valS5303 = _M0L1jS1768->$0;
    if (_M0L3valS5303 < _M0L6n__preS1762) {
      int32_t _M0L3valS5341 = _M0L1jS1768->$0;
      int32_t _M0L5startS1774;
      int32_t _M0L3valS5340;
      int32_t _M0L6_2atmpS5339;
      int32_t _M0L3endS1776;
      int32_t _M0L3valS5338;
      int32_t _M0L10pre__firedS1777;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5336;
      int32_t _M0L3valS5337;
      float _M0L7tpre__jS1778;
      struct _M0TPB8MutLocalGiE* _M0L1sS1779;
      int32_t _M0L3valS5335;
      int32_t _M0L6_2atmpS5334;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1774
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1775, _M0L3valS5341);
      _M0L3valS5340 = _M0L1jS1768->$0;
      _M0L6_2atmpS5339 = _M0L3valS5340 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1776
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1775, _M0L6_2atmpS5339);
      _M0L3valS5338 = _M0L1jS1768->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1777
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1763, _M0L3valS5338);
      _M0L4tpreS5336 = _M0L4varsS1769->$0;
      _M0L3valS5337 = _M0L1jS1768->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1778
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5336, _M0L3valS5337);
      _M0L1sS1779
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1779)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1779->$0 = _M0L5startS1774;
      while (1) {
        int32_t _M0L3valS5304 = _M0L1sS1779->$0;
        if (_M0L3valS5304 < _M0L3endS1776) {
          int32_t _M0L3valS5333 = _M0L1sS1779->$0;
          int32_t _M0L9post__idxS1780;
          int32_t _M0L11post__firedS1782;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5332;
          float _M0L8tpost__iS1783;
          int32_t _M0L3valS5322;
          float _M0L6_2atmpS5320;
          float _M0L6w__minS5321;
          int32_t _M0L3valS5327;
          float _M0L6_2atmpS5325;
          float _M0L6w__maxS5326;
          int32_t _M0L3valS5331;
          int32_t _M0L6_2atmpS5330;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1780
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1781, _M0L3valS5333);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1782
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1765, _M0L9post__idxS1780);
          _M0L5tpostS5332 = _M0L4varsS1769->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1783
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5332, _M0L9post__idxS1780);
          if (_M0L10pre__firedS1777) {
            float _M0L3etaS5309 = _M0L5paramS1767->$0;
            float _M0L1rS5314 = _M0L5paramS1767->$1;
            float _M0L6_2atmpS5312 = 0x1p+1f * _M0L1rS5314;
            float _M0L6tau__yS5313 = _M0L5paramS1767->$2;
            float _M0L6_2atmpS5311 = _M0L6_2atmpS5312 * _M0L6tau__yS5313;
            float _M0L6_2atmpS5310 = _M0L8tpost__iS1783 - _M0L6_2atmpS5311;
            float _M0L2dwS1784 = _M0L3etaS5309 * _M0L6_2atmpS5310;
            int32_t _M0L3valS5305 = _M0L1sS1779->$0;
            int32_t _M0L3valS5308 = _M0L1sS1779->$0;
            float _M0L6_2atmpS5307;
            float _M0L6_2atmpS5306;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5307
            = _M0MPC15array5Array2atGfE(_M0L1wS1785, _M0L3valS5308);
            _M0L6_2atmpS5306 = _M0L6_2atmpS5307 + _M0L2dwS1784;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1785, _M0L3valS5305, _M0L6_2atmpS5306);
          }
          if (_M0L11post__firedS1782) {
            float _M0L3etaS5319 = _M0L5paramS1767->$0;
            float _M0L2dwS1786 = _M0L3etaS5319 * _M0L7tpre__jS1778;
            int32_t _M0L3valS5315 = _M0L1sS1779->$0;
            int32_t _M0L3valS5318 = _M0L1sS1779->$0;
            float _M0L6_2atmpS5317;
            float _M0L6_2atmpS5316;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5317
            = _M0MPC15array5Array2atGfE(_M0L1wS1785, _M0L3valS5318);
            _M0L6_2atmpS5316 = _M0L6_2atmpS5317 + _M0L2dwS1786;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1785, _M0L3valS5315, _M0L6_2atmpS5316);
          }
          _M0L3valS5322 = _M0L1sS1779->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5320
          = _M0MPC15array5Array2atGfE(_M0L1wS1785, _M0L3valS5322);
          _M0L6w__minS5321 = _M0L5paramS1767->$4;
          if (_M0L6_2atmpS5320 < _M0L6w__minS5321) {
            int32_t _M0L3valS5323 = _M0L1sS1779->$0;
            float _M0L6w__minS5324 = _M0L5paramS1767->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1785, _M0L3valS5323, _M0L6w__minS5324);
          }
          _M0L3valS5327 = _M0L1sS1779->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5325
          = _M0MPC15array5Array2atGfE(_M0L1wS1785, _M0L3valS5327);
          _M0L6w__maxS5326 = _M0L5paramS1767->$3;
          if (_M0L6_2atmpS5325 > _M0L6w__maxS5326) {
            int32_t _M0L3valS5328 = _M0L1sS1779->$0;
            float _M0L6w__maxS5329 = _M0L5paramS1767->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1785, _M0L3valS5328, _M0L6w__maxS5329);
          }
          _M0L3valS5331 = _M0L1sS1779->$0;
          _M0L6_2atmpS5330 = _M0L3valS5331 + 1;
          _M0L1sS1779->$0 = _M0L6_2atmpS5330;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1779);
        }
        break;
      }
      _M0L3valS5335 = _M0L1jS1768->$0;
      _M0L6_2atmpS5334 = _M0L3valS5335 + 1;
      _M0L1jS1768->$0 = _M0L6_2atmpS5334;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1768);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1759;
  float _M0L2glS1760;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_5965;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1759 = -0x1p+0f;
  _M0L2glS1760 = -0x1p+0f;
  _block_5965
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_5965)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5965->$0 = _M0L1cS1759;
  _block_5965->$1 = _M0L2glS1760;
  _block_5965->$2 = 0x1.ep+3f;
  _block_5965->$3 = -0x1.9p+5f;
  _block_5965->$4 = -0x1.ep+5f;
  _block_5965->$5 = -0x1.18p+6f;
  _block_5965->$6 = 0x1.eb851eb851eb8p-5f;
  _block_5965->$7 = 0x1p+1f;
  _block_5965->$8 = 0x0p+0f;
  _block_5965->$9 = 0x0p+0f;
  _block_5965->$10 = 0x0p+0f;
  return _block_5965;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1733,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1735,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1738
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1732;
  float _M0L2vtS5257;
  float _M0L2vrS5258;
  float _M0L6spreadS1734;
  int32_t _M0L7_2abindS1736;
  int32_t _M0L1kS1737;
  struct _M0TPB5ArrayGfE* _M0L1wS1740;
  struct _M0TPB5ArrayGbE* _M0L4fireS1741;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1742;
  struct _M0TPB5ArrayGfE* _M0L1iS1743;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1744;
  struct _M0TPB5ArrayGfE* _M0L2geS1745;
  struct _M0TPB5ArrayGfE* _M0L2giS1746;
  struct _M0TPB5ArrayGfE* _M0L2heS1747;
  struct _M0TPB5ArrayGfE* _M0L2hiS1748;
  struct _M0TPB5ArrayGfE* _M0L3gluS1749;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1750;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1751;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1752;
  float _M0L4e__eS1753;
  float _M0L4e__iS1754;
  float _M0L3treS1755;
  float _M0L3tdeS1756;
  float _M0L3triS1757;
  float _M0L3tdiS1758;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS5256;
  struct _M0TP26RiantR8snn__mbt2IF* _block_5967;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1732 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  _M0L2vtS5257 = _M0L5paramS1735->$3;
  _M0L2vrS5258 = _M0L5paramS1735->$4;
  _M0L6spreadS1734 = _M0L2vtS5257 - _M0L2vrS5258;
  _M0L7_2abindS1736 = 0;
  _M0L1kS1737 = _M0L7_2abindS1736;
  while (1) {
    if (_M0L1kS1737 < _M0L1nS1733) {
      float _M0L2vrS5252 = _M0L5paramS1735->$4;
      float _M0L6_2atmpS5254;
      float _M0L6_2atmpS5253;
      float _M0L6_2atmpS5251;
      int32_t _M0L6_2atmpS5255;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS5254 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1738);
      _M0L6_2atmpS5253 = _M0L6_2atmpS5254 * _M0L6spreadS1734;
      _M0L6_2atmpS5251 = _M0L2vrS5252 + _M0L6_2atmpS5253;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1732, _M0L1kS1737, _M0L6_2atmpS5251);
      _M0L6_2atmpS5255 = _M0L1kS1737 + 1;
      _M0L1kS1737 = _M0L6_2atmpS5255;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1740 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1741 = _M0MPC15array5Array4makeGbE(_M0L1nS1733, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1742 = _M0MPC15array5Array4makeGiE(_M0L1nS1733, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1743 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1744 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1745 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1746 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1747 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1748 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1749 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1750 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1751 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1752 = _M0MPC15array5Array4makeGfE(_M0L1nS1733, 0x1p+0f);
  _M0L4e__eS1753 = 0x0p+0f;
  _M0L4e__iS1754 = -0x1.2cp+6f;
  _M0L3treS1755 = 0x1p+0f;
  _M0L3tdeS1756 = 0x1.8p+2f;
  _M0L3triS1757 = 0x1p-1f;
  _M0L3tdiS1758 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS5256 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1735);
  _block_5967
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_5967)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_5967->$0 = _M0L5paramS1735;
  _block_5967->$1 = _M0L6_2atmpS5256;
  _block_5967->$2 = _M0L1nS1733;
  _block_5967->$3 = _M0L1vS1732;
  _block_5967->$4 = _M0L1wS1740;
  _block_5967->$5 = _M0L4fireS1741;
  _block_5967->$6 = _M0L4tabsS1742;
  _block_5967->$7 = _M0L1iS1743;
  _block_5967->$8 = _M0L9syn__currS1744;
  _block_5967->$9 = _M0L2geS1745;
  _block_5967->$10 = _M0L2giS1746;
  _block_5967->$11 = _M0L2heS1747;
  _block_5967->$12 = _M0L2hiS1748;
  _block_5967->$13 = _M0L3gluS1749;
  _block_5967->$14 = _M0L4gabaS1750;
  _block_5967->$15 = _M0L7gsyn__eS1751;
  _block_5967->$16 = _M0L7gsyn__iS1752;
  _block_5967->$17 = _M0L4e__eS1753;
  _block_5967->$18 = _M0L4e__iS1754;
  _block_5967->$19 = _M0L3treS1755;
  _block_5967->$20 = _M0L3tdeS1756;
  _block_5967->$21 = _M0L3triS1757;
  _block_5967->$22 = _M0L3tdiS1758;
  return _block_5967;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_5968;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_5968
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_5968)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5968->$0 = 0x1p+1f;
  return _block_5968;
}

int32_t _M0FP26RiantR8snn__mbt14integrate__any(
  void* _M0L1pS1713,
  float _M0L2dtS1696
) {
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1xS1695;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1xS1698;
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1xS1700;
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1xS1702;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1xS1704;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1xS1706;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1xS1708;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1xS1710;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1xS1712;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  switch (Moonbit_object_tag(_M0L1pS1713)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__* _M0L7_2aIF__S1714 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4_2axS1715 =
        _M0L7_2aIF__S1714->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1715);
      _M0L1xS1712 = _M0L4_2axS1715;
      goto join_1711;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__* _M0L9_2aAdEx__S1716 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4_2axS1717 =
        _M0L9_2aAdEx__S1716->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1717);
      _M0L1xS1710 = _M0L4_2axS1717;
      goto join_1709;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__* _M0L15_2aAdExSinExp__S1718 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4_2axS1719 =
        _M0L15_2aAdExSinExp__S1718->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1719);
      _M0L1xS1708 = _M0L4_2axS1719;
      goto join_1707;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__* _M0L7_2aIZ__S1720 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt2IZ* _M0L4_2axS1721 =
        _M0L7_2aIZ__S1720->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1721);
      _M0L1xS1706 = _M0L4_2axS1721;
      goto join_1705;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__* _M0L7_2aHH__S1722 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt2HH* _M0L4_2axS1723 =
        _M0L7_2aHH__S1722->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1723);
      _M0L1xS1704 = _M0L4_2axS1723;
      goto join_1703;
      break;
    }
    
    case 5: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__* _M0L7_2aML__S1724 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L4_2axS1725 =
        _M0L7_2aML__S1724->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1725);
      _M0L1xS1702 = _M0L4_2axS1725;
      goto join_1701;
      break;
    }
    
    case 6: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__* _M0L12_2aPoisson__S1726 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt7Poisson* _M0L4_2axS1727 =
        _M0L12_2aPoisson__S1726->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1727);
      _M0L1xS1700 = _M0L4_2axS1727;
      goto join_1699;
      break;
    }
    
    case 7: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__* _M0L7_2aWC__S1728 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4_2axS1729 =
        _M0L7_2aWC__S1728->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1729);
      _M0L1xS1698 = _M0L4_2axS1729;
      goto join_1697;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__* _M0L11_2aHetRec__S1730 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__*)_M0L1pS1713;
      struct _M0TP26RiantR8snn__mbt6HetRec* _M0L4_2axS1731 =
        _M0L11_2aHetRec__S1730->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1731);
      _M0L1xS1695 = _M0L4_2axS1731;
      goto join_1694;
      break;
    }
  }
  goto joinlet_5977;
  join_1711:;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L1xS1712, _M0L2dtS1696);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1xS1712);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L1xS1712, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1712);
  joinlet_5977:;
  goto joinlet_5976;
  join_1709:;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1xS1710, _M0L2dtS1696);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1xS1710);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt10step__adex(_M0L1xS1710, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1710);
  joinlet_5976:;
  goto joinlet_5975;
  join_1707:;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L1xS1708, _M0L2dtS1696);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L1xS1708);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L1xS1708, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1708);
  joinlet_5975:;
  goto joinlet_5974;
  join_1705:;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__iz(_M0L1xS1706, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1706);
  joinlet_5974:;
  goto joinlet_5973;
  join_1703:;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__hh(_M0L1xS1704, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1704);
  joinlet_5973:;
  goto joinlet_5972;
  join_1701:;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__ml(_M0L1xS1702, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1702);
  joinlet_5972:;
  goto joinlet_5971;
  join_1699:;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt13step__poisson(_M0L1xS1700, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1700);
  joinlet_5971:;
  goto joinlet_5970;
  join_1697:;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__wc(_M0L1xS1698, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1698);
  joinlet_5970:;
  goto joinlet_5969;
  join_1694:;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__hetrec(_M0L1xS1695, _M0L2dtS1696);
  moonbit_decref_cycle_free(_M0L1xS1695);
  joinlet_5969:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS1689,
  float _M0L2dtS1692
) {
  int32_t _M0L1nS1688;
  int32_t _M0L7_2abindS1690;
  int32_t _M0L1kS1691;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS1688 = _M0L1pS1689->$1;
  _M0L7_2abindS1690 = 0;
  _M0L1kS1691 = _M0L7_2abindS1690;
  while (1) {
    if (_M0L1kS1691 < _M0L1nS1688) {
      struct _M0TPB5ArrayGfE* _M0L1xS5231 = _M0L1pS1689->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS5244 = _M0L1pS1689->$2;
      float _M0L6_2atmpS5233;
      struct _M0TPB5ArrayGfE* _M0L1xS5243;
      float _M0L6_2atmpS5242;
      float _M0L6_2atmpS5239;
      struct _M0TPB5ArrayGfE* _M0L1gS5241;
      float _M0L6_2atmpS5240;
      float _M0L6_2atmpS5236;
      struct _M0TPB5ArrayGfE* _M0L1iS5238;
      float _M0L6_2atmpS5237;
      float _M0L6_2atmpS5235;
      float _M0L6_2atmpS5234;
      float _M0L6_2atmpS5232;
      struct _M0TPB5ArrayGfE* _M0L1rS5245;
      struct _M0TPB5ArrayGfE* _M0L1xS5248;
      float _M0L6_2atmpS5247;
      float _M0L6_2atmpS5246;
      struct _M0TPB5ArrayGfE* _M0L1gS5249;
      int32_t _M0L6_2atmpS5250;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5233 = _M0MPC15array5Array2atGfE(_M0L1xS5244, _M0L1kS1691);
      _M0L1xS5243 = _M0L1pS1689->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5242 = _M0MPC15array5Array2atGfE(_M0L1xS5243, _M0L1kS1691);
      _M0L6_2atmpS5239 = -_M0L6_2atmpS5242;
      _M0L1gS5241 = _M0L1pS1689->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5240 = _M0MPC15array5Array2atGfE(_M0L1gS5241, _M0L1kS1691);
      _M0L6_2atmpS5236 = _M0L6_2atmpS5239 + _M0L6_2atmpS5240;
      _M0L1iS5238 = _M0L1pS1689->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5237 = _M0MPC15array5Array2atGfE(_M0L1iS5238, _M0L1kS1691);
      _M0L6_2atmpS5235 = _M0L6_2atmpS5236 + _M0L6_2atmpS5237;
      _M0L6_2atmpS5234 = _M0L2dtS1692 * _M0L6_2atmpS5235;
      _M0L6_2atmpS5232 = _M0L6_2atmpS5233 + _M0L6_2atmpS5234;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS5231, _M0L1kS1691, _M0L6_2atmpS5232);
      _M0L1rS5245 = _M0L1pS1689->$3;
      _M0L1xS5248 = _M0L1pS1689->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5247 = _M0MPC15array5Array2atGfE(_M0L1xS5248, _M0L1kS1691);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5246 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5247);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS5245, _M0L1kS1691, _M0L6_2atmpS5246);
      _M0L1gS5249 = _M0L1pS1689->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS5249, _M0L1kS1691, 0x0p+0f);
      _M0L6_2atmpS5250 = _M0L1kS1691 + 1;
      _M0L1kS1691 = _M0L6_2atmpS5250;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS1681,
  float _M0L2dtS1683
) {
  int32_t _M0L1nS1680;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS5230;
  float _M0L4rateS5229;
  float _M0L8rate__dtS1682;
  int32_t _M0L7_2abindS1684;
  int32_t _M0L1iS1685;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS1680 = _M0L1pS1681->$1;
  _M0L5paramS5230 = _M0L1pS1681->$0;
  _M0L4rateS5229 = _M0L5paramS5230->$0;
  _M0L8rate__dtS1682 = _M0L4rateS5229 * _M0L2dtS1683;
  _M0L7_2abindS1684 = 0;
  _M0L1iS1685 = _M0L7_2abindS1684;
  while (1) {
    if (_M0L1iS1685 < _M0L1nS1680) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS5227 = _M0L1pS1681->$4;
      float _M0L1uS1686;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS5224;
      struct _M0TPB5ArrayGbE* _M0L4fireS5225;
      int32_t _M0L6_2atmpS5226;
      int32_t _M0L6_2atmpS5228;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS1686 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS5227);
      _M0L9randcacheS5224 = _M0L1pS1681->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS5224, _M0L1iS1685, _M0L1uS1686);
      _M0L4fireS5225 = _M0L1pS1681->$2;
      _M0L6_2atmpS5226 = _M0L1uS1686 < _M0L8rate__dtS1682;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5225, _M0L1iS1685, _M0L6_2atmpS5226);
      _M0L6_2atmpS5228 = _M0L1iS1685 + 1;
      _M0L1iS1685 = _M0L6_2atmpS5228;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1pS1640,
  float _M0L2dtS1669
) {
  int32_t _M0L1nS1639;
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* _M0L3p__S1641;
  float _M0L2cmS1642;
  float _M0L2elS1643;
  float _M0L2ekS1644;
  float _M0L3ecaS1645;
  float _M0L2glS1646;
  float _M0L2gkS1647;
  float _M0L3gcaS1648;
  float _M0L6tau__eS1649;
  float _M0L6tau__iS1650;
  float _M0L2v1S1651;
  float _M0L2v2S1652;
  float _M0L2v3S1653;
  float _M0L2v4S1654;
  float _M0L3phiS1655;
  float _M0L4e__eS1656;
  float _M0L4e__iS1657;
  int32_t _M0L7_2abindS1658;
  int32_t _M0L1iS1659;
  int32_t _M0L7_2abindS1671;
  int32_t _M0L1iS1672;
  int32_t _M0L7_2abindS1674;
  int32_t _M0L1iS1675;
  int32_t _M0L7_2abindS1677;
  int32_t _M0L1iS1678;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
  _M0L1nS1639 = _M0L1pS1640->$1;
  _M0L3p__S1641 = _M0L1pS1640->$0;
  _M0L2cmS1642 = _M0L3p__S1641->$0;
  _M0L2elS1643 = _M0L3p__S1641->$1;
  _M0L2ekS1644 = _M0L3p__S1641->$2;
  _M0L3ecaS1645 = _M0L3p__S1641->$3;
  _M0L2glS1646 = _M0L3p__S1641->$4;
  _M0L2gkS1647 = _M0L3p__S1641->$5;
  _M0L3gcaS1648 = _M0L3p__S1641->$6;
  _M0L6tau__eS1649 = _M0L3p__S1641->$7;
  _M0L6tau__iS1650 = _M0L3p__S1641->$8;
  _M0L2v1S1651 = _M0L3p__S1641->$9;
  _M0L2v2S1652 = _M0L3p__S1641->$10;
  _M0L2v3S1653 = _M0L3p__S1641->$11;
  _M0L2v4S1654 = _M0L3p__S1641->$12;
  _M0L3phiS1655 = _M0L3p__S1641->$13;
  _M0L4e__eS1656 = _M0L3p__S1641->$14;
  _M0L4e__iS1657 = _M0L3p__S1641->$15;
  _M0L7_2abindS1658 = 0;
  _M0L1iS1659 = _M0L7_2abindS1658;
  while (1) {
    if (_M0L1iS1659 < _M0L1nS1639) {
      struct _M0TPB5ArrayGfE* _M0L1vS5178 = _M0L1pS1640->$2;
      float _M0L1vS1660;
      struct _M0TPB5ArrayGfE* _M0L1wS5177;
      float _M0L1wS1661;
      float _M0L6_2atmpS5176;
      float _M0L6_2atmpS5175;
      float _M0L6_2atmpS5174;
      float _M0L6_2atmpS5173;
      float _M0L5m__ssS1662;
      struct _M0TPB5ArrayGfE* _M0L1iS5172;
      float _M0L6_2atmpS5169;
      float _M0L6_2atmpS5171;
      float _M0L6_2atmpS5170;
      float _M0L6_2atmpS5165;
      float _M0L6_2atmpS5168;
      float _M0L6_2atmpS5167;
      float _M0L6_2atmpS5166;
      float _M0L6_2atmpS5161;
      float _M0L6_2atmpS5164;
      float _M0L6_2atmpS5163;
      float _M0L6_2atmpS5162;
      float _M0L2dvS1663;
      float _M0L6_2atmpS5160;
      float _M0L6_2atmpS5159;
      float _M0L6_2atmpS5158;
      float _M0L6_2atmpS5157;
      float _M0L5n__ssS1664;
      float _M0L6_2atmpS5155;
      float _M0L6_2atmpS5156;
      float _M0L9cosh__argS1665;
      float _M0L6_2atmpS5152;
      float _M0L6_2atmpS5154;
      float _M0L6_2atmpS5153;
      float _M0L6_2atmpS5151;
      float _M0L9cosh__valS1666;
      float _M0L6_2atmpS5149;
      float _M0L3tauS1667;
      float _M0L6_2atmpS5148;
      float _M0L2dwS1668;
      struct _M0TPB5ArrayGfE* _M0L1vS5141;
      float _M0L6_2atmpS5144;
      float _M0L6_2atmpS5143;
      float _M0L6_2atmpS5142;
      struct _M0TPB5ArrayGfE* _M0L1wS5145;
      float _M0L6_2atmpS5147;
      float _M0L6_2atmpS5146;
      int32_t _M0L6_2atmpS5179;
      #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1vS1660 = _M0MPC15array5Array2atGfE(_M0L1vS5178, _M0L1iS1659);
      _M0L1wS5177 = _M0L1pS1640->$3;
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1wS1661 = _M0MPC15array5Array2atGfE(_M0L1wS5177, _M0L1iS1659);
      _M0L6_2atmpS5176 = _M0L1vS1660 - _M0L2v1S1651;
      _M0L6_2atmpS5175 = _M0L6_2atmpS5176 / _M0L2v2S1652;
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5174 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5175);
      _M0L6_2atmpS5173 = 0x1p+0f + _M0L6_2atmpS5174;
      _M0L5m__ssS1662 = 0x1p-1f * _M0L6_2atmpS5173;
      _M0L1iS5172 = _M0L1pS1640->$5;
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5169 = _M0MPC15array5Array2atGfE(_M0L1iS5172, _M0L1iS1659);
      _M0L6_2atmpS5171 = _M0L2elS1643 - _M0L1vS1660;
      _M0L6_2atmpS5170 = _M0L2glS1646 * _M0L6_2atmpS5171;
      _M0L6_2atmpS5165 = _M0L6_2atmpS5169 + _M0L6_2atmpS5170;
      _M0L6_2atmpS5168 = _M0L3ecaS1645 - _M0L1vS1660;
      _M0L6_2atmpS5167 = _M0L3gcaS1648 * _M0L6_2atmpS5168;
      _M0L6_2atmpS5166 = _M0L6_2atmpS5167 * _M0L5m__ssS1662;
      _M0L6_2atmpS5161 = _M0L6_2atmpS5165 + _M0L6_2atmpS5166;
      _M0L6_2atmpS5164 = _M0L2ekS1644 - _M0L1vS1660;
      _M0L6_2atmpS5163 = _M0L2gkS1647 * _M0L6_2atmpS5164;
      _M0L6_2atmpS5162 = _M0L6_2atmpS5163 * _M0L1wS1661;
      _M0L2dvS1663 = _M0L6_2atmpS5161 + _M0L6_2atmpS5162;
      _M0L6_2atmpS5160 = _M0L1vS1660 - _M0L2v3S1653;
      _M0L6_2atmpS5159 = _M0L6_2atmpS5160 / _M0L2v4S1654;
      #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5158 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5159);
      _M0L6_2atmpS5157 = 0x1p+0f + _M0L6_2atmpS5158;
      _M0L5n__ssS1664 = 0x1p-1f * _M0L6_2atmpS5157;
      _M0L6_2atmpS5155 = _M0L1vS1660 - _M0L2v3S1653;
      _M0L6_2atmpS5156 = 0x1p+1f * _M0L2v4S1654;
      _M0L9cosh__argS1665 = _M0L6_2atmpS5155 / _M0L6_2atmpS5156;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5152 = _M0FP26RiantR8snn__mbt4expf(_M0L9cosh__argS1665);
      _M0L6_2atmpS5154 = -_M0L9cosh__argS1665;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5153 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5154);
      _M0L6_2atmpS5151 = _M0L6_2atmpS5152 + _M0L6_2atmpS5153;
      _M0L9cosh__valS1666 = 0x1p-1f * _M0L6_2atmpS5151;
      _M0L6_2atmpS5149 = _M0L3phiS1655 * _M0L9cosh__valS1666;
      if (_M0L6_2atmpS5149 != 0x0p+0f) {
        float _M0L6_2atmpS5150 = _M0L3phiS1655 * _M0L9cosh__valS1666;
        _M0L3tauS1667 = 0x1p+0f / _M0L6_2atmpS5150;
      } else {
        _M0L3tauS1667 = 0x0p+0f;
      }
      _M0L6_2atmpS5148 = _M0L5n__ssS1664 - _M0L1wS1661;
      _M0L2dwS1668 = _M0L6_2atmpS5148 / _M0L3tauS1667;
      _M0L1vS5141 = _M0L1pS1640->$2;
      _M0L6_2atmpS5144 = _M0L2dtS1669 / _M0L2cmS1642;
      _M0L6_2atmpS5143 = _M0L6_2atmpS5144 * _M0L2dvS1663;
      _M0L6_2atmpS5142 = _M0L1vS1660 + _M0L6_2atmpS5143;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5141, _M0L1iS1659, _M0L6_2atmpS5142);
      _M0L1wS5145 = _M0L1pS1640->$3;
      _M0L6_2atmpS5147 = _M0L2dtS1669 * _M0L2dwS1668;
      _M0L6_2atmpS5146 = _M0L1wS1661 + _M0L6_2atmpS5147;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS5145, _M0L1iS1659, _M0L6_2atmpS5146);
      _M0L6_2atmpS5179 = _M0L1iS1659 + 1;
      _M0L1iS1659 = _M0L6_2atmpS5179;
      continue;
    }
    break;
  }
  _M0L7_2abindS1671 = 0;
  _M0L1iS1672 = _M0L7_2abindS1671;
  while (1) {
    if (_M0L1iS1672 < _M0L1nS1639) {
      struct _M0TPB5ArrayGfE* _M0L1vS5180 = _M0L1pS1640->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5198 = _M0L1pS1640->$2;
      float _M0L6_2atmpS5182;
      float _M0L6_2atmpS5184;
      struct _M0TPB5ArrayGfE* _M0L2geS5197;
      float _M0L6_2atmpS5193;
      struct _M0TPB5ArrayGfE* _M0L1vS5196;
      float _M0L6_2atmpS5195;
      float _M0L6_2atmpS5194;
      float _M0L6_2atmpS5186;
      struct _M0TPB5ArrayGfE* _M0L2giS5192;
      float _M0L6_2atmpS5188;
      struct _M0TPB5ArrayGfE* _M0L1vS5191;
      float _M0L6_2atmpS5190;
      float _M0L6_2atmpS5189;
      float _M0L6_2atmpS5187;
      float _M0L6_2atmpS5185;
      float _M0L6_2atmpS5183;
      float _M0L6_2atmpS5181;
      int32_t _M0L6_2atmpS5199;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5182 = _M0MPC15array5Array2atGfE(_M0L1vS5198, _M0L1iS1672);
      _M0L6_2atmpS5184 = _M0L2dtS1669 / _M0L2cmS1642;
      _M0L2geS5197 = _M0L1pS1640->$6;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5193 = _M0MPC15array5Array2atGfE(_M0L2geS5197, _M0L1iS1672);
      _M0L1vS5196 = _M0L1pS1640->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5195 = _M0MPC15array5Array2atGfE(_M0L1vS5196, _M0L1iS1672);
      _M0L6_2atmpS5194 = _M0L4e__eS1656 - _M0L6_2atmpS5195;
      _M0L6_2atmpS5186 = _M0L6_2atmpS5193 * _M0L6_2atmpS5194;
      _M0L2giS5192 = _M0L1pS1640->$7;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5188 = _M0MPC15array5Array2atGfE(_M0L2giS5192, _M0L1iS1672);
      _M0L1vS5191 = _M0L1pS1640->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5190 = _M0MPC15array5Array2atGfE(_M0L1vS5191, _M0L1iS1672);
      _M0L6_2atmpS5189 = _M0L4e__iS1657 - _M0L6_2atmpS5190;
      _M0L6_2atmpS5187 = _M0L6_2atmpS5188 * _M0L6_2atmpS5189;
      _M0L6_2atmpS5185 = _M0L6_2atmpS5186 + _M0L6_2atmpS5187;
      _M0L6_2atmpS5183 = _M0L6_2atmpS5184 * _M0L6_2atmpS5185;
      _M0L6_2atmpS5181 = _M0L6_2atmpS5182 + _M0L6_2atmpS5183;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5180, _M0L1iS1672, _M0L6_2atmpS5181);
      _M0L6_2atmpS5199 = _M0L1iS1672 + 1;
      _M0L1iS1672 = _M0L6_2atmpS5199;
      continue;
    }
    break;
  }
  _M0L7_2abindS1674 = 0;
  _M0L1iS1675 = _M0L7_2abindS1674;
  while (1) {
    if (_M0L1iS1675 < _M0L1nS1639) {
      struct _M0TPB5ArrayGfE* _M0L2geS5200 = _M0L1pS1640->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS5208 = _M0L1pS1640->$6;
      float _M0L6_2atmpS5202;
      struct _M0TPB5ArrayGfE* _M0L2geS5207;
      float _M0L6_2atmpS5206;
      float _M0L6_2atmpS5205;
      float _M0L6_2atmpS5204;
      float _M0L6_2atmpS5203;
      float _M0L6_2atmpS5201;
      struct _M0TPB5ArrayGfE* _M0L2giS5209;
      struct _M0TPB5ArrayGfE* _M0L2giS5217;
      float _M0L6_2atmpS5211;
      struct _M0TPB5ArrayGfE* _M0L2giS5216;
      float _M0L6_2atmpS5215;
      float _M0L6_2atmpS5214;
      float _M0L6_2atmpS5213;
      float _M0L6_2atmpS5212;
      float _M0L6_2atmpS5210;
      int32_t _M0L6_2atmpS5218;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5202 = _M0MPC15array5Array2atGfE(_M0L2geS5208, _M0L1iS1675);
      _M0L2geS5207 = _M0L1pS1640->$6;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5206 = _M0MPC15array5Array2atGfE(_M0L2geS5207, _M0L1iS1675);
      _M0L6_2atmpS5205 = -_M0L6_2atmpS5206;
      _M0L6_2atmpS5204 = _M0L6_2atmpS5205 / _M0L6tau__eS1649;
      _M0L6_2atmpS5203 = _M0L2dtS1669 * _M0L6_2atmpS5204;
      _M0L6_2atmpS5201 = _M0L6_2atmpS5202 + _M0L6_2atmpS5203;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS5200, _M0L1iS1675, _M0L6_2atmpS5201);
      _M0L2giS5209 = _M0L1pS1640->$7;
      _M0L2giS5217 = _M0L1pS1640->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5211 = _M0MPC15array5Array2atGfE(_M0L2giS5217, _M0L1iS1675);
      _M0L2giS5216 = _M0L1pS1640->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5215 = _M0MPC15array5Array2atGfE(_M0L2giS5216, _M0L1iS1675);
      _M0L6_2atmpS5214 = -_M0L6_2atmpS5215;
      _M0L6_2atmpS5213 = _M0L6_2atmpS5214 / _M0L6tau__iS1650;
      _M0L6_2atmpS5212 = _M0L2dtS1669 * _M0L6_2atmpS5213;
      _M0L6_2atmpS5210 = _M0L6_2atmpS5211 + _M0L6_2atmpS5212;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS5209, _M0L1iS1675, _M0L6_2atmpS5210);
      _M0L6_2atmpS5218 = _M0L1iS1675 + 1;
      _M0L1iS1675 = _M0L6_2atmpS5218;
      continue;
    }
    break;
  }
  _M0L7_2abindS1677 = 0;
  _M0L1iS1678 = _M0L7_2abindS1677;
  while (1) {
    if (_M0L1iS1678 < _M0L1nS1639) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5219 = _M0L1pS1640->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5222 = _M0L1pS1640->$2;
      float _M0L6_2atmpS5221;
      int32_t _M0L6_2atmpS5220;
      int32_t _M0L6_2atmpS5223;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5221 = _M0MPC15array5Array2atGfE(_M0L1vS5222, _M0L1iS1678);
      _M0L6_2atmpS5220 = _M0L6_2atmpS5221 > 0x1.4p+4f;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5219, _M0L1iS1678, _M0L6_2atmpS5220);
      _M0L6_2atmpS5223 = _M0L1iS1678 + 1;
      _M0L1iS1678 = _M0L6_2atmpS5223;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS1608,
  float _M0L2dtS1620
) {
  int32_t _M0L1nS1607;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S1609;
  float _M0L1aS1610;
  float _M0L1bS1611;
  float _M0L1cS1612;
  float _M0L1dS1613;
  float _M0L6tau__eS1614;
  float _M0L6tau__iS1615;
  float _M0L4e__eS1616;
  float _M0L4e__iS1617;
  int32_t _M0L7_2abindS1618;
  int32_t _M0L1iS1619;
  int32_t _M0L7_2abindS1622;
  int32_t _M0L1iS1623;
  int32_t _M0L7_2abindS1629;
  int32_t _M0L1iS1630;
  int32_t _M0L7_2abindS1633;
  int32_t _M0L1iS1634;
  int32_t _M0L7_2abindS1636;
  int32_t _M0L1iS1637;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS1607 = _M0L1pS1608->$1;
  _M0L3p__S1609 = _M0L1pS1608->$0;
  _M0L1aS1610 = _M0L3p__S1609->$0;
  _M0L1bS1611 = _M0L3p__S1609->$1;
  _M0L1cS1612 = _M0L3p__S1609->$2;
  _M0L1dS1613 = _M0L3p__S1609->$3;
  _M0L6tau__eS1614 = _M0L3p__S1609->$4;
  _M0L6tau__iS1615 = _M0L3p__S1609->$5;
  _M0L4e__eS1616 = _M0L3p__S1609->$6;
  _M0L4e__iS1617 = _M0L3p__S1609->$7;
  _M0L7_2abindS1618 = 0;
  _M0L1iS1619 = _M0L7_2abindS1618;
  while (1) {
    if (_M0L1iS1619 < _M0L1nS1607) {
      struct _M0TPB5ArrayGfE* _M0L2geS5049 = _M0L1pS1608->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS5057 = _M0L1pS1608->$6;
      float _M0L6_2atmpS5051;
      struct _M0TPB5ArrayGfE* _M0L2geS5056;
      float _M0L6_2atmpS5055;
      float _M0L6_2atmpS5054;
      float _M0L6_2atmpS5053;
      float _M0L6_2atmpS5052;
      float _M0L6_2atmpS5050;
      struct _M0TPB5ArrayGfE* _M0L2giS5058;
      struct _M0TPB5ArrayGfE* _M0L2giS5066;
      float _M0L6_2atmpS5060;
      struct _M0TPB5ArrayGfE* _M0L2giS5065;
      float _M0L6_2atmpS5064;
      float _M0L6_2atmpS5063;
      float _M0L6_2atmpS5062;
      float _M0L6_2atmpS5061;
      float _M0L6_2atmpS5059;
      int32_t _M0L6_2atmpS5067;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5051 = _M0MPC15array5Array2atGfE(_M0L2geS5057, _M0L1iS1619);
      _M0L2geS5056 = _M0L1pS1608->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5055 = _M0MPC15array5Array2atGfE(_M0L2geS5056, _M0L1iS1619);
      _M0L6_2atmpS5054 = -_M0L6_2atmpS5055;
      _M0L6_2atmpS5053 = _M0L2dtS1620 * _M0L6_2atmpS5054;
      _M0L6_2atmpS5052 = _M0L6_2atmpS5053 / _M0L6tau__eS1614;
      _M0L6_2atmpS5050 = _M0L6_2atmpS5051 + _M0L6_2atmpS5052;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS5049, _M0L1iS1619, _M0L6_2atmpS5050);
      _M0L2giS5058 = _M0L1pS1608->$7;
      _M0L2giS5066 = _M0L1pS1608->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5060 = _M0MPC15array5Array2atGfE(_M0L2giS5066, _M0L1iS1619);
      _M0L2giS5065 = _M0L1pS1608->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5064 = _M0MPC15array5Array2atGfE(_M0L2giS5065, _M0L1iS1619);
      _M0L6_2atmpS5063 = -_M0L6_2atmpS5064;
      _M0L6_2atmpS5062 = _M0L2dtS1620 * _M0L6_2atmpS5063;
      _M0L6_2atmpS5061 = _M0L6_2atmpS5062 / _M0L6tau__iS1615;
      _M0L6_2atmpS5059 = _M0L6_2atmpS5060 + _M0L6_2atmpS5061;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS5058, _M0L1iS1619, _M0L6_2atmpS5059);
      _M0L6_2atmpS5067 = _M0L1iS1619 + 1;
      _M0L1iS1619 = _M0L6_2atmpS5067;
      continue;
    }
    break;
  }
  _M0L7_2abindS1622 = 0;
  _M0L1iS1623 = _M0L7_2abindS1622;
  while (1) {
    if (_M0L1iS1623 < _M0L1nS1607) {
      struct _M0TPB5ArrayGfE* _M0L1vS5093 = _M0L1pS1608->$2;
      float _M0L1vS1624;
      struct _M0TPB5ArrayGfE* _M0L1uS5092;
      float _M0L1uS1625;
      struct _M0TPB5ArrayGfE* _M0L1iS5091;
      float _M0L2iiS1626;
      struct _M0TPB5ArrayGfE* _M0L1vS5068;
      float _M0L6_2atmpS5071;
      float _M0L6_2atmpS5078;
      float _M0L6_2atmpS5076;
      float _M0L6_2atmpS5077;
      float _M0L6_2atmpS5075;
      float _M0L6_2atmpS5074;
      float _M0L6_2atmpS5073;
      float _M0L6_2atmpS5072;
      float _M0L6_2atmpS5070;
      float _M0L6_2atmpS5069;
      struct _M0TPB5ArrayGfE* _M0L1vS5090;
      float _M0L2v2S1627;
      struct _M0TPB5ArrayGfE* _M0L1vS5079;
      float _M0L6_2atmpS5082;
      float _M0L6_2atmpS5089;
      float _M0L6_2atmpS5087;
      float _M0L6_2atmpS5088;
      float _M0L6_2atmpS5086;
      float _M0L6_2atmpS5085;
      float _M0L6_2atmpS5084;
      float _M0L6_2atmpS5083;
      float _M0L6_2atmpS5081;
      float _M0L6_2atmpS5080;
      int32_t _M0L6_2atmpS5094;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1624 = _M0MPC15array5Array2atGfE(_M0L1vS5093, _M0L1iS1623);
      _M0L1uS5092 = _M0L1pS1608->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS1625 = _M0MPC15array5Array2atGfE(_M0L1uS5092, _M0L1iS1623);
      _M0L1iS5091 = _M0L1pS1608->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS1626 = _M0MPC15array5Array2atGfE(_M0L1iS5091, _M0L1iS1623);
      _M0L1vS5068 = _M0L1pS1608->$2;
      _M0L6_2atmpS5071 = 0x1p-1f * _M0L2dtS1620;
      _M0L6_2atmpS5078 = 0x1.47ae147ae147bp-5f * _M0L1vS1624;
      _M0L6_2atmpS5076 = _M0L6_2atmpS5078 * _M0L1vS1624;
      _M0L6_2atmpS5077 = 0x1.4p+2f * _M0L1vS1624;
      _M0L6_2atmpS5075 = _M0L6_2atmpS5076 + _M0L6_2atmpS5077;
      _M0L6_2atmpS5074 = _M0L6_2atmpS5075 + 0x1.18p+7f;
      _M0L6_2atmpS5073 = _M0L6_2atmpS5074 - _M0L1uS1625;
      _M0L6_2atmpS5072 = _M0L6_2atmpS5073 + _M0L2iiS1626;
      _M0L6_2atmpS5070 = _M0L6_2atmpS5071 * _M0L6_2atmpS5072;
      _M0L6_2atmpS5069 = _M0L1vS1624 + _M0L6_2atmpS5070;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5068, _M0L1iS1623, _M0L6_2atmpS5069);
      _M0L1vS5090 = _M0L1pS1608->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S1627 = _M0MPC15array5Array2atGfE(_M0L1vS5090, _M0L1iS1623);
      _M0L1vS5079 = _M0L1pS1608->$2;
      _M0L6_2atmpS5082 = 0x1p-1f * _M0L2dtS1620;
      _M0L6_2atmpS5089 = 0x1.47ae147ae147bp-5f * _M0L2v2S1627;
      _M0L6_2atmpS5087 = _M0L6_2atmpS5089 * _M0L2v2S1627;
      _M0L6_2atmpS5088 = 0x1.4p+2f * _M0L2v2S1627;
      _M0L6_2atmpS5086 = _M0L6_2atmpS5087 + _M0L6_2atmpS5088;
      _M0L6_2atmpS5085 = _M0L6_2atmpS5086 + 0x1.18p+7f;
      _M0L6_2atmpS5084 = _M0L6_2atmpS5085 - _M0L1uS1625;
      _M0L6_2atmpS5083 = _M0L6_2atmpS5084 + _M0L2iiS1626;
      _M0L6_2atmpS5081 = _M0L6_2atmpS5082 * _M0L6_2atmpS5083;
      _M0L6_2atmpS5080 = _M0L2v2S1627 + _M0L6_2atmpS5081;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5079, _M0L1iS1623, _M0L6_2atmpS5080);
      _M0L6_2atmpS5094 = _M0L1iS1623 + 1;
      _M0L1iS1623 = _M0L6_2atmpS5094;
      continue;
    }
    break;
  }
  _M0L7_2abindS1629 = 0;
  _M0L1iS1630 = _M0L7_2abindS1629;
  while (1) {
    if (_M0L1iS1630 < _M0L1nS1607) {
      struct _M0TPB5ArrayGfE* _M0L1vS5105 = _M0L1pS1608->$2;
      float _M0L1vS1631;
      struct _M0TPB5ArrayGfE* _M0L1uS5095;
      struct _M0TPB5ArrayGfE* _M0L1uS5104;
      float _M0L6_2atmpS5097;
      float _M0L6_2atmpS5099;
      float _M0L6_2atmpS5101;
      struct _M0TPB5ArrayGfE* _M0L1uS5103;
      float _M0L6_2atmpS5102;
      float _M0L6_2atmpS5100;
      float _M0L6_2atmpS5098;
      float _M0L6_2atmpS5096;
      int32_t _M0L6_2atmpS5106;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1631 = _M0MPC15array5Array2atGfE(_M0L1vS5105, _M0L1iS1630);
      _M0L1uS5095 = _M0L1pS1608->$3;
      _M0L1uS5104 = _M0L1pS1608->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5097 = _M0MPC15array5Array2atGfE(_M0L1uS5104, _M0L1iS1630);
      _M0L6_2atmpS5099 = _M0L2dtS1620 * _M0L1aS1610;
      _M0L6_2atmpS5101 = _M0L1bS1611 * _M0L1vS1631;
      _M0L1uS5103 = _M0L1pS1608->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5102 = _M0MPC15array5Array2atGfE(_M0L1uS5103, _M0L1iS1630);
      _M0L6_2atmpS5100 = _M0L6_2atmpS5101 - _M0L6_2atmpS5102;
      _M0L6_2atmpS5098 = _M0L6_2atmpS5099 * _M0L6_2atmpS5100;
      _M0L6_2atmpS5096 = _M0L6_2atmpS5097 + _M0L6_2atmpS5098;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS5095, _M0L1iS1630, _M0L6_2atmpS5096);
      _M0L6_2atmpS5106 = _M0L1iS1630 + 1;
      _M0L1iS1630 = _M0L6_2atmpS5106;
      continue;
    }
    break;
  }
  _M0L7_2abindS1633 = 0;
  _M0L1iS1634 = _M0L7_2abindS1633;
  while (1) {
    if (_M0L1iS1634 < _M0L1nS1607) {
      struct _M0TPB5ArrayGfE* _M0L1vS5107 = _M0L1pS1608->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5124 = _M0L1pS1608->$2;
      float _M0L6_2atmpS5109;
      struct _M0TPB5ArrayGfE* _M0L2geS5123;
      float _M0L6_2atmpS5119;
      struct _M0TPB5ArrayGfE* _M0L1vS5122;
      float _M0L6_2atmpS5121;
      float _M0L6_2atmpS5120;
      float _M0L6_2atmpS5112;
      struct _M0TPB5ArrayGfE* _M0L2giS5118;
      float _M0L6_2atmpS5114;
      struct _M0TPB5ArrayGfE* _M0L1vS5117;
      float _M0L6_2atmpS5116;
      float _M0L6_2atmpS5115;
      float _M0L6_2atmpS5113;
      float _M0L6_2atmpS5111;
      float _M0L6_2atmpS5110;
      float _M0L6_2atmpS5108;
      int32_t _M0L6_2atmpS5125;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5109 = _M0MPC15array5Array2atGfE(_M0L1vS5124, _M0L1iS1634);
      _M0L2geS5123 = _M0L1pS1608->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5119 = _M0MPC15array5Array2atGfE(_M0L2geS5123, _M0L1iS1634);
      _M0L1vS5122 = _M0L1pS1608->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5121 = _M0MPC15array5Array2atGfE(_M0L1vS5122, _M0L1iS1634);
      _M0L6_2atmpS5120 = _M0L4e__eS1616 - _M0L6_2atmpS5121;
      _M0L6_2atmpS5112 = _M0L6_2atmpS5119 * _M0L6_2atmpS5120;
      _M0L2giS5118 = _M0L1pS1608->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5114 = _M0MPC15array5Array2atGfE(_M0L2giS5118, _M0L1iS1634);
      _M0L1vS5117 = _M0L1pS1608->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5116 = _M0MPC15array5Array2atGfE(_M0L1vS5117, _M0L1iS1634);
      _M0L6_2atmpS5115 = _M0L4e__iS1617 - _M0L6_2atmpS5116;
      _M0L6_2atmpS5113 = _M0L6_2atmpS5114 * _M0L6_2atmpS5115;
      _M0L6_2atmpS5111 = _M0L6_2atmpS5112 + _M0L6_2atmpS5113;
      _M0L6_2atmpS5110 = _M0L2dtS1620 * _M0L6_2atmpS5111;
      _M0L6_2atmpS5108 = _M0L6_2atmpS5109 + _M0L6_2atmpS5110;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5107, _M0L1iS1634, _M0L6_2atmpS5108);
      _M0L6_2atmpS5125 = _M0L1iS1634 + 1;
      _M0L1iS1634 = _M0L6_2atmpS5125;
      continue;
    }
    break;
  }
  _M0L7_2abindS1636 = 0;
  _M0L1iS1637 = _M0L7_2abindS1636;
  while (1) {
    if (_M0L1iS1637 < _M0L1nS1607) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5126 = _M0L1pS1608->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5129 = _M0L1pS1608->$2;
      float _M0L6_2atmpS5128;
      int32_t _M0L6_2atmpS5127;
      struct _M0TPB5ArrayGfE* _M0L1vS5130;
      struct _M0TPB5ArrayGbE* _M0L4fireS5132;
      float _M0L6_2atmpS5131;
      struct _M0TPB5ArrayGfE* _M0L1uS5134;
      struct _M0TPB5ArrayGfE* _M0L1uS5139;
      float _M0L6_2atmpS5136;
      struct _M0TPB5ArrayGbE* _M0L4fireS5138;
      float _M0L6_2atmpS5137;
      float _M0L6_2atmpS5135;
      int32_t _M0L6_2atmpS5140;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5128 = _M0MPC15array5Array2atGfE(_M0L1vS5129, _M0L1iS1637);
      _M0L6_2atmpS5127 = _M0L6_2atmpS5128 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5126, _M0L1iS1637, _M0L6_2atmpS5127);
      _M0L1vS5130 = _M0L1pS1608->$2;
      _M0L4fireS5132 = _M0L1pS1608->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS5132, _M0L1iS1637)) {
        _M0L6_2atmpS5131 = _M0L1cS1612;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS5133 = _M0L1pS1608->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS5131
        = _M0MPC15array5Array2atGfE(_M0L1vS5133, _M0L1iS1637);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5130, _M0L1iS1637, _M0L6_2atmpS5131);
      _M0L1uS5134 = _M0L1pS1608->$3;
      _M0L1uS5139 = _M0L1pS1608->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5136 = _M0MPC15array5Array2atGfE(_M0L1uS5139, _M0L1iS1637);
      _M0L4fireS5138 = _M0L1pS1608->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS5138, _M0L1iS1637)) {
        _M0L6_2atmpS5137 = _M0L1dS1613;
      } else {
        _M0L6_2atmpS5137 = 0x0p+0f;
      }
      _M0L6_2atmpS5135 = _M0L6_2atmpS5136 + _M0L6_2atmpS5137;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS5134, _M0L1iS1637, _M0L6_2atmpS5135);
      _M0L6_2atmpS5140 = _M0L1iS1637 + 1;
      _M0L1iS1637 = _M0L6_2atmpS5140;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS1564,
  float _M0L2dtS1590
) {
  int32_t _M0L1nS1563;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S1565;
  float _M0L2cmS1566;
  float _M0L2glS1567;
  float _M0L2elS1568;
  float _M0L2ekS1569;
  float _M0L2enS1570;
  float _M0L2gnS1571;
  float _M0L2gkS1572;
  float _M0L2vtS1573;
  float _M0L6tau__eS1574;
  float _M0L6tau__iS1575;
  float _M0L4e__eS1576;
  float _M0L4e__iS1577;
  int32_t _M0L7_2abindS1578;
  int32_t _M0L1iS1579;
  int32_t _M0L7_2abindS1604;
  int32_t _M0L1iS1605;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS1563 = _M0L1pS1564->$1;
  _M0L3p__S1565 = _M0L1pS1564->$0;
  _M0L2cmS1566 = _M0L3p__S1565->$0;
  _M0L2glS1567 = _M0L3p__S1565->$1;
  _M0L2elS1568 = _M0L3p__S1565->$2;
  _M0L2ekS1569 = _M0L3p__S1565->$3;
  _M0L2enS1570 = _M0L3p__S1565->$4;
  _M0L2gnS1571 = _M0L3p__S1565->$5;
  _M0L2gkS1572 = _M0L3p__S1565->$6;
  _M0L2vtS1573 = _M0L3p__S1565->$7;
  _M0L6tau__eS1574 = _M0L3p__S1565->$8;
  _M0L6tau__iS1575 = _M0L3p__S1565->$9;
  _M0L4e__eS1576 = _M0L3p__S1565->$10;
  _M0L4e__iS1577 = _M0L3p__S1565->$11;
  _M0L7_2abindS1578 = 0;
  _M0L1iS1579 = _M0L7_2abindS1578;
  while (1) {
    if (_M0L1iS1579 < _M0L1nS1563) {
      struct _M0TPB5ArrayGfE* _M0L1vS5042 = _M0L1pS1564->$2;
      float _M0L1vS1580;
      struct _M0TPB5ArrayGfE* _M0L1mS5041;
      float _M0L1mS1581;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS5040;
      float _M0L2nnS1582;
      struct _M0TPB5ArrayGfE* _M0L1hS5039;
      float _M0L1hS1583;
      struct _M0TPB5ArrayGfE* _M0L2geS5038;
      float _M0L2geS1584;
      struct _M0TPB5ArrayGfE* _M0L2giS5037;
      float _M0L2giS1585;
      struct _M0TPB5ArrayGbE* _M0L4fireS4940;
      float _M0L6_2atmpS5036;
      float _M0L7am__numS1586;
      float _M0L6_2atmpS5035;
      float _M0L7bm__numS1587;
      float _M0L6_2atmpS5030;
      float _M0L6_2atmpS5029;
      float _M0L6_2atmpS5028;
      float _M0L2amS1588;
      float _M0L6_2atmpS5023;
      float _M0L6_2atmpS5022;
      float _M0L6_2atmpS5021;
      float _M0L2bmS1589;
      struct _M0TPB5ArrayGfE* _M0L1mS4941;
      float _M0L6_2atmpS4947;
      float _M0L6_2atmpS4945;
      float _M0L6_2atmpS4946;
      float _M0L6_2atmpS4944;
      float _M0L6_2atmpS4943;
      float _M0L6_2atmpS4942;
      float _M0L6_2atmpS5020;
      float _M0L7an__numS1591;
      float _M0L6_2atmpS5015;
      float _M0L6_2atmpS5014;
      float _M0L6_2atmpS5013;
      float _M0L2anS1592;
      float _M0L6_2atmpS5012;
      float _M0L6_2atmpS5011;
      float _M0L6_2atmpS5010;
      float _M0L6_2atmpS5009;
      float _M0L2bnS1593;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4948;
      float _M0L6_2atmpS4954;
      float _M0L6_2atmpS4952;
      float _M0L6_2atmpS4953;
      float _M0L6_2atmpS4951;
      float _M0L6_2atmpS4950;
      float _M0L6_2atmpS4949;
      float _M0L6_2atmpS5008;
      float _M0L6_2atmpS5007;
      float _M0L6_2atmpS5006;
      float _M0L6_2atmpS5005;
      float _M0L2ahS1594;
      float _M0L6_2atmpS5004;
      float _M0L6_2atmpS5003;
      float _M0L6_2atmpS5002;
      float _M0L6_2atmpS5001;
      float _M0L9bh__denomS1595;
      float _M0L2bhS1596;
      struct _M0TPB5ArrayGfE* _M0L1hS4955;
      float _M0L6_2atmpS4961;
      float _M0L6_2atmpS4959;
      float _M0L6_2atmpS4960;
      float _M0L6_2atmpS4958;
      float _M0L6_2atmpS4957;
      float _M0L6_2atmpS4956;
      struct _M0TPB5ArrayGfE* _M0L1mS5000;
      float _M0L6m__newS1597;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4999;
      float _M0L6n__newS1598;
      struct _M0TPB5ArrayGfE* _M0L1hS4998;
      float _M0L6h__newS1599;
      float _M0L6_2atmpS4997;
      float _M0L6_2atmpS4996;
      float _M0L3m3hS1600;
      float _M0L6_2atmpS4995;
      float _M0L6_2atmpS4994;
      float _M0L2n4S1601;
      struct _M0TPB5ArrayGfE* _M0L1iS4993;
      float _M0L6_2atmpS4990;
      float _M0L6_2atmpS4992;
      float _M0L6_2atmpS4991;
      float _M0L6_2atmpS4987;
      float _M0L6_2atmpS4989;
      float _M0L6_2atmpS4988;
      float _M0L6_2atmpS4984;
      float _M0L6_2atmpS4986;
      float _M0L6_2atmpS4985;
      float _M0L6_2atmpS4980;
      float _M0L6_2atmpS4982;
      float _M0L6_2atmpS4983;
      float _M0L6_2atmpS4981;
      float _M0L6_2atmpS4976;
      float _M0L6_2atmpS4978;
      float _M0L6_2atmpS4979;
      float _M0L6_2atmpS4977;
      float _M0L7currentS1602;
      struct _M0TPB5ArrayGfE* _M0L1vS4962;
      float _M0L6_2atmpS4965;
      float _M0L6_2atmpS4964;
      float _M0L6_2atmpS4963;
      struct _M0TPB5ArrayGfE* _M0L2geS4966;
      float _M0L6_2atmpS4970;
      float _M0L6_2atmpS4969;
      float _M0L6_2atmpS4968;
      float _M0L6_2atmpS4967;
      struct _M0TPB5ArrayGfE* _M0L2giS4971;
      float _M0L6_2atmpS4975;
      float _M0L6_2atmpS4974;
      float _M0L6_2atmpS4973;
      float _M0L6_2atmpS4972;
      int32_t _M0L6_2atmpS5043;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS1580 = _M0MPC15array5Array2atGfE(_M0L1vS5042, _M0L1iS1579);
      _M0L1mS5041 = _M0L1pS1564->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS1581 = _M0MPC15array5Array2atGfE(_M0L1mS5041, _M0L1iS1579);
      _M0L7n__gateS5040 = _M0L1pS1564->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1582
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS5040, _M0L1iS1579);
      _M0L1hS5039 = _M0L1pS1564->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1583 = _M0MPC15array5Array2atGfE(_M0L1hS5039, _M0L1iS1579);
      _M0L2geS5038 = _M0L1pS1564->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1584 = _M0MPC15array5Array2atGfE(_M0L2geS5038, _M0L1iS1579);
      _M0L2giS5037 = _M0L1pS1564->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1585 = _M0MPC15array5Array2atGfE(_M0L2giS5037, _M0L1iS1579);
      _M0L4fireS4940 = _M0L1pS1564->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4940, _M0L1iS1579, 0);
      _M0L6_2atmpS5036 = 0x1.ap+3f - _M0L1vS1580;
      _M0L7am__numS1586 = _M0L6_2atmpS5036 + _M0L2vtS1573;
      _M0L6_2atmpS5035 = _M0L1vS1580 - _M0L2vtS1573;
      _M0L7bm__numS1587 = _M0L6_2atmpS5035 - 0x1.4p+5f;
      _M0L6_2atmpS5030 = _M0L7am__numS1586 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5029 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5030);
      _M0L6_2atmpS5028 = _M0L6_2atmpS5029 - 0x1p+0f;
      if (_M0L6_2atmpS5028 != 0x0p+0f) {
        float _M0L6_2atmpS5031 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1586;
        float _M0L6_2atmpS5034 = _M0L7am__numS1586 / 0x1p+2f;
        float _M0L6_2atmpS5033;
        float _M0L6_2atmpS5032;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS5033 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5034);
        _M0L6_2atmpS5032 = _M0L6_2atmpS5033 - 0x1p+0f;
        _M0L2amS1588 = _M0L6_2atmpS5031 / _M0L6_2atmpS5032;
      } else {
        _M0L2amS1588 = 0x0p+0f;
      }
      _M0L6_2atmpS5023 = _M0L7bm__numS1587 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5022 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5023);
      _M0L6_2atmpS5021 = _M0L6_2atmpS5022 - 0x1p+0f;
      if (_M0L6_2atmpS5021 != 0x0p+0f) {
        float _M0L6_2atmpS5024 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1587;
        float _M0L6_2atmpS5027 = _M0L7bm__numS1587 / 0x1.4p+2f;
        float _M0L6_2atmpS5026;
        float _M0L6_2atmpS5025;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS5026 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5027);
        _M0L6_2atmpS5025 = _M0L6_2atmpS5026 - 0x1p+0f;
        _M0L2bmS1589 = _M0L6_2atmpS5024 / _M0L6_2atmpS5025;
      } else {
        _M0L2bmS1589 = 0x0p+0f;
      }
      _M0L1mS4941 = _M0L1pS1564->$3;
      _M0L6_2atmpS4947 = 0x1p+0f - _M0L1mS1581;
      _M0L6_2atmpS4945 = _M0L2amS1588 * _M0L6_2atmpS4947;
      _M0L6_2atmpS4946 = _M0L2bmS1589 * _M0L1mS1581;
      _M0L6_2atmpS4944 = _M0L6_2atmpS4945 - _M0L6_2atmpS4946;
      _M0L6_2atmpS4943 = _M0L2dtS1590 * _M0L6_2atmpS4944;
      _M0L6_2atmpS4942 = _M0L1mS1581 + _M0L6_2atmpS4943;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS4941, _M0L1iS1579, _M0L6_2atmpS4942);
      _M0L6_2atmpS5020 = 0x1.ep+3f - _M0L1vS1580;
      _M0L7an__numS1591 = _M0L6_2atmpS5020 + _M0L2vtS1573;
      _M0L6_2atmpS5015 = _M0L7an__numS1591 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5014 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5015);
      _M0L6_2atmpS5013 = _M0L6_2atmpS5014 - 0x1p+0f;
      if (_M0L6_2atmpS5013 != 0x0p+0f) {
        float _M0L6_2atmpS5016 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1591;
        float _M0L6_2atmpS5019 = _M0L7an__numS1591 / 0x1.4p+2f;
        float _M0L6_2atmpS5018;
        float _M0L6_2atmpS5017;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS5018 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5019);
        _M0L6_2atmpS5017 = _M0L6_2atmpS5018 - 0x1p+0f;
        _M0L2anS1592 = _M0L6_2atmpS5016 / _M0L6_2atmpS5017;
      } else {
        _M0L2anS1592 = 0x0p+0f;
      }
      _M0L6_2atmpS5012 = 0x1.4p+3f - _M0L1vS1580;
      _M0L6_2atmpS5011 = _M0L6_2atmpS5012 + _M0L2vtS1573;
      _M0L6_2atmpS5010 = _M0L6_2atmpS5011 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5009 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5010);
      _M0L2bnS1593 = 0x1p-1f * _M0L6_2atmpS5009;
      _M0L7n__gateS4948 = _M0L1pS1564->$4;
      _M0L6_2atmpS4954 = 0x1p+0f - _M0L2nnS1582;
      _M0L6_2atmpS4952 = _M0L2anS1592 * _M0L6_2atmpS4954;
      _M0L6_2atmpS4953 = _M0L2bnS1593 * _M0L2nnS1582;
      _M0L6_2atmpS4951 = _M0L6_2atmpS4952 - _M0L6_2atmpS4953;
      _M0L6_2atmpS4950 = _M0L2dtS1590 * _M0L6_2atmpS4951;
      _M0L6_2atmpS4949 = _M0L2nnS1582 + _M0L6_2atmpS4950;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS4948, _M0L1iS1579, _M0L6_2atmpS4949);
      _M0L6_2atmpS5008 = 0x1.1p+4f - _M0L1vS1580;
      _M0L6_2atmpS5007 = _M0L6_2atmpS5008 + _M0L2vtS1573;
      _M0L6_2atmpS5006 = _M0L6_2atmpS5007 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5005 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5006);
      _M0L2ahS1594 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS5005;
      _M0L6_2atmpS5004 = 0x1.4p+5f - _M0L1vS1580;
      _M0L6_2atmpS5003 = _M0L6_2atmpS5004 + _M0L2vtS1573;
      _M0L6_2atmpS5002 = _M0L6_2atmpS5003 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5001 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5002);
      _M0L9bh__denomS1595 = 0x1p+0f + _M0L6_2atmpS5001;
      if (_M0L9bh__denomS1595 != 0x0p+0f) {
        _M0L2bhS1596 = 0x1p+2f / _M0L9bh__denomS1595;
      } else {
        _M0L2bhS1596 = 0x0p+0f;
      }
      _M0L1hS4955 = _M0L1pS1564->$5;
      _M0L6_2atmpS4961 = 0x1p+0f - _M0L1hS1583;
      _M0L6_2atmpS4959 = _M0L2ahS1594 * _M0L6_2atmpS4961;
      _M0L6_2atmpS4960 = _M0L2bhS1596 * _M0L1hS1583;
      _M0L6_2atmpS4958 = _M0L6_2atmpS4959 - _M0L6_2atmpS4960;
      _M0L6_2atmpS4957 = _M0L2dtS1590 * _M0L6_2atmpS4958;
      _M0L6_2atmpS4956 = _M0L1hS1583 + _M0L6_2atmpS4957;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS4955, _M0L1iS1579, _M0L6_2atmpS4956);
      _M0L1mS5000 = _M0L1pS1564->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1597 = _M0MPC15array5Array2atGfE(_M0L1mS5000, _M0L1iS1579);
      _M0L7n__gateS4999 = _M0L1pS1564->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1598
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4999, _M0L1iS1579);
      _M0L1hS4998 = _M0L1pS1564->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1599 = _M0MPC15array5Array2atGfE(_M0L1hS4998, _M0L1iS1579);
      _M0L6_2atmpS4997 = _M0L6m__newS1597 * _M0L6m__newS1597;
      _M0L6_2atmpS4996 = _M0L6_2atmpS4997 * _M0L6m__newS1597;
      _M0L3m3hS1600 = _M0L6_2atmpS4996 * _M0L6h__newS1599;
      _M0L6_2atmpS4995 = _M0L6n__newS1598 * _M0L6n__newS1598;
      _M0L6_2atmpS4994 = _M0L6_2atmpS4995 * _M0L6n__newS1598;
      _M0L2n4S1601 = _M0L6_2atmpS4994 * _M0L6n__newS1598;
      _M0L1iS4993 = _M0L1pS1564->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4990 = _M0MPC15array5Array2atGfE(_M0L1iS4993, _M0L1iS1579);
      _M0L6_2atmpS4992 = _M0L2elS1568 - _M0L1vS1580;
      _M0L6_2atmpS4991 = _M0L2glS1567 * _M0L6_2atmpS4992;
      _M0L6_2atmpS4987 = _M0L6_2atmpS4990 + _M0L6_2atmpS4991;
      _M0L6_2atmpS4989 = _M0L4e__eS1576 - _M0L1vS1580;
      _M0L6_2atmpS4988 = _M0L2geS1584 * _M0L6_2atmpS4989;
      _M0L6_2atmpS4984 = _M0L6_2atmpS4987 + _M0L6_2atmpS4988;
      _M0L6_2atmpS4986 = _M0L4e__iS1577 - _M0L1vS1580;
      _M0L6_2atmpS4985 = _M0L2giS1585 * _M0L6_2atmpS4986;
      _M0L6_2atmpS4980 = _M0L6_2atmpS4984 + _M0L6_2atmpS4985;
      _M0L6_2atmpS4982 = _M0L2gnS1571 * _M0L3m3hS1600;
      _M0L6_2atmpS4983 = _M0L2enS1570 - _M0L1vS1580;
      _M0L6_2atmpS4981 = _M0L6_2atmpS4982 * _M0L6_2atmpS4983;
      _M0L6_2atmpS4976 = _M0L6_2atmpS4980 + _M0L6_2atmpS4981;
      _M0L6_2atmpS4978 = _M0L2gkS1572 * _M0L2n4S1601;
      _M0L6_2atmpS4979 = _M0L2ekS1569 - _M0L1vS1580;
      _M0L6_2atmpS4977 = _M0L6_2atmpS4978 * _M0L6_2atmpS4979;
      _M0L7currentS1602 = _M0L6_2atmpS4976 + _M0L6_2atmpS4977;
      _M0L1vS4962 = _M0L1pS1564->$2;
      _M0L6_2atmpS4965 = _M0L2dtS1590 / _M0L2cmS1566;
      _M0L6_2atmpS4964 = _M0L6_2atmpS4965 * _M0L7currentS1602;
      _M0L6_2atmpS4963 = _M0L1vS1580 + _M0L6_2atmpS4964;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4962, _M0L1iS1579, _M0L6_2atmpS4963);
      _M0L2geS4966 = _M0L1pS1564->$8;
      _M0L6_2atmpS4970 = -_M0L2geS1584;
      _M0L6_2atmpS4969 = _M0L6_2atmpS4970 / _M0L6tau__eS1574;
      _M0L6_2atmpS4968 = _M0L2dtS1590 * _M0L6_2atmpS4969;
      _M0L6_2atmpS4967 = _M0L2geS1584 + _M0L6_2atmpS4968;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4966, _M0L1iS1579, _M0L6_2atmpS4967);
      _M0L2giS4971 = _M0L1pS1564->$9;
      _M0L6_2atmpS4975 = -_M0L2giS1585;
      _M0L6_2atmpS4974 = _M0L6_2atmpS4975 / _M0L6tau__iS1575;
      _M0L6_2atmpS4973 = _M0L2dtS1590 * _M0L6_2atmpS4974;
      _M0L6_2atmpS4972 = _M0L2giS1585 + _M0L6_2atmpS4973;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4971, _M0L1iS1579, _M0L6_2atmpS4972);
      _M0L6_2atmpS5043 = _M0L1iS1579 + 1;
      _M0L1iS1579 = _M0L6_2atmpS5043;
      continue;
    }
    break;
  }
  _M0L7_2abindS1604 = 0;
  _M0L1iS1605 = _M0L7_2abindS1604;
  while (1) {
    if (_M0L1iS1605 < _M0L1nS1563) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5044 = _M0L1pS1564->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS5047 = _M0L1pS1564->$2;
      float _M0L6_2atmpS5046;
      int32_t _M0L6_2atmpS5045;
      int32_t _M0L6_2atmpS5048;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5046 = _M0MPC15array5Array2atGfE(_M0L1vS5047, _M0L1iS1605);
      _M0L6_2atmpS5045 = _M0L6_2atmpS5046 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5044, _M0L1iS1605, _M0L6_2atmpS5045);
      _M0L6_2atmpS5048 = _M0L1iS1605 + 1;
      _M0L1iS1605 = _M0L6_2atmpS5048;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS1534,
  float _M0L2dtS1542
) {
  int32_t _M0L1nS1533;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4939;
  int32_t _M0L2ndS1535;
  int32_t _M0L8total__dS1536;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4938;
  float _M0L9steepnessS1537;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4937;
  float _M0L6tau__mS1538;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4936;
  float _M0L9tau__rateS1539;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4935;
  float _M0L8tau__absS1540;
  float _M0L6_2atmpS4934;
  int32_t _M0L11tabs__stepsS1541;
  int32_t _M0L7_2abindS1543;
  int32_t _M0L1iS1544;
  int32_t _M0L7_2abindS1547;
  int32_t _M0L1iS1548;
  int32_t _M0L7_2abindS1557;
  int32_t _M0L1iS1558;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS1533 = _M0L1pS1534->$1;
  _M0L5paramS4939 = _M0L1pS1534->$0;
  _M0L2ndS1535 = _M0L5paramS4939->$0;
  _M0L8total__dS1536 = _M0L1nS1533 * _M0L2ndS1535;
  _M0L5paramS4938 = _M0L1pS1534->$0;
  _M0L9steepnessS1537 = _M0L5paramS4938->$7;
  _M0L5paramS4937 = _M0L1pS1534->$0;
  _M0L6tau__mS1538 = _M0L5paramS4937->$8;
  _M0L5paramS4936 = _M0L1pS1534->$0;
  _M0L9tau__rateS1539 = _M0L5paramS4936->$9;
  _M0L5paramS4935 = _M0L1pS1534->$0;
  _M0L8tau__absS1540 = _M0L5paramS4935->$6;
  _M0L6_2atmpS4934 = _M0L8tau__absS1540 / _M0L2dtS1542;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS1541 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4934);
  _M0L7_2abindS1543 = 0;
  _M0L1iS1544 = _M0L7_2abindS1543;
  while (1) {
    if (_M0L1iS1544 < _M0L8total__dS1536) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS4862 = _M0L1pS1534->$6;
      float _M0L7tau__diS1545;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4850;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4861;
      float _M0L6_2atmpS4852;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4860;
      float _M0L6_2atmpS4859;
      float _M0L6_2atmpS4856;
      struct _M0TPB5ArrayGfE* _M0L4is__S4858;
      float _M0L6_2atmpS4857;
      float _M0L6_2atmpS4855;
      float _M0L6_2atmpS4854;
      float _M0L6_2atmpS4853;
      float _M0L6_2atmpS4851;
      int32_t _M0L6_2atmpS4863;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS1545
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS4862, _M0L1iS1544);
      _M0L4v__dS4850 = _M0L1pS1534->$2;
      _M0L4v__dS4861 = _M0L1pS1534->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4852
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4861, _M0L1iS1544);
      _M0L4v__dS4860 = _M0L1pS1534->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4859
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4860, _M0L1iS1544);
      _M0L6_2atmpS4856 = -_M0L6_2atmpS4859;
      _M0L4is__S4858 = _M0L1pS1534->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4857
      = _M0MPC15array5Array2atGfE(_M0L4is__S4858, _M0L1iS1544);
      _M0L6_2atmpS4855 = _M0L6_2atmpS4856 - _M0L6_2atmpS4857;
      _M0L6_2atmpS4854 = _M0L2dtS1542 * _M0L6_2atmpS4855;
      _M0L6_2atmpS4853 = _M0L6_2atmpS4854 / _M0L7tau__diS1545;
      _M0L6_2atmpS4851 = _M0L6_2atmpS4852 + _M0L6_2atmpS4853;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS4850, _M0L1iS1544, _M0L6_2atmpS4851);
      _M0L6_2atmpS4863 = _M0L1iS1544 + 1;
      _M0L1iS1544 = _M0L6_2atmpS4863;
      continue;
    }
    break;
  }
  _M0L7_2abindS1547 = 0;
  _M0L1iS1548 = _M0L7_2abindS1547;
  while (1) {
    if (_M0L1iS1548 < _M0L1nS1533) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4884 = _M0L1pS1534->$11;
      int32_t _M0L5startS1549;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4882;
      int32_t _M0L6_2atmpS4883;
      int32_t _M0L3endS1550;
      float _M0L16dt__over__tau__mS1551;
      struct _M0TPB8MutLocalGiE* _M0L1sS1552;
      int32_t _M0L6_2atmpS4885;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS1549
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4884, _M0L1iS1548);
      _M0L6colptrS4882 = _M0L1pS1534->$11;
      _M0L6_2atmpS4883 = _M0L1iS1548 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS1550
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4882, _M0L6_2atmpS4883);
      _M0L16dt__over__tau__mS1551 = _M0L2dtS1542 / _M0L6tau__mS1538;
      _M0L1sS1552
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1552)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1552->$0 = _M0L5startS1549;
      while (1) {
        int32_t _M0L3valS4864 = _M0L1sS1552->$0;
        if (_M0L3valS4864 < _M0L3endS1550) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS4880 = _M0L1pS1534->$12;
          int32_t _M0L3valS4881 = _M0L1sS1552->$0;
          int32_t _M0L9dend__idxS1553;
          struct _M0TPB5ArrayGfE* _M0L6w__synS4878;
          int32_t _M0L3valS4879;
          float _M0L1wS1554;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4865;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4875;
          float _M0L6_2atmpS4867;
          struct _M0TPB5ArrayGfE* _M0L4v__dS4874;
          float _M0L6_2atmpS4873;
          float _M0L6_2atmpS4870;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4872;
          float _M0L6_2atmpS4871;
          float _M0L6_2atmpS4869;
          float _M0L6_2atmpS4868;
          float _M0L6_2atmpS4866;
          int32_t _M0L3valS4877;
          int32_t _M0L6_2atmpS4876;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS1553
          = _M0MPC15array5Array2atGiE(_M0L6i__synS4880, _M0L3valS4881);
          _M0L6w__synS4878 = _M0L1pS1534->$13;
          _M0L3valS4879 = _M0L1sS1552->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS1554
          = _M0MPC15array5Array2atGfE(_M0L6w__synS4878, _M0L3valS4879);
          _M0L4v__sS4865 = _M0L1pS1534->$3;
          _M0L4v__sS4875 = _M0L1pS1534->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4867
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4875, _M0L1iS1548);
          _M0L4v__dS4874 = _M0L1pS1534->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4873
          = _M0MPC15array5Array2atGfE(_M0L4v__dS4874, _M0L9dend__idxS1553);
          _M0L6_2atmpS4870 = _M0L1wS1554 * _M0L6_2atmpS4873;
          _M0L4v__sS4872 = _M0L1pS1534->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4871
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4872, _M0L1iS1548);
          _M0L6_2atmpS4869 = _M0L6_2atmpS4870 - _M0L6_2atmpS4871;
          _M0L6_2atmpS4868 = _M0L6_2atmpS4869 * _M0L16dt__over__tau__mS1551;
          _M0L6_2atmpS4866 = _M0L6_2atmpS4867 + _M0L6_2atmpS4868;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS4865, _M0L1iS1548, _M0L6_2atmpS4866);
          _M0L3valS4877 = _M0L1sS1552->$0;
          _M0L6_2atmpS4876 = _M0L3valS4877 + 1;
          _M0L1sS1552->$0 = _M0L6_2atmpS4876;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1552);
        }
        break;
      }
      _M0L6_2atmpS4885 = _M0L1iS1548 + 1;
      _M0L1iS1548 = _M0L6_2atmpS4885;
      continue;
    }
    break;
  }
  _M0L7_2abindS1557 = 0;
  _M0L1iS1558 = _M0L7_2abindS1557;
  while (1) {
    if (_M0L1iS1558 < _M0L1nS1533) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4887 = _M0L1pS1534->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4890 = _M0L1pS1534->$8;
      int32_t _M0L6_2atmpS4889;
      int32_t _M0L6_2atmpS4888;
      struct _M0TPB5ArrayGbE* _M0L4fireS4891;
      struct _M0TPB5ArrayGfE* _M0L5traceS4892;
      struct _M0TPB5ArrayGfE* _M0L5traceS4900;
      float _M0L6_2atmpS4894;
      struct _M0TPB5ArrayGfE* _M0L5traceS4899;
      float _M0L6_2atmpS4898;
      float _M0L6_2atmpS4897;
      float _M0L6_2atmpS4896;
      float _M0L6_2atmpS4895;
      float _M0L6_2atmpS4893;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4902;
      int32_t _M0L6_2atmpS4901;
      struct _M0TPB5ArrayGfE* _M0L5traceS4903;
      struct _M0TPB5ArrayGfE* _M0L5traceS4912;
      float _M0L6_2atmpS4905;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4911;
      float _M0L6_2atmpS4908;
      struct _M0TPB5ArrayGfE* _M0L5traceS4910;
      float _M0L6_2atmpS4909;
      float _M0L6_2atmpS4907;
      float _M0L6_2atmpS4906;
      float _M0L6_2atmpS4904;
      float _M0L6_2atmpS4928;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4933;
      float _M0L6_2atmpS4930;
      struct _M0TPB5ArrayGfE* _M0L5traceS4932;
      float _M0L6_2atmpS4931;
      float _M0L6_2atmpS4929;
      float _M0L12sigmoid__argS1561;
      float _M0L4rateS1562;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4914;
      float _M0L6_2atmpS4913;
      int32_t _M0L6_2atmpS4886;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4889
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4890, _M0L1iS1558);
      _M0L6_2atmpS4888 = _M0L6_2atmpS4889 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4887, _M0L1iS1558, _M0L6_2atmpS4888);
      _M0L4fireS4891 = _M0L1pS1534->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4891, _M0L1iS1558, 0);
      _M0L5traceS4892 = _M0L1pS1534->$9;
      _M0L5traceS4900 = _M0L1pS1534->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4894
      = _M0MPC15array5Array2atGfE(_M0L5traceS4900, _M0L1iS1558);
      _M0L5traceS4899 = _M0L1pS1534->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4898
      = _M0MPC15array5Array2atGfE(_M0L5traceS4899, _M0L1iS1558);
      _M0L6_2atmpS4897 = -_M0L6_2atmpS4898;
      _M0L6_2atmpS4896 = _M0L6_2atmpS4897 / _M0L9tau__rateS1539;
      _M0L6_2atmpS4895 = _M0L2dtS1542 * _M0L6_2atmpS4896;
      _M0L6_2atmpS4893 = _M0L6_2atmpS4894 + _M0L6_2atmpS4895;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4892, _M0L1iS1558, _M0L6_2atmpS4893);
      _M0L4tabsS4902 = _M0L1pS1534->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4901
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4902, _M0L1iS1558);
      if (_M0L6_2atmpS4901 > 0) {
        goto join_1559;
      }
      _M0L5traceS4903 = _M0L1pS1534->$9;
      _M0L5traceS4912 = _M0L1pS1534->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4905
      = _M0MPC15array5Array2atGfE(_M0L5traceS4912, _M0L1iS1558);
      _M0L4v__sS4911 = _M0L1pS1534->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4908
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4911, _M0L1iS1558);
      _M0L5traceS4910 = _M0L1pS1534->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4909
      = _M0MPC15array5Array2atGfE(_M0L5traceS4910, _M0L1iS1558);
      _M0L6_2atmpS4907 = _M0L6_2atmpS4908 - _M0L6_2atmpS4909;
      _M0L6_2atmpS4906 = _M0L6_2atmpS4907 / _M0L9tau__rateS1539;
      _M0L6_2atmpS4904 = _M0L6_2atmpS4905 + _M0L6_2atmpS4906;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4903, _M0L1iS1558, _M0L6_2atmpS4904);
      _M0L6_2atmpS4928 = -_M0L9steepnessS1537;
      _M0L4v__sS4933 = _M0L1pS1534->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4930
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4933, _M0L1iS1558);
      _M0L5traceS4932 = _M0L1pS1534->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4931
      = _M0MPC15array5Array2atGfE(_M0L5traceS4932, _M0L1iS1558);
      _M0L6_2atmpS4929 = _M0L6_2atmpS4930 - _M0L6_2atmpS4931;
      _M0L12sigmoid__argS1561 = _M0L6_2atmpS4928 * _M0L6_2atmpS4929;
      if (_M0L12sigmoid__argS1561 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS4922 = _M0L1pS1534->$5;
        float _M0L6_2atmpS4921;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4921
        = _M0MPC15array5Array2atGfE(_M0L1rS4922, _M0L1iS1558);
        _M0L4rateS1562 = _M0L6_2atmpS4921 * _M0L2dtS1542;
      } else if (_M0L12sigmoid__argS1561 < -0x1.6p+6f) {
        _M0L4rateS1562 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS4927 = _M0L1pS1534->$5;
        float _M0L6_2atmpS4926;
        float _M0L6_2atmpS4923;
        float _M0L6_2atmpS4925;
        float _M0L6_2atmpS4924;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4926
        = _M0MPC15array5Array2atGfE(_M0L1rS4927, _M0L1iS1558);
        _M0L6_2atmpS4923 = _M0L6_2atmpS4926 * _M0L2dtS1542;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4925
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS1561);
        _M0L6_2atmpS4924 = 0x1p+0f + _M0L6_2atmpS4925;
        _M0L4rateS1562 = _M0L6_2atmpS4923 / _M0L6_2atmpS4924;
      }
      _M0L9randcacheS4914 = _M0L1pS1534->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4913
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS4914, _M0L1iS1558);
      if (_M0L6_2atmpS4913 < _M0L4rateS1562) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4915 = _M0L1pS1534->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4916;
        struct _M0TPB5ArrayGfE* _M0L5traceS4917;
        struct _M0TPB5ArrayGfE* _M0L5traceS4920;
        float _M0L6_2atmpS4919;
        float _M0L6_2atmpS4918;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4915, _M0L1iS1558, 1);
        _M0L4tabsS4916 = _M0L1pS1534->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4916, _M0L1iS1558, _M0L11tabs__stepsS1541);
        _M0L5traceS4917 = _M0L1pS1534->$9;
        _M0L5traceS4920 = _M0L1pS1534->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4919
        = _M0MPC15array5Array2atGfE(_M0L5traceS4920, _M0L1iS1558);
        _M0L6_2atmpS4918 = _M0L6_2atmpS4919 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS4917, _M0L1iS1558, _M0L6_2atmpS4918);
      }
      goto join_1559;
      goto joinlet_5995;
      join_1559:;
      _M0L6_2atmpS4886 = _M0L1iS1558 + 1;
      _M0L1iS1558 = _M0L6_2atmpS4886;
      continue;
      joinlet_5995:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1512,
  float _M0L2dtS1527
) {
  int32_t _M0L1nS1511;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S1513;
  float _M0L2tmS1514;
  float _M0L2vtS1515;
  float _M0L2vrS1516;
  float _M0L2elS1517;
  float _M0L1rS1518;
  float _M0L9dt__slopeS1519;
  float _M0L2twS1520;
  float _M0L1aS1521;
  float _M0L1bS1522;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4849;
  float _M0L2atS1523;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4848;
  float _M0L6tau__aS1524;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4847;
  float _M0L11tabs__constS1525;
  float _M0L6_2atmpS4846;
  int32_t _M0L11tabs__stepsS1526;
  int32_t _M0L7_2abindS1528;
  int32_t _M0L1iS1529;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1511 = _M0L1pS1512->$2;
  _M0L3p__S1513 = _M0L1pS1512->$0;
  _M0L2tmS1514 = _M0L3p__S1513->$5;
  _M0L2vtS1515 = _M0L3p__S1513->$2;
  _M0L2vrS1516 = _M0L3p__S1513->$3;
  _M0L2elS1517 = _M0L3p__S1513->$4;
  _M0L1rS1518 = _M0L3p__S1513->$6;
  _M0L9dt__slopeS1519 = _M0L3p__S1513->$7;
  _M0L2twS1520 = _M0L3p__S1513->$8;
  _M0L1aS1521 = _M0L3p__S1513->$9;
  _M0L1bS1522 = _M0L3p__S1513->$10;
  _M0L5spikeS4849 = _M0L1pS1512->$1;
  _M0L2atS1523 = _M0L5spikeS4849->$0;
  _M0L5spikeS4848 = _M0L1pS1512->$1;
  _M0L6tau__aS1524 = _M0L5spikeS4848->$1;
  _M0L5spikeS4847 = _M0L1pS1512->$1;
  _M0L11tabs__constS1525 = _M0L5spikeS4847->$3;
  _M0L6_2atmpS4846 = _M0L11tabs__constS1525 / _M0L2dtS1527;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS1526 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4846);
  _M0L7_2abindS1528 = 0;
  _M0L1iS1529 = _M0L7_2abindS1528;
  while (1) {
    if (_M0L1iS1529 < _M0L1nS1511) {
      struct _M0TPB5ArrayGfE* _M0L1vS4759 = _M0L1pS1512->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4761 = _M0L1pS1512->$5;
      float _M0L6_2atmpS4760;
      struct _M0TPB5ArrayGbE* _M0L4fireS4763;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4764;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4767;
      int32_t _M0L6_2atmpS4766;
      int32_t _M0L6_2atmpS4765;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4769;
      int32_t _M0L6_2atmpS4768;
      struct _M0TPB5ArrayGfE* _M0L1wS4770;
      struct _M0TPB5ArrayGfE* _M0L1wS4782;
      float _M0L6_2atmpS4772;
      struct _M0TPB5ArrayGfE* _M0L1vS4781;
      float _M0L6_2atmpS4780;
      float _M0L6_2atmpS4779;
      float _M0L6_2atmpS4776;
      struct _M0TPB5ArrayGfE* _M0L1wS4778;
      float _M0L6_2atmpS4777;
      float _M0L6_2atmpS4775;
      float _M0L6_2atmpS4774;
      float _M0L6_2atmpS4773;
      float _M0L6_2atmpS4771;
      float _M0L9exp__termS1532;
      struct _M0TPB5ArrayGfE* _M0L1vS4783;
      struct _M0TPB5ArrayGfE* _M0L1vS4805;
      float _M0L6_2atmpS4785;
      struct _M0TPB5ArrayGfE* _M0L1vS4804;
      float _M0L6_2atmpS4803;
      float _M0L6_2atmpS4802;
      float _M0L6_2atmpS4801;
      float _M0L6_2atmpS4797;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4800;
      float _M0L6_2atmpS4799;
      float _M0L6_2atmpS4798;
      float _M0L6_2atmpS4793;
      struct _M0TPB5ArrayGfE* _M0L1wS4796;
      float _M0L6_2atmpS4795;
      float _M0L6_2atmpS4794;
      float _M0L6_2atmpS4789;
      struct _M0TPB5ArrayGfE* _M0L1iS4792;
      float _M0L6_2atmpS4791;
      float _M0L6_2atmpS4790;
      float _M0L6_2atmpS4788;
      float _M0L6_2atmpS4787;
      float _M0L6_2atmpS4786;
      float _M0L6_2atmpS4784;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4806;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4814;
      float _M0L6_2atmpS4808;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4813;
      float _M0L6_2atmpS4812;
      float _M0L6_2atmpS4811;
      float _M0L6_2atmpS4810;
      float _M0L6_2atmpS4809;
      float _M0L6_2atmpS4807;
      struct _M0TPB5ArrayGbE* _M0L4fireS4815;
      struct _M0TPB5ArrayGfE* _M0L1vS4818;
      float _M0L6_2atmpS4817;
      int32_t _M0L6_2atmpS4816;
      struct _M0TPB5ArrayGfE* _M0L1vS4819;
      struct _M0TPB5ArrayGbE* _M0L4fireS4821;
      float _M0L6_2atmpS4820;
      struct _M0TPB5ArrayGfE* _M0L1wS4823;
      struct _M0TPB5ArrayGbE* _M0L4fireS4825;
      float _M0L6_2atmpS4824;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4829;
      struct _M0TPB5ArrayGbE* _M0L4fireS4831;
      float _M0L6_2atmpS4830;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4835;
      struct _M0TPB5ArrayGbE* _M0L4fireS4837;
      int32_t _M0L6_2atmpS4836;
      int32_t _M0L6_2atmpS4758;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4761, _M0L1iS1529)) {
        _M0L6_2atmpS4760 = _M0L2vrS1516;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4762 = _M0L1pS1512->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4760
        = _M0MPC15array5Array2atGfE(_M0L1vS4762, _M0L1iS1529);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4759, _M0L1iS1529, _M0L6_2atmpS4760);
      _M0L4fireS4763 = _M0L1pS1512->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4763, _M0L1iS1529, 0);
      _M0L4tabsS4764 = _M0L1pS1512->$7;
      _M0L4tabsS4767 = _M0L1pS1512->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4766
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4767, _M0L1iS1529);
      _M0L6_2atmpS4765 = _M0L6_2atmpS4766 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4764, _M0L1iS1529, _M0L6_2atmpS4765);
      _M0L4tabsS4769 = _M0L1pS1512->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4768
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4769, _M0L1iS1529);
      if (_M0L6_2atmpS4768 > 0) {
        goto join_1530;
      }
      _M0L1wS4770 = _M0L1pS1512->$4;
      _M0L1wS4782 = _M0L1pS1512->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4772 = _M0MPC15array5Array2atGfE(_M0L1wS4782, _M0L1iS1529);
      _M0L1vS4781 = _M0L1pS1512->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4780 = _M0MPC15array5Array2atGfE(_M0L1vS4781, _M0L1iS1529);
      _M0L6_2atmpS4779 = _M0L6_2atmpS4780 - _M0L2elS1517;
      _M0L6_2atmpS4776 = _M0L1aS1521 * _M0L6_2atmpS4779;
      _M0L1wS4778 = _M0L1pS1512->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4777 = _M0MPC15array5Array2atGfE(_M0L1wS4778, _M0L1iS1529);
      _M0L6_2atmpS4775 = _M0L6_2atmpS4776 - _M0L6_2atmpS4777;
      _M0L6_2atmpS4774 = _M0L2dtS1527 * _M0L6_2atmpS4775;
      _M0L6_2atmpS4773 = _M0L6_2atmpS4774 / _M0L2twS1520;
      _M0L6_2atmpS4771 = _M0L6_2atmpS4772 + _M0L6_2atmpS4773;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4770, _M0L1iS1529, _M0L6_2atmpS4771);
      if (_M0L9dt__slopeS1519 < 0x0p+0f) {
        _M0L9exp__termS1532 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4845 = _M0L1pS1512->$3;
        float _M0L6_2atmpS4842;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4844;
        float _M0L6_2atmpS4843;
        float _M0L6_2atmpS4841;
        float _M0L6_2atmpS4840;
        float _M0L6_2atmpS4839;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4842
        = _M0MPC15array5Array2atGfE(_M0L1vS4845, _M0L1iS1529);
        _M0L9thresholdS4844 = _M0L1pS1512->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4843
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4844, _M0L1iS1529);
        _M0L6_2atmpS4841 = _M0L6_2atmpS4842 - _M0L6_2atmpS4843;
        _M0L6_2atmpS4840 = _M0L6_2atmpS4841 / _M0L9dt__slopeS1519;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4839 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4840);
        _M0L9exp__termS1532 = _M0L9dt__slopeS1519 * _M0L6_2atmpS4839;
      }
      _M0L1vS4783 = _M0L1pS1512->$3;
      _M0L1vS4805 = _M0L1pS1512->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4785 = _M0MPC15array5Array2atGfE(_M0L1vS4805, _M0L1iS1529);
      _M0L1vS4804 = _M0L1pS1512->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4803 = _M0MPC15array5Array2atGfE(_M0L1vS4804, _M0L1iS1529);
      _M0L6_2atmpS4802 = _M0L6_2atmpS4803 - _M0L2elS1517;
      _M0L6_2atmpS4801 = -_M0L6_2atmpS4802;
      _M0L6_2atmpS4797 = _M0L6_2atmpS4801 + _M0L9exp__termS1532;
      _M0L9syn__currS4800 = _M0L1pS1512->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4799
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4800, _M0L1iS1529);
      _M0L6_2atmpS4798 = _M0L1rS1518 * _M0L6_2atmpS4799;
      _M0L6_2atmpS4793 = _M0L6_2atmpS4797 - _M0L6_2atmpS4798;
      _M0L1wS4796 = _M0L1pS1512->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4795 = _M0MPC15array5Array2atGfE(_M0L1wS4796, _M0L1iS1529);
      _M0L6_2atmpS4794 = _M0L1rS1518 * _M0L6_2atmpS4795;
      _M0L6_2atmpS4789 = _M0L6_2atmpS4793 - _M0L6_2atmpS4794;
      _M0L1iS4792 = _M0L1pS1512->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4791 = _M0MPC15array5Array2atGfE(_M0L1iS4792, _M0L1iS1529);
      _M0L6_2atmpS4790 = _M0L1rS1518 * _M0L6_2atmpS4791;
      _M0L6_2atmpS4788 = _M0L6_2atmpS4789 + _M0L6_2atmpS4790;
      _M0L6_2atmpS4787 = _M0L2dtS1527 * _M0L6_2atmpS4788;
      _M0L6_2atmpS4786 = _M0L6_2atmpS4787 / _M0L2tmS1514;
      _M0L6_2atmpS4784 = _M0L6_2atmpS4785 + _M0L6_2atmpS4786;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4783, _M0L1iS1529, _M0L6_2atmpS4784);
      _M0L9thresholdS4806 = _M0L1pS1512->$6;
      _M0L9thresholdS4814 = _M0L1pS1512->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4808
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4814, _M0L1iS1529);
      _M0L9thresholdS4813 = _M0L1pS1512->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4812
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4813, _M0L1iS1529);
      _M0L6_2atmpS4811 = _M0L2vtS1515 - _M0L6_2atmpS4812;
      _M0L6_2atmpS4810 = _M0L2dtS1527 * _M0L6_2atmpS4811;
      _M0L6_2atmpS4809 = _M0L6_2atmpS4810 / _M0L6tau__aS1524;
      _M0L6_2atmpS4807 = _M0L6_2atmpS4808 + _M0L6_2atmpS4809;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4806, _M0L1iS1529, _M0L6_2atmpS4807);
      _M0L4fireS4815 = _M0L1pS1512->$5;
      _M0L1vS4818 = _M0L1pS1512->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4817 = _M0MPC15array5Array2atGfE(_M0L1vS4818, _M0L1iS1529);
      _M0L6_2atmpS4816 = _M0L6_2atmpS4817 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4815, _M0L1iS1529, _M0L6_2atmpS4816);
      _M0L1vS4819 = _M0L1pS1512->$3;
      _M0L4fireS4821 = _M0L1pS1512->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4821, _M0L1iS1529)) {
        _M0L6_2atmpS4820 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4822 = _M0L1pS1512->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4820
        = _M0MPC15array5Array2atGfE(_M0L1vS4822, _M0L1iS1529);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4819, _M0L1iS1529, _M0L6_2atmpS4820);
      _M0L1wS4823 = _M0L1pS1512->$4;
      _M0L4fireS4825 = _M0L1pS1512->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4825, _M0L1iS1529)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4827 = _M0L1pS1512->$4;
        float _M0L6_2atmpS4826;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4826
        = _M0MPC15array5Array2atGfE(_M0L1wS4827, _M0L1iS1529);
        _M0L6_2atmpS4824 = _M0L6_2atmpS4826 + _M0L1bS1522;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4828 = _M0L1pS1512->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4824
        = _M0MPC15array5Array2atGfE(_M0L1wS4828, _M0L1iS1529);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4823, _M0L1iS1529, _M0L6_2atmpS4824);
      _M0L9thresholdS4829 = _M0L1pS1512->$6;
      _M0L4fireS4831 = _M0L1pS1512->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4831, _M0L1iS1529)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4833 = _M0L1pS1512->$6;
        float _M0L6_2atmpS4832;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4832
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4833, _M0L1iS1529);
        _M0L6_2atmpS4830 = _M0L6_2atmpS4832 + _M0L2atS1523;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4834 = _M0L1pS1512->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4830
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4834, _M0L1iS1529);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4829, _M0L1iS1529, _M0L6_2atmpS4830);
      _M0L4tabsS4835 = _M0L1pS1512->$7;
      _M0L4fireS4837 = _M0L1pS1512->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4837, _M0L1iS1529)) {
        _M0L6_2atmpS4836 = _M0L11tabs__stepsS1526;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4838 = _M0L1pS1512->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4836
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4838, _M0L1iS1529);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4835, _M0L1iS1529, _M0L6_2atmpS4836);
      goto join_1530;
      goto joinlet_5997;
      join_1530:;
      _M0L6_2atmpS4758 = _M0L1iS1529 + 1;
      _M0L1iS1529 = _M0L6_2atmpS4758;
      continue;
      joinlet_5997:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1507
) {
  int32_t _M0L1nS1506;
  int32_t _M0L7_2abindS1508;
  int32_t _M0L1iS1509;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1506 = _M0L1pS1507->$2;
  _M0L7_2abindS1508 = 0;
  _M0L1iS1509 = _M0L7_2abindS1508;
  while (1) {
    if (_M0L1iS1509 < _M0L1nS1506) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4735 = _M0L1pS1507->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4756 = _M0L1pS1507->$10;
      float _M0L6_2atmpS4751;
      struct _M0TPB5ArrayGfE* _M0L1vS4755;
      float _M0L6_2atmpS4753;
      float _M0L4e__eS4754;
      float _M0L6_2atmpS4752;
      float _M0L6_2atmpS4748;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4750;
      float _M0L6_2atmpS4749;
      float _M0L6_2atmpS4737;
      struct _M0TPB5ArrayGfE* _M0L2giS4747;
      float _M0L6_2atmpS4742;
      struct _M0TPB5ArrayGfE* _M0L1vS4746;
      float _M0L6_2atmpS4744;
      float _M0L4e__iS4745;
      float _M0L6_2atmpS4743;
      float _M0L6_2atmpS4739;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4741;
      float _M0L6_2atmpS4740;
      float _M0L6_2atmpS4738;
      float _M0L6_2atmpS4736;
      int32_t _M0L6_2atmpS4757;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4751 = _M0MPC15array5Array2atGfE(_M0L2geS4756, _M0L1iS1509);
      _M0L1vS4755 = _M0L1pS1507->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4753 = _M0MPC15array5Array2atGfE(_M0L1vS4755, _M0L1iS1509);
      _M0L4e__eS4754 = _M0L1pS1507->$16;
      _M0L6_2atmpS4752 = _M0L6_2atmpS4753 - _M0L4e__eS4754;
      _M0L6_2atmpS4748 = _M0L6_2atmpS4751 * _M0L6_2atmpS4752;
      _M0L7gsyn__eS4750 = _M0L1pS1507->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4749
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4750, _M0L1iS1509);
      _M0L6_2atmpS4737 = _M0L6_2atmpS4748 * _M0L6_2atmpS4749;
      _M0L2giS4747 = _M0L1pS1507->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4742 = _M0MPC15array5Array2atGfE(_M0L2giS4747, _M0L1iS1509);
      _M0L1vS4746 = _M0L1pS1507->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4744 = _M0MPC15array5Array2atGfE(_M0L1vS4746, _M0L1iS1509);
      _M0L4e__iS4745 = _M0L1pS1507->$17;
      _M0L6_2atmpS4743 = _M0L6_2atmpS4744 - _M0L4e__iS4745;
      _M0L6_2atmpS4739 = _M0L6_2atmpS4742 * _M0L6_2atmpS4743;
      _M0L7gsyn__iS4741 = _M0L1pS1507->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4740
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4741, _M0L1iS1509);
      _M0L6_2atmpS4738 = _M0L6_2atmpS4739 * _M0L6_2atmpS4740;
      _M0L6_2atmpS4736 = _M0L6_2atmpS4737 + _M0L6_2atmpS4738;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4735, _M0L1iS1509, _M0L6_2atmpS4736);
      _M0L6_2atmpS4757 = _M0L1iS1509 + 1;
      _M0L1iS1509 = _M0L6_2atmpS4757;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1496,
  float _M0L2dtS1501
) {
  int32_t _M0L1nS1495;
  float _M0L6tau__eS1497;
  float _M0L6tau__iS1498;
  int32_t _M0L7_2abindS1499;
  int32_t _M0L1iS1500;
  int32_t _M0L7_2abindS1503;
  int32_t _M0L1iS1504;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1495 = _M0L1pS1496->$2;
  _M0L6tau__eS1497 = _M0L1pS1496->$18;
  _M0L6tau__iS1498 = _M0L1pS1496->$19;
  _M0L7_2abindS1499 = 0;
  _M0L1iS1500 = _M0L7_2abindS1499;
  while (1) {
    if (_M0L1iS1500 < _M0L1nS1495) {
      struct _M0TPB5ArrayGfE* _M0L2geS4701 = _M0L1pS1496->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS4706 = _M0L1pS1496->$10;
      float _M0L6_2atmpS4703;
      struct _M0TPB5ArrayGfE* _M0L3gluS4705;
      float _M0L6_2atmpS4704;
      float _M0L6_2atmpS4702;
      struct _M0TPB5ArrayGfE* _M0L2giS4707;
      struct _M0TPB5ArrayGfE* _M0L2giS4712;
      float _M0L6_2atmpS4709;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4711;
      float _M0L6_2atmpS4710;
      float _M0L6_2atmpS4708;
      struct _M0TPB5ArrayGfE* _M0L2geS4713;
      struct _M0TPB5ArrayGfE* _M0L2geS4721;
      float _M0L6_2atmpS4715;
      struct _M0TPB5ArrayGfE* _M0L2geS4720;
      float _M0L6_2atmpS4719;
      float _M0L6_2atmpS4718;
      float _M0L6_2atmpS4717;
      float _M0L6_2atmpS4716;
      float _M0L6_2atmpS4714;
      struct _M0TPB5ArrayGfE* _M0L2giS4722;
      struct _M0TPB5ArrayGfE* _M0L2giS4730;
      float _M0L6_2atmpS4724;
      struct _M0TPB5ArrayGfE* _M0L2giS4729;
      float _M0L6_2atmpS4728;
      float _M0L6_2atmpS4727;
      float _M0L6_2atmpS4726;
      float _M0L6_2atmpS4725;
      float _M0L6_2atmpS4723;
      int32_t _M0L6_2atmpS4731;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4703 = _M0MPC15array5Array2atGfE(_M0L2geS4706, _M0L1iS1500);
      _M0L3gluS4705 = _M0L1pS1496->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4704
      = _M0MPC15array5Array2atGfE(_M0L3gluS4705, _M0L1iS1500);
      _M0L6_2atmpS4702 = _M0L6_2atmpS4703 + _M0L6_2atmpS4704;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4701, _M0L1iS1500, _M0L6_2atmpS4702);
      _M0L2giS4707 = _M0L1pS1496->$11;
      _M0L2giS4712 = _M0L1pS1496->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4709 = _M0MPC15array5Array2atGfE(_M0L2giS4712, _M0L1iS1500);
      _M0L4gabaS4711 = _M0L1pS1496->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4710
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4711, _M0L1iS1500);
      _M0L6_2atmpS4708 = _M0L6_2atmpS4709 + _M0L6_2atmpS4710;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4707, _M0L1iS1500, _M0L6_2atmpS4708);
      _M0L2geS4713 = _M0L1pS1496->$10;
      _M0L2geS4721 = _M0L1pS1496->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4715 = _M0MPC15array5Array2atGfE(_M0L2geS4721, _M0L1iS1500);
      _M0L2geS4720 = _M0L1pS1496->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4719 = _M0MPC15array5Array2atGfE(_M0L2geS4720, _M0L1iS1500);
      _M0L6_2atmpS4718 = -_M0L6_2atmpS4719;
      _M0L6_2atmpS4717 = _M0L6_2atmpS4718 / _M0L6tau__eS1497;
      _M0L6_2atmpS4716 = _M0L2dtS1501 * _M0L6_2atmpS4717;
      _M0L6_2atmpS4714 = _M0L6_2atmpS4715 + _M0L6_2atmpS4716;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4713, _M0L1iS1500, _M0L6_2atmpS4714);
      _M0L2giS4722 = _M0L1pS1496->$11;
      _M0L2giS4730 = _M0L1pS1496->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4724 = _M0MPC15array5Array2atGfE(_M0L2giS4730, _M0L1iS1500);
      _M0L2giS4729 = _M0L1pS1496->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4728 = _M0MPC15array5Array2atGfE(_M0L2giS4729, _M0L1iS1500);
      _M0L6_2atmpS4727 = -_M0L6_2atmpS4728;
      _M0L6_2atmpS4726 = _M0L6_2atmpS4727 / _M0L6tau__iS1498;
      _M0L6_2atmpS4725 = _M0L2dtS1501 * _M0L6_2atmpS4726;
      _M0L6_2atmpS4723 = _M0L6_2atmpS4724 + _M0L6_2atmpS4725;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4722, _M0L1iS1500, _M0L6_2atmpS4723);
      _M0L6_2atmpS4731 = _M0L1iS1500 + 1;
      _M0L1iS1500 = _M0L6_2atmpS4731;
      continue;
    }
    break;
  }
  _M0L7_2abindS1503 = 0;
  _M0L1iS1504 = _M0L7_2abindS1503;
  while (1) {
    if (_M0L1iS1504 < _M0L1nS1495) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4732 = _M0L1pS1496->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4733;
      int32_t _M0L6_2atmpS4734;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4732, _M0L1iS1504, 0x0p+0f);
      _M0L4gabaS4733 = _M0L1pS1496->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4733, _M0L1iS1504, 0x0p+0f);
      _M0L6_2atmpS4734 = _M0L1iS1504 + 1;
      _M0L1iS1504 = _M0L6_2atmpS4734;
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
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1474,
  float _M0L2dtS1489
) {
  int32_t _M0L1nS1473;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1475;
  float _M0L2tmS1476;
  float _M0L2vtS1477;
  float _M0L2vrS1478;
  float _M0L2elS1479;
  float _M0L1rS1480;
  float _M0L9dt__slopeS1481;
  float _M0L2twS1482;
  float _M0L1aS1483;
  float _M0L1bS1484;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4700;
  float _M0L2atS1485;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4699;
  float _M0L6tau__aS1486;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4698;
  float _M0L11tabs__constS1487;
  float _M0L6_2atmpS4697;
  int32_t _M0L11tabs__stepsS1488;
  int32_t _M0L7_2abindS1490;
  int32_t _M0L1iS1491;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1473 = _M0L1pS1474->$2;
  _M0L3p__S1475 = _M0L1pS1474->$0;
  _M0L2tmS1476 = _M0L3p__S1475->$5;
  _M0L2vtS1477 = _M0L3p__S1475->$2;
  _M0L2vrS1478 = _M0L3p__S1475->$3;
  _M0L2elS1479 = _M0L3p__S1475->$4;
  _M0L1rS1480 = _M0L3p__S1475->$6;
  _M0L9dt__slopeS1481 = _M0L3p__S1475->$7;
  _M0L2twS1482 = _M0L3p__S1475->$8;
  _M0L1aS1483 = _M0L3p__S1475->$9;
  _M0L1bS1484 = _M0L3p__S1475->$10;
  _M0L5spikeS4700 = _M0L1pS1474->$1;
  _M0L2atS1485 = _M0L5spikeS4700->$0;
  _M0L5spikeS4699 = _M0L1pS1474->$1;
  _M0L6tau__aS1486 = _M0L5spikeS4699->$1;
  _M0L5spikeS4698 = _M0L1pS1474->$1;
  _M0L11tabs__constS1487 = _M0L5spikeS4698->$3;
  _M0L6_2atmpS4697 = _M0L11tabs__constS1487 / _M0L2dtS1489;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1488 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4697);
  _M0L7_2abindS1490 = 0;
  _M0L1iS1491 = _M0L7_2abindS1490;
  while (1) {
    if (_M0L1iS1491 < _M0L1nS1473) {
      struct _M0TPB5ArrayGfE* _M0L1vS4610 = _M0L1pS1474->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4612 = _M0L1pS1474->$5;
      float _M0L6_2atmpS4611;
      struct _M0TPB5ArrayGbE* _M0L4fireS4614;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4615;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4618;
      int32_t _M0L6_2atmpS4617;
      int32_t _M0L6_2atmpS4616;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4620;
      int32_t _M0L6_2atmpS4619;
      struct _M0TPB5ArrayGfE* _M0L1wS4621;
      struct _M0TPB5ArrayGfE* _M0L1wS4633;
      float _M0L6_2atmpS4623;
      struct _M0TPB5ArrayGfE* _M0L1vS4632;
      float _M0L6_2atmpS4631;
      float _M0L6_2atmpS4630;
      float _M0L6_2atmpS4627;
      struct _M0TPB5ArrayGfE* _M0L1wS4629;
      float _M0L6_2atmpS4628;
      float _M0L6_2atmpS4626;
      float _M0L6_2atmpS4625;
      float _M0L6_2atmpS4624;
      float _M0L6_2atmpS4622;
      float _M0L9exp__termS1494;
      struct _M0TPB5ArrayGfE* _M0L1vS4634;
      struct _M0TPB5ArrayGfE* _M0L1vS4656;
      float _M0L6_2atmpS4636;
      struct _M0TPB5ArrayGfE* _M0L1vS4655;
      float _M0L6_2atmpS4654;
      float _M0L6_2atmpS4653;
      float _M0L6_2atmpS4652;
      float _M0L6_2atmpS4648;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4651;
      float _M0L6_2atmpS4650;
      float _M0L6_2atmpS4649;
      float _M0L6_2atmpS4644;
      struct _M0TPB5ArrayGfE* _M0L1wS4647;
      float _M0L6_2atmpS4646;
      float _M0L6_2atmpS4645;
      float _M0L6_2atmpS4640;
      struct _M0TPB5ArrayGfE* _M0L1iS4643;
      float _M0L6_2atmpS4642;
      float _M0L6_2atmpS4641;
      float _M0L6_2atmpS4639;
      float _M0L6_2atmpS4638;
      float _M0L6_2atmpS4637;
      float _M0L6_2atmpS4635;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4657;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4665;
      float _M0L6_2atmpS4659;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4664;
      float _M0L6_2atmpS4663;
      float _M0L6_2atmpS4662;
      float _M0L6_2atmpS4661;
      float _M0L6_2atmpS4660;
      float _M0L6_2atmpS4658;
      struct _M0TPB5ArrayGbE* _M0L4fireS4666;
      struct _M0TPB5ArrayGfE* _M0L1vS4669;
      float _M0L6_2atmpS4668;
      int32_t _M0L6_2atmpS4667;
      struct _M0TPB5ArrayGfE* _M0L1vS4670;
      struct _M0TPB5ArrayGbE* _M0L4fireS4672;
      float _M0L6_2atmpS4671;
      struct _M0TPB5ArrayGfE* _M0L1wS4674;
      struct _M0TPB5ArrayGbE* _M0L4fireS4676;
      float _M0L6_2atmpS4675;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4680;
      struct _M0TPB5ArrayGbE* _M0L4fireS4682;
      float _M0L6_2atmpS4681;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4686;
      struct _M0TPB5ArrayGbE* _M0L4fireS4688;
      int32_t _M0L6_2atmpS4687;
      int32_t _M0L6_2atmpS4609;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4612, _M0L1iS1491)) {
        _M0L6_2atmpS4611 = _M0L2vrS1478;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4613 = _M0L1pS1474->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4611
        = _M0MPC15array5Array2atGfE(_M0L1vS4613, _M0L1iS1491);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4610, _M0L1iS1491, _M0L6_2atmpS4611);
      _M0L4fireS4614 = _M0L1pS1474->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4614, _M0L1iS1491, 0);
      _M0L4tabsS4615 = _M0L1pS1474->$7;
      _M0L4tabsS4618 = _M0L1pS1474->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4617
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4618, _M0L1iS1491);
      _M0L6_2atmpS4616 = _M0L6_2atmpS4617 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4615, _M0L1iS1491, _M0L6_2atmpS4616);
      _M0L4tabsS4620 = _M0L1pS1474->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4619
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4620, _M0L1iS1491);
      if (_M0L6_2atmpS4619 > 0) {
        goto join_1492;
      }
      _M0L1wS4621 = _M0L1pS1474->$4;
      _M0L1wS4633 = _M0L1pS1474->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4623 = _M0MPC15array5Array2atGfE(_M0L1wS4633, _M0L1iS1491);
      _M0L1vS4632 = _M0L1pS1474->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4631 = _M0MPC15array5Array2atGfE(_M0L1vS4632, _M0L1iS1491);
      _M0L6_2atmpS4630 = _M0L6_2atmpS4631 - _M0L2elS1479;
      _M0L6_2atmpS4627 = _M0L1aS1483 * _M0L6_2atmpS4630;
      _M0L1wS4629 = _M0L1pS1474->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4628 = _M0MPC15array5Array2atGfE(_M0L1wS4629, _M0L1iS1491);
      _M0L6_2atmpS4626 = _M0L6_2atmpS4627 - _M0L6_2atmpS4628;
      _M0L6_2atmpS4625 = _M0L2dtS1489 * _M0L6_2atmpS4626;
      _M0L6_2atmpS4624 = _M0L6_2atmpS4625 / _M0L2twS1482;
      _M0L6_2atmpS4622 = _M0L6_2atmpS4623 + _M0L6_2atmpS4624;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4621, _M0L1iS1491, _M0L6_2atmpS4622);
      if (_M0L9dt__slopeS1481 < 0x0p+0f) {
        _M0L9exp__termS1494 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4696 = _M0L1pS1474->$3;
        float _M0L6_2atmpS4693;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4695;
        float _M0L6_2atmpS4694;
        float _M0L6_2atmpS4692;
        float _M0L6_2atmpS4691;
        float _M0L6_2atmpS4690;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4693
        = _M0MPC15array5Array2atGfE(_M0L1vS4696, _M0L1iS1491);
        _M0L9thresholdS4695 = _M0L1pS1474->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4694
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4695, _M0L1iS1491);
        _M0L6_2atmpS4692 = _M0L6_2atmpS4693 - _M0L6_2atmpS4694;
        _M0L6_2atmpS4691 = _M0L6_2atmpS4692 / _M0L9dt__slopeS1481;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4690 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4691);
        _M0L9exp__termS1494 = _M0L9dt__slopeS1481 * _M0L6_2atmpS4690;
      }
      _M0L1vS4634 = _M0L1pS1474->$3;
      _M0L1vS4656 = _M0L1pS1474->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4636 = _M0MPC15array5Array2atGfE(_M0L1vS4656, _M0L1iS1491);
      _M0L1vS4655 = _M0L1pS1474->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4654 = _M0MPC15array5Array2atGfE(_M0L1vS4655, _M0L1iS1491);
      _M0L6_2atmpS4653 = _M0L6_2atmpS4654 - _M0L2elS1479;
      _M0L6_2atmpS4652 = -_M0L6_2atmpS4653;
      _M0L6_2atmpS4648 = _M0L6_2atmpS4652 + _M0L9exp__termS1494;
      _M0L9syn__currS4651 = _M0L1pS1474->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4650
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4651, _M0L1iS1491);
      _M0L6_2atmpS4649 = _M0L1rS1480 * _M0L6_2atmpS4650;
      _M0L6_2atmpS4644 = _M0L6_2atmpS4648 - _M0L6_2atmpS4649;
      _M0L1wS4647 = _M0L1pS1474->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4646 = _M0MPC15array5Array2atGfE(_M0L1wS4647, _M0L1iS1491);
      _M0L6_2atmpS4645 = _M0L1rS1480 * _M0L6_2atmpS4646;
      _M0L6_2atmpS4640 = _M0L6_2atmpS4644 - _M0L6_2atmpS4645;
      _M0L1iS4643 = _M0L1pS1474->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4642 = _M0MPC15array5Array2atGfE(_M0L1iS4643, _M0L1iS1491);
      _M0L6_2atmpS4641 = _M0L1rS1480 * _M0L6_2atmpS4642;
      _M0L6_2atmpS4639 = _M0L6_2atmpS4640 + _M0L6_2atmpS4641;
      _M0L6_2atmpS4638 = _M0L2dtS1489 * _M0L6_2atmpS4639;
      _M0L6_2atmpS4637 = _M0L6_2atmpS4638 / _M0L2tmS1476;
      _M0L6_2atmpS4635 = _M0L6_2atmpS4636 + _M0L6_2atmpS4637;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4634, _M0L1iS1491, _M0L6_2atmpS4635);
      _M0L9thresholdS4657 = _M0L1pS1474->$6;
      _M0L9thresholdS4665 = _M0L1pS1474->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4659
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4665, _M0L1iS1491);
      _M0L9thresholdS4664 = _M0L1pS1474->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4663
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4664, _M0L1iS1491);
      _M0L6_2atmpS4662 = _M0L2vtS1477 - _M0L6_2atmpS4663;
      _M0L6_2atmpS4661 = _M0L2dtS1489 * _M0L6_2atmpS4662;
      _M0L6_2atmpS4660 = _M0L6_2atmpS4661 / _M0L6tau__aS1486;
      _M0L6_2atmpS4658 = _M0L6_2atmpS4659 + _M0L6_2atmpS4660;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4657, _M0L1iS1491, _M0L6_2atmpS4658);
      _M0L4fireS4666 = _M0L1pS1474->$5;
      _M0L1vS4669 = _M0L1pS1474->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4668 = _M0MPC15array5Array2atGfE(_M0L1vS4669, _M0L1iS1491);
      _M0L6_2atmpS4667 = _M0L6_2atmpS4668 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4666, _M0L1iS1491, _M0L6_2atmpS4667);
      _M0L1vS4670 = _M0L1pS1474->$3;
      _M0L4fireS4672 = _M0L1pS1474->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4672, _M0L1iS1491)) {
        _M0L6_2atmpS4671 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4673 = _M0L1pS1474->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4671
        = _M0MPC15array5Array2atGfE(_M0L1vS4673, _M0L1iS1491);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4670, _M0L1iS1491, _M0L6_2atmpS4671);
      _M0L1wS4674 = _M0L1pS1474->$4;
      _M0L4fireS4676 = _M0L1pS1474->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4676, _M0L1iS1491)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4678 = _M0L1pS1474->$4;
        float _M0L6_2atmpS4677;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4677
        = _M0MPC15array5Array2atGfE(_M0L1wS4678, _M0L1iS1491);
        _M0L6_2atmpS4675 = _M0L6_2atmpS4677 + _M0L1bS1484;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4679 = _M0L1pS1474->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4675
        = _M0MPC15array5Array2atGfE(_M0L1wS4679, _M0L1iS1491);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4674, _M0L1iS1491, _M0L6_2atmpS4675);
      _M0L9thresholdS4680 = _M0L1pS1474->$6;
      _M0L4fireS4682 = _M0L1pS1474->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4682, _M0L1iS1491)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4684 = _M0L1pS1474->$6;
        float _M0L6_2atmpS4683;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4683
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4684, _M0L1iS1491);
        _M0L6_2atmpS4681 = _M0L6_2atmpS4683 + _M0L2atS1485;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4685 = _M0L1pS1474->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4681
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4685, _M0L1iS1491);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4680, _M0L1iS1491, _M0L6_2atmpS4681);
      _M0L4tabsS4686 = _M0L1pS1474->$7;
      _M0L4fireS4688 = _M0L1pS1474->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4688, _M0L1iS1491)) {
        _M0L6_2atmpS4687 = _M0L11tabs__stepsS1488;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4689 = _M0L1pS1474->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4687
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4689, _M0L1iS1491);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4686, _M0L1iS1491, _M0L6_2atmpS4687);
      goto join_1492;
      goto joinlet_6002;
      join_1492:;
      _M0L6_2atmpS4609 = _M0L1iS1491 + 1;
      _M0L1iS1491 = _M0L6_2atmpS4609;
      continue;
      joinlet_6002:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1469
) {
  int32_t _M0L1nS1468;
  int32_t _M0L7_2abindS1470;
  int32_t _M0L1iS1471;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1468 = _M0L1pS1469->$2;
  _M0L7_2abindS1470 = 0;
  _M0L1iS1471 = _M0L7_2abindS1470;
  while (1) {
    if (_M0L1iS1471 < _M0L1nS1468) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4586 = _M0L1pS1469->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4607 = _M0L1pS1469->$10;
      float _M0L6_2atmpS4602;
      struct _M0TPB5ArrayGfE* _M0L1vS4606;
      float _M0L6_2atmpS4604;
      float _M0L4e__eS4605;
      float _M0L6_2atmpS4603;
      float _M0L6_2atmpS4599;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4601;
      float _M0L6_2atmpS4600;
      float _M0L6_2atmpS4588;
      struct _M0TPB5ArrayGfE* _M0L2giS4598;
      float _M0L6_2atmpS4593;
      struct _M0TPB5ArrayGfE* _M0L1vS4597;
      float _M0L6_2atmpS4595;
      float _M0L4e__iS4596;
      float _M0L6_2atmpS4594;
      float _M0L6_2atmpS4590;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4592;
      float _M0L6_2atmpS4591;
      float _M0L6_2atmpS4589;
      float _M0L6_2atmpS4587;
      int32_t _M0L6_2atmpS4608;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4602 = _M0MPC15array5Array2atGfE(_M0L2geS4607, _M0L1iS1471);
      _M0L1vS4606 = _M0L1pS1469->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4604 = _M0MPC15array5Array2atGfE(_M0L1vS4606, _M0L1iS1471);
      _M0L4e__eS4605 = _M0L1pS1469->$18;
      _M0L6_2atmpS4603 = _M0L6_2atmpS4604 - _M0L4e__eS4605;
      _M0L6_2atmpS4599 = _M0L6_2atmpS4602 * _M0L6_2atmpS4603;
      _M0L7gsyn__eS4601 = _M0L1pS1469->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4600
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4601, _M0L1iS1471);
      _M0L6_2atmpS4588 = _M0L6_2atmpS4599 * _M0L6_2atmpS4600;
      _M0L2giS4598 = _M0L1pS1469->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4593 = _M0MPC15array5Array2atGfE(_M0L2giS4598, _M0L1iS1471);
      _M0L1vS4597 = _M0L1pS1469->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4595 = _M0MPC15array5Array2atGfE(_M0L1vS4597, _M0L1iS1471);
      _M0L4e__iS4596 = _M0L1pS1469->$19;
      _M0L6_2atmpS4594 = _M0L6_2atmpS4595 - _M0L4e__iS4596;
      _M0L6_2atmpS4590 = _M0L6_2atmpS4593 * _M0L6_2atmpS4594;
      _M0L7gsyn__iS4592 = _M0L1pS1469->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4591
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4592, _M0L1iS1471);
      _M0L6_2atmpS4589 = _M0L6_2atmpS4590 * _M0L6_2atmpS4591;
      _M0L6_2atmpS4587 = _M0L6_2atmpS4588 + _M0L6_2atmpS4589;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4586, _M0L1iS1471, _M0L6_2atmpS4587);
      _M0L6_2atmpS4608 = _M0L1iS1471 + 1;
      _M0L1iS1471 = _M0L6_2atmpS4608;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1460,
  float _M0L2dtS1463
) {
  int32_t _M0L1nS1459;
  int32_t _M0L7_2abindS1461;
  int32_t _M0L1iS1462;
  int32_t _M0L7_2abindS1465;
  int32_t _M0L1iS1466;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1459 = _M0L1pS1460->$2;
  _M0L7_2abindS1461 = 0;
  _M0L1iS1462 = _M0L7_2abindS1461;
  while (1) {
    if (_M0L1iS1462 < _M0L1nS1459) {
      struct _M0TPB5ArrayGfE* _M0L2heS4524 = _M0L1pS1460->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS4529 = _M0L1pS1460->$12;
      float _M0L6_2atmpS4526;
      struct _M0TPB5ArrayGfE* _M0L3gluS4528;
      float _M0L6_2atmpS4527;
      float _M0L6_2atmpS4525;
      struct _M0TPB5ArrayGfE* _M0L2hiS4530;
      struct _M0TPB5ArrayGfE* _M0L2hiS4535;
      float _M0L6_2atmpS4532;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4534;
      float _M0L6_2atmpS4533;
      float _M0L6_2atmpS4531;
      struct _M0TPB5ArrayGfE* _M0L2geS4536;
      struct _M0TPB5ArrayGfE* _M0L2geS4548;
      float _M0L6_2atmpS4538;
      struct _M0TPB5ArrayGfE* _M0L2geS4547;
      float _M0L6_2atmpS4546;
      float _M0L6_2atmpS4544;
      float _M0L3tdeS4545;
      float _M0L6_2atmpS4541;
      struct _M0TPB5ArrayGfE* _M0L2heS4543;
      float _M0L6_2atmpS4542;
      float _M0L6_2atmpS4540;
      float _M0L6_2atmpS4539;
      float _M0L6_2atmpS4537;
      struct _M0TPB5ArrayGfE* _M0L2heS4549;
      struct _M0TPB5ArrayGfE* _M0L2heS4558;
      float _M0L6_2atmpS4551;
      struct _M0TPB5ArrayGfE* _M0L2heS4557;
      float _M0L6_2atmpS4556;
      float _M0L6_2atmpS4554;
      float _M0L3treS4555;
      float _M0L6_2atmpS4553;
      float _M0L6_2atmpS4552;
      float _M0L6_2atmpS4550;
      struct _M0TPB5ArrayGfE* _M0L2giS4559;
      struct _M0TPB5ArrayGfE* _M0L2giS4571;
      float _M0L6_2atmpS4561;
      struct _M0TPB5ArrayGfE* _M0L2giS4570;
      float _M0L6_2atmpS4569;
      float _M0L6_2atmpS4567;
      float _M0L3tdiS4568;
      float _M0L6_2atmpS4564;
      struct _M0TPB5ArrayGfE* _M0L2hiS4566;
      float _M0L6_2atmpS4565;
      float _M0L6_2atmpS4563;
      float _M0L6_2atmpS4562;
      float _M0L6_2atmpS4560;
      struct _M0TPB5ArrayGfE* _M0L2hiS4572;
      struct _M0TPB5ArrayGfE* _M0L2hiS4581;
      float _M0L6_2atmpS4574;
      struct _M0TPB5ArrayGfE* _M0L2hiS4580;
      float _M0L6_2atmpS4579;
      float _M0L6_2atmpS4577;
      float _M0L3triS4578;
      float _M0L6_2atmpS4576;
      float _M0L6_2atmpS4575;
      float _M0L6_2atmpS4573;
      int32_t _M0L6_2atmpS4582;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4526 = _M0MPC15array5Array2atGfE(_M0L2heS4529, _M0L1iS1462);
      _M0L3gluS4528 = _M0L1pS1460->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4527
      = _M0MPC15array5Array2atGfE(_M0L3gluS4528, _M0L1iS1462);
      _M0L6_2atmpS4525 = _M0L6_2atmpS4526 + _M0L6_2atmpS4527;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4524, _M0L1iS1462, _M0L6_2atmpS4525);
      _M0L2hiS4530 = _M0L1pS1460->$13;
      _M0L2hiS4535 = _M0L1pS1460->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4532 = _M0MPC15array5Array2atGfE(_M0L2hiS4535, _M0L1iS1462);
      _M0L4gabaS4534 = _M0L1pS1460->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4533
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4534, _M0L1iS1462);
      _M0L6_2atmpS4531 = _M0L6_2atmpS4532 + _M0L6_2atmpS4533;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4530, _M0L1iS1462, _M0L6_2atmpS4531);
      _M0L2geS4536 = _M0L1pS1460->$10;
      _M0L2geS4548 = _M0L1pS1460->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4538 = _M0MPC15array5Array2atGfE(_M0L2geS4548, _M0L1iS1462);
      _M0L2geS4547 = _M0L1pS1460->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4546 = _M0MPC15array5Array2atGfE(_M0L2geS4547, _M0L1iS1462);
      _M0L6_2atmpS4544 = -_M0L6_2atmpS4546;
      _M0L3tdeS4545 = _M0L1pS1460->$21;
      _M0L6_2atmpS4541 = _M0L6_2atmpS4544 / _M0L3tdeS4545;
      _M0L2heS4543 = _M0L1pS1460->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4542 = _M0MPC15array5Array2atGfE(_M0L2heS4543, _M0L1iS1462);
      _M0L6_2atmpS4540 = _M0L6_2atmpS4541 + _M0L6_2atmpS4542;
      _M0L6_2atmpS4539 = _M0L2dtS1463 * _M0L6_2atmpS4540;
      _M0L6_2atmpS4537 = _M0L6_2atmpS4538 + _M0L6_2atmpS4539;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4536, _M0L1iS1462, _M0L6_2atmpS4537);
      _M0L2heS4549 = _M0L1pS1460->$12;
      _M0L2heS4558 = _M0L1pS1460->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4551 = _M0MPC15array5Array2atGfE(_M0L2heS4558, _M0L1iS1462);
      _M0L2heS4557 = _M0L1pS1460->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4556 = _M0MPC15array5Array2atGfE(_M0L2heS4557, _M0L1iS1462);
      _M0L6_2atmpS4554 = -_M0L6_2atmpS4556;
      _M0L3treS4555 = _M0L1pS1460->$20;
      _M0L6_2atmpS4553 = _M0L6_2atmpS4554 / _M0L3treS4555;
      _M0L6_2atmpS4552 = _M0L2dtS1463 * _M0L6_2atmpS4553;
      _M0L6_2atmpS4550 = _M0L6_2atmpS4551 + _M0L6_2atmpS4552;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4549, _M0L1iS1462, _M0L6_2atmpS4550);
      _M0L2giS4559 = _M0L1pS1460->$11;
      _M0L2giS4571 = _M0L1pS1460->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4561 = _M0MPC15array5Array2atGfE(_M0L2giS4571, _M0L1iS1462);
      _M0L2giS4570 = _M0L1pS1460->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4569 = _M0MPC15array5Array2atGfE(_M0L2giS4570, _M0L1iS1462);
      _M0L6_2atmpS4567 = -_M0L6_2atmpS4569;
      _M0L3tdiS4568 = _M0L1pS1460->$23;
      _M0L6_2atmpS4564 = _M0L6_2atmpS4567 / _M0L3tdiS4568;
      _M0L2hiS4566 = _M0L1pS1460->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4565 = _M0MPC15array5Array2atGfE(_M0L2hiS4566, _M0L1iS1462);
      _M0L6_2atmpS4563 = _M0L6_2atmpS4564 + _M0L6_2atmpS4565;
      _M0L6_2atmpS4562 = _M0L2dtS1463 * _M0L6_2atmpS4563;
      _M0L6_2atmpS4560 = _M0L6_2atmpS4561 + _M0L6_2atmpS4562;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4559, _M0L1iS1462, _M0L6_2atmpS4560);
      _M0L2hiS4572 = _M0L1pS1460->$13;
      _M0L2hiS4581 = _M0L1pS1460->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4574 = _M0MPC15array5Array2atGfE(_M0L2hiS4581, _M0L1iS1462);
      _M0L2hiS4580 = _M0L1pS1460->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4579 = _M0MPC15array5Array2atGfE(_M0L2hiS4580, _M0L1iS1462);
      _M0L6_2atmpS4577 = -_M0L6_2atmpS4579;
      _M0L3triS4578 = _M0L1pS1460->$22;
      _M0L6_2atmpS4576 = _M0L6_2atmpS4577 / _M0L3triS4578;
      _M0L6_2atmpS4575 = _M0L2dtS1463 * _M0L6_2atmpS4576;
      _M0L6_2atmpS4573 = _M0L6_2atmpS4574 + _M0L6_2atmpS4575;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4572, _M0L1iS1462, _M0L6_2atmpS4573);
      _M0L6_2atmpS4582 = _M0L1iS1462 + 1;
      _M0L1iS1462 = _M0L6_2atmpS4582;
      continue;
    }
    break;
  }
  _M0L7_2abindS1465 = 0;
  _M0L1iS1466 = _M0L7_2abindS1465;
  while (1) {
    if (_M0L1iS1466 < _M0L1nS1459) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4583 = _M0L1pS1460->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4584;
      int32_t _M0L6_2atmpS4585;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4583, _M0L1iS1466, 0x0p+0f);
      _M0L4gabaS4584 = _M0L1pS1460->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4584, _M0L1iS1466, 0x0p+0f);
      _M0L6_2atmpS4585 = _M0L1iS1466 + 1;
      _M0L1iS1466 = _M0L6_2atmpS4585;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1435,
  float _M0L6t__nowS1446
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS4523;
  int32_t _M0L6_2atmpS4522;
  int32_t _M0L10use__delayS1434;
  struct _M0TPB5ArrayGfE* _M0L3rhoS4521;
  int32_t _M0L6_2atmpS4520;
  int32_t _M0L8use__rhoS1436;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS4523 = _M0L1cS1435->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4522 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS4523);
  _M0L10use__delayS1434 = _M0L6_2atmpS4522 > 0;
  _M0L3rhoS4521 = _M0L1cS1435->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4520 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS4521);
  _M0L8use__rhoS1436 = _M0L6_2atmpS4520 > 0;
  if (_M0L10use__delayS1434) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4483 = _M0L1cS1435->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS4482 = _M0L3preS4483->$5;
    int32_t _M0L6n__preS1437;
    struct _M0TPB8MutLocalGiE* _M0L1jS1438;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1437 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4482);
    _M0L1jS1438
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1438)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1438->$0 = 0;
    while (1) {
      int32_t _M0L3valS4451 = _M0L1jS1438->$0;
      if (_M0L3valS4451 < _M0L6n__preS1437) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4454 = _M0L1cS1435->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS4452 = _M0L3preS4454->$5;
        int32_t _M0L3valS4453 = _M0L1jS1438->$0;
        int32_t _M0L3valS4481;
        int32_t _M0L6_2atmpS4480;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS4452, _M0L3valS4453)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4479 =
            _M0L1cS1435->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4477 = _M0L6matrixS4479->$2;
          int32_t _M0L3valS4478 = _M0L1jS1438->$0;
          int32_t _M0L5startS1439;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4476;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4473;
          int32_t _M0L3valS4475;
          int32_t _M0L6_2atmpS4474;
          int32_t _M0L3endS1440;
          struct _M0TPB8MutLocalGiE* _M0L1sS1441;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1439
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4477, _M0L3valS4478);
          _M0L6matrixS4476 = _M0L1cS1435->$4;
          _M0L6rowptrS4473 = _M0L6matrixS4476->$2;
          _M0L3valS4475 = _M0L1jS1438->$0;
          _M0L6_2atmpS4474 = _M0L3valS4475 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1440
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4473, _M0L6_2atmpS4474);
          _M0L1sS1441
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1441)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1441->$0 = _M0L5startS1439;
          while (1) {
            int32_t _M0L3valS4455 = _M0L1sS1441->$0;
            if (_M0L3valS4455 < _M0L3endS1440) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4472 =
                _M0L1cS1435->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS4470 = _M0L6matrixS4472->$3;
              int32_t _M0L3valS4471 = _M0L1sS1441->$0;
              int32_t _M0L9post__idxS1442;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4469;
              struct _M0TPB5ArrayGfE* _M0L4valsS4467;
              int32_t _M0L3valS4468;
              float _M0L1wS1443;
              struct _M0TPB5ArrayGfE* _M0L6delaysS4465;
              int32_t _M0L3valS4466;
              float _M0L1dS1444;
              float _M0L9w__scaledS1445;
              int32_t _M0L3valS4461;
              int32_t _M0L6_2atmpS4460;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1442
              = _M0MPC15array5Array2atGiE(_M0L6colptrS4470, _M0L3valS4471);
              _M0L6matrixS4469 = _M0L1cS1435->$4;
              _M0L4valsS4467 = _M0L6matrixS4469->$4;
              _M0L3valS4468 = _M0L1sS1441->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1443
              = _M0MPC15array5Array2atGfE(_M0L4valsS4467, _M0L3valS4468);
              _M0L6delaysS4465 = _M0L1cS1435->$5;
              _M0L3valS4466 = _M0L1sS1441->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1444
              = _M0MPC15array5Array2atGfE(_M0L6delaysS4465, _M0L3valS4466);
              if (_M0L8use__rhoS1436) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS4463 = _M0L1cS1435->$6;
                int32_t _M0L3valS4464 = _M0L1sS1441->$0;
                float _M0L6_2atmpS4462;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4462
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4463, _M0L3valS4464);
                _M0L9w__scaledS1445 = _M0L1wS1443 * _M0L6_2atmpS4462;
              } else {
                _M0L9w__scaledS1445 = _M0L1wS1443;
              }
              if (_M0L1dS1444 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1435, _M0L9post__idxS1442, _M0L9w__scaledS1445);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS4456 =
                  _M0L1cS1435->$7;
                float _M0L6_2atmpS4457 = _M0L6t__nowS1446 + _M0L1dS1444;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS4458;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4459;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS4456, _M0L6_2atmpS4457);
                _M0L14pending__postsS4458 = _M0L1cS1435->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS4458, _M0L9post__idxS1442);
                _M0L16pending__weightsS4459 = _M0L1cS1435->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS4459, _M0L9w__scaledS1445);
              }
              _M0L3valS4461 = _M0L1sS1441->$0;
              _M0L6_2atmpS4460 = _M0L3valS4461 + 1;
              _M0L1sS1441->$0 = _M0L6_2atmpS4460;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1441);
            }
            break;
          }
        }
        _M0L3valS4481 = _M0L1jS1438->$0;
        _M0L6_2atmpS4480 = _M0L3valS4481 + 1;
        _M0L1jS1438->$0 = _M0L6_2atmpS4480;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1438);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS4517 = _M0L1cS1435->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1449;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS4517 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS4517)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS4517, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4517) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4518 = _M0L1cS1435->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5694 = _M0L4postS4518->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS5694);
      _M0L6targetS1449 = _M0L8_2afieldS5694;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4519 = _M0L1cS1435->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5695 = _M0L4postS4519->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS5695);
      _M0L6targetS1449 = _M0L8_2afieldS5695;
    }
    if (_M0L8use__rhoS1436) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4513 = _M0L1cS1435->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4512 = _M0L3preS4513->$5;
      int32_t _M0L6n__preS1450;
      struct _M0TPB8MutLocalGiE* _M0L1jS1451;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1450 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4512);
      _M0L1jS1451
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1451)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1451->$0 = 0;
      while (1) {
        int32_t _M0L3valS4484 = _M0L1jS1451->$0;
        if (_M0L3valS4484 < _M0L6n__preS1450) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4487 = _M0L1cS1435->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS4485 = _M0L3preS4487->$5;
          int32_t _M0L3valS4486 = _M0L1jS1451->$0;
          int32_t _M0L3valS4511;
          int32_t _M0L6_2atmpS4510;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS4485, _M0L3valS4486)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4509 =
              _M0L1cS1435->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4507 = _M0L6matrixS4509->$2;
            int32_t _M0L3valS4508 = _M0L1jS1451->$0;
            int32_t _M0L5startS1452;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4506;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4503;
            int32_t _M0L3valS4505;
            int32_t _M0L6_2atmpS4504;
            int32_t _M0L3endS1453;
            struct _M0TPB8MutLocalGiE* _M0L1sS1454;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1452
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4507, _M0L3valS4508);
            _M0L6matrixS4506 = _M0L1cS1435->$4;
            _M0L6rowptrS4503 = _M0L6matrixS4506->$2;
            _M0L3valS4505 = _M0L1jS1451->$0;
            _M0L6_2atmpS4504 = _M0L3valS4505 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1453
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4503, _M0L6_2atmpS4504);
            _M0L1sS1454
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1454)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1454->$0 = _M0L5startS1452;
            while (1) {
              int32_t _M0L3valS4488 = _M0L1sS1454->$0;
              if (_M0L3valS4488 < _M0L3endS1453) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4502 =
                  _M0L1cS1435->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS4500 =
                  _M0L6matrixS4502->$3;
                int32_t _M0L3valS4501 = _M0L1sS1454->$0;
                int32_t _M0L9post__idxS1455;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4499;
                struct _M0TPB5ArrayGfE* _M0L4valsS4497;
                int32_t _M0L3valS4498;
                float _M0L6_2atmpS4493;
                struct _M0TPB5ArrayGfE* _M0L3rhoS4495;
                int32_t _M0L3valS4496;
                float _M0L6_2atmpS4494;
                float _M0L9w__scaledS1456;
                float _M0L6_2atmpS4490;
                float _M0L6_2atmpS4489;
                int32_t _M0L3valS4492;
                int32_t _M0L6_2atmpS4491;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1455
                = _M0MPC15array5Array2atGiE(_M0L6colptrS4500, _M0L3valS4501);
                _M0L6matrixS4499 = _M0L1cS1435->$4;
                _M0L4valsS4497 = _M0L6matrixS4499->$4;
                _M0L3valS4498 = _M0L1sS1454->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4493
                = _M0MPC15array5Array2atGfE(_M0L4valsS4497, _M0L3valS4498);
                _M0L3rhoS4495 = _M0L1cS1435->$6;
                _M0L3valS4496 = _M0L1sS1454->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4494
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4495, _M0L3valS4496);
                _M0L9w__scaledS1456 = _M0L6_2atmpS4493 * _M0L6_2atmpS4494;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4490
                = _M0MPC15array5Array2atGfE(_M0L6targetS1449, _M0L9post__idxS1455);
                _M0L6_2atmpS4489 = _M0L6_2atmpS4490 + _M0L9w__scaledS1456;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1449, _M0L9post__idxS1455, _M0L6_2atmpS4489);
                _M0L3valS4492 = _M0L1sS1454->$0;
                _M0L6_2atmpS4491 = _M0L3valS4492 + 1;
                _M0L1sS1454->$0 = _M0L6_2atmpS4491;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1454);
              }
              break;
            }
          }
          _M0L3valS4511 = _M0L1jS1451->$0;
          _M0L6_2atmpS4510 = _M0L3valS4511 + 1;
          _M0L1jS1451->$0 = _M0L6_2atmpS4510;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1451);
          moonbit_decref_cycle_free(_M0L6targetS1449);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4514 =
        _M0L1cS1435->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4516 = _M0L1cS1435->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4515 = _M0L3preS4516->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS4514, _M0L4fireS4515, _M0L6targetS1449);
      moonbit_decref_cycle_free(_M0L6targetS1449);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1427,
  float _M0L6t__nowS1430
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS4450;
  int32_t _M0L1nS1426;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1428;
  struct _M0TPB8MutLocalGiE* _M0L1kS1429;
  int32_t _M0L3valS4449;
  int32_t _M0L6_2atmpS4448;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1432;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS4450 = _M0L1cS1427->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1426 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS4450);
  if (_M0L1nS1426 == 0) {
    return 0;
  }
  _M0L4keptS1428
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1428)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1428->$0 = 0;
  _M0L1kS1429
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1429)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1429->$0 = 0;
  while (1) {
    int32_t _M0L3valS4411 = _M0L1kS1429->$0;
    if (_M0L3valS4411 < _M0L1nS1426) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4413 = _M0L1cS1427->$7;
      int32_t _M0L3valS4414 = _M0L1kS1429->$0;
      float _M0L6_2atmpS4412;
      int32_t _M0L3valS4441;
      int32_t _M0L6_2atmpS4440;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS4412
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4413, _M0L3valS4414);
      if (_M0L6_2atmpS4412 <= _M0L6t__nowS1430) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS4419 = _M0L1cS1427->$8;
        int32_t _M0L3valS4420 = _M0L1kS1429->$0;
        int32_t _M0L6_2atmpS4415;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4417;
        int32_t _M0L3valS4418;
        float _M0L6_2atmpS4416;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4415
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4419, _M0L3valS4420);
        _M0L16pending__weightsS4417 = _M0L1cS1427->$9;
        _M0L3valS4418 = _M0L1kS1429->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4416
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4417, _M0L3valS4418);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1427, _M0L6_2atmpS4415, _M0L6_2atmpS4416);
      } else {
        int32_t _M0L3valS4421 = _M0L4keptS1428->$0;
        int32_t _M0L3valS4422 = _M0L1kS1429->$0;
        int32_t _M0L3valS4439;
        int32_t _M0L6_2atmpS4438;
        if (_M0L3valS4421 != _M0L3valS4422) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4423 = _M0L1cS1427->$7;
          int32_t _M0L3valS4424 = _M0L4keptS1428->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4426 = _M0L1cS1427->$7;
          int32_t _M0L3valS4427 = _M0L1kS1429->$0;
          float _M0L6_2atmpS4425;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4428;
          int32_t _M0L3valS4429;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4431;
          int32_t _M0L3valS4432;
          int32_t _M0L6_2atmpS4430;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4433;
          int32_t _M0L3valS4434;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4436;
          int32_t _M0L3valS4437;
          float _M0L6_2atmpS4435;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4425
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4426, _M0L3valS4427);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS4423, _M0L3valS4424, _M0L6_2atmpS4425);
          _M0L14pending__postsS4428 = _M0L1cS1427->$8;
          _M0L3valS4429 = _M0L4keptS1428->$0;
          _M0L14pending__postsS4431 = _M0L1cS1427->$8;
          _M0L3valS4432 = _M0L1kS1429->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4430
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4431, _M0L3valS4432);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS4428, _M0L3valS4429, _M0L6_2atmpS4430);
          _M0L16pending__weightsS4433 = _M0L1cS1427->$9;
          _M0L3valS4434 = _M0L4keptS1428->$0;
          _M0L16pending__weightsS4436 = _M0L1cS1427->$9;
          _M0L3valS4437 = _M0L1kS1429->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4435
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4436, _M0L3valS4437);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS4433, _M0L3valS4434, _M0L6_2atmpS4435);
        }
        _M0L3valS4439 = _M0L4keptS1428->$0;
        _M0L6_2atmpS4438 = _M0L3valS4439 + 1;
        _M0L4keptS1428->$0 = _M0L6_2atmpS4438;
      }
      _M0L3valS4441 = _M0L1kS1429->$0;
      _M0L6_2atmpS4440 = _M0L3valS4441 + 1;
      _M0L1kS1429->$0 = _M0L6_2atmpS4440;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1429);
    }
    break;
  }
  _M0L3valS4449 = _M0L4keptS1428->$0;
  moonbit_decref_cycle_free(_M0L4keptS1428);
  _M0L6_2atmpS4448 = _M0L1nS1426 - _M0L3valS4449;
  _M0L4dropS1432
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1432)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1432->$0 = _M0L6_2atmpS4448;
  while (1) {
    int32_t _M0L3valS4442 = _M0L4dropS1432->$0;
    if (_M0L3valS4442 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4443 = _M0L1cS1427->$7;
      void* _M0L6_2atmpS5697;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS4444;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4445;
      void* _M0L6_2atmpS5696;
      int32_t _M0L3valS4447;
      int32_t _M0L6_2atmpS4446;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5697
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS4443);
      moonbit_decref_cycle_free(_M0L6_2atmpS5697);
      _M0L14pending__postsS4444 = _M0L1cS1427->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS4444);
      _M0L16pending__weightsS4445 = _M0L1cS1427->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5696
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS4445);
      moonbit_decref_cycle_free(_M0L6_2atmpS5696);
      _M0L3valS4447 = _M0L4dropS1432->$0;
      _M0L6_2atmpS4446 = _M0L3valS4447 - 1;
      _M0L4dropS1432->$0 = _M0L6_2atmpS4446;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS1432);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1423,
  int32_t _M0L9post__idxS1424,
  float _M0L1wS1425
) {
  moonbit_string_t _M0L3symS4398;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS4398 = _M0L1cS1423->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS4398 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS4398)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS4398, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4398) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4404 = _M0L1cS1423->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4399 = _M0L4postS4404->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4403 = _M0L1cS1423->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4402 = _M0L4postS4403->$13;
    float _M0L6_2atmpS4401;
    float _M0L6_2atmpS4400;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4401
    = _M0MPC15array5Array2atGfE(_M0L3gluS4402, _M0L9post__idxS1424);
    _M0L6_2atmpS4400 = _M0L6_2atmpS4401 + _M0L1wS1425;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS4399, _M0L9post__idxS1424, _M0L6_2atmpS4400);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4410 = _M0L1cS1423->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4405 = _M0L4postS4410->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4409 = _M0L1cS1423->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4408 = _M0L4postS4409->$14;
    float _M0L6_2atmpS4407;
    float _M0L6_2atmpS4406;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4407
    = _M0MPC15array5Array2atGfE(_M0L4gabaS4408, _M0L9post__idxS1424);
    _M0L6_2atmpS4406 = _M0L6_2atmpS4407 + _M0L1wS1425;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS4405, _M0L9post__idxS1424, _M0L6_2atmpS4406);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1420,
  float _M0L1tS1422
) {
  int32_t _M0L11step__countS4384;
  int32_t _M0L6_2atmpS4383;
  int32_t _M0L11step__countS4386;
  int32_t _M0L9rec__stepS4387;
  int32_t _M0L6_2atmpS4385;
  moonbit_string_t _M0L3symS4390;
  float _M0L1vS1421;
  struct _M0TPB5ArrayGfE* _M0L4dataS4388;
  struct _M0TPB5ArrayGfE* _M0L5timesS4389;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS4384 = _M0L1mS1420->$6;
  _M0L6_2atmpS4383 = _M0L11step__countS4384 + 1;
  _M0L1mS1420->$6 = _M0L6_2atmpS4383;
  _M0L11step__countS4386 = _M0L1mS1420->$6;
  _M0L9rec__stepS4387 = _M0L1mS1420->$5;
  _M0L6_2atmpS4385 = _M0L11step__countS4386 % _M0L9rec__stepS4387;
  if (_M0L6_2atmpS4385 != 0) {
    return 0;
  }
  _M0L3symS4390 = _M0L1mS1420->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS4390 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS4390)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS4390, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS4390) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4393 = _M0L1mS1420->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS4391 = _M0L3popS4393->$3;
    int32_t _M0L6neuronS4392 = _M0L1mS1420->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1421 = _M0MPC15array5Array2atGfE(_M0L1vS4391, _M0L6neuronS4392);
  } else {
    moonbit_string_t _M0L3symS4394 = _M0L1mS1420->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS4394 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS4394)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS4394, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS4394) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4397 = _M0L1mS1420->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4395 = _M0L3popS4397->$5;
      int32_t _M0L6neuronS4396 = _M0L1mS1420->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4395, _M0L6neuronS4396)) {
        _M0L1vS1421 = 0x1p+0f;
      } else {
        _M0L1vS1421 = 0x0p+0f;
      }
    } else {
      _M0L1vS1421 = 0x0p+0f;
    }
  }
  _M0L4dataS4388 = _M0L1mS1420->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS4388, _M0L1vS1421);
  _M0L5timesS4389 = _M0L1mS1420->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS4389, _M0L1tS1422);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1418,
  int32_t _M0L6neuronS1419
) {
  float* _M0L6_2atmpS4382;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4379;
  float* _M0L6_2atmpS4381;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4380;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_6012;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS4382 = moonbit_empty_float_array;
  _M0L6_2atmpS4379
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4379)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4379->$0 = _M0L6_2atmpS4382;
  _M0L6_2atmpS4379->$1 = 0;
  _M0L6_2atmpS4381 = moonbit_empty_float_array;
  _M0L6_2atmpS4380
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4380)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4380->$0 = _M0L6_2atmpS4381;
  _M0L6_2atmpS4380->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1418);
  _block_6012
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_6012)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 75, 0);
  _block_6012->$0 = _M0L3popS1418;
  _block_6012->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_6012->$2 = _M0L6_2atmpS4379;
  _block_6012->$3 = _M0L6_2atmpS4380;
  _block_6012->$4 = _M0L6neuronS1419;
  _block_6012->$5 = 1;
  _block_6012->$6 = 0;
  return _block_6012;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1414
) {
  int32_t _M0L1nS1413;
  int32_t _M0L7_2abindS1415;
  int32_t _M0L1iS1416;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1413 = _M0L1pS1414->$2;
  _M0L7_2abindS1415 = 0;
  _M0L1iS1416 = _M0L7_2abindS1415;
  while (1) {
    if (_M0L1iS1416 < _M0L1nS1413) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4356 = _M0L1pS1414->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS4377 = _M0L1pS1414->$9;
      float _M0L6_2atmpS4372;
      struct _M0TPB5ArrayGfE* _M0L1vS4376;
      float _M0L6_2atmpS4374;
      float _M0L4e__eS4375;
      float _M0L6_2atmpS4373;
      float _M0L6_2atmpS4369;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4371;
      float _M0L6_2atmpS4370;
      float _M0L6_2atmpS4358;
      struct _M0TPB5ArrayGfE* _M0L2giS4368;
      float _M0L6_2atmpS4363;
      struct _M0TPB5ArrayGfE* _M0L1vS4367;
      float _M0L6_2atmpS4365;
      float _M0L4e__iS4366;
      float _M0L6_2atmpS4364;
      float _M0L6_2atmpS4360;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4362;
      float _M0L6_2atmpS4361;
      float _M0L6_2atmpS4359;
      float _M0L6_2atmpS4357;
      int32_t _M0L6_2atmpS4378;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4372 = _M0MPC15array5Array2atGfE(_M0L2geS4377, _M0L1iS1416);
      _M0L1vS4376 = _M0L1pS1414->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4374 = _M0MPC15array5Array2atGfE(_M0L1vS4376, _M0L1iS1416);
      _M0L4e__eS4375 = _M0L1pS1414->$17;
      _M0L6_2atmpS4373 = _M0L6_2atmpS4374 - _M0L4e__eS4375;
      _M0L6_2atmpS4369 = _M0L6_2atmpS4372 * _M0L6_2atmpS4373;
      _M0L7gsyn__eS4371 = _M0L1pS1414->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4370
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4371, _M0L1iS1416);
      _M0L6_2atmpS4358 = _M0L6_2atmpS4369 * _M0L6_2atmpS4370;
      _M0L2giS4368 = _M0L1pS1414->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4363 = _M0MPC15array5Array2atGfE(_M0L2giS4368, _M0L1iS1416);
      _M0L1vS4367 = _M0L1pS1414->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4365 = _M0MPC15array5Array2atGfE(_M0L1vS4367, _M0L1iS1416);
      _M0L4e__iS4366 = _M0L1pS1414->$18;
      _M0L6_2atmpS4364 = _M0L6_2atmpS4365 - _M0L4e__iS4366;
      _M0L6_2atmpS4360 = _M0L6_2atmpS4363 * _M0L6_2atmpS4364;
      _M0L7gsyn__iS4362 = _M0L1pS1414->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4361
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4362, _M0L1iS1416);
      _M0L6_2atmpS4359 = _M0L6_2atmpS4360 * _M0L6_2atmpS4361;
      _M0L6_2atmpS4357 = _M0L6_2atmpS4358 + _M0L6_2atmpS4359;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4356, _M0L1iS1416, _M0L6_2atmpS4357);
      _M0L6_2atmpS4378 = _M0L1iS1416 + 1;
      _M0L1iS1416 = _M0L6_2atmpS4378;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1405,
  float _M0L2dtS1408
) {
  int32_t _M0L1nS1404;
  int32_t _M0L7_2abindS1406;
  int32_t _M0L1iS1407;
  int32_t _M0L7_2abindS1410;
  int32_t _M0L1iS1411;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1404 = _M0L1pS1405->$2;
  _M0L7_2abindS1406 = 0;
  _M0L1iS1407 = _M0L7_2abindS1406;
  while (1) {
    if (_M0L1iS1407 < _M0L1nS1404) {
      struct _M0TPB5ArrayGfE* _M0L2heS4294 = _M0L1pS1405->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS4299 = _M0L1pS1405->$11;
      float _M0L6_2atmpS4296;
      struct _M0TPB5ArrayGfE* _M0L3gluS4298;
      float _M0L6_2atmpS4297;
      float _M0L6_2atmpS4295;
      struct _M0TPB5ArrayGfE* _M0L2hiS4300;
      struct _M0TPB5ArrayGfE* _M0L2hiS4305;
      float _M0L6_2atmpS4302;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4304;
      float _M0L6_2atmpS4303;
      float _M0L6_2atmpS4301;
      struct _M0TPB5ArrayGfE* _M0L2geS4306;
      struct _M0TPB5ArrayGfE* _M0L2geS4318;
      float _M0L6_2atmpS4308;
      struct _M0TPB5ArrayGfE* _M0L2geS4317;
      float _M0L6_2atmpS4316;
      float _M0L6_2atmpS4314;
      float _M0L3tdeS4315;
      float _M0L6_2atmpS4311;
      struct _M0TPB5ArrayGfE* _M0L2heS4313;
      float _M0L6_2atmpS4312;
      float _M0L6_2atmpS4310;
      float _M0L6_2atmpS4309;
      float _M0L6_2atmpS4307;
      struct _M0TPB5ArrayGfE* _M0L2heS4319;
      struct _M0TPB5ArrayGfE* _M0L2heS4328;
      float _M0L6_2atmpS4321;
      struct _M0TPB5ArrayGfE* _M0L2heS4327;
      float _M0L6_2atmpS4326;
      float _M0L6_2atmpS4324;
      float _M0L3treS4325;
      float _M0L6_2atmpS4323;
      float _M0L6_2atmpS4322;
      float _M0L6_2atmpS4320;
      struct _M0TPB5ArrayGfE* _M0L2giS4329;
      struct _M0TPB5ArrayGfE* _M0L2giS4341;
      float _M0L6_2atmpS4331;
      struct _M0TPB5ArrayGfE* _M0L2giS4340;
      float _M0L6_2atmpS4339;
      float _M0L6_2atmpS4337;
      float _M0L3tdiS4338;
      float _M0L6_2atmpS4334;
      struct _M0TPB5ArrayGfE* _M0L2hiS4336;
      float _M0L6_2atmpS4335;
      float _M0L6_2atmpS4333;
      float _M0L6_2atmpS4332;
      float _M0L6_2atmpS4330;
      struct _M0TPB5ArrayGfE* _M0L2hiS4342;
      struct _M0TPB5ArrayGfE* _M0L2hiS4351;
      float _M0L6_2atmpS4344;
      struct _M0TPB5ArrayGfE* _M0L2hiS4350;
      float _M0L6_2atmpS4349;
      float _M0L6_2atmpS4347;
      float _M0L3triS4348;
      float _M0L6_2atmpS4346;
      float _M0L6_2atmpS4345;
      float _M0L6_2atmpS4343;
      int32_t _M0L6_2atmpS4352;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4296 = _M0MPC15array5Array2atGfE(_M0L2heS4299, _M0L1iS1407);
      _M0L3gluS4298 = _M0L1pS1405->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4297
      = _M0MPC15array5Array2atGfE(_M0L3gluS4298, _M0L1iS1407);
      _M0L6_2atmpS4295 = _M0L6_2atmpS4296 + _M0L6_2atmpS4297;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4294, _M0L1iS1407, _M0L6_2atmpS4295);
      _M0L2hiS4300 = _M0L1pS1405->$12;
      _M0L2hiS4305 = _M0L1pS1405->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4302 = _M0MPC15array5Array2atGfE(_M0L2hiS4305, _M0L1iS1407);
      _M0L4gabaS4304 = _M0L1pS1405->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4303
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4304, _M0L1iS1407);
      _M0L6_2atmpS4301 = _M0L6_2atmpS4302 + _M0L6_2atmpS4303;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4300, _M0L1iS1407, _M0L6_2atmpS4301);
      _M0L2geS4306 = _M0L1pS1405->$9;
      _M0L2geS4318 = _M0L1pS1405->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4308 = _M0MPC15array5Array2atGfE(_M0L2geS4318, _M0L1iS1407);
      _M0L2geS4317 = _M0L1pS1405->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4316 = _M0MPC15array5Array2atGfE(_M0L2geS4317, _M0L1iS1407);
      _M0L6_2atmpS4314 = -_M0L6_2atmpS4316;
      _M0L3tdeS4315 = _M0L1pS1405->$20;
      _M0L6_2atmpS4311 = _M0L6_2atmpS4314 / _M0L3tdeS4315;
      _M0L2heS4313 = _M0L1pS1405->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4312 = _M0MPC15array5Array2atGfE(_M0L2heS4313, _M0L1iS1407);
      _M0L6_2atmpS4310 = _M0L6_2atmpS4311 + _M0L6_2atmpS4312;
      _M0L6_2atmpS4309 = _M0L2dtS1408 * _M0L6_2atmpS4310;
      _M0L6_2atmpS4307 = _M0L6_2atmpS4308 + _M0L6_2atmpS4309;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4306, _M0L1iS1407, _M0L6_2atmpS4307);
      _M0L2heS4319 = _M0L1pS1405->$11;
      _M0L2heS4328 = _M0L1pS1405->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4321 = _M0MPC15array5Array2atGfE(_M0L2heS4328, _M0L1iS1407);
      _M0L2heS4327 = _M0L1pS1405->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4326 = _M0MPC15array5Array2atGfE(_M0L2heS4327, _M0L1iS1407);
      _M0L6_2atmpS4324 = -_M0L6_2atmpS4326;
      _M0L3treS4325 = _M0L1pS1405->$19;
      _M0L6_2atmpS4323 = _M0L6_2atmpS4324 / _M0L3treS4325;
      _M0L6_2atmpS4322 = _M0L2dtS1408 * _M0L6_2atmpS4323;
      _M0L6_2atmpS4320 = _M0L6_2atmpS4321 + _M0L6_2atmpS4322;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4319, _M0L1iS1407, _M0L6_2atmpS4320);
      _M0L2giS4329 = _M0L1pS1405->$10;
      _M0L2giS4341 = _M0L1pS1405->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4331 = _M0MPC15array5Array2atGfE(_M0L2giS4341, _M0L1iS1407);
      _M0L2giS4340 = _M0L1pS1405->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4339 = _M0MPC15array5Array2atGfE(_M0L2giS4340, _M0L1iS1407);
      _M0L6_2atmpS4337 = -_M0L6_2atmpS4339;
      _M0L3tdiS4338 = _M0L1pS1405->$22;
      _M0L6_2atmpS4334 = _M0L6_2atmpS4337 / _M0L3tdiS4338;
      _M0L2hiS4336 = _M0L1pS1405->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4335 = _M0MPC15array5Array2atGfE(_M0L2hiS4336, _M0L1iS1407);
      _M0L6_2atmpS4333 = _M0L6_2atmpS4334 + _M0L6_2atmpS4335;
      _M0L6_2atmpS4332 = _M0L2dtS1408 * _M0L6_2atmpS4333;
      _M0L6_2atmpS4330 = _M0L6_2atmpS4331 + _M0L6_2atmpS4332;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4329, _M0L1iS1407, _M0L6_2atmpS4330);
      _M0L2hiS4342 = _M0L1pS1405->$12;
      _M0L2hiS4351 = _M0L1pS1405->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4344 = _M0MPC15array5Array2atGfE(_M0L2hiS4351, _M0L1iS1407);
      _M0L2hiS4350 = _M0L1pS1405->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4349 = _M0MPC15array5Array2atGfE(_M0L2hiS4350, _M0L1iS1407);
      _M0L6_2atmpS4347 = -_M0L6_2atmpS4349;
      _M0L3triS4348 = _M0L1pS1405->$21;
      _M0L6_2atmpS4346 = _M0L6_2atmpS4347 / _M0L3triS4348;
      _M0L6_2atmpS4345 = _M0L2dtS1408 * _M0L6_2atmpS4346;
      _M0L6_2atmpS4343 = _M0L6_2atmpS4344 + _M0L6_2atmpS4345;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4342, _M0L1iS1407, _M0L6_2atmpS4343);
      _M0L6_2atmpS4352 = _M0L1iS1407 + 1;
      _M0L1iS1407 = _M0L6_2atmpS4352;
      continue;
    }
    break;
  }
  _M0L7_2abindS1410 = 0;
  _M0L1iS1411 = _M0L7_2abindS1410;
  while (1) {
    if (_M0L1iS1411 < _M0L1nS1404) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4353 = _M0L1pS1405->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4354;
      int32_t _M0L6_2atmpS4355;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4353, _M0L1iS1411, 0x0p+0f);
      _M0L4gabaS4354 = _M0L1pS1405->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4354, _M0L1iS1411, 0x0p+0f);
      _M0L6_2atmpS4355 = _M0L1iS1411 + 1;
      _M0L1iS1411 = _M0L6_2atmpS4355;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1390,
  float _M0L2dtS1399
) {
  int32_t _M0L1nS1389;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1391;
  float _M0L2tmS1392;
  float _M0L2elS1393;
  float _M0L1rS1394;
  float _M0L2vtS1395;
  float _M0L2vrS1396;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS4293;
  float _M0L11tabs__constS1397;
  float _M0L6_2atmpS4292;
  int32_t _M0L11tabs__stepsS1398;
  int32_t _M0L7_2abindS1400;
  int32_t _M0L1iS1401;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1389 = _M0L1pS1390->$2;
  _M0L3p__S1391 = _M0L1pS1390->$0;
  _M0L2tmS1392 = _M0L3p__S1391->$2;
  _M0L2elS1393 = _M0L3p__S1391->$5;
  _M0L1rS1394 = _M0L3p__S1391->$6;
  _M0L2vtS1395 = _M0L3p__S1391->$3;
  _M0L2vrS1396 = _M0L3p__S1391->$4;
  _M0L5spikeS4293 = _M0L1pS1390->$1;
  _M0L11tabs__constS1397 = _M0L5spikeS4293->$0;
  _M0L6_2atmpS4292 = _M0L11tabs__constS1397 / _M0L2dtS1399;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1398 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4292);
  _M0L7_2abindS1400 = 0;
  _M0L1iS1401 = _M0L7_2abindS1400;
  while (1) {
    if (_M0L1iS1401 < _M0L1nS1389) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4252 = _M0L1pS1390->$6;
      int32_t _M0L6_2atmpS4251;
      struct _M0TPB5ArrayGfE* _M0L1vS4258;
      struct _M0TPB5ArrayGfE* _M0L1vS4279;
      float _M0L6_2atmpS4260;
      float _M0L6_2atmpS4262;
      struct _M0TPB5ArrayGfE* _M0L1vS4278;
      float _M0L6_2atmpS4277;
      float _M0L6_2atmpS4276;
      float _M0L6_2atmpS4268;
      struct _M0TPB5ArrayGfE* _M0L1wS4275;
      float _M0L6_2atmpS4274;
      float _M0L6_2atmpS4271;
      struct _M0TPB5ArrayGfE* _M0L1iS4273;
      float _M0L6_2atmpS4272;
      float _M0L6_2atmpS4270;
      float _M0L6_2atmpS4269;
      float _M0L6_2atmpS4264;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4267;
      float _M0L6_2atmpS4266;
      float _M0L6_2atmpS4265;
      float _M0L6_2atmpS4263;
      float _M0L6_2atmpS4261;
      float _M0L6_2atmpS4259;
      struct _M0TPB5ArrayGbE* _M0L4fireS4280;
      struct _M0TPB5ArrayGfE* _M0L1vS4283;
      float _M0L6_2atmpS4282;
      int32_t _M0L6_2atmpS4281;
      struct _M0TPB5ArrayGfE* _M0L1vS4284;
      struct _M0TPB5ArrayGbE* _M0L4fireS4286;
      float _M0L6_2atmpS4285;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4288;
      struct _M0TPB5ArrayGbE* _M0L4fireS4290;
      int32_t _M0L6_2atmpS4289;
      int32_t _M0L6_2atmpS4250;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4251
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4252, _M0L1iS1401);
      if (_M0L6_2atmpS4251 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4253 = _M0L1pS1390->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4254;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4257;
        int32_t _M0L6_2atmpS4256;
        int32_t _M0L6_2atmpS4255;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4253, _M0L1iS1401, 0);
        _M0L4tabsS4254 = _M0L1pS1390->$6;
        _M0L4tabsS4257 = _M0L1pS1390->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4256
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4257, _M0L1iS1401);
        _M0L6_2atmpS4255 = _M0L6_2atmpS4256 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4254, _M0L1iS1401, _M0L6_2atmpS4255);
        goto join_1402;
      }
      _M0L1vS4258 = _M0L1pS1390->$3;
      _M0L1vS4279 = _M0L1pS1390->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4260 = _M0MPC15array5Array2atGfE(_M0L1vS4279, _M0L1iS1401);
      _M0L6_2atmpS4262 = _M0L2dtS1399 / _M0L2tmS1392;
      _M0L1vS4278 = _M0L1pS1390->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4277 = _M0MPC15array5Array2atGfE(_M0L1vS4278, _M0L1iS1401);
      _M0L6_2atmpS4276 = _M0L6_2atmpS4277 - _M0L2elS1393;
      _M0L6_2atmpS4268 = -_M0L6_2atmpS4276;
      _M0L1wS4275 = _M0L1pS1390->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4274 = _M0MPC15array5Array2atGfE(_M0L1wS4275, _M0L1iS1401);
      _M0L6_2atmpS4271 = -_M0L6_2atmpS4274;
      _M0L1iS4273 = _M0L1pS1390->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4272 = _M0MPC15array5Array2atGfE(_M0L1iS4273, _M0L1iS1401);
      _M0L6_2atmpS4270 = _M0L6_2atmpS4271 + _M0L6_2atmpS4272;
      _M0L6_2atmpS4269 = _M0L1rS1394 * _M0L6_2atmpS4270;
      _M0L6_2atmpS4264 = _M0L6_2atmpS4268 + _M0L6_2atmpS4269;
      _M0L9syn__currS4267 = _M0L1pS1390->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4266
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4267, _M0L1iS1401);
      _M0L6_2atmpS4265 = _M0L1rS1394 * _M0L6_2atmpS4266;
      _M0L6_2atmpS4263 = _M0L6_2atmpS4264 - _M0L6_2atmpS4265;
      _M0L6_2atmpS4261 = _M0L6_2atmpS4262 * _M0L6_2atmpS4263;
      _M0L6_2atmpS4259 = _M0L6_2atmpS4260 + _M0L6_2atmpS4261;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4258, _M0L1iS1401, _M0L6_2atmpS4259);
      _M0L4fireS4280 = _M0L1pS1390->$5;
      _M0L1vS4283 = _M0L1pS1390->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4282 = _M0MPC15array5Array2atGfE(_M0L1vS4283, _M0L1iS1401);
      _M0L6_2atmpS4281 = _M0L6_2atmpS4282 > _M0L2vtS1395;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4280, _M0L1iS1401, _M0L6_2atmpS4281);
      _M0L1vS4284 = _M0L1pS1390->$3;
      _M0L4fireS4286 = _M0L1pS1390->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4286, _M0L1iS1401)) {
        _M0L6_2atmpS4285 = _M0L2vrS1396;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4287 = _M0L1pS1390->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4285
        = _M0MPC15array5Array2atGfE(_M0L1vS4287, _M0L1iS1401);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4284, _M0L1iS1401, _M0L6_2atmpS4285);
      _M0L4tabsS4288 = _M0L1pS1390->$6;
      _M0L4fireS4290 = _M0L1pS1390->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4290, _M0L1iS1401)) {
        _M0L6_2atmpS4289 = _M0L11tabs__stepsS1398;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4291 = _M0L1pS1390->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4289
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4291, _M0L1iS1401);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4288, _M0L1iS1401, _M0L6_2atmpS4289);
      goto join_1402;
      goto joinlet_6017;
      join_1402:;
      _M0L6_2atmpS4250 = _M0L1iS1401 + 1;
      _M0L1iS1401 = _M0L6_2atmpS4250;
      continue;
      joinlet_6017:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1377,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1380,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1386
) {
  int32_t _M0L4rowsS1376;
  int32_t _M0L7_2abindS1378;
  int32_t _M0L1iS1379;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1376 = _M0L1mS1377->$0;
  _M0L7_2abindS1378 = 0;
  _M0L1iS1379 = _M0L7_2abindS1378;
  while (1) {
    if (_M0L1iS1379 < _M0L4rowsS1376) {
      int32_t _M0L6_2atmpS4249;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1380, _M0L1iS1379)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4248 = _M0L1mS1377->$2;
        int32_t _M0L5startS1381;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4246;
        int32_t _M0L6_2atmpS4247;
        int32_t _M0L3endS1382;
        int32_t _M0L1kS1383;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1381
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4248, _M0L1iS1379);
        _M0L6rowptrS4246 = _M0L1mS1377->$2;
        _M0L6_2atmpS4247 = _M0L1iS1379 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1382
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4246, _M0L6_2atmpS4247);
        _M0L1kS1383 = _M0L5startS1381;
        while (1) {
          if (_M0L1kS1383 < _M0L3endS1382) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS4244 = _M0L1mS1377->$3;
            int32_t _M0L9post__idxS1384;
            struct _M0TPB5ArrayGfE* _M0L4valsS4243;
            float _M0L1wS1385;
            float _M0L6_2atmpS4242;
            float _M0L6_2atmpS4241;
            int32_t _M0L6_2atmpS4245;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1384
            = _M0MPC15array5Array2atGiE(_M0L6colptrS4244, _M0L1kS1383);
            _M0L4valsS4243 = _M0L1mS1377->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1385
            = _M0MPC15array5Array2atGfE(_M0L4valsS4243, _M0L1kS1383);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS4242
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1386, _M0L9post__idxS1384);
            _M0L6_2atmpS4241 = _M0L6_2atmpS4242 + _M0L1wS1385;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1386, _M0L9post__idxS1384, _M0L6_2atmpS4241);
            _M0L6_2atmpS4245 = _M0L1kS1383 + 1;
            _M0L1kS1383 = _M0L6_2atmpS4245;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS4249 = _M0L1iS1379 + 1;
      _M0L1iS1379 = _M0L6_2atmpS4249;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS1370,
  int32_t _M0L4colsS1371,
  float _M0L2muS1372,
  float _M0L5sigmaS1373,
  float _M0L1pS1374,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1375
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS1370, _M0L4colsS1371, _M0L2muS1372, _M0L5sigmaS1373, _M0L1pS1374, 0, _M0L3rngS1375);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS1284,
  int32_t _M0L4colsS1288,
  float _M0L2muS1294,
  float _M0L5sigmaS1295,
  float _M0L1pS1307,
  int32_t _M0L4ruleS1301,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1297
) {
  float* _M0L6_2atmpS4240;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4239;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS1283;
  int32_t _M0L7_2abindS1285;
  int32_t _M0L1iS1286;
  int32_t _M0L6_2atmpS4238;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1360;
  int32_t* _M0L6_2atmpS4237;
  struct _M0TPB5ArrayGiE* _M0L6colptrS1361;
  float* _M0L6_2atmpS4236;
  struct _M0TPB5ArrayGfE* _M0L4valsS1362;
  int32_t _M0L7_2abindS1363;
  int32_t _M0L1iS1364;
  int32_t _M0L6_2atmpS4235;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_6039;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS4240 = moonbit_empty_float_array;
  _M0L6_2atmpS4239
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4239)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4239->$0 = _M0L6_2atmpS4240;
  _M0L6_2atmpS4239->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS1283
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS1284, _M0L6_2atmpS4239);
  _M0L7_2abindS1285 = 0;
  _M0L1iS1286 = _M0L7_2abindS1285;
  while (1) {
    if (_M0L1iS1286 < _M0L4rowsS1284) {
      struct _M0TPB5ArrayGfE* _M0L3rowS1287;
      int32_t _M0L7_2abindS1289;
      int32_t _M0L1jS1290;
      int32_t _M0L6_2atmpS4191;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS1287 = _M0MPC15array5Array4makeGfE(_M0L4colsS1288, 0x0p+0f);
      _M0L7_2abindS1289 = 0;
      _M0L1jS1290 = _M0L7_2abindS1289;
      while (1) {
        if (_M0L1jS1290 < _M0L4colsS1288) {
          double _M0L2z1S1292;
          struct _M0TUddE* _M0L7_2abindS1296;
          double _M0L5_2az1S1298;
          float _M0L6_2atmpS4189;
          float _M0L6_2atmpS4188;
          float _M0L1wS1293;
          int32_t _M0L6_2atmpS4190;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS1296
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS1297);
          _M0L5_2az1S1298 = _M0L7_2abindS1296->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS1296);
          _M0L2z1S1292 = _M0L5_2az1S1298;
          goto join_1291;
          goto joinlet_6022;
          join_1291:;
          _M0L6_2atmpS4189 = (float)_M0L2z1S1292;
          _M0L6_2atmpS4188 = _M0L5sigmaS1295 * _M0L6_2atmpS4189;
          _M0L1wS1293 = _M0L2muS1294 + _M0L6_2atmpS4188;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS1287, _M0L1jS1290, _M0L1wS1293);
          joinlet_6022:;
          _M0L6_2atmpS4190 = _M0L1jS1290 + 1;
          _M0L1jS1290 = _M0L6_2atmpS4190;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS1283, _M0L1iS1286, _M0L3rowS1287);
      _M0L6_2atmpS4191 = _M0L1iS1286 + 1;
      _M0L1iS1286 = _M0L6_2atmpS4191;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS1301) {
    case 0: {
      int32_t _M0L7_2abindS1302 = 0;
      int32_t _M0L1iS1303 = _M0L7_2abindS1302;
      while (1) {
        if (_M0L1iS1303 < _M0L4rowsS1284) {
          int32_t _M0L7_2abindS1304 = 0;
          int32_t _M0L1jS1305 = _M0L7_2abindS1304;
          int32_t _M0L6_2atmpS4194;
          while (1) {
            if (_M0L1jS1305 < _M0L4colsS1288) {
              float _M0L1uS1306;
              int32_t _M0L6_2atmpS4193;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS1306 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1297);
              if (_M0L1uS1306 >= _M0L1pS1307) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4192;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4192
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1283, _M0L1iS1303);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4192, _M0L1jS1305, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4192);
              }
              _M0L6_2atmpS4193 = _M0L1jS1305 + 1;
              _M0L1jS1305 = _M0L6_2atmpS4193;
              continue;
            }
            break;
          }
          _M0L6_2atmpS4194 = _M0L1iS1303 + 1;
          _M0L1iS1303 = _M0L6_2atmpS4194;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS4212 = (float)_M0L4rowsS1284;
      float _M0L6_2atmpS4211 = _M0L6_2atmpS4212 * _M0L1pS1307;
      int32_t _M0L7n__keepS1310;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS1310 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4211);
      if (_M0L7n__keepS1310 > 0 && _M0L7n__keepS1310 <= _M0L4rowsS1284) {
        int32_t _M0L7_2abindS1311 = 0;
        int32_t _M0L1jS1312 = _M0L7_2abindS1311;
        while (1) {
          if (_M0L1jS1312 < _M0L4colsS1288) {
            int32_t* _M0L6_2atmpS4206 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS1313 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS1314;
            int32_t _M0L1kS1315;
            int32_t _M0L7n__dropS1317;
            int32_t _M0L7_2abindS1318;
            int32_t _M0L1kS1319;
            int32_t _M0L7_2abindS1325;
            int32_t _M0L1kS1326;
            int32_t _M0L6_2atmpS4207;
            Moonbit_object_header(_M0L8pre__idxS1313)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
            _M0L8pre__idxS1313->$0 = _M0L6_2atmpS4206;
            _M0L8pre__idxS1313->$1 = 0;
            _M0L7_2abindS1314 = 0;
            _M0L1kS1315 = _M0L7_2abindS1314;
            while (1) {
              if (_M0L1kS1315 < _M0L4rowsS1284) {
                int32_t _M0L6_2atmpS4195;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS1313, _M0L1kS1315);
                _M0L6_2atmpS4195 = _M0L1kS1315 + 1;
                _M0L1kS1315 = _M0L6_2atmpS4195;
                continue;
              }
              break;
            }
            _M0L7n__dropS1317 = _M0L4rowsS1284 - _M0L7n__keepS1310;
            _M0L7_2abindS1318 = 0;
            _M0L1kS1319 = _M0L7_2abindS1318;
            while (1) {
              if (_M0L1kS1319 < _M0L7n__dropS1317) {
                float _M0L1uS1320;
                float _M0L6_2atmpS4199;
                float _M0L6_2atmpS4201;
                float _M0L6_2atmpS4200;
                float _M0L6_2atmpS4198;
                int32_t _M0L6_2atmpS4197;
                int32_t _M0L6r__idxS1321;
                int32_t _M0L10r__clampedS1322;
                int32_t _M0L3tmpS1323;
                int32_t _M0L6_2atmpS4196;
                int32_t _M0L6_2atmpS4202;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS1320 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1297);
                _M0L6_2atmpS4199 = (float)_M0L4rowsS1284;
                _M0L6_2atmpS4201 = (float)_M0L1kS1319;
                _M0L6_2atmpS4200 = _M0L6_2atmpS4201 * _M0L1uS1320;
                _M0L6_2atmpS4198 = _M0L6_2atmpS4199 - _M0L6_2atmpS4200;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4197
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS4198);
                _M0L6r__idxS1321 = _M0L1kS1319 + _M0L6_2atmpS4197;
                if (_M0L6r__idxS1321 >= _M0L4rowsS1284) {
                  _M0L10r__clampedS1322 = _M0L4rowsS1284 - 1;
                } else {
                  _M0L10r__clampedS1322 = _M0L6r__idxS1321;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1323
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1313, _M0L1kS1319);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4196
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1313, _M0L10r__clampedS1322);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS1313, _M0L1kS1319, _M0L6_2atmpS4196);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS1313, _M0L10r__clampedS1322, _M0L3tmpS1323);
                _M0L6_2atmpS4202 = _M0L1kS1319 + 1;
                _M0L1kS1319 = _M0L6_2atmpS4202;
                continue;
              }
              break;
            }
            _M0L7_2abindS1325 = 0;
            _M0L1kS1326 = _M0L7_2abindS1325;
            while (1) {
              if (_M0L1kS1326 < _M0L7n__dropS1317) {
                int32_t _M0L6_2atmpS4204;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4203;
                int32_t _M0L6_2atmpS4205;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4204
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1313, _M0L1kS1326);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4203
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1283, _M0L6_2atmpS4204);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4203, _M0L1jS1312, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4203);
                _M0L6_2atmpS4205 = _M0L1kS1326 + 1;
                _M0L1kS1326 = _M0L6_2atmpS4205;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS1313);
              }
              break;
            }
            _M0L6_2atmpS4207 = _M0L1jS1312 + 1;
            _M0L1jS1312 = _M0L6_2atmpS4207;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS1310 == 0) {
        int32_t _M0L7_2abindS1329 = 0;
        int32_t _M0L1iS1330 = _M0L7_2abindS1329;
        while (1) {
          if (_M0L1iS1330 < _M0L4rowsS1284) {
            int32_t _M0L7_2abindS1331 = 0;
            int32_t _M0L1jS1332 = _M0L7_2abindS1331;
            int32_t _M0L6_2atmpS4210;
            while (1) {
              if (_M0L1jS1332 < _M0L4colsS1288) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4208;
                int32_t _M0L6_2atmpS4209;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4208
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1283, _M0L1iS1330);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4208, _M0L1jS1332, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4208);
                _M0L6_2atmpS4209 = _M0L1jS1332 + 1;
                _M0L1jS1332 = _M0L6_2atmpS4209;
                continue;
              }
              break;
            }
            _M0L6_2atmpS4210 = _M0L1iS1330 + 1;
            _M0L1iS1330 = _M0L6_2atmpS4210;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS4230 = (float)_M0L4colsS1288;
      float _M0L6_2atmpS4229 = _M0L6_2atmpS4230 * _M0L1pS1307;
      int32_t _M0L7n__keepS1335;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS1335 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4229);
      if (_M0L7n__keepS1335 > 0 && _M0L7n__keepS1335 <= _M0L4colsS1288) {
        int32_t _M0L7_2abindS1336 = 0;
        int32_t _M0L1iS1337 = _M0L7_2abindS1336;
        while (1) {
          if (_M0L1iS1337 < _M0L4rowsS1284) {
            int32_t* _M0L6_2atmpS4224 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS1338 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS1339;
            int32_t _M0L1kS1340;
            int32_t _M0L7n__dropS1342;
            int32_t _M0L7_2abindS1343;
            int32_t _M0L1kS1344;
            int32_t _M0L7_2abindS1350;
            int32_t _M0L1kS1351;
            int32_t _M0L6_2atmpS4225;
            Moonbit_object_header(_M0L9post__idxS1338)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
            _M0L9post__idxS1338->$0 = _M0L6_2atmpS4224;
            _M0L9post__idxS1338->$1 = 0;
            _M0L7_2abindS1339 = 0;
            _M0L1kS1340 = _M0L7_2abindS1339;
            while (1) {
              if (_M0L1kS1340 < _M0L4colsS1288) {
                int32_t _M0L6_2atmpS4213;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS1338, _M0L1kS1340);
                _M0L6_2atmpS4213 = _M0L1kS1340 + 1;
                _M0L1kS1340 = _M0L6_2atmpS4213;
                continue;
              }
              break;
            }
            _M0L7n__dropS1342 = _M0L4colsS1288 - _M0L7n__keepS1335;
            _M0L7_2abindS1343 = 0;
            _M0L1kS1344 = _M0L7_2abindS1343;
            while (1) {
              if (_M0L1kS1344 < _M0L7n__dropS1342) {
                float _M0L1uS1345;
                float _M0L6_2atmpS4217;
                float _M0L6_2atmpS4219;
                float _M0L6_2atmpS4218;
                float _M0L6_2atmpS4216;
                int32_t _M0L6_2atmpS4215;
                int32_t _M0L6r__idxS1346;
                int32_t _M0L10r__clampedS1347;
                int32_t _M0L3tmpS1348;
                int32_t _M0L6_2atmpS4214;
                int32_t _M0L6_2atmpS4220;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS1345 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1297);
                _M0L6_2atmpS4217 = (float)_M0L4colsS1288;
                _M0L6_2atmpS4219 = (float)_M0L1kS1344;
                _M0L6_2atmpS4218 = _M0L6_2atmpS4219 * _M0L1uS1345;
                _M0L6_2atmpS4216 = _M0L6_2atmpS4217 - _M0L6_2atmpS4218;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4215
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS4216);
                _M0L6r__idxS1346 = _M0L1kS1344 + _M0L6_2atmpS4215;
                if (_M0L6r__idxS1346 >= _M0L4colsS1288) {
                  _M0L10r__clampedS1347 = _M0L4colsS1288 - 1;
                } else {
                  _M0L10r__clampedS1347 = _M0L6r__idxS1346;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1348
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1338, _M0L1kS1344);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4214
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1338, _M0L10r__clampedS1347);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS1338, _M0L1kS1344, _M0L6_2atmpS4214);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS1338, _M0L10r__clampedS1347, _M0L3tmpS1348);
                _M0L6_2atmpS4220 = _M0L1kS1344 + 1;
                _M0L1kS1344 = _M0L6_2atmpS4220;
                continue;
              }
              break;
            }
            _M0L7_2abindS1350 = 0;
            _M0L1kS1351 = _M0L7_2abindS1350;
            while (1) {
              if (_M0L1kS1351 < _M0L7n__dropS1342) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4221;
                int32_t _M0L6_2atmpS4222;
                int32_t _M0L6_2atmpS4223;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4221
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1283, _M0L1iS1337);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4222
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1338, _M0L1kS1351);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4221, _M0L6_2atmpS4222, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4221);
                _M0L6_2atmpS4223 = _M0L1kS1351 + 1;
                _M0L1kS1351 = _M0L6_2atmpS4223;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS1338);
              }
              break;
            }
            _M0L6_2atmpS4225 = _M0L1iS1337 + 1;
            _M0L1iS1337 = _M0L6_2atmpS4225;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS1335 == 0) {
        int32_t _M0L7_2abindS1354 = 0;
        int32_t _M0L1iS1355 = _M0L7_2abindS1354;
        while (1) {
          if (_M0L1iS1355 < _M0L4rowsS1284) {
            int32_t _M0L7_2abindS1356 = 0;
            int32_t _M0L1jS1357 = _M0L7_2abindS1356;
            int32_t _M0L6_2atmpS4228;
            while (1) {
              if (_M0L1jS1357 < _M0L4colsS1288) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4226;
                int32_t _M0L6_2atmpS4227;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4226
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1283, _M0L1iS1355);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4226, _M0L1jS1357, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4226);
                _M0L6_2atmpS4227 = _M0L1jS1357 + 1;
                _M0L1jS1357 = _M0L6_2atmpS4227;
                continue;
              }
              break;
            }
            _M0L6_2atmpS4228 = _M0L1iS1355 + 1;
            _M0L1iS1355 = _M0L6_2atmpS4228;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS4238 = _M0L4rowsS1284 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS1360 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS4238, 0);
  _M0L6_2atmpS4237 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS1361
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS1361)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6colptrS1361->$0 = _M0L6_2atmpS4237;
  _M0L6colptrS1361->$1 = 0;
  _M0L6_2atmpS4236 = moonbit_empty_float_array;
  _M0L4valsS1362
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS1362)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L4valsS1362->$0 = _M0L6_2atmpS4236;
  _M0L4valsS1362->$1 = 0;
  _M0L7_2abindS1363 = 0;
  _M0L1iS1364 = _M0L7_2abindS1363;
  while (1) {
    if (_M0L1iS1364 < _M0L4rowsS1284) {
      int32_t _M0L6_2atmpS4231;
      int32_t _M0L7_2abindS1365;
      int32_t _M0L1jS1366;
      int32_t _M0L6_2atmpS4234;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS4231 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1362);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS1360, _M0L1iS1364, _M0L6_2atmpS4231);
      _M0L7_2abindS1365 = 0;
      _M0L1jS1366 = _M0L7_2abindS1365;
      while (1) {
        if (_M0L1jS1366 < _M0L4colsS1288) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS4232;
          float _M0L1vS1367;
          int32_t _M0L6_2atmpS4233;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS4232
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1283, _M0L1iS1364);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS1367
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS4232, _M0L1jS1366);
          moonbit_decref_cycle_free(_M0L6_2atmpS4232);
          if (_M0L1vS1367 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS1361, _M0L1jS1366);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS1362, _M0L1vS1367);
          }
          _M0L6_2atmpS4233 = _M0L1jS1366 + 1;
          _M0L1jS1366 = _M0L6_2atmpS4233;
          continue;
        }
        break;
      }
      _M0L6_2atmpS4234 = _M0L1iS1364 + 1;
      _M0L1iS1364 = _M0L6_2atmpS4234;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS1283);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS4235 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1362);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS1360, _M0L4rowsS1284, _M0L6_2atmpS4235);
  _block_6039
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_6039)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 81, 0);
  _block_6039->$0 = _M0L4rowsS1284;
  _block_6039->$1 = _M0L4colsS1288;
  _block_6039->$2 = _M0L6rowptrS1360;
  _block_6039->$3 = _M0L6colptrS1361;
  _block_6039->$4 = _M0L4valsS1362;
  return _block_6039;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1282
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS4187;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS4187 = _M0L1mS1282->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS4187);
}

int32_t _M0FP26RiantR8snn__mbt20ca__plasticity__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1260,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1247,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1249,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1259,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1255,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS1244,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS1251,
  float _M0L6t__nowS1245,
  float _M0L2dtS1272
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS4078;
  int32_t _M0L6_2atmpS4077;
  int32_t _if__result_6040;
  int32_t _M0L6n__preS1246;
  int32_t _M0L7n__postS1248;
  float _M0L8tau__preS4186;
  float _M0L13inv__tau__preS1250;
  float _M0L9tau__postS4185;
  float _M0L14inv__tau__postS1252;
  struct _M0TPB8MutLocalGiE* _M0L1jS1253;
  struct _M0TPB8MutLocalGiE* _M0L1kS1263;
  struct _M0TPB8MutLocalGiE* _M0L2jjS1271;
  struct _M0TPB8MutLocalGiE* _M0L2iiS1274;
  struct _M0TPB8MutLocalGiE* _M0L3jj2S1276;
  struct _M0TPB8MutLocalGiE* _M0L3ii2S1278;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1280;
  #line 1495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS4078 = _M0L4varsS1244->$4;
  #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS4077 = _M0MPC15array5Array6lengthGbE(_M0L6activeS4078);
  if (_M0L6_2atmpS4077 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS4076 = _M0L4varsS1244->$4;
    int32_t _M0L6_2atmpS4075;
    #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4075 = _M0MPC15array5Array2atGbE(_M0L6activeS4076, 0);
    _if__result_6040 = !_M0L6_2atmpS4075;
  } else {
    _if__result_6040 = 0;
  }
  if (_if__result_6040) {
    return 0;
  }
  #line 1509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1246 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1247);
  #line 1510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1248 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1249);
  _M0L8tau__preS4186 = _M0L5paramS1251->$2;
  _M0L13inv__tau__preS1250 = 0x1p+0f / _M0L8tau__preS4186;
  _M0L9tau__postS4185 = _M0L5paramS1251->$3;
  _M0L14inv__tau__postS1252 = 0x1p+0f / _M0L9tau__postS4185;
  _M0L1jS1253
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1253)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1253->$0 = 0;
  while (1) {
    int32_t _M0L3valS4079 = _M0L1jS1253->$0;
    if (_M0L3valS4079 < _M0L6n__preS1246) {
      int32_t _M0L3valS4080 = _M0L1jS1253->$0;
      int32_t _M0L3valS4095;
      int32_t _M0L6_2atmpS4094;
      #line 1516 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1247, _M0L3valS4080)) {
        int32_t _M0L3valS4093 = _M0L1jS1253->$0;
        int32_t _M0L5startS1254;
        int32_t _M0L3valS4092;
        int32_t _M0L6_2atmpS4091;
        int32_t _M0L5end__S1256;
        struct _M0TPB8MutLocalGiE* _M0L1sS1257;
        #line 1517 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1254
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1255, _M0L3valS4093);
        _M0L3valS4092 = _M0L1jS1253->$0;
        _M0L6_2atmpS4091 = _M0L3valS4092 + 1;
        #line 1518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5end__S1256
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1255, _M0L6_2atmpS4091);
        _M0L1sS1257
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1257)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1257->$0 = _M0L5startS1254;
        while (1) {
          int32_t _M0L3valS4081 = _M0L1sS1257->$0;
          if (_M0L3valS4081 < _M0L5end__S1256) {
            int32_t _M0L3valS4090 = _M0L1sS1257->$0;
            int32_t _M0L1iS1258;
            int32_t _M0L3valS4082;
            int32_t _M0L3valS4087;
            float _M0L6_2atmpS4084;
            struct _M0TPB5ArrayGfE* _M0L5tpostS4086;
            float _M0L6_2atmpS4085;
            float _M0L6_2atmpS4083;
            int32_t _M0L3valS4089;
            int32_t _M0L6_2atmpS4088;
            #line 1521 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L1iS1258
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1259, _M0L3valS4090);
            _M0L3valS4082 = _M0L1sS1257->$0;
            _M0L3valS4087 = _M0L1sS1257->$0;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4084
            = _M0MPC15array5Array2atGfE(_M0L1wS1260, _M0L3valS4087);
            _M0L5tpostS4086 = _M0L4varsS1244->$3;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4085
            = _M0MPC15array5Array2atGfE(_M0L5tpostS4086, _M0L1iS1258);
            _M0L6_2atmpS4083 = _M0L6_2atmpS4084 + _M0L6_2atmpS4085;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1260, _M0L3valS4082, _M0L6_2atmpS4083);
            _M0L3valS4089 = _M0L1sS1257->$0;
            _M0L6_2atmpS4088 = _M0L3valS4089 + 1;
            _M0L1sS1257->$0 = _M0L6_2atmpS4088;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1257);
          }
          break;
        }
      }
      _M0L3valS4095 = _M0L1jS1253->$0;
      _M0L6_2atmpS4094 = _M0L3valS4095 + 1;
      _M0L1jS1253->$0 = _M0L6_2atmpS4094;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1253);
    }
    break;
  }
  _M0L1kS1263
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1263)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1263->$0 = 0;
  while (1) {
    int32_t _M0L3valS4096 = _M0L1kS1263->$0;
    if (_M0L3valS4096 < _M0L7n__postS1248) {
      int32_t _M0L3valS4097 = _M0L1kS1263->$0;
      int32_t _M0L3valS4118;
      int32_t _M0L6_2atmpS4117;
      #line 1531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1249, _M0L3valS4097)) {
        struct _M0TPB8MutLocalGiE* _M0L2j2S1264 =
          (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L2j2S1264)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2j2S1264->$0 = 0;
        while (1) {
          int32_t _M0L3valS4098 = _M0L2j2S1264->$0;
          if (_M0L3valS4098 < _M0L6n__preS1246) {
            int32_t _M0L3valS4116 = _M0L2j2S1264->$0;
            int32_t _M0L5startS1265;
            int32_t _M0L3valS4115;
            int32_t _M0L6_2atmpS4114;
            int32_t _M0L5end__S1266;
            struct _M0TPB8MutLocalGiE* _M0L1sS1267;
            int32_t _M0L3valS4113;
            int32_t _M0L6_2atmpS4112;
            #line 1537 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5startS1265
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1255, _M0L3valS4116);
            _M0L3valS4115 = _M0L2j2S1264->$0;
            _M0L6_2atmpS4114 = _M0L3valS4115 + 1;
            #line 1538 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5end__S1266
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1255, _M0L6_2atmpS4114);
            _M0L1sS1267
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1267)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1267->$0 = _M0L5startS1265;
            while (1) {
              int32_t _M0L3valS4099 = _M0L1sS1267->$0;
              if (_M0L3valS4099 < _M0L5end__S1266) {
                int32_t _M0L3valS4102 = _M0L1sS1267->$0;
                int32_t _M0L6_2atmpS4100;
                int32_t _M0L3valS4101;
                int32_t _M0L3valS4111;
                int32_t _M0L6_2atmpS4110;
                #line 1541 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                _M0L6_2atmpS4100
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1259, _M0L3valS4102);
                _M0L3valS4101 = _M0L1kS1263->$0;
                if (_M0L6_2atmpS4100 == _M0L3valS4101) {
                  int32_t _M0L3valS4103 = _M0L1sS1267->$0;
                  int32_t _M0L3valS4109 = _M0L1sS1267->$0;
                  float _M0L6_2atmpS4105;
                  struct _M0TPB5ArrayGfE* _M0L4tpreS4107;
                  int32_t _M0L3valS4108;
                  float _M0L6_2atmpS4106;
                  float _M0L6_2atmpS4104;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4105
                  = _M0MPC15array5Array2atGfE(_M0L1wS1260, _M0L3valS4109);
                  _M0L4tpreS4107 = _M0L4varsS1244->$2;
                  _M0L3valS4108 = _M0L2j2S1264->$0;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4106
                  = _M0MPC15array5Array2atGfE(_M0L4tpreS4107, _M0L3valS4108);
                  _M0L6_2atmpS4104 = _M0L6_2atmpS4105 + _M0L6_2atmpS4106;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0MPC15array5Array3setGfE(_M0L1wS1260, _M0L3valS4103, _M0L6_2atmpS4104);
                }
                _M0L3valS4111 = _M0L1sS1267->$0;
                _M0L6_2atmpS4110 = _M0L3valS4111 + 1;
                _M0L1sS1267->$0 = _M0L6_2atmpS4110;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1267);
              }
              break;
            }
            _M0L3valS4113 = _M0L2j2S1264->$0;
            _M0L6_2atmpS4112 = _M0L3valS4113 + 1;
            _M0L2j2S1264->$0 = _M0L6_2atmpS4112;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L2j2S1264);
          }
          break;
        }
      }
      _M0L3valS4118 = _M0L1kS1263->$0;
      _M0L6_2atmpS4117 = _M0L3valS4118 + 1;
      _M0L1kS1263->$0 = _M0L6_2atmpS4117;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1263);
    }
    break;
  }
  _M0L2jjS1271
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2jjS1271)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2jjS1271->$0 = 0;
  while (1) {
    int32_t _M0L3valS4119 = _M0L2jjS1271->$0;
    if (_M0L3valS4119 < _M0L6n__preS1246) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4120 = _M0L4varsS1244->$2;
      int32_t _M0L3valS4121 = _M0L2jjS1271->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4130 = _M0L4varsS1244->$2;
      int32_t _M0L3valS4131 = _M0L2jjS1271->$0;
      float _M0L6_2atmpS4123;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4128;
      int32_t _M0L3valS4129;
      float _M0L6_2atmpS4127;
      float _M0L6_2atmpS4126;
      float _M0L6_2atmpS4125;
      float _M0L6_2atmpS4124;
      float _M0L6_2atmpS4122;
      int32_t _M0L3valS4133;
      int32_t _M0L6_2atmpS4132;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4123
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4130, _M0L3valS4131);
      _M0L4tpreS4128 = _M0L4varsS1244->$2;
      _M0L3valS4129 = _M0L2jjS1271->$0;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4127
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4128, _M0L3valS4129);
      _M0L6_2atmpS4126 = -_M0L6_2atmpS4127;
      _M0L6_2atmpS4125 = _M0L2dtS1272 * _M0L6_2atmpS4126;
      _M0L6_2atmpS4124 = _M0L6_2atmpS4125 * _M0L13inv__tau__preS1250;
      _M0L6_2atmpS4122 = _M0L6_2atmpS4123 + _M0L6_2atmpS4124;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4120, _M0L3valS4121, _M0L6_2atmpS4122);
      _M0L3valS4133 = _M0L2jjS1271->$0;
      _M0L6_2atmpS4132 = _M0L3valS4133 + 1;
      _M0L2jjS1271->$0 = _M0L6_2atmpS4132;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2jjS1271);
    }
    break;
  }
  _M0L2iiS1274
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2iiS1274)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2iiS1274->$0 = 0;
  while (1) {
    int32_t _M0L3valS4134 = _M0L2iiS1274->$0;
    if (_M0L3valS4134 < _M0L7n__postS1248) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4135 = _M0L4varsS1244->$3;
      int32_t _M0L3valS4136 = _M0L2iiS1274->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4145 = _M0L4varsS1244->$3;
      int32_t _M0L3valS4146 = _M0L2iiS1274->$0;
      float _M0L6_2atmpS4138;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4143;
      int32_t _M0L3valS4144;
      float _M0L6_2atmpS4142;
      float _M0L6_2atmpS4141;
      float _M0L6_2atmpS4140;
      float _M0L6_2atmpS4139;
      float _M0L6_2atmpS4137;
      int32_t _M0L3valS4148;
      int32_t _M0L6_2atmpS4147;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4138
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4145, _M0L3valS4146);
      _M0L5tpostS4143 = _M0L4varsS1244->$3;
      _M0L3valS4144 = _M0L2iiS1274->$0;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4142
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4143, _M0L3valS4144);
      _M0L6_2atmpS4141 = -_M0L6_2atmpS4142;
      _M0L6_2atmpS4140 = _M0L2dtS1272 * _M0L6_2atmpS4141;
      _M0L6_2atmpS4139 = _M0L6_2atmpS4140 * _M0L14inv__tau__postS1252;
      _M0L6_2atmpS4137 = _M0L6_2atmpS4138 + _M0L6_2atmpS4139;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4135, _M0L3valS4136, _M0L6_2atmpS4137);
      _M0L3valS4148 = _M0L2iiS1274->$0;
      _M0L6_2atmpS4147 = _M0L3valS4148 + 1;
      _M0L2iiS1274->$0 = _M0L6_2atmpS4147;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2iiS1274);
    }
    break;
  }
  _M0L3jj2S1276
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3jj2S1276)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3jj2S1276->$0 = 0;
  while (1) {
    int32_t _M0L3valS4149 = _M0L3jj2S1276->$0;
    if (_M0L3valS4149 < _M0L6n__preS1246) {
      int32_t _M0L3valS4150 = _M0L3jj2S1276->$0;
      int32_t _M0L3valS4159;
      int32_t _M0L6_2atmpS4158;
      #line 1565 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1247, _M0L3valS4150)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4151 = _M0L4varsS1244->$2;
        int32_t _M0L3valS4152 = _M0L3jj2S1276->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4156 = _M0L4varsS1244->$2;
        int32_t _M0L3valS4157 = _M0L3jj2S1276->$0;
        float _M0L6_2atmpS4154;
        float _M0L6a__preS4155;
        float _M0L6_2atmpS4153;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4154
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4156, _M0L3valS4157);
        _M0L6a__preS4155 = _M0L5paramS1251->$0;
        _M0L6_2atmpS4153 = _M0L6_2atmpS4154 + _M0L6a__preS4155;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4151, _M0L3valS4152, _M0L6_2atmpS4153);
      }
      _M0L3valS4159 = _M0L3jj2S1276->$0;
      _M0L6_2atmpS4158 = _M0L3valS4159 + 1;
      _M0L3jj2S1276->$0 = _M0L6_2atmpS4158;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3jj2S1276);
    }
    break;
  }
  _M0L3ii2S1278
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3ii2S1278)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3ii2S1278->$0 = 0;
  while (1) {
    int32_t _M0L3valS4160 = _M0L3ii2S1278->$0;
    if (_M0L3valS4160 < _M0L7n__postS1248) {
      int32_t _M0L3valS4161 = _M0L3ii2S1278->$0;
      int32_t _M0L3valS4170;
      int32_t _M0L6_2atmpS4169;
      #line 1572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1249, _M0L3valS4161)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4162 = _M0L4varsS1244->$3;
        int32_t _M0L3valS4163 = _M0L3ii2S1278->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4167 = _M0L4varsS1244->$3;
        int32_t _M0L3valS4168 = _M0L3ii2S1278->$0;
        float _M0L6_2atmpS4165;
        float _M0L7a__postS4166;
        float _M0L6_2atmpS4164;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4165
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4167, _M0L3valS4168);
        _M0L7a__postS4166 = _M0L5paramS1251->$1;
        _M0L6_2atmpS4164 = _M0L6_2atmpS4165 + _M0L7a__postS4166;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4162, _M0L3valS4163, _M0L6_2atmpS4164);
      }
      _M0L3valS4170 = _M0L3ii2S1278->$0;
      _M0L6_2atmpS4169 = _M0L3valS4170 + 1;
      _M0L3ii2S1278->$0 = _M0L6_2atmpS4169;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3ii2S1278);
    }
    break;
  }
  _M0L2s2S1280
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1280)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1280->$0 = 0;
  while (1) {
    int32_t _M0L3valS4171 = _M0L2s2S1280->$0;
    int32_t _M0L6_2atmpS4172;
    #line 1579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4172 = _M0MPC15array5Array6lengthGfE(_M0L1wS1260);
    if (_M0L3valS4171 < _M0L6_2atmpS4172) {
      int32_t _M0L3valS4175 = _M0L2s2S1280->$0;
      float _M0L6_2atmpS4173;
      float _M0L6w__minS4174;
      int32_t _M0L3valS4180;
      float _M0L6_2atmpS4178;
      float _M0L6w__maxS4179;
      int32_t _M0L3valS4184;
      int32_t _M0L6_2atmpS4183;
      #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4173
      = _M0MPC15array5Array2atGfE(_M0L1wS1260, _M0L3valS4175);
      _M0L6w__minS4174 = _M0L5paramS1251->$5;
      if (_M0L6_2atmpS4173 < _M0L6w__minS4174) {
        int32_t _M0L3valS4176 = _M0L2s2S1280->$0;
        float _M0L6w__minS4177 = _M0L5paramS1251->$5;
        #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1260, _M0L3valS4176, _M0L6w__minS4177);
      }
      _M0L3valS4180 = _M0L2s2S1280->$0;
      #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4178
      = _M0MPC15array5Array2atGfE(_M0L1wS1260, _M0L3valS4180);
      _M0L6w__maxS4179 = _M0L5paramS1251->$4;
      if (_M0L6_2atmpS4178 > _M0L6w__maxS4179) {
        int32_t _M0L3valS4181 = _M0L2s2S1280->$0;
        float _M0L6w__maxS4182 = _M0L5paramS1251->$4;
        #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1260, _M0L3valS4181, _M0L6w__maxS4182);
      }
      _M0L3valS4184 = _M0L2s2S1280->$0;
      _M0L6_2atmpS4183 = _M0L3valS4184 + 1;
      _M0L2s2S1280->$0 = _M0L6_2atmpS4183;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1280);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1240,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1213,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1215,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1235,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1228,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS1220,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS1217,
  float _M0L6t__nowS1211,
  float _M0L2dtS1221
) {
  int32_t _M0L6n__preS1212;
  int32_t _M0L7n__postS1214;
  float _M0L6tau__xS4074;
  float _M0L11inv__tau__xS1216;
  float _M0L6tau__yS4073;
  float _M0L11inv__tau__yS1218;
  struct _M0TPB8MutLocalGiE* _M0L1jS1219;
  struct _M0TPB8MutLocalGiE* _M0L1iS1223;
  float _M0L4a__xS4070;
  float _M0L6tau__xS4072;
  float _M0L6_2atmpS4071;
  float _M0L7coef__xS1225;
  float _M0L4a__yS4067;
  float _M0L6tau__yS4069;
  float _M0L6_2atmpS4068;
  float _M0L7coef__yS1226;
  #line 1296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1212 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1213);
  #line 1309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1214 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1215);
  _M0L6tau__xS4074 = _M0L5paramS1217->$2;
  _M0L11inv__tau__xS1216 = 0x1p+0f / _M0L6tau__xS4074;
  _M0L6tau__yS4073 = _M0L5paramS1217->$3;
  _M0L11inv__tau__yS1218 = 0x1p+0f / _M0L6tau__yS4073;
  _M0L1jS1219
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1219->$0 = 0;
  while (1) {
    int32_t _M0L3valS3944 = _M0L1jS1219->$0;
    if (_M0L3valS3944 < _M0L6n__preS1212) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3945 = _M0L4varsS1220->$0;
      int32_t _M0L3valS3946 = _M0L1jS1219->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3955 = _M0L4varsS1220->$0;
      int32_t _M0L3valS3956 = _M0L1jS1219->$0;
      float _M0L6_2atmpS3948;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3953;
      int32_t _M0L3valS3954;
      float _M0L6_2atmpS3952;
      float _M0L6_2atmpS3951;
      float _M0L6_2atmpS3950;
      float _M0L6_2atmpS3949;
      float _M0L6_2atmpS3947;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3957;
      int32_t _M0L3valS3958;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3967;
      int32_t _M0L3valS3968;
      float _M0L6_2atmpS3960;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3965;
      int32_t _M0L3valS3966;
      float _M0L6_2atmpS3964;
      float _M0L6_2atmpS3963;
      float _M0L6_2atmpS3962;
      float _M0L6_2atmpS3961;
      float _M0L6_2atmpS3959;
      int32_t _M0L3valS3969;
      int32_t _M0L3valS3983;
      int32_t _M0L6_2atmpS3982;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3948
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3955, _M0L3valS3956);
      _M0L5tr__xS3953 = _M0L4varsS1220->$0;
      _M0L3valS3954 = _M0L1jS1219->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3952
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3953, _M0L3valS3954);
      _M0L6_2atmpS3951 = -_M0L6_2atmpS3952;
      _M0L6_2atmpS3950 = _M0L2dtS1221 * _M0L6_2atmpS3951;
      _M0L6_2atmpS3949 = _M0L6_2atmpS3950 * _M0L11inv__tau__xS1216;
      _M0L6_2atmpS3947 = _M0L6_2atmpS3948 + _M0L6_2atmpS3949;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3945, _M0L3valS3946, _M0L6_2atmpS3947);
      _M0L5tr__yS3957 = _M0L4varsS1220->$1;
      _M0L3valS3958 = _M0L1jS1219->$0;
      _M0L5tr__yS3967 = _M0L4varsS1220->$1;
      _M0L3valS3968 = _M0L1jS1219->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3960
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3967, _M0L3valS3968);
      _M0L5tr__yS3965 = _M0L4varsS1220->$1;
      _M0L3valS3966 = _M0L1jS1219->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3964
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3965, _M0L3valS3966);
      _M0L6_2atmpS3963 = -_M0L6_2atmpS3964;
      _M0L6_2atmpS3962 = _M0L2dtS1221 * _M0L6_2atmpS3963;
      _M0L6_2atmpS3961 = _M0L6_2atmpS3962 * _M0L11inv__tau__yS1218;
      _M0L6_2atmpS3959 = _M0L6_2atmpS3960 + _M0L6_2atmpS3961;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__yS3957, _M0L3valS3958, _M0L6_2atmpS3959);
      _M0L3valS3969 = _M0L1jS1219->$0;
      #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1213, _M0L3valS3969)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3970 = _M0L4varsS1220->$0;
        int32_t _M0L3valS3971 = _M0L1jS1219->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3974 = _M0L4varsS1220->$0;
        int32_t _M0L3valS3975 = _M0L1jS1219->$0;
        float _M0L6_2atmpS3973;
        float _M0L6_2atmpS3972;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3976;
        int32_t _M0L3valS3977;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3980;
        int32_t _M0L3valS3981;
        float _M0L6_2atmpS3979;
        float _M0L6_2atmpS3978;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3973
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3974, _M0L3valS3975);
        _M0L6_2atmpS3972 = _M0L6_2atmpS3973 + 0x1p+0f;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3970, _M0L3valS3971, _M0L6_2atmpS3972);
        _M0L5tr__yS3976 = _M0L4varsS1220->$1;
        _M0L3valS3977 = _M0L1jS1219->$0;
        _M0L5tr__yS3980 = _M0L4varsS1220->$1;
        _M0L3valS3981 = _M0L1jS1219->$0;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3979
        = _M0MPC15array5Array2atGfE(_M0L5tr__yS3980, _M0L3valS3981);
        _M0L6_2atmpS3978 = _M0L6_2atmpS3979 + 0x1p+0f;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__yS3976, _M0L3valS3977, _M0L6_2atmpS3978);
      }
      _M0L3valS3983 = _M0L1jS1219->$0;
      _M0L6_2atmpS3982 = _M0L3valS3983 + 1;
      _M0L1jS1219->$0 = _M0L6_2atmpS3982;
      continue;
    }
    break;
  }
  _M0L1iS1223
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1223)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1223->$0 = 0;
  while (1) {
    int32_t _M0L3valS3984 = _M0L1iS1223->$0;
    if (_M0L3valS3984 < _M0L7n__postS1214) {
      struct _M0TPB5ArrayGfE* _M0L5to__xS3985 = _M0L4varsS1220->$2;
      int32_t _M0L3valS3986 = _M0L1iS1223->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3995 = _M0L4varsS1220->$2;
      int32_t _M0L3valS3996 = _M0L1iS1223->$0;
      float _M0L6_2atmpS3988;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3993;
      int32_t _M0L3valS3994;
      float _M0L6_2atmpS3992;
      float _M0L6_2atmpS3991;
      float _M0L6_2atmpS3990;
      float _M0L6_2atmpS3989;
      float _M0L6_2atmpS3987;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3997;
      int32_t _M0L3valS3998;
      struct _M0TPB5ArrayGfE* _M0L5to__yS4007;
      int32_t _M0L3valS4008;
      float _M0L6_2atmpS4000;
      struct _M0TPB5ArrayGfE* _M0L5to__yS4005;
      int32_t _M0L3valS4006;
      float _M0L6_2atmpS4004;
      float _M0L6_2atmpS4003;
      float _M0L6_2atmpS4002;
      float _M0L6_2atmpS4001;
      float _M0L6_2atmpS3999;
      int32_t _M0L3valS4009;
      int32_t _M0L3valS4023;
      int32_t _M0L6_2atmpS4022;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3988
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3995, _M0L3valS3996);
      _M0L5to__xS3993 = _M0L4varsS1220->$2;
      _M0L3valS3994 = _M0L1iS1223->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3992
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3993, _M0L3valS3994);
      _M0L6_2atmpS3991 = -_M0L6_2atmpS3992;
      _M0L6_2atmpS3990 = _M0L2dtS1221 * _M0L6_2atmpS3991;
      _M0L6_2atmpS3989 = _M0L6_2atmpS3990 * _M0L11inv__tau__xS1216;
      _M0L6_2atmpS3987 = _M0L6_2atmpS3988 + _M0L6_2atmpS3989;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__xS3985, _M0L3valS3986, _M0L6_2atmpS3987);
      _M0L5to__yS3997 = _M0L4varsS1220->$3;
      _M0L3valS3998 = _M0L1iS1223->$0;
      _M0L5to__yS4007 = _M0L4varsS1220->$3;
      _M0L3valS4008 = _M0L1iS1223->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4000
      = _M0MPC15array5Array2atGfE(_M0L5to__yS4007, _M0L3valS4008);
      _M0L5to__yS4005 = _M0L4varsS1220->$3;
      _M0L3valS4006 = _M0L1iS1223->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4004
      = _M0MPC15array5Array2atGfE(_M0L5to__yS4005, _M0L3valS4006);
      _M0L6_2atmpS4003 = -_M0L6_2atmpS4004;
      _M0L6_2atmpS4002 = _M0L2dtS1221 * _M0L6_2atmpS4003;
      _M0L6_2atmpS4001 = _M0L6_2atmpS4002 * _M0L11inv__tau__yS1218;
      _M0L6_2atmpS3999 = _M0L6_2atmpS4000 + _M0L6_2atmpS4001;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3997, _M0L3valS3998, _M0L6_2atmpS3999);
      _M0L3valS4009 = _M0L1iS1223->$0;
      #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1215, _M0L3valS4009)) {
        struct _M0TPB5ArrayGfE* _M0L5to__xS4010 = _M0L4varsS1220->$2;
        int32_t _M0L3valS4011 = _M0L1iS1223->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__xS4014 = _M0L4varsS1220->$2;
        int32_t _M0L3valS4015 = _M0L1iS1223->$0;
        float _M0L6_2atmpS4013;
        float _M0L6_2atmpS4012;
        struct _M0TPB5ArrayGfE* _M0L5to__yS4016;
        int32_t _M0L3valS4017;
        struct _M0TPB5ArrayGfE* _M0L5to__yS4020;
        int32_t _M0L3valS4021;
        float _M0L6_2atmpS4019;
        float _M0L6_2atmpS4018;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4013
        = _M0MPC15array5Array2atGfE(_M0L5to__xS4014, _M0L3valS4015);
        _M0L6_2atmpS4012 = _M0L6_2atmpS4013 + 0x1p+0f;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__xS4010, _M0L3valS4011, _M0L6_2atmpS4012);
        _M0L5to__yS4016 = _M0L4varsS1220->$3;
        _M0L3valS4017 = _M0L1iS1223->$0;
        _M0L5to__yS4020 = _M0L4varsS1220->$3;
        _M0L3valS4021 = _M0L1iS1223->$0;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4019
        = _M0MPC15array5Array2atGfE(_M0L5to__yS4020, _M0L3valS4021);
        _M0L6_2atmpS4018 = _M0L6_2atmpS4019 + 0x1p+0f;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS4016, _M0L3valS4017, _M0L6_2atmpS4018);
      }
      _M0L3valS4023 = _M0L1iS1223->$0;
      _M0L6_2atmpS4022 = _M0L3valS4023 + 1;
      _M0L1iS1223->$0 = _M0L6_2atmpS4022;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1223);
    }
    break;
  }
  _M0L4a__xS4070 = _M0L5paramS1217->$0;
  _M0L6tau__xS4072 = _M0L5paramS1217->$2;
  _M0L6_2atmpS4071 = 0x1p+1f * _M0L6tau__xS4072;
  _M0L7coef__xS1225 = _M0L4a__xS4070 / _M0L6_2atmpS4071;
  _M0L4a__yS4067 = _M0L5paramS1217->$1;
  _M0L6tau__yS4069 = _M0L5paramS1217->$3;
  _M0L6_2atmpS4068 = 0x1p+1f * _M0L6tau__yS4069;
  _M0L7coef__yS1226 = _M0L4a__yS4067 / _M0L6_2atmpS4068;
  _M0L1jS1219->$0 = 0;
  while (1) {
    int32_t _M0L3valS4024 = _M0L1jS1219->$0;
    if (_M0L3valS4024 < _M0L6n__preS1212) {
      int32_t _M0L3valS4066 = _M0L1jS1219->$0;
      int32_t _M0L5startS1227;
      int32_t _M0L3valS4065;
      int32_t _M0L6_2atmpS4064;
      int32_t _M0L3endS1229;
      int32_t _M0L3valS4063;
      int32_t _M0L10pre__firedS1230;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS4061;
      int32_t _M0L3valS4062;
      float _M0L8tr__x__jS1231;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS4059;
      int32_t _M0L3valS4060;
      float _M0L8tr__y__jS1232;
      struct _M0TPB8MutLocalGiE* _M0L1sS1233;
      int32_t _M0L3valS4058;
      int32_t _M0L6_2atmpS4057;
      #line 1346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1227
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1228, _M0L3valS4066);
      _M0L3valS4065 = _M0L1jS1219->$0;
      _M0L6_2atmpS4064 = _M0L3valS4065 + 1;
      #line 1347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1229
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1228, _M0L6_2atmpS4064);
      _M0L3valS4063 = _M0L1jS1219->$0;
      #line 1348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1230
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1213, _M0L3valS4063);
      _M0L5tr__xS4061 = _M0L4varsS1220->$0;
      _M0L3valS4062 = _M0L1jS1219->$0;
      #line 1349 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__x__jS1231
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS4061, _M0L3valS4062);
      _M0L5tr__yS4059 = _M0L4varsS1220->$1;
      _M0L3valS4060 = _M0L1jS1219->$0;
      #line 1350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__y__jS1232
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS4059, _M0L3valS4060);
      _M0L1sS1233
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1233)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1233->$0 = _M0L5startS1227;
      while (1) {
        int32_t _M0L3valS4025 = _M0L1sS1233->$0;
        if (_M0L3valS4025 < _M0L3endS1229) {
          int32_t _M0L3valS4056 = _M0L1sS1233->$0;
          int32_t _M0L9post__idxS1234;
          int32_t _M0L11post__firedS1236;
          struct _M0TPB5ArrayGfE* _M0L5to__xS4055;
          float _M0L8to__x__iS1237;
          struct _M0TPB5ArrayGfE* _M0L5to__yS4054;
          float _M0L8to__y__iS1238;
          int32_t _M0L3valS4044;
          float _M0L6_2atmpS4042;
          float _M0L6w__minS4043;
          int32_t _M0L3valS4049;
          float _M0L6_2atmpS4047;
          float _M0L6w__maxS4048;
          int32_t _M0L3valS4053;
          int32_t _M0L6_2atmpS4052;
          #line 1353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1234
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1235, _M0L3valS4056);
          #line 1354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1236
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1215, _M0L9post__idxS1234);
          _M0L5to__xS4055 = _M0L4varsS1220->$2;
          #line 1355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__x__iS1237
          = _M0MPC15array5Array2atGfE(_M0L5to__xS4055, _M0L9post__idxS1234);
          _M0L5to__yS4054 = _M0L4varsS1220->$3;
          #line 1356 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__y__iS1238
          = _M0MPC15array5Array2atGfE(_M0L5to__yS4054, _M0L9post__idxS1234);
          if (_M0L10pre__firedS1230) {
            float _M0L10alpha__preS4032 = _M0L5paramS1217->$4;
            float _M0L6_2atmpS4033 = _M0L7coef__xS1225 * _M0L8to__x__iS1237;
            float _M0L6_2atmpS4030 = _M0L10alpha__preS4032 + _M0L6_2atmpS4033;
            float _M0L6_2atmpS4031 = _M0L7coef__yS1226 * _M0L8to__y__iS1238;
            float _M0L2dwS1239 = _M0L6_2atmpS4030 - _M0L6_2atmpS4031;
            int32_t _M0L3valS4026 = _M0L1sS1233->$0;
            int32_t _M0L3valS4029 = _M0L1sS1233->$0;
            float _M0L6_2atmpS4028;
            float _M0L6_2atmpS4027;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4028
            = _M0MPC15array5Array2atGfE(_M0L1wS1240, _M0L3valS4029);
            _M0L6_2atmpS4027 = _M0L6_2atmpS4028 + _M0L2dwS1239;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1240, _M0L3valS4026, _M0L6_2atmpS4027);
          }
          if (_M0L11post__firedS1236) {
            float _M0L11alpha__postS4040 = _M0L5paramS1217->$5;
            float _M0L6_2atmpS4041 = _M0L7coef__xS1225 * _M0L8tr__x__jS1231;
            float _M0L6_2atmpS4038 =
              _M0L11alpha__postS4040 + _M0L6_2atmpS4041;
            float _M0L6_2atmpS4039 = _M0L7coef__yS1226 * _M0L8tr__y__jS1232;
            float _M0L2dwS1241 = _M0L6_2atmpS4038 - _M0L6_2atmpS4039;
            int32_t _M0L3valS4034 = _M0L1sS1233->$0;
            int32_t _M0L3valS4037 = _M0L1sS1233->$0;
            float _M0L6_2atmpS4036;
            float _M0L6_2atmpS4035;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4036
            = _M0MPC15array5Array2atGfE(_M0L1wS1240, _M0L3valS4037);
            _M0L6_2atmpS4035 = _M0L6_2atmpS4036 + _M0L2dwS1241;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1240, _M0L3valS4034, _M0L6_2atmpS4035);
          }
          _M0L3valS4044 = _M0L1sS1233->$0;
          #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS4042
          = _M0MPC15array5Array2atGfE(_M0L1wS1240, _M0L3valS4044);
          _M0L6w__minS4043 = _M0L5paramS1217->$7;
          if (_M0L6_2atmpS4042 < _M0L6w__minS4043) {
            int32_t _M0L3valS4045 = _M0L1sS1233->$0;
            float _M0L6w__minS4046 = _M0L5paramS1217->$7;
            #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1240, _M0L3valS4045, _M0L6w__minS4046);
          }
          _M0L3valS4049 = _M0L1sS1233->$0;
          #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS4047
          = _M0MPC15array5Array2atGfE(_M0L1wS1240, _M0L3valS4049);
          _M0L6w__maxS4048 = _M0L5paramS1217->$6;
          if (_M0L6_2atmpS4047 > _M0L6w__maxS4048) {
            int32_t _M0L3valS4050 = _M0L1sS1233->$0;
            float _M0L6w__maxS4051 = _M0L5paramS1217->$6;
            #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1240, _M0L3valS4050, _M0L6w__maxS4051);
          }
          _M0L3valS4053 = _M0L1sS1233->$0;
          _M0L6_2atmpS4052 = _M0L3valS4053 + 1;
          _M0L1sS1233->$0 = _M0L6_2atmpS4052;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1233);
        }
        break;
      }
      _M0L3valS4058 = _M0L1jS1219->$0;
      _M0L6_2atmpS4057 = _M0L3valS4058 + 1;
      _M0L1jS1219->$0 = _M0L6_2atmpS4057;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1219);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1208,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1185,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1187,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1205,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1199,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1193,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS1190,
  float _M0L6t__nowS1194,
  float _M0L2dtS1189
) {
  int32_t _M0L6n__preS1184;
  int32_t _M0L7n__postS1186;
  float _M0L6_2atmpS3942;
  float _M0L8tau__preS3943;
  float _M0L6_2atmpS3941;
  float _M0L10decay__preS1188;
  float _M0L6_2atmpS3939;
  float _M0L9tau__postS3940;
  float _M0L6_2atmpS3938;
  float _M0L11decay__postS1191;
  struct _M0TPB8MutLocalGiE* _M0L1jS1192;
  struct _M0TPB8MutLocalGiE* _M0L1iS1196;
  #line 1080 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1091 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1184 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1185);
  #line 1092 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1186 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1187);
  _M0L6_2atmpS3942 = -_M0L2dtS1189;
  _M0L8tau__preS3943 = _M0L5paramS1190->$5;
  _M0L6_2atmpS3941 = _M0L6_2atmpS3942 / _M0L8tau__preS3943;
  #line 1093 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1188 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3941);
  _M0L6_2atmpS3939 = -_M0L2dtS1189;
  _M0L9tau__postS3940 = _M0L5paramS1190->$6;
  _M0L6_2atmpS3938 = _M0L6_2atmpS3939 / _M0L9tau__postS3940;
  #line 1094 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1191 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3938);
  _M0L1jS1192
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1192)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1192->$0 = 0;
  while (1) {
    int32_t _M0L3valS3858 = _M0L1jS1192->$0;
    if (_M0L3valS3858 < _M0L6n__preS1184) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3859 = _M0L4varsS1193->$0;
      int32_t _M0L3valS3860 = _M0L1jS1192->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3863 = _M0L4varsS1193->$0;
      int32_t _M0L3valS3864 = _M0L1jS1192->$0;
      float _M0L6_2atmpS3862;
      float _M0L6_2atmpS3861;
      int32_t _M0L3valS3865;
      int32_t _M0L3valS3875;
      int32_t _M0L6_2atmpS3874;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3862
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3863, _M0L3valS3864);
      _M0L6_2atmpS3861 = _M0L6_2atmpS3862 * _M0L10decay__preS1188;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3859, _M0L3valS3860, _M0L6_2atmpS3861);
      _M0L3valS3865 = _M0L1jS1192->$0;
      #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1185, _M0L3valS3865)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3866 = _M0L4varsS1193->$0;
        int32_t _M0L3valS3867 = _M0L1jS1192->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3870 = _M0L4varsS1193->$0;
        int32_t _M0L3valS3871 = _M0L1jS1192->$0;
        float _M0L6_2atmpS3869;
        float _M0L6_2atmpS3868;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3872;
        int32_t _M0L3valS3873;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3869
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3870, _M0L3valS3871);
        _M0L6_2atmpS3868 = _M0L6_2atmpS3869 + 0x1p+0f;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3866, _M0L3valS3867, _M0L6_2atmpS3868);
        _M0L9last__preS3872 = _M0L4varsS1193->$2;
        _M0L3valS3873 = _M0L1jS1192->$0;
        #line 1101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3872, _M0L3valS3873, _M0L6t__nowS1194);
      }
      _M0L3valS3875 = _M0L1jS1192->$0;
      _M0L6_2atmpS3874 = _M0L3valS3875 + 1;
      _M0L1jS1192->$0 = _M0L6_2atmpS3874;
      continue;
    }
    break;
  }
  _M0L1iS1196
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1196)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1196->$0 = 0;
  while (1) {
    int32_t _M0L3valS3876 = _M0L1iS1196->$0;
    if (_M0L3valS3876 < _M0L7n__postS1186) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3877 = _M0L4varsS1193->$1;
      int32_t _M0L3valS3878 = _M0L1iS1196->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3881 = _M0L4varsS1193->$1;
      int32_t _M0L3valS3882 = _M0L1iS1196->$0;
      float _M0L6_2atmpS3880;
      float _M0L6_2atmpS3879;
      int32_t _M0L3valS3883;
      int32_t _M0L3valS3893;
      int32_t _M0L6_2atmpS3892;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3880
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3881, _M0L3valS3882);
      _M0L6_2atmpS3879 = _M0L6_2atmpS3880 * _M0L11decay__postS1191;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3877, _M0L3valS3878, _M0L6_2atmpS3879);
      _M0L3valS3883 = _M0L1iS1196->$0;
      #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1187, _M0L3valS3883)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3884 = _M0L4varsS1193->$1;
        int32_t _M0L3valS3885 = _M0L1iS1196->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3888 = _M0L4varsS1193->$1;
        int32_t _M0L3valS3889 = _M0L1iS1196->$0;
        float _M0L6_2atmpS3887;
        float _M0L6_2atmpS3886;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3890;
        int32_t _M0L3valS3891;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3887
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3888, _M0L3valS3889);
        _M0L6_2atmpS3886 = _M0L6_2atmpS3887 + 0x1p+0f;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3884, _M0L3valS3885, _M0L6_2atmpS3886);
        _M0L10last__postS3890 = _M0L4varsS1193->$3;
        _M0L3valS3891 = _M0L1iS1196->$0;
        #line 1110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3890, _M0L3valS3891, _M0L6t__nowS1194);
      }
      _M0L3valS3893 = _M0L1iS1196->$0;
      _M0L6_2atmpS3892 = _M0L3valS3893 + 1;
      _M0L1iS1196->$0 = _M0L6_2atmpS3892;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1196);
    }
    break;
  }
  _M0L1jS1192->$0 = 0;
  while (1) {
    int32_t _M0L3valS3894 = _M0L1jS1192->$0;
    if (_M0L3valS3894 < _M0L6n__preS1184) {
      int32_t _M0L3valS3937 = _M0L1jS1192->$0;
      int32_t _M0L5startS1198;
      int32_t _M0L3valS3936;
      int32_t _M0L6_2atmpS3935;
      int32_t _M0L3endS1200;
      int32_t _M0L3valS3934;
      int32_t _M0L10pre__firedS1201;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3932;
      int32_t _M0L3valS3933;
      float _M0L7tpre__jS1202;
      struct _M0TPB8MutLocalGiE* _M0L1sS1203;
      int32_t _M0L3valS3931;
      int32_t _M0L6_2atmpS3930;
      #line 1120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1198
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1199, _M0L3valS3937);
      _M0L3valS3936 = _M0L1jS1192->$0;
      _M0L6_2atmpS3935 = _M0L3valS3936 + 1;
      #line 1121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1200
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1199, _M0L6_2atmpS3935);
      _M0L3valS3934 = _M0L1jS1192->$0;
      #line 1122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1201
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1185, _M0L3valS3934);
      _M0L4tpreS3932 = _M0L4varsS1193->$0;
      _M0L3valS3933 = _M0L1jS1192->$0;
      #line 1123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L7tpre__jS1202
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3932, _M0L3valS3933);
      _M0L1sS1203
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1203)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1203->$0 = _M0L5startS1198;
      while (1) {
        int32_t _M0L3valS3895 = _M0L1sS1203->$0;
        if (_M0L3valS3895 < _M0L3endS1200) {
          int32_t _M0L3valS3929 = _M0L1sS1203->$0;
          int32_t _M0L9post__idxS1204;
          int32_t _M0L11post__firedS1206;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3928;
          float _M0L8tpost__iS1207;
          int32_t _M0L3valS3918;
          float _M0L6_2atmpS3916;
          float _M0L6w__minS3917;
          int32_t _M0L3valS3923;
          float _M0L6_2atmpS3921;
          float _M0L6w__maxS3922;
          int32_t _M0L3valS3927;
          int32_t _M0L6_2atmpS3926;
          #line 1126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1204
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1205, _M0L3valS3929);
          #line 1127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1206
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1187, _M0L9post__idxS1204);
          _M0L5tpostS3928 = _M0L4varsS1193->$1;
          #line 1128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8tpost__iS1207
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3928, _M0L9post__idxS1204);
          if (_M0L10pre__firedS1201) {
            int32_t _M0L3valS3896 = _M0L1sS1203->$0;
            int32_t _M0L3valS3905 = _M0L1sS1203->$0;
            float _M0L6_2atmpS3898;
            float _M0L3etaS3900;
            float _M0L5kappaS3904;
            float _M0L6_2atmpS3902;
            float _M0L5alphaS3903;
            float _M0L6_2atmpS3901;
            float _M0L6_2atmpS3899;
            float _M0L6_2atmpS3897;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3898
            = _M0MPC15array5Array2atGfE(_M0L1wS1208, _M0L3valS3905);
            _M0L3etaS3900 = _M0L5paramS1190->$0;
            _M0L5kappaS3904 = _M0L5paramS1190->$3;
            _M0L6_2atmpS3902 = _M0L5kappaS3904 * _M0L8tpost__iS1207;
            _M0L5alphaS3903 = _M0L5paramS1190->$1;
            _M0L6_2atmpS3901 = _M0L6_2atmpS3902 + _M0L5alphaS3903;
            _M0L6_2atmpS3899 = _M0L3etaS3900 * _M0L6_2atmpS3901;
            _M0L6_2atmpS3897 = _M0L6_2atmpS3898 + _M0L6_2atmpS3899;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1208, _M0L3valS3896, _M0L6_2atmpS3897);
          }
          if (_M0L11post__firedS1206) {
            int32_t _M0L3valS3906 = _M0L1sS1203->$0;
            int32_t _M0L3valS3915 = _M0L1sS1203->$0;
            float _M0L6_2atmpS3908;
            float _M0L3etaS3910;
            float _M0L5gammaS3914;
            float _M0L6_2atmpS3912;
            float _M0L4betaS3913;
            float _M0L6_2atmpS3911;
            float _M0L6_2atmpS3909;
            float _M0L6_2atmpS3907;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3908
            = _M0MPC15array5Array2atGfE(_M0L1wS1208, _M0L3valS3915);
            _M0L3etaS3910 = _M0L5paramS1190->$0;
            _M0L5gammaS3914 = _M0L5paramS1190->$4;
            _M0L6_2atmpS3912 = _M0L5gammaS3914 * _M0L7tpre__jS1202;
            _M0L4betaS3913 = _M0L5paramS1190->$2;
            _M0L6_2atmpS3911 = _M0L6_2atmpS3912 + _M0L4betaS3913;
            _M0L6_2atmpS3909 = _M0L3etaS3910 * _M0L6_2atmpS3911;
            _M0L6_2atmpS3907 = _M0L6_2atmpS3908 + _M0L6_2atmpS3909;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1208, _M0L3valS3906, _M0L6_2atmpS3907);
          }
          _M0L3valS3918 = _M0L1sS1203->$0;
          #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3916
          = _M0MPC15array5Array2atGfE(_M0L1wS1208, _M0L3valS3918);
          _M0L6w__minS3917 = _M0L5paramS1190->$8;
          if (_M0L6_2atmpS3916 < _M0L6w__minS3917) {
            int32_t _M0L3valS3919 = _M0L1sS1203->$0;
            float _M0L6w__minS3920 = _M0L5paramS1190->$8;
            #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1208, _M0L3valS3919, _M0L6w__minS3920);
          }
          _M0L3valS3923 = _M0L1sS1203->$0;
          #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3921
          = _M0MPC15array5Array2atGfE(_M0L1wS1208, _M0L3valS3923);
          _M0L6w__maxS3922 = _M0L5paramS1190->$7;
          if (_M0L6_2atmpS3921 > _M0L6w__maxS3922) {
            int32_t _M0L3valS3924 = _M0L1sS1203->$0;
            float _M0L6w__maxS3925 = _M0L5paramS1190->$7;
            #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1208, _M0L3valS3924, _M0L6w__maxS3925);
          }
          _M0L3valS3927 = _M0L1sS1203->$0;
          _M0L6_2atmpS3926 = _M0L3valS3927 + 1;
          _M0L1sS1203->$0 = _M0L6_2atmpS3926;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1203);
        }
        break;
      }
      _M0L3valS3931 = _M0L1jS1192->$0;
      _M0L6_2atmpS3930 = _M0L3valS3931 + 1;
      _M0L1jS1192->$0 = _M0L6_2atmpS3930;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1192);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1181,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1161,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1163,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1178,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1174,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1159,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS1166,
  float _M0L6t__nowS1169,
  float _M0L2dtS1165
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3775;
  int32_t _M0L6_2atmpS3774;
  int32_t _if__result_6059;
  int32_t _M0L6n__preS1160;
  int32_t _M0L7n__postS1162;
  float _M0L6_2atmpS3856;
  float _M0L8tau__preS3857;
  float _M0L6_2atmpS3855;
  float _M0L10decay__preS1164;
  float _M0L6_2atmpS3853;
  float _M0L9tau__postS3854;
  float _M0L6_2atmpS3852;
  float _M0L11decay__postS1167;
  struct _M0TPB8MutLocalGiE* _M0L1jS1168;
  struct _M0TPB8MutLocalGiE* _M0L1iS1171;
  #line 905 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3775 = _M0L4varsS1159->$4;
  #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3774 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3775);
  if (_M0L6_2atmpS3774 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3773 = _M0L4varsS1159->$4;
    int32_t _M0L6_2atmpS3772;
    #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3772 = _M0MPC15array5Array2atGbE(_M0L6activeS3773, 0);
    _if__result_6059 = !_M0L6_2atmpS3772;
  } else {
    _if__result_6059 = 0;
  }
  if (_if__result_6059) {
    return 0;
  }
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1160 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1161);
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1162 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1163);
  _M0L6_2atmpS3856 = -_M0L2dtS1165;
  _M0L8tau__preS3857 = _M0L5paramS1166->$2;
  _M0L6_2atmpS3855 = _M0L6_2atmpS3856 / _M0L8tau__preS3857;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1164 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3855);
  _M0L6_2atmpS3853 = -_M0L2dtS1165;
  _M0L9tau__postS3854 = _M0L5paramS1166->$3;
  _M0L6_2atmpS3852 = _M0L6_2atmpS3853 / _M0L9tau__postS3854;
  #line 924 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1167 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3852);
  _M0L1jS1168
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1168)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1168->$0 = 0;
  while (1) {
    int32_t _M0L3valS3776 = _M0L1jS1168->$0;
    if (_M0L3valS3776 < _M0L6n__preS1160) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3777 = _M0L4varsS1159->$0;
      int32_t _M0L3valS3778 = _M0L1jS1168->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3781 = _M0L4varsS1159->$0;
      int32_t _M0L3valS3782 = _M0L1jS1168->$0;
      float _M0L6_2atmpS3780;
      float _M0L6_2atmpS3779;
      int32_t _M0L3valS3783;
      int32_t _M0L3valS3794;
      int32_t _M0L6_2atmpS3793;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3780
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3781, _M0L3valS3782);
      _M0L6_2atmpS3779 = _M0L6_2atmpS3780 * _M0L10decay__preS1164;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3777, _M0L3valS3778, _M0L6_2atmpS3779);
      _M0L3valS3783 = _M0L1jS1168->$0;
      #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1161, _M0L3valS3783)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3784 = _M0L4varsS1159->$0;
        int32_t _M0L3valS3785 = _M0L1jS1168->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3789 = _M0L4varsS1159->$0;
        int32_t _M0L3valS3790 = _M0L1jS1168->$0;
        float _M0L6_2atmpS3787;
        float _M0L6a__preS3788;
        float _M0L6_2atmpS3786;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3791;
        int32_t _M0L3valS3792;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3787
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3789, _M0L3valS3790);
        _M0L6a__preS3788 = _M0L5paramS1166->$0;
        _M0L6_2atmpS3786 = _M0L6_2atmpS3787 + _M0L6a__preS3788;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3784, _M0L3valS3785, _M0L6_2atmpS3786);
        _M0L9last__preS3791 = _M0L4varsS1159->$2;
        _M0L3valS3792 = _M0L1jS1168->$0;
        #line 930 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3791, _M0L3valS3792, _M0L6t__nowS1169);
      }
      _M0L3valS3794 = _M0L1jS1168->$0;
      _M0L6_2atmpS3793 = _M0L3valS3794 + 1;
      _M0L1jS1168->$0 = _M0L6_2atmpS3793;
      continue;
    }
    break;
  }
  _M0L1iS1171
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1171)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1171->$0 = 0;
  while (1) {
    int32_t _M0L3valS3795 = _M0L1iS1171->$0;
    if (_M0L3valS3795 < _M0L7n__postS1162) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3796 = _M0L4varsS1159->$1;
      int32_t _M0L3valS3797 = _M0L1iS1171->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3800 = _M0L4varsS1159->$1;
      int32_t _M0L3valS3801 = _M0L1iS1171->$0;
      float _M0L6_2atmpS3799;
      float _M0L6_2atmpS3798;
      int32_t _M0L3valS3802;
      int32_t _M0L3valS3813;
      int32_t _M0L6_2atmpS3812;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3799
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3800, _M0L3valS3801);
      _M0L6_2atmpS3798 = _M0L6_2atmpS3799 * _M0L11decay__postS1167;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3796, _M0L3valS3797, _M0L6_2atmpS3798);
      _M0L3valS3802 = _M0L1iS1171->$0;
      #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1163, _M0L3valS3802)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3803 = _M0L4varsS1159->$1;
        int32_t _M0L3valS3804 = _M0L1iS1171->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3808 = _M0L4varsS1159->$1;
        int32_t _M0L3valS3809 = _M0L1iS1171->$0;
        float _M0L6_2atmpS3806;
        float _M0L7a__postS3807;
        float _M0L6_2atmpS3805;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3810;
        int32_t _M0L3valS3811;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3806
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3808, _M0L3valS3809);
        _M0L7a__postS3807 = _M0L5paramS1166->$1;
        _M0L6_2atmpS3805 = _M0L6_2atmpS3806 + _M0L7a__postS3807;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3803, _M0L3valS3804, _M0L6_2atmpS3805);
        _M0L10last__postS3810 = _M0L4varsS1159->$3;
        _M0L3valS3811 = _M0L1iS1171->$0;
        #line 939 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3810, _M0L3valS3811, _M0L6t__nowS1169);
      }
      _M0L3valS3813 = _M0L1iS1171->$0;
      _M0L6_2atmpS3812 = _M0L3valS3813 + 1;
      _M0L1iS1171->$0 = _M0L6_2atmpS3812;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1171);
    }
    break;
  }
  _M0L1jS1168->$0 = 0;
  while (1) {
    int32_t _M0L3valS3814 = _M0L1jS1168->$0;
    if (_M0L3valS3814 < _M0L6n__preS1160) {
      int32_t _M0L3valS3851 = _M0L1jS1168->$0;
      int32_t _M0L5startS1173;
      int32_t _M0L3valS3850;
      int32_t _M0L6_2atmpS3849;
      int32_t _M0L3endS1175;
      struct _M0TPB8MutLocalGiE* _M0L1sS1176;
      int32_t _M0L3valS3848;
      int32_t _M0L6_2atmpS3847;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1173
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1174, _M0L3valS3851);
      _M0L3valS3850 = _M0L1jS1168->$0;
      _M0L6_2atmpS3849 = _M0L3valS3850 + 1;
      #line 948 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1175
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1174, _M0L6_2atmpS3849);
      _M0L1sS1176
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1176)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1176->$0 = _M0L5startS1173;
      while (1) {
        int32_t _M0L3valS3815 = _M0L1sS1176->$0;
        if (_M0L3valS3815 < _M0L3endS1175) {
          int32_t _M0L3valS3846 = _M0L1sS1176->$0;
          int32_t _M0L9post__idxS1177;
          int32_t _M0L3valS3845;
          int32_t _M0L10pre__firedS1179;
          int32_t _M0L11post__firedS1180;
          int32_t _M0L3valS3835;
          float _M0L6_2atmpS3833;
          float _M0L6w__minS3834;
          int32_t _M0L3valS3840;
          float _M0L6_2atmpS3838;
          float _M0L6w__maxS3839;
          int32_t _M0L3valS3844;
          int32_t _M0L6_2atmpS3843;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1177
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1178, _M0L3valS3846);
          _M0L3valS3845 = _M0L1jS1168->$0;
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS1179
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1161, _M0L3valS3845);
          #line 953 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1180
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1163, _M0L9post__idxS1177);
          if (_M0L10pre__firedS1179) {
            int32_t _M0L3valS3816 = _M0L1sS1176->$0;
            int32_t _M0L3valS3823 = _M0L1sS1176->$0;
            float _M0L6_2atmpS3818;
            float _M0L7a__postS3820;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3822;
            float _M0L6_2atmpS3821;
            float _M0L6_2atmpS3819;
            float _M0L6_2atmpS3817;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3818
            = _M0MPC15array5Array2atGfE(_M0L1wS1181, _M0L3valS3823);
            _M0L7a__postS3820 = _M0L5paramS1166->$1;
            _M0L5tpostS3822 = _M0L4varsS1159->$1;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3821
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3822, _M0L9post__idxS1177);
            _M0L6_2atmpS3819 = _M0L7a__postS3820 * _M0L6_2atmpS3821;
            _M0L6_2atmpS3817 = _M0L6_2atmpS3818 + _M0L6_2atmpS3819;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1181, _M0L3valS3816, _M0L6_2atmpS3817);
          }
          if (_M0L11post__firedS1180) {
            int32_t _M0L3valS3824 = _M0L1sS1176->$0;
            int32_t _M0L3valS3832 = _M0L1sS1176->$0;
            float _M0L6_2atmpS3826;
            float _M0L6a__preS3828;
            struct _M0TPB5ArrayGfE* _M0L4tpreS3830;
            int32_t _M0L3valS3831;
            float _M0L6_2atmpS3829;
            float _M0L6_2atmpS3827;
            float _M0L6_2atmpS3825;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3826
            = _M0MPC15array5Array2atGfE(_M0L1wS1181, _M0L3valS3832);
            _M0L6a__preS3828 = _M0L5paramS1166->$0;
            _M0L4tpreS3830 = _M0L4varsS1159->$0;
            _M0L3valS3831 = _M0L1jS1168->$0;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3829
            = _M0MPC15array5Array2atGfE(_M0L4tpreS3830, _M0L3valS3831);
            _M0L6_2atmpS3827 = _M0L6a__preS3828 * _M0L6_2atmpS3829;
            _M0L6_2atmpS3825 = _M0L6_2atmpS3826 + _M0L6_2atmpS3827;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1181, _M0L3valS3824, _M0L6_2atmpS3825);
          }
          _M0L3valS3835 = _M0L1sS1176->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3833
          = _M0MPC15array5Array2atGfE(_M0L1wS1181, _M0L3valS3835);
          _M0L6w__minS3834 = _M0L5paramS1166->$5;
          if (_M0L6_2atmpS3833 < _M0L6w__minS3834) {
            int32_t _M0L3valS3836 = _M0L1sS1176->$0;
            float _M0L6w__minS3837 = _M0L5paramS1166->$5;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1181, _M0L3valS3836, _M0L6w__minS3837);
          }
          _M0L3valS3840 = _M0L1sS1176->$0;
          #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3838
          = _M0MPC15array5Array2atGfE(_M0L1wS1181, _M0L3valS3840);
          _M0L6w__maxS3839 = _M0L5paramS1166->$4;
          if (_M0L6_2atmpS3838 > _M0L6w__maxS3839) {
            int32_t _M0L3valS3841 = _M0L1sS1176->$0;
            float _M0L6w__maxS3842 = _M0L5paramS1166->$4;
            #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1181, _M0L3valS3841, _M0L6w__maxS3842);
          }
          _M0L3valS3844 = _M0L1sS1176->$0;
          _M0L6_2atmpS3843 = _M0L3valS3844 + 1;
          _M0L1sS1176->$0 = _M0L6_2atmpS3843;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1176);
        }
        break;
      }
      _M0L3valS3848 = _M0L1jS1168->$0;
      _M0L6_2atmpS3847 = _M0L3valS3848 + 1;
      _M0L1jS1168->$0 = _M0L6_2atmpS3847;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1168);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1138,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1128,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1130,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1137,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1133,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS1140,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS1139,
  float _M0L2dtS1152
) {
  int32_t _M0L6n__preS1127;
  int32_t _M0L7n__postS1129;
  struct _M0TPB8MutLocalGiE* _M0L1jS1131;
  int32_t _M0L3nnzS1143;
  float _M0L4a__xS3770;
  float _M0L6tau__xS3771;
  float _M0L18a__x__over__tau__xS1144;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1145;
  float _M0L6tau__xS3769;
  float _M0L11inv__tau__xS1149;
  float _M0L6tau__yS3768;
  float _M0L11inv__tau__yS1150;
  struct _M0TPB8MutLocalGiE* _M0L1iS1151;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1157;
  #line 622 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 632 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1127 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1128);
  #line 633 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1129 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1130);
  _M0L1jS1131
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1131)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1131->$0 = 0;
  while (1) {
    int32_t _M0L3valS3668 = _M0L1jS1131->$0;
    if (_M0L3valS3668 < _M0L6n__preS1127) {
      int32_t _M0L3valS3669 = _M0L1jS1131->$0;
      int32_t _M0L3valS3690;
      int32_t _M0L6_2atmpS3689;
      #line 637 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1128, _M0L3valS3669)) {
        int32_t _M0L3valS3688 = _M0L1jS1131->$0;
        int32_t _M0L5startS1132;
        int32_t _M0L3valS3687;
        int32_t _M0L6_2atmpS3686;
        int32_t _M0L3endS1134;
        struct _M0TPB8MutLocalGiE* _M0L1sS1135;
        #line 638 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1132
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1133, _M0L3valS3688);
        _M0L3valS3687 = _M0L1jS1131->$0;
        _M0L6_2atmpS3686 = _M0L3valS3687 + 1;
        #line 639 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1134
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1133, _M0L6_2atmpS3686);
        _M0L1sS1135
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1135)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1135->$0 = _M0L5startS1132;
        while (1) {
          int32_t _M0L3valS3670 = _M0L1sS1135->$0;
          if (_M0L3valS3670 < _M0L3endS1134) {
            int32_t _M0L3valS3685 = _M0L1sS1135->$0;
            int32_t _M0L9post__idxS1136;
            int32_t _M0L3valS3671;
            int32_t _M0L3valS3682;
            float _M0L6_2atmpS3680;
            float _M0L10alpha__preS3681;
            float _M0L6_2atmpS3673;
            float _M0L4a__yS3678;
            float _M0L6tau__yS3679;
            float _M0L6_2atmpS3675;
            struct _M0TPB5ArrayGfE* _M0L5to__yS3677;
            float _M0L6_2atmpS3676;
            float _M0L6_2atmpS3674;
            float _M0L6_2atmpS3672;
            int32_t _M0L3valS3684;
            int32_t _M0L6_2atmpS3683;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1136
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1137, _M0L3valS3685);
            _M0L3valS3671 = _M0L1sS1135->$0;
            _M0L3valS3682 = _M0L1sS1135->$0;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3680
            = _M0MPC15array5Array2atGfE(_M0L1wS1138, _M0L3valS3682);
            _M0L10alpha__preS3681 = _M0L5paramS1139->$4;
            _M0L6_2atmpS3673 = _M0L6_2atmpS3680 + _M0L10alpha__preS3681;
            _M0L4a__yS3678 = _M0L5paramS1139->$1;
            _M0L6tau__yS3679 = _M0L5paramS1139->$3;
            _M0L6_2atmpS3675 = _M0L4a__yS3678 / _M0L6tau__yS3679;
            _M0L5to__yS3677 = _M0L4varsS1140->$1;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3676
            = _M0MPC15array5Array2atGfE(_M0L5to__yS3677, _M0L9post__idxS1136);
            _M0L6_2atmpS3674 = _M0L6_2atmpS3675 * _M0L6_2atmpS3676;
            _M0L6_2atmpS3672 = _M0L6_2atmpS3673 - _M0L6_2atmpS3674;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1138, _M0L3valS3671, _M0L6_2atmpS3672);
            _M0L3valS3684 = _M0L1sS1135->$0;
            _M0L6_2atmpS3683 = _M0L3valS3684 + 1;
            _M0L1sS1135->$0 = _M0L6_2atmpS3683;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1135);
          }
          break;
        }
      }
      _M0L3valS3690 = _M0L1jS1131->$0;
      _M0L6_2atmpS3689 = _M0L3valS3690 + 1;
      _M0L1jS1131->$0 = _M0L6_2atmpS3689;
      continue;
    }
    break;
  }
  #line 650 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1143 = _M0MPC15array5Array6lengthGfE(_M0L1wS1138);
  _M0L4a__xS3770 = _M0L5paramS1139->$0;
  _M0L6tau__xS3771 = _M0L5paramS1139->$2;
  _M0L18a__x__over__tau__xS1144 = _M0L4a__xS3770 / _M0L6tau__xS3771;
  _M0L2s2S1145
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1145)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1145->$0 = 0;
  while (1) {
    int32_t _M0L3valS3691 = _M0L2s2S1145->$0;
    if (_M0L3valS3691 < _M0L3nnzS1143) {
      int32_t _M0L3valS3704 = _M0L2s2S1145->$0;
      int32_t _M0L9post__idxS1146;
      int32_t _M0L3valS3703;
      int32_t _M0L6_2atmpS3702;
      #line 654 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1146
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1137, _M0L3valS3704);
      #line 655 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1130, _M0L9post__idxS1146)
      ) {
        int32_t _M0L3valS3701 = _M0L2s2S1145->$0;
        int32_t _M0L6j__preS1147;
        int32_t _M0L3valS3692;
        int32_t _M0L3valS3700;
        float _M0L6_2atmpS3698;
        float _M0L11alpha__postS3699;
        float _M0L6_2atmpS3694;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3697;
        float _M0L6_2atmpS3696;
        float _M0L6_2atmpS3695;
        float _M0L6_2atmpS3693;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1147
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1133, _M0L3valS3701);
        _M0L3valS3692 = _M0L2s2S1145->$0;
        _M0L3valS3700 = _M0L2s2S1145->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3698
        = _M0MPC15array5Array2atGfE(_M0L1wS1138, _M0L3valS3700);
        _M0L11alpha__postS3699 = _M0L5paramS1139->$5;
        _M0L6_2atmpS3694 = _M0L6_2atmpS3698 + _M0L11alpha__postS3699;
        _M0L5tr__xS3697 = _M0L4varsS1140->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3696
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3697, _M0L6j__preS1147);
        _M0L6_2atmpS3695 = _M0L18a__x__over__tau__xS1144 * _M0L6_2atmpS3696;
        _M0L6_2atmpS3693 = _M0L6_2atmpS3694 + _M0L6_2atmpS3695;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1138, _M0L3valS3692, _M0L6_2atmpS3693);
      }
      _M0L3valS3703 = _M0L2s2S1145->$0;
      _M0L6_2atmpS3702 = _M0L3valS3703 + 1;
      _M0L2s2S1145->$0 = _M0L6_2atmpS3702;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1145);
    }
    break;
  }
  _M0L6tau__xS3769 = _M0L5paramS1139->$2;
  _M0L11inv__tau__xS1149 = 0x1p+0f / _M0L6tau__xS3769;
  _M0L6tau__yS3768 = _M0L5paramS1139->$3;
  _M0L11inv__tau__yS1150 = 0x1p+0f / _M0L6tau__yS3768;
  _M0L1iS1151
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1151)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1151->$0 = 0;
  while (1) {
    int32_t _M0L3valS3705 = _M0L1iS1151->$0;
    if (_M0L3valS3705 < _M0L7n__postS1129) {
      struct _M0TPB5ArrayGfE* _M0L5to__yS3706 = _M0L4varsS1140->$1;
      int32_t _M0L3valS3707 = _M0L1iS1151->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3716 = _M0L4varsS1140->$1;
      int32_t _M0L3valS3717 = _M0L1iS1151->$0;
      float _M0L6_2atmpS3709;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3714;
      int32_t _M0L3valS3715;
      float _M0L6_2atmpS3713;
      float _M0L6_2atmpS3712;
      float _M0L6_2atmpS3711;
      float _M0L6_2atmpS3710;
      float _M0L6_2atmpS3708;
      int32_t _M0L3valS3719;
      int32_t _M0L6_2atmpS3718;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3709
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3716, _M0L3valS3717);
      _M0L5to__yS3714 = _M0L4varsS1140->$1;
      _M0L3valS3715 = _M0L1iS1151->$0;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3713
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3714, _M0L3valS3715);
      _M0L6_2atmpS3712 = -_M0L6_2atmpS3713;
      _M0L6_2atmpS3711 = _M0L2dtS1152 * _M0L6_2atmpS3712;
      _M0L6_2atmpS3710 = _M0L6_2atmpS3711 * _M0L11inv__tau__yS1150;
      _M0L6_2atmpS3708 = _M0L6_2atmpS3709 + _M0L6_2atmpS3710;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3706, _M0L3valS3707, _M0L6_2atmpS3708);
      _M0L3valS3719 = _M0L1iS1151->$0;
      _M0L6_2atmpS3718 = _M0L3valS3719 + 1;
      _M0L1iS1151->$0 = _M0L6_2atmpS3718;
      continue;
    }
    break;
  }
  _M0L1jS1131->$0 = 0;
  while (1) {
    int32_t _M0L3valS3720 = _M0L1jS1131->$0;
    if (_M0L3valS3720 < _M0L6n__preS1127) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3721 = _M0L4varsS1140->$0;
      int32_t _M0L3valS3722 = _M0L1jS1131->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3731 = _M0L4varsS1140->$0;
      int32_t _M0L3valS3732 = _M0L1jS1131->$0;
      float _M0L6_2atmpS3724;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3729;
      int32_t _M0L3valS3730;
      float _M0L6_2atmpS3728;
      float _M0L6_2atmpS3727;
      float _M0L6_2atmpS3726;
      float _M0L6_2atmpS3725;
      float _M0L6_2atmpS3723;
      int32_t _M0L3valS3734;
      int32_t _M0L6_2atmpS3733;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3724
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3731, _M0L3valS3732);
      _M0L5tr__xS3729 = _M0L4varsS1140->$0;
      _M0L3valS3730 = _M0L1jS1131->$0;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3728
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3729, _M0L3valS3730);
      _M0L6_2atmpS3727 = -_M0L6_2atmpS3728;
      _M0L6_2atmpS3726 = _M0L2dtS1152 * _M0L6_2atmpS3727;
      _M0L6_2atmpS3725 = _M0L6_2atmpS3726 * _M0L11inv__tau__xS1149;
      _M0L6_2atmpS3723 = _M0L6_2atmpS3724 + _M0L6_2atmpS3725;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3721, _M0L3valS3722, _M0L6_2atmpS3723);
      _M0L3valS3734 = _M0L1jS1131->$0;
      _M0L6_2atmpS3733 = _M0L3valS3734 + 1;
      _M0L1jS1131->$0 = _M0L6_2atmpS3733;
      continue;
    }
    break;
  }
  _M0L1iS1151->$0 = 0;
  while (1) {
    int32_t _M0L3valS3735 = _M0L1iS1151->$0;
    if (_M0L3valS3735 < _M0L7n__postS1129) {
      int32_t _M0L3valS3736 = _M0L1iS1151->$0;
      int32_t _M0L3valS3744;
      int32_t _M0L6_2atmpS3743;
      #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1130, _M0L3valS3736)) {
        struct _M0TPB5ArrayGfE* _M0L5to__yS3737 = _M0L4varsS1140->$1;
        int32_t _M0L3valS3738 = _M0L1iS1151->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3741 = _M0L4varsS1140->$1;
        int32_t _M0L3valS3742 = _M0L1iS1151->$0;
        float _M0L6_2atmpS3740;
        float _M0L6_2atmpS3739;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3740
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3741, _M0L3valS3742);
        _M0L6_2atmpS3739 = _M0L6_2atmpS3740 + 0x1p+0f;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3737, _M0L3valS3738, _M0L6_2atmpS3739);
      }
      _M0L3valS3744 = _M0L1iS1151->$0;
      _M0L6_2atmpS3743 = _M0L3valS3744 + 1;
      _M0L1iS1151->$0 = _M0L6_2atmpS3743;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1151);
    }
    break;
  }
  _M0L1jS1131->$0 = 0;
  while (1) {
    int32_t _M0L3valS3745 = _M0L1jS1131->$0;
    if (_M0L3valS3745 < _M0L6n__preS1127) {
      int32_t _M0L3valS3746 = _M0L1jS1131->$0;
      int32_t _M0L3valS3754;
      int32_t _M0L6_2atmpS3753;
      #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1128, _M0L3valS3746)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3747 = _M0L4varsS1140->$0;
        int32_t _M0L3valS3748 = _M0L1jS1131->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3751 = _M0L4varsS1140->$0;
        int32_t _M0L3valS3752 = _M0L1jS1131->$0;
        float _M0L6_2atmpS3750;
        float _M0L6_2atmpS3749;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3750
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3751, _M0L3valS3752);
        _M0L6_2atmpS3749 = _M0L6_2atmpS3750 + 0x1p+0f;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3747, _M0L3valS3748, _M0L6_2atmpS3749);
      }
      _M0L3valS3754 = _M0L1jS1131->$0;
      _M0L6_2atmpS3753 = _M0L3valS3754 + 1;
      _M0L1jS1131->$0 = _M0L6_2atmpS3753;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1131);
    }
    break;
  }
  _M0L2s3S1157
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1157)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1157->$0 = 0;
  while (1) {
    int32_t _M0L3valS3755 = _M0L2s3S1157->$0;
    if (_M0L3valS3755 < _M0L3nnzS1143) {
      int32_t _M0L3valS3758 = _M0L2s3S1157->$0;
      float _M0L6_2atmpS3756;
      float _M0L6w__minS3757;
      int32_t _M0L3valS3767;
      int32_t _M0L6_2atmpS3766;
      #line 692 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3756
      = _M0MPC15array5Array2atGfE(_M0L1wS1138, _M0L3valS3758);
      _M0L6w__minS3757 = _M0L5paramS1139->$7;
      if (_M0L6_2atmpS3756 < _M0L6w__minS3757) {
        int32_t _M0L3valS3759 = _M0L2s3S1157->$0;
        float _M0L6w__minS3760 = _M0L5paramS1139->$7;
        #line 693 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1138, _M0L3valS3759, _M0L6w__minS3760);
      } else {
        int32_t _M0L3valS3763 = _M0L2s3S1157->$0;
        float _M0L6_2atmpS3761;
        float _M0L6w__maxS3762;
        #line 694 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3761
        = _M0MPC15array5Array2atGfE(_M0L1wS1138, _M0L3valS3763);
        _M0L6w__maxS3762 = _M0L5paramS1139->$6;
        if (_M0L6_2atmpS3761 > _M0L6w__maxS3762) {
          int32_t _M0L3valS3764 = _M0L2s3S1157->$0;
          float _M0L6w__maxS3765 = _M0L5paramS1139->$6;
          #line 695 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1138, _M0L3valS3764, _M0L6w__maxS3765);
        }
      }
      _M0L3valS3767 = _M0L2s3S1157->$0;
      _M0L6_2atmpS3766 = _M0L3valS3767 + 1;
      _M0L2s3S1157->$0 = _M0L6_2atmpS3766;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1157);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1113,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1089,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1091,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1108,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1104,
  struct _M0TPB5ArrayGfE* _M0L4tpreS1099,
  struct _M0TPB5ArrayGfE* _M0L5tpostS1095,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS1093,
  float _M0L2dtS1096
) {
  int32_t _M0L6n__preS1088;
  int32_t _M0L7n__postS1090;
  float _M0L3tauS3667;
  float _M0L8inv__tauS1092;
  struct _M0TPB8MutLocalGiE* _M0L1iS1094;
  struct _M0TPB8MutLocalGiE* _M0L1jS1098;
  int32_t _M0L3nnzS1116;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1117;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1125;
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 461 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1088 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1089);
  #line 462 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1090 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1091);
  _M0L3tauS3667 = _M0L5paramS1093->$1;
  _M0L8inv__tauS1092 = 0x1p+0f / _M0L3tauS3667;
  _M0L1iS1094
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1094)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1094->$0 = 0;
  while (1) {
    int32_t _M0L3valS3581 = _M0L1iS1094->$0;
    if (_M0L3valS3581 < _M0L7n__postS1090) {
      int32_t _M0L3valS3582 = _M0L1iS1094->$0;
      int32_t _M0L3valS3590 = _M0L1iS1094->$0;
      float _M0L6_2atmpS3584;
      int32_t _M0L3valS3589;
      float _M0L6_2atmpS3588;
      float _M0L6_2atmpS3587;
      float _M0L6_2atmpS3586;
      float _M0L6_2atmpS3585;
      float _M0L6_2atmpS3583;
      int32_t _M0L3valS3592;
      int32_t _M0L6_2atmpS3591;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3584
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1095, _M0L3valS3590);
      _M0L3valS3589 = _M0L1iS1094->$0;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3588
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1095, _M0L3valS3589);
      _M0L6_2atmpS3587 = -_M0L6_2atmpS3588;
      _M0L6_2atmpS3586 = _M0L2dtS1096 * _M0L6_2atmpS3587;
      _M0L6_2atmpS3585 = _M0L6_2atmpS3586 * _M0L8inv__tauS1092;
      _M0L6_2atmpS3583 = _M0L6_2atmpS3584 + _M0L6_2atmpS3585;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS1095, _M0L3valS3582, _M0L6_2atmpS3583);
      _M0L3valS3592 = _M0L1iS1094->$0;
      _M0L6_2atmpS3591 = _M0L3valS3592 + 1;
      _M0L1iS1094->$0 = _M0L6_2atmpS3591;
      continue;
    }
    break;
  }
  _M0L1jS1098
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1098)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1098->$0 = 0;
  while (1) {
    int32_t _M0L3valS3593 = _M0L1jS1098->$0;
    if (_M0L3valS3593 < _M0L6n__preS1088) {
      int32_t _M0L3valS3594 = _M0L1jS1098->$0;
      int32_t _M0L3valS3602 = _M0L1jS1098->$0;
      float _M0L6_2atmpS3596;
      int32_t _M0L3valS3601;
      float _M0L6_2atmpS3600;
      float _M0L6_2atmpS3599;
      float _M0L6_2atmpS3598;
      float _M0L6_2atmpS3597;
      float _M0L6_2atmpS3595;
      int32_t _M0L3valS3604;
      int32_t _M0L6_2atmpS3603;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3596
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1099, _M0L3valS3602);
      _M0L3valS3601 = _M0L1jS1098->$0;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3600
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1099, _M0L3valS3601);
      _M0L6_2atmpS3599 = -_M0L6_2atmpS3600;
      _M0L6_2atmpS3598 = _M0L2dtS1096 * _M0L6_2atmpS3599;
      _M0L6_2atmpS3597 = _M0L6_2atmpS3598 * _M0L8inv__tauS1092;
      _M0L6_2atmpS3595 = _M0L6_2atmpS3596 + _M0L6_2atmpS3597;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS1099, _M0L3valS3594, _M0L6_2atmpS3595);
      _M0L3valS3604 = _M0L1jS1098->$0;
      _M0L6_2atmpS3603 = _M0L3valS3604 + 1;
      _M0L1jS1098->$0 = _M0L6_2atmpS3603;
      continue;
    }
    break;
  }
  _M0L1iS1094->$0 = 0;
  while (1) {
    int32_t _M0L3valS3605 = _M0L1iS1094->$0;
    if (_M0L3valS3605 < _M0L7n__postS1090) {
      int32_t _M0L3valS3606 = _M0L1iS1094->$0;
      int32_t _M0L3valS3612;
      int32_t _M0L6_2atmpS3611;
      #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1091, _M0L3valS3606)) {
        int32_t _M0L3valS3607 = _M0L1iS1094->$0;
        int32_t _M0L3valS3610 = _M0L1iS1094->$0;
        float _M0L6_2atmpS3609;
        float _M0L6_2atmpS3608;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3609
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1095, _M0L3valS3610);
        _M0L6_2atmpS3608 = _M0L6_2atmpS3609 + 0x1p+0f;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS1095, _M0L3valS3607, _M0L6_2atmpS3608);
      }
      _M0L3valS3612 = _M0L1iS1094->$0;
      _M0L6_2atmpS3611 = _M0L3valS3612 + 1;
      _M0L1iS1094->$0 = _M0L6_2atmpS3611;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1094);
    }
    break;
  }
  _M0L1jS1098->$0 = 0;
  while (1) {
    int32_t _M0L3valS3613 = _M0L1jS1098->$0;
    if (_M0L3valS3613 < _M0L6n__preS1088) {
      int32_t _M0L3valS3614 = _M0L1jS1098->$0;
      int32_t _M0L3valS3620;
      int32_t _M0L6_2atmpS3619;
      #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1089, _M0L3valS3614)) {
        int32_t _M0L3valS3615 = _M0L1jS1098->$0;
        int32_t _M0L3valS3618 = _M0L1jS1098->$0;
        float _M0L6_2atmpS3617;
        float _M0L6_2atmpS3616;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3617
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1099, _M0L3valS3618);
        _M0L6_2atmpS3616 = _M0L6_2atmpS3617 + 0x1p+0f;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS1099, _M0L3valS3615, _M0L6_2atmpS3616);
      }
      _M0L3valS3620 = _M0L1jS1098->$0;
      _M0L6_2atmpS3619 = _M0L3valS3620 + 1;
      _M0L1jS1098->$0 = _M0L6_2atmpS3619;
      continue;
    }
    break;
  }
  _M0L1jS1098->$0 = 0;
  while (1) {
    int32_t _M0L3valS3621 = _M0L1jS1098->$0;
    if (_M0L3valS3621 < _M0L6n__preS1088) {
      int32_t _M0L3valS3622 = _M0L1jS1098->$0;
      int32_t _M0L3valS3640;
      int32_t _M0L6_2atmpS3639;
      #line 493 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1089, _M0L3valS3622)) {
        int32_t _M0L3valS3638 = _M0L1jS1098->$0;
        int32_t _M0L5startS1103;
        int32_t _M0L3valS3637;
        int32_t _M0L6_2atmpS3636;
        int32_t _M0L3endS1105;
        struct _M0TPB8MutLocalGiE* _M0L1sS1106;
        #line 494 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1103
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1104, _M0L3valS3638);
        _M0L3valS3637 = _M0L1jS1098->$0;
        _M0L6_2atmpS3636 = _M0L3valS3637 + 1;
        #line 495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1105
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1104, _M0L6_2atmpS3636);
        _M0L1sS1106
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1106)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1106->$0 = _M0L5startS1103;
        while (1) {
          int32_t _M0L3valS3623 = _M0L1sS1106->$0;
          if (_M0L3valS3623 < _M0L3endS1105) {
            int32_t _M0L3valS3635 = _M0L1sS1106->$0;
            int32_t _M0L9post__idxS1107;
            int32_t _M0L3valS3634;
            float _M0L6_2atmpS3632;
            float _M0L6_2atmpS3633;
            float _M0L5ratioS1109;
            float _M0L3lnxS1110;
            float _M0L1xS1111;
            float _M0L1aS3630;
            float _M0L6_2atmpS3631;
            float _M0L2dwS1112;
            int32_t _M0L3valS3624;
            int32_t _M0L3valS3627;
            float _M0L6_2atmpS3626;
            float _M0L6_2atmpS3625;
            int32_t _M0L3valS3629;
            int32_t _M0L6_2atmpS3628;
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1107
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1108, _M0L3valS3635);
            _M0L3valS3634 = _M0L1jS1098->$0;
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3632
            = _M0MPC15array5Array2atGfE(_M0L4tpreS1099, _M0L3valS3634);
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3633
            = _M0MPC15array5Array2atGfE(_M0L5tpostS1095, _M0L9post__idxS1107);
            _M0L5ratioS1109 = _M0L6_2atmpS3632 / _M0L6_2atmpS3633;
            #line 500 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L3lnxS1110 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1109);
            _M0L1xS1111 = _M0L3lnxS1110 * _M0L3lnxS1110;
            _M0L1aS3630 = _M0L5paramS1093->$0;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3631
            = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1111);
            _M0L2dwS1112 = _M0L1aS3630 * _M0L6_2atmpS3631;
            _M0L3valS3624 = _M0L1sS1106->$0;
            _M0L3valS3627 = _M0L1sS1106->$0;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3626
            = _M0MPC15array5Array2atGfE(_M0L1wS1113, _M0L3valS3627);
            _M0L6_2atmpS3625 = _M0L6_2atmpS3626 + _M0L2dwS1112;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1113, _M0L3valS3624, _M0L6_2atmpS3625);
            _M0L3valS3629 = _M0L1sS1106->$0;
            _M0L6_2atmpS3628 = _M0L3valS3629 + 1;
            _M0L1sS1106->$0 = _M0L6_2atmpS3628;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1106);
          }
          break;
        }
      }
      _M0L3valS3640 = _M0L1jS1098->$0;
      _M0L6_2atmpS3639 = _M0L3valS3640 + 1;
      _M0L1jS1098->$0 = _M0L6_2atmpS3639;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1098);
    }
    break;
  }
  #line 511 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1116 = _M0MPC15array5Array6lengthGfE(_M0L1wS1113);
  _M0L2s2S1117
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1117)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1117->$0 = 0;
  while (1) {
    int32_t _M0L3valS3641 = _M0L2s2S1117->$0;
    if (_M0L3valS3641 < _M0L3nnzS1116) {
      int32_t _M0L3valS3653 = _M0L2s2S1117->$0;
      int32_t _M0L9post__idxS1118;
      int32_t _M0L3valS3652;
      int32_t _M0L6_2atmpS3651;
      #line 514 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1118
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1108, _M0L3valS3653);
      #line 515 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1091, _M0L9post__idxS1118)
      ) {
        int32_t _M0L3valS3650 = _M0L2s2S1117->$0;
        int32_t _M0L6j__preS1119;
        float _M0L6_2atmpS3648;
        float _M0L6_2atmpS3649;
        float _M0L5ratioS1120;
        float _M0L3lnxS1121;
        float _M0L1xS1122;
        float _M0L1aS3646;
        float _M0L6_2atmpS3647;
        float _M0L2dwS1123;
        int32_t _M0L3valS3642;
        int32_t _M0L3valS3645;
        float _M0L6_2atmpS3644;
        float _M0L6_2atmpS3643;
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1119
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1104, _M0L3valS3650);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3648
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1099, _M0L6j__preS1119);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3649
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1095, _M0L9post__idxS1118);
        _M0L5ratioS1120 = _M0L6_2atmpS3648 / _M0L6_2atmpS3649;
        #line 520 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3lnxS1121 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1120);
        _M0L1xS1122 = _M0L3lnxS1121 * _M0L3lnxS1121;
        _M0L1aS3646 = _M0L5paramS1093->$0;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3647
        = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1122);
        _M0L2dwS1123 = _M0L1aS3646 * _M0L6_2atmpS3647;
        _M0L3valS3642 = _M0L2s2S1117->$0;
        _M0L3valS3645 = _M0L2s2S1117->$0;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3644
        = _M0MPC15array5Array2atGfE(_M0L1wS1113, _M0L3valS3645);
        _M0L6_2atmpS3643 = _M0L6_2atmpS3644 + _M0L2dwS1123;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1113, _M0L3valS3642, _M0L6_2atmpS3643);
      }
      _M0L3valS3652 = _M0L2s2S1117->$0;
      _M0L6_2atmpS3651 = _M0L3valS3652 + 1;
      _M0L2s2S1117->$0 = _M0L6_2atmpS3651;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1117);
    }
    break;
  }
  _M0L2s3S1125
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1125)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1125->$0 = 0;
  while (1) {
    int32_t _M0L3valS3654 = _M0L2s3S1125->$0;
    if (_M0L3valS3654 < _M0L3nnzS1116) {
      int32_t _M0L3valS3657 = _M0L2s3S1125->$0;
      float _M0L6_2atmpS3655;
      float _M0L6w__minS3656;
      int32_t _M0L3valS3666;
      int32_t _M0L6_2atmpS3665;
      #line 530 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3655
      = _M0MPC15array5Array2atGfE(_M0L1wS1113, _M0L3valS3657);
      _M0L6w__minS3656 = _M0L5paramS1093->$3;
      if (_M0L6_2atmpS3655 < _M0L6w__minS3656) {
        int32_t _M0L3valS3658 = _M0L2s3S1125->$0;
        float _M0L6w__minS3659 = _M0L5paramS1093->$3;
        #line 531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1113, _M0L3valS3658, _M0L6w__minS3659);
      } else {
        int32_t _M0L3valS3662 = _M0L2s3S1125->$0;
        float _M0L6_2atmpS3660;
        float _M0L6w__maxS3661;
        #line 532 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3660
        = _M0MPC15array5Array2atGfE(_M0L1wS1113, _M0L3valS3662);
        _M0L6w__maxS3661 = _M0L5paramS1093->$2;
        if (_M0L6_2atmpS3660 > _M0L6w__maxS3661) {
          int32_t _M0L3valS3663 = _M0L2s3S1125->$0;
          float _M0L6w__maxS3664 = _M0L5paramS1093->$2;
          #line 533 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1113, _M0L3valS3663, _M0L6w__maxS3664);
        }
      }
      _M0L3valS3666 = _M0L2s3S1125->$0;
      _M0L6_2atmpS3665 = _M0L3valS3666 + 1;
      _M0L2s3S1125->$0 = _M0L6_2atmpS3665;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1125);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1082,
  int32_t _M0L1sS1086
) {
  int32_t _M0L6_2atmpS3580;
  int32_t _M0L1nS1081;
  struct _M0TPB8MutLocalGiE* _M0L2loS1083;
  struct _M0TPB8MutLocalGiE* _M0L2hiS1084;
  int32_t _result_6081;
  #line 542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 543 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3580 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS1082);
  _M0L1nS1081 = _M0L6_2atmpS3580 - 1;
  _M0L2loS1083
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2loS1083)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2loS1083->$0 = 0;
  _M0L2hiS1084
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2hiS1084)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2hiS1084->$0 = _M0L1nS1081;
  while (1) {
    int32_t _M0L3valS3572 = _M0L2loS1083->$0;
    int32_t _M0L3valS3573 = _M0L2hiS1084->$0;
    if (_M0L3valS3572 < _M0L3valS3573) {
      int32_t _M0L3valS3578 = _M0L2loS1083->$0;
      int32_t _M0L3valS3579 = _M0L2hiS1084->$0;
      int32_t _M0L6_2atmpS3577 = _M0L3valS3578 + _M0L3valS3579;
      int32_t _M0L6_2atmpS3576 = _M0L6_2atmpS3577 + 1;
      int32_t _M0L3midS1085 = _M0L6_2atmpS3576 / 2;
      int32_t _M0L6_2atmpS3574;
      #line 548 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3574
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1082, _M0L3midS1085);
      if (_M0L6_2atmpS3574 <= _M0L1sS1086) {
        _M0L2loS1083->$0 = _M0L3midS1085;
      } else {
        int32_t _M0L6_2atmpS3575 = _M0L3midS1085 - 1;
        _M0L2hiS1084->$0 = _M0L6_2atmpS3575;
      }
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2hiS1084);
    }
    break;
  }
  _result_6081 = _M0L2loS1083->$0;
  moonbit_decref_cycle_free(_M0L2loS1083);
  return _result_6081;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS1078) {
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 428 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS1078)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS3571 = -_M0L1xS1078;
    float _M0L3argS1079 = _M0L6_2atmpS3571 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS3569 = 0x1p+0f - _M0L1xS1078;
    float _M0L6_2atmpS3570;
    float _M0L1vS1080;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3570 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS1079);
    _M0L1vS1080 = _M0L6_2atmpS3569 * _M0L6_2atmpS3570;
    #line 433 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS1080)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS1080;
    }
  }
}

struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0MP26RiantR8snn__mbt9STDPEntry3new(
  int32_t _M0L11conn__indexS1075,
  int32_t _M0L6n__preS1076,
  int32_t _M0L7n__postS1077
) {
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L6_2atmpS3565;
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L6_2atmpS3566;
  float* _M0L6_2atmpS3568;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3567;
  struct _M0TP26RiantR8snn__mbt9STDPEntry* _block_6082;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3565
  = _M0MP26RiantR8snn__mbt13STDPVariables3new(_M0L6n__preS1076, _M0L7n__postS1077);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3566 = _M0MP26RiantR8snn__mbt12STDPGerstner3new();
  _M0L6_2atmpS3568 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3568[0] = 0x0p+0f;
  _M0L6_2atmpS3567
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3567)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS3567->$0 = _M0L6_2atmpS3568;
  _M0L6_2atmpS3567->$1 = 1;
  _block_6082
  = (struct _M0TP26RiantR8snn__mbt9STDPEntry*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9STDPEntry));
  Moonbit_object_header(_block_6082)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 86, 0);
  _block_6082->$0 = _M0L11conn__indexS1075;
  _block_6082->$1 = _M0L6n__preS1076;
  _block_6082->$2 = _M0L7n__postS1077;
  _block_6082->$3 = _M0L6_2atmpS3565;
  _block_6082->$4 = _M0L6_2atmpS3566;
  _block_6082->$5 = _M0L6_2atmpS3567;
  return _block_6082;
}

struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0MP26RiantR8snn__mbt13STDPVariables3new(
  int32_t _M0L6n__preS1073,
  int32_t _M0L7n__postS1074
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3559;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3560;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3561;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3562;
  uint8_t* _M0L6_2atmpS3564;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS3563;
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _block_6083;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3559 = _M0MPC15array5Array4makeGfE(_M0L6n__preS1073, 0x0p+0f);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3560 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1074, 0x0p+0f);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3561 = _M0MPC15array5Array4makeGfE(_M0L6n__preS1073, 0x0p+0f);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3562 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1074, 0x0p+0f);
  _M0L6_2atmpS3564 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS3564[0] = 1;
  _M0L6_2atmpS3563
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS3563)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 91, 0);
  _M0L6_2atmpS3563->$0 = _M0L6_2atmpS3564;
  _M0L6_2atmpS3563->$1 = 1;
  _block_6083
  = (struct _M0TP26RiantR8snn__mbt13STDPVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13STDPVariables));
  Moonbit_object_header(_block_6083)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 94, 0);
  _block_6083->$0 = _M0L6_2atmpS3559;
  _block_6083->$1 = _M0L6_2atmpS3560;
  _block_6083->$2 = _M0L6_2atmpS3561;
  _block_6083->$3 = _M0L6_2atmpS3562;
  _block_6083->$4 = _M0L6_2atmpS3563;
  return _block_6083;
}

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
) {
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _block_6084;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _block_6084
  = (struct _M0TP26RiantR8snn__mbt12STDPGerstner*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12STDPGerstner));
  Moonbit_object_header(_block_6084)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6084->$0 = 0x1.47ae147ae147bp-7f;
  _block_6084->$1 = 0x1.47ae147ae147bp-7f;
  _block_6084->$2 = 0x1.4p+4f;
  _block_6084->$3 = 0x1.4p+4f;
  _block_6084->$4 = 0x1.ep+4f;
  _block_6084->$5 = 0x0p+0f;
  return _block_6084;
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
  float _M0L6_2atmpS3558;
  float _M0L11inh__lambdaS1048;
  int32_t _M0L7_2abindS1050;
  int32_t _M0L1kS1051;
  float _M0L6_2atmpS3557;
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
  _M0L6_2atmpS3558 = _M0L2r0S1045 * _M0L3kIES1042;
  _M0L11inh__lambdaS1048 = _M0L6_2atmpS3558 * _M0L2dtS1049;
  _M0L7_2abindS1050 = 0;
  _M0L1kS1051 = _M0L7_2abindS1050;
  while (1) {
    if (_M0L1kS1051 < _M0L1nS1039) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3471 = _M0L1sS1040->$4;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3479;
      int32_t _M0L1mS1054;
      int32_t _M0L6_2atmpS3470;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3471, _M0L1kS1051, 0);
      if (_M0L11inh__lambdaS1048 <= 0x0p+0f) {
        goto join_1052;
      }
      _M0L3rngS3479 = _M0L1sS1040->$7;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0L1mS1054
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3479, _M0L11inh__lambdaS1048);
      if (_M0L1mS1054 > 0) {
        struct _M0TPB5ArrayGfE* _M0L2giS3472 = _M0L1sS1040->$3;
        struct _M0TPB5ArrayGfE* _M0L2giS3478 = _M0L1sS1040->$3;
        float _M0L6_2atmpS3474;
        float _M0L6_2atmpS3477;
        float _M0L6_2atmpS3476;
        float _M0L6_2atmpS3475;
        float _M0L6_2atmpS3473;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3474
        = _M0MPC15array5Array2atGfE(_M0L2giS3478, _M0L1kS1051);
        _M0L6_2atmpS3477 = (float)_M0L1mS1054;
        _M0L6_2atmpS3476 = _M0L1wS1046 * _M0L6_2atmpS3477;
        _M0L6_2atmpS3475 = _M0L6_2atmpS3476 * _M0L3wIES1047;
        _M0L6_2atmpS3473 = _M0L6_2atmpS3474 + _M0L6_2atmpS3475;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L2giS3472, _M0L1kS1051, _M0L6_2atmpS3473);
      }
      goto join_1052;
      goto joinlet_6086;
      join_1052:;
      _M0L6_2atmpS3470 = _M0L1kS1051 + 1;
      _M0L1kS1051 = _M0L6_2atmpS3470;
      continue;
      joinlet_6086:;
    }
    break;
  }
  _M0L6_2atmpS3557 = _M0L2dtS1049 / _M0L3tauS1044;
  _M0L2ccS1055 = 0x1p+0f - _M0L6_2atmpS3557;
  if (_M0L5paramS1041->$6) {
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3517 = _M0L1sS1040->$7;
    double _M0L6_2atmpS3516;
    float _M0L6_2atmpS3515;
    float _M0L2reS1056;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3480;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3485;
    float _M0L6_2atmpS3484;
    float _M0L6_2atmpS3483;
    float _M0L6_2atmpS3482;
    float _M0L6_2atmpS3481;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3514;
    float _M0L6_2atmpS3513;
    float _M0L6_2atmpS3512;
    struct _M0TPB8MutLocalGfE* _M0L2nbS1057;
    float _M0L3valS3486;
    float _M0L3valS3487;
    float _M0L6_2atmpS3510;
    float _M0L3valS3511;
    float _M0L6_2atmpS3507;
    struct _M0TPB5ArrayGfE* _M0L1rS3509;
    float _M0L6_2atmpS3508;
    float _M0L6_2atmpS3506;
    struct _M0TPB8MutLocalGfE* _M0L5erateS1058;
    float _M0L3valS3488;
    struct _M0TPB5ArrayGfE* _M0L1rS3489;
    struct _M0TPB5ArrayGfE* _M0L1rS3496;
    float _M0L6_2atmpS3491;
    float _M0L3valS3495;
    float _M0L6_2atmpS3494;
    float _M0L6_2atmpS3493;
    float _M0L6_2atmpS3492;
    float _M0L6_2atmpS3490;
    float _M0L3valS3505;
    float _M0L11exc__lambdaS1059;
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3504;
    int32_t _M0L1mS1060;
    #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3516 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3517);
    _M0L6_2atmpS3515 = (float)_M0L6_2atmpS3516;
    _M0L2reS1056 = _M0L6_2atmpS3515 - 0x1p-1f;
    _M0L5noiseS3480 = _M0L1sS1040->$6;
    _M0L5noiseS3485 = _M0L1sS1040->$6;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3484 = _M0MPC15array5Array2atGfE(_M0L5noiseS3485, 0);
    _M0L6_2atmpS3483 = _M0L6_2atmpS3484 - _M0L2reS1056;
    _M0L6_2atmpS3482 = _M0L6_2atmpS3483 * _M0L2ccS1055;
    _M0L6_2atmpS3481 = _M0L6_2atmpS3482 + _M0L2reS1056;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L5noiseS3480, 0, _M0L6_2atmpS3481);
    _M0L5noiseS3514 = _M0L1sS1040->$6;
    #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3513 = _M0MPC15array5Array2atGfE(_M0L5noiseS3514, 0);
    _M0L6_2atmpS3512 = _M0L6_2atmpS3513 * _M0L4betaS1043;
    _M0L2nbS1057
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L2nbS1057)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L2nbS1057->$0 = _M0L6_2atmpS3512;
    _M0L3valS3486 = _M0L2nbS1057->$0;
    if (_M0L3valS3486 > 0x1p+0f) {
      _M0L2nbS1057->$0 = 0x1p+0f;
    }
    _M0L3valS3487 = _M0L2nbS1057->$0;
    if (_M0L3valS3487 < 0x0p+0f) {
      _M0L2nbS1057->$0 = 0x0p+0f;
    }
    _M0L6_2atmpS3510 = _M0L2r0S1045 / 0x1p+1f;
    _M0L3valS3511 = _M0L2nbS1057->$0;
    moonbit_decref_cycle_free(_M0L2nbS1057);
    _M0L6_2atmpS3507 = _M0L6_2atmpS3510 * _M0L3valS3511;
    _M0L1rS3509 = _M0L1sS1040->$5;
    #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3508 = _M0MPC15array5Array2atGfE(_M0L1rS3509, 0);
    _M0L6_2atmpS3506 = _M0L6_2atmpS3507 + _M0L6_2atmpS3508;
    _M0L5erateS1058
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L5erateS1058)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L5erateS1058->$0 = _M0L6_2atmpS3506;
    _M0L3valS3488 = _M0L5erateS1058->$0;
    if (_M0L3valS3488 < 0x0p+0f) {
      _M0L5erateS1058->$0 = 0x0p+0f;
    }
    _M0L1rS3489 = _M0L1sS1040->$5;
    _M0L1rS3496 = _M0L1sS1040->$5;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3491 = _M0MPC15array5Array2atGfE(_M0L1rS3496, 0);
    _M0L3valS3495 = _M0L5erateS1058->$0;
    _M0L6_2atmpS3494 = _M0L2r0S1045 - _M0L3valS3495;
    _M0L6_2atmpS3493 = _M0L6_2atmpS3494 / 0x1.9p+8f;
    _M0L6_2atmpS3492 = _M0L6_2atmpS3493 * _M0L2dtS1049;
    _M0L6_2atmpS3490 = _M0L6_2atmpS3491 + _M0L6_2atmpS3492;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L1rS3489, 0, _M0L6_2atmpS3490);
    _M0L3valS3505 = _M0L5erateS1058->$0;
    moonbit_decref_cycle_free(_M0L5erateS1058);
    _M0L11exc__lambdaS1059 = _M0L3valS3505 * _M0L2dtS1049;
    _M0L3rngS3504 = _M0L1sS1040->$7;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L1mS1060
    = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3504, _M0L11exc__lambdaS1059);
    if (_M0L1mS1060 > 0) {
      float _M0L6_2atmpS3503 = (float)_M0L1mS1060;
      float _M0L3addS1061 = _M0L1wS1046 * _M0L6_2atmpS3503;
      int32_t _M0L7_2abindS1062 = 0;
      int32_t _M0L1iS1063 = _M0L7_2abindS1062;
      while (1) {
        if (_M0L1iS1063 < _M0L1nS1039) {
          struct _M0TPB5ArrayGfE* _M0L2geS3497 = _M0L1sS1040->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3500 = _M0L1sS1040->$2;
          float _M0L6_2atmpS3499;
          float _M0L6_2atmpS3498;
          struct _M0TPB5ArrayGbE* _M0L4fireS3501;
          int32_t _M0L6_2atmpS3502;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3499
          = _M0MPC15array5Array2atGfE(_M0L2geS3500, _M0L1iS1063);
          _M0L6_2atmpS3498 = _M0L6_2atmpS3499 + _M0L3addS1061;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3497, _M0L1iS1063, _M0L6_2atmpS3498);
          _M0L4fireS3501 = _M0L1sS1040->$4;
          #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3501, _M0L1iS1063, 1);
          _M0L6_2atmpS3502 = _M0L1iS1063 + 1;
          _M0L1iS1063 = _M0L6_2atmpS3502;
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
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3555 =
          _M0L1sS1040->$7;
        double _M0L6_2atmpS3554;
        float _M0L6_2atmpS3553;
        float _M0L2reS1067;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3518;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3523;
        float _M0L6_2atmpS3522;
        float _M0L6_2atmpS3521;
        float _M0L6_2atmpS3520;
        float _M0L6_2atmpS3519;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3552;
        float _M0L6_2atmpS3551;
        float _M0L6_2atmpS3550;
        struct _M0TPB8MutLocalGfE* _M0L2nbS1068;
        float _M0L3valS3524;
        float _M0L3valS3525;
        float _M0L6_2atmpS3548;
        float _M0L3valS3549;
        float _M0L6_2atmpS3545;
        struct _M0TPB5ArrayGfE* _M0L1rS3547;
        float _M0L6_2atmpS3546;
        float _M0L6_2atmpS3544;
        struct _M0TPB8MutLocalGfE* _M0L5erateS1069;
        float _M0L3valS3526;
        struct _M0TPB5ArrayGfE* _M0L1rS3527;
        struct _M0TPB5ArrayGfE* _M0L1rS3534;
        float _M0L6_2atmpS3529;
        float _M0L3valS3533;
        float _M0L6_2atmpS3532;
        float _M0L6_2atmpS3531;
        float _M0L6_2atmpS3530;
        float _M0L6_2atmpS3528;
        float _M0L3valS3543;
        float _M0L11exc__lambdaS1070;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3542;
        int32_t _M0L1mS1071;
        int32_t _M0L6_2atmpS3556;
        #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3554 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3555);
        _M0L6_2atmpS3553 = (float)_M0L6_2atmpS3554;
        _M0L2reS1067 = _M0L6_2atmpS3553 - 0x1p-1f;
        _M0L5noiseS3518 = _M0L1sS1040->$6;
        _M0L5noiseS3523 = _M0L1sS1040->$6;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3522
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3523, _M0L1iS1066);
        _M0L6_2atmpS3521 = _M0L6_2atmpS3522 - _M0L2reS1067;
        _M0L6_2atmpS3520 = _M0L6_2atmpS3521 * _M0L2ccS1055;
        _M0L6_2atmpS3519 = _M0L6_2atmpS3520 + _M0L2reS1067;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L5noiseS3518, _M0L1iS1066, _M0L6_2atmpS3519);
        _M0L5noiseS3552 = _M0L1sS1040->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3551
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3552, _M0L1iS1066);
        _M0L6_2atmpS3550 = _M0L6_2atmpS3551 * _M0L4betaS1043;
        _M0L2nbS1068
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L2nbS1068)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2nbS1068->$0 = _M0L6_2atmpS3550;
        _M0L3valS3524 = _M0L2nbS1068->$0;
        if (_M0L3valS3524 > 0x1p+0f) {
          _M0L2nbS1068->$0 = 0x1p+0f;
        }
        _M0L3valS3525 = _M0L2nbS1068->$0;
        if (_M0L3valS3525 < 0x0p+0f) {
          _M0L2nbS1068->$0 = 0x0p+0f;
        }
        _M0L6_2atmpS3548 = _M0L2r0S1045 / 0x1p+1f;
        _M0L3valS3549 = _M0L2nbS1068->$0;
        moonbit_decref_cycle_free(_M0L2nbS1068);
        _M0L6_2atmpS3545 = _M0L6_2atmpS3548 * _M0L3valS3549;
        _M0L1rS3547 = _M0L1sS1040->$5;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3546
        = _M0MPC15array5Array2atGfE(_M0L1rS3547, _M0L1iS1066);
        _M0L6_2atmpS3544 = _M0L6_2atmpS3545 + _M0L6_2atmpS3546;
        _M0L5erateS1069
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L5erateS1069)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L5erateS1069->$0 = _M0L6_2atmpS3544;
        _M0L3valS3526 = _M0L5erateS1069->$0;
        if (_M0L3valS3526 < 0x0p+0f) {
          _M0L5erateS1069->$0 = 0x0p+0f;
        }
        _M0L1rS3527 = _M0L1sS1040->$5;
        _M0L1rS3534 = _M0L1sS1040->$5;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3529
        = _M0MPC15array5Array2atGfE(_M0L1rS3534, _M0L1iS1066);
        _M0L3valS3533 = _M0L5erateS1069->$0;
        _M0L6_2atmpS3532 = _M0L2r0S1045 - _M0L3valS3533;
        _M0L6_2atmpS3531 = _M0L6_2atmpS3532 / 0x1.9p+8f;
        _M0L6_2atmpS3530 = _M0L6_2atmpS3531 * _M0L2dtS1049;
        _M0L6_2atmpS3528 = _M0L6_2atmpS3529 + _M0L6_2atmpS3530;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L1rS3527, _M0L1iS1066, _M0L6_2atmpS3528);
        _M0L3valS3543 = _M0L5erateS1069->$0;
        moonbit_decref_cycle_free(_M0L5erateS1069);
        _M0L11exc__lambdaS1070 = _M0L3valS3543 * _M0L2dtS1049;
        _M0L3rngS3542 = _M0L1sS1040->$7;
        #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L1mS1071
        = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3542, _M0L11exc__lambdaS1070);
        if (_M0L1mS1071 > 0) {
          struct _M0TPB5ArrayGfE* _M0L2geS3535 = _M0L1sS1040->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3540 = _M0L1sS1040->$2;
          float _M0L6_2atmpS3537;
          float _M0L6_2atmpS3539;
          float _M0L6_2atmpS3538;
          float _M0L6_2atmpS3536;
          struct _M0TPB5ArrayGbE* _M0L4fireS3541;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3537
          = _M0MPC15array5Array2atGfE(_M0L2geS3540, _M0L1iS1066);
          _M0L6_2atmpS3539 = (float)_M0L1mS1071;
          _M0L6_2atmpS3538 = _M0L1wS1046 * _M0L6_2atmpS3539;
          _M0L6_2atmpS3536 = _M0L6_2atmpS3537 + _M0L6_2atmpS3538;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3535, _M0L1iS1066, _M0L6_2atmpS3536);
          _M0L4fireS3541 = _M0L1sS1040->$4;
          #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3541, _M0L1iS1066, 1);
        }
        _M0L6_2atmpS3556 = _M0L1iS1066 + 1;
        _M0L1iS1066 = _M0L6_2atmpS3556;
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
  uint64_t _M0L6_2atmpS3469;
  struct _M0TUmmmmE* _M0L1tS1037;
  uint64_t _M0L6_2atmpS3465;
  uint64_t _M0L6_2atmpS3466;
  uint64_t _M0L6_2atmpS3467;
  uint64_t _M0L6_2atmpS3468;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_6089;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS1035 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS1036);
  _M0L6_2atmpS3469 = _M0L1sS1035->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS1037 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS3469);
  _M0L6_2atmpS3465 = _M0L1sS1035->$0;
  _M0L6_2atmpS3466 = _M0L1sS1035->$1;
  _M0L6_2atmpS3467 = _M0L1sS1035->$2;
  moonbit_decref_cycle_free(_M0L1sS1035);
  _M0L6_2atmpS3468 = _M0L1tS1037->$0;
  moonbit_decref_cycle_free(_M0L1tS1037);
  _block_6089
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_6089)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6089->$0 = _M0L6_2atmpS3465;
  _block_6089->$1 = _M0L6_2atmpS3466;
  _block_6089->$2 = _M0L6_2atmpS3467;
  _block_6089->$3 = _M0L6_2atmpS3468;
  return _block_6089;
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
  struct _M0TUmmmmE* _block_6090;
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
  _block_6090 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_6090)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6090->$0 = _M0L2z1S1028;
  _block_6090->$1 = _M0L2z2S1030;
  _block_6090->$2 = _M0L2z3S1032;
  _block_6090->$3 = _M0L2z4S1034;
  return _block_6090;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS1024) {
  uint64_t _M0L6_2atmpS3464;
  uint64_t _M0L6_2atmpS3463;
  uint64_t _M0L1zS1023;
  uint64_t _M0L6_2atmpS3462;
  uint64_t _M0L6_2atmpS3461;
  uint64_t _M0L1zS1025;
  uint64_t _M0L6_2atmpS3460;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3464 = _M0L1zS1024 >> 30;
  _M0L6_2atmpS3463 = _M0L1zS1024 ^ _M0L6_2atmpS3464;
  _M0L1zS1023 = _M0L6_2atmpS3463 * 13787848793156543929ull;
  _M0L6_2atmpS3462 = _M0L1zS1023 >> 27;
  _M0L6_2atmpS3461 = _M0L1zS1023 ^ _M0L6_2atmpS3462;
  _M0L1zS1025 = _M0L6_2atmpS3461 * 10723151780598845931ull;
  _M0L6_2atmpS3460 = _M0L1zS1025 >> 31;
  return _M0L1zS1025 ^ _M0L6_2atmpS3460;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS1011
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3444;
  int32_t _M0L6_2atmpS3443;
  float _M0L12noise__sigmaS3445;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3444 = _M0L1sS1011->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3443 = _M0MPC15array5Array2atGbE(_M0L6activeS3444, 0);
  if (!_M0L6_2atmpS3443) {
    return 0;
  }
  _M0L12noise__sigmaS3445 = _M0L1sS1011->$4;
  if (_M0L12noise__sigmaS3445 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1012 = 0;
    int32_t _M0L7_2abindS1013 = _M0L1sS1011->$3;
    int32_t _M0L1kS1014 = _M0L7_2abindS1012;
    while (1) {
      if (_M0L1kS1014 < _M0L7_2abindS1013) {
        struct _M0TPB5ArrayGfE* _M0L1iS3446 = _M0L1sS1011->$2;
        float _M0L7i__baseS3447 = _M0L1sS1011->$0;
        int32_t _M0L6_2atmpS3448;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3446, _M0L1kS1014, _M0L7i__baseS3447);
        _M0L6_2atmpS3448 = _M0L1kS1014 + 1;
        _M0L1kS1014 = _M0L6_2atmpS3448;
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
      int32_t _M0L3valS3449 = _M0L1kS1017->$0;
      int32_t _M0L1nS3450 = _M0L1sS1011->$3;
      if (_M0L3valS3449 < _M0L1nS3450) {
        double _M0L2z1S1019;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3459 =
          _M0L1sS1011->$5;
        struct _M0TUddE* _M0L7_2abindS1020;
        double _M0L5_2az1S1021;
        struct _M0TPB5ArrayGfE* _M0L1iS3451;
        int32_t _M0L3valS3452;
        float _M0L7i__baseS3454;
        float _M0L6_2atmpS3456;
        float _M0L6_2atmpS3455;
        float _M0L6_2atmpS3453;
        int32_t _M0L3valS3458;
        int32_t _M0L6_2atmpS3457;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1020
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3459);
        _M0L5_2az1S1021 = _M0L7_2abindS1020->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1020);
        _M0L2z1S1019 = _M0L5_2az1S1021;
        goto join_1018;
        goto joinlet_6093;
        join_1018:;
        _M0L1iS3451 = _M0L1sS1011->$2;
        _M0L3valS3452 = _M0L1kS1017->$0;
        _M0L7i__baseS3454 = _M0L1sS1011->$0;
        _M0L6_2atmpS3456 = (float)_M0L2z1S1019;
        _M0L6_2atmpS3455 = _M0L5sigmaS1016 * _M0L6_2atmpS3456;
        _M0L6_2atmpS3453 = _M0L7i__baseS3454 + _M0L6_2atmpS3455;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3451, _M0L3valS3452, _M0L6_2atmpS3453);
        _M0L3valS3458 = _M0L1kS1017->$0;
        _M0L6_2atmpS3457 = _M0L3valS3458 + 1;
        _M0L1kS1017->$0 = _M0L6_2atmpS3457;
        joinlet_6093:;
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
  struct _M0TPB5ArrayGbE* _M0L6activeS3428;
  int32_t _M0L6_2atmpS3427;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS999;
  int32_t _M0L1nS1000;
  float _M0L12noise__sigmaS3429;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3428 = _M0L1sS998->$1;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3427 = _M0MPC15array5Array2atGbE(_M0L6activeS3428, 0);
  if (!_M0L6_2atmpS3427) {
    return 0;
  }
  _M0L3popS999 = _M0L1sS998->$2;
  _M0L1nS1000 = _M0L3popS999->$2;
  _M0L12noise__sigmaS3429 = _M0L1sS998->$3;
  if (_M0L12noise__sigmaS3429 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1001 = 0;
    int32_t _M0L1iS1002 = _M0L7_2abindS1001;
    while (1) {
      if (_M0L1iS1002 < _M0L1nS1000) {
        struct _M0TPB5ArrayGfE* _M0L1iS3430 = _M0L3popS999->$7;
        float _M0L7i__baseS3431 = _M0L1sS998->$0;
        int32_t _M0L6_2atmpS3432;
        #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3430, _M0L1iS1002, _M0L7i__baseS3431);
        _M0L6_2atmpS3432 = _M0L1iS1002 + 1;
        _M0L1iS1002 = _M0L6_2atmpS3432;
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
      int32_t _M0L3valS3433 = _M0L1kS1005->$0;
      if (_M0L3valS3433 < _M0L1nS1000) {
        double _M0L2z1S1007;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3442 = _M0L1sS998->$4;
        struct _M0TUddE* _M0L7_2abindS1008;
        double _M0L5_2az1S1009;
        struct _M0TPB5ArrayGfE* _M0L1iS3434;
        int32_t _M0L3valS3435;
        float _M0L7i__baseS3437;
        float _M0L6_2atmpS3439;
        float _M0L6_2atmpS3438;
        float _M0L6_2atmpS3436;
        int32_t _M0L3valS3441;
        int32_t _M0L6_2atmpS3440;
        #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1008
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3442);
        _M0L5_2az1S1009 = _M0L7_2abindS1008->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1008);
        _M0L2z1S1007 = _M0L5_2az1S1009;
        goto join_1006;
        goto joinlet_6096;
        join_1006:;
        _M0L1iS3434 = _M0L3popS999->$7;
        _M0L3valS3435 = _M0L1kS1005->$0;
        _M0L7i__baseS3437 = _M0L1sS998->$0;
        _M0L6_2atmpS3439 = (float)_M0L2z1S1007;
        _M0L6_2atmpS3438 = _M0L5sigmaS1004 * _M0L6_2atmpS3439;
        _M0L6_2atmpS3436 = _M0L7i__baseS3437 + _M0L6_2atmpS3438;
        #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3434, _M0L3valS3435, _M0L6_2atmpS3436);
        _M0L3valS3441 = _M0L1kS1005->$0;
        _M0L6_2atmpS3440 = _M0L3valS3441 + 1;
        _M0L1kS1005->$0 = _M0L6_2atmpS3440;
        joinlet_6096:;
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
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3414;
  struct _M0TPB5ArrayGbE* _M0L6activeS3413;
  int32_t _M0L6_2atmpS3412;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3426;
  float _M0L4rateS3425;
  float _M0L6lambdaS988;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS990;
  int32_t _M0L7_2abindS991;
  int32_t* _M0L7_2abindS992;
  int32_t _M0L2__S993;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS3414 = _M0L1sS987->$0;
  _M0L6activeS3413 = _M0L5paramS3414->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3412 = _M0MPC15array5Array2atGbE(_M0L6activeS3413, 0);
  if (!_M0L6_2atmpS3412) {
    return 0;
  }
  _M0L5paramS3426 = _M0L1sS987->$0;
  _M0L4rateS3425 = _M0L5paramS3426->$0;
  _M0L6lambdaS988 = _M0L4rateS3425 * _M0L2dtS989;
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
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3423 = _M0L1sS987->$3;
      int32_t _M0L1kS995;
      int32_t _M0L6_2atmpS3424;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS995
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3423, _M0L6lambdaS988);
      if (_M0L1kS995 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS3415 = _M0L1sS987->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS3422 = _M0L1sS987->$2;
        float _M0L6_2atmpS3417;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3421;
        float _M0L2muS3419;
        float _M0L6_2atmpS3420;
        float _M0L6_2atmpS3418;
        float _M0L6_2atmpS3416;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS3417 = _M0MPC15array5Array2atGfE(_M0L1gS3422, _M0L1nS994);
        _M0L5paramS3421 = _M0L1sS987->$0;
        _M0L2muS3419 = _M0L5paramS3421->$1;
        _M0L6_2atmpS3420 = (float)_M0L1kS995;
        _M0L6_2atmpS3418 = _M0L2muS3419 * _M0L6_2atmpS3420;
        _M0L6_2atmpS3416 = _M0L6_2atmpS3417 + _M0L6_2atmpS3418;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS3415, _M0L1nS994, _M0L6_2atmpS3416);
      }
      _M0L6_2atmpS3424 = _M0L2__S993 + 1;
      _M0L2__S993 = _M0L6_2atmpS3424;
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
  int32_t* _M0L6_2atmpS3411;
  struct _M0TPB5ArrayGiE* _M0L7neuronsS979;
  int32_t _M0L7_2abindS980;
  int32_t _M0L1kS981;
  struct _M0TPB5ArrayGfE* _M0L1gS983;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L6_2atmpS3410;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _block_6099;
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1nS977 = _M0L3popS978->$2;
  _M0L6_2atmpS3411 = (int32_t*)moonbit_empty_int32_array;
  _M0L7neuronsS979
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L7neuronsS979)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L7neuronsS979->$0 = _M0L6_2atmpS3411;
  _M0L7neuronsS979->$1 = 0;
  _M0L7_2abindS980 = 0;
  _M0L1kS981 = _M0L7_2abindS980;
  while (1) {
    if (_M0L1kS981 < _M0L1nS977) {
      int32_t _M0L6_2atmpS3409;
      #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0MPC15array5Array4pushGiE(_M0L7neuronsS979, _M0L1kS981);
      _M0L6_2atmpS3409 = _M0L1kS981 + 1;
      _M0L1kS981 = _M0L6_2atmpS3409;
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
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5698 = _M0L3popS978->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5698);
    _M0L1gS983 = _M0L8_2afieldS5698;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5699 = _M0L3popS978->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5699);
    _M0L1gS983 = _M0L8_2afieldS5699;
  }
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3410 = _M0MP26RiantR8snn__mbt12PoissonFixed3new(_M0L4rateS985);
  moonbit_incref_cycle_free(_M0L3rngS986);
  _block_6099
  = (struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF));
  Moonbit_object_header(_block_6099)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 101, 0);
  _block_6099->$0 = _M0L6_2atmpS3410;
  _block_6099->$1 = _M0L7neuronsS979;
  _block_6099->$2 = _M0L1gS983;
  _block_6099->$3 = _M0L3rngS986;
  return _block_6099;
}

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float _M0L4rateS976
) {
  uint8_t* _M0L6_2atmpS3408;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS3407;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _block_6100;
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3408 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS3408[0] = 1;
  _M0L6_2atmpS3407
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS3407)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 91, 0);
  _M0L6_2atmpS3407->$0 = _M0L6_2atmpS3408;
  _M0L6_2atmpS3407->$1 = 1;
  _block_6100
  = (struct _M0TP26RiantR8snn__mbt12PoissonFixed*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed));
  Moonbit_object_header(_block_6100)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 107, 0);
  _block_6100->$0 = _M0L4rateS976;
  _block_6100->$1 = 0x1p+0f;
  _block_6100->$2 = _M0L6_2atmpS3407;
  return _block_6100;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS959,
  float _M0L4timeS957,
  float _M0L2dtS962
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3406;
  int32_t _M0L6n__preS958;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3405;
  int32_t _M0L7n__postS960;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3404;
  float _M0L4rateS3403;
  float _M0L6lambdaS961;
  int32_t _M0L7_2abindS963;
  int32_t _M0L1iS964;
  moonbit_string_t _M0L3symS3400;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS966;
  int32_t _M0L7_2abindS967;
  int32_t _M0L1iS968;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS3406 = _M0L1sS959->$0;
  _M0L6n__preS958 = _M0L5paramS3406->$1;
  _M0L4postS3405 = _M0L1sS959->$1;
  _M0L7n__postS960 = _M0L4postS3405->$2;
  _M0L5paramS3404 = _M0L1sS959->$0;
  _M0L4rateS3403 = _M0L5paramS3404->$0;
  _M0L6lambdaS961 = _M0L4rateS3403 * _M0L2dtS962;
  _M0L7_2abindS963 = 0;
  _M0L1iS964 = _M0L7_2abindS963;
  while (1) {
    if (_M0L1iS964 < _M0L6n__preS958) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3385 = _M0L1sS959->$3;
      int32_t _M0L6_2atmpS3386;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3385, _M0L1iS964, 0);
      _M0L6_2atmpS3386 = _M0L1iS964 + 1;
      _M0L1iS964 = _M0L6_2atmpS3386;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS961 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS3400 = _M0L1sS959->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS3400 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS3400)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS3400, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3400) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3401 = _M0L1sS959->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5700 = _M0L4postS3401->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5700);
    _M0L9g__targetS966 = _M0L8_2afieldS5700;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3402 = _M0L1sS959->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5701 = _M0L4postS3402->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5701);
    _M0L9g__targetS966 = _M0L8_2afieldS5701;
  }
  _M0L7_2abindS967 = 0;
  _M0L1iS968 = _M0L7_2abindS967;
  while (1) {
    if (_M0L1iS968 < _M0L6n__preS958) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3390 =
        _M0L1sS959->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS3389 = _M0L5paramS3390->$2;
      int32_t _M0L6_2atmpS3388;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3399;
      int32_t _M0L1kS971;
      int32_t _M0L6_2atmpS3387;
      moonbit_incref_cycle_free(_M0L6activeS3389);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS3388
      = _M0MPC15array5Array2atGbE(_M0L6activeS3389, _M0L1iS968);
      moonbit_decref_cycle_free(_M0L6activeS3389);
      if (!_M0L6_2atmpS3388) {
        goto join_969;
      }
      _M0L3rngS3399 = _M0L1sS959->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS971
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3399, _M0L6lambdaS961);
      if (_M0L1kS971 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS3391 = _M0L1sS959->$3;
        int32_t _M0L7_2abindS972;
        int32_t _M0L1jS973;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS3391, _M0L1iS968, 1);
        _M0L7_2abindS972 = 0;
        _M0L1jS973 = _M0L7_2abindS972;
        while (1) {
          if (_M0L1jS973 < _M0L7n__postS960) {
            int32_t _M0L6_2atmpS3397 = _M0L1jS973 * _M0L6n__preS958;
            int32_t _M0L3idxS974 = _M0L6_2atmpS3397 + _M0L1iS968;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS3392 = _M0L1sS959->$5;
            int32_t _M0L6_2atmpS3398;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS3392, _M0L3idxS974)
            ) {
              float _M0L6_2atmpS3394;
              struct _M0TPB5ArrayGfE* _M0L7weightsS3396;
              float _M0L6_2atmpS3395;
              float _M0L6_2atmpS3393;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3394
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS966, _M0L1jS973);
              _M0L7weightsS3396 = _M0L1sS959->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3395
              = _M0MPC15array5Array2atGfE(_M0L7weightsS3396, _M0L3idxS974);
              _M0L6_2atmpS3393 = _M0L6_2atmpS3394 + _M0L6_2atmpS3395;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS966, _M0L1jS973, _M0L6_2atmpS3393);
            }
            _M0L6_2atmpS3398 = _M0L1jS973 + 1;
            _M0L1jS973 = _M0L6_2atmpS3398;
            continue;
          }
          break;
        }
      }
      goto join_969;
      goto joinlet_6103;
      join_969:;
      _M0L6_2atmpS3387 = _M0L1iS968 + 1;
      _M0L1iS968 = _M0L6_2atmpS3387;
      continue;
      joinlet_6103:;
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
  double _M0L6_2atmpS3384;
  double _M0L6_2atmpS3383;
  double _M0L1rS955;
  double _M0L5thetaS956;
  double _M0L6_2atmpS3382;
  double _M0L6_2atmpS3379;
  double _M0L6_2atmpS3381;
  double _M0L6_2atmpS3380;
  struct _M0TUddE* _block_6105;
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
  _M0L6_2atmpS3384 = _M0FPC14math2ln(_M0L8u1__safeS953);
  _M0L6_2atmpS3383 = -0x1p+1 * _M0L6_2atmpS3384;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS955 = sqrt(_M0L6_2atmpS3383);
  _M0L5thetaS956 = 0x1.921fb54442d18p+2 * _M0L2u2S954;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3382 = _M0FPC14math3cos(_M0L5thetaS956);
  _M0L6_2atmpS3379 = _M0L1rS955 * _M0L6_2atmpS3382;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3381 = _M0FPC14math3sin(_M0L5thetaS956);
  _M0L6_2atmpS3380 = _M0L1rS955 * _M0L6_2atmpS3381;
  _block_6105 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_6105)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6105->$0 = _M0L6_2atmpS3379;
  _block_6105->$1 = _M0L6_2atmpS3380;
  return _block_6105;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS949,
  float _M0L6lambdaS943
) {
  float _M0L6_2atmpS3378;
  float _M0L6_2atmpS3377;
  double _M0L1lS944;
  struct _M0TPB8MutLocalGdE* _M0L1pS945;
  struct _M0TPB8MutLocalGiE* _M0L1kS946;
  float _M0L6_2atmpS3376;
  int32_t _M0L8ten__lamS948;
  int32_t _M0L3capS947;
  int32_t _M0L3valS3375;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS943 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS3378 = -_M0L6lambdaS943;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3377 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3378);
  _M0L1lS944 = (double)_M0L6_2atmpS3377;
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
  _M0L6_2atmpS3376 = _M0L6lambdaS943 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS948 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3376);
  if (_M0L8ten__lamS948 > 100) {
    _M0L3capS947 = _M0L8ten__lamS948;
  } else {
    _M0L3capS947 = 100;
  }
  while (1) {
    int32_t _M0L3valS3367 = _M0L1kS946->$0;
    int32_t _M0L6_2atmpS3366 = _M0L3valS3367 + 1;
    double _M0L3valS3369;
    double _M0L6_2atmpS3370;
    double _M0L6_2atmpS3368;
    double _M0L3valS3371;
    int32_t _M0L3valS3373;
    _M0L1kS946->$0 = _M0L6_2atmpS3366;
    _M0L3valS3369 = _M0L1pS945->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS3370 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS949);
    _M0L6_2atmpS3368 = _M0L3valS3369 * _M0L6_2atmpS3370;
    _M0L1pS945->$0 = _M0L6_2atmpS3368;
    _M0L3valS3371 = _M0L1pS945->$0;
    if (_M0L3valS3371 < _M0L1lS944) {
      int32_t _M0L3valS3372;
      moonbit_decref_cycle_free(_M0L1pS945);
      _M0L3valS3372 = _M0L1kS946->$0;
      moonbit_decref_cycle_free(_M0L1kS946);
      return _M0L3valS3372 - 1;
    }
    _M0L3valS3373 = _M0L1kS946->$0;
    if (_M0L3valS3373 > _M0L3capS947) {
      int32_t _M0L3valS3374;
      moonbit_decref_cycle_free(_M0L1pS945);
      _M0L3valS3374 = _M0L1kS946->$0;
      moonbit_decref_cycle_free(_M0L1kS946);
      return _M0L3valS3374 - 1;
    }
    continue;
    break;
  }
  _M0L3valS3375 = _M0L1kS946->$0;
  moonbit_decref_cycle_free(_M0L1kS946);
  return _M0L3valS3375 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS941
) {
  uint64_t _M0L1uS940;
  uint64_t _M0L4bitsS942;
  double _M0L6_2atmpS3365;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS940 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS941);
  _M0L4bitsS942 = _M0L1uS940 >> 11;
  _M0L6_2atmpS3365 = (double)_M0L4bitsS942;
  return _M0L6_2atmpS3365 * 0x1p-53;
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
    int32_t _M0L3valS3327 = _M0L1iS933->$0;
    int32_t _M0L1nS3328 = _M0L1sS934->$0;
    if (_M0L3valS3327 < _M0L1nS3328) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3329 = _M0L1sS934->$4;
      int32_t _M0L3valS3330 = _M0L1iS933->$0;
      int32_t _M0L3valS3332;
      int32_t _M0L6_2atmpS3331;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3329, _M0L3valS3330, 0);
      _M0L3valS3332 = _M0L1iS933->$0;
      _M0L6_2atmpS3331 = _M0L3valS3332 + 1;
      _M0L1iS933->$0 = _M0L6_2atmpS3331;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS933);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS3336 = _M0L1sS934->$3;
    int32_t _M0L6_2atmpS3335;
    int32_t _if__result_6109;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS3335 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3336, 0);
    if (_M0L6_2atmpS3335 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS3334 = _M0L1sS934->$2;
      float _M0L6_2atmpS3333;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3333 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS3334, 0);
      _if__result_6109 = _M0L6_2atmpS3333 <= _M0L1tS936;
    } else {
      _if__result_6109 = 0;
    }
    if (_if__result_6109) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3364 =
        _M0L1sS934->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS3361 = _M0L5paramS3364->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3363 = _M0L1sS934->$3;
      int32_t _M0L6_2atmpS3362;
      int32_t _M0L1jS937;
      struct _M0TPB5ArrayGbE* _M0L4fireS3337;
      struct _M0TPB5ArrayGfE* _M0L1gS3338;
      struct _M0TPB5ArrayGfE* _M0L1gS3341;
      float _M0L6_2atmpS3340;
      float _M0L6_2atmpS3339;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3347;
      int32_t _M0L6_2atmpS3346;
      int32_t _M0L6_2atmpS3342;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3345;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS3344;
      int32_t _M0L6_2atmpS3343;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3362 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3363, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS937
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS3361, _M0L6_2atmpS3362);
      _M0L4fireS3337 = _M0L1sS934->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3337, _M0L1jS937, 1);
      _M0L1gS3338 = _M0L1sS934->$5;
      _M0L1gS3341 = _M0L1sS934->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3340 = _M0MPC15array5Array2atGfE(_M0L1gS3341, _M0L1jS937);
      _M0L6_2atmpS3339 = _M0L6_2atmpS3340 + _M0L1wS938;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS3338, _M0L1jS937, _M0L6_2atmpS3339);
      _M0L11next__indexS3347 = _M0L1sS934->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3346 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3347, 0);
      _M0L6_2atmpS3342 = _M0L6_2atmpS3346 + 1;
      _M0L5paramS3345 = _M0L1sS934->$1;
      _M0L10spiketimesS3344 = _M0L5paramS3345->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3343 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS3344);
      if (_M0L6_2atmpS3342 < _M0L6_2atmpS3343) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3348 = _M0L1sS934->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3351 = _M0L1sS934->$3;
        int32_t _M0L6_2atmpS3350;
        int32_t _M0L6_2atmpS3349;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3352;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3357;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS3354;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3356;
        int32_t _M0L6_2atmpS3355;
        float _M0L6_2atmpS3353;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3350
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3351, 0);
        _M0L6_2atmpS3349 = _M0L6_2atmpS3350 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3348, 0, _M0L6_2atmpS3349);
        _M0L11next__spikeS3352 = _M0L1sS934->$2;
        _M0L5paramS3357 = _M0L1sS934->$1;
        _M0L10spiketimesS3354 = _M0L5paramS3357->$0;
        _M0L11next__indexS3356 = _M0L1sS934->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3355
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3356, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3353
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS3354, _M0L6_2atmpS3355);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3352, 0, _M0L6_2atmpS3353);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3358 = _M0L1sS934->$2;
        float _M0L6_2atmpS3359 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3360;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3358, 0, _M0L6_2atmpS3359);
        _M0L11next__indexS3360 = _M0L1sS934->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3360, 0, -1);
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
  struct _M0TPB5ArrayGbE* _M0L6activeS3240;
  int32_t _M0L6_2atmpS3239;
  int32_t _if__result_6110;
  int32_t _M0L6n__preS919;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3242;
  int32_t _M0L6_2atmpS3241;
  float _M0L11u__baselineS921;
  float _M0L6tau__fS3326;
  float _M0L11inv__tau__fS923;
  float _M0L6tau__dS3325;
  float _M0L11inv__tau__dS924;
  struct _M0TPB8MutLocalGiE* _M0L1jS925;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3240 = _M0L4varsS918->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3239 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3240);
  if (_M0L6_2atmpS3239 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3238 = _M0L4varsS918->$6;
    int32_t _M0L6_2atmpS3237;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3237 = _M0MPC15array5Array2atGbE(_M0L6activeS3238, 0);
    _if__result_6110 = !_M0L6_2atmpS3237;
  } else {
    _if__result_6110 = 0;
  }
  if (_if__result_6110) {
    return 0;
  }
  _M0L6n__preS919 = _M0L4varsS918->$0;
  _M0L3rhoS3242 = _M0L3synS920->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3241 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3242);
  if (_M0L6_2atmpS3241 == 0) {
    return 0;
  }
  _M0L11u__baselineS921 = _M0L5paramS922->$0;
  _M0L6tau__fS3326 = _M0L5paramS922->$1;
  _M0L11inv__tau__fS923 = 0x1p+0f / _M0L6tau__fS3326;
  _M0L6tau__dS3325 = _M0L5paramS922->$2;
  _M0L11inv__tau__dS924 = 0x1p+0f / _M0L6tau__dS3325;
  _M0L1jS925
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS925)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS925->$0 = 0;
  while (1) {
    int32_t _M0L3valS3243 = _M0L1jS925->$0;
    if (_M0L3valS3243 < _M0L6n__preS919) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3246 = _M0L3synS920->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3244 = _M0L3preS3246->$5;
      int32_t _M0L3valS3245 = _M0L1jS925->$0;
      int32_t _M0L3valS3273;
      int32_t _M0L6_2atmpS3272;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3244, _M0L3valS3245)) {
        struct _M0TPB5ArrayGfE* _M0L1uS3247 = _M0L4varsS918->$2;
        int32_t _M0L3valS3248 = _M0L1jS925->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS3256 = _M0L4varsS918->$2;
        int32_t _M0L3valS3257 = _M0L1jS925->$0;
        float _M0L6_2atmpS3250;
        struct _M0TPB5ArrayGfE* _M0L1uS3254;
        int32_t _M0L3valS3255;
        float _M0L6_2atmpS3253;
        float _M0L6_2atmpS3252;
        float _M0L6_2atmpS3251;
        float _M0L6_2atmpS3249;
        struct _M0TPB5ArrayGfE* _M0L1xS3258;
        int32_t _M0L3valS3259;
        struct _M0TPB5ArrayGfE* _M0L1xS3270;
        int32_t _M0L3valS3271;
        float _M0L6_2atmpS3261;
        struct _M0TPB5ArrayGfE* _M0L1uS3268;
        int32_t _M0L3valS3269;
        float _M0L6_2atmpS3267;
        float _M0L6_2atmpS3263;
        struct _M0TPB5ArrayGfE* _M0L1xS3265;
        int32_t _M0L3valS3266;
        float _M0L6_2atmpS3264;
        float _M0L6_2atmpS3262;
        float _M0L6_2atmpS3260;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3250
        = _M0MPC15array5Array2atGfE(_M0L1uS3256, _M0L3valS3257);
        _M0L1uS3254 = _M0L4varsS918->$2;
        _M0L3valS3255 = _M0L1jS925->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3253
        = _M0MPC15array5Array2atGfE(_M0L1uS3254, _M0L3valS3255);
        _M0L6_2atmpS3252 = 0x1p+0f - _M0L6_2atmpS3253;
        _M0L6_2atmpS3251 = _M0L11u__baselineS921 * _M0L6_2atmpS3252;
        _M0L6_2atmpS3249 = _M0L6_2atmpS3250 + _M0L6_2atmpS3251;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3247, _M0L3valS3248, _M0L6_2atmpS3249);
        _M0L1xS3258 = _M0L4varsS918->$3;
        _M0L3valS3259 = _M0L1jS925->$0;
        _M0L1xS3270 = _M0L4varsS918->$3;
        _M0L3valS3271 = _M0L1jS925->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3261
        = _M0MPC15array5Array2atGfE(_M0L1xS3270, _M0L3valS3271);
        _M0L1uS3268 = _M0L4varsS918->$2;
        _M0L3valS3269 = _M0L1jS925->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3267
        = _M0MPC15array5Array2atGfE(_M0L1uS3268, _M0L3valS3269);
        _M0L6_2atmpS3263 = -_M0L6_2atmpS3267;
        _M0L1xS3265 = _M0L4varsS918->$3;
        _M0L3valS3266 = _M0L1jS925->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3264
        = _M0MPC15array5Array2atGfE(_M0L1xS3265, _M0L3valS3266);
        _M0L6_2atmpS3262 = _M0L6_2atmpS3263 * _M0L6_2atmpS3264;
        _M0L6_2atmpS3260 = _M0L6_2atmpS3261 + _M0L6_2atmpS3262;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3258, _M0L3valS3259, _M0L6_2atmpS3260);
      }
      _M0L3valS3273 = _M0L1jS925->$0;
      _M0L6_2atmpS3272 = _M0L3valS3273 + 1;
      _M0L1jS925->$0 = _M0L6_2atmpS3272;
      continue;
    }
    break;
  }
  _M0L1jS925->$0 = 0;
  while (1) {
    int32_t _M0L3valS3274 = _M0L1jS925->$0;
    if (_M0L3valS3274 < _M0L6n__preS919) {
      struct _M0TPB5ArrayGfE* _M0L1uS3275 = _M0L4varsS918->$2;
      int32_t _M0L3valS3276 = _M0L1jS925->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS3285 = _M0L4varsS918->$2;
      int32_t _M0L3valS3286 = _M0L1jS925->$0;
      float _M0L6_2atmpS3278;
      struct _M0TPB5ArrayGfE* _M0L1uS3283;
      int32_t _M0L3valS3284;
      float _M0L6_2atmpS3282;
      float _M0L6_2atmpS3281;
      float _M0L6_2atmpS3280;
      float _M0L6_2atmpS3279;
      float _M0L6_2atmpS3277;
      struct _M0TPB5ArrayGfE* _M0L1xS3287;
      int32_t _M0L3valS3288;
      struct _M0TPB5ArrayGfE* _M0L1xS3297;
      int32_t _M0L3valS3298;
      float _M0L6_2atmpS3290;
      struct _M0TPB5ArrayGfE* _M0L1xS3295;
      int32_t _M0L3valS3296;
      float _M0L6_2atmpS3294;
      float _M0L6_2atmpS3293;
      float _M0L6_2atmpS3292;
      float _M0L6_2atmpS3291;
      float _M0L6_2atmpS3289;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS3299;
      int32_t _M0L3valS3300;
      struct _M0TPB5ArrayGfE* _M0L1uS3306;
      int32_t _M0L3valS3307;
      float _M0L6_2atmpS3302;
      struct _M0TPB5ArrayGfE* _M0L1xS3304;
      int32_t _M0L3valS3305;
      float _M0L6_2atmpS3303;
      float _M0L6_2atmpS3301;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3324;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3322;
      int32_t _M0L3valS3323;
      int32_t _M0L5startS928;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3321;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3318;
      int32_t _M0L3valS3320;
      int32_t _M0L6_2atmpS3319;
      int32_t _M0L3endS929;
      struct _M0TPB8MutLocalGiE* _M0L1sS930;
      int32_t _M0L3valS3317;
      int32_t _M0L6_2atmpS3316;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3278
      = _M0MPC15array5Array2atGfE(_M0L1uS3285, _M0L3valS3286);
      _M0L1uS3283 = _M0L4varsS918->$2;
      _M0L3valS3284 = _M0L1jS925->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3282
      = _M0MPC15array5Array2atGfE(_M0L1uS3283, _M0L3valS3284);
      _M0L6_2atmpS3281 = _M0L11u__baselineS921 - _M0L6_2atmpS3282;
      _M0L6_2atmpS3280 = _M0L2dtS927 * _M0L6_2atmpS3281;
      _M0L6_2atmpS3279 = _M0L6_2atmpS3280 * _M0L11inv__tau__fS923;
      _M0L6_2atmpS3277 = _M0L6_2atmpS3278 + _M0L6_2atmpS3279;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS3275, _M0L3valS3276, _M0L6_2atmpS3277);
      _M0L1xS3287 = _M0L4varsS918->$3;
      _M0L3valS3288 = _M0L1jS925->$0;
      _M0L1xS3297 = _M0L4varsS918->$3;
      _M0L3valS3298 = _M0L1jS925->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3290
      = _M0MPC15array5Array2atGfE(_M0L1xS3297, _M0L3valS3298);
      _M0L1xS3295 = _M0L4varsS918->$3;
      _M0L3valS3296 = _M0L1jS925->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3294
      = _M0MPC15array5Array2atGfE(_M0L1xS3295, _M0L3valS3296);
      _M0L6_2atmpS3293 = 0x1p+0f - _M0L6_2atmpS3294;
      _M0L6_2atmpS3292 = _M0L2dtS927 * _M0L6_2atmpS3293;
      _M0L6_2atmpS3291 = _M0L6_2atmpS3292 * _M0L11inv__tau__dS924;
      _M0L6_2atmpS3289 = _M0L6_2atmpS3290 + _M0L6_2atmpS3291;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS3287, _M0L3valS3288, _M0L6_2atmpS3289);
      _M0L8rho__preS3299 = _M0L4varsS918->$4;
      _M0L3valS3300 = _M0L1jS925->$0;
      _M0L1uS3306 = _M0L4varsS918->$2;
      _M0L3valS3307 = _M0L1jS925->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3302
      = _M0MPC15array5Array2atGfE(_M0L1uS3306, _M0L3valS3307);
      _M0L1xS3304 = _M0L4varsS918->$3;
      _M0L3valS3305 = _M0L1jS925->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3303
      = _M0MPC15array5Array2atGfE(_M0L1xS3304, _M0L3valS3305);
      _M0L6_2atmpS3301 = _M0L6_2atmpS3302 * _M0L6_2atmpS3303;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS3299, _M0L3valS3300, _M0L6_2atmpS3301);
      _M0L6matrixS3324 = _M0L3synS920->$4;
      _M0L6rowptrS3322 = _M0L6matrixS3324->$2;
      _M0L3valS3323 = _M0L1jS925->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS928
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3322, _M0L3valS3323);
      _M0L6matrixS3321 = _M0L3synS920->$4;
      _M0L6rowptrS3318 = _M0L6matrixS3321->$2;
      _M0L3valS3320 = _M0L1jS925->$0;
      _M0L6_2atmpS3319 = _M0L3valS3320 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS929
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3318, _M0L6_2atmpS3319);
      _M0L1sS930
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS930)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS930->$0 = _M0L5startS928;
      while (1) {
        int32_t _M0L3valS3308 = _M0L1sS930->$0;
        if (_M0L3valS3308 < _M0L3endS929) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS3309 = _M0L3synS920->$6;
          int32_t _M0L3valS3310 = _M0L1sS930->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS3312 = _M0L4varsS918->$4;
          int32_t _M0L3valS3313 = _M0L1jS925->$0;
          float _M0L6_2atmpS3311;
          int32_t _M0L3valS3315;
          int32_t _M0L6_2atmpS3314;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS3311
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS3312, _M0L3valS3313);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS3309, _M0L3valS3310, _M0L6_2atmpS3311);
          _M0L3valS3315 = _M0L1sS930->$0;
          _M0L6_2atmpS3314 = _M0L3valS3315 + 1;
          _M0L1sS930->$0 = _M0L6_2atmpS3314;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS930);
        }
        break;
      }
      _M0L3valS3317 = _M0L1jS925->$0;
      _M0L6_2atmpS3316 = _M0L3valS3317 + 1;
      _M0L1jS925->$0 = _M0L6_2atmpS3316;
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
  struct _M0TPB5ArrayGbE* _M0L6activeS3148;
  int32_t _M0L6_2atmpS3147;
  int32_t _if__result_6114;
  int32_t _M0L6n__preS900;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3150;
  int32_t _M0L6_2atmpS3149;
  struct _M0TPB8MutLocalGiE* _M0L1jS902;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3148 = _M0L4varsS899->$6;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3147 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3148);
  if (_M0L6_2atmpS3147 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3146 = _M0L4varsS899->$6;
    int32_t _M0L6_2atmpS3145;
    #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3145 = _M0MPC15array5Array2atGbE(_M0L6activeS3146, 0);
    _if__result_6114 = !_M0L6_2atmpS3145;
  } else {
    _if__result_6114 = 0;
  }
  if (_if__result_6114) {
    return 0;
  }
  _M0L6n__preS900 = _M0L4varsS899->$0;
  _M0L3rhoS3150 = _M0L3synS901->$6;
  #line 357 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3149 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3150);
  if (_M0L6_2atmpS3149 == 0) {
    return 0;
  }
  _M0L1jS902
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS902)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS902->$0 = 0;
  while (1) {
    int32_t _M0L3valS3151 = _M0L1jS902->$0;
    if (_M0L3valS3151 < _M0L6n__preS900) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3154 = _M0L3synS901->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3152 = _M0L3preS3154->$5;
      int32_t _M0L3valS3153 = _M0L1jS902->$0;
      int32_t _M0L3valS3236;
      int32_t _M0L6_2atmpS3235;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3152, _M0L3valS3153)) {
        struct _M0TPB5ArrayGfE* _M0L6tau__dS3233 = _M0L5paramS904->$0;
        int32_t _M0L3valS3234 = _M0L1jS902->$0;
        float _M0L9tau__d__jS903;
        struct _M0TPB5ArrayGfE* _M0L6tau__fS3231;
        int32_t _M0L3valS3232;
        float _M0L9tau__f__jS905;
        struct _M0TPB5ArrayGfE* _M0L1uS3229;
        int32_t _M0L3valS3230;
        float _M0L14u__baseline__jS906;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3227;
        int32_t _M0L3valS3228;
        float _M0L6_2atmpS3226;
        float _M0L7dt__preS907;
        float _M0L7dt__preS909;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3155;
        int32_t _M0L3valS3156;
        float _M0L6_2atmpS3225;
        float _M0L6arg__fS910;
        struct _M0TPB5ArrayGfE* _M0L1uS3157;
        int32_t _M0L3valS3158;
        struct _M0TPB5ArrayGfE* _M0L1uS3164;
        int32_t _M0L3valS3165;
        float _M0L6_2atmpS3163;
        float _M0L6_2atmpS3161;
        float _M0L6_2atmpS3162;
        float _M0L6_2atmpS3160;
        float _M0L6_2atmpS3159;
        float _M0L6_2atmpS3224;
        float _M0L6arg__dS911;
        struct _M0TPB5ArrayGfE* _M0L1xS3166;
        int32_t _M0L3valS3167;
        struct _M0TPB5ArrayGfE* _M0L1xS3173;
        int32_t _M0L3valS3174;
        float _M0L6_2atmpS3172;
        float _M0L6_2atmpS3170;
        float _M0L6_2atmpS3171;
        float _M0L6_2atmpS3169;
        float _M0L6_2atmpS3168;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3175;
        int32_t _M0L3valS3176;
        struct _M0TPB5ArrayGfE* _M0L1uS3182;
        int32_t _M0L3valS3183;
        float _M0L6_2atmpS3178;
        struct _M0TPB5ArrayGfE* _M0L1xS3180;
        int32_t _M0L3valS3181;
        float _M0L6_2atmpS3179;
        float _M0L6_2atmpS3177;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3223;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3221;
        int32_t _M0L3valS3222;
        int32_t _M0L5startS912;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3220;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3217;
        int32_t _M0L3valS3219;
        int32_t _M0L6_2atmpS3218;
        int32_t _M0L3endS913;
        struct _M0TPB8MutLocalGiE* _M0L1sS914;
        struct _M0TPB5ArrayGfE* _M0L1uS3192;
        int32_t _M0L3valS3193;
        struct _M0TPB5ArrayGfE* _M0L1uS3201;
        int32_t _M0L3valS3202;
        float _M0L6_2atmpS3195;
        struct _M0TPB5ArrayGfE* _M0L1uS3199;
        int32_t _M0L3valS3200;
        float _M0L6_2atmpS3198;
        float _M0L6_2atmpS3197;
        float _M0L6_2atmpS3196;
        float _M0L6_2atmpS3194;
        struct _M0TPB5ArrayGfE* _M0L1xS3203;
        int32_t _M0L3valS3204;
        struct _M0TPB5ArrayGfE* _M0L1xS3215;
        int32_t _M0L3valS3216;
        float _M0L6_2atmpS3206;
        struct _M0TPB5ArrayGfE* _M0L1uS3213;
        int32_t _M0L3valS3214;
        float _M0L6_2atmpS3212;
        float _M0L6_2atmpS3208;
        struct _M0TPB5ArrayGfE* _M0L1xS3210;
        int32_t _M0L3valS3211;
        float _M0L6_2atmpS3209;
        float _M0L6_2atmpS3207;
        float _M0L6_2atmpS3205;
        #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__d__jS903
        = _M0MPC15array5Array2atGfE(_M0L6tau__dS3233, _M0L3valS3234);
        _M0L6tau__fS3231 = _M0L5paramS904->$1;
        _M0L3valS3232 = _M0L1jS902->$0;
        #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__f__jS905
        = _M0MPC15array5Array2atGfE(_M0L6tau__fS3231, _M0L3valS3232);
        _M0L1uS3229 = _M0L5paramS904->$2;
        _M0L3valS3230 = _M0L1jS902->$0;
        #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L14u__baseline__jS906
        = _M0MPC15array5Array2atGfE(_M0L1uS3229, _M0L3valS3230);
        _M0L11last__spikeS3227 = _M0L4varsS899->$5;
        _M0L3valS3228 = _M0L1jS902->$0;
        #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3226
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3227, _M0L3valS3228);
        _M0L7dt__preS907 = _M0L6t__nowS908 - _M0L6_2atmpS3226;
        if (_M0L7dt__preS907 < 0x0p+0f) {
          _M0L7dt__preS909 = 0x0p+0f;
        } else {
          _M0L7dt__preS909 = _M0L7dt__preS907;
        }
        _M0L11last__spikeS3155 = _M0L4varsS899->$5;
        _M0L3valS3156 = _M0L1jS902->$0;
        #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3155, _M0L3valS3156, _M0L6t__nowS908);
        _M0L6_2atmpS3225 = -_M0L7dt__preS909;
        _M0L6arg__fS910 = _M0L6_2atmpS3225 / _M0L9tau__f__jS905;
        _M0L1uS3157 = _M0L4varsS899->$2;
        _M0L3valS3158 = _M0L1jS902->$0;
        _M0L1uS3164 = _M0L4varsS899->$2;
        _M0L3valS3165 = _M0L1jS902->$0;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3163
        = _M0MPC15array5Array2atGfE(_M0L1uS3164, _M0L3valS3165);
        _M0L6_2atmpS3161 = _M0L14u__baseline__jS906 - _M0L6_2atmpS3163;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3162 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS910);
        _M0L6_2atmpS3160 = _M0L6_2atmpS3161 * _M0L6_2atmpS3162;
        _M0L6_2atmpS3159 = _M0L14u__baseline__jS906 - _M0L6_2atmpS3160;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3157, _M0L3valS3158, _M0L6_2atmpS3159);
        _M0L6_2atmpS3224 = -_M0L7dt__preS909;
        _M0L6arg__dS911 = _M0L6_2atmpS3224 / _M0L9tau__d__jS903;
        _M0L1xS3166 = _M0L4varsS899->$3;
        _M0L3valS3167 = _M0L1jS902->$0;
        _M0L1xS3173 = _M0L4varsS899->$3;
        _M0L3valS3174 = _M0L1jS902->$0;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3172
        = _M0MPC15array5Array2atGfE(_M0L1xS3173, _M0L3valS3174);
        _M0L6_2atmpS3170 = 0x1p+0f - _M0L6_2atmpS3172;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3171 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS911);
        _M0L6_2atmpS3169 = _M0L6_2atmpS3170 * _M0L6_2atmpS3171;
        _M0L6_2atmpS3168 = 0x1p+0f - _M0L6_2atmpS3169;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3166, _M0L3valS3167, _M0L6_2atmpS3168);
        _M0L8rho__preS3175 = _M0L4varsS899->$4;
        _M0L3valS3176 = _M0L1jS902->$0;
        _M0L1uS3182 = _M0L4varsS899->$2;
        _M0L3valS3183 = _M0L1jS902->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3178
        = _M0MPC15array5Array2atGfE(_M0L1uS3182, _M0L3valS3183);
        _M0L1xS3180 = _M0L4varsS899->$3;
        _M0L3valS3181 = _M0L1jS902->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3179
        = _M0MPC15array5Array2atGfE(_M0L1xS3180, _M0L3valS3181);
        _M0L6_2atmpS3177 = _M0L6_2atmpS3178 * _M0L6_2atmpS3179;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3175, _M0L3valS3176, _M0L6_2atmpS3177);
        _M0L6matrixS3223 = _M0L3synS901->$4;
        _M0L6rowptrS3221 = _M0L6matrixS3223->$2;
        _M0L3valS3222 = _M0L1jS902->$0;
        #line 374 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS912
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3221, _M0L3valS3222);
        _M0L6matrixS3220 = _M0L3synS901->$4;
        _M0L6rowptrS3217 = _M0L6matrixS3220->$2;
        _M0L3valS3219 = _M0L1jS902->$0;
        _M0L6_2atmpS3218 = _M0L3valS3219 + 1;
        #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS913
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3217, _M0L6_2atmpS3218);
        _M0L1sS914
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS914)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS914->$0 = _M0L5startS912;
        while (1) {
          int32_t _M0L3valS3184 = _M0L1sS914->$0;
          if (_M0L3valS3184 < _M0L3endS913) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3185 = _M0L3synS901->$6;
            int32_t _M0L3valS3186 = _M0L1sS914->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3188 = _M0L4varsS899->$4;
            int32_t _M0L3valS3189 = _M0L1jS902->$0;
            float _M0L6_2atmpS3187;
            int32_t _M0L3valS3191;
            int32_t _M0L6_2atmpS3190;
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3187
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3188, _M0L3valS3189);
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3185, _M0L3valS3186, _M0L6_2atmpS3187);
            _M0L3valS3191 = _M0L1sS914->$0;
            _M0L6_2atmpS3190 = _M0L3valS3191 + 1;
            _M0L1sS914->$0 = _M0L6_2atmpS3190;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS914);
          }
          break;
        }
        _M0L1uS3192 = _M0L4varsS899->$2;
        _M0L3valS3193 = _M0L1jS902->$0;
        _M0L1uS3201 = _M0L4varsS899->$2;
        _M0L3valS3202 = _M0L1jS902->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3195
        = _M0MPC15array5Array2atGfE(_M0L1uS3201, _M0L3valS3202);
        _M0L1uS3199 = _M0L4varsS899->$2;
        _M0L3valS3200 = _M0L1jS902->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3198
        = _M0MPC15array5Array2atGfE(_M0L1uS3199, _M0L3valS3200);
        _M0L6_2atmpS3197 = 0x1p+0f - _M0L6_2atmpS3198;
        _M0L6_2atmpS3196 = _M0L14u__baseline__jS906 * _M0L6_2atmpS3197;
        _M0L6_2atmpS3194 = _M0L6_2atmpS3195 + _M0L6_2atmpS3196;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3192, _M0L3valS3193, _M0L6_2atmpS3194);
        _M0L1xS3203 = _M0L4varsS899->$3;
        _M0L3valS3204 = _M0L1jS902->$0;
        _M0L1xS3215 = _M0L4varsS899->$3;
        _M0L3valS3216 = _M0L1jS902->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3206
        = _M0MPC15array5Array2atGfE(_M0L1xS3215, _M0L3valS3216);
        _M0L1uS3213 = _M0L4varsS899->$2;
        _M0L3valS3214 = _M0L1jS902->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3212
        = _M0MPC15array5Array2atGfE(_M0L1uS3213, _M0L3valS3214);
        _M0L6_2atmpS3208 = -_M0L6_2atmpS3212;
        _M0L1xS3210 = _M0L4varsS899->$3;
        _M0L3valS3211 = _M0L1jS902->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3209
        = _M0MPC15array5Array2atGfE(_M0L1xS3210, _M0L3valS3211);
        _M0L6_2atmpS3207 = _M0L6_2atmpS3208 * _M0L6_2atmpS3209;
        _M0L6_2atmpS3205 = _M0L6_2atmpS3206 + _M0L6_2atmpS3207;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3203, _M0L3valS3204, _M0L6_2atmpS3205);
      }
      _M0L3valS3236 = _M0L1jS902->$0;
      _M0L6_2atmpS3235 = _M0L3valS3236 + 1;
      _M0L1jS902->$0 = _M0L6_2atmpS3235;
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
  struct _M0TPB5ArrayGbE* _M0L6activeS3062;
  int32_t _M0L6_2atmpS3061;
  int32_t _if__result_6117;
  int32_t _M0L6n__preS882;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3064;
  int32_t _M0L6_2atmpS3063;
  float _M0L6tau__fS884;
  float _M0L6tau__dS886;
  float _M0L11u__baselineS887;
  struct _M0TPB8MutLocalGiE* _M0L1jS888;
  #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3062 = _M0L4varsS881->$6;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3061 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3062);
  if (_M0L6_2atmpS3061 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3060 = _M0L4varsS881->$6;
    int32_t _M0L6_2atmpS3059;
    #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3059 = _M0MPC15array5Array2atGbE(_M0L6activeS3060, 0);
    _if__result_6117 = !_M0L6_2atmpS3059;
  } else {
    _if__result_6117 = 0;
  }
  if (_if__result_6117) {
    return 0;
  }
  _M0L6n__preS882 = _M0L4varsS881->$0;
  _M0L3rhoS3064 = _M0L3synS883->$6;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3063 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3064);
  if (_M0L6_2atmpS3063 == 0) {
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
    int32_t _M0L3valS3065 = _M0L1jS888->$0;
    if (_M0L3valS3065 < _M0L6n__preS882) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3068 = _M0L3synS883->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3066 = _M0L3preS3068->$5;
      int32_t _M0L3valS3067 = _M0L1jS888->$0;
      int32_t _M0L3valS3144;
      int32_t _M0L6_2atmpS3143;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3066, _M0L3valS3067)) {
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3141 = _M0L4varsS881->$5;
        int32_t _M0L3valS3142 = _M0L1jS888->$0;
        float _M0L6_2atmpS3140;
        float _M0L7dt__preS889;
        float _M0L7dt__preS891;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3069;
        int32_t _M0L3valS3070;
        float _M0L6_2atmpS3139;
        float _M0L6arg__fS892;
        struct _M0TPB5ArrayGfE* _M0L1uS3071;
        int32_t _M0L3valS3072;
        struct _M0TPB5ArrayGfE* _M0L1uS3078;
        int32_t _M0L3valS3079;
        float _M0L6_2atmpS3077;
        float _M0L6_2atmpS3075;
        float _M0L6_2atmpS3076;
        float _M0L6_2atmpS3074;
        float _M0L6_2atmpS3073;
        float _M0L6_2atmpS3138;
        float _M0L6arg__dS893;
        struct _M0TPB5ArrayGfE* _M0L1xS3080;
        int32_t _M0L3valS3081;
        struct _M0TPB5ArrayGfE* _M0L1xS3087;
        int32_t _M0L3valS3088;
        float _M0L6_2atmpS3086;
        float _M0L6_2atmpS3084;
        float _M0L6_2atmpS3085;
        float _M0L6_2atmpS3083;
        float _M0L6_2atmpS3082;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3089;
        int32_t _M0L3valS3090;
        struct _M0TPB5ArrayGfE* _M0L1uS3096;
        int32_t _M0L3valS3097;
        float _M0L6_2atmpS3092;
        struct _M0TPB5ArrayGfE* _M0L1xS3094;
        int32_t _M0L3valS3095;
        float _M0L6_2atmpS3093;
        float _M0L6_2atmpS3091;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3137;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3135;
        int32_t _M0L3valS3136;
        int32_t _M0L5startS894;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3134;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3131;
        int32_t _M0L3valS3133;
        int32_t _M0L6_2atmpS3132;
        int32_t _M0L3endS895;
        struct _M0TPB8MutLocalGiE* _M0L1sS896;
        struct _M0TPB5ArrayGfE* _M0L1uS3106;
        int32_t _M0L3valS3107;
        struct _M0TPB5ArrayGfE* _M0L1uS3115;
        int32_t _M0L3valS3116;
        float _M0L6_2atmpS3109;
        struct _M0TPB5ArrayGfE* _M0L1uS3113;
        int32_t _M0L3valS3114;
        float _M0L6_2atmpS3112;
        float _M0L6_2atmpS3111;
        float _M0L6_2atmpS3110;
        float _M0L6_2atmpS3108;
        struct _M0TPB5ArrayGfE* _M0L1xS3117;
        int32_t _M0L3valS3118;
        struct _M0TPB5ArrayGfE* _M0L1xS3129;
        int32_t _M0L3valS3130;
        float _M0L6_2atmpS3120;
        struct _M0TPB5ArrayGfE* _M0L1uS3127;
        int32_t _M0L3valS3128;
        float _M0L6_2atmpS3126;
        float _M0L6_2atmpS3122;
        struct _M0TPB5ArrayGfE* _M0L1xS3124;
        int32_t _M0L3valS3125;
        float _M0L6_2atmpS3123;
        float _M0L6_2atmpS3121;
        float _M0L6_2atmpS3119;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3140
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3141, _M0L3valS3142);
        _M0L7dt__preS889 = _M0L6t__nowS890 - _M0L6_2atmpS3140;
        if (_M0L7dt__preS889 < 0x0p+0f) {
          _M0L7dt__preS891 = 0x0p+0f;
        } else {
          _M0L7dt__preS891 = _M0L7dt__preS889;
        }
        _M0L11last__spikeS3069 = _M0L4varsS881->$5;
        _M0L3valS3070 = _M0L1jS888->$0;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3069, _M0L3valS3070, _M0L6t__nowS890);
        _M0L6_2atmpS3139 = -_M0L7dt__preS891;
        _M0L6arg__fS892 = _M0L6_2atmpS3139 / _M0L6tau__fS884;
        _M0L1uS3071 = _M0L4varsS881->$2;
        _M0L3valS3072 = _M0L1jS888->$0;
        _M0L1uS3078 = _M0L4varsS881->$2;
        _M0L3valS3079 = _M0L1jS888->$0;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3077
        = _M0MPC15array5Array2atGfE(_M0L1uS3078, _M0L3valS3079);
        _M0L6_2atmpS3075 = _M0L11u__baselineS887 - _M0L6_2atmpS3077;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3076 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS892);
        _M0L6_2atmpS3074 = _M0L6_2atmpS3075 * _M0L6_2atmpS3076;
        _M0L6_2atmpS3073 = _M0L11u__baselineS887 - _M0L6_2atmpS3074;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3071, _M0L3valS3072, _M0L6_2atmpS3073);
        _M0L6_2atmpS3138 = -_M0L7dt__preS891;
        _M0L6arg__dS893 = _M0L6_2atmpS3138 / _M0L6tau__dS886;
        _M0L1xS3080 = _M0L4varsS881->$3;
        _M0L3valS3081 = _M0L1jS888->$0;
        _M0L1xS3087 = _M0L4varsS881->$3;
        _M0L3valS3088 = _M0L1jS888->$0;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3086
        = _M0MPC15array5Array2atGfE(_M0L1xS3087, _M0L3valS3088);
        _M0L6_2atmpS3084 = 0x1p+0f - _M0L6_2atmpS3086;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3085 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS893);
        _M0L6_2atmpS3083 = _M0L6_2atmpS3084 * _M0L6_2atmpS3085;
        _M0L6_2atmpS3082 = 0x1p+0f - _M0L6_2atmpS3083;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3080, _M0L3valS3081, _M0L6_2atmpS3082);
        _M0L8rho__preS3089 = _M0L4varsS881->$4;
        _M0L3valS3090 = _M0L1jS888->$0;
        _M0L1uS3096 = _M0L4varsS881->$2;
        _M0L3valS3097 = _M0L1jS888->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3092
        = _M0MPC15array5Array2atGfE(_M0L1uS3096, _M0L3valS3097);
        _M0L1xS3094 = _M0L4varsS881->$3;
        _M0L3valS3095 = _M0L1jS888->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3093
        = _M0MPC15array5Array2atGfE(_M0L1xS3094, _M0L3valS3095);
        _M0L6_2atmpS3091 = _M0L6_2atmpS3092 * _M0L6_2atmpS3093;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3089, _M0L3valS3090, _M0L6_2atmpS3091);
        _M0L6matrixS3137 = _M0L3synS883->$4;
        _M0L6rowptrS3135 = _M0L6matrixS3137->$2;
        _M0L3valS3136 = _M0L1jS888->$0;
        #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS894
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3135, _M0L3valS3136);
        _M0L6matrixS3134 = _M0L3synS883->$4;
        _M0L6rowptrS3131 = _M0L6matrixS3134->$2;
        _M0L3valS3133 = _M0L1jS888->$0;
        _M0L6_2atmpS3132 = _M0L3valS3133 + 1;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS895
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3131, _M0L6_2atmpS3132);
        _M0L1sS896
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS896)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS896->$0 = _M0L5startS894;
        while (1) {
          int32_t _M0L3valS3098 = _M0L1sS896->$0;
          if (_M0L3valS3098 < _M0L3endS895) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3099 = _M0L3synS883->$6;
            int32_t _M0L3valS3100 = _M0L1sS896->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3102 = _M0L4varsS881->$4;
            int32_t _M0L3valS3103 = _M0L1jS888->$0;
            float _M0L6_2atmpS3101;
            int32_t _M0L3valS3105;
            int32_t _M0L6_2atmpS3104;
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3101
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3102, _M0L3valS3103);
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3099, _M0L3valS3100, _M0L6_2atmpS3101);
            _M0L3valS3105 = _M0L1sS896->$0;
            _M0L6_2atmpS3104 = _M0L3valS3105 + 1;
            _M0L1sS896->$0 = _M0L6_2atmpS3104;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS896);
          }
          break;
        }
        _M0L1uS3106 = _M0L4varsS881->$2;
        _M0L3valS3107 = _M0L1jS888->$0;
        _M0L1uS3115 = _M0L4varsS881->$2;
        _M0L3valS3116 = _M0L1jS888->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3109
        = _M0MPC15array5Array2atGfE(_M0L1uS3115, _M0L3valS3116);
        _M0L1uS3113 = _M0L4varsS881->$2;
        _M0L3valS3114 = _M0L1jS888->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3112
        = _M0MPC15array5Array2atGfE(_M0L1uS3113, _M0L3valS3114);
        _M0L6_2atmpS3111 = 0x1p+0f - _M0L6_2atmpS3112;
        _M0L6_2atmpS3110 = _M0L11u__baselineS887 * _M0L6_2atmpS3111;
        _M0L6_2atmpS3108 = _M0L6_2atmpS3109 + _M0L6_2atmpS3110;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3106, _M0L3valS3107, _M0L6_2atmpS3108);
        _M0L1xS3117 = _M0L4varsS881->$3;
        _M0L3valS3118 = _M0L1jS888->$0;
        _M0L1xS3129 = _M0L4varsS881->$3;
        _M0L3valS3130 = _M0L1jS888->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3120
        = _M0MPC15array5Array2atGfE(_M0L1xS3129, _M0L3valS3130);
        _M0L1uS3127 = _M0L4varsS881->$2;
        _M0L3valS3128 = _M0L1jS888->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3126
        = _M0MPC15array5Array2atGfE(_M0L1uS3127, _M0L3valS3128);
        _M0L6_2atmpS3122 = -_M0L6_2atmpS3126;
        _M0L1xS3124 = _M0L4varsS881->$3;
        _M0L3valS3125 = _M0L1jS888->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3123
        = _M0MPC15array5Array2atGfE(_M0L1xS3124, _M0L3valS3125);
        _M0L6_2atmpS3121 = _M0L6_2atmpS3122 * _M0L6_2atmpS3123;
        _M0L6_2atmpS3119 = _M0L6_2atmpS3120 + _M0L6_2atmpS3121;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3117, _M0L3valS3118, _M0L6_2atmpS3119);
      }
      _M0L3valS3144 = _M0L1jS888->$0;
      _M0L6_2atmpS3143 = _M0L3valS3144 + 1;
      _M0L1jS888->$0 = _M0L6_2atmpS3143;
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
  struct _M0TPB5ArrayGfE* _M0L1tS3051;
  struct _M0TPB5ArrayGfE* _M0L1tS3054;
  float _M0L6_2atmpS3053;
  float _M0L6_2atmpS3052;
  struct _M0TPB5ArrayGiE* _M0L2ttS3055;
  struct _M0TPB5ArrayGiE* _M0L2ttS3058;
  int32_t _M0L6_2atmpS3057;
  int32_t _M0L6_2atmpS3056;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS3051 = _M0L1tS879->$0;
  _M0L1tS3054 = _M0L1tS879->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3053 = _M0MPC15array5Array2atGfE(_M0L1tS3054, 0);
  _M0L6_2atmpS3052 = _M0L6_2atmpS3053 + _M0L2dtS880;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS3051, 0, _M0L6_2atmpS3052);
  _M0L2ttS3055 = _M0L1tS879->$1;
  _M0L2ttS3058 = _M0L1tS879->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3057 = _M0MPC15array5Array2atGiE(_M0L2ttS3058, 0);
  _M0L6_2atmpS3056 = _M0L6_2atmpS3057 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS3055, 0, _M0L6_2atmpS3056);
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS878
) {
  struct _M0TPB5ArrayGfE* _M0L1tS3050;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS3050 = _M0L1tS878->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS3050, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS3049;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3046;
  int32_t* _M0L6_2atmpS3048;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS3047;
  struct _M0TP26RiantR8snn__mbt4Time* _block_6120;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3049 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3049[0] = 0x0p+0f;
  _M0L6_2atmpS3046
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3046)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS3046->$0 = _M0L6_2atmpS3049;
  _M0L6_2atmpS3046->$1 = 1;
  _M0L6_2atmpS3048 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS3048[0] = 0;
  _M0L6_2atmpS3047
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS3047)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS3047->$0 = _M0L6_2atmpS3048;
  _M0L6_2atmpS3047->$1 = 1;
  _block_6120
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_6120)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 110, 0);
  _block_6120->$0 = _M0L6_2atmpS3046;
  _block_6120->$1 = _M0L6_2atmpS3047;
  _block_6120->$2 = 0x1p-3f;
  return _block_6120;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS876
) {
  uint32_t _M0L1uS875;
  uint32_t _M0L4bitsS877;
  double _M0L6_2atmpS3045;
  double _M0L6_2atmpS3044;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS875 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS876);
  _M0L4bitsS877 = _M0L1uS875 >> 8;
  _M0L6_2atmpS3045 = (double)_M0L4bitsS877;
  _M0L6_2atmpS3044 = _M0L6_2atmpS3045 * 0x1p-24;
  return (float)_M0L6_2atmpS3044;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS874
) {
  uint64_t _M0L1uS873;
  uint64_t _M0L6_2atmpS3043;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS873 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS874);
  _M0L6_2atmpS3043 = _M0L1uS873 >> 32;
  return (uint32_t)_M0L6_2atmpS3043;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS866
) {
  uint64_t _M0L2s0S865;
  uint64_t _M0L2s1S867;
  uint64_t _M0L2s2S868;
  uint64_t _M0L2s3S869;
  uint64_t _M0L3tmpS870;
  uint64_t _M0L6_2atmpS3042;
  uint64_t _M0L3resS871;
  uint64_t _M0L1tS872;
  uint64_t _M0L6_2atmpS3032;
  uint64_t _M0L6_2atmpS3033;
  uint64_t _M0L2s2S3035;
  uint64_t _M0L6_2atmpS3034;
  uint64_t _M0L2s3S3037;
  uint64_t _M0L6_2atmpS3036;
  uint64_t _M0L2s2S3039;
  uint64_t _M0L6_2atmpS3038;
  uint64_t _M0L2s3S3041;
  uint64_t _M0L6_2atmpS3040;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S865 = _M0L1rS866->$0;
  _M0L2s1S867 = _M0L1rS866->$1;
  _M0L2s2S868 = _M0L1rS866->$2;
  _M0L2s3S869 = _M0L1rS866->$3;
  _M0L3tmpS870 = _M0L2s0S865 + _M0L2s3S869;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3042 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS870, 23);
  _M0L3resS871 = _M0L6_2atmpS3042 + _M0L2s0S865;
  _M0L1tS872 = _M0L2s1S867 << 17;
  _M0L6_2atmpS3032 = _M0L2s2S868 ^ _M0L2s0S865;
  _M0L1rS866->$2 = _M0L6_2atmpS3032;
  _M0L6_2atmpS3033 = _M0L2s3S869 ^ _M0L2s1S867;
  _M0L1rS866->$3 = _M0L6_2atmpS3033;
  _M0L2s2S3035 = _M0L1rS866->$2;
  _M0L6_2atmpS3034 = _M0L2s1S867 ^ _M0L2s2S3035;
  _M0L1rS866->$1 = _M0L6_2atmpS3034;
  _M0L2s3S3037 = _M0L1rS866->$3;
  _M0L6_2atmpS3036 = _M0L2s0S865 ^ _M0L2s3S3037;
  _M0L1rS866->$0 = _M0L6_2atmpS3036;
  _M0L2s2S3039 = _M0L1rS866->$2;
  _M0L6_2atmpS3038 = _M0L2s2S3039 ^ _M0L1tS872;
  _M0L1rS866->$2 = _M0L6_2atmpS3038;
  _M0L2s3S3041 = _M0L1rS866->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3040 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S3041, 45);
  _M0L1rS866->$3 = _M0L6_2atmpS3040;
  return _M0L3resS871;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS863, int32_t _M0L1kS864) {
  uint64_t _M0L6_2atmpS3029;
  int32_t _M0L6_2atmpS3031;
  uint64_t _M0L6_2atmpS3030;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3029 = _M0L1xS863 << (_M0L1kS864 & 63);
  _M0L6_2atmpS3031 = 64 - _M0L1kS864;
  _M0L6_2atmpS3030 = _M0L1xS863 >> (_M0L6_2atmpS3031 & 63);
  return _M0L6_2atmpS3029 | _M0L6_2atmpS3030;
}

float _M0MP26RiantR8snn__mbt7Monitor12firing__rate(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS859
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS3028;
  int32_t _M0L1nS858;
  struct _M0TPB5ArrayGfE* _M0L5timesS3027;
  int32_t _M0L4n__tS860;
  struct _M0TPB5ArrayGfE* _M0L5timesS3025;
  int32_t _M0L6_2atmpS3026;
  float _M0L6_2atmpS3022;
  struct _M0TPB5ArrayGfE* _M0L5timesS3024;
  float _M0L6_2atmpS3023;
  float _M0L9total__msS861;
  int32_t _M0L9n__spikesS862;
  float _M0L6_2atmpS3021;
  float _M0L6_2atmpS3020;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS3028 = _M0L1mS859->$2;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS858 = _M0MPC15array5Array6lengthGfE(_M0L4dataS3028);
  if (_M0L1nS858 < 2) {
    return 0x0p+0f;
  }
  _M0L5timesS3027 = _M0L1mS859->$3;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4n__tS860 = _M0MPC15array5Array6lengthGfE(_M0L5timesS3027);
  if (_M0L4n__tS860 < 2) {
    return 0x0p+0f;
  }
  _M0L5timesS3025 = _M0L1mS859->$3;
  _M0L6_2atmpS3026 = _M0L4n__tS860 - 1;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS3022
  = _M0MPC15array5Array2atGfE(_M0L5timesS3025, _M0L6_2atmpS3026);
  _M0L5timesS3024 = _M0L1mS859->$3;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS3023 = _M0MPC15array5Array2atGfE(_M0L5timesS3024, 0);
  _M0L9total__msS861 = _M0L6_2atmpS3022 - _M0L6_2atmpS3023;
  if (_M0L9total__msS861 <= 0x0p+0f) {
    return 0x0p+0f;
  }
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L9n__spikesS862
  = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L1mS859);
  _M0L6_2atmpS3021 = (float)_M0L9n__spikesS862;
  _M0L6_2atmpS3020 = _M0L6_2atmpS3021 * 0x1.f4p+9f;
  return _M0L6_2atmpS3020 / _M0L9total__msS861;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS851
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS3019;
  int32_t _M0L1nS850;
  struct _M0TPB8MutLocalGiE* _M0L5countS852;
  struct _M0TPB8MutLocalGfE* _M0L4prevS853;
  int32_t _M0L7_2abindS854;
  int32_t _M0L1iS855;
  int32_t _result_6122;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS3019 = _M0L1mS851->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS850 = _M0MPC15array5Array6lengthGfE(_M0L4dataS3019);
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
      struct _M0TPB5ArrayGfE* _M0L4dataS3017 = _M0L1mS851->$2;
      float _M0L3curS856;
      float _M0L3valS3014;
      int32_t _M0L6_2atmpS3018;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS856 = _M0MPC15array5Array2atGfE(_M0L4dataS3017, _M0L1iS855);
      _M0L3valS3014 = _M0L4prevS853->$0;
      if (_M0L3valS3014 < 0x1p-1f && _M0L3curS856 >= 0x1p-1f) {
        int32_t _M0L3valS3016 = _M0L5countS852->$0;
        int32_t _M0L6_2atmpS3015 = _M0L3valS3016 + 1;
        _M0L5countS852->$0 = _M0L6_2atmpS3015;
      }
      _M0L4prevS853->$0 = _M0L3curS856;
      _M0L6_2atmpS3018 = _M0L1iS855 + 1;
      _M0L1iS855 = _M0L6_2atmpS3018;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS853);
    }
    break;
  }
  _result_6122 = _M0L5countS852->$0;
  moonbit_decref_cycle_free(_M0L5countS852);
  return _result_6122;
}

double _M0FPC14math2ln(double _M0L1xS836) {
  struct _M0TUdiE* _M0L7_2abindS837;
  double _M0L5_2af1S838;
  int32_t _M0L5_2akiS839;
  double _M0L1fS841;
  double _M0L1kS842;
  double _M0L6_2atmpS3007;
  double _M0L1sS843;
  double _M0L2s2S844;
  double _M0L2s4S845;
  double _M0L6_2atmpS3006;
  double _M0L6_2atmpS3005;
  double _M0L6_2atmpS3004;
  double _M0L6_2atmpS3003;
  double _M0L6_2atmpS3002;
  double _M0L6_2atmpS3001;
  double _M0L2t1S846;
  double _M0L6_2atmpS3000;
  double _M0L6_2atmpS2999;
  double _M0L6_2atmpS2998;
  double _M0L6_2atmpS2997;
  double _M0L2t2S847;
  double _M0L1rS848;
  double _M0L6_2atmpS2996;
  double _M0L4hfsqS849;
  double _M0L6_2atmpS2989;
  double _M0L6_2atmpS2995;
  double _M0L6_2atmpS2993;
  double _M0L6_2atmpS2994;
  double _M0L6_2atmpS2992;
  double _M0L6_2atmpS2991;
  double _M0L6_2atmpS2990;
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
    double _M0L6_2atmpS3011 = _M0L5_2af1S838 * 0x1p+1;
    double _M0L6_2atmpS3008 = _M0L6_2atmpS3011 - 0x1p+0;
    int32_t _M0L6_2atmpS3010 = _M0L5_2akiS839 - 1;
    double _M0L6_2atmpS3009 = (double)_M0L6_2atmpS3010;
    _M0L1fS841 = _M0L6_2atmpS3008;
    _M0L1kS842 = _M0L6_2atmpS3009;
    goto join_840;
  } else {
    double _M0L6_2atmpS3012 = _M0L5_2af1S838 - 0x1p+0;
    double _M0L6_2atmpS3013 = (double)_M0L5_2akiS839;
    _M0L1fS841 = _M0L6_2atmpS3012;
    _M0L1kS842 = _M0L6_2atmpS3013;
    goto join_840;
  }
  join_840:;
  _M0L6_2atmpS3007 = 0x1p+1 + _M0L1fS841;
  _M0L1sS843 = _M0L1fS841 / _M0L6_2atmpS3007;
  _M0L2s2S844 = _M0L1sS843 * _M0L1sS843;
  _M0L2s4S845 = _M0L2s2S844 * _M0L2s2S844;
  _M0L6_2atmpS3006 = _M0L2s4S845 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS3005 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS3006;
  _M0L6_2atmpS3004 = _M0L2s4S845 * _M0L6_2atmpS3005;
  _M0L6_2atmpS3003 = 0x1.2492494229359p-2 + _M0L6_2atmpS3004;
  _M0L6_2atmpS3002 = _M0L2s4S845 * _M0L6_2atmpS3003;
  _M0L6_2atmpS3001 = 0x1.5555555555593p-1 + _M0L6_2atmpS3002;
  _M0L2t1S846 = _M0L2s2S844 * _M0L6_2atmpS3001;
  _M0L6_2atmpS3000 = _M0L2s4S845 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2999 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS3000;
  _M0L6_2atmpS2998 = _M0L2s4S845 * _M0L6_2atmpS2999;
  _M0L6_2atmpS2997 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2998;
  _M0L2t2S847 = _M0L2s4S845 * _M0L6_2atmpS2997;
  _M0L1rS848 = _M0L2t1S846 + _M0L2t2S847;
  _M0L6_2atmpS2996 = 0x1p-1 * _M0L1fS841;
  _M0L4hfsqS849 = _M0L6_2atmpS2996 * _M0L1fS841;
  _M0L6_2atmpS2989 = _M0L1kS842 * 0x1.62e42feep-1;
  _M0L6_2atmpS2995 = _M0L4hfsqS849 + _M0L1rS848;
  _M0L6_2atmpS2993 = _M0L1sS843 * _M0L6_2atmpS2995;
  _M0L6_2atmpS2994 = _M0L1kS842 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2992 = _M0L6_2atmpS2993 + _M0L6_2atmpS2994;
  _M0L6_2atmpS2991 = _M0L4hfsqS849 - _M0L6_2atmpS2992;
  _M0L6_2atmpS2990 = _M0L6_2atmpS2991 - _M0L1fS841;
  return _M0L6_2atmpS2989 - _M0L6_2atmpS2990;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS829) {
  struct _M0TUdiE* _M0L7_2abindS830;
  double _M0L10_2anorm__fS831;
  int32_t _M0L6_2aexpS832;
  uint64_t _M0L1uS833;
  uint64_t _M0L6_2atmpS2988;
  uint64_t _M0L6_2atmpS2987;
  int32_t _M0L6_2atmpS2986;
  int32_t _M0L6_2atmpS2985;
  int32_t _M0L3expS834;
  uint64_t _M0L6_2atmpS2984;
  uint64_t _M0L6_2atmpS2983;
  uint64_t _M0L6_2atmpS2982;
  double _M0L4fracS835;
  struct _M0TUdiE* _block_6125;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS829 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS829)
    || _M0MPC16double6Double7is__nan(_M0L1fS829)
  ) {
    struct _M0TUdiE* _block_6124 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_6124)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_6124->$0 = _M0L1fS829;
    _block_6124->$1 = 0;
    return _block_6124;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS830 = _M0FPC14math9normalize(_M0L1fS829);
  _M0L10_2anorm__fS831 = _M0L7_2abindS830->$0;
  _M0L6_2aexpS832 = _M0L7_2abindS830->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS830);
  _M0L1uS833 = *(int64_t*)&_M0L10_2anorm__fS831;
  _M0L6_2atmpS2988 = _M0L1uS833 >> 52;
  _M0L6_2atmpS2987 = _M0L6_2atmpS2988 & 2047ull;
  _M0L6_2atmpS2986 = (int32_t)_M0L6_2atmpS2987;
  _M0L6_2atmpS2985 = _M0L6_2aexpS832 + _M0L6_2atmpS2986;
  _M0L3expS834 = _M0L6_2atmpS2985 - 1022;
  _M0L6_2atmpS2984 = ~9218868437227405312ull;
  _M0L6_2atmpS2983 = _M0L1uS833 & _M0L6_2atmpS2984;
  _M0L6_2atmpS2982 = _M0L6_2atmpS2983 | 4602678819172646912ull;
  _M0L4fracS835 = *(double*)&_M0L6_2atmpS2982;
  _block_6125 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_6125)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6125->$0 = _M0L4fracS835;
  _block_6125->$1 = _M0L3expS834;
  return _block_6125;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS828) {
  double _M0L6_2atmpS2979;
  struct _M0TUdiE* _block_6127;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2979 = fabs(_M0L1fS828);
  if (_M0L6_2atmpS2979 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2981 = (double)4503599627370496ll;
    double _M0L6_2atmpS2980 = _M0L1fS828 * _M0L6_2atmpS2981;
    struct _M0TUdiE* _block_6126 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_6126)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_6126->$0 = _M0L6_2atmpS2980;
    _block_6126->$1 = -52;
    return _block_6126;
  }
  _block_6127 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_6127)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6127->$0 = _M0L1fS828;
  _block_6127->$1 = 0;
  return _block_6127;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS827) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS827 != _M0L4selfS827;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS826) {
  double _M0L6_2atmpS2978;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2978 = (double)_M0L4selfS826;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2978);
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
      float* _M0L3bufS2970 = _M0L3arrS805->$0;
      int32_t _M0L6_2atmpS2971;
      _M0L3bufS2970[_M0L1iS807] = _M0L4elemS808;
      _M0L6_2atmpS2971 = _M0L1iS807 + 1;
      _M0L1iS807 = _M0L6_2atmpS2971;
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
      uint8_t* _M0L3bufS2972 = _M0L3arrS810->$0;
      int32_t _M0L6_2atmpS2973;
      _M0L3bufS2972[_M0L1iS812] = _M0L4elemS813;
      _M0L6_2atmpS2973 = _M0L1iS812 + 1;
      _M0L1iS812 = _M0L6_2atmpS2973;
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
      int32_t* _M0L3bufS2974 = _M0L3arrS815->$0;
      int32_t _M0L6_2atmpS2975;
      _M0L3bufS2974[_M0L1iS817] = _M0L4elemS818;
      _M0L6_2atmpS2975 = _M0L1iS817 + 1;
      _M0L1iS817 = _M0L6_2atmpS2975;
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
      struct _M0TPB5ArrayGfE** _M0L3bufS2976 = _M0L3arrS820->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS5702 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2976[_M0L1iS822];
      int32_t _M0L6_2atmpS2977;
      moonbit_incref_cycle_free(_M0L4elemS823);
      if (_M0L6_2aoldS5702) {
        moonbit_decref_cycle_free(_M0L6_2aoldS5702);
      }
      _M0L3bufS2976[_M0L1iS822] = _M0L4elemS823;
      _M0L6_2atmpS2977 = _M0L1iS822 + 1;
      _M0L1iS822 = _M0L6_2atmpS2977;
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
    float* _M0L6_2atmpS2966;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2966 = _M0MPC15array5Array6bufferGfE(_M0L4selfS790);
    _M0L6_2atmpS2966[_M0L5indexS791] = _M0L5valueS792;
    moonbit_decref_cycle_free(_M0L6_2atmpS2966);
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
    int32_t* _M0L6_2atmpS2967;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2967 = _M0MPC15array5Array6bufferGiE(_M0L4selfS794);
    _M0L6_2atmpS2967[_M0L5indexS795] = _M0L5valueS796;
    moonbit_decref_cycle_free(_M0L6_2atmpS2967);
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
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2968;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS5703;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2968
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS798);
    _M0L6_2aoldS5703
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2968[_M0L5indexS799];
    if (_M0L6_2aoldS5703) {
      moonbit_decref_cycle_free(_M0L6_2aoldS5703);
    }
    _M0L6_2atmpS2968[_M0L5indexS799] = _M0L5valueS800;
    moonbit_decref_cycle_free(_M0L6_2atmpS2968);
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
    uint8_t* _M0L6_2atmpS2969;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2969 = _M0MPC15array5Array6bufferGbE(_M0L4selfS802);
    _M0L6_2atmpS2969[_M0L5indexS803] = _M0L5valueS804;
    moonbit_decref_cycle_free(_M0L6_2atmpS2969);
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
    float* _M0L3bufS2964 = _M0L4selfS782->$0;
    float _M0L1vS784 = (float)_M0L3bufS2964[_M0L5indexS783];
    void* _block_6132;
    _M0L4selfS782->$1 = _M0L5indexS783;
    _block_6132
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_6132)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_6132)->$0 = _M0L1vS784;
    return _block_6132;
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
    int32_t* _M0L3bufS2965 = _M0L4selfS786->$0;
    int32_t _M0L1vS788 = (int32_t)_M0L3bufS2965[_M0L5indexS787];
    _M0L4selfS786->$1 = _M0L5indexS787;
    return (int64_t)_M0L1vS788;
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS764,
  int32_t _M0L5indexS765
) {
  int32_t _M0L3lenS763;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS763 = _M0L4selfS764->$1;
  if (_M0L5indexS765 >= 0 && _M0L5indexS765 < _M0L3lenS763) {
    float* _M0L6_2atmpS2958;
    float _result_6133;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2958 = _M0MPC15array5Array6bufferGfE(_M0L4selfS764);
    _result_6133 = (float)_M0L6_2atmpS2958[_M0L5indexS765];
    moonbit_decref_cycle_free(_M0L6_2atmpS2958);
    return _result_6133;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS767,
  int32_t _M0L5indexS768
) {
  int32_t _M0L3lenS766;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS766 = _M0L4selfS767->$1;
  if (_M0L5indexS768 >= 0 && _M0L5indexS768 < _M0L3lenS766) {
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS2959;
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L6_2atmpS5704;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2959
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L4selfS767);
    _M0L6_2atmpS5704
    = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L6_2atmpS2959[
        _M0L5indexS768
      ];
    if (_M0L6_2atmpS5704) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5704);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2959);
    return _M0L6_2atmpS5704;
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
    moonbit_string_t* _M0L6_2atmpS2960;
    moonbit_string_t _M0L6_2atmpS5705;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2960 = _M0MPC15array5Array6bufferGsE(_M0L4selfS770);
    _M0L6_2atmpS5705 = (moonbit_string_t)_M0L6_2atmpS2960[_M0L5indexS771];
    moonbit_incref_cycle_free(_M0L6_2atmpS5705);
    moonbit_decref_cycle_free(_M0L6_2atmpS2960);
    return _M0L6_2atmpS5705;
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
    int32_t* _M0L6_2atmpS2961;
    int32_t _result_6134;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2961 = _M0MPC15array5Array6bufferGiE(_M0L4selfS773);
    _result_6134 = (int32_t)_M0L6_2atmpS2961[_M0L5indexS774];
    moonbit_decref_cycle_free(_M0L6_2atmpS2961);
    return _result_6134;
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
    uint8_t* _M0L6_2atmpS2962;
    int32_t _result_6135;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2962 = _M0MPC15array5Array6bufferGbE(_M0L4selfS776);
    _result_6135 = (int32_t)_M0L6_2atmpS2962[_M0L5indexS777];
    moonbit_decref_cycle_free(_M0L6_2atmpS2962);
    return _result_6135;
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
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2963;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS5706;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2963
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS779);
    _M0L6_2atmpS5706
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2963[_M0L5indexS780];
    if (_M0L6_2atmpS5706) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5706);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2963);
    return _M0L6_2atmpS5706;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS762) {
  moonbit_string_t _M0L6_2atmpS2957;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2957 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS762);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2957);
  moonbit_decref_cycle_free(_M0L6_2atmpS2957);
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
  uint64_t _M0L6_2atmpS2956;
  uint64_t _M0L6_2atmpS2955;
  int32_t _M0L8ieeeSignS748;
  uint64_t _M0L12ieeeMantissaS749;
  uint64_t _M0L6_2atmpS2954;
  uint64_t _M0L6_2atmpS2953;
  int32_t _M0L12ieeeExponentS750;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS751;
  struct _M0TPB17FloatingDecimal64* _M0L1vS752;
  moonbit_string_t _result_6137;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS744 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L3valS744 >= -0x1p+53 && _M0L3valS744 <= 0x1p+53) {
    if (_M0L3valS744 >= -0x1p+31 && _M0L3valS744 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS745;
      double _M0L6_2atmpS2942;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS745 = _M0MPC16double6Double7to__int(_M0L3valS744);
      _M0L6_2atmpS2942 = (double)_M0L1iS745;
      if (_M0L6_2atmpS2942 == _M0L3valS744) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS745, 10);
      }
    } else {
      int64_t _M0L1iS746;
      double _M0L6_2atmpS2943;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS746 = _M0MPC16double6Double9to__int64(_M0L3valS744);
      _M0L6_2atmpS2943 = (double)_M0L1iS746;
      if (_M0L6_2atmpS2943 == _M0L3valS744) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS746, 10);
      }
    }
  }
  _M0L4bitsS747 = *(int64_t*)&_M0L3valS744;
  _M0L6_2atmpS2956 = _M0L4bitsS747 >> 63;
  _M0L6_2atmpS2955 = _M0L6_2atmpS2956 & 1ull;
  _M0L8ieeeSignS748 = _M0L6_2atmpS2955 != 0ull;
  _M0L12ieeeMantissaS749 = _M0L4bitsS747 & 4503599627370495ull;
  _M0L6_2atmpS2954 = _M0L4bitsS747 >> 52;
  _M0L6_2atmpS2953 = _M0L6_2atmpS2954 & 2047ull;
  _M0L12ieeeExponentS750 = (int32_t)_M0L6_2atmpS2953;
  if (
    _M0L12ieeeExponentS750 == 2047
    || _M0L12ieeeExponentS750 == 0 && _M0L12ieeeMantissaS749 == 0ull
  ) {
    int32_t _M0L6_2atmpS2944 = _M0L12ieeeExponentS750 != 0;
    int32_t _M0L6_2atmpS2945 = _M0L12ieeeMantissaS749 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS748, _M0L6_2atmpS2944, _M0L6_2atmpS2945);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS751
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS749, _M0L12ieeeExponentS750);
  if (_M0L7_2abindS751 == 0) {
    uint32_t _M0L6_2atmpS2946;
    if (_M0L7_2abindS751) {
      moonbit_decref_cycle_free(_M0L7_2abindS751);
    }
    _M0L6_2atmpS2946 = *(uint32_t*)&_M0L12ieeeExponentS750;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS752 = _M0FPB3d2d(_M0L12ieeeMantissaS749, _M0L6_2atmpS2946);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS753 = _M0L7_2abindS751;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS754 = _M0L7_2aSomeS753;
    struct _M0TPB17FloatingDecimal64* _M0L1xS755 = _M0L4_2afS754;
    while (1) {
      uint64_t _M0L8mantissaS2952 = _M0L1xS755->$0;
      uint64_t _M0L1qS756 = _M0L8mantissaS2952 / 10ull;
      uint64_t _M0L8mantissaS2950 = _M0L1xS755->$0;
      uint64_t _M0L6_2atmpS2951 = 10ull * _M0L1qS756;
      uint64_t _M0L1rS757 = _M0L8mantissaS2950 - _M0L6_2atmpS2951;
      int32_t _M0L8exponentS2949;
      int32_t _M0L6_2atmpS2948;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2947;
      if (_M0L1rS757 != 0ull) {
        _M0L1vS752 = _M0L1xS755;
        break;
      }
      _M0L8exponentS2949 = _M0L1xS755->$1;
      moonbit_decref_cycle_free(_M0L1xS755);
      _M0L6_2atmpS2948 = _M0L8exponentS2949 + 1;
      _M0L6_2atmpS2947
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2947)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2947->$0 = _M0L1qS756;
      _M0L6_2atmpS2947->$1 = _M0L6_2atmpS2948;
      _M0L1xS755 = _M0L6_2atmpS2947;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_6137 = _M0FPB9to__chars(_M0L1vS752, _M0L8ieeeSignS748);
  moonbit_decref_cycle_free(_M0L1vS752);
  return _result_6137;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS739,
  int32_t _M0L12ieeeExponentS741
) {
  uint64_t _M0L2m2S738;
  int32_t _M0L6_2atmpS2941;
  int32_t _M0L2e2S740;
  int32_t _M0L6_2atmpS2940;
  uint64_t _M0L6_2atmpS2939;
  uint64_t _M0L4maskS742;
  uint64_t _M0L8fractionS743;
  int32_t _M0L6_2atmpS2938;
  uint64_t _M0L6_2atmpS2937;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2936;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S738 = 4503599627370496ull | _M0L12ieeeMantissaS739;
  _M0L6_2atmpS2941 = _M0L12ieeeExponentS741 - 1023;
  _M0L2e2S740 = _M0L6_2atmpS2941 - 52;
  if (_M0L2e2S740 > 0) {
    return 0;
  }
  if (_M0L2e2S740 < -52) {
    return 0;
  }
  _M0L6_2atmpS2940 = -_M0L2e2S740;
  _M0L6_2atmpS2939 = 1ull << (_M0L6_2atmpS2940 & 63);
  _M0L4maskS742 = _M0L6_2atmpS2939 - 1ull;
  _M0L8fractionS743 = _M0L2m2S738 & _M0L4maskS742;
  if (_M0L8fractionS743 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2938 = -_M0L2e2S740;
  _M0L6_2atmpS2937 = _M0L2m2S738 >> (_M0L6_2atmpS2938 & 63);
  _M0L6_2atmpS2936
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2936)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2936->$0 = _M0L6_2atmpS2937;
  _M0L6_2atmpS2936->$1 = 0;
  return _M0L6_2atmpS2936;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS706,
  int32_t _M0L4signS704
) {
  moonbit_bytes_t _M0L6resultS702;
  int32_t _M0Lm5indexS703;
  uint64_t _M0L6outputS705;
  int32_t _M0L7olengthS707;
  int32_t _M0L8exponentS2935;
  int32_t _M0L6_2atmpS2934;
  int32_t _M0Lm3expS708;
  int32_t _M0L6_2atmpS2933;
  int32_t _M0L6_2atmpS2931;
  int32_t _M0L18scientificNotationS709;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS702 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS703 = 0;
  if (_M0L4signS704) {
    int32_t _M0L6_2atmpS2805 = _M0Lm5indexS703;
    int32_t _M0L6_2atmpS2806;
    if (
      _M0L6_2atmpS2805 < 0
      || _M0L6_2atmpS2805 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2805] = 45;
    _M0L6_2atmpS2806 = _M0Lm5indexS703;
    _M0Lm5indexS703 = _M0L6_2atmpS2806 + 1;
  }
  _M0L6outputS705 = _M0L1vS706->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS707 = _M0FPB17decimal__length17(_M0L6outputS705);
  _M0L8exponentS2935 = _M0L1vS706->$1;
  _M0L6_2atmpS2934 = _M0L8exponentS2935 + _M0L7olengthS707;
  _M0Lm3expS708 = _M0L6_2atmpS2934 - 1;
  _M0L6_2atmpS2933 = _M0Lm3expS708;
  if (_M0L6_2atmpS2933 >= -6) {
    int32_t _M0L6_2atmpS2932 = _M0Lm3expS708;
    _M0L6_2atmpS2931 = _M0L6_2atmpS2932 < 21;
  } else {
    _M0L6_2atmpS2931 = 0;
  }
  _M0L18scientificNotationS709 = !_M0L6_2atmpS2931;
  if (_M0L18scientificNotationS709) {
    int32_t _M0L7_2abindS710 = _M0L7olengthS707 - 1;
    uint64_t _M0L6outputS711;
    int32_t _M0L1iS712 = 0;
    uint64_t _M0L6outputS713 = _M0L6outputS705;
    int32_t _M0L6_2atmpS2807;
    int32_t _M0L6_2atmpS2811;
    int32_t _M0L6_2atmpS2810;
    int32_t _M0L6_2atmpS2809;
    int32_t _M0L6_2atmpS2808;
    int32_t _M0L6_2atmpS2815;
    int32_t _M0L6_2atmpS2816;
    int32_t _M0L6_2atmpS2817;
    int32_t _M0L6_2atmpS2818;
    int32_t _M0L6_2atmpS2819;
    int32_t _M0L6_2atmpS2825;
    int32_t _M0L6_2atmpS2858;
    moonbit_string_t _result_6139;
    while (1) {
      if (_M0L1iS712 < _M0L7_2abindS710) {
        uint64_t _M0L1cS714 = _M0L6outputS713 % 10ull;
        int32_t _M0L6_2atmpS2864 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2863 = _M0L6_2atmpS2864 + _M0L7olengthS707;
        int32_t _M0L6_2atmpS2859 = _M0L6_2atmpS2863 - _M0L1iS712;
        int32_t _M0L6_2atmpS2862 = (int32_t)_M0L1cS714;
        int32_t _M0L6_2atmpS2861 = 48 + _M0L6_2atmpS2862;
        int32_t _M0L6_2atmpS2860 = _M0L6_2atmpS2861 & 0xff;
        int32_t _M0L6_2atmpS2865;
        uint64_t _M0L6_2atmpS2866;
        if (
          _M0L6_2atmpS2859 < 0
          || _M0L6_2atmpS2859 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2859] = _M0L6_2atmpS2860;
        _M0L6_2atmpS2865 = _M0L1iS712 + 1;
        _M0L6_2atmpS2866 = _M0L6outputS713 / 10ull;
        _M0L1iS712 = _M0L6_2atmpS2865;
        _M0L6outputS713 = _M0L6_2atmpS2866;
        continue;
      } else {
        _M0L6outputS711 = _M0L6outputS713;
      }
      break;
    }
    _M0L6_2atmpS2807 = _M0Lm5indexS703;
    _M0L6_2atmpS2811 = (int32_t)_M0L6outputS711;
    _M0L6_2atmpS2810 = _M0L6_2atmpS2811 % 10;
    _M0L6_2atmpS2809 = 48 + _M0L6_2atmpS2810;
    _M0L6_2atmpS2808 = _M0L6_2atmpS2809 & 0xff;
    if (
      _M0L6_2atmpS2807 < 0
      || _M0L6_2atmpS2807 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2807] = _M0L6_2atmpS2808;
    if (_M0L7olengthS707 > 1) {
      int32_t _M0L6_2atmpS2813 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2812 = _M0L6_2atmpS2813 + 1;
      if (
        _M0L6_2atmpS2812 < 0
        || _M0L6_2atmpS2812 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2812] = 46;
    } else {
      int32_t _M0L6_2atmpS2814 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2814 - 1;
    }
    _M0L6_2atmpS2815 = _M0Lm5indexS703;
    _M0L6_2atmpS2816 = _M0L7olengthS707 + 1;
    _M0Lm5indexS703 = _M0L6_2atmpS2815 + _M0L6_2atmpS2816;
    _M0L6_2atmpS2817 = _M0Lm5indexS703;
    if (
      _M0L6_2atmpS2817 < 0
      || _M0L6_2atmpS2817 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2817] = 101;
    _M0L6_2atmpS2818 = _M0Lm5indexS703;
    _M0Lm5indexS703 = _M0L6_2atmpS2818 + 1;
    _M0L6_2atmpS2819 = _M0Lm3expS708;
    if (_M0L6_2atmpS2819 < 0) {
      int32_t _M0L6_2atmpS2820 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2821;
      int32_t _M0L6_2atmpS2822;
      if (
        _M0L6_2atmpS2820 < 0
        || _M0L6_2atmpS2820 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2820] = 45;
      _M0L6_2atmpS2821 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2821 + 1;
      _M0L6_2atmpS2822 = _M0Lm3expS708;
      _M0Lm3expS708 = -_M0L6_2atmpS2822;
    } else {
      int32_t _M0L6_2atmpS2823 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2824;
      if (
        _M0L6_2atmpS2823 < 0
        || _M0L6_2atmpS2823 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2823] = 43;
      _M0L6_2atmpS2824 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2824 + 1;
    }
    _M0L6_2atmpS2825 = _M0Lm3expS708;
    if (_M0L6_2atmpS2825 >= 100) {
      int32_t _M0L6_2atmpS2841 = _M0Lm3expS708;
      int32_t _M0L1aS716 = _M0L6_2atmpS2841 / 100;
      int32_t _M0L6_2atmpS2840 = _M0Lm3expS708;
      int32_t _M0L6_2atmpS2839 = _M0L6_2atmpS2840 / 10;
      int32_t _M0L1bS717 = _M0L6_2atmpS2839 % 10;
      int32_t _M0L6_2atmpS2838 = _M0Lm3expS708;
      int32_t _M0L1cS718 = _M0L6_2atmpS2838 % 10;
      int32_t _M0L6_2atmpS2826 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2828 = 48 + _M0L1aS716;
      int32_t _M0L6_2atmpS2827 = _M0L6_2atmpS2828 & 0xff;
      int32_t _M0L6_2atmpS2832;
      int32_t _M0L6_2atmpS2829;
      int32_t _M0L6_2atmpS2831;
      int32_t _M0L6_2atmpS2830;
      int32_t _M0L6_2atmpS2836;
      int32_t _M0L6_2atmpS2833;
      int32_t _M0L6_2atmpS2835;
      int32_t _M0L6_2atmpS2834;
      int32_t _M0L6_2atmpS2837;
      if (
        _M0L6_2atmpS2826 < 0
        || _M0L6_2atmpS2826 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2826] = _M0L6_2atmpS2827;
      _M0L6_2atmpS2832 = _M0Lm5indexS703;
      _M0L6_2atmpS2829 = _M0L6_2atmpS2832 + 1;
      _M0L6_2atmpS2831 = 48 + _M0L1bS717;
      _M0L6_2atmpS2830 = _M0L6_2atmpS2831 & 0xff;
      if (
        _M0L6_2atmpS2829 < 0
        || _M0L6_2atmpS2829 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2829] = _M0L6_2atmpS2830;
      _M0L6_2atmpS2836 = _M0Lm5indexS703;
      _M0L6_2atmpS2833 = _M0L6_2atmpS2836 + 2;
      _M0L6_2atmpS2835 = 48 + _M0L1cS718;
      _M0L6_2atmpS2834 = _M0L6_2atmpS2835 & 0xff;
      if (
        _M0L6_2atmpS2833 < 0
        || _M0L6_2atmpS2833 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2833] = _M0L6_2atmpS2834;
      _M0L6_2atmpS2837 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2837 + 3;
    } else {
      int32_t _M0L6_2atmpS2842 = _M0Lm3expS708;
      if (_M0L6_2atmpS2842 >= 10) {
        int32_t _M0L6_2atmpS2852 = _M0Lm3expS708;
        int32_t _M0L1aS719 = _M0L6_2atmpS2852 / 10;
        int32_t _M0L6_2atmpS2851 = _M0Lm3expS708;
        int32_t _M0L1bS720 = _M0L6_2atmpS2851 % 10;
        int32_t _M0L6_2atmpS2843 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2845 = 48 + _M0L1aS719;
        int32_t _M0L6_2atmpS2844 = _M0L6_2atmpS2845 & 0xff;
        int32_t _M0L6_2atmpS2849;
        int32_t _M0L6_2atmpS2846;
        int32_t _M0L6_2atmpS2848;
        int32_t _M0L6_2atmpS2847;
        int32_t _M0L6_2atmpS2850;
        if (
          _M0L6_2atmpS2843 < 0
          || _M0L6_2atmpS2843 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2843] = _M0L6_2atmpS2844;
        _M0L6_2atmpS2849 = _M0Lm5indexS703;
        _M0L6_2atmpS2846 = _M0L6_2atmpS2849 + 1;
        _M0L6_2atmpS2848 = 48 + _M0L1bS720;
        _M0L6_2atmpS2847 = _M0L6_2atmpS2848 & 0xff;
        if (
          _M0L6_2atmpS2846 < 0
          || _M0L6_2atmpS2846 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2846] = _M0L6_2atmpS2847;
        _M0L6_2atmpS2850 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2850 + 2;
      } else {
        int32_t _M0L6_2atmpS2853 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2856 = _M0Lm3expS708;
        int32_t _M0L6_2atmpS2855 = 48 + _M0L6_2atmpS2856;
        int32_t _M0L6_2atmpS2854 = _M0L6_2atmpS2855 & 0xff;
        int32_t _M0L6_2atmpS2857;
        if (
          _M0L6_2atmpS2853 < 0
          || _M0L6_2atmpS2853 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2853] = _M0L6_2atmpS2854;
        _M0L6_2atmpS2857 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2857 + 1;
      }
    }
    _M0L6_2atmpS2858 = _M0Lm5indexS703;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6139
    = _M0FPB19string__from__bytes(_M0L6resultS702, 0, _M0L6_2atmpS2858);
    moonbit_decref_cycle_free(_M0L6resultS702);
    return _result_6139;
  } else {
    int32_t _M0L6_2atmpS2867 = _M0Lm3expS708;
    int32_t _M0L6_2atmpS2930;
    moonbit_string_t _result_6145;
    if (_M0L6_2atmpS2867 < 0) {
      int32_t _M0L6_2atmpS2868 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2870;
      int32_t _M0L6_2atmpS2869;
      int32_t _M0L6_2atmpS2871;
      int32_t _M0L1iS721;
      int32_t _M0L6_2atmpS2886;
      int32_t _M0L6_2atmpS2888;
      int32_t _M0L6_2atmpS2887;
      int32_t _M0L7currentS723;
      int32_t _M0L1iS724;
      uint64_t _M0L6outputS725;
      if (
        _M0L6_2atmpS2868 < 0
        || _M0L6_2atmpS2868 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2868] = 48;
      _M0L6_2atmpS2870 = _M0Lm5indexS703;
      _M0L6_2atmpS2869 = _M0L6_2atmpS2870 + 1;
      if (
        _M0L6_2atmpS2869 < 0
        || _M0L6_2atmpS2869 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2869] = 46;
      _M0L6_2atmpS2871 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2871 + 2;
      _M0L1iS721 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2872 = _M0Lm3expS708;
        if (_M0L1iS721 > _M0L6_2atmpS2872) {
          int32_t _M0L6_2atmpS2875 = _M0Lm5indexS703;
          int32_t _M0L6_2atmpS2874 = _M0L6_2atmpS2875 - _M0L1iS721;
          int32_t _M0L6_2atmpS2873 = _M0L6_2atmpS2874 - 1;
          int32_t _M0L6_2atmpS2876;
          if (
            _M0L6_2atmpS2873 < 0
            || _M0L6_2atmpS2873 >= Moonbit_array_length(_M0L6resultS702)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS702[_M0L6_2atmpS2873] = 48;
          _M0L6_2atmpS2876 = _M0L1iS721 - 1;
          _M0L1iS721 = _M0L6_2atmpS2876;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2886 = _M0Lm5indexS703;
      _M0L6_2atmpS2888 = _M0Lm3expS708;
      _M0L6_2atmpS2887 = -1 - _M0L6_2atmpS2888;
      _M0L7currentS723 = _M0L6_2atmpS2886 + _M0L6_2atmpS2887;
      _M0L1iS724 = 0;
      _M0L6outputS725 = _M0L6outputS705;
      while (1) {
        if (_M0L1iS724 < _M0L7olengthS707) {
          int32_t _M0L6_2atmpS2883 = _M0L7currentS723 + _M0L7olengthS707;
          int32_t _M0L6_2atmpS2882 = _M0L6_2atmpS2883 - _M0L1iS724;
          int32_t _M0L6_2atmpS2877 = _M0L6_2atmpS2882 - 1;
          uint64_t _M0L6_2atmpS2881 = _M0L6outputS725 % 10ull;
          int32_t _M0L6_2atmpS2880 = (int32_t)_M0L6_2atmpS2881;
          int32_t _M0L6_2atmpS2879 = 48 + _M0L6_2atmpS2880;
          int32_t _M0L6_2atmpS2878 = _M0L6_2atmpS2879 & 0xff;
          int32_t _M0L6_2atmpS2884;
          uint64_t _M0L6_2atmpS2885;
          if (
            _M0L6_2atmpS2877 < 0
            || _M0L6_2atmpS2877 >= Moonbit_array_length(_M0L6resultS702)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS702[_M0L6_2atmpS2877] = _M0L6_2atmpS2878;
          _M0L6_2atmpS2884 = _M0L1iS724 + 1;
          _M0L6_2atmpS2885 = _M0L6outputS725 / 10ull;
          _M0L1iS724 = _M0L6_2atmpS2884;
          _M0L6outputS725 = _M0L6_2atmpS2885;
          continue;
        }
        break;
      }
      _M0Lm5indexS703 = _M0L7currentS723 + _M0L7olengthS707;
    } else {
      int32_t _M0L6_2atmpS2890 = _M0Lm3expS708;
      int32_t _M0L6_2atmpS2889 = _M0L6_2atmpS2890 + 1;
      if (_M0L6_2atmpS2889 >= _M0L7olengthS707) {
        int32_t _M0L1iS727 = 0;
        uint64_t _M0L6outputS728 = _M0L6outputS705;
        int32_t _M0L6_2atmpS2901;
        int32_t _M0L6_2atmpS2906;
        int32_t _M0L7_2abindS730;
        int32_t _M0L1iS731;
        int32_t _M0L6_2atmpS2907;
        int32_t _M0L6_2atmpS2910;
        int32_t _M0L6_2atmpS2909;
        int32_t _M0L6_2atmpS2908;
        while (1) {
          if (_M0L1iS727 < _M0L7olengthS707) {
            int32_t _M0L6_2atmpS2898 = _M0Lm5indexS703;
            int32_t _M0L6_2atmpS2897 = _M0L6_2atmpS2898 + _M0L7olengthS707;
            int32_t _M0L6_2atmpS2896 = _M0L6_2atmpS2897 - _M0L1iS727;
            int32_t _M0L6_2atmpS2891 = _M0L6_2atmpS2896 - 1;
            uint64_t _M0L6_2atmpS2895 = _M0L6outputS728 % 10ull;
            int32_t _M0L6_2atmpS2894 = (int32_t)_M0L6_2atmpS2895;
            int32_t _M0L6_2atmpS2893 = 48 + _M0L6_2atmpS2894;
            int32_t _M0L6_2atmpS2892 = _M0L6_2atmpS2893 & 0xff;
            int32_t _M0L6_2atmpS2899;
            uint64_t _M0L6_2atmpS2900;
            if (
              _M0L6_2atmpS2891 < 0
              || _M0L6_2atmpS2891 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2891] = _M0L6_2atmpS2892;
            _M0L6_2atmpS2899 = _M0L1iS727 + 1;
            _M0L6_2atmpS2900 = _M0L6outputS728 / 10ull;
            _M0L1iS727 = _M0L6_2atmpS2899;
            _M0L6outputS728 = _M0L6_2atmpS2900;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2901 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2901 + _M0L7olengthS707;
        _M0L6_2atmpS2906 = _M0Lm3expS708;
        _M0L7_2abindS730 = _M0L6_2atmpS2906 + 1;
        _M0L1iS731 = _M0L7olengthS707;
        while (1) {
          if (_M0L1iS731 < _M0L7_2abindS730) {
            int32_t _M0L6_2atmpS2904 = _M0Lm5indexS703;
            int32_t _M0L6_2atmpS2903 = _M0L6_2atmpS2904 + _M0L1iS731;
            int32_t _M0L6_2atmpS2902 = _M0L6_2atmpS2903 - _M0L7olengthS707;
            int32_t _M0L6_2atmpS2905;
            if (
              _M0L6_2atmpS2902 < 0
              || _M0L6_2atmpS2902 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2902] = 48;
            _M0L6_2atmpS2905 = _M0L1iS731 + 1;
            _M0L1iS731 = _M0L6_2atmpS2905;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2907 = _M0Lm5indexS703;
        _M0L6_2atmpS2910 = _M0Lm3expS708;
        _M0L6_2atmpS2909 = _M0L6_2atmpS2910 + 1;
        _M0L6_2atmpS2908 = _M0L6_2atmpS2909 - _M0L7olengthS707;
        _M0Lm5indexS703 = _M0L6_2atmpS2907 + _M0L6_2atmpS2908;
      } else {
        int32_t _M0L6_2atmpS2927 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2926 = _M0L6_2atmpS2927 + 1;
        int32_t _M0L1iS733 = 0;
        int32_t _M0L7currentS734 = _M0L6_2atmpS2926;
        uint64_t _M0L6outputS735 = _M0L6outputS705;
        int32_t _M0L6_2atmpS2928;
        int32_t _M0L6_2atmpS2929;
        while (1) {
          if (_M0L1iS733 < _M0L7olengthS707) {
            int32_t _M0L6_2atmpS2922 = _M0L7olengthS707 - _M0L1iS733;
            int32_t _M0L6_2atmpS2920 = _M0L6_2atmpS2922 - 1;
            int32_t _M0L6_2atmpS2921 = _M0Lm3expS708;
            int32_t _M0L7currentS736;
            int32_t _M0L6_2atmpS2917;
            int32_t _M0L6_2atmpS2916;
            int32_t _M0L6_2atmpS2911;
            uint64_t _M0L6_2atmpS2915;
            int32_t _M0L6_2atmpS2914;
            int32_t _M0L6_2atmpS2913;
            int32_t _M0L6_2atmpS2912;
            int32_t _M0L6_2atmpS2918;
            uint64_t _M0L6_2atmpS2919;
            if (_M0L6_2atmpS2920 == _M0L6_2atmpS2921) {
              int32_t _M0L6_2atmpS2925 = _M0L7currentS734 + _M0L7olengthS707;
              int32_t _M0L6_2atmpS2924 = _M0L6_2atmpS2925 - _M0L1iS733;
              int32_t _M0L6_2atmpS2923 = _M0L6_2atmpS2924 - 1;
              if (
                _M0L6_2atmpS2923 < 0
                || _M0L6_2atmpS2923 >= Moonbit_array_length(_M0L6resultS702)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS702[_M0L6_2atmpS2923] = 46;
              _M0L7currentS736 = _M0L7currentS734 - 1;
            } else {
              _M0L7currentS736 = _M0L7currentS734;
            }
            _M0L6_2atmpS2917 = _M0L7currentS736 + _M0L7olengthS707;
            _M0L6_2atmpS2916 = _M0L6_2atmpS2917 - _M0L1iS733;
            _M0L6_2atmpS2911 = _M0L6_2atmpS2916 - 1;
            _M0L6_2atmpS2915 = _M0L6outputS735 % 10ull;
            _M0L6_2atmpS2914 = (int32_t)_M0L6_2atmpS2915;
            _M0L6_2atmpS2913 = 48 + _M0L6_2atmpS2914;
            _M0L6_2atmpS2912 = _M0L6_2atmpS2913 & 0xff;
            if (
              _M0L6_2atmpS2911 < 0
              || _M0L6_2atmpS2911 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2911] = _M0L6_2atmpS2912;
            _M0L6_2atmpS2918 = _M0L1iS733 + 1;
            _M0L6_2atmpS2919 = _M0L6outputS735 / 10ull;
            _M0L1iS733 = _M0L6_2atmpS2918;
            _M0L7currentS734 = _M0L7currentS736;
            _M0L6outputS735 = _M0L6_2atmpS2919;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2928 = _M0Lm5indexS703;
        _M0L6_2atmpS2929 = _M0L7olengthS707 + 1;
        _M0Lm5indexS703 = _M0L6_2atmpS2928 + _M0L6_2atmpS2929;
      }
    }
    _M0L6_2atmpS2930 = _M0Lm5indexS703;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6145
    = _M0FPB19string__from__bytes(_M0L6resultS702, 0, _M0L6_2atmpS2930);
    moonbit_decref_cycle_free(_M0L6resultS702);
    return _result_6145;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS648,
  uint32_t _M0L12ieeeExponentS647
) {
  int32_t _M0Lm2e2S645;
  uint64_t _M0Lm2m2S646;
  uint64_t _M0L6_2atmpS2804;
  uint64_t _M0L6_2atmpS2803;
  int32_t _M0L4evenS649;
  uint64_t _M0L6_2atmpS2802;
  uint64_t _M0L2mvS650;
  int32_t _M0L7mmShiftS651;
  uint64_t _M0Lm2vrS652;
  uint64_t _M0Lm2vpS653;
  uint64_t _M0Lm2vmS654;
  int32_t _M0Lm3e10S655;
  int32_t _M0Lm17vmIsTrailingZerosS656;
  int32_t _M0Lm17vrIsTrailingZerosS657;
  int32_t _M0L6_2atmpS2704;
  int32_t _M0Lm7removedS676;
  int32_t _M0Lm16lastRemovedDigitS677;
  uint64_t _M0Lm6outputS678;
  int32_t _M0L6_2atmpS2800;
  int32_t _M0L6_2atmpS2801;
  int32_t _M0L3expS701;
  uint64_t _M0L6_2atmpS2799;
  struct _M0TPB17FloatingDecimal64* _block_6151;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S645 = 0;
  _M0Lm2m2S646 = 0ull;
  if (_M0L12ieeeExponentS647 == 0u) {
    _M0Lm2e2S645 = -1076;
    _M0Lm2m2S646 = _M0L12ieeeMantissaS648;
  } else {
    int32_t _M0L6_2atmpS2703 = *(int32_t*)&_M0L12ieeeExponentS647;
    int32_t _M0L6_2atmpS2702 = _M0L6_2atmpS2703 - 1023;
    int32_t _M0L6_2atmpS2701 = _M0L6_2atmpS2702 - 52;
    _M0Lm2e2S645 = _M0L6_2atmpS2701 - 2;
    _M0Lm2m2S646 = 4503599627370496ull | _M0L12ieeeMantissaS648;
  }
  _M0L6_2atmpS2804 = _M0Lm2m2S646;
  _M0L6_2atmpS2803 = _M0L6_2atmpS2804 & 1ull;
  _M0L4evenS649 = _M0L6_2atmpS2803 == 0ull;
  _M0L6_2atmpS2802 = _M0Lm2m2S646;
  _M0L2mvS650 = 4ull * _M0L6_2atmpS2802;
  _M0L7mmShiftS651
  = _M0L12ieeeMantissaS648 != 0ull || _M0L12ieeeExponentS647 <= 1u;
  _M0Lm2vrS652 = 0ull;
  _M0Lm2vpS653 = 0ull;
  _M0Lm2vmS654 = 0ull;
  _M0Lm3e10S655 = 0;
  _M0Lm17vmIsTrailingZerosS656 = 0;
  _M0Lm17vrIsTrailingZerosS657 = 0;
  _M0L6_2atmpS2704 = _M0Lm2e2S645;
  if (_M0L6_2atmpS2704 >= 0) {
    int32_t _M0L6_2atmpS2726 = _M0Lm2e2S645;
    int32_t _M0L6_2atmpS2722;
    int32_t _M0L6_2atmpS2725;
    int32_t _M0L6_2atmpS2724;
    int32_t _M0L6_2atmpS2723;
    int32_t _M0L1qS658;
    int32_t _M0L6_2atmpS2721;
    int32_t _M0L6_2atmpS2720;
    int32_t _M0L1kS659;
    int32_t _M0L6_2atmpS2719;
    int32_t _M0L6_2atmpS2718;
    int32_t _M0L6_2atmpS2717;
    int32_t _M0L1iS660;
    struct _M0TPB8Pow5Pair _M0L4pow5S661;
    uint64_t _M0L6_2atmpS2716;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS662;
    uint64_t _M0L8_2avrOutS663;
    uint64_t _M0L8_2avpOutS664;
    uint64_t _M0L8_2avmOutS665;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2722 = _M0FPB9log10Pow2(_M0L6_2atmpS2726);
    _M0L6_2atmpS2725 = _M0Lm2e2S645;
    _M0L6_2atmpS2724 = _M0L6_2atmpS2725 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2723 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2724);
    _M0L1qS658 = _M0L6_2atmpS2722 - _M0L6_2atmpS2723;
    _M0Lm3e10S655 = _M0L1qS658;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2721 = _M0FPB8pow5bits(_M0L1qS658);
    _M0L6_2atmpS2720 = 125 + _M0L6_2atmpS2721;
    _M0L1kS659 = _M0L6_2atmpS2720 - 1;
    _M0L6_2atmpS2719 = _M0Lm2e2S645;
    _M0L6_2atmpS2718 = -_M0L6_2atmpS2719;
    _M0L6_2atmpS2717 = _M0L6_2atmpS2718 + _M0L1qS658;
    _M0L1iS660 = _M0L6_2atmpS2717 + _M0L1kS659;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S661 = _M0FPB22double__computeInvPow5(_M0L1qS658);
    _M0L6_2atmpS2716 = _M0Lm2m2S646;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS662
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2716, _M0L4pow5S661, _M0L1iS660, _M0L7mmShiftS651);
    _M0L8_2avrOutS663 = _M0L7_2abindS662.$0;
    _M0L8_2avpOutS664 = _M0L7_2abindS662.$1;
    _M0L8_2avmOutS665 = _M0L7_2abindS662.$2;
    _M0Lm2vrS652 = _M0L8_2avrOutS663;
    _M0Lm2vpS653 = _M0L8_2avpOutS664;
    _M0Lm2vmS654 = _M0L8_2avmOutS665;
    if (_M0L1qS658 <= 21) {
      int32_t _M0L6_2atmpS2712 = (int32_t)_M0L2mvS650;
      uint64_t _M0L6_2atmpS2715 = _M0L2mvS650 / 5ull;
      int32_t _M0L6_2atmpS2714 = (int32_t)_M0L6_2atmpS2715;
      int32_t _M0L6_2atmpS2713 = 5 * _M0L6_2atmpS2714;
      int32_t _M0L6mvMod5S666 = _M0L6_2atmpS2712 - _M0L6_2atmpS2713;
      if (_M0L6mvMod5S666 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS657
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS650, _M0L1qS658);
      } else if (_M0L4evenS649) {
        uint64_t _M0L6_2atmpS2706 = _M0L2mvS650 - 1ull;
        uint64_t _M0L6_2atmpS2707;
        uint64_t _M0L6_2atmpS2705;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2707 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS651);
        _M0L6_2atmpS2705 = _M0L6_2atmpS2706 - _M0L6_2atmpS2707;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS656
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2705, _M0L1qS658);
      } else {
        uint64_t _M0L6_2atmpS2708 = _M0Lm2vpS653;
        uint64_t _M0L6_2atmpS2711 = _M0L2mvS650 + 2ull;
        int32_t _M0L6_2atmpS2710;
        uint64_t _M0L6_2atmpS2709;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2710
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2711, _M0L1qS658);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2709 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2710);
        _M0Lm2vpS653 = _M0L6_2atmpS2708 - _M0L6_2atmpS2709;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2740 = _M0Lm2e2S645;
    int32_t _M0L6_2atmpS2739 = -_M0L6_2atmpS2740;
    int32_t _M0L6_2atmpS2734;
    int32_t _M0L6_2atmpS2738;
    int32_t _M0L6_2atmpS2737;
    int32_t _M0L6_2atmpS2736;
    int32_t _M0L6_2atmpS2735;
    int32_t _M0L1qS667;
    int32_t _M0L6_2atmpS2727;
    int32_t _M0L6_2atmpS2733;
    int32_t _M0L6_2atmpS2732;
    int32_t _M0L1iS668;
    int32_t _M0L6_2atmpS2731;
    int32_t _M0L1kS669;
    int32_t _M0L1jS670;
    struct _M0TPB8Pow5Pair _M0L4pow5S671;
    uint64_t _M0L6_2atmpS2730;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS672;
    uint64_t _M0L8_2avrOutS673;
    uint64_t _M0L8_2avpOutS674;
    uint64_t _M0L8_2avmOutS675;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2734 = _M0FPB9log10Pow5(_M0L6_2atmpS2739);
    _M0L6_2atmpS2738 = _M0Lm2e2S645;
    _M0L6_2atmpS2737 = -_M0L6_2atmpS2738;
    _M0L6_2atmpS2736 = _M0L6_2atmpS2737 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2735 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2736);
    _M0L1qS667 = _M0L6_2atmpS2734 - _M0L6_2atmpS2735;
    _M0L6_2atmpS2727 = _M0Lm2e2S645;
    _M0Lm3e10S655 = _M0L1qS667 + _M0L6_2atmpS2727;
    _M0L6_2atmpS2733 = _M0Lm2e2S645;
    _M0L6_2atmpS2732 = -_M0L6_2atmpS2733;
    _M0L1iS668 = _M0L6_2atmpS2732 - _M0L1qS667;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2731 = _M0FPB8pow5bits(_M0L1iS668);
    _M0L1kS669 = _M0L6_2atmpS2731 - 125;
    _M0L1jS670 = _M0L1qS667 - _M0L1kS669;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S671 = _M0FPB19double__computePow5(_M0L1iS668);
    _M0L6_2atmpS2730 = _M0Lm2m2S646;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS672
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2730, _M0L4pow5S671, _M0L1jS670, _M0L7mmShiftS651);
    _M0L8_2avrOutS673 = _M0L7_2abindS672.$0;
    _M0L8_2avpOutS674 = _M0L7_2abindS672.$1;
    _M0L8_2avmOutS675 = _M0L7_2abindS672.$2;
    _M0Lm2vrS652 = _M0L8_2avrOutS673;
    _M0Lm2vpS653 = _M0L8_2avpOutS674;
    _M0Lm2vmS654 = _M0L8_2avmOutS675;
    if (_M0L1qS667 <= 1) {
      _M0Lm17vrIsTrailingZerosS657 = 1;
      if (_M0L4evenS649) {
        int32_t _M0L6_2atmpS2728;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2728 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS651);
        _M0Lm17vmIsTrailingZerosS656 = _M0L6_2atmpS2728 == 1;
      } else {
        uint64_t _M0L6_2atmpS2729 = _M0Lm2vpS653;
        _M0Lm2vpS653 = _M0L6_2atmpS2729 - 1ull;
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
    int32_t _if__result_6148;
    uint64_t _M0L6_2atmpS2770;
    uint64_t _M0L6_2atmpS2776;
    uint64_t _M0L6_2atmpS2777;
    int32_t _if__result_6149;
    int32_t _M0L6_2atmpS2773;
    int64_t _M0L6_2atmpS2772;
    uint64_t _M0L6_2atmpS2771;
    while (1) {
      uint64_t _M0L6_2atmpS2753 = _M0Lm2vpS653;
      uint64_t _M0L7vpDiv10S679 = _M0L6_2atmpS2753 / 10ull;
      uint64_t _M0L6_2atmpS2752 = _M0Lm2vmS654;
      uint64_t _M0L7vmDiv10S680 = _M0L6_2atmpS2752 / 10ull;
      uint64_t _M0L6_2atmpS2751;
      int32_t _M0L6_2atmpS2748;
      int32_t _M0L6_2atmpS2750;
      int32_t _M0L6_2atmpS2749;
      int32_t _M0L7vmMod10S682;
      uint64_t _M0L6_2atmpS2747;
      uint64_t _M0L7vrDiv10S683;
      uint64_t _M0L6_2atmpS2746;
      int32_t _M0L6_2atmpS2743;
      int32_t _M0L6_2atmpS2745;
      int32_t _M0L6_2atmpS2744;
      int32_t _M0L7vrMod10S684;
      int32_t _M0L6_2atmpS2742;
      if (_M0L7vpDiv10S679 <= _M0L7vmDiv10S680) {
        break;
      }
      _M0L6_2atmpS2751 = _M0Lm2vmS654;
      _M0L6_2atmpS2748 = (int32_t)_M0L6_2atmpS2751;
      _M0L6_2atmpS2750 = (int32_t)_M0L7vmDiv10S680;
      _M0L6_2atmpS2749 = 10 * _M0L6_2atmpS2750;
      _M0L7vmMod10S682 = _M0L6_2atmpS2748 - _M0L6_2atmpS2749;
      _M0L6_2atmpS2747 = _M0Lm2vrS652;
      _M0L7vrDiv10S683 = _M0L6_2atmpS2747 / 10ull;
      _M0L6_2atmpS2746 = _M0Lm2vrS652;
      _M0L6_2atmpS2743 = (int32_t)_M0L6_2atmpS2746;
      _M0L6_2atmpS2745 = (int32_t)_M0L7vrDiv10S683;
      _M0L6_2atmpS2744 = 10 * _M0L6_2atmpS2745;
      _M0L7vrMod10S684 = _M0L6_2atmpS2743 - _M0L6_2atmpS2744;
      _M0Lm17vmIsTrailingZerosS656
      = _M0Lm17vmIsTrailingZerosS656 && _M0L7vmMod10S682 == 0;
      if (_M0Lm17vrIsTrailingZerosS657) {
        int32_t _M0L6_2atmpS2741 = _M0Lm16lastRemovedDigitS677;
        _M0Lm17vrIsTrailingZerosS657 = _M0L6_2atmpS2741 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS657 = 0;
      }
      _M0Lm16lastRemovedDigitS677 = _M0L7vrMod10S684;
      _M0Lm2vrS652 = _M0L7vrDiv10S683;
      _M0Lm2vpS653 = _M0L7vpDiv10S679;
      _M0Lm2vmS654 = _M0L7vmDiv10S680;
      _M0L6_2atmpS2742 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2742 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS656) {
      while (1) {
        uint64_t _M0L6_2atmpS2766 = _M0Lm2vmS654;
        uint64_t _M0L7vmDiv10S685 = _M0L6_2atmpS2766 / 10ull;
        uint64_t _M0L6_2atmpS2765 = _M0Lm2vmS654;
        int32_t _M0L6_2atmpS2762 = (int32_t)_M0L6_2atmpS2765;
        int32_t _M0L6_2atmpS2764 = (int32_t)_M0L7vmDiv10S685;
        int32_t _M0L6_2atmpS2763 = 10 * _M0L6_2atmpS2764;
        int32_t _M0L7vmMod10S686 = _M0L6_2atmpS2762 - _M0L6_2atmpS2763;
        uint64_t _M0L6_2atmpS2761;
        uint64_t _M0L7vpDiv10S688;
        uint64_t _M0L6_2atmpS2760;
        uint64_t _M0L7vrDiv10S689;
        uint64_t _M0L6_2atmpS2759;
        int32_t _M0L6_2atmpS2756;
        int32_t _M0L6_2atmpS2758;
        int32_t _M0L6_2atmpS2757;
        int32_t _M0L7vrMod10S690;
        int32_t _M0L6_2atmpS2755;
        if (_M0L7vmMod10S686 != 0) {
          break;
        }
        _M0L6_2atmpS2761 = _M0Lm2vpS653;
        _M0L7vpDiv10S688 = _M0L6_2atmpS2761 / 10ull;
        _M0L6_2atmpS2760 = _M0Lm2vrS652;
        _M0L7vrDiv10S689 = _M0L6_2atmpS2760 / 10ull;
        _M0L6_2atmpS2759 = _M0Lm2vrS652;
        _M0L6_2atmpS2756 = (int32_t)_M0L6_2atmpS2759;
        _M0L6_2atmpS2758 = (int32_t)_M0L7vrDiv10S689;
        _M0L6_2atmpS2757 = 10 * _M0L6_2atmpS2758;
        _M0L7vrMod10S690 = _M0L6_2atmpS2756 - _M0L6_2atmpS2757;
        if (_M0Lm17vrIsTrailingZerosS657) {
          int32_t _M0L6_2atmpS2754 = _M0Lm16lastRemovedDigitS677;
          _M0Lm17vrIsTrailingZerosS657 = _M0L6_2atmpS2754 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS657 = 0;
        }
        _M0Lm16lastRemovedDigitS677 = _M0L7vrMod10S690;
        _M0Lm2vrS652 = _M0L7vrDiv10S689;
        _M0Lm2vpS653 = _M0L7vpDiv10S688;
        _M0Lm2vmS654 = _M0L7vmDiv10S685;
        _M0L6_2atmpS2755 = _M0Lm7removedS676;
        _M0Lm7removedS676 = _M0L6_2atmpS2755 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS657) {
      int32_t _M0L6_2atmpS2769 = _M0Lm16lastRemovedDigitS677;
      if (_M0L6_2atmpS2769 == 5) {
        uint64_t _M0L6_2atmpS2768 = _M0Lm2vrS652;
        uint64_t _M0L6_2atmpS2767 = _M0L6_2atmpS2768 % 2ull;
        _if__result_6148 = _M0L6_2atmpS2767 == 0ull;
      } else {
        _if__result_6148 = 0;
      }
    } else {
      _if__result_6148 = 0;
    }
    if (_if__result_6148) {
      _M0Lm16lastRemovedDigitS677 = 4;
    }
    _M0L6_2atmpS2770 = _M0Lm2vrS652;
    _M0L6_2atmpS2776 = _M0Lm2vrS652;
    _M0L6_2atmpS2777 = _M0Lm2vmS654;
    if (_M0L6_2atmpS2776 == _M0L6_2atmpS2777) {
      if (!_M0L4evenS649) {
        _if__result_6149 = 1;
      } else {
        int32_t _M0L6_2atmpS2775 = _M0Lm17vmIsTrailingZerosS656;
        _if__result_6149 = !_M0L6_2atmpS2775;
      }
    } else {
      _if__result_6149 = 0;
    }
    if (_if__result_6149) {
      _M0L6_2atmpS2773 = 1;
    } else {
      int32_t _M0L6_2atmpS2774 = _M0Lm16lastRemovedDigitS677;
      _M0L6_2atmpS2773 = _M0L6_2atmpS2774 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2772 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2773);
    _M0L6_2atmpS2771 = *(uint64_t*)&_M0L6_2atmpS2772;
    _M0Lm6outputS678 = _M0L6_2atmpS2770 + _M0L6_2atmpS2771;
  } else {
    int32_t _M0Lm7roundUpS691 = 0;
    uint64_t _M0L6_2atmpS2798 = _M0Lm2vpS653;
    uint64_t _M0L8vpDiv100S692 = _M0L6_2atmpS2798 / 100ull;
    uint64_t _M0L6_2atmpS2797 = _M0Lm2vmS654;
    uint64_t _M0L8vmDiv100S693 = _M0L6_2atmpS2797 / 100ull;
    uint64_t _M0L6_2atmpS2792;
    uint64_t _M0L6_2atmpS2795;
    uint64_t _M0L6_2atmpS2796;
    int32_t _M0L6_2atmpS2794;
    uint64_t _M0L6_2atmpS2793;
    if (_M0L8vpDiv100S692 > _M0L8vmDiv100S693) {
      uint64_t _M0L6_2atmpS2783 = _M0Lm2vrS652;
      uint64_t _M0L8vrDiv100S694 = _M0L6_2atmpS2783 / 100ull;
      uint64_t _M0L6_2atmpS2782 = _M0Lm2vrS652;
      int32_t _M0L6_2atmpS2779 = (int32_t)_M0L6_2atmpS2782;
      int32_t _M0L6_2atmpS2781 = (int32_t)_M0L8vrDiv100S694;
      int32_t _M0L6_2atmpS2780 = 100 * _M0L6_2atmpS2781;
      int32_t _M0L8vrMod100S695 = _M0L6_2atmpS2779 - _M0L6_2atmpS2780;
      int32_t _M0L6_2atmpS2778;
      _M0Lm7roundUpS691 = _M0L8vrMod100S695 >= 50;
      _M0Lm2vrS652 = _M0L8vrDiv100S694;
      _M0Lm2vpS653 = _M0L8vpDiv100S692;
      _M0Lm2vmS654 = _M0L8vmDiv100S693;
      _M0L6_2atmpS2778 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2778 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2791 = _M0Lm2vpS653;
      uint64_t _M0L7vpDiv10S696 = _M0L6_2atmpS2791 / 10ull;
      uint64_t _M0L6_2atmpS2790 = _M0Lm2vmS654;
      uint64_t _M0L7vmDiv10S697 = _M0L6_2atmpS2790 / 10ull;
      uint64_t _M0L6_2atmpS2789;
      uint64_t _M0L7vrDiv10S699;
      uint64_t _M0L6_2atmpS2788;
      int32_t _M0L6_2atmpS2785;
      int32_t _M0L6_2atmpS2787;
      int32_t _M0L6_2atmpS2786;
      int32_t _M0L7vrMod10S700;
      int32_t _M0L6_2atmpS2784;
      if (_M0L7vpDiv10S696 <= _M0L7vmDiv10S697) {
        break;
      }
      _M0L6_2atmpS2789 = _M0Lm2vrS652;
      _M0L7vrDiv10S699 = _M0L6_2atmpS2789 / 10ull;
      _M0L6_2atmpS2788 = _M0Lm2vrS652;
      _M0L6_2atmpS2785 = (int32_t)_M0L6_2atmpS2788;
      _M0L6_2atmpS2787 = (int32_t)_M0L7vrDiv10S699;
      _M0L6_2atmpS2786 = 10 * _M0L6_2atmpS2787;
      _M0L7vrMod10S700 = _M0L6_2atmpS2785 - _M0L6_2atmpS2786;
      _M0Lm7roundUpS691 = _M0L7vrMod10S700 >= 5;
      _M0Lm2vrS652 = _M0L7vrDiv10S699;
      _M0Lm2vpS653 = _M0L7vpDiv10S696;
      _M0Lm2vmS654 = _M0L7vmDiv10S697;
      _M0L6_2atmpS2784 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2784 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2792 = _M0Lm2vrS652;
    _M0L6_2atmpS2795 = _M0Lm2vrS652;
    _M0L6_2atmpS2796 = _M0Lm2vmS654;
    _M0L6_2atmpS2794
    = _M0L6_2atmpS2795 == _M0L6_2atmpS2796 || _M0Lm7roundUpS691;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2793 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2794);
    _M0Lm6outputS678 = _M0L6_2atmpS2792 + _M0L6_2atmpS2793;
  }
  _M0L6_2atmpS2800 = _M0Lm3e10S655;
  _M0L6_2atmpS2801 = _M0Lm7removedS676;
  _M0L3expS701 = _M0L6_2atmpS2800 + _M0L6_2atmpS2801;
  _M0L6_2atmpS2799 = _M0Lm6outputS678;
  _block_6151
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_6151)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6151->$0 = _M0L6_2atmpS2799;
  _block_6151->$1 = _M0L3expS701;
  return _block_6151;
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
  int32_t _M0L6_2atmpS2700;
  int32_t _M0L6_2atmpS2699;
  int32_t _M0L4baseS623;
  int32_t _M0L5base2S625;
  int32_t _M0L6offsetS626;
  int32_t _M0L6_2atmpS2698;
  uint64_t _M0L4mul0S627;
  int32_t _M0L6_2atmpS2697;
  int32_t _M0L6_2atmpS2696;
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
  int32_t _M0L6_2atmpS2694;
  int32_t _M0L6_2atmpS2695;
  int32_t _M0L5deltaS638;
  uint64_t _M0L6_2atmpS2693;
  uint64_t _M0L6_2atmpS2685;
  int32_t _M0L6_2atmpS2692;
  uint32_t _M0L6_2atmpS2689;
  int32_t _M0L6_2atmpS2691;
  int32_t _M0L6_2atmpS2690;
  uint32_t _M0L6_2atmpS2688;
  uint32_t _M0L6_2atmpS2687;
  uint64_t _M0L6_2atmpS2686;
  uint64_t _M0L1aS639;
  uint64_t _M0L6_2atmpS2684;
  uint64_t _M0L1bS640;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2700 = _M0L1iS624 + 26;
  _M0L6_2atmpS2699 = _M0L6_2atmpS2700 - 1;
  _M0L4baseS623 = _M0L6_2atmpS2699 / 26;
  _M0L5base2S625 = _M0L4baseS623 * 26;
  _M0L6offsetS626 = _M0L5base2S625 - _M0L1iS624;
  _M0L6_2atmpS2698 = _M0L4baseS623 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2698);
  _M0L6_2atmpS2697 = _M0L4baseS623 * 2;
  _M0L6_2atmpS2696 = _M0L6_2atmpS2697 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S628
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2696);
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
    uint64_t _M0L6_2atmpS2683 = _M0Lm5high1S637;
    _M0Lm5high1S637 = _M0L6_2atmpS2683 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2694 = _M0FPB8pow5bits(_M0L5base2S625);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2695 = _M0FPB8pow5bits(_M0L1iS624);
  _M0L5deltaS638 = _M0L6_2atmpS2694 - _M0L6_2atmpS2695;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2693
  = _M0FPB13shiftright128(_M0L7_2alow0S634, _M0L3sumS636, _M0L5deltaS638);
  _M0L6_2atmpS2685 = _M0L6_2atmpS2693 + 1ull;
  _M0L6_2atmpS2692 = _M0L1iS624 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2689
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS2692);
  _M0L6_2atmpS2691 = _M0L1iS624 % 16;
  _M0L6_2atmpS2690 = _M0L6_2atmpS2691 << 1;
  _M0L6_2atmpS2688 = _M0L6_2atmpS2689 >> (_M0L6_2atmpS2690 & 31);
  _M0L6_2atmpS2687 = _M0L6_2atmpS2688 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2686 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2687);
  _M0L1aS639 = _M0L6_2atmpS2685 + _M0L6_2atmpS2686;
  _M0L6_2atmpS2684 = _M0Lm5high1S637;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS640
  = _M0FPB13shiftright128(_M0L3sumS636, _M0L6_2atmpS2684, _M0L5deltaS638);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS639, .$1 = _M0L1bS640};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS606) {
  int32_t _M0L4baseS605;
  int32_t _M0L5base2S607;
  int32_t _M0L6offsetS608;
  int32_t _M0L6_2atmpS2682;
  uint64_t _M0L4mul0S609;
  int32_t _M0L6_2atmpS2681;
  int32_t _M0L6_2atmpS2680;
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
  int32_t _M0L6_2atmpS2678;
  int32_t _M0L6_2atmpS2679;
  int32_t _M0L5deltaS620;
  uint64_t _M0L6_2atmpS2670;
  int32_t _M0L6_2atmpS2677;
  uint32_t _M0L6_2atmpS2674;
  int32_t _M0L6_2atmpS2676;
  int32_t _M0L6_2atmpS2675;
  uint32_t _M0L6_2atmpS2673;
  uint32_t _M0L6_2atmpS2672;
  uint64_t _M0L6_2atmpS2671;
  uint64_t _M0L1aS621;
  uint64_t _M0L6_2atmpS2669;
  uint64_t _M0L1bS622;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS605 = _M0L1iS606 / 26;
  _M0L5base2S607 = _M0L4baseS605 * 26;
  _M0L6offsetS608 = _M0L1iS606 - _M0L5base2S607;
  _M0L6_2atmpS2682 = _M0L4baseS605 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2682);
  _M0L6_2atmpS2681 = _M0L4baseS605 * 2;
  _M0L6_2atmpS2680 = _M0L6_2atmpS2681 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S610
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2680);
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
    uint64_t _M0L6_2atmpS2668 = _M0Lm5high1S619;
    _M0Lm5high1S619 = _M0L6_2atmpS2668 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2678 = _M0FPB8pow5bits(_M0L1iS606);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2679 = _M0FPB8pow5bits(_M0L5base2S607);
  _M0L5deltaS620 = _M0L6_2atmpS2678 - _M0L6_2atmpS2679;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2670
  = _M0FPB13shiftright128(_M0L7_2alow0S616, _M0L3sumS618, _M0L5deltaS620);
  _M0L6_2atmpS2677 = _M0L1iS606 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2674
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS2677);
  _M0L6_2atmpS2676 = _M0L1iS606 % 16;
  _M0L6_2atmpS2675 = _M0L6_2atmpS2676 << 1;
  _M0L6_2atmpS2673 = _M0L6_2atmpS2674 >> (_M0L6_2atmpS2675 & 31);
  _M0L6_2atmpS2672 = _M0L6_2atmpS2673 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2671 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2672);
  _M0L1aS621 = _M0L6_2atmpS2670 + _M0L6_2atmpS2671;
  _M0L6_2atmpS2669 = _M0Lm5high1S619;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS622
  = _M0FPB13shiftright128(_M0L3sumS618, _M0L6_2atmpS2669, _M0L5deltaS620);
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
  uint64_t _M0L6_2atmpS2667;
  uint64_t _M0L2hiS587;
  uint64_t _M0L3lo2S588;
  uint64_t _M0L6_2atmpS2665;
  uint64_t _M0L6_2atmpS2666;
  uint64_t _M0L4mid2S589;
  uint64_t _M0L6_2atmpS2664;
  uint64_t _M0L3hi2S590;
  int32_t _M0L6_2atmpS2663;
  int32_t _M0L6_2atmpS2662;
  uint64_t _M0L2vpS591;
  uint64_t _M0Lm2vmS593;
  int32_t _M0L6_2atmpS2661;
  int32_t _M0L6_2atmpS2660;
  uint64_t _M0L2vrS604;
  uint64_t _M0L6_2atmpS2659;
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
    _M0L6_2atmpS2667 = 1ull;
  } else {
    _M0L6_2atmpS2667 = 0ull;
  }
  _M0L2hiS587 = _M0L6_2ahi2S585 + _M0L6_2atmpS2667;
  _M0L3lo2S588 = _M0L5_2aloS581 + _M0L7_2amul0S575;
  _M0L6_2atmpS2665 = _M0L3midS586 + _M0L7_2amul1S577;
  if (_M0L3lo2S588 < _M0L5_2aloS581) {
    _M0L6_2atmpS2666 = 1ull;
  } else {
    _M0L6_2atmpS2666 = 0ull;
  }
  _M0L4mid2S589 = _M0L6_2atmpS2665 + _M0L6_2atmpS2666;
  if (_M0L4mid2S589 < _M0L3midS586) {
    _M0L6_2atmpS2664 = 1ull;
  } else {
    _M0L6_2atmpS2664 = 0ull;
  }
  _M0L3hi2S590 = _M0L2hiS587 + _M0L6_2atmpS2664;
  _M0L6_2atmpS2663 = _M0L1jS592 - 64;
  _M0L6_2atmpS2662 = _M0L6_2atmpS2663 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS591
  = _M0FPB13shiftright128(_M0L4mid2S589, _M0L3hi2S590, _M0L6_2atmpS2662);
  _M0Lm2vmS593 = 0ull;
  if (_M0L7mmShiftS594) {
    uint64_t _M0L3lo3S595 = _M0L5_2aloS581 - _M0L7_2amul0S575;
    uint64_t _M0L6_2atmpS2649 = _M0L3midS586 - _M0L7_2amul1S577;
    uint64_t _M0L6_2atmpS2650;
    uint64_t _M0L4mid3S596;
    uint64_t _M0L6_2atmpS2648;
    uint64_t _M0L3hi3S597;
    int32_t _M0L6_2atmpS2647;
    int32_t _M0L6_2atmpS2646;
    if (_M0L5_2aloS581 < _M0L3lo3S595) {
      _M0L6_2atmpS2650 = 1ull;
    } else {
      _M0L6_2atmpS2650 = 0ull;
    }
    _M0L4mid3S596 = _M0L6_2atmpS2649 - _M0L6_2atmpS2650;
    if (_M0L3midS586 < _M0L4mid3S596) {
      _M0L6_2atmpS2648 = 1ull;
    } else {
      _M0L6_2atmpS2648 = 0ull;
    }
    _M0L3hi3S597 = _M0L2hiS587 - _M0L6_2atmpS2648;
    _M0L6_2atmpS2647 = _M0L1jS592 - 64;
    _M0L6_2atmpS2646 = _M0L6_2atmpS2647 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS593
    = _M0FPB13shiftright128(_M0L4mid3S596, _M0L3hi3S597, _M0L6_2atmpS2646);
  } else {
    uint64_t _M0L3lo3S598 = _M0L5_2aloS581 + _M0L5_2aloS581;
    uint64_t _M0L6_2atmpS2657 = _M0L3midS586 + _M0L3midS586;
    uint64_t _M0L6_2atmpS2658;
    uint64_t _M0L4mid3S599;
    uint64_t _M0L6_2atmpS2655;
    uint64_t _M0L6_2atmpS2656;
    uint64_t _M0L3hi3S600;
    uint64_t _M0L3lo4S601;
    uint64_t _M0L6_2atmpS2653;
    uint64_t _M0L6_2atmpS2654;
    uint64_t _M0L4mid4S602;
    uint64_t _M0L6_2atmpS2652;
    uint64_t _M0L3hi4S603;
    int32_t _M0L6_2atmpS2651;
    if (_M0L3lo3S598 < _M0L5_2aloS581) {
      _M0L6_2atmpS2658 = 1ull;
    } else {
      _M0L6_2atmpS2658 = 0ull;
    }
    _M0L4mid3S599 = _M0L6_2atmpS2657 + _M0L6_2atmpS2658;
    _M0L6_2atmpS2655 = _M0L2hiS587 + _M0L2hiS587;
    if (_M0L4mid3S599 < _M0L3midS586) {
      _M0L6_2atmpS2656 = 1ull;
    } else {
      _M0L6_2atmpS2656 = 0ull;
    }
    _M0L3hi3S600 = _M0L6_2atmpS2655 + _M0L6_2atmpS2656;
    _M0L3lo4S601 = _M0L3lo3S598 - _M0L7_2amul0S575;
    _M0L6_2atmpS2653 = _M0L4mid3S599 - _M0L7_2amul1S577;
    if (_M0L3lo3S598 < _M0L3lo4S601) {
      _M0L6_2atmpS2654 = 1ull;
    } else {
      _M0L6_2atmpS2654 = 0ull;
    }
    _M0L4mid4S602 = _M0L6_2atmpS2653 - _M0L6_2atmpS2654;
    if (_M0L4mid3S599 < _M0L4mid4S602) {
      _M0L6_2atmpS2652 = 1ull;
    } else {
      _M0L6_2atmpS2652 = 0ull;
    }
    _M0L3hi4S603 = _M0L3hi3S600 - _M0L6_2atmpS2652;
    _M0L6_2atmpS2651 = _M0L1jS592 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS593
    = _M0FPB13shiftright128(_M0L4mid4S602, _M0L3hi4S603, _M0L6_2atmpS2651);
  }
  _M0L6_2atmpS2661 = _M0L1jS592 - 64;
  _M0L6_2atmpS2660 = _M0L6_2atmpS2661 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS604
  = _M0FPB13shiftright128(_M0L3midS586, _M0L2hiS587, _M0L6_2atmpS2660);
  _M0L6_2atmpS2659 = _M0Lm2vmS593;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS604,
                                                .$1 = _M0L2vpS591,
                                                .$2 = _M0L6_2atmpS2659};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS573,
  int32_t _M0L1pS574
) {
  uint64_t _M0L6_2atmpS2645;
  uint64_t _M0L6_2atmpS2644;
  uint64_t _M0L6_2atmpS2643;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2645 = 1ull << (_M0L1pS574 & 63);
  _M0L6_2atmpS2644 = _M0L6_2atmpS2645 - 1ull;
  _M0L6_2atmpS2643 = _M0L5valueS573 & _M0L6_2atmpS2644;
  return _M0L6_2atmpS2643 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS571,
  int32_t _M0L1pS572
) {
  int32_t _M0L6_2atmpS2642;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2642 = _M0FPB10pow5Factor(_M0L5valueS571);
  return _M0L6_2atmpS2642 >= _M0L1pS572;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS566) {
  uint64_t _M0L6_2atmpS2633;
  uint64_t _M0L6_2atmpS2634;
  uint64_t _M0L6_2atmpS2635;
  uint64_t _M0L6_2atmpS2636;
  uint64_t _M0L6_2atmpS2641;
  int32_t _M0L5countS567;
  uint64_t _M0L1vS568;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2633 = _M0L5valueS566 % 5ull;
  if (_M0L6_2atmpS2633 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2634 = _M0L5valueS566 % 25ull;
  if (_M0L6_2atmpS2634 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS2635 = _M0L5valueS566 % 125ull;
  if (_M0L6_2atmpS2635 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS2636 = _M0L5valueS566 % 625ull;
  if (_M0L6_2atmpS2636 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS2641 = _M0L5valueS566 / 625ull;
  _M0L5countS567 = 4;
  _M0L1vS568 = _M0L6_2atmpS2641;
  while (1) {
    if (_M0L1vS568 > 0ull) {
      uint64_t _M0L6_2atmpS2637 = _M0L1vS568 % 5ull;
      int32_t _M0L6_2atmpS2638;
      uint64_t _M0L6_2atmpS2639;
      if (_M0L6_2atmpS2637 != 0ull) {
        return _M0L5countS567;
      }
      _M0L6_2atmpS2638 = _M0L5countS567 + 1;
      _M0L6_2atmpS2639 = _M0L1vS568 / 5ull;
      _M0L5countS567 = _M0L6_2atmpS2638;
      _M0L1vS568 = _M0L6_2atmpS2639;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS570;
      moonbit_string_t _M0L6_2atmpS2640;
      int32_t _result_6153;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS570
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS570, (moonbit_string_t)moonbit_string_literal_13.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS570, _M0L5valueS566);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS2640
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS570);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS570);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_6153 = _M0FPC15abort5abortGiE(_M0L6_2atmpS2640);
      moonbit_decref_cycle_free(_M0L6_2atmpS2640);
      return _result_6153;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS565,
  uint64_t _M0L2hiS563,
  int32_t _M0L4distS564
) {
  int32_t _M0L6_2atmpS2632;
  uint64_t _M0L6_2atmpS2630;
  uint64_t _M0L6_2atmpS2631;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2632 = 64 - _M0L4distS564;
  _M0L6_2atmpS2630 = _M0L2hiS563 << (_M0L6_2atmpS2632 & 63);
  _M0L6_2atmpS2631 = _M0L2loS565 >> (_M0L4distS564 & 63);
  return _M0L6_2atmpS2630 | _M0L6_2atmpS2631;
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
  uint64_t _M0L6_2atmpS2628;
  uint64_t _M0L6_2atmpS2629;
  uint64_t _M0L1yS559;
  uint64_t _M0L6_2atmpS2626;
  uint64_t _M0L6_2atmpS2627;
  uint64_t _M0L1zS560;
  uint64_t _M0L6_2atmpS2624;
  uint64_t _M0L6_2atmpS2625;
  uint64_t _M0L6_2atmpS2622;
  uint64_t _M0L6_2atmpS2623;
  uint64_t _M0L1wS561;
  uint64_t _M0L2loS562;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS552 = _M0L1aS553 & 4294967295ull;
  _M0L3aHiS554 = _M0L1aS553 >> 32;
  _M0L3bLoS555 = _M0L1bS556 & 4294967295ull;
  _M0L3bHiS557 = _M0L1bS556 >> 32;
  _M0L1xS558 = _M0L3aLoS552 * _M0L3bLoS555;
  _M0L6_2atmpS2628 = _M0L3aHiS554 * _M0L3bLoS555;
  _M0L6_2atmpS2629 = _M0L1xS558 >> 32;
  _M0L1yS559 = _M0L6_2atmpS2628 + _M0L6_2atmpS2629;
  _M0L6_2atmpS2626 = _M0L3aLoS552 * _M0L3bHiS557;
  _M0L6_2atmpS2627 = _M0L1yS559 & 4294967295ull;
  _M0L1zS560 = _M0L6_2atmpS2626 + _M0L6_2atmpS2627;
  _M0L6_2atmpS2624 = _M0L3aHiS554 * _M0L3bHiS557;
  _M0L6_2atmpS2625 = _M0L1yS559 >> 32;
  _M0L6_2atmpS2622 = _M0L6_2atmpS2624 + _M0L6_2atmpS2625;
  _M0L6_2atmpS2623 = _M0L1zS560 >> 32;
  _M0L1wS561 = _M0L6_2atmpS2622 + _M0L6_2atmpS2623;
  _M0L2loS562 = _M0L1aS553 * _M0L1bS556;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS562, .$1 = _M0L1wS561};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS550,
  int32_t _M0L4fromS547,
  int32_t _M0L2toS546
) {
  int32_t _M0L3lenS545;
  int32_t _M0L6_2atmpS2621;
  uint16_t* _M0L6bufferS548;
  int32_t _M0L1iS549;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS545 = _M0L2toS546 - _M0L4fromS547;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2621 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS548
  = (uint16_t*)moonbit_make_string(_M0L3lenS545, _M0L6_2atmpS2621);
  _M0L1iS549 = 0;
  while (1) {
    if (_M0L1iS549 < _M0L3lenS545) {
      int32_t _M0L6_2atmpS2619 = _M0L4fromS547 + _M0L1iS549;
      int32_t _M0L6_2atmpS2618;
      int32_t _M0L6_2atmpS2617;
      int32_t _M0L6_2atmpS2620;
      if (
        _M0L6_2atmpS2619 < 0
        || _M0L6_2atmpS2619 >= Moonbit_array_length(_M0L5bytesS550)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2618 = (int32_t)_M0L5bytesS550[_M0L6_2atmpS2619];
      _M0L6_2atmpS2617 = (uint16_t)_M0L6_2atmpS2618;
      if (
        _M0L1iS549 < 0 || _M0L1iS549 >= Moonbit_array_length(_M0L6bufferS548)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS548[_M0L1iS549] = _M0L6_2atmpS2617;
      _M0L6_2atmpS2620 = _M0L1iS549 + 1;
      _M0L1iS549 = _M0L6_2atmpS2620;
      continue;
    }
    break;
  }
  return _M0L6bufferS548;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS544) {
  int32_t _M0L6_2atmpS2616;
  uint32_t _M0L6_2atmpS2615;
  uint32_t _M0L6_2atmpS2614;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2616 = _M0L1eS544 * 78913;
  _M0L6_2atmpS2615 = *(uint32_t*)&_M0L6_2atmpS2616;
  _M0L6_2atmpS2614 = _M0L6_2atmpS2615 >> 18;
  return *(int32_t*)&_M0L6_2atmpS2614;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS543) {
  int32_t _M0L6_2atmpS2613;
  uint32_t _M0L6_2atmpS2612;
  uint32_t _M0L6_2atmpS2611;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2613 = _M0L1eS543 * 732923;
  _M0L6_2atmpS2612 = *(uint32_t*)&_M0L6_2atmpS2613;
  _M0L6_2atmpS2611 = _M0L6_2atmpS2612 >> 20;
  return *(int32_t*)&_M0L6_2atmpS2611;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS541,
  int32_t _M0L8exponentS542,
  int32_t _M0L8mantissaS539
) {
  moonbit_string_t _M0L1sS540;
  moonbit_string_t _result_6156;
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
    moonbit_string_t _result_6155;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6155
    = moonbit_add_string(_M0L1sS540, (moonbit_string_t)moonbit_string_literal_16.data);
    moonbit_decref_cycle_free(_M0L1sS540);
    return _result_6155;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_6156
  = moonbit_add_string(_M0L1sS540, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref_cycle_free(_M0L1sS540);
  return _result_6156;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS538) {
  int32_t _M0L6_2atmpS2610;
  uint32_t _M0L6_2atmpS2609;
  uint32_t _M0L6_2atmpS2608;
  int32_t _M0L6_2atmpS2607;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2610 = _M0L1eS538 * 1217359;
  _M0L6_2atmpS2609 = *(uint32_t*)&_M0L6_2atmpS2610;
  _M0L6_2atmpS2608 = _M0L6_2atmpS2609 >> 19;
  _M0L6_2atmpS2607 = *(int32_t*)&_M0L6_2atmpS2608;
  return _M0L6_2atmpS2607 + 1;
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
  float* _M0L6_2atmpS2603;
  struct _M0TPB5ArrayGfE* _block_6157;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2603 = (float*)moonbit_make_float_array_raw(_M0L3lenS532);
  _block_6157
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_6157)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_6157->$0 = _M0L6_2atmpS2603;
  _block_6157->$1 = _M0L3lenS532;
  return _block_6157;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS533
) {
  uint8_t* _M0L6_2atmpS2604;
  struct _M0TPB5ArrayGbE* _block_6158;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2604 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS533);
  _block_6158
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_6158)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 91, 0);
  _block_6158->$0 = _M0L6_2atmpS2604;
  _block_6158->$1 = _M0L3lenS533;
  return _block_6158;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS534
) {
  int32_t* _M0L6_2atmpS2605;
  struct _M0TPB5ArrayGiE* _block_6159;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2605 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS534);
  _block_6159
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_6159)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_6159->$0 = _M0L6_2atmpS2605;
  _block_6159->$1 = _M0L3lenS534;
  return _block_6159;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS535
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS2606;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_6160;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2606
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS535, 0);
  _block_6160
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_6160)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 114, 0);
  _block_6160->$0 = _M0L6_2atmpS2606;
  _block_6160->$1 = _M0L3lenS535;
  return _block_6160;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS528,
  int32_t _M0L5indexS529
) {
  uint64_t* _M0L6_2atmpS2601;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2601 = _M0L4selfS528;
  if (
    _M0L5indexS529 < 0
    || _M0L5indexS529 >= Moonbit_array_length(_M0L6_2atmpS2601)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS2601[_M0L5indexS529];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS530,
  int32_t _M0L5indexS531
) {
  uint32_t* _M0L6_2atmpS2602;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2602 = _M0L4selfS530;
  if (
    _M0L5indexS531 < 0
    || _M0L5indexS531 >= Moonbit_array_length(_M0L6_2atmpS2602)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS2602[_M0L5indexS531];
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
  int32_t _M0L3lenS2573;
  int32_t* _M0L6_2atmpS2575;
  int32_t _M0L6_2atmpS2574;
  int32_t _M0L6lengthS514;
  int32_t* _M0L3bufS2578;
  int32_t _M0L6_2atmpS2579;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2573 = _M0L4selfS513->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2575 = _M0MPC15array5Array6bufferGiE(_M0L4selfS513);
  _M0L6_2atmpS2574 = Moonbit_array_length(_M0L6_2atmpS2575);
  moonbit_decref_cycle_free(_M0L6_2atmpS2575);
  if (_M0L3lenS2573 == _M0L6_2atmpS2574) {
    int32_t _M0L3lenS2577 = _M0L4selfS513->$1;
    int32_t _M0L6_2atmpS2576 = _M0L3lenS2577 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS513, _M0L6_2atmpS2576);
  }
  _M0L6lengthS514 = _M0L4selfS513->$1;
  _M0L3bufS2578 = _M0L4selfS513->$0;
  _M0L3bufS2578[_M0L6lengthS514] = _M0L5valueS515;
  _M0L6_2atmpS2579 = _M0L6lengthS514 + 1;
  _M0L4selfS513->$1 = _M0L6_2atmpS2579;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS516,
  moonbit_string_t _M0L5valueS518
) {
  int32_t _M0L3lenS2580;
  moonbit_string_t* _M0L6_2atmpS2582;
  int32_t _M0L6_2atmpS2581;
  int32_t _M0L6lengthS517;
  moonbit_string_t* _M0L3bufS2585;
  moonbit_string_t _M0L6_2aoldS5707;
  int32_t _M0L6_2atmpS2586;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2580 = _M0L4selfS516->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2582 = _M0MPC15array5Array6bufferGsE(_M0L4selfS516);
  _M0L6_2atmpS2581 = Moonbit_array_length(_M0L6_2atmpS2582);
  moonbit_decref_cycle_free(_M0L6_2atmpS2582);
  if (_M0L3lenS2580 == _M0L6_2atmpS2581) {
    int32_t _M0L3lenS2584 = _M0L4selfS516->$1;
    int32_t _M0L6_2atmpS2583 = _M0L3lenS2584 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS516, _M0L6_2atmpS2583);
  }
  _M0L6lengthS517 = _M0L4selfS516->$1;
  _M0L3bufS2585 = _M0L4selfS516->$0;
  _M0L6_2aoldS5707 = (moonbit_string_t)_M0L3bufS2585[_M0L6lengthS517];
  moonbit_decref_cycle_free(_M0L6_2aoldS5707);
  _M0L3bufS2585[_M0L6lengthS517] = _M0L5valueS518;
  _M0L6_2atmpS2586 = _M0L6lengthS517 + 1;
  _M0L4selfS516->$1 = _M0L6_2atmpS2586;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS519,
  struct _M0TUsiE* _M0L5valueS521
) {
  int32_t _M0L3lenS2587;
  struct _M0TUsiE** _M0L6_2atmpS2589;
  int32_t _M0L6_2atmpS2588;
  int32_t _M0L6lengthS520;
  struct _M0TUsiE** _M0L3bufS2592;
  struct _M0TUsiE* _M0L6_2aoldS5708;
  int32_t _M0L6_2atmpS2593;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2587 = _M0L4selfS519->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2589 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS519);
  _M0L6_2atmpS2588 = Moonbit_array_length(_M0L6_2atmpS2589);
  moonbit_decref_cycle_free(_M0L6_2atmpS2589);
  if (_M0L3lenS2587 == _M0L6_2atmpS2588) {
    int32_t _M0L3lenS2591 = _M0L4selfS519->$1;
    int32_t _M0L6_2atmpS2590 = _M0L3lenS2591 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS519, _M0L6_2atmpS2590);
  }
  _M0L6lengthS520 = _M0L4selfS519->$1;
  _M0L3bufS2592 = _M0L4selfS519->$0;
  _M0L6_2aoldS5708 = (struct _M0TUsiE*)_M0L3bufS2592[_M0L6lengthS520];
  if (_M0L6_2aoldS5708) {
    moonbit_decref_cycle_free(_M0L6_2aoldS5708);
  }
  _M0L3bufS2592[_M0L6lengthS520] = _M0L5valueS521;
  _M0L6_2atmpS2593 = _M0L6lengthS520 + 1;
  _M0L4selfS519->$1 = _M0L6_2atmpS2593;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS522,
  float _M0L5valueS524
) {
  int32_t _M0L3lenS2594;
  float* _M0L6_2atmpS2596;
  int32_t _M0L6_2atmpS2595;
  int32_t _M0L6lengthS523;
  float* _M0L3bufS2599;
  int32_t _M0L6_2atmpS2600;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2594 = _M0L4selfS522->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2596 = _M0MPC15array5Array6bufferGfE(_M0L4selfS522);
  _M0L6_2atmpS2595 = Moonbit_array_length(_M0L6_2atmpS2596);
  moonbit_decref_cycle_free(_M0L6_2atmpS2596);
  if (_M0L3lenS2594 == _M0L6_2atmpS2595) {
    int32_t _M0L3lenS2598 = _M0L4selfS522->$1;
    int32_t _M0L6_2atmpS2597 = _M0L3lenS2598 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS522, _M0L6_2atmpS2597);
  }
  _M0L6lengthS523 = _M0L4selfS522->$1;
  _M0L3bufS2599 = _M0L4selfS522->$0;
  _M0L3bufS2599[_M0L6lengthS523] = _M0L5valueS524;
  _M0L6_2atmpS2600 = _M0L6lengthS523 + 1;
  _M0L4selfS522->$1 = _M0L6_2atmpS2600;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS498,
  int32_t _M0L8requiredS500
) {
  int32_t _M0L8old__capS497;
  int32_t _M0L3lenS2569;
  int32_t _M0L8new__capS499;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS497 = _M0MPC15array5Array8capacityGiE(_M0L4selfS498);
  _M0L3lenS2569 = _M0L4selfS498->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS499
  = _M0FPB23array__growth__capacity(_M0L8old__capS497, _M0L3lenS2569, _M0L8requiredS500);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS498, _M0L8new__capS499);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS502,
  int32_t _M0L8requiredS504
) {
  int32_t _M0L8old__capS501;
  int32_t _M0L3lenS2570;
  int32_t _M0L8new__capS503;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS501 = _M0MPC15array5Array8capacityGsE(_M0L4selfS502);
  _M0L3lenS2570 = _M0L4selfS502->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS503
  = _M0FPB23array__growth__capacity(_M0L8old__capS501, _M0L3lenS2570, _M0L8requiredS504);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS502, _M0L8new__capS503);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS506,
  int32_t _M0L8requiredS508
) {
  int32_t _M0L8old__capS505;
  int32_t _M0L3lenS2571;
  int32_t _M0L8new__capS507;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS505 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS506);
  _M0L3lenS2571 = _M0L4selfS506->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS507
  = _M0FPB23array__growth__capacity(_M0L8old__capS505, _M0L3lenS2571, _M0L8requiredS508);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS506, _M0L8new__capS507);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS510,
  int32_t _M0L8requiredS512
) {
  int32_t _M0L8old__capS509;
  int32_t _M0L3lenS2572;
  int32_t _M0L8new__capS511;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS509 = _M0MPC15array5Array8capacityGfE(_M0L4selfS510);
  _M0L3lenS2572 = _M0L4selfS510->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS511
  = _M0FPB23array__growth__capacity(_M0L8old__capS509, _M0L3lenS2572, _M0L8requiredS512);
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
  int32_t* _M0L6_2aoldS5709;
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
  _M0L6_2aoldS5709 = _M0L4selfS474->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5709);
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
  moonbit_string_t* _M0L6_2aoldS5710;
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
  _M0L6_2aoldS5710 = _M0L4selfS480->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5710);
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
  struct _M0TUsiE** _M0L6_2aoldS5711;
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
  _M0L6_2aoldS5711 = _M0L4selfS486->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5711);
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
  float* _M0L6_2aoldS5712;
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
  _M0L6_2aoldS5712 = _M0L4selfS492->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5712);
  _M0L4selfS492->$0 = _M0L8new__bufS496;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS469
) {
  int32_t* _M0L6_2atmpS2565;
  int32_t _result_6161;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2565 = _M0MPC15array5Array6bufferGiE(_M0L4selfS469);
  _result_6161 = Moonbit_array_length(_M0L6_2atmpS2565);
  moonbit_decref_cycle_free(_M0L6_2atmpS2565);
  return _result_6161;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS470
) {
  moonbit_string_t* _M0L6_2atmpS2566;
  int32_t _result_6162;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2566 = _M0MPC15array5Array6bufferGsE(_M0L4selfS470);
  _result_6162 = Moonbit_array_length(_M0L6_2atmpS2566);
  moonbit_decref_cycle_free(_M0L6_2atmpS2566);
  return _result_6162;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS471
) {
  struct _M0TUsiE** _M0L6_2atmpS2567;
  int32_t _result_6163;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2567 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS471);
  _result_6163 = Moonbit_array_length(_M0L6_2atmpS2567);
  moonbit_decref_cycle_free(_M0L6_2atmpS2567);
  return _result_6163;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS472
) {
  float* _M0L6_2atmpS2568;
  int32_t _result_6164;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2568 = _M0MPC15array5Array6bufferGfE(_M0L4selfS472);
  _result_6164 = Moonbit_array_length(_M0L6_2atmpS2568);
  moonbit_decref_cycle_free(_M0L6_2atmpS2568);
  return _result_6164;
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
  float* _M0L8_2afieldS5713;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5713 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5713);
  return _M0L8_2afieldS5713;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS453) {
  int32_t* _M0L8_2afieldS5714;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5714 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5714);
  return _M0L8_2afieldS5714;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS454
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L8_2afieldS5715;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5715 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5715);
  return _M0L8_2afieldS5715;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS5716;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5716 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5716);
  return _M0L8_2afieldS5716;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS5717;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5717 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5717);
  return _M0L8_2afieldS5717;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS457) {
  uint8_t* _M0L8_2afieldS5718;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5718 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5718);
  return _M0L8_2afieldS5718;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS458
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS5719;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5719 = _M0L4selfS458->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5719);
  return _M0L8_2afieldS5719;
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
  int32_t _M0L3endS2563;
  int32_t _M0L5startS2564;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS2562;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS2555;
  int32_t _M0L6_2atmpS2554;
  int32_t _if__result_6166;
  uint16_t* _M0L4dataS2556;
  int32_t _M0L3lenS2557;
  moonbit_string_t _M0L6_2atmpS2558;
  int32_t _M0L6_2atmpS2559;
  int32_t _M0L3lenS2561;
  int32_t _M0L6_2atmpS2560;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS2563 = _M0L3strS448.$2;
  _M0L5startS2564 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS2563 - _M0L5startS2564;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS2562 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS2562 + _M0L8str__lenS447;
  _M0L4dataS2555 = _M0L4selfS450->$0;
  _M0L6_2atmpS2554 = Moonbit_array_length(_M0L4dataS2555);
  if (_M0L8requiredS449 > _M0L6_2atmpS2554) {
    _if__result_6166 = 1;
  } else {
    int32_t _M0L3lenS2553 = _M0L4selfS450->$1;
    _if__result_6166 = _M0L8requiredS449 < _M0L3lenS2553;
  }
  if (_if__result_6166) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS2556 = _M0L4selfS450->$0;
  _M0L3lenS2557 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS2556);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2558 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2559 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2556, _M0L3lenS2557, _M0L6_2atmpS2558, _M0L6_2atmpS2559, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS2556);
  moonbit_decref_cycle_free(_M0L6_2atmpS2558);
  _M0L3lenS2561 = _M0L4selfS450->$1;
  _M0L6_2atmpS2560 = _M0L3lenS2561 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS2560;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_6167;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS2552;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS2551;
  moonbit_string_t _result_6168;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS2550 = Moonbit_array_length(_M0L3strS444);
    _if__result_6167 = _M0L3endS443 == _M0L6_2atmpS2550;
  } else {
    _if__result_6167 = 0;
  }
  if (_if__result_6167) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS2552 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS2552, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS2551 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_6168
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS2551, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS2551);
  return _result_6168;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_6169;
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
      int32_t _M0L6_2atmpS2549 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_6169 = _M0L6_2atmpS2549 <= _M0L3lenS436;
    } else {
      _if__result_6169 = 0;
    }
  } else {
    _if__result_6169 = 0;
  }
  if (_if__result_6169) {
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
  int32_t _M0L6_2atmpS2548;
  int32_t _M0L6_2atmpS2547;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS2546;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS2548 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS2547 = _M0L13bytes__offsetS423 + _M0L6_2atmpS2548;
  _M0L2e1S422 = _M0L6_2atmpS2547 - 1;
  _M0L6_2atmpS2546 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS2546 - 1;
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
        int32_t _M0L6_2atmpS2543 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS2542 = (int32_t)_M0L6_2atmpS2543;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS2542;
        uint32_t _M0L6_2atmpS2538 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS2537;
        int32_t _M0L6_2atmpS2539;
        uint32_t _M0L6_2atmpS2541;
        int32_t _M0L6_2atmpS2540;
        int32_t _M0L6_2atmpS2544;
        int32_t _M0L6_2atmpS2545;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2537 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2538);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS2537;
        _M0L6_2atmpS2539 = _M0L1jS433 + 1;
        _M0L6_2atmpS2541 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2540 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2541);
        if (
          _M0L6_2atmpS2539 < 0
          || _M0L6_2atmpS2539 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS2539] = _M0L6_2atmpS2540;
        _M0L6_2atmpS2544 = _M0L1iS432 + 1;
        _M0L6_2atmpS2545 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS2544;
        _M0L1jS433 = _M0L6_2atmpS2545;
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
  int32_t _M0L6_2atmpS2536;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2536 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS2536 & 0xff;
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
    int64_t _M0L6_2atmpS2535 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS2535;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS2532;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2532 = 1;
      } else {
        _M0L6_2atmpS2532 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS2532;
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
      int32_t _M0L6_2atmpS2533;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2533 = 1;
      } else {
        _M0L6_2atmpS2533 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS2533;
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
      int32_t _M0L6_2atmpS2534;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2534 = 1;
      } else {
        _M0L6_2atmpS2534 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS2534;
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
  int32_t _M0L6_2atmpS2531;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2531 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS2531;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS2508 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS2508;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS2507 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS2506 = 48 + _M0L6_2atmpS2507;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS2506;
      int32_t _M0L6_2atmpS2505 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS2504 = 48 + _M0L6_2atmpS2505;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS2504;
      int32_t _M0L6_2atmpS2503 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS2502 = 48 + _M0L6_2atmpS2503;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS2502;
      int32_t _M0L6_2atmpS2501 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS2500 = 48 + _M0L6_2atmpS2501;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS2500;
      int32_t _M0L6_2atmpS2492 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS2491 = _M0L6_2atmpS2492 - 4;
      int32_t _M0L6_2atmpS2494;
      int32_t _M0L6_2atmpS2493;
      int32_t _M0L6_2atmpS2496;
      int32_t _M0L6_2atmpS2495;
      int32_t _M0L6_2atmpS2498;
      int32_t _M0L6_2atmpS2497;
      int32_t _M0L6_2atmpS2499;
      _M0L6bufferS381[_M0L6_2atmpS2491] = _M0L6d1__hiS377;
      _M0L6_2atmpS2494 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2493 = _M0L6_2atmpS2494 - 3;
      _M0L6bufferS381[_M0L6_2atmpS2493] = _M0L6d1__loS378;
      _M0L6_2atmpS2496 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2495 = _M0L6_2atmpS2496 - 2;
      _M0L6bufferS381[_M0L6_2atmpS2495] = _M0L6d2__hiS379;
      _M0L6_2atmpS2498 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2497 = _M0L6_2atmpS2498 - 1;
      _M0L6bufferS381[_M0L6_2atmpS2497] = _M0L6d2__loS380;
      _M0L6_2atmpS2499 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS2499;
      continue;
    } else {
      int32_t _M0L6_2atmpS2530 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS2530;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS2517 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS2516 = 48 + _M0L6_2atmpS2517;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS2516;
          int32_t _M0L6_2atmpS2515 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS2514 = 48 + _M0L6_2atmpS2515;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS2514;
          int32_t _M0L6_2atmpS2510 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2509 = _M0L6_2atmpS2510 - 2;
          int32_t _M0L6_2atmpS2512;
          int32_t _M0L6_2atmpS2511;
          int32_t _M0L6_2atmpS2513;
          _M0L6bufferS381[_M0L6_2atmpS2509] = _M0L5d__hiS388;
          _M0L6_2atmpS2512 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2511 = _M0L6_2atmpS2512 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2511] = _M0L5d__loS389;
          _M0L6_2atmpS2513 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS2513;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS2525 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS2524 = 48 + _M0L6_2atmpS2525;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS2524;
          int32_t _M0L6_2atmpS2523 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS2522 = 48 + _M0L6_2atmpS2523;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS2522;
          int32_t _M0L6_2atmpS2519 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2518 = _M0L6_2atmpS2519 - 2;
          int32_t _M0L6_2atmpS2521;
          int32_t _M0L6_2atmpS2520;
          _M0L6bufferS381[_M0L6_2atmpS2518] = _M0L5d__hiS391;
          _M0L6_2atmpS2521 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2520 = _M0L6_2atmpS2521 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2520] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS2529 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2526 = _M0L6_2atmpS2529 - 1;
          int32_t _M0L6_2atmpS2528 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS2527 = (uint16_t)_M0L6_2atmpS2528;
          _M0L6bufferS381[_M0L6_2atmpS2526] = _M0L6_2atmpS2527;
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
  int32_t _M0L6_2atmpS2476;
  int32_t _M0L6_2atmpS2475;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS2476 = _M0L5radixS355 - 1;
  _M0L6_2atmpS2475 = _M0L5radixS355 & _M0L6_2atmpS2476;
  if (_M0L6_2atmpS2475 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS2483;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS2483 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS2483;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS2482 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS2482;
        int32_t _M0L6_2atmpS2479 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS2477 = _M0L6_2atmpS2479 - 1;
        int32_t _M0L6_2atmpS2478 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS2480;
        uint64_t _M0L6_2atmpS2481;
        _M0L6bufferS361[_M0L6_2atmpS2477] = _M0L6_2atmpS2478;
        _M0L6_2atmpS2480 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS2481 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS2480;
        _M0L1nS359 = _M0L6_2atmpS2481;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2490 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS2490;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS2489 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS2488 = _M0L1nS367 - _M0L6_2atmpS2489;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS2488;
        int32_t _M0L6_2atmpS2486 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS2484 = _M0L6_2atmpS2486 - 1;
        int32_t _M0L6_2atmpS2485 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS2487;
        _M0L6bufferS361[_M0L6_2atmpS2484] = _M0L6_2atmpS2485;
        _M0L6_2atmpS2487 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS2487;
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
  int32_t _M0L6_2atmpS2474;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2474 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS2474;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS2471 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS2471;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS2465 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS2463 = _M0L6_2atmpS2465 - 2;
      int32_t _M0L6_2atmpS2464 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS2468;
      int32_t _M0L6_2atmpS2466;
      int32_t _M0L6_2atmpS2467;
      int32_t _M0L6_2atmpS2469;
      uint64_t _M0L6_2atmpS2470;
      _M0L6bufferS348[_M0L6_2atmpS2463] = _M0L6_2atmpS2464;
      _M0L6_2atmpS2468 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS2466 = _M0L6_2atmpS2468 - 1;
      _M0L6_2atmpS2467
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS2466] = _M0L6_2atmpS2467;
      _M0L6_2atmpS2469 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS2470 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS2469;
      _M0L1nS344 = _M0L6_2atmpS2470;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS2473 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS2473;
      int32_t _M0L6_2atmpS2472 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS2472;
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
      uint64_t _M0L6_2atmpS2461 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS2462 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS2461;
      _M0L5countS341 = _M0L6_2atmpS2462;
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
    int32_t _M0L6_2atmpS2460;
    int32_t _M0L6_2atmpS2459;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS2460 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS2459 = _M0L6_2atmpS2460 / 4;
    return _M0L6_2atmpS2459 + 1;
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
    int32_t _M0L6_2atmpS2458 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS2458;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS2455;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2455 = 1;
      } else {
        _M0L6_2atmpS2455 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS2455;
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
      int32_t _M0L6_2atmpS2456;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2456 = 1;
      } else {
        _M0L6_2atmpS2456 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS2456;
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
      int32_t _M0L6_2atmpS2457;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2457 = 1;
      } else {
        _M0L6_2atmpS2457 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS2457;
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
      uint32_t _M0L6_2atmpS2453 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS2454 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS2453;
      _M0L5countS315 = _M0L6_2atmpS2454;
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
    int32_t _M0L6_2atmpS2452;
    int32_t _M0L6_2atmpS2451;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS2452 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS2451 = _M0L6_2atmpS2452 / 4;
    return _M0L6_2atmpS2451 + 1;
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
  int32_t _M0L6_2atmpS2450;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2450 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS2450;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS2427 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS2427;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS2426 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS2425 = 48 + _M0L6_2atmpS2426;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS2425;
      int32_t _M0L6_2atmpS2424 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS2423 = 48 + _M0L6_2atmpS2424;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS2423;
      int32_t _M0L6_2atmpS2422 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS2421 = 48 + _M0L6_2atmpS2422;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS2421;
      int32_t _M0L6_2atmpS2420 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS2419 = 48 + _M0L6_2atmpS2420;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS2419;
      int32_t _M0L6_2atmpS2411 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS2410 = _M0L6_2atmpS2411 - 4;
      int32_t _M0L6_2atmpS2413;
      int32_t _M0L6_2atmpS2412;
      int32_t _M0L6_2atmpS2415;
      int32_t _M0L6_2atmpS2414;
      int32_t _M0L6_2atmpS2417;
      int32_t _M0L6_2atmpS2416;
      int32_t _M0L6_2atmpS2418;
      _M0L6bufferS294[_M0L6_2atmpS2410] = _M0L6d1__hiS290;
      _M0L6_2atmpS2413 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2412 = _M0L6_2atmpS2413 - 3;
      _M0L6bufferS294[_M0L6_2atmpS2412] = _M0L6d1__loS291;
      _M0L6_2atmpS2415 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2414 = _M0L6_2atmpS2415 - 2;
      _M0L6bufferS294[_M0L6_2atmpS2414] = _M0L6d2__hiS292;
      _M0L6_2atmpS2417 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2416 = _M0L6_2atmpS2417 - 1;
      _M0L6bufferS294[_M0L6_2atmpS2416] = _M0L6d2__loS293;
      _M0L6_2atmpS2418 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS2418;
      continue;
    } else {
      int32_t _M0L6_2atmpS2449 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS2449;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS2436 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS2435 = 48 + _M0L6_2atmpS2436;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS2435;
          int32_t _M0L6_2atmpS2434 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS2433 = 48 + _M0L6_2atmpS2434;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS2433;
          int32_t _M0L6_2atmpS2429 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2428 = _M0L6_2atmpS2429 - 2;
          int32_t _M0L6_2atmpS2431;
          int32_t _M0L6_2atmpS2430;
          int32_t _M0L6_2atmpS2432;
          _M0L6bufferS294[_M0L6_2atmpS2428] = _M0L5d__hiS301;
          _M0L6_2atmpS2431 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2430 = _M0L6_2atmpS2431 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2430] = _M0L5d__loS302;
          _M0L6_2atmpS2432 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS2432;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS2444 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS2443 = 48 + _M0L6_2atmpS2444;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS2443;
          int32_t _M0L6_2atmpS2442 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS2441 = 48 + _M0L6_2atmpS2442;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS2441;
          int32_t _M0L6_2atmpS2438 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2437 = _M0L6_2atmpS2438 - 2;
          int32_t _M0L6_2atmpS2440;
          int32_t _M0L6_2atmpS2439;
          _M0L6bufferS294[_M0L6_2atmpS2437] = _M0L5d__hiS304;
          _M0L6_2atmpS2440 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2439 = _M0L6_2atmpS2440 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2439] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS2448 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2445 = _M0L6_2atmpS2448 - 1;
          int32_t _M0L6_2atmpS2447 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS2446 = (uint16_t)_M0L6_2atmpS2447;
          _M0L6bufferS294[_M0L6_2atmpS2445] = _M0L6_2atmpS2446;
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
  int32_t _M0L6_2atmpS2395;
  int32_t _M0L6_2atmpS2394;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS2395 = _M0L5radixS268 - 1;
  _M0L6_2atmpS2394 = _M0L5radixS268 & _M0L6_2atmpS2395;
  if (_M0L6_2atmpS2394 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS2402;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS2402 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS2402;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS2401 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS2401;
        int32_t _M0L6_2atmpS2398 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS2396 = _M0L6_2atmpS2398 - 1;
        int32_t _M0L6_2atmpS2397 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS2399;
        uint32_t _M0L6_2atmpS2400;
        _M0L6bufferS274[_M0L6_2atmpS2396] = _M0L6_2atmpS2397;
        _M0L6_2atmpS2399 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS2400 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS2399;
        _M0L1nS272 = _M0L6_2atmpS2400;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2409 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS2409;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS2408 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS2407 = _M0L1nS280 - _M0L6_2atmpS2408;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS2407;
        int32_t _M0L6_2atmpS2405 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS2403 = _M0L6_2atmpS2405 - 1;
        int32_t _M0L6_2atmpS2404 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS2406;
        _M0L6bufferS274[_M0L6_2atmpS2403] = _M0L6_2atmpS2404;
        _M0L6_2atmpS2406 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS2406;
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
  int32_t _M0L6_2atmpS2393;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2393 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS2393;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS2390 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS2390;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS2384 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS2382 = _M0L6_2atmpS2384 - 2;
      int32_t _M0L6_2atmpS2383 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS2387;
      int32_t _M0L6_2atmpS2385;
      int32_t _M0L6_2atmpS2386;
      int32_t _M0L6_2atmpS2388;
      uint32_t _M0L6_2atmpS2389;
      _M0L6bufferS261[_M0L6_2atmpS2382] = _M0L6_2atmpS2383;
      _M0L6_2atmpS2387 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS2385 = _M0L6_2atmpS2387 - 1;
      _M0L6_2atmpS2386
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS2385] = _M0L6_2atmpS2386;
      _M0L6_2atmpS2388 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS2389 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS2388;
      _M0L1nS257 = _M0L6_2atmpS2389;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS2392 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS2392;
      int32_t _M0L6_2atmpS2391 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS2391;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS2381;
  moonbit_string_t _result_6183;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS2381
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS2381);
  if (_M0L6_2atmpS2381.$1) {
    moonbit_decref(_M0L6_2atmpS2381.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_6183 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_6183;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS2378;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2378 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS2378);
  moonbit_decref_cycle_free(_M0L6_2atmpS2378);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS2379;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2379 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS2379);
  moonbit_decref_cycle_free(_M0L6_2atmpS2379);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS2380;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2380 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS2380);
  moonbit_decref_cycle_free(_M0L6_2atmpS2380);
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
  moonbit_string_t _M0L8_2afieldS5720;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS5720 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5720);
  return _M0L8_2afieldS5720;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS2377;
  int64_t _M0L6_2atmpS2376;
  struct _M0TPC16string10StringView _M0L6_2atmpS2375;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2377 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS2376 = (int64_t)_M0L6_2atmpS2377;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2375
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS2376);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS2375);
  moonbit_decref_cycle_free(_M0L6_2atmpS2375.$0);
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
  int32_t _M0L6_2atmpS2359;
  int32_t _if__result_6184;
  int32_t _M0L6_2atmpS2367;
  int32_t _if__result_6185;
  int32_t _M0L6_2atmpS2369;
  int32_t _M0L6_2atmpS2370;
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
  _M0L6_2atmpS2359 = _M0Lm2loS236;
  if (_M0L6_2atmpS2359 > 0) {
    int32_t _M0L6_2atmpS2358 = _M0Lm2loS236;
    if (_M0L6_2atmpS2358 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2357 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS2356 = _M0L4selfS235[_M0L6_2atmpS2357];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2356)) {
        int32_t _M0L6_2atmpS2355 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS2354 = _M0L6_2atmpS2355 - 1;
        int32_t _M0L6_2atmpS2353 = _M0L4selfS235[_M0L6_2atmpS2354];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6184
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2353);
      } else {
        _if__result_6184 = 0;
      }
    } else {
      _if__result_6184 = 0;
    }
  } else {
    _if__result_6184 = 0;
  }
  if (_if__result_6184) {
    int32_t _M0L6_2atmpS2360 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS2360 + 1;
  }
  _M0L6_2atmpS2367 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2367 > 0) {
    int32_t _M0L6_2atmpS2366 = _M0Lm2hiS238;
    if (_M0L6_2atmpS2366 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2365 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS2364 = _M0L4selfS235[_M0L6_2atmpS2365];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2364)) {
        int32_t _M0L6_2atmpS2363 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS2362 = _M0L6_2atmpS2363 - 1;
        int32_t _M0L6_2atmpS2361 = _M0L4selfS235[_M0L6_2atmpS2362];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6185
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2361);
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
    int32_t _M0L6_2atmpS2368 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS2368 - 1;
  }
  _M0L6_2atmpS2369 = _M0Lm2loS236;
  _M0L6_2atmpS2370 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2369 >= _M0L6_2atmpS2370) {
    int32_t _M0L6_2atmpS2371 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2372 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2371,
                                                 .$2 = _M0L6_2atmpS2372};
  } else {
    int32_t _M0L6_2atmpS2373 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2374 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2373,
                                                 .$2 = _M0L6_2atmpS2374};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS2352;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS2352
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS2352);
  if (_M0L6_2atmpS2352.$1) {
    moonbit_decref(_M0L6_2atmpS2352.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS2351;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS2351
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS2351);
  if (_M0L6_2atmpS2351.$1) {
    moonbit_decref(_M0L6_2atmpS2351.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS2350;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2350 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS2350;
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
  int32_t _M0L6_2atmpS2349;
  struct _M0TPC16string10StringView _M0L6_2atmpS2347;
  struct _M0TPB6Logger _M0L6_2atmpS2348;
  moonbit_string_t _result_6186;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS2349 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS2347
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS2349
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS2348
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS2347, _M0L6_2atmpS2348, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS2347.$0);
  if (_M0L6_2atmpS2348.$1) {
    moonbit_decref(_M0L6_2atmpS2348.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6186 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_6186;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS2345;
  int32_t _M0L5startS2346;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS2345 = _M0L4selfS218.$2;
  _M0L5startS2346 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS2345 - _M0L5startS2346;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 124, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS2342;
    int32_t _M0L5startS2344;
    int32_t _M0L6_2atmpS2343;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS2326;
    int32_t _M0L6_2atmpS2327;
    int32_t _M0L6_2atmpS2328;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS2342 = _M0L4selfS218.$0;
    _M0L5startS2344 = _M0L4selfS218.$1;
    _M0L6_2atmpS2343 = _M0L5startS2344 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS2342[_M0L6_2atmpS2343];
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
        int32_t _M0L6_2atmpS2329;
        int32_t _M0L6_2atmpS2330;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS2329 = _M0L1iS220 + 1;
        _M0L6_2atmpS2330 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2329;
        _M0L3segS221 = _M0L6_2atmpS2330;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS2331;
        int32_t _M0L6_2atmpS2332;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS2331 = _M0L1iS220 + 1;
        _M0L6_2atmpS2332 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2331;
        _M0L3segS221 = _M0L6_2atmpS2332;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS2333;
        int32_t _M0L6_2atmpS2334;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS2333 = _M0L1iS220 + 1;
        _M0L6_2atmpS2334 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2333;
        _M0L3segS221 = _M0L6_2atmpS2334;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS2335;
        int32_t _M0L6_2atmpS2336;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS2335 = _M0L1iS220 + 1;
        _M0L6_2atmpS2336 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2335;
        _M0L3segS221 = _M0L6_2atmpS2336;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS2338;
          moonbit_string_t _M0L6_2atmpS2337;
          int32_t _M0L6_2atmpS2339;
          int32_t _M0L6_2atmpS2340;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
          _M0L6_2atmpS2338 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS2337 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS2338);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS2337);
          moonbit_decref_cycle_free(_M0L6_2atmpS2337);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS2339 = _M0L1iS220 + 1;
          _M0L6_2atmpS2340 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS2339;
          _M0L3segS221 = _M0L6_2atmpS2340;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS2341 = _M0L1iS220 + 1;
          int32_t _tmp_6189 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS2341;
          _M0L3segS221 = _tmp_6189;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_6188;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2326 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS2326);
    _M0L6_2atmpS2327 = _M0L1iS220 + 1;
    _M0L6_2atmpS2328 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS2327;
    _M0L3segS221 = _M0L6_2atmpS2328;
    continue;
    joinlet_6188:;
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
    int64_t _M0L6_2atmpS2325 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS2324;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2324
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS2325);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS2324);
    moonbit_decref_cycle_free(_M0L6_2atmpS2324.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS2322;
  int32_t _M0L5startS2323;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS2300;
  int32_t _if__result_6190;
  int32_t _M0L6_2atmpS2310;
  int32_t _if__result_6191;
  int32_t _M0L6_2atmpS2312;
  int32_t _M0L6_2atmpS2313;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS2322 = _M0L4selfS201.$2;
  _M0L5startS2323 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS2322 - _M0L5startS2323;
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
  _M0L6_2atmpS2300 = _M0Lm2loS202;
  if (_M0L6_2atmpS2300 > 0) {
    int32_t _M0L6_2atmpS2299 = _M0Lm2loS202;
    if (_M0L6_2atmpS2299 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2298 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS2297 = _M0L4baseS209 + _M0L6_2atmpS2298;
      int32_t _M0L6_2atmpS2296 = _M0L3strS208[_M0L6_2atmpS2297];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2296)) {
        int32_t _M0L6_2atmpS2295 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS2294 = _M0L4baseS209 + _M0L6_2atmpS2295;
        int32_t _M0L6_2atmpS2293 = _M0L6_2atmpS2294 - 1;
        int32_t _M0L6_2atmpS2292 = _M0L3strS208[_M0L6_2atmpS2293];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6190
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2292);
      } else {
        _if__result_6190 = 0;
      }
    } else {
      _if__result_6190 = 0;
    }
  } else {
    _if__result_6190 = 0;
  }
  if (_if__result_6190) {
    int32_t _M0L6_2atmpS2301 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS2301 + 1;
  }
  _M0L6_2atmpS2310 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2310 > 0) {
    int32_t _M0L6_2atmpS2309 = _M0Lm2hiS204;
    if (_M0L6_2atmpS2309 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2308 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS2307 = _M0L4baseS209 + _M0L6_2atmpS2308;
      int32_t _M0L6_2atmpS2306 = _M0L3strS208[_M0L6_2atmpS2307];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2306)) {
        int32_t _M0L6_2atmpS2305 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS2304 = _M0L4baseS209 + _M0L6_2atmpS2305;
        int32_t _M0L6_2atmpS2303 = _M0L6_2atmpS2304 - 1;
        int32_t _M0L6_2atmpS2302 = _M0L3strS208[_M0L6_2atmpS2303];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6191
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2302);
      } else {
        _if__result_6191 = 0;
      }
    } else {
      _if__result_6191 = 0;
    }
  } else {
    _if__result_6191 = 0;
  }
  if (_if__result_6191) {
    int32_t _M0L6_2atmpS2311 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS2311 - 1;
  }
  _M0L6_2atmpS2312 = _M0Lm2loS202;
  _M0L6_2atmpS2313 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2312 >= _M0L6_2atmpS2313) {
    int32_t _M0L6_2atmpS2317 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2314 = _M0L4baseS209 + _M0L6_2atmpS2317;
    int32_t _M0L6_2atmpS2316 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2315 = _M0L4baseS209 + _M0L6_2atmpS2316;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2314,
                                                 .$2 = _M0L6_2atmpS2315};
  } else {
    int32_t _M0L6_2atmpS2321 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2318 = _M0L4baseS209 + _M0L6_2atmpS2321;
    int32_t _M0L6_2atmpS2320 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS2319 = _M0L4baseS209 + _M0L6_2atmpS2320;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2318,
                                                 .$2 = _M0L6_2atmpS2319};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS2289;
  int32_t _M0L6_2atmpS2288;
  int32_t _M0L6_2atmpS2291;
  int32_t _M0L6_2atmpS2290;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS2287;
  moonbit_string_t _result_6192;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2289 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2288
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2289);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2288);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2291 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2290
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2291);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2290);
  _M0L6_2atmpS2287 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6192 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS2287);
  moonbit_decref_cycle_free(_M0L6_2atmpS2287);
  return _result_6192;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS2284;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2284 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2284);
  } else {
    int32_t _M0L6_2atmpS2286;
    int32_t _M0L6_2atmpS2285;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2286 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2285 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS2286, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2285);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS2282;
  int32_t _M0L6_2atmpS2283;
  int32_t _M0L6_2atmpS2281;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2282 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS2283 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS2281 = _M0L6_2atmpS2282 - _M0L6_2atmpS2283;
  return _M0L6_2atmpS2281 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS2279;
  int32_t _M0L6_2atmpS2280;
  int32_t _M0L6_2atmpS2278;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2279 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS2280 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS2278 = _M0L6_2atmpS2279 % _M0L6_2atmpS2280;
  return _M0L6_2atmpS2278 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS2276;
  int32_t _M0L6_2atmpS2277;
  int32_t _M0L6_2atmpS2275;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2276 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS2277 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS2275 = _M0L6_2atmpS2276 / _M0L6_2atmpS2277;
  return _M0L6_2atmpS2275 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS2273;
  int32_t _M0L6_2atmpS2274;
  int32_t _M0L6_2atmpS2272;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2273 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS2274 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS2272 = _M0L6_2atmpS2273 + _M0L6_2atmpS2274;
  return _M0L6_2atmpS2272 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS2271;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS2271 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS2271;
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
  int32_t _M0L3lenS2270;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS2265;
  int32_t _M0L6_2atmpS2264;
  int32_t _if__result_6193;
  uint16_t* _M0L4dataS2266;
  int32_t _M0L3lenS2267;
  int32_t _M0L3lenS2269;
  int32_t _M0L6_2atmpS2268;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS2270 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS2270 + _M0L8str__lenS182;
  _M0L4dataS2265 = _M0L4selfS185->$0;
  _M0L6_2atmpS2264 = Moonbit_array_length(_M0L4dataS2265);
  if (_M0L8requiredS184 > _M0L6_2atmpS2264) {
    _if__result_6193 = 1;
  } else {
    int32_t _M0L3lenS2263 = _M0L4selfS185->$1;
    _if__result_6193 = _M0L8requiredS184 < _M0L3lenS2263;
  }
  if (_if__result_6193) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS2266 = _M0L4selfS185->$0;
  _M0L3lenS2267 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS2266);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2266, _M0L3lenS2267, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS2266);
  _M0L3lenS2269 = _M0L4selfS185->$1;
  _M0L6_2atmpS2268 = _M0L3lenS2269 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS2268;
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
      int32_t _M0L6_2atmpS2260 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS2261;
      int32_t _M0L6_2atmpS2262;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS2260;
      _M0L6_2atmpS2261 = _M0L1iS176 + 1;
      _M0L6_2atmpS2262 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS2261;
      _M0L1jS177 = _M0L6_2atmpS2262;
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
    int32_t _M0L3lenS2231 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS2233 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2232 = Moonbit_array_length(_M0L4dataS2233);
    uint16_t* _M0L4dataS2236;
    int32_t _M0L3lenS2237;
    int32_t _M0L6_2atmpS2238;
    int32_t _M0L3lenS2240;
    int32_t _M0L6_2atmpS2239;
    if (_M0L3lenS2231 >= _M0L6_2atmpS2232) {
      int32_t _M0L3lenS2235 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2234 = _M0L3lenS2235 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2234);
    }
    _M0L4dataS2236 = _M0L4selfS171->$0;
    _M0L3lenS2237 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS2236);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2238 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS2237 < 0
      || _M0L3lenS2237 >= Moonbit_array_length(_M0L4dataS2236)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2236[_M0L3lenS2237] = _M0L6_2atmpS2238;
    moonbit_decref_cycle_free(_M0L4dataS2236);
    _M0L3lenS2240 = _M0L4selfS171->$1;
    _M0L6_2atmpS2239 = _M0L3lenS2240 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS2239;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS2244 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2242 = Moonbit_array_length(_M0L4dataS2244);
    int32_t _M0L3lenS2243 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS2241 = _M0L6_2atmpS2242 - _M0L3lenS2243;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS2247;
    int32_t _M0L3lenS2248;
    uint32_t _M0L6_2atmpS2251;
    uint32_t _M0L6_2atmpS2250;
    int32_t _M0L6_2atmpS2249;
    uint16_t* _M0L4dataS2252;
    int32_t _M0L3lenS2257;
    int32_t _M0L6_2atmpS2253;
    uint32_t _M0L6_2atmpS2256;
    uint32_t _M0L6_2atmpS2255;
    int32_t _M0L6_2atmpS2254;
    int32_t _M0L3lenS2259;
    int32_t _M0L6_2atmpS2258;
    if (_M0L6_2atmpS2241 < 2) {
      int32_t _M0L3lenS2246 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2245 = _M0L3lenS2246 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2245);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS2247 = _M0L4selfS171->$0;
    _M0L3lenS2248 = _M0L4selfS171->$1;
    _M0L6_2atmpS2251 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS2250 = 55296u + _M0L6_2atmpS2251;
    moonbit_incref_cycle_free(_M0L4dataS2247);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2249 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2250);
    if (
      _M0L3lenS2248 < 0
      || _M0L3lenS2248 >= Moonbit_array_length(_M0L4dataS2247)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2247[_M0L3lenS2248] = _M0L6_2atmpS2249;
    moonbit_decref_cycle_free(_M0L4dataS2247);
    _M0L4dataS2252 = _M0L4selfS171->$0;
    _M0L3lenS2257 = _M0L4selfS171->$1;
    _M0L6_2atmpS2253 = _M0L3lenS2257 + 1;
    _M0L6_2atmpS2256 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS2255 = 56320u + _M0L6_2atmpS2256;
    moonbit_incref_cycle_free(_M0L4dataS2252);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2254 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2255);
    if (
      _M0L6_2atmpS2253 < 0
      || _M0L6_2atmpS2253 >= Moonbit_array_length(_M0L4dataS2252)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2252[_M0L6_2atmpS2253] = _M0L6_2atmpS2254;
    moonbit_decref_cycle_free(_M0L4dataS2252);
    _M0L3lenS2259 = _M0L4selfS171->$1;
    _M0L6_2atmpS2258 = _M0L3lenS2259 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS2258;
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
  uint16_t* _M0L4dataS2230;
  int32_t _M0L6_2atmpS2228;
  int32_t _M0L3lenS2229;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS2225;
  int32_t _M0L6_2atmpS2226;
  int32_t _M0L3lenS2227;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS5721;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS2230 = _M0L4selfS166->$0;
  _M0L6_2atmpS2228 = Moonbit_array_length(_M0L4dataS2230);
  _M0L3lenS2229 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS2228, _M0L3lenS2229, _M0L8requiredS167);
  _M0L4dataS2225 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS2225);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2226 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS2227 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2225, _M0L13new__capacityS165, _M0L6_2atmpS2226, _M0L3lenS2227, 0, 0);
  _M0L6_2aoldS5721 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5721);
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
  int32_t _M0L6_2atmpS2224;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2224 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS2224;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS2223;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2223 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS2223;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS2214;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS2214 = _M0L4selfS155->$1;
  if (_M0L3lenS2214 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS2215 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS2217 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS2216 = Moonbit_array_length(_M0L4dataS2217);
    if (_M0L3lenS2215 == _M0L6_2atmpS2216) {
      uint16_t* _M0L4dataS2218 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS2218);
      return _M0L4dataS2218;
    } else {
      uint16_t* _M0L4dataS2219 = _M0L4selfS155->$0;
      int32_t _M0L3lenS2220 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS2221;
      int32_t _M0L3lenS2222;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS2219);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS2221 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS2222 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2219, _M0L3lenS2220, _M0L6_2atmpS2221, _M0L3lenS2222, 0, 0);
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
  int32_t _if__result_6196;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS2210 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS2211 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS2210 <= _M0L6_2atmpS2211) {
            int32_t _M0L6_2atmpS2209 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_6196 = _M0L6_2atmpS2209 <= _M0L13allocate__lenS148;
          } else {
            _if__result_6196 = 0;
          }
        } else {
          _if__result_6196 = 0;
        }
      } else {
        _if__result_6196 = 0;
      }
    } else {
      _if__result_6196 = 0;
    }
  } else {
    _if__result_6196 = 0;
  }
  if (_if__result_6196) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS2213;
    moonbit_string_t _M0L6_2atmpS2212;
    uint16_t* _result_6197;
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
    _M0L6_2atmpS2213 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS2213);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS2212
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_6197 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS2212);
    moonbit_decref_cycle_free(_M0L6_2atmpS2212);
    return _result_6197;
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
  struct _M0TPB13StringBuilder* _block_6198;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS2208 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS2208 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_6198
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_6198)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 129, 0);
  _block_6198->$0 = _M0L4dataS140;
  _block_6198->$1 = 0;
  return _block_6198;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS2207;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2207 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS2207;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_6199;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS2188 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS2189;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2189
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
          if (_M0L6_2atmpS2188 <= _M0L6_2atmpS2189) {
            int32_t _M0L6_2atmpS2187 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_6199 = _M0L6_2atmpS2187 <= _M0L13allocate__lenS113;
          } else {
            _if__result_6199 = 0;
          }
        } else {
          _if__result_6199 = 0;
        }
      } else {
        _if__result_6199 = 0;
      }
    } else {
      _if__result_6199 = 0;
    }
  } else {
    _if__result_6199 = 0;
  }
  if (_if__result_6199) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS2191;
    moonbit_string_t _M0L6_2atmpS2190;
    int32_t* _result_6200;
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
    _M0L6_2atmpS2191 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS2191);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2190
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6200
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS2190);
    moonbit_decref_cycle_free(_M0L6_2atmpS2190);
    return _result_6200;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_6201;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS2193 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS2194;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2194
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
          if (_M0L6_2atmpS2193 <= _M0L6_2atmpS2194) {
            int32_t _M0L6_2atmpS2192 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_6201 = _M0L6_2atmpS2192 <= _M0L13allocate__lenS119;
          } else {
            _if__result_6201 = 0;
          }
        } else {
          _if__result_6201 = 0;
        }
      } else {
        _if__result_6201 = 0;
      }
    } else {
      _if__result_6201 = 0;
    }
  } else {
    _if__result_6201 = 0;
  }
  if (_if__result_6201) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS2196;
    moonbit_string_t _M0L6_2atmpS2195;
    moonbit_string_t* _result_6202;
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
    _M0L6_2atmpS2196 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS2196);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2195
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6202
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS2195);
    moonbit_decref_cycle_free(_M0L6_2atmpS2195);
    return _result_6202;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_6203;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS2198 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS2199;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2199
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
          if (_M0L6_2atmpS2198 <= _M0L6_2atmpS2199) {
            int32_t _M0L6_2atmpS2197 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_6203 = _M0L6_2atmpS2197 <= _M0L13allocate__lenS125;
          } else {
            _if__result_6203 = 0;
          }
        } else {
          _if__result_6203 = 0;
        }
      } else {
        _if__result_6203 = 0;
      }
    } else {
      _if__result_6203 = 0;
    }
  } else {
    _if__result_6203 = 0;
  }
  if (_if__result_6203) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, 0, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS2201;
    moonbit_string_t _M0L6_2atmpS2200;
    struct _M0TUsiE** _result_6204;
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
    _M0L6_2atmpS2201 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS2201);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2200
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6204
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS2200);
    moonbit_decref_cycle_free(_M0L6_2atmpS2200);
    return _result_6204;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_6205;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS2203 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS2204;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2204
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS2203 <= _M0L6_2atmpS2204) {
            int32_t _M0L6_2atmpS2202 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_6205 = _M0L6_2atmpS2202 <= _M0L13allocate__lenS131;
          } else {
            _if__result_6205 = 0;
          }
        } else {
          _if__result_6205 = 0;
        }
      } else {
        _if__result_6205 = 0;
      }
    } else {
      _if__result_6205 = 0;
    }
  } else {
    _if__result_6205 = 0;
  }
  if (_if__result_6205) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS2206;
    moonbit_string_t _M0L6_2atmpS2205;
    float* _result_6206;
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
    _M0L6_2atmpS2206 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS2206);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2205
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6206
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS2205);
    moonbit_decref_cycle_free(_M0L6_2atmpS2205);
    return _result_6206;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS2184;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS2184
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS2184);
  if (_M0L6_2atmpS2184.$1) {
    moonbit_decref(_M0L6_2atmpS2184.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS2185;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS2185
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS2185);
  if (_M0L6_2atmpS2185.$1) {
    moonbit_decref(_M0L6_2atmpS2185.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS2186;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS2186
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS2186);
  if (_M0L6_2atmpS2186.$1) {
    moonbit_decref(_M0L6_2atmpS2186.$1);
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
        int32_t _M0L6_2atmpS2139 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS2141 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS2140;
        int32_t _M0L6_2atmpS2142;
        if (
          _M0L6_2atmpS2141 < 0
          || _M0L6_2atmpS2141 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2140 = (int32_t)_M0L3srcS19[_M0L6_2atmpS2141];
        if (
          _M0L6_2atmpS2139 < 0
          || _M0L6_2atmpS2139 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS2139] = _M0L6_2atmpS2140;
        _M0L6_2atmpS2142 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS2142;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2147 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS2147;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS2143 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS2145 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS2144;
        int32_t _M0L6_2atmpS2146;
        if (
          _M0L6_2atmpS2145 < 0
          || _M0L6_2atmpS2145 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2144 = (int32_t)_M0L3srcS19[_M0L6_2atmpS2145];
        if (
          _M0L6_2atmpS2143 < 0
          || _M0L6_2atmpS2143 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS2143] = _M0L6_2atmpS2144;
        _M0L6_2atmpS2146 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS2146;
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
        int32_t _M0L6_2atmpS2148 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS2150 = _M0L11src__offsetS30 + _M0L1iS31;
        int32_t _M0L6_2atmpS2149;
        int32_t _M0L6_2atmpS2151;
        if (
          _M0L6_2atmpS2150 < 0
          || _M0L6_2atmpS2150 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2149 = (int32_t)_M0L3srcS28[_M0L6_2atmpS2150];
        if (
          _M0L6_2atmpS2148 < 0
          || _M0L6_2atmpS2148 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS2148] = _M0L6_2atmpS2149;
        _M0L6_2atmpS2151 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS2151;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2156 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS2156;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS2152 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS2154 = _M0L11src__offsetS30 + _M0L1iS34;
        int32_t _M0L6_2atmpS2153;
        int32_t _M0L6_2atmpS2155;
        if (
          _M0L6_2atmpS2154 < 0
          || _M0L6_2atmpS2154 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2153 = (int32_t)_M0L3srcS28[_M0L6_2atmpS2154];
        if (
          _M0L6_2atmpS2152 < 0
          || _M0L6_2atmpS2152 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS2152] = _M0L6_2atmpS2153;
        _M0L6_2atmpS2155 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS2155;
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
        int32_t _M0L6_2atmpS2157 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS2159 = _M0L11src__offsetS39 + _M0L1iS40;
        moonbit_string_t _M0L6_2atmpS2158;
        moonbit_string_t _M0L6_2aoldS5722;
        int32_t _M0L6_2atmpS2160;
        if (
          _M0L6_2atmpS2159 < 0
          || _M0L6_2atmpS2159 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2158 = (moonbit_string_t)_M0L3srcS37[_M0L6_2atmpS2159];
        if (
          _M0L6_2atmpS2157 < 0
          || _M0L6_2atmpS2157 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5722 = (moonbit_string_t)_M0L3dstS36[_M0L6_2atmpS2157];
        moonbit_incref_cycle_free(_M0L6_2atmpS2158);
        moonbit_decref_cycle_free(_M0L6_2aoldS5722);
        _M0L3dstS36[_M0L6_2atmpS2157] = _M0L6_2atmpS2158;
        _M0L6_2atmpS2160 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS2160;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2165 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS2165;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS2161 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS2163 = _M0L11src__offsetS39 + _M0L1iS43;
        moonbit_string_t _M0L6_2atmpS2162;
        moonbit_string_t _M0L6_2aoldS5723;
        int32_t _M0L6_2atmpS2164;
        if (
          _M0L6_2atmpS2163 < 0
          || _M0L6_2atmpS2163 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2162 = (moonbit_string_t)_M0L3srcS37[_M0L6_2atmpS2163];
        if (
          _M0L6_2atmpS2161 < 0
          || _M0L6_2atmpS2161 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5723 = (moonbit_string_t)_M0L3dstS36[_M0L6_2atmpS2161];
        moonbit_incref_cycle_free(_M0L6_2atmpS2162);
        moonbit_decref_cycle_free(_M0L6_2aoldS5723);
        _M0L3dstS36[_M0L6_2atmpS2161] = _M0L6_2atmpS2162;
        _M0L6_2atmpS2164 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS2164;
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
        int32_t _M0L6_2atmpS2166 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS2168 = _M0L11src__offsetS48 + _M0L1iS49;
        struct _M0TUsiE* _M0L6_2atmpS2167;
        struct _M0TUsiE* _M0L6_2aoldS5724;
        int32_t _M0L6_2atmpS2169;
        if (
          _M0L6_2atmpS2168 < 0
          || _M0L6_2atmpS2168 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2167 = (struct _M0TUsiE*)_M0L3srcS46[_M0L6_2atmpS2168];
        if (
          _M0L6_2atmpS2166 < 0
          || _M0L6_2atmpS2166 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5724 = (struct _M0TUsiE*)_M0L3dstS45[_M0L6_2atmpS2166];
        if (_M0L6_2atmpS2167) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2167);
        }
        if (_M0L6_2aoldS5724) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5724);
        }
        _M0L3dstS45[_M0L6_2atmpS2166] = _M0L6_2atmpS2167;
        _M0L6_2atmpS2169 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS2169;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2174 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS2174;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS2170 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS2172 = _M0L11src__offsetS48 + _M0L1iS52;
        struct _M0TUsiE* _M0L6_2atmpS2171;
        struct _M0TUsiE* _M0L6_2aoldS5725;
        int32_t _M0L6_2atmpS2173;
        if (
          _M0L6_2atmpS2172 < 0
          || _M0L6_2atmpS2172 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2171 = (struct _M0TUsiE*)_M0L3srcS46[_M0L6_2atmpS2172];
        if (
          _M0L6_2atmpS2170 < 0
          || _M0L6_2atmpS2170 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5725 = (struct _M0TUsiE*)_M0L3dstS45[_M0L6_2atmpS2170];
        if (_M0L6_2atmpS2171) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2171);
        }
        if (_M0L6_2aoldS5725) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5725);
        }
        _M0L3dstS45[_M0L6_2atmpS2170] = _M0L6_2atmpS2171;
        _M0L6_2atmpS2173 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS2173;
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
        int32_t _M0L6_2atmpS2175 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS2177 = _M0L11src__offsetS57 + _M0L1iS58;
        float _M0L6_2atmpS2176;
        int32_t _M0L6_2atmpS2178;
        if (
          _M0L6_2atmpS2177 < 0
          || _M0L6_2atmpS2177 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2176 = (float)_M0L3srcS55[_M0L6_2atmpS2177];
        if (
          _M0L6_2atmpS2175 < 0
          || _M0L6_2atmpS2175 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS2175] = _M0L6_2atmpS2176;
        _M0L6_2atmpS2178 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS2178;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2183 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS2183;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS2179 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS2181 = _M0L11src__offsetS57 + _M0L1iS61;
        float _M0L6_2atmpS2180;
        int32_t _M0L6_2atmpS2182;
        if (
          _M0L6_2atmpS2181 < 0
          || _M0L6_2atmpS2181 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2180 = (float)_M0L3srcS55[_M0L6_2atmpS2181];
        if (
          _M0L6_2atmpS2179 < 0
          || _M0L6_2atmpS2179 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS2179] = _M0L6_2atmpS2180;
        _M0L6_2atmpS2182 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS2182;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS2106) {
  switch (Moonbit_object_tag(_M0L4_2aeS2106)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS2106);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 4: {
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
  void* _M0L11_2aobj__ptrS2134,
  struct _M0TPB4Show _M0L8_2aparamS2133
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2132 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2134;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS2132, _M0L8_2aparamS2133);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2131,
  struct _M0TPB4Show _M0L8_2aparamS2130
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2129 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2131;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS2129, _M0L8_2aparamS2130);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2128,
  int32_t _M0L8_2aparamS2127
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2126 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2128;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS2126, _M0L8_2aparamS2127);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2125,
  struct _M0TPC16string10StringView _M0L8_2aparamS2124
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2123 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2125;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS2123, _M0L8_2aparamS2124);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2122,
  moonbit_string_t _M0L8_2aparamS2119,
  int32_t _M0L8_2aparamS2120,
  int32_t _M0L8_2aparamS2121
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2118 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2122;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS2118, _M0L8_2aparamS2119, _M0L8_2aparamS2120, _M0L8_2aparamS2121);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2117,
  moonbit_string_t _M0L8_2aparamS2116
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2115 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2117;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS2115, _M0L8_2aparamS2116);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_6217 = 9218868437227405311ll;
  int64_t _tmp_6218;
  int64_t _tmp_6219;
  int64_t _tmp_6220;
  int64_t _tmp_6221;
  _M0FPB18double__max__value = *(double*)&_tmp_6217;
  _tmp_6218 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_6218;
  _tmp_6219 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_6219;
  _tmp_6220 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_6220;
  _tmp_6221 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_6221;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS2138;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS2099;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS2100;
  int32_t _M0L7_2abindS2101;
  struct _M0TUsiE** _M0L7_2abindS2102;
  int32_t _M0L6_2acntS5902;
  int32_t _M0L2__S2103;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS2138
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS2099
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS2099)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 132, 0);
  _M0L12async__testsS2099->$0 = _M0L6_2atmpS2138;
  _M0L12async__testsS2099->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS2100
  = _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS2101 = _M0L7_2abindS2100->$1;
  _M0L7_2abindS2102 = _M0L7_2abindS2100->$0;
  _M0L6_2acntS5902
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS2100));
  if (_M0L6_2acntS5902 > 1) {
    int32_t _M0L11_2anew__cntS5903 = _M0L6_2acntS5902 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS2100), _M0L11_2anew__cntS5903);
    moonbit_incref_cycle_free(_M0L7_2abindS2102);
  } else if (_M0L6_2acntS5902 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS2100);
  }
  _M0L2__S2103 = 0;
  while (1) {
    if (_M0L2__S2103 < _M0L7_2abindS2101) {
      struct _M0TUsiE* _M0L3argS2104 =
        (struct _M0TUsiE*)_M0L7_2abindS2102[_M0L2__S2103];
      moonbit_string_t _M0L6_2atmpS2135 = _M0L3argS2104->$0;
      int32_t _M0L6_2atmpS2136 = _M0L3argS2104->$1;
      int32_t _M0L6_2atmpS2137;
      moonbit_incref_cycle_free(_M0L6_2atmpS2135);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples25festa2024__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS2099, _M0L6_2atmpS2135, _M0L6_2atmpS2136);
      moonbit_decref_cycle_free(_M0L6_2atmpS2135);
      _M0L6_2atmpS2137 = _M0L2__S2103 + 1;
      _M0L2__S2103 = _M0L6_2atmpS2137;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS2102);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\festa2024\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples25festa2024__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25festa2024__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS2099);
  moonbit_decref_cycle_free(_M0L12async__testsS2099);
  moonbit_flush_cycles();
  return 0;
}