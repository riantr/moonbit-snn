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

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt15HetRecParameter;

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002;

struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples22hetrec__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples22hetrec__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt6HetRec;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB6Logger;

struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TWRPC15error5ErrorEu;

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

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
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

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997 {
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

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err {
  void* $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples22hetrec__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples22hetrec__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
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

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1009(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1002(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS997(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS974(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S967(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples22hetrec__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt23hetrec__refresh__random(
  struct _M0TP26RiantR8snn__mbt6HetRec*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt6HetRec* _M0MP26RiantR8snn__mbt6HetRec3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt15HetRecParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0MP26RiantR8snn__mbt15HetRecParameter3new(
  
);

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec*,
  float
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

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

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
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

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 104, 101, 116, 114, 101, 99, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_19 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_12 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_31 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_29 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[111]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 110, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 104, 101, 116, 114, 101, 99, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 
    114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_26 =
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
} const moonbit_string_literal_15 =
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
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1009$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1009
  };

uint32_t const moonbit_layout_table_data[60] =
  {
    sizeof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997)
    / 4, 1,
    offsetof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997, $1)
    / 4
    * 2,
    sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002)
    / 4, 1,
    offsetof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt6HetRec) / 4, 13,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6HetRec, $13) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2097
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1030,
  moonbit_string_t _M0L8filenameS999,
  int32_t _M0L5indexS1001
) {
  struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997* _closure_2122;
  struct _M0TWEu* _M0L13handle__startS997;
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002* _closure_2123;
  struct _M0TWssbEu* _M0L14handle__resultS1002;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1009;
  void* _M0L11_2atry__errS1024;
  struct moonbit_result_0 _tmp_2125;
  int32_t _handle__error__result_2126;
  int32_t _M0L6_2atmpS2085;
  void* _M0L3errS1025;
  moonbit_string_t _M0L4nameS1027;
  struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1028;
  moonbit_string_t _M0L7_2anameS1029;
  int32_t _M0L6_2acntS2116;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS999);
  _closure_2122
  = (struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997*)moonbit_malloc(sizeof(struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997));
  Moonbit_object_header(_closure_2122)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2122->code
  = &_M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS997;
  _closure_2122->$0 = _M0L5indexS1001;
  _closure_2122->$1 = _M0L8filenameS999;
  _M0L13handle__startS997 = (struct _M0TWEu*)_closure_2122;
  moonbit_incref_cycle_free(_M0L8filenameS999);
  _closure_2123
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002*)moonbit_malloc(sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002));
  Moonbit_object_header(_closure_2123)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2123->code
  = &_M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1002;
  _closure_2123->$0 = _M0L5indexS1001;
  _closure_2123->$1 = _M0L8filenameS999;
  _M0L14handle__resultS1002 = (struct _M0TWssbEu*)_closure_2123;
  _M0L17error__to__stringS1009
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1009$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2125
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1030, _M0L8filenameS999, _M0L5indexS1001, _M0L13handle__startS997, _M0L14handle__resultS1002, _M0L17error__to__stringS1009);
  if (_tmp_2125.tag) {
    int32_t const _M0L5_2aokS2094 = _tmp_2125.data.ok;
    _handle__error__result_2126 = _M0L5_2aokS2094;
  } else {
    void* const _M0L6_2aerrS2095 = _tmp_2125.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
    moonbit_decref_cycle_free(_M0L13handle__startS997);
    _M0L11_2atry__errS1024 = _M0L6_2aerrS2095;
    goto join_1023;
  }
  if (_handle__error__result_2126) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
    moonbit_decref_cycle_free(_M0L13handle__startS997);
    _M0L6_2atmpS2085 = 1;
  } else {
    struct moonbit_result_0 _tmp_2127;
    int32_t _handle__error__result_2128;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2127
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1030, _M0L8filenameS999, _M0L5indexS1001, _M0L13handle__startS997, _M0L14handle__resultS1002, _M0L17error__to__stringS1009);
    if (_tmp_2127.tag) {
      int32_t const _M0L5_2aokS2092 = _tmp_2127.data.ok;
      _handle__error__result_2128 = _M0L5_2aokS2092;
    } else {
      void* const _M0L6_2aerrS2093 = _tmp_2127.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
      moonbit_decref_cycle_free(_M0L13handle__startS997);
      _M0L11_2atry__errS1024 = _M0L6_2aerrS2093;
      goto join_1023;
    }
    if (_handle__error__result_2128) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
      moonbit_decref_cycle_free(_M0L13handle__startS997);
      _M0L6_2atmpS2085 = 1;
    } else {
      struct moonbit_result_0 _tmp_2129;
      int32_t _handle__error__result_2130;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2129
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1030, _M0L8filenameS999, _M0L5indexS1001, _M0L13handle__startS997, _M0L14handle__resultS1002, _M0L17error__to__stringS1009);
      if (_tmp_2129.tag) {
        int32_t const _M0L5_2aokS2090 = _tmp_2129.data.ok;
        _handle__error__result_2130 = _M0L5_2aokS2090;
      } else {
        void* const _M0L6_2aerrS2091 = _tmp_2129.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
        moonbit_decref_cycle_free(_M0L13handle__startS997);
        _M0L11_2atry__errS1024 = _M0L6_2aerrS2091;
        goto join_1023;
      }
      if (_handle__error__result_2130) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
        moonbit_decref_cycle_free(_M0L13handle__startS997);
        _M0L6_2atmpS2085 = 1;
      } else {
        struct moonbit_result_0 _tmp_2131;
        int32_t _handle__error__result_2132;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2131
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1030, _M0L8filenameS999, _M0L5indexS1001, _M0L13handle__startS997, _M0L14handle__resultS1002, _M0L17error__to__stringS1009);
        if (_tmp_2131.tag) {
          int32_t const _M0L5_2aokS2088 = _tmp_2131.data.ok;
          _handle__error__result_2132 = _M0L5_2aokS2088;
        } else {
          void* const _M0L6_2aerrS2089 = _tmp_2131.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
          moonbit_decref_cycle_free(_M0L13handle__startS997);
          _M0L11_2atry__errS1024 = _M0L6_2aerrS2089;
          goto join_1023;
        }
        if (_handle__error__result_2132) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
          moonbit_decref_cycle_free(_M0L13handle__startS997);
          _M0L6_2atmpS2085 = 1;
        } else {
          struct moonbit_result_0 _tmp_2133;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2133
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1030, _M0L8filenameS999, _M0L5indexS1001, _M0L13handle__startS997, _M0L14handle__resultS1002, _M0L17error__to__stringS1009);
          moonbit_decref_cycle_free(_M0L13handle__startS997);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1009);
          if (_tmp_2133.tag) {
            int32_t const _M0L5_2aokS2086 = _tmp_2133.data.ok;
            _M0L6_2atmpS2085 = _M0L5_2aokS2086;
          } else {
            void* const _M0L6_2aerrS2087 = _tmp_2133.data.err;
            _M0L11_2atry__errS1024 = _M0L6_2aerrS2087;
            goto join_1023;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2085) {
    void* _M0L125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2096 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2096)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2096)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1024
    = _M0L125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2096;
    goto join_1023;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1002);
  }
  goto joinlet_2124;
  join_1023:;
  _M0L3errS1025 = _M0L11_2atry__errS1024;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1028
  = (struct _M0DTPC15error5Error125RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1025;
  _M0L7_2anameS1029 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1028->$0;
  _M0L6_2acntS2116
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1028));
  if (_M0L6_2acntS2116 > 1) {
    int32_t _M0L11_2anew__cntS2117 = _M0L6_2acntS2116 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1028), _M0L11_2anew__cntS2117);
    moonbit_incref_cycle_free(_M0L7_2anameS1029);
  } else if (_M0L6_2acntS2116 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1028);
  }
  _M0L4nameS1027 = _M0L7_2anameS1029;
  goto join_1026;
  goto joinlet_2134;
  join_1026:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1002(_M0L14handle__resultS1002, _M0L4nameS1027, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1002);
  moonbit_decref_cycle_free(_M0L4nameS1027);
  joinlet_2134:;
  joinlet_2124:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1009(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2084,
  void* _M0L3errS1010
) {
  void* _M0L1eS1012;
  moonbit_string_t _M0L1eS1014;
  moonbit_string_t _result_2137;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1010)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1015 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1010;
      moonbit_string_t _M0L4_2aeS1016 = _M0L10_2aFailureS1015->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1016);
      _M0L1eS1014 = _M0L4_2aeS1016;
      goto join_1013;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1017 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1010;
      moonbit_string_t _M0L4_2aeS1018 = _M0L15_2aInspectErrorS1017->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1018);
      _M0L1eS1014 = _M0L4_2aeS1018;
      goto join_1013;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1019 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1010;
      moonbit_string_t _M0L4_2aeS1020 = _M0L16_2aSnapshotErrorS1019->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1020);
      _M0L1eS1014 = _M0L4_2aeS1020;
      goto join_1013;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1021 =
        (struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1010;
      moonbit_string_t _M0L4_2aeS1022 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1021->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1022);
      _M0L1eS1014 = _M0L4_2aeS1022;
      goto join_1013;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1010);
      _M0L1eS1012 = _M0L3errS1010;
      goto join_1011;
      break;
    }
  }
  join_1013:;
  return _M0L1eS1014;
  join_1011:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _result_2137 = _M0FP15Error10to__string(_M0L1eS1012);
  moonbit_decref_cycle_free(_M0L1eS1012);
  return _result_2137;
}

int32_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1002(
  struct _M0TWssbEu* _M0L6_2aenvS2081,
  moonbit_string_t _M0L10__testnameS1003,
  moonbit_string_t _M0L7messageS1004,
  int32_t _M0L7skippedS1005
) {
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002* _M0L14_2acasted__envS2082;
  moonbit_string_t _M0L8filenameS999;
  int32_t _M0L5indexS1001;
  moonbit_string_t _M0L10file__nameS1006;
  moonbit_string_t _M0L7messageS1007;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1008;
  moonbit_string_t _M0L6_2atmpS2083;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2082
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1002*)_M0L6_2aenvS2081;
  _M0L8filenameS999 = _M0L14_2acasted__envS2082->$1;
  _M0L5indexS1001 = _M0L14_2acasted__envS2082->$0;
  if (!_M0L7skippedS1005 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1006
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS999, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1007
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1004, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1008
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1008, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1008, _M0L10file__nameS1006);
  moonbit_decref_cycle_free(_M0L10file__nameS1006);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1008, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1008, _M0L5indexS1001);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1008, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1008, _M0L7messageS1007);
  moonbit_decref_cycle_free(_M0L7messageS1007);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1008, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2083
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1008);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1008);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2083);
  moonbit_decref_cycle_free(_M0L6_2atmpS2083);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS997(
  struct _M0TWEu* _M0L6_2aenvS2078
) {
  struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997* _M0L14_2acasted__envS2079;
  moonbit_string_t _M0L8filenameS999;
  int32_t _M0L5indexS1001;
  moonbit_string_t _M0L10file__nameS998;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1000;
  moonbit_string_t _M0L6_2atmpS2080;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2079
  = (struct _M0R125_24RiantR_2fsnn__mbt_2fexamples_2fhetrec__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c997*)_M0L6_2aenvS2078;
  _M0L8filenameS999 = _M0L14_2acasted__envS2079->$1;
  _M0L5indexS1001 = _M0L14_2acasted__envS2079->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS998
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS999, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1000
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1000, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1000, _M0L10file__nameS998);
  moonbit_decref_cycle_free(_M0L10file__nameS998);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1000, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1000, _M0L5indexS1001);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1000, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2080
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1000);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1000);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2080);
  moonbit_decref_cycle_free(_M0L6_2atmpS2080);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S967;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS974;
  struct _M0TUsiE** _M0L6_2atmpS2077;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS981;
  moonbit_string_t* _M0L9cli__argsS982;
  moonbit_string_t _M0L6_2atmpS2076;
  moonbit_string_t _M0L6_2atmpS2075;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS983;
  int32_t _M0L7_2abindS984;
  moonbit_string_t* _M0L7_2abindS985;
  int32_t _M0L6_2acntS2118;
  int32_t _M0L2__S986;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S967 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS974 = 0;
  _M0L6_2atmpS2077 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS981
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS981)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS981->$0 = _M0L6_2atmpS2077;
  _M0L16file__and__indexS981->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS982
  = _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS982)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2076 = (moonbit_string_t)_M0L9cli__argsS982[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2076);
  moonbit_decref_cycle_free(_M0L9cli__argsS982);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2075
  = _M0MP46RiantR8snn__mbt8examples22hetrec__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2076);
  moonbit_decref_cycle_free(_M0L6_2atmpS2076);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS983
  = _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS974(_M0L51moonbit__test__driver__internal__split__mbt__stringS974, _M0L6_2atmpS2075, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2075);
  _M0L7_2abindS984 = _M0L10test__argsS983->$1;
  _M0L7_2abindS985 = _M0L10test__argsS983->$0;
  _M0L6_2acntS2118
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS983));
  if (_M0L6_2acntS2118 > 1) {
    int32_t _M0L11_2anew__cntS2119 = _M0L6_2acntS2118 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS983), _M0L11_2anew__cntS2119);
    moonbit_incref_cycle_free(_M0L7_2abindS985);
  } else if (_M0L6_2acntS2118 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS983);
  }
  _M0L2__S986 = 0;
  while (1) {
    if (_M0L2__S986 < _M0L7_2abindS984) {
      moonbit_string_t _M0L3argS987 =
        (moonbit_string_t)_M0L7_2abindS985[_M0L2__S986];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS988;
      moonbit_string_t _M0L4fileS989;
      moonbit_string_t _M0L5rangeS990;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS991;
      moonbit_string_t _M0L6_2atmpS2073;
      int32_t _M0L5startS992;
      moonbit_string_t _M0L6_2atmpS2072;
      int32_t _M0L3endS993;
      int32_t _M0L1iS994;
      int32_t _M0L6_2atmpS2074;
      moonbit_incref_cycle_free(_M0L3argS987);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS988
      = _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS974(_M0L51moonbit__test__driver__internal__split__mbt__stringS974, _M0L3argS987, 58);
      moonbit_decref_cycle_free(_M0L3argS987);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS989
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS988, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS990
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS988, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS988);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS991
      = _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS974(_M0L51moonbit__test__driver__internal__split__mbt__stringS974, _M0L5rangeS990, 45);
      moonbit_decref_cycle_free(_M0L5rangeS990);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2073
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS991, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS992
      = _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S967(_M0L45moonbit__test__driver__internal__parse__int__S967, _M0L6_2atmpS2073);
      moonbit_decref_cycle_free(_M0L6_2atmpS2073);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2072
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS991, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS991);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS993
      = _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S967(_M0L45moonbit__test__driver__internal__parse__int__S967, _M0L6_2atmpS2072);
      moonbit_decref_cycle_free(_M0L6_2atmpS2072);
      _M0L1iS994 = _M0L5startS992;
      while (1) {
        if (_M0L1iS994 < _M0L3endS993) {
          struct _M0TUsiE* _M0L8_2atupleS2070;
          int32_t _M0L6_2atmpS2071;
          moonbit_incref_cycle_free(_M0L4fileS989);
          _M0L8_2atupleS2070
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2070)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2070->$0 = _M0L4fileS989;
          _M0L8_2atupleS2070->$1 = _M0L1iS994;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS981, _M0L8_2atupleS2070);
          _M0L6_2atmpS2071 = _M0L1iS994 + 1;
          _M0L1iS994 = _M0L6_2atmpS2071;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS989);
        }
        break;
      }
      _M0L6_2atmpS2074 = _M0L2__S986 + 1;
      _M0L2__S986 = _M0L6_2atmpS2074;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS985);
    }
    break;
  }
  return _M0L16file__and__indexS981;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS974(
  int32_t _M0L6_2aenvS2051,
  moonbit_string_t _M0L1sS975,
  int32_t _M0L3sepS976
) {
  moonbit_string_t* _M0L6_2atmpS2069;
  struct _M0TPB5ArrayGsE* _M0L3resS977;
  struct _M0TPB8MutLocalGiE* _M0L1iS978;
  struct _M0TPB8MutLocalGiE* _M0L5startS979;
  int32_t _M0L3valS2064;
  int32_t _M0L6_2atmpS2065;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2069 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS977
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS977)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS977->$0 = _M0L6_2atmpS2069;
  _M0L3resS977->$1 = 0;
  _M0L1iS978
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS978)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS978->$0 = 0;
  _M0L5startS979
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS979)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS979->$0 = 0;
  while (1) {
    int32_t _M0L3valS2052 = _M0L1iS978->$0;
    int32_t _M0L6_2atmpS2053 = Moonbit_array_length(_M0L1sS975);
    if (_M0L3valS2052 < _M0L6_2atmpS2053) {
      int32_t _M0L3valS2056 = _M0L1iS978->$0;
      int32_t _M0L6_2atmpS2055;
      int32_t _M0L6_2atmpS2054;
      int32_t _M0L3valS2063;
      int32_t _M0L6_2atmpS2062;
      if (
        _M0L3valS2056 < 0
        || _M0L3valS2056 >= Moonbit_array_length(_M0L1sS975)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2055 = _M0L1sS975[_M0L3valS2056];
      _M0L6_2atmpS2054 = _M0L6_2atmpS2055;
      if (_M0L6_2atmpS2054 == _M0L3sepS976) {
        int32_t _M0L3valS2058 = _M0L5startS979->$0;
        int32_t _M0L3valS2059 = _M0L1iS978->$0;
        moonbit_string_t _M0L6_2atmpS2057;
        int32_t _M0L3valS2061;
        int32_t _M0L6_2atmpS2060;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2057
        = _M0MPC16string6String17unsafe__substring(_M0L1sS975, _M0L3valS2058, _M0L3valS2059);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS977, _M0L6_2atmpS2057);
        _M0L3valS2061 = _M0L1iS978->$0;
        _M0L6_2atmpS2060 = _M0L3valS2061 + 1;
        _M0L5startS979->$0 = _M0L6_2atmpS2060;
      }
      _M0L3valS2063 = _M0L1iS978->$0;
      _M0L6_2atmpS2062 = _M0L3valS2063 + 1;
      _M0L1iS978->$0 = _M0L6_2atmpS2062;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS978);
    }
    break;
  }
  _M0L3valS2064 = _M0L5startS979->$0;
  _M0L6_2atmpS2065 = Moonbit_array_length(_M0L1sS975);
  if (_M0L3valS2064 < _M0L6_2atmpS2065) {
    int32_t _M0L3valS2067 = _M0L5startS979->$0;
    int32_t _M0L6_2atmpS2068;
    moonbit_string_t _M0L6_2atmpS2066;
    moonbit_decref_cycle_free(_M0L5startS979);
    _M0L6_2atmpS2068 = Moonbit_array_length(_M0L1sS975);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2066
    = _M0MPC16string6String17unsafe__substring(_M0L1sS975, _M0L3valS2067, _M0L6_2atmpS2068);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS977, _M0L6_2atmpS2066);
  } else {
    moonbit_decref_cycle_free(_M0L5startS979);
  }
  return _M0L3resS977;
}

int32_t _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S967(
  int32_t _M0L6_2aenvS2044,
  moonbit_string_t _M0L1sS968
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS969;
  int32_t _M0L3lenS970;
  int32_t _M0L7_2abindS971;
  int32_t _M0L1iS972;
  int32_t _result_2142;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS969
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS969)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS969->$0 = 0;
  _M0L3lenS970 = Moonbit_array_length(_M0L1sS968);
  _M0L7_2abindS971 = 0;
  _M0L1iS972 = _M0L7_2abindS971;
  while (1) {
    if (_M0L1iS972 < _M0L3lenS970) {
      int32_t _M0L3valS2049 = _M0L3resS969->$0;
      int32_t _M0L6_2atmpS2046 = _M0L3valS2049 * 10;
      int32_t _M0L6_2atmpS2048;
      int32_t _M0L6_2atmpS2047;
      int32_t _M0L6_2atmpS2045;
      int32_t _M0L6_2atmpS2050;
      if (_M0L1iS972 < 0 || _M0L1iS972 >= Moonbit_array_length(_M0L1sS968)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2048 = _M0L1sS968[_M0L1iS972];
      _M0L6_2atmpS2047 = _M0L6_2atmpS2048 - 48;
      _M0L6_2atmpS2045 = _M0L6_2atmpS2046 + _M0L6_2atmpS2047;
      _M0L3resS969->$0 = _M0L6_2atmpS2045;
      _M0L6_2atmpS2050 = _M0L1iS972 + 1;
      _M0L1iS972 = _M0L6_2atmpS2050;
      continue;
    }
    break;
  }
  _result_2142 = _M0L3resS969->$0;
  moonbit_decref_cycle_free(_M0L3resS969);
  return _result_2142;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples22hetrec__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS966
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS966);
  return _M0L4selfS966;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S936,
  moonbit_string_t _M0L12_2adiscard__S937,
  int32_t _M0L12_2adiscard__S938,
  struct _M0TWEu* _M0L12_2adiscard__S939,
  struct _M0TWssbEu* _M0L12_2adiscard__S940,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S941
) {
  struct moonbit_result_0 _result_2143;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _result_2143.tag = 1;
  _result_2143.data.ok = 0;
  return _result_2143;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S942,
  moonbit_string_t _M0L12_2adiscard__S943,
  int32_t _M0L12_2adiscard__S944,
  struct _M0TWEu* _M0L12_2adiscard__S945,
  struct _M0TWssbEu* _M0L12_2adiscard__S946,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S947
) {
  struct moonbit_result_0 _result_2144;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _result_2144.tag = 1;
  _result_2144.data.ok = 0;
  return _result_2144;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S948,
  moonbit_string_t _M0L12_2adiscard__S949,
  int32_t _M0L12_2adiscard__S950,
  struct _M0TWEu* _M0L12_2adiscard__S951,
  struct _M0TWssbEu* _M0L12_2adiscard__S952,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S953
) {
  struct moonbit_result_0 _result_2145;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _result_2145.tag = 1;
  _result_2145.data.ok = 0;
  return _result_2145;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S954,
  moonbit_string_t _M0L12_2adiscard__S955,
  int32_t _M0L12_2adiscard__S956,
  struct _M0TWEu* _M0L12_2adiscard__S957,
  struct _M0TWssbEu* _M0L12_2adiscard__S958,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S959
) {
  struct moonbit_result_0 _result_2146;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _result_2146.tag = 1;
  _result_2146.data.ok = 0;
  return _result_2146;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S960,
  moonbit_string_t _M0L12_2adiscard__S961,
  int32_t _M0L12_2adiscard__S962,
  struct _M0TWEu* _M0L12_2adiscard__S963,
  struct _M0TWssbEu* _M0L12_2adiscard__S964,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S965
) {
  struct moonbit_result_0 _result_2147;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _result_2147.tag = 1;
  _result_2147.data.ok = 0;
  return _result_2147;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S935
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23hetrec__refresh__random(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS897,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS900
) {
  int32_t _M0L1nS896;
  int32_t _M0L7_2abindS898;
  int32_t _M0L1iS899;
  #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS896 = _M0L1pS897->$1;
  _M0L7_2abindS898 = 0;
  _M0L1iS899 = _M0L7_2abindS898;
  while (1) {
    if (_M0L1iS899 < _M0L1nS896) {
      struct _M0TPB5ArrayGfE* _M0L9randcacheS2041 = _M0L1pS897->$10;
      float _M0L6_2atmpS2042;
      int32_t _M0L6_2atmpS2043;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS2042 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS900);
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS2041, _M0L1iS899, _M0L6_2atmpS2042);
      _M0L6_2atmpS2043 = _M0L1iS899 + 1;
      _M0L1iS899 = _M0L6_2atmpS2043;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt6HetRec* _M0MP26RiantR8snn__mbt6HetRec3new(
  int32_t _M0L1nS861,
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS859,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS869
) {
  int32_t _M0L2ndS858;
  int32_t _M0L8total__dS860;
  struct _M0TPB5ArrayGfE* _M0L4v__dS862;
  struct _M0TPB5ArrayGfE* _M0L4v__sS863;
  struct _M0TPB5ArrayGfE* _M0L4is__S864;
  struct _M0TPB5ArrayGfE* _M0L1rS865;
  int32_t _M0L7_2abindS866;
  int32_t _M0L1iS867;
  struct _M0TPB5ArrayGfE* _M0L6tau__dS871;
  int32_t _M0L7_2abindS872;
  int32_t _M0L1iS873;
  struct _M0TPB5ArrayGbE* _M0L4fireS876;
  struct _M0TPB5ArrayGiE* _M0L4tabsS877;
  struct _M0TPB5ArrayGfE* _M0L5traceS878;
  struct _M0TPB5ArrayGfE* _M0L9randcacheS879;
  int32_t _M0L6_2atmpS2040;
  struct _M0TPB5ArrayGiE* _M0L6colptrS880;
  int32_t* _M0L6_2atmpS2039;
  struct _M0TPB5ArrayGiE* _M0L6i__synS881;
  float* _M0L6_2atmpS2038;
  struct _M0TPB5ArrayGfE* _M0L6w__synS882;
  int32_t _M0L7_2abindS883;
  int32_t _M0L1iS884;
  int32_t _M0L6_2atmpS2037;
  struct _M0TP26RiantR8snn__mbt6HetRec* _block_2155;
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L2ndS858 = _M0L5paramS859->$0;
  _M0L8total__dS860 = _M0L1nS861 * _M0L2ndS858;
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L4v__dS862 = _M0MPC15array5Array4makeGfE(_M0L8total__dS860, 0x0p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L4v__sS863 = _M0MPC15array5Array4makeGfE(_M0L1nS861, 0x0p+0f);
  #line 132 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L4is__S864 = _M0MPC15array5Array4makeGfE(_M0L8total__dS860, 0x0p+0f);
  #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1rS865 = _M0MPC15array5Array4makeGfE(_M0L1nS861, 0x0p+0f);
  _M0L7_2abindS866 = 0;
  _M0L1iS867 = _M0L7_2abindS866;
  while (1) {
    if (_M0L1iS867 < _M0L1nS861) {
      float _M0L1uS868;
      float _M0L9rate__lowS2017;
      float _M0L10rate__highS2020;
      float _M0L9rate__lowS2021;
      float _M0L6_2atmpS2019;
      float _M0L6_2atmpS2018;
      float _M0L6_2atmpS2016;
      int32_t _M0L6_2atmpS2022;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L1uS868 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS869);
      _M0L9rate__lowS2017 = _M0L5paramS859->$4;
      _M0L10rate__highS2020 = _M0L5paramS859->$5;
      _M0L9rate__lowS2021 = _M0L5paramS859->$4;
      _M0L6_2atmpS2019 = _M0L10rate__highS2020 - _M0L9rate__lowS2021;
      _M0L6_2atmpS2018 = _M0L1uS868 * _M0L6_2atmpS2019;
      _M0L6_2atmpS2016 = _M0L9rate__lowS2017 + _M0L6_2atmpS2018;
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS865, _M0L1iS867, _M0L6_2atmpS2016);
      _M0L6_2atmpS2022 = _M0L1iS867 + 1;
      _M0L1iS867 = _M0L6_2atmpS2022;
      continue;
    }
    break;
  }
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L6tau__dS871 = _M0MPC15array5Array4makeGfE(_M0L8total__dS860, 0x0p+0f);
  _M0L7_2abindS872 = 0;
  _M0L1iS873 = _M0L7_2abindS872;
  while (1) {
    if (_M0L1iS873 < _M0L8total__dS860) {
      float _M0L1uS874;
      float _M0L11tau__d__lowS2024;
      float _M0L12tau__d__highS2027;
      float _M0L11tau__d__lowS2028;
      float _M0L6_2atmpS2026;
      float _M0L6_2atmpS2025;
      float _M0L6_2atmpS2023;
      int32_t _M0L6_2atmpS2029;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L1uS874 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS869);
      _M0L11tau__d__lowS2024 = _M0L5paramS859->$2;
      _M0L12tau__d__highS2027 = _M0L5paramS859->$3;
      _M0L11tau__d__lowS2028 = _M0L5paramS859->$2;
      _M0L6_2atmpS2026 = _M0L12tau__d__highS2027 - _M0L11tau__d__lowS2028;
      _M0L6_2atmpS2025 = _M0L1uS874 * _M0L6_2atmpS2026;
      _M0L6_2atmpS2023 = _M0L11tau__d__lowS2024 + _M0L6_2atmpS2025;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L6tau__dS871, _M0L1iS873, _M0L6_2atmpS2023);
      _M0L6_2atmpS2029 = _M0L1iS873 + 1;
      _M0L1iS873 = _M0L6_2atmpS2029;
      continue;
    }
    break;
  }
  #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L4fireS876 = _M0MPC15array5Array4makeGbE(_M0L1nS861, 0);
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L4tabsS877 = _M0MPC15array5Array4makeGiE(_M0L1nS861, 0);
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L5traceS878 = _M0MPC15array5Array4makeGfE(_M0L1nS861, 0x0p+0f);
  #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L9randcacheS879 = _M0MPC15array5Array4makeGfE(_M0L1nS861, 0x0p+0f);
  _M0L6_2atmpS2040 = _M0L1nS861 + 1;
  #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L6colptrS880 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2040, 0);
  _M0L6_2atmpS2039 = (int32_t*)moonbit_empty_int32_array;
  _M0L6i__synS881
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6i__synS881)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6i__synS881->$0 = _M0L6_2atmpS2039;
  _M0L6i__synS881->$1 = 0;
  _M0L6_2atmpS2038 = moonbit_empty_float_array;
  _M0L6w__synS882
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6w__synS882)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6w__synS882->$0 = _M0L6_2atmpS2038;
  _M0L6w__synS882->$1 = 0;
  _M0L7_2abindS883 = 0;
  _M0L1iS884 = _M0L7_2abindS883;
  while (1) {
    if (_M0L1iS884 < _M0L1nS861) {
      int32_t _M0L6_2atmpS2030;
      int32_t _M0L7_2abindS885;
      int32_t _M0L1jS886;
      int32_t _M0L6_2atmpS2036;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS2030 = _M0MPC15array5Array6lengthGiE(_M0L6i__synS881);
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L6colptrS880, _M0L1iS884, _M0L6_2atmpS2030);
      _M0L7_2abindS885 = 0;
      _M0L1jS886 = _M0L7_2abindS885;
      while (1) {
        if (_M0L1jS886 < _M0L2ndS858) {
          int32_t _M0L6_2atmpS2034 = _M0L1jS886 * _M0L1nS861;
          int32_t _M0L6d__idxS887 = _M0L6_2atmpS2034 + _M0L1iS884;
          int32_t _M0L7_2abindS888;
          int32_t _M0L1kS889;
          int32_t _M0L6_2atmpS2035;
          #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array4pushGiE(_M0L6i__synS881, _M0L6d__idxS887);
          #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array4pushGfE(_M0L6w__synS882, 0x1p+0f);
          _M0L7_2abindS888 = 0;
          _M0L1kS889 = _M0L7_2abindS888;
          while (1) {
            if (_M0L1kS889 < _M0L1nS861) {
              float _M0L1uS892;
              float _M0L7overlapS2032;
              int32_t _M0L6_2atmpS2031;
              if (_M0L1kS889 == _M0L1iS884) {
                goto join_890;
              }
              #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
              _M0L1uS892 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS869);
              _M0L7overlapS2032 = _M0L5paramS859->$1;
              if (_M0L1uS892 < _M0L7overlapS2032) {
                int32_t _M0L6_2atmpS2033 = _M0L1jS886 * _M0L1nS861;
                int32_t _M0L7d__idx2S893 = _M0L6_2atmpS2033 + _M0L1kS889;
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
                _M0MPC15array5Array4pushGiE(_M0L6i__synS881, _M0L7d__idx2S893);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
                _M0MPC15array5Array4pushGfE(_M0L6w__synS882, 0x1p+0f);
              }
              goto join_890;
              goto joinlet_2154;
              join_890:;
              _M0L6_2atmpS2031 = _M0L1kS889 + 1;
              _M0L1kS889 = _M0L6_2atmpS2031;
              continue;
              joinlet_2154:;
            }
            break;
          }
          _M0L6_2atmpS2035 = _M0L1jS886 + 1;
          _M0L1jS886 = _M0L6_2atmpS2035;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2036 = _M0L1iS884 + 1;
      _M0L1iS884 = _M0L6_2atmpS2036;
      continue;
    }
    break;
  }
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L6_2atmpS2037 = _M0MPC15array5Array6lengthGiE(_M0L6i__synS881);
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0MPC15array5Array3setGiE(_M0L6colptrS880, _M0L1nS861, _M0L6_2atmpS2037);
  moonbit_incref_cycle_free(_M0L5paramS859);
  _block_2155
  = (struct _M0TP26RiantR8snn__mbt6HetRec*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt6HetRec));
  Moonbit_object_header(_block_2155)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2155->$0 = _M0L5paramS859;
  _block_2155->$1 = _M0L1nS861;
  _block_2155->$2 = _M0L4v__dS862;
  _block_2155->$3 = _M0L4v__sS863;
  _block_2155->$4 = _M0L4is__S864;
  _block_2155->$5 = _M0L1rS865;
  _block_2155->$6 = _M0L6tau__dS871;
  _block_2155->$7 = _M0L4fireS876;
  _block_2155->$8 = _M0L4tabsS877;
  _block_2155->$9 = _M0L5traceS878;
  _block_2155->$10 = _M0L9randcacheS879;
  _block_2155->$11 = _M0L6colptrS880;
  _block_2155->$12 = _M0L6i__synS881;
  _block_2155->$13 = _M0L6w__synS882;
  return _block_2155;
}

struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0MP26RiantR8snn__mbt15HetRecParameter3new(
  
) {
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _block_2156;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _block_2156
  = (struct _M0TP26RiantR8snn__mbt15HetRecParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15HetRecParameter));
  Moonbit_object_header(_block_2156)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2156->$0 = 2;
  _block_2156->$1 = 0x1p-1f;
  _block_2156->$2 = 0x1.4p+3f;
  _block_2156->$3 = 0x1.9p+6f;
  _block_2156->$4 = 0x0p+0f;
  _block_2156->$5 = 0x1p+0f;
  _block_2156->$6 = 0x1.4p+2f;
  _block_2156->$7 = 0x1p+0f;
  _block_2156->$8 = 0x1.4p+4f;
  _block_2156->$9 = 0x1.9p+6f;
  return _block_2156;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS829,
  float _M0L2dtS837
) {
  int32_t _M0L1nS828;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS2015;
  int32_t _M0L2ndS830;
  int32_t _M0L8total__dS831;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS2014;
  float _M0L9steepnessS832;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS2013;
  float _M0L6tau__mS833;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS2012;
  float _M0L9tau__rateS834;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS2011;
  float _M0L8tau__absS835;
  float _M0L6_2atmpS2010;
  int32_t _M0L11tabs__stepsS836;
  int32_t _M0L7_2abindS838;
  int32_t _M0L1iS839;
  int32_t _M0L7_2abindS842;
  int32_t _M0L1iS843;
  int32_t _M0L7_2abindS852;
  int32_t _M0L1iS853;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS828 = _M0L1pS829->$1;
  _M0L5paramS2015 = _M0L1pS829->$0;
  _M0L2ndS830 = _M0L5paramS2015->$0;
  _M0L8total__dS831 = _M0L1nS828 * _M0L2ndS830;
  _M0L5paramS2014 = _M0L1pS829->$0;
  _M0L9steepnessS832 = _M0L5paramS2014->$7;
  _M0L5paramS2013 = _M0L1pS829->$0;
  _M0L6tau__mS833 = _M0L5paramS2013->$8;
  _M0L5paramS2012 = _M0L1pS829->$0;
  _M0L9tau__rateS834 = _M0L5paramS2012->$9;
  _M0L5paramS2011 = _M0L1pS829->$0;
  _M0L8tau__absS835 = _M0L5paramS2011->$6;
  _M0L6_2atmpS2010 = _M0L8tau__absS835 / _M0L2dtS837;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS836 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2010);
  _M0L7_2abindS838 = 0;
  _M0L1iS839 = _M0L7_2abindS838;
  while (1) {
    if (_M0L1iS839 < _M0L8total__dS831) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS1938 = _M0L1pS829->$6;
      float _M0L7tau__diS840;
      struct _M0TPB5ArrayGfE* _M0L4v__dS1926;
      struct _M0TPB5ArrayGfE* _M0L4v__dS1937;
      float _M0L6_2atmpS1928;
      struct _M0TPB5ArrayGfE* _M0L4v__dS1936;
      float _M0L6_2atmpS1935;
      float _M0L6_2atmpS1932;
      struct _M0TPB5ArrayGfE* _M0L4is__S1934;
      float _M0L6_2atmpS1933;
      float _M0L6_2atmpS1931;
      float _M0L6_2atmpS1930;
      float _M0L6_2atmpS1929;
      float _M0L6_2atmpS1927;
      int32_t _M0L6_2atmpS1939;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS840
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS1938, _M0L1iS839);
      _M0L4v__dS1926 = _M0L1pS829->$2;
      _M0L4v__dS1937 = _M0L1pS829->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1928
      = _M0MPC15array5Array2atGfE(_M0L4v__dS1937, _M0L1iS839);
      _M0L4v__dS1936 = _M0L1pS829->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1935
      = _M0MPC15array5Array2atGfE(_M0L4v__dS1936, _M0L1iS839);
      _M0L6_2atmpS1932 = -_M0L6_2atmpS1935;
      _M0L4is__S1934 = _M0L1pS829->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1933
      = _M0MPC15array5Array2atGfE(_M0L4is__S1934, _M0L1iS839);
      _M0L6_2atmpS1931 = _M0L6_2atmpS1932 - _M0L6_2atmpS1933;
      _M0L6_2atmpS1930 = _M0L2dtS837 * _M0L6_2atmpS1931;
      _M0L6_2atmpS1929 = _M0L6_2atmpS1930 / _M0L7tau__diS840;
      _M0L6_2atmpS1927 = _M0L6_2atmpS1928 + _M0L6_2atmpS1929;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS1926, _M0L1iS839, _M0L6_2atmpS1927);
      _M0L6_2atmpS1939 = _M0L1iS839 + 1;
      _M0L1iS839 = _M0L6_2atmpS1939;
      continue;
    }
    break;
  }
  _M0L7_2abindS842 = 0;
  _M0L1iS843 = _M0L7_2abindS842;
  while (1) {
    if (_M0L1iS843 < _M0L1nS828) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS1960 = _M0L1pS829->$11;
      int32_t _M0L5startS844;
      struct _M0TPB5ArrayGiE* _M0L6colptrS1958;
      int32_t _M0L6_2atmpS1959;
      int32_t _M0L3endS845;
      float _M0L16dt__over__tau__mS846;
      struct _M0TPB8MutLocalGiE* _M0L1sS847;
      int32_t _M0L6_2atmpS1961;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS844
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1960, _M0L1iS843);
      _M0L6colptrS1958 = _M0L1pS829->$11;
      _M0L6_2atmpS1959 = _M0L1iS843 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS845
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1958, _M0L6_2atmpS1959);
      _M0L16dt__over__tau__mS846 = _M0L2dtS837 / _M0L6tau__mS833;
      _M0L1sS847
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS847)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS847->$0 = _M0L5startS844;
      while (1) {
        int32_t _M0L3valS1940 = _M0L1sS847->$0;
        if (_M0L3valS1940 < _M0L3endS845) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS1956 = _M0L1pS829->$12;
          int32_t _M0L3valS1957 = _M0L1sS847->$0;
          int32_t _M0L9dend__idxS848;
          struct _M0TPB5ArrayGfE* _M0L6w__synS1954;
          int32_t _M0L3valS1955;
          float _M0L1wS849;
          struct _M0TPB5ArrayGfE* _M0L4v__sS1941;
          struct _M0TPB5ArrayGfE* _M0L4v__sS1951;
          float _M0L6_2atmpS1943;
          struct _M0TPB5ArrayGfE* _M0L4v__dS1950;
          float _M0L6_2atmpS1949;
          float _M0L6_2atmpS1946;
          struct _M0TPB5ArrayGfE* _M0L4v__sS1948;
          float _M0L6_2atmpS1947;
          float _M0L6_2atmpS1945;
          float _M0L6_2atmpS1944;
          float _M0L6_2atmpS1942;
          int32_t _M0L3valS1953;
          int32_t _M0L6_2atmpS1952;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS848
          = _M0MPC15array5Array2atGiE(_M0L6i__synS1956, _M0L3valS1957);
          _M0L6w__synS1954 = _M0L1pS829->$13;
          _M0L3valS1955 = _M0L1sS847->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS849
          = _M0MPC15array5Array2atGfE(_M0L6w__synS1954, _M0L3valS1955);
          _M0L4v__sS1941 = _M0L1pS829->$3;
          _M0L4v__sS1951 = _M0L1pS829->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS1943
          = _M0MPC15array5Array2atGfE(_M0L4v__sS1951, _M0L1iS843);
          _M0L4v__dS1950 = _M0L1pS829->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS1949
          = _M0MPC15array5Array2atGfE(_M0L4v__dS1950, _M0L9dend__idxS848);
          _M0L6_2atmpS1946 = _M0L1wS849 * _M0L6_2atmpS1949;
          _M0L4v__sS1948 = _M0L1pS829->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS1947
          = _M0MPC15array5Array2atGfE(_M0L4v__sS1948, _M0L1iS843);
          _M0L6_2atmpS1945 = _M0L6_2atmpS1946 - _M0L6_2atmpS1947;
          _M0L6_2atmpS1944 = _M0L6_2atmpS1945 * _M0L16dt__over__tau__mS846;
          _M0L6_2atmpS1942 = _M0L6_2atmpS1943 + _M0L6_2atmpS1944;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS1941, _M0L1iS843, _M0L6_2atmpS1942);
          _M0L3valS1953 = _M0L1sS847->$0;
          _M0L6_2atmpS1952 = _M0L3valS1953 + 1;
          _M0L1sS847->$0 = _M0L6_2atmpS1952;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS847);
        }
        break;
      }
      _M0L6_2atmpS1961 = _M0L1iS843 + 1;
      _M0L1iS843 = _M0L6_2atmpS1961;
      continue;
    }
    break;
  }
  _M0L7_2abindS852 = 0;
  _M0L1iS853 = _M0L7_2abindS852;
  while (1) {
    if (_M0L1iS853 < _M0L1nS828) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1963 = _M0L1pS829->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1966 = _M0L1pS829->$8;
      int32_t _M0L6_2atmpS1965;
      int32_t _M0L6_2atmpS1964;
      struct _M0TPB5ArrayGbE* _M0L4fireS1967;
      struct _M0TPB5ArrayGfE* _M0L5traceS1968;
      struct _M0TPB5ArrayGfE* _M0L5traceS1976;
      float _M0L6_2atmpS1970;
      struct _M0TPB5ArrayGfE* _M0L5traceS1975;
      float _M0L6_2atmpS1974;
      float _M0L6_2atmpS1973;
      float _M0L6_2atmpS1972;
      float _M0L6_2atmpS1971;
      float _M0L6_2atmpS1969;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1978;
      int32_t _M0L6_2atmpS1977;
      struct _M0TPB5ArrayGfE* _M0L5traceS1979;
      struct _M0TPB5ArrayGfE* _M0L5traceS1988;
      float _M0L6_2atmpS1981;
      struct _M0TPB5ArrayGfE* _M0L4v__sS1987;
      float _M0L6_2atmpS1984;
      struct _M0TPB5ArrayGfE* _M0L5traceS1986;
      float _M0L6_2atmpS1985;
      float _M0L6_2atmpS1983;
      float _M0L6_2atmpS1982;
      float _M0L6_2atmpS1980;
      float _M0L6_2atmpS2004;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2009;
      float _M0L6_2atmpS2006;
      struct _M0TPB5ArrayGfE* _M0L5traceS2008;
      float _M0L6_2atmpS2007;
      float _M0L6_2atmpS2005;
      float _M0L12sigmoid__argS856;
      float _M0L4rateS857;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS1990;
      float _M0L6_2atmpS1989;
      int32_t _M0L6_2atmpS1962;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1965
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1966, _M0L1iS853);
      _M0L6_2atmpS1964 = _M0L6_2atmpS1965 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1963, _M0L1iS853, _M0L6_2atmpS1964);
      _M0L4fireS1967 = _M0L1pS829->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1967, _M0L1iS853, 0);
      _M0L5traceS1968 = _M0L1pS829->$9;
      _M0L5traceS1976 = _M0L1pS829->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1970
      = _M0MPC15array5Array2atGfE(_M0L5traceS1976, _M0L1iS853);
      _M0L5traceS1975 = _M0L1pS829->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1974
      = _M0MPC15array5Array2atGfE(_M0L5traceS1975, _M0L1iS853);
      _M0L6_2atmpS1973 = -_M0L6_2atmpS1974;
      _M0L6_2atmpS1972 = _M0L6_2atmpS1973 / _M0L9tau__rateS834;
      _M0L6_2atmpS1971 = _M0L2dtS837 * _M0L6_2atmpS1972;
      _M0L6_2atmpS1969 = _M0L6_2atmpS1970 + _M0L6_2atmpS1971;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS1968, _M0L1iS853, _M0L6_2atmpS1969);
      _M0L4tabsS1978 = _M0L1pS829->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1977
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1978, _M0L1iS853);
      if (_M0L6_2atmpS1977 > 0) {
        goto join_854;
      }
      _M0L5traceS1979 = _M0L1pS829->$9;
      _M0L5traceS1988 = _M0L1pS829->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1981
      = _M0MPC15array5Array2atGfE(_M0L5traceS1988, _M0L1iS853);
      _M0L4v__sS1987 = _M0L1pS829->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1984
      = _M0MPC15array5Array2atGfE(_M0L4v__sS1987, _M0L1iS853);
      _M0L5traceS1986 = _M0L1pS829->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1985
      = _M0MPC15array5Array2atGfE(_M0L5traceS1986, _M0L1iS853);
      _M0L6_2atmpS1983 = _M0L6_2atmpS1984 - _M0L6_2atmpS1985;
      _M0L6_2atmpS1982 = _M0L6_2atmpS1983 / _M0L9tau__rateS834;
      _M0L6_2atmpS1980 = _M0L6_2atmpS1981 + _M0L6_2atmpS1982;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS1979, _M0L1iS853, _M0L6_2atmpS1980);
      _M0L6_2atmpS2004 = -_M0L9steepnessS832;
      _M0L4v__sS2009 = _M0L1pS829->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS2006
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2009, _M0L1iS853);
      _M0L5traceS2008 = _M0L1pS829->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS2007
      = _M0MPC15array5Array2atGfE(_M0L5traceS2008, _M0L1iS853);
      _M0L6_2atmpS2005 = _M0L6_2atmpS2006 - _M0L6_2atmpS2007;
      _M0L12sigmoid__argS856 = _M0L6_2atmpS2004 * _M0L6_2atmpS2005;
      if (_M0L12sigmoid__argS856 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS1998 = _M0L1pS829->$5;
        float _M0L6_2atmpS1997;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS1997 = _M0MPC15array5Array2atGfE(_M0L1rS1998, _M0L1iS853);
        _M0L4rateS857 = _M0L6_2atmpS1997 * _M0L2dtS837;
      } else if (_M0L12sigmoid__argS856 < -0x1.6p+6f) {
        _M0L4rateS857 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS2003 = _M0L1pS829->$5;
        float _M0L6_2atmpS2002;
        float _M0L6_2atmpS1999;
        float _M0L6_2atmpS2001;
        float _M0L6_2atmpS2000;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS2002 = _M0MPC15array5Array2atGfE(_M0L1rS2003, _M0L1iS853);
        _M0L6_2atmpS1999 = _M0L6_2atmpS2002 * _M0L2dtS837;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS2001
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS856);
        _M0L6_2atmpS2000 = 0x1p+0f + _M0L6_2atmpS2001;
        _M0L4rateS857 = _M0L6_2atmpS1999 / _M0L6_2atmpS2000;
      }
      _M0L9randcacheS1990 = _M0L1pS829->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS1989
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS1990, _M0L1iS853);
      if (_M0L6_2atmpS1989 < _M0L4rateS857) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1991 = _M0L1pS829->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1992;
        struct _M0TPB5ArrayGfE* _M0L5traceS1993;
        struct _M0TPB5ArrayGfE* _M0L5traceS1996;
        float _M0L6_2atmpS1995;
        float _M0L6_2atmpS1994;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1991, _M0L1iS853, 1);
        _M0L4tabsS1992 = _M0L1pS829->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1992, _M0L1iS853, _M0L11tabs__stepsS836);
        _M0L5traceS1993 = _M0L1pS829->$9;
        _M0L5traceS1996 = _M0L1pS829->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS1995
        = _M0MPC15array5Array2atGfE(_M0L5traceS1996, _M0L1iS853);
        _M0L6_2atmpS1994 = _M0L6_2atmpS1995 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS1993, _M0L1iS853, _M0L6_2atmpS1994);
      }
      goto join_854;
      goto joinlet_2161;
      join_854:;
      _M0L6_2atmpS1962 = _M0L1iS853 + 1;
      _M0L1iS853 = _M0L6_2atmpS1962;
      continue;
      joinlet_2161:;
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS826
) {
  struct _M0TUmmmmE* _M0L1sS825;
  uint64_t _M0L6_2atmpS1925;
  struct _M0TUmmmmE* _M0L1tS827;
  uint64_t _M0L6_2atmpS1921;
  uint64_t _M0L6_2atmpS1922;
  uint64_t _M0L6_2atmpS1923;
  uint64_t _M0L6_2atmpS1924;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2162;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS825 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS826);
  _M0L6_2atmpS1925 = _M0L1sS825->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS827 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1925);
  _M0L6_2atmpS1921 = _M0L1sS825->$0;
  _M0L6_2atmpS1922 = _M0L1sS825->$1;
  _M0L6_2atmpS1923 = _M0L1sS825->$2;
  moonbit_decref_cycle_free(_M0L1sS825);
  _M0L6_2atmpS1924 = _M0L1tS827->$0;
  moonbit_decref_cycle_free(_M0L1tS827);
  _block_2162
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2162)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2162->$0 = _M0L6_2atmpS1921;
  _block_2162->$1 = _M0L6_2atmpS1922;
  _block_2162->$2 = _M0L6_2atmpS1923;
  _block_2162->$3 = _M0L6_2atmpS1924;
  return _block_2162;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS817) {
  uint64_t _M0L2s1S816;
  uint64_t _M0L2z1S818;
  uint64_t _M0L2s2S819;
  uint64_t _M0L2z2S820;
  uint64_t _M0L2s3S821;
  uint64_t _M0L2z3S822;
  uint64_t _M0L2s4S823;
  uint64_t _M0L2z4S824;
  struct _M0TUmmmmE* _block_2163;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S816 = _M0L4seedS817 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S818 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S816);
  _M0L2s2S819 = _M0L2s1S816 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S820 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S819);
  _M0L2s3S821 = _M0L2s2S819 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S822 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S821);
  _M0L2s4S823 = _M0L2s3S821 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S824 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S823);
  _block_2163 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2163->$0 = _M0L2z1S818;
  _block_2163->$1 = _M0L2z2S820;
  _block_2163->$2 = _M0L2z3S822;
  _block_2163->$3 = _M0L2z4S824;
  return _block_2163;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS814) {
  uint64_t _M0L6_2atmpS1920;
  uint64_t _M0L6_2atmpS1919;
  uint64_t _M0L1zS813;
  uint64_t _M0L6_2atmpS1918;
  uint64_t _M0L6_2atmpS1917;
  uint64_t _M0L1zS815;
  uint64_t _M0L6_2atmpS1916;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1920 = _M0L1zS814 >> 30;
  _M0L6_2atmpS1919 = _M0L1zS814 ^ _M0L6_2atmpS1920;
  _M0L1zS813 = _M0L6_2atmpS1919 * 13787848793156543929ull;
  _M0L6_2atmpS1918 = _M0L1zS813 >> 27;
  _M0L6_2atmpS1917 = _M0L1zS813 ^ _M0L6_2atmpS1918;
  _M0L1zS815 = _M0L6_2atmpS1917 * 10723151780598845931ull;
  _M0L6_2atmpS1916 = _M0L1zS815 >> 31;
  return _M0L1zS815 ^ _M0L6_2atmpS1916;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS811
) {
  uint32_t _M0L1uS810;
  uint32_t _M0L4bitsS812;
  double _M0L6_2atmpS1915;
  double _M0L6_2atmpS1914;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS810 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS811);
  _M0L4bitsS812 = _M0L1uS810 >> 8;
  _M0L6_2atmpS1915 = (double)_M0L4bitsS812;
  _M0L6_2atmpS1914 = _M0L6_2atmpS1915 * 0x1p-24;
  return (float)_M0L6_2atmpS1914;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS809
) {
  uint64_t _M0L1uS808;
  uint64_t _M0L6_2atmpS1913;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS808 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS809);
  _M0L6_2atmpS1913 = _M0L1uS808 >> 32;
  return (uint32_t)_M0L6_2atmpS1913;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS801
) {
  uint64_t _M0L2s0S800;
  uint64_t _M0L2s1S802;
  uint64_t _M0L2s2S803;
  uint64_t _M0L2s3S804;
  uint64_t _M0L3tmpS805;
  uint64_t _M0L6_2atmpS1912;
  uint64_t _M0L3resS806;
  uint64_t _M0L1tS807;
  uint64_t _M0L6_2atmpS1902;
  uint64_t _M0L6_2atmpS1903;
  uint64_t _M0L2s2S1905;
  uint64_t _M0L6_2atmpS1904;
  uint64_t _M0L2s3S1907;
  uint64_t _M0L6_2atmpS1906;
  uint64_t _M0L2s2S1909;
  uint64_t _M0L6_2atmpS1908;
  uint64_t _M0L2s3S1911;
  uint64_t _M0L6_2atmpS1910;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S800 = _M0L1rS801->$0;
  _M0L2s1S802 = _M0L1rS801->$1;
  _M0L2s2S803 = _M0L1rS801->$2;
  _M0L2s3S804 = _M0L1rS801->$3;
  _M0L3tmpS805 = _M0L2s0S800 + _M0L2s3S804;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1912 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS805, 23);
  _M0L3resS806 = _M0L6_2atmpS1912 + _M0L2s0S800;
  _M0L1tS807 = _M0L2s1S802 << 17;
  _M0L6_2atmpS1902 = _M0L2s2S803 ^ _M0L2s0S800;
  _M0L1rS801->$2 = _M0L6_2atmpS1902;
  _M0L6_2atmpS1903 = _M0L2s3S804 ^ _M0L2s1S802;
  _M0L1rS801->$3 = _M0L6_2atmpS1903;
  _M0L2s2S1905 = _M0L1rS801->$2;
  _M0L6_2atmpS1904 = _M0L2s1S802 ^ _M0L2s2S1905;
  _M0L1rS801->$1 = _M0L6_2atmpS1904;
  _M0L2s3S1907 = _M0L1rS801->$3;
  _M0L6_2atmpS1906 = _M0L2s0S800 ^ _M0L2s3S1907;
  _M0L1rS801->$0 = _M0L6_2atmpS1906;
  _M0L2s2S1909 = _M0L1rS801->$2;
  _M0L6_2atmpS1908 = _M0L2s2S1909 ^ _M0L1tS807;
  _M0L1rS801->$2 = _M0L6_2atmpS1908;
  _M0L2s3S1911 = _M0L1rS801->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1910 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1911, 45);
  _M0L1rS801->$3 = _M0L6_2atmpS1910;
  return _M0L3resS806;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS798, int32_t _M0L1kS799) {
  uint64_t _M0L6_2atmpS1899;
  int32_t _M0L6_2atmpS1901;
  uint64_t _M0L6_2atmpS1900;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1899 = _M0L1xS798 << (_M0L1kS799 & 63);
  _M0L6_2atmpS1901 = 64 - _M0L1kS799;
  _M0L6_2atmpS1900 = _M0L1xS798 >> (_M0L6_2atmpS1901 & 63);
  return _M0L6_2atmpS1899 | _M0L6_2atmpS1900;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS797) {
  double _M0L6_2atmpS1898;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1898 = (double)_M0L4selfS797;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1898);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS796) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS796 != _M0L4selfS796) {
    return 0;
  } else if (_M0L4selfS796 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS796 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS796;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS782,
  float _M0L4elemS784
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS781;
  int32_t _M0L1iS783;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS781 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS782);
  _M0L1iS783 = 0;
  while (1) {
    if (_M0L1iS783 < _M0L3lenS782) {
      float* _M0L3bufS1892 = _M0L3arrS781->$0;
      int32_t _M0L6_2atmpS1893;
      _M0L3bufS1892[_M0L1iS783] = _M0L4elemS784;
      _M0L6_2atmpS1893 = _M0L1iS783 + 1;
      _M0L1iS783 = _M0L6_2atmpS1893;
      continue;
    }
    break;
  }
  return _M0L3arrS781;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS787,
  int32_t _M0L4elemS789
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS786;
  int32_t _M0L1iS788;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS786 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS787);
  _M0L1iS788 = 0;
  while (1) {
    if (_M0L1iS788 < _M0L3lenS787) {
      uint8_t* _M0L3bufS1894 = _M0L3arrS786->$0;
      int32_t _M0L6_2atmpS1895;
      _M0L3bufS1894[_M0L1iS788] = _M0L4elemS789;
      _M0L6_2atmpS1895 = _M0L1iS788 + 1;
      _M0L1iS788 = _M0L6_2atmpS1895;
      continue;
    }
    break;
  }
  return _M0L3arrS786;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS792,
  int32_t _M0L4elemS794
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS791;
  int32_t _M0L1iS793;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS791 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS792);
  _M0L1iS793 = 0;
  while (1) {
    if (_M0L1iS793 < _M0L3lenS792) {
      int32_t* _M0L3bufS1896 = _M0L3arrS791->$0;
      int32_t _M0L6_2atmpS1897;
      _M0L3bufS1896[_M0L1iS793] = _M0L4elemS794;
      _M0L6_2atmpS1897 = _M0L1iS793 + 1;
      _M0L1iS793 = _M0L6_2atmpS1897;
      continue;
    }
    break;
  }
  return _M0L3arrS791;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS770,
  int32_t _M0L5indexS771,
  float _M0L5valueS772
) {
  int32_t _M0L3lenS769;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS769 = _M0L4selfS770->$1;
  if (_M0L5indexS771 >= 0 && _M0L5indexS771 < _M0L3lenS769) {
    float* _M0L6_2atmpS1889;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1889 = _M0MPC15array5Array6bufferGfE(_M0L4selfS770);
    _M0L6_2atmpS1889[_M0L5indexS771] = _M0L5valueS772;
    moonbit_decref_cycle_free(_M0L6_2atmpS1889);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS774,
  int32_t _M0L5indexS775,
  int32_t _M0L5valueS776
) {
  int32_t _M0L3lenS773;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS773 = _M0L4selfS774->$1;
  if (_M0L5indexS775 >= 0 && _M0L5indexS775 < _M0L3lenS773) {
    int32_t* _M0L6_2atmpS1890;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1890 = _M0MPC15array5Array6bufferGiE(_M0L4selfS774);
    _M0L6_2atmpS1890[_M0L5indexS775] = _M0L5valueS776;
    moonbit_decref_cycle_free(_M0L6_2atmpS1890);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS778,
  int32_t _M0L5indexS779,
  int32_t _M0L5valueS780
) {
  int32_t _M0L3lenS777;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS777 = _M0L4selfS778->$1;
  if (_M0L5indexS779 >= 0 && _M0L5indexS779 < _M0L3lenS777) {
    uint8_t* _M0L6_2atmpS1891;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1891 = _M0MPC15array5Array6bufferGbE(_M0L4selfS778);
    _M0L6_2atmpS1891[_M0L5indexS779] = _M0L5valueS780;
    moonbit_decref_cycle_free(_M0L6_2atmpS1891);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS758,
  int32_t _M0L5indexS759
) {
  int32_t _M0L3lenS757;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS757 = _M0L4selfS758->$1;
  if (_M0L5indexS759 >= 0 && _M0L5indexS759 < _M0L3lenS757) {
    uint8_t* _M0L6_2atmpS1885;
    int32_t _result_2167;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1885 = _M0MPC15array5Array6bufferGbE(_M0L4selfS758);
    _result_2167 = (int32_t)_M0L6_2atmpS1885[_M0L5indexS759];
    moonbit_decref_cycle_free(_M0L6_2atmpS1885);
    return _result_2167;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS761,
  int32_t _M0L5indexS762
) {
  int32_t _M0L3lenS760;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS760 = _M0L4selfS761->$1;
  if (_M0L5indexS762 >= 0 && _M0L5indexS762 < _M0L3lenS760) {
    float* _M0L6_2atmpS1886;
    float _result_2168;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1886 = _M0MPC15array5Array6bufferGfE(_M0L4selfS761);
    _result_2168 = (float)_M0L6_2atmpS1886[_M0L5indexS762];
    moonbit_decref_cycle_free(_M0L6_2atmpS1886);
    return _result_2168;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS764,
  int32_t _M0L5indexS765
) {
  int32_t _M0L3lenS763;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS763 = _M0L4selfS764->$1;
  if (_M0L5indexS765 >= 0 && _M0L5indexS765 < _M0L3lenS763) {
    int32_t* _M0L6_2atmpS1887;
    int32_t _result_2169;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1887 = _M0MPC15array5Array6bufferGiE(_M0L4selfS764);
    _result_2169 = (int32_t)_M0L6_2atmpS1887[_M0L5indexS765];
    moonbit_decref_cycle_free(_M0L6_2atmpS1887);
    return _result_2169;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS767,
  int32_t _M0L5indexS768
) {
  int32_t _M0L3lenS766;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS766 = _M0L4selfS767->$1;
  if (_M0L5indexS768 >= 0 && _M0L5indexS768 < _M0L3lenS766) {
    moonbit_string_t* _M0L6_2atmpS1888;
    moonbit_string_t _M0L6_2atmpS2098;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1888 = _M0MPC15array5Array6bufferGsE(_M0L4selfS767);
    _M0L6_2atmpS2098 = (moonbit_string_t)_M0L6_2atmpS1888[_M0L5indexS768];
    moonbit_incref_cycle_free(_M0L6_2atmpS2098);
    moonbit_decref_cycle_free(_M0L6_2atmpS1888);
    return _M0L6_2atmpS2098;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS756) {
  moonbit_string_t _M0L6_2atmpS1884;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1884 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS756);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1884);
  moonbit_decref_cycle_free(_M0L6_2atmpS1884);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS755) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS755);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS740) {
  uint64_t _M0L4bitsS743;
  uint64_t _M0L6_2atmpS1883;
  uint64_t _M0L6_2atmpS1882;
  int32_t _M0L8ieeeSignS744;
  uint64_t _M0L12ieeeMantissaS745;
  uint64_t _M0L6_2atmpS1881;
  uint64_t _M0L6_2atmpS1880;
  int32_t _M0L12ieeeExponentS746;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS747;
  struct _M0TPB17FloatingDecimal64* _M0L1vS748;
  moonbit_string_t _result_2171;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS740 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS740 >= -0x1p+53 && _M0L3valS740 <= 0x1p+53) {
    if (_M0L3valS740 >= -0x1p+31 && _M0L3valS740 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS741;
      double _M0L6_2atmpS1869;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS741 = _M0MPC16double6Double7to__int(_M0L3valS740);
      _M0L6_2atmpS1869 = (double)_M0L1iS741;
      if (_M0L6_2atmpS1869 == _M0L3valS740) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS741, 10);
      }
    } else {
      int64_t _M0L1iS742;
      double _M0L6_2atmpS1870;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS742 = _M0MPC16double6Double9to__int64(_M0L3valS740);
      _M0L6_2atmpS1870 = (double)_M0L1iS742;
      if (_M0L6_2atmpS1870 == _M0L3valS740) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS742, 10);
      }
    }
  }
  _M0L4bitsS743 = *(int64_t*)&_M0L3valS740;
  _M0L6_2atmpS1883 = _M0L4bitsS743 >> 63;
  _M0L6_2atmpS1882 = _M0L6_2atmpS1883 & 1ull;
  _M0L8ieeeSignS744 = _M0L6_2atmpS1882 != 0ull;
  _M0L12ieeeMantissaS745 = _M0L4bitsS743 & 4503599627370495ull;
  _M0L6_2atmpS1881 = _M0L4bitsS743 >> 52;
  _M0L6_2atmpS1880 = _M0L6_2atmpS1881 & 2047ull;
  _M0L12ieeeExponentS746 = (int32_t)_M0L6_2atmpS1880;
  if (
    _M0L12ieeeExponentS746 == 2047
    || _M0L12ieeeExponentS746 == 0 && _M0L12ieeeMantissaS745 == 0ull
  ) {
    int32_t _M0L6_2atmpS1871 = _M0L12ieeeExponentS746 != 0;
    int32_t _M0L6_2atmpS1872 = _M0L12ieeeMantissaS745 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS744, _M0L6_2atmpS1871, _M0L6_2atmpS1872);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS747
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS745, _M0L12ieeeExponentS746);
  if (_M0L7_2abindS747 == 0) {
    uint32_t _M0L6_2atmpS1873;
    if (_M0L7_2abindS747) {
      moonbit_decref_cycle_free(_M0L7_2abindS747);
    }
    _M0L6_2atmpS1873 = *(uint32_t*)&_M0L12ieeeExponentS746;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS748 = _M0FPB3d2d(_M0L12ieeeMantissaS745, _M0L6_2atmpS1873);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS749 = _M0L7_2abindS747;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS750 = _M0L7_2aSomeS749;
    struct _M0TPB17FloatingDecimal64* _M0L1xS751 = _M0L4_2afS750;
    while (1) {
      uint64_t _M0L8mantissaS1879 = _M0L1xS751->$0;
      uint64_t _M0L1qS752 = _M0L8mantissaS1879 / 10ull;
      uint64_t _M0L8mantissaS1877 = _M0L1xS751->$0;
      uint64_t _M0L6_2atmpS1878 = 10ull * _M0L1qS752;
      uint64_t _M0L1rS753 = _M0L8mantissaS1877 - _M0L6_2atmpS1878;
      int32_t _M0L8exponentS1876;
      int32_t _M0L6_2atmpS1875;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1874;
      if (_M0L1rS753 != 0ull) {
        _M0L1vS748 = _M0L1xS751;
        break;
      }
      _M0L8exponentS1876 = _M0L1xS751->$1;
      moonbit_decref_cycle_free(_M0L1xS751);
      _M0L6_2atmpS1875 = _M0L8exponentS1876 + 1;
      _M0L6_2atmpS1874
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1874)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1874->$0 = _M0L1qS752;
      _M0L6_2atmpS1874->$1 = _M0L6_2atmpS1875;
      _M0L1xS751 = _M0L6_2atmpS1874;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2171 = _M0FPB9to__chars(_M0L1vS748, _M0L8ieeeSignS744);
  moonbit_decref_cycle_free(_M0L1vS748);
  return _result_2171;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS735,
  int32_t _M0L12ieeeExponentS737
) {
  uint64_t _M0L2m2S734;
  int32_t _M0L6_2atmpS1868;
  int32_t _M0L2e2S736;
  int32_t _M0L6_2atmpS1867;
  uint64_t _M0L6_2atmpS1866;
  uint64_t _M0L4maskS738;
  uint64_t _M0L8fractionS739;
  int32_t _M0L6_2atmpS1865;
  uint64_t _M0L6_2atmpS1864;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1863;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S734 = 4503599627370496ull | _M0L12ieeeMantissaS735;
  _M0L6_2atmpS1868 = _M0L12ieeeExponentS737 - 1023;
  _M0L2e2S736 = _M0L6_2atmpS1868 - 52;
  if (_M0L2e2S736 > 0) {
    return 0;
  }
  if (_M0L2e2S736 < -52) {
    return 0;
  }
  _M0L6_2atmpS1867 = -_M0L2e2S736;
  _M0L6_2atmpS1866 = 1ull << (_M0L6_2atmpS1867 & 63);
  _M0L4maskS738 = _M0L6_2atmpS1866 - 1ull;
  _M0L8fractionS739 = _M0L2m2S734 & _M0L4maskS738;
  if (_M0L8fractionS739 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1865 = -_M0L2e2S736;
  _M0L6_2atmpS1864 = _M0L2m2S734 >> (_M0L6_2atmpS1865 & 63);
  _M0L6_2atmpS1863
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1863)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1863->$0 = _M0L6_2atmpS1864;
  _M0L6_2atmpS1863->$1 = 0;
  return _M0L6_2atmpS1863;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS702,
  int32_t _M0L4signS700
) {
  moonbit_bytes_t _M0L6resultS698;
  int32_t _M0Lm5indexS699;
  uint64_t _M0L6outputS701;
  int32_t _M0L7olengthS703;
  int32_t _M0L8exponentS1862;
  int32_t _M0L6_2atmpS1861;
  int32_t _M0Lm3expS704;
  int32_t _M0L6_2atmpS1860;
  int32_t _M0L6_2atmpS1858;
  int32_t _M0L18scientificNotationS705;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS698 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS699 = 0;
  if (_M0L4signS700) {
    int32_t _M0L6_2atmpS1732 = _M0Lm5indexS699;
    int32_t _M0L6_2atmpS1733;
    if (
      _M0L6_2atmpS1732 < 0
      || _M0L6_2atmpS1732 >= Moonbit_array_length(_M0L6resultS698)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS698[_M0L6_2atmpS1732] = 45;
    _M0L6_2atmpS1733 = _M0Lm5indexS699;
    _M0Lm5indexS699 = _M0L6_2atmpS1733 + 1;
  }
  _M0L6outputS701 = _M0L1vS702->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS703 = _M0FPB17decimal__length17(_M0L6outputS701);
  _M0L8exponentS1862 = _M0L1vS702->$1;
  _M0L6_2atmpS1861 = _M0L8exponentS1862 + _M0L7olengthS703;
  _M0Lm3expS704 = _M0L6_2atmpS1861 - 1;
  _M0L6_2atmpS1860 = _M0Lm3expS704;
  if (_M0L6_2atmpS1860 >= -6) {
    int32_t _M0L6_2atmpS1859 = _M0Lm3expS704;
    _M0L6_2atmpS1858 = _M0L6_2atmpS1859 < 21;
  } else {
    _M0L6_2atmpS1858 = 0;
  }
  _M0L18scientificNotationS705 = !_M0L6_2atmpS1858;
  if (_M0L18scientificNotationS705) {
    int32_t _M0L7_2abindS706 = _M0L7olengthS703 - 1;
    uint64_t _M0L6outputS707;
    int32_t _M0L1iS708 = 0;
    uint64_t _M0L6outputS709 = _M0L6outputS701;
    int32_t _M0L6_2atmpS1734;
    int32_t _M0L6_2atmpS1738;
    int32_t _M0L6_2atmpS1737;
    int32_t _M0L6_2atmpS1736;
    int32_t _M0L6_2atmpS1735;
    int32_t _M0L6_2atmpS1742;
    int32_t _M0L6_2atmpS1743;
    int32_t _M0L6_2atmpS1744;
    int32_t _M0L6_2atmpS1745;
    int32_t _M0L6_2atmpS1746;
    int32_t _M0L6_2atmpS1752;
    int32_t _M0L6_2atmpS1785;
    moonbit_string_t _result_2173;
    while (1) {
      if (_M0L1iS708 < _M0L7_2abindS706) {
        uint64_t _M0L1cS710 = _M0L6outputS709 % 10ull;
        int32_t _M0L6_2atmpS1791 = _M0Lm5indexS699;
        int32_t _M0L6_2atmpS1790 = _M0L6_2atmpS1791 + _M0L7olengthS703;
        int32_t _M0L6_2atmpS1786 = _M0L6_2atmpS1790 - _M0L1iS708;
        int32_t _M0L6_2atmpS1789 = (int32_t)_M0L1cS710;
        int32_t _M0L6_2atmpS1788 = 48 + _M0L6_2atmpS1789;
        int32_t _M0L6_2atmpS1787 = _M0L6_2atmpS1788 & 0xff;
        int32_t _M0L6_2atmpS1792;
        uint64_t _M0L6_2atmpS1793;
        if (
          _M0L6_2atmpS1786 < 0
          || _M0L6_2atmpS1786 >= Moonbit_array_length(_M0L6resultS698)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS698[_M0L6_2atmpS1786] = _M0L6_2atmpS1787;
        _M0L6_2atmpS1792 = _M0L1iS708 + 1;
        _M0L6_2atmpS1793 = _M0L6outputS709 / 10ull;
        _M0L1iS708 = _M0L6_2atmpS1792;
        _M0L6outputS709 = _M0L6_2atmpS1793;
        continue;
      } else {
        _M0L6outputS707 = _M0L6outputS709;
      }
      break;
    }
    _M0L6_2atmpS1734 = _M0Lm5indexS699;
    _M0L6_2atmpS1738 = (int32_t)_M0L6outputS707;
    _M0L6_2atmpS1737 = _M0L6_2atmpS1738 % 10;
    _M0L6_2atmpS1736 = 48 + _M0L6_2atmpS1737;
    _M0L6_2atmpS1735 = _M0L6_2atmpS1736 & 0xff;
    if (
      _M0L6_2atmpS1734 < 0
      || _M0L6_2atmpS1734 >= Moonbit_array_length(_M0L6resultS698)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS698[_M0L6_2atmpS1734] = _M0L6_2atmpS1735;
    if (_M0L7olengthS703 > 1) {
      int32_t _M0L6_2atmpS1740 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1739 = _M0L6_2atmpS1740 + 1;
      if (
        _M0L6_2atmpS1739 < 0
        || _M0L6_2atmpS1739 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1739] = 46;
    } else {
      int32_t _M0L6_2atmpS1741 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1741 - 1;
    }
    _M0L6_2atmpS1742 = _M0Lm5indexS699;
    _M0L6_2atmpS1743 = _M0L7olengthS703 + 1;
    _M0Lm5indexS699 = _M0L6_2atmpS1742 + _M0L6_2atmpS1743;
    _M0L6_2atmpS1744 = _M0Lm5indexS699;
    if (
      _M0L6_2atmpS1744 < 0
      || _M0L6_2atmpS1744 >= Moonbit_array_length(_M0L6resultS698)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS698[_M0L6_2atmpS1744] = 101;
    _M0L6_2atmpS1745 = _M0Lm5indexS699;
    _M0Lm5indexS699 = _M0L6_2atmpS1745 + 1;
    _M0L6_2atmpS1746 = _M0Lm3expS704;
    if (_M0L6_2atmpS1746 < 0) {
      int32_t _M0L6_2atmpS1747 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1748;
      int32_t _M0L6_2atmpS1749;
      if (
        _M0L6_2atmpS1747 < 0
        || _M0L6_2atmpS1747 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1747] = 45;
      _M0L6_2atmpS1748 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1748 + 1;
      _M0L6_2atmpS1749 = _M0Lm3expS704;
      _M0Lm3expS704 = -_M0L6_2atmpS1749;
    } else {
      int32_t _M0L6_2atmpS1750 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1751;
      if (
        _M0L6_2atmpS1750 < 0
        || _M0L6_2atmpS1750 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1750] = 43;
      _M0L6_2atmpS1751 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1751 + 1;
    }
    _M0L6_2atmpS1752 = _M0Lm3expS704;
    if (_M0L6_2atmpS1752 >= 100) {
      int32_t _M0L6_2atmpS1768 = _M0Lm3expS704;
      int32_t _M0L1aS712 = _M0L6_2atmpS1768 / 100;
      int32_t _M0L6_2atmpS1767 = _M0Lm3expS704;
      int32_t _M0L6_2atmpS1766 = _M0L6_2atmpS1767 / 10;
      int32_t _M0L1bS713 = _M0L6_2atmpS1766 % 10;
      int32_t _M0L6_2atmpS1765 = _M0Lm3expS704;
      int32_t _M0L1cS714 = _M0L6_2atmpS1765 % 10;
      int32_t _M0L6_2atmpS1753 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1755 = 48 + _M0L1aS712;
      int32_t _M0L6_2atmpS1754 = _M0L6_2atmpS1755 & 0xff;
      int32_t _M0L6_2atmpS1759;
      int32_t _M0L6_2atmpS1756;
      int32_t _M0L6_2atmpS1758;
      int32_t _M0L6_2atmpS1757;
      int32_t _M0L6_2atmpS1763;
      int32_t _M0L6_2atmpS1760;
      int32_t _M0L6_2atmpS1762;
      int32_t _M0L6_2atmpS1761;
      int32_t _M0L6_2atmpS1764;
      if (
        _M0L6_2atmpS1753 < 0
        || _M0L6_2atmpS1753 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1753] = _M0L6_2atmpS1754;
      _M0L6_2atmpS1759 = _M0Lm5indexS699;
      _M0L6_2atmpS1756 = _M0L6_2atmpS1759 + 1;
      _M0L6_2atmpS1758 = 48 + _M0L1bS713;
      _M0L6_2atmpS1757 = _M0L6_2atmpS1758 & 0xff;
      if (
        _M0L6_2atmpS1756 < 0
        || _M0L6_2atmpS1756 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1756] = _M0L6_2atmpS1757;
      _M0L6_2atmpS1763 = _M0Lm5indexS699;
      _M0L6_2atmpS1760 = _M0L6_2atmpS1763 + 2;
      _M0L6_2atmpS1762 = 48 + _M0L1cS714;
      _M0L6_2atmpS1761 = _M0L6_2atmpS1762 & 0xff;
      if (
        _M0L6_2atmpS1760 < 0
        || _M0L6_2atmpS1760 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1760] = _M0L6_2atmpS1761;
      _M0L6_2atmpS1764 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1764 + 3;
    } else {
      int32_t _M0L6_2atmpS1769 = _M0Lm3expS704;
      if (_M0L6_2atmpS1769 >= 10) {
        int32_t _M0L6_2atmpS1779 = _M0Lm3expS704;
        int32_t _M0L1aS715 = _M0L6_2atmpS1779 / 10;
        int32_t _M0L6_2atmpS1778 = _M0Lm3expS704;
        int32_t _M0L1bS716 = _M0L6_2atmpS1778 % 10;
        int32_t _M0L6_2atmpS1770 = _M0Lm5indexS699;
        int32_t _M0L6_2atmpS1772 = 48 + _M0L1aS715;
        int32_t _M0L6_2atmpS1771 = _M0L6_2atmpS1772 & 0xff;
        int32_t _M0L6_2atmpS1776;
        int32_t _M0L6_2atmpS1773;
        int32_t _M0L6_2atmpS1775;
        int32_t _M0L6_2atmpS1774;
        int32_t _M0L6_2atmpS1777;
        if (
          _M0L6_2atmpS1770 < 0
          || _M0L6_2atmpS1770 >= Moonbit_array_length(_M0L6resultS698)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS698[_M0L6_2atmpS1770] = _M0L6_2atmpS1771;
        _M0L6_2atmpS1776 = _M0Lm5indexS699;
        _M0L6_2atmpS1773 = _M0L6_2atmpS1776 + 1;
        _M0L6_2atmpS1775 = 48 + _M0L1bS716;
        _M0L6_2atmpS1774 = _M0L6_2atmpS1775 & 0xff;
        if (
          _M0L6_2atmpS1773 < 0
          || _M0L6_2atmpS1773 >= Moonbit_array_length(_M0L6resultS698)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS698[_M0L6_2atmpS1773] = _M0L6_2atmpS1774;
        _M0L6_2atmpS1777 = _M0Lm5indexS699;
        _M0Lm5indexS699 = _M0L6_2atmpS1777 + 2;
      } else {
        int32_t _M0L6_2atmpS1780 = _M0Lm5indexS699;
        int32_t _M0L6_2atmpS1783 = _M0Lm3expS704;
        int32_t _M0L6_2atmpS1782 = 48 + _M0L6_2atmpS1783;
        int32_t _M0L6_2atmpS1781 = _M0L6_2atmpS1782 & 0xff;
        int32_t _M0L6_2atmpS1784;
        if (
          _M0L6_2atmpS1780 < 0
          || _M0L6_2atmpS1780 >= Moonbit_array_length(_M0L6resultS698)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS698[_M0L6_2atmpS1780] = _M0L6_2atmpS1781;
        _M0L6_2atmpS1784 = _M0Lm5indexS699;
        _M0Lm5indexS699 = _M0L6_2atmpS1784 + 1;
      }
    }
    _M0L6_2atmpS1785 = _M0Lm5indexS699;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2173
    = _M0FPB19string__from__bytes(_M0L6resultS698, 0, _M0L6_2atmpS1785);
    moonbit_decref_cycle_free(_M0L6resultS698);
    return _result_2173;
  } else {
    int32_t _M0L6_2atmpS1794 = _M0Lm3expS704;
    int32_t _M0L6_2atmpS1857;
    moonbit_string_t _result_2179;
    if (_M0L6_2atmpS1794 < 0) {
      int32_t _M0L6_2atmpS1795 = _M0Lm5indexS699;
      int32_t _M0L6_2atmpS1797;
      int32_t _M0L6_2atmpS1796;
      int32_t _M0L6_2atmpS1798;
      int32_t _M0L1iS717;
      int32_t _M0L6_2atmpS1813;
      int32_t _M0L6_2atmpS1815;
      int32_t _M0L6_2atmpS1814;
      int32_t _M0L7currentS719;
      int32_t _M0L1iS720;
      uint64_t _M0L6outputS721;
      if (
        _M0L6_2atmpS1795 < 0
        || _M0L6_2atmpS1795 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1795] = 48;
      _M0L6_2atmpS1797 = _M0Lm5indexS699;
      _M0L6_2atmpS1796 = _M0L6_2atmpS1797 + 1;
      if (
        _M0L6_2atmpS1796 < 0
        || _M0L6_2atmpS1796 >= Moonbit_array_length(_M0L6resultS698)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS698[_M0L6_2atmpS1796] = 46;
      _M0L6_2atmpS1798 = _M0Lm5indexS699;
      _M0Lm5indexS699 = _M0L6_2atmpS1798 + 2;
      _M0L1iS717 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1799 = _M0Lm3expS704;
        if (_M0L1iS717 > _M0L6_2atmpS1799) {
          int32_t _M0L6_2atmpS1802 = _M0Lm5indexS699;
          int32_t _M0L6_2atmpS1801 = _M0L6_2atmpS1802 - _M0L1iS717;
          int32_t _M0L6_2atmpS1800 = _M0L6_2atmpS1801 - 1;
          int32_t _M0L6_2atmpS1803;
          if (
            _M0L6_2atmpS1800 < 0
            || _M0L6_2atmpS1800 >= Moonbit_array_length(_M0L6resultS698)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS698[_M0L6_2atmpS1800] = 48;
          _M0L6_2atmpS1803 = _M0L1iS717 - 1;
          _M0L1iS717 = _M0L6_2atmpS1803;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1813 = _M0Lm5indexS699;
      _M0L6_2atmpS1815 = _M0Lm3expS704;
      _M0L6_2atmpS1814 = -1 - _M0L6_2atmpS1815;
      _M0L7currentS719 = _M0L6_2atmpS1813 + _M0L6_2atmpS1814;
      _M0L1iS720 = 0;
      _M0L6outputS721 = _M0L6outputS701;
      while (1) {
        if (_M0L1iS720 < _M0L7olengthS703) {
          int32_t _M0L6_2atmpS1810 = _M0L7currentS719 + _M0L7olengthS703;
          int32_t _M0L6_2atmpS1809 = _M0L6_2atmpS1810 - _M0L1iS720;
          int32_t _M0L6_2atmpS1804 = _M0L6_2atmpS1809 - 1;
          uint64_t _M0L6_2atmpS1808 = _M0L6outputS721 % 10ull;
          int32_t _M0L6_2atmpS1807 = (int32_t)_M0L6_2atmpS1808;
          int32_t _M0L6_2atmpS1806 = 48 + _M0L6_2atmpS1807;
          int32_t _M0L6_2atmpS1805 = _M0L6_2atmpS1806 & 0xff;
          int32_t _M0L6_2atmpS1811;
          uint64_t _M0L6_2atmpS1812;
          if (
            _M0L6_2atmpS1804 < 0
            || _M0L6_2atmpS1804 >= Moonbit_array_length(_M0L6resultS698)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS698[_M0L6_2atmpS1804] = _M0L6_2atmpS1805;
          _M0L6_2atmpS1811 = _M0L1iS720 + 1;
          _M0L6_2atmpS1812 = _M0L6outputS721 / 10ull;
          _M0L1iS720 = _M0L6_2atmpS1811;
          _M0L6outputS721 = _M0L6_2atmpS1812;
          continue;
        }
        break;
      }
      _M0Lm5indexS699 = _M0L7currentS719 + _M0L7olengthS703;
    } else {
      int32_t _M0L6_2atmpS1817 = _M0Lm3expS704;
      int32_t _M0L6_2atmpS1816 = _M0L6_2atmpS1817 + 1;
      if (_M0L6_2atmpS1816 >= _M0L7olengthS703) {
        int32_t _M0L1iS723 = 0;
        uint64_t _M0L6outputS724 = _M0L6outputS701;
        int32_t _M0L6_2atmpS1828;
        int32_t _M0L6_2atmpS1833;
        int32_t _M0L7_2abindS726;
        int32_t _M0L1iS727;
        int32_t _M0L6_2atmpS1834;
        int32_t _M0L6_2atmpS1837;
        int32_t _M0L6_2atmpS1836;
        int32_t _M0L6_2atmpS1835;
        while (1) {
          if (_M0L1iS723 < _M0L7olengthS703) {
            int32_t _M0L6_2atmpS1825 = _M0Lm5indexS699;
            int32_t _M0L6_2atmpS1824 = _M0L6_2atmpS1825 + _M0L7olengthS703;
            int32_t _M0L6_2atmpS1823 = _M0L6_2atmpS1824 - _M0L1iS723;
            int32_t _M0L6_2atmpS1818 = _M0L6_2atmpS1823 - 1;
            uint64_t _M0L6_2atmpS1822 = _M0L6outputS724 % 10ull;
            int32_t _M0L6_2atmpS1821 = (int32_t)_M0L6_2atmpS1822;
            int32_t _M0L6_2atmpS1820 = 48 + _M0L6_2atmpS1821;
            int32_t _M0L6_2atmpS1819 = _M0L6_2atmpS1820 & 0xff;
            int32_t _M0L6_2atmpS1826;
            uint64_t _M0L6_2atmpS1827;
            if (
              _M0L6_2atmpS1818 < 0
              || _M0L6_2atmpS1818 >= Moonbit_array_length(_M0L6resultS698)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS698[_M0L6_2atmpS1818] = _M0L6_2atmpS1819;
            _M0L6_2atmpS1826 = _M0L1iS723 + 1;
            _M0L6_2atmpS1827 = _M0L6outputS724 / 10ull;
            _M0L1iS723 = _M0L6_2atmpS1826;
            _M0L6outputS724 = _M0L6_2atmpS1827;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1828 = _M0Lm5indexS699;
        _M0Lm5indexS699 = _M0L6_2atmpS1828 + _M0L7olengthS703;
        _M0L6_2atmpS1833 = _M0Lm3expS704;
        _M0L7_2abindS726 = _M0L6_2atmpS1833 + 1;
        _M0L1iS727 = _M0L7olengthS703;
        while (1) {
          if (_M0L1iS727 < _M0L7_2abindS726) {
            int32_t _M0L6_2atmpS1831 = _M0Lm5indexS699;
            int32_t _M0L6_2atmpS1830 = _M0L6_2atmpS1831 + _M0L1iS727;
            int32_t _M0L6_2atmpS1829 = _M0L6_2atmpS1830 - _M0L7olengthS703;
            int32_t _M0L6_2atmpS1832;
            if (
              _M0L6_2atmpS1829 < 0
              || _M0L6_2atmpS1829 >= Moonbit_array_length(_M0L6resultS698)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS698[_M0L6_2atmpS1829] = 48;
            _M0L6_2atmpS1832 = _M0L1iS727 + 1;
            _M0L1iS727 = _M0L6_2atmpS1832;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1834 = _M0Lm5indexS699;
        _M0L6_2atmpS1837 = _M0Lm3expS704;
        _M0L6_2atmpS1836 = _M0L6_2atmpS1837 + 1;
        _M0L6_2atmpS1835 = _M0L6_2atmpS1836 - _M0L7olengthS703;
        _M0Lm5indexS699 = _M0L6_2atmpS1834 + _M0L6_2atmpS1835;
      } else {
        int32_t _M0L6_2atmpS1854 = _M0Lm5indexS699;
        int32_t _M0L6_2atmpS1853 = _M0L6_2atmpS1854 + 1;
        int32_t _M0L1iS729 = 0;
        int32_t _M0L7currentS730 = _M0L6_2atmpS1853;
        uint64_t _M0L6outputS731 = _M0L6outputS701;
        int32_t _M0L6_2atmpS1855;
        int32_t _M0L6_2atmpS1856;
        while (1) {
          if (_M0L1iS729 < _M0L7olengthS703) {
            int32_t _M0L6_2atmpS1849 = _M0L7olengthS703 - _M0L1iS729;
            int32_t _M0L6_2atmpS1847 = _M0L6_2atmpS1849 - 1;
            int32_t _M0L6_2atmpS1848 = _M0Lm3expS704;
            int32_t _M0L7currentS732;
            int32_t _M0L6_2atmpS1844;
            int32_t _M0L6_2atmpS1843;
            int32_t _M0L6_2atmpS1838;
            uint64_t _M0L6_2atmpS1842;
            int32_t _M0L6_2atmpS1841;
            int32_t _M0L6_2atmpS1840;
            int32_t _M0L6_2atmpS1839;
            int32_t _M0L6_2atmpS1845;
            uint64_t _M0L6_2atmpS1846;
            if (_M0L6_2atmpS1847 == _M0L6_2atmpS1848) {
              int32_t _M0L6_2atmpS1852 = _M0L7currentS730 + _M0L7olengthS703;
              int32_t _M0L6_2atmpS1851 = _M0L6_2atmpS1852 - _M0L1iS729;
              int32_t _M0L6_2atmpS1850 = _M0L6_2atmpS1851 - 1;
              if (
                _M0L6_2atmpS1850 < 0
                || _M0L6_2atmpS1850 >= Moonbit_array_length(_M0L6resultS698)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS698[_M0L6_2atmpS1850] = 46;
              _M0L7currentS732 = _M0L7currentS730 - 1;
            } else {
              _M0L7currentS732 = _M0L7currentS730;
            }
            _M0L6_2atmpS1844 = _M0L7currentS732 + _M0L7olengthS703;
            _M0L6_2atmpS1843 = _M0L6_2atmpS1844 - _M0L1iS729;
            _M0L6_2atmpS1838 = _M0L6_2atmpS1843 - 1;
            _M0L6_2atmpS1842 = _M0L6outputS731 % 10ull;
            _M0L6_2atmpS1841 = (int32_t)_M0L6_2atmpS1842;
            _M0L6_2atmpS1840 = 48 + _M0L6_2atmpS1841;
            _M0L6_2atmpS1839 = _M0L6_2atmpS1840 & 0xff;
            if (
              _M0L6_2atmpS1838 < 0
              || _M0L6_2atmpS1838 >= Moonbit_array_length(_M0L6resultS698)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS698[_M0L6_2atmpS1838] = _M0L6_2atmpS1839;
            _M0L6_2atmpS1845 = _M0L1iS729 + 1;
            _M0L6_2atmpS1846 = _M0L6outputS731 / 10ull;
            _M0L1iS729 = _M0L6_2atmpS1845;
            _M0L7currentS730 = _M0L7currentS732;
            _M0L6outputS731 = _M0L6_2atmpS1846;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1855 = _M0Lm5indexS699;
        _M0L6_2atmpS1856 = _M0L7olengthS703 + 1;
        _M0Lm5indexS699 = _M0L6_2atmpS1855 + _M0L6_2atmpS1856;
      }
    }
    _M0L6_2atmpS1857 = _M0Lm5indexS699;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2179
    = _M0FPB19string__from__bytes(_M0L6resultS698, 0, _M0L6_2atmpS1857);
    moonbit_decref_cycle_free(_M0L6resultS698);
    return _result_2179;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS644,
  uint32_t _M0L12ieeeExponentS643
) {
  int32_t _M0Lm2e2S641;
  uint64_t _M0Lm2m2S642;
  uint64_t _M0L6_2atmpS1731;
  uint64_t _M0L6_2atmpS1730;
  int32_t _M0L4evenS645;
  uint64_t _M0L6_2atmpS1729;
  uint64_t _M0L2mvS646;
  int32_t _M0L7mmShiftS647;
  uint64_t _M0Lm2vrS648;
  uint64_t _M0Lm2vpS649;
  uint64_t _M0Lm2vmS650;
  int32_t _M0Lm3e10S651;
  int32_t _M0Lm17vmIsTrailingZerosS652;
  int32_t _M0Lm17vrIsTrailingZerosS653;
  int32_t _M0L6_2atmpS1631;
  int32_t _M0Lm7removedS672;
  int32_t _M0Lm16lastRemovedDigitS673;
  uint64_t _M0Lm6outputS674;
  int32_t _M0L6_2atmpS1727;
  int32_t _M0L6_2atmpS1728;
  int32_t _M0L3expS697;
  uint64_t _M0L6_2atmpS1726;
  struct _M0TPB17FloatingDecimal64* _block_2185;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S641 = 0;
  _M0Lm2m2S642 = 0ull;
  if (_M0L12ieeeExponentS643 == 0u) {
    _M0Lm2e2S641 = -1076;
    _M0Lm2m2S642 = _M0L12ieeeMantissaS644;
  } else {
    int32_t _M0L6_2atmpS1630 = *(int32_t*)&_M0L12ieeeExponentS643;
    int32_t _M0L6_2atmpS1629 = _M0L6_2atmpS1630 - 1023;
    int32_t _M0L6_2atmpS1628 = _M0L6_2atmpS1629 - 52;
    _M0Lm2e2S641 = _M0L6_2atmpS1628 - 2;
    _M0Lm2m2S642 = 4503599627370496ull | _M0L12ieeeMantissaS644;
  }
  _M0L6_2atmpS1731 = _M0Lm2m2S642;
  _M0L6_2atmpS1730 = _M0L6_2atmpS1731 & 1ull;
  _M0L4evenS645 = _M0L6_2atmpS1730 == 0ull;
  _M0L6_2atmpS1729 = _M0Lm2m2S642;
  _M0L2mvS646 = 4ull * _M0L6_2atmpS1729;
  _M0L7mmShiftS647
  = _M0L12ieeeMantissaS644 != 0ull || _M0L12ieeeExponentS643 <= 1u;
  _M0Lm2vrS648 = 0ull;
  _M0Lm2vpS649 = 0ull;
  _M0Lm2vmS650 = 0ull;
  _M0Lm3e10S651 = 0;
  _M0Lm17vmIsTrailingZerosS652 = 0;
  _M0Lm17vrIsTrailingZerosS653 = 0;
  _M0L6_2atmpS1631 = _M0Lm2e2S641;
  if (_M0L6_2atmpS1631 >= 0) {
    int32_t _M0L6_2atmpS1653 = _M0Lm2e2S641;
    int32_t _M0L6_2atmpS1649;
    int32_t _M0L6_2atmpS1652;
    int32_t _M0L6_2atmpS1651;
    int32_t _M0L6_2atmpS1650;
    int32_t _M0L1qS654;
    int32_t _M0L6_2atmpS1648;
    int32_t _M0L6_2atmpS1647;
    int32_t _M0L1kS655;
    int32_t _M0L6_2atmpS1646;
    int32_t _M0L6_2atmpS1645;
    int32_t _M0L6_2atmpS1644;
    int32_t _M0L1iS656;
    struct _M0TPB8Pow5Pair _M0L4pow5S657;
    uint64_t _M0L6_2atmpS1643;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS658;
    uint64_t _M0L8_2avrOutS659;
    uint64_t _M0L8_2avpOutS660;
    uint64_t _M0L8_2avmOutS661;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1649 = _M0FPB9log10Pow2(_M0L6_2atmpS1653);
    _M0L6_2atmpS1652 = _M0Lm2e2S641;
    _M0L6_2atmpS1651 = _M0L6_2atmpS1652 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1650 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1651);
    _M0L1qS654 = _M0L6_2atmpS1649 - _M0L6_2atmpS1650;
    _M0Lm3e10S651 = _M0L1qS654;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1648 = _M0FPB8pow5bits(_M0L1qS654);
    _M0L6_2atmpS1647 = 125 + _M0L6_2atmpS1648;
    _M0L1kS655 = _M0L6_2atmpS1647 - 1;
    _M0L6_2atmpS1646 = _M0Lm2e2S641;
    _M0L6_2atmpS1645 = -_M0L6_2atmpS1646;
    _M0L6_2atmpS1644 = _M0L6_2atmpS1645 + _M0L1qS654;
    _M0L1iS656 = _M0L6_2atmpS1644 + _M0L1kS655;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S657 = _M0FPB22double__computeInvPow5(_M0L1qS654);
    _M0L6_2atmpS1643 = _M0Lm2m2S642;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS658
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1643, _M0L4pow5S657, _M0L1iS656, _M0L7mmShiftS647);
    _M0L8_2avrOutS659 = _M0L7_2abindS658.$0;
    _M0L8_2avpOutS660 = _M0L7_2abindS658.$1;
    _M0L8_2avmOutS661 = _M0L7_2abindS658.$2;
    _M0Lm2vrS648 = _M0L8_2avrOutS659;
    _M0Lm2vpS649 = _M0L8_2avpOutS660;
    _M0Lm2vmS650 = _M0L8_2avmOutS661;
    if (_M0L1qS654 <= 21) {
      int32_t _M0L6_2atmpS1639 = (int32_t)_M0L2mvS646;
      uint64_t _M0L6_2atmpS1642 = _M0L2mvS646 / 5ull;
      int32_t _M0L6_2atmpS1641 = (int32_t)_M0L6_2atmpS1642;
      int32_t _M0L6_2atmpS1640 = 5 * _M0L6_2atmpS1641;
      int32_t _M0L6mvMod5S662 = _M0L6_2atmpS1639 - _M0L6_2atmpS1640;
      if (_M0L6mvMod5S662 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS653
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS646, _M0L1qS654);
      } else if (_M0L4evenS645) {
        uint64_t _M0L6_2atmpS1633 = _M0L2mvS646 - 1ull;
        uint64_t _M0L6_2atmpS1634;
        uint64_t _M0L6_2atmpS1632;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1634 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS647);
        _M0L6_2atmpS1632 = _M0L6_2atmpS1633 - _M0L6_2atmpS1634;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS652
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1632, _M0L1qS654);
      } else {
        uint64_t _M0L6_2atmpS1635 = _M0Lm2vpS649;
        uint64_t _M0L6_2atmpS1638 = _M0L2mvS646 + 2ull;
        int32_t _M0L6_2atmpS1637;
        uint64_t _M0L6_2atmpS1636;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1637
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1638, _M0L1qS654);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1636 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1637);
        _M0Lm2vpS649 = _M0L6_2atmpS1635 - _M0L6_2atmpS1636;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1667 = _M0Lm2e2S641;
    int32_t _M0L6_2atmpS1666 = -_M0L6_2atmpS1667;
    int32_t _M0L6_2atmpS1661;
    int32_t _M0L6_2atmpS1665;
    int32_t _M0L6_2atmpS1664;
    int32_t _M0L6_2atmpS1663;
    int32_t _M0L6_2atmpS1662;
    int32_t _M0L1qS663;
    int32_t _M0L6_2atmpS1654;
    int32_t _M0L6_2atmpS1660;
    int32_t _M0L6_2atmpS1659;
    int32_t _M0L1iS664;
    int32_t _M0L6_2atmpS1658;
    int32_t _M0L1kS665;
    int32_t _M0L1jS666;
    struct _M0TPB8Pow5Pair _M0L4pow5S667;
    uint64_t _M0L6_2atmpS1657;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS668;
    uint64_t _M0L8_2avrOutS669;
    uint64_t _M0L8_2avpOutS670;
    uint64_t _M0L8_2avmOutS671;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1661 = _M0FPB9log10Pow5(_M0L6_2atmpS1666);
    _M0L6_2atmpS1665 = _M0Lm2e2S641;
    _M0L6_2atmpS1664 = -_M0L6_2atmpS1665;
    _M0L6_2atmpS1663 = _M0L6_2atmpS1664 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1662 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1663);
    _M0L1qS663 = _M0L6_2atmpS1661 - _M0L6_2atmpS1662;
    _M0L6_2atmpS1654 = _M0Lm2e2S641;
    _M0Lm3e10S651 = _M0L1qS663 + _M0L6_2atmpS1654;
    _M0L6_2atmpS1660 = _M0Lm2e2S641;
    _M0L6_2atmpS1659 = -_M0L6_2atmpS1660;
    _M0L1iS664 = _M0L6_2atmpS1659 - _M0L1qS663;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1658 = _M0FPB8pow5bits(_M0L1iS664);
    _M0L1kS665 = _M0L6_2atmpS1658 - 125;
    _M0L1jS666 = _M0L1qS663 - _M0L1kS665;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S667 = _M0FPB19double__computePow5(_M0L1iS664);
    _M0L6_2atmpS1657 = _M0Lm2m2S642;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS668
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1657, _M0L4pow5S667, _M0L1jS666, _M0L7mmShiftS647);
    _M0L8_2avrOutS669 = _M0L7_2abindS668.$0;
    _M0L8_2avpOutS670 = _M0L7_2abindS668.$1;
    _M0L8_2avmOutS671 = _M0L7_2abindS668.$2;
    _M0Lm2vrS648 = _M0L8_2avrOutS669;
    _M0Lm2vpS649 = _M0L8_2avpOutS670;
    _M0Lm2vmS650 = _M0L8_2avmOutS671;
    if (_M0L1qS663 <= 1) {
      _M0Lm17vrIsTrailingZerosS653 = 1;
      if (_M0L4evenS645) {
        int32_t _M0L6_2atmpS1655;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1655 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS647);
        _M0Lm17vmIsTrailingZerosS652 = _M0L6_2atmpS1655 == 1;
      } else {
        uint64_t _M0L6_2atmpS1656 = _M0Lm2vpS649;
        _M0Lm2vpS649 = _M0L6_2atmpS1656 - 1ull;
      }
    } else if (_M0L1qS663 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS653
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS646, _M0L1qS663);
    }
  }
  _M0Lm7removedS672 = 0;
  _M0Lm16lastRemovedDigitS673 = 0;
  _M0Lm6outputS674 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS652 || _M0Lm17vrIsTrailingZerosS653) {
    int32_t _if__result_2182;
    uint64_t _M0L6_2atmpS1697;
    uint64_t _M0L6_2atmpS1703;
    uint64_t _M0L6_2atmpS1704;
    int32_t _if__result_2183;
    int32_t _M0L6_2atmpS1700;
    int64_t _M0L6_2atmpS1699;
    uint64_t _M0L6_2atmpS1698;
    while (1) {
      uint64_t _M0L6_2atmpS1680 = _M0Lm2vpS649;
      uint64_t _M0L7vpDiv10S675 = _M0L6_2atmpS1680 / 10ull;
      uint64_t _M0L6_2atmpS1679 = _M0Lm2vmS650;
      uint64_t _M0L7vmDiv10S676 = _M0L6_2atmpS1679 / 10ull;
      uint64_t _M0L6_2atmpS1678;
      int32_t _M0L6_2atmpS1675;
      int32_t _M0L6_2atmpS1677;
      int32_t _M0L6_2atmpS1676;
      int32_t _M0L7vmMod10S678;
      uint64_t _M0L6_2atmpS1674;
      uint64_t _M0L7vrDiv10S679;
      uint64_t _M0L6_2atmpS1673;
      int32_t _M0L6_2atmpS1670;
      int32_t _M0L6_2atmpS1672;
      int32_t _M0L6_2atmpS1671;
      int32_t _M0L7vrMod10S680;
      int32_t _M0L6_2atmpS1669;
      if (_M0L7vpDiv10S675 <= _M0L7vmDiv10S676) {
        break;
      }
      _M0L6_2atmpS1678 = _M0Lm2vmS650;
      _M0L6_2atmpS1675 = (int32_t)_M0L6_2atmpS1678;
      _M0L6_2atmpS1677 = (int32_t)_M0L7vmDiv10S676;
      _M0L6_2atmpS1676 = 10 * _M0L6_2atmpS1677;
      _M0L7vmMod10S678 = _M0L6_2atmpS1675 - _M0L6_2atmpS1676;
      _M0L6_2atmpS1674 = _M0Lm2vrS648;
      _M0L7vrDiv10S679 = _M0L6_2atmpS1674 / 10ull;
      _M0L6_2atmpS1673 = _M0Lm2vrS648;
      _M0L6_2atmpS1670 = (int32_t)_M0L6_2atmpS1673;
      _M0L6_2atmpS1672 = (int32_t)_M0L7vrDiv10S679;
      _M0L6_2atmpS1671 = 10 * _M0L6_2atmpS1672;
      _M0L7vrMod10S680 = _M0L6_2atmpS1670 - _M0L6_2atmpS1671;
      _M0Lm17vmIsTrailingZerosS652
      = _M0Lm17vmIsTrailingZerosS652 && _M0L7vmMod10S678 == 0;
      if (_M0Lm17vrIsTrailingZerosS653) {
        int32_t _M0L6_2atmpS1668 = _M0Lm16lastRemovedDigitS673;
        _M0Lm17vrIsTrailingZerosS653 = _M0L6_2atmpS1668 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS653 = 0;
      }
      _M0Lm16lastRemovedDigitS673 = _M0L7vrMod10S680;
      _M0Lm2vrS648 = _M0L7vrDiv10S679;
      _M0Lm2vpS649 = _M0L7vpDiv10S675;
      _M0Lm2vmS650 = _M0L7vmDiv10S676;
      _M0L6_2atmpS1669 = _M0Lm7removedS672;
      _M0Lm7removedS672 = _M0L6_2atmpS1669 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS652) {
      while (1) {
        uint64_t _M0L6_2atmpS1693 = _M0Lm2vmS650;
        uint64_t _M0L7vmDiv10S681 = _M0L6_2atmpS1693 / 10ull;
        uint64_t _M0L6_2atmpS1692 = _M0Lm2vmS650;
        int32_t _M0L6_2atmpS1689 = (int32_t)_M0L6_2atmpS1692;
        int32_t _M0L6_2atmpS1691 = (int32_t)_M0L7vmDiv10S681;
        int32_t _M0L6_2atmpS1690 = 10 * _M0L6_2atmpS1691;
        int32_t _M0L7vmMod10S682 = _M0L6_2atmpS1689 - _M0L6_2atmpS1690;
        uint64_t _M0L6_2atmpS1688;
        uint64_t _M0L7vpDiv10S684;
        uint64_t _M0L6_2atmpS1687;
        uint64_t _M0L7vrDiv10S685;
        uint64_t _M0L6_2atmpS1686;
        int32_t _M0L6_2atmpS1683;
        int32_t _M0L6_2atmpS1685;
        int32_t _M0L6_2atmpS1684;
        int32_t _M0L7vrMod10S686;
        int32_t _M0L6_2atmpS1682;
        if (_M0L7vmMod10S682 != 0) {
          break;
        }
        _M0L6_2atmpS1688 = _M0Lm2vpS649;
        _M0L7vpDiv10S684 = _M0L6_2atmpS1688 / 10ull;
        _M0L6_2atmpS1687 = _M0Lm2vrS648;
        _M0L7vrDiv10S685 = _M0L6_2atmpS1687 / 10ull;
        _M0L6_2atmpS1686 = _M0Lm2vrS648;
        _M0L6_2atmpS1683 = (int32_t)_M0L6_2atmpS1686;
        _M0L6_2atmpS1685 = (int32_t)_M0L7vrDiv10S685;
        _M0L6_2atmpS1684 = 10 * _M0L6_2atmpS1685;
        _M0L7vrMod10S686 = _M0L6_2atmpS1683 - _M0L6_2atmpS1684;
        if (_M0Lm17vrIsTrailingZerosS653) {
          int32_t _M0L6_2atmpS1681 = _M0Lm16lastRemovedDigitS673;
          _M0Lm17vrIsTrailingZerosS653 = _M0L6_2atmpS1681 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS653 = 0;
        }
        _M0Lm16lastRemovedDigitS673 = _M0L7vrMod10S686;
        _M0Lm2vrS648 = _M0L7vrDiv10S685;
        _M0Lm2vpS649 = _M0L7vpDiv10S684;
        _M0Lm2vmS650 = _M0L7vmDiv10S681;
        _M0L6_2atmpS1682 = _M0Lm7removedS672;
        _M0Lm7removedS672 = _M0L6_2atmpS1682 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS653) {
      int32_t _M0L6_2atmpS1696 = _M0Lm16lastRemovedDigitS673;
      if (_M0L6_2atmpS1696 == 5) {
        uint64_t _M0L6_2atmpS1695 = _M0Lm2vrS648;
        uint64_t _M0L6_2atmpS1694 = _M0L6_2atmpS1695 % 2ull;
        _if__result_2182 = _M0L6_2atmpS1694 == 0ull;
      } else {
        _if__result_2182 = 0;
      }
    } else {
      _if__result_2182 = 0;
    }
    if (_if__result_2182) {
      _M0Lm16lastRemovedDigitS673 = 4;
    }
    _M0L6_2atmpS1697 = _M0Lm2vrS648;
    _M0L6_2atmpS1703 = _M0Lm2vrS648;
    _M0L6_2atmpS1704 = _M0Lm2vmS650;
    if (_M0L6_2atmpS1703 == _M0L6_2atmpS1704) {
      if (!_M0L4evenS645) {
        _if__result_2183 = 1;
      } else {
        int32_t _M0L6_2atmpS1702 = _M0Lm17vmIsTrailingZerosS652;
        _if__result_2183 = !_M0L6_2atmpS1702;
      }
    } else {
      _if__result_2183 = 0;
    }
    if (_if__result_2183) {
      _M0L6_2atmpS1700 = 1;
    } else {
      int32_t _M0L6_2atmpS1701 = _M0Lm16lastRemovedDigitS673;
      _M0L6_2atmpS1700 = _M0L6_2atmpS1701 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1699 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1700);
    _M0L6_2atmpS1698 = *(uint64_t*)&_M0L6_2atmpS1699;
    _M0Lm6outputS674 = _M0L6_2atmpS1697 + _M0L6_2atmpS1698;
  } else {
    int32_t _M0Lm7roundUpS687 = 0;
    uint64_t _M0L6_2atmpS1725 = _M0Lm2vpS649;
    uint64_t _M0L8vpDiv100S688 = _M0L6_2atmpS1725 / 100ull;
    uint64_t _M0L6_2atmpS1724 = _M0Lm2vmS650;
    uint64_t _M0L8vmDiv100S689 = _M0L6_2atmpS1724 / 100ull;
    uint64_t _M0L6_2atmpS1719;
    uint64_t _M0L6_2atmpS1722;
    uint64_t _M0L6_2atmpS1723;
    int32_t _M0L6_2atmpS1721;
    uint64_t _M0L6_2atmpS1720;
    if (_M0L8vpDiv100S688 > _M0L8vmDiv100S689) {
      uint64_t _M0L6_2atmpS1710 = _M0Lm2vrS648;
      uint64_t _M0L8vrDiv100S690 = _M0L6_2atmpS1710 / 100ull;
      uint64_t _M0L6_2atmpS1709 = _M0Lm2vrS648;
      int32_t _M0L6_2atmpS1706 = (int32_t)_M0L6_2atmpS1709;
      int32_t _M0L6_2atmpS1708 = (int32_t)_M0L8vrDiv100S690;
      int32_t _M0L6_2atmpS1707 = 100 * _M0L6_2atmpS1708;
      int32_t _M0L8vrMod100S691 = _M0L6_2atmpS1706 - _M0L6_2atmpS1707;
      int32_t _M0L6_2atmpS1705;
      _M0Lm7roundUpS687 = _M0L8vrMod100S691 >= 50;
      _M0Lm2vrS648 = _M0L8vrDiv100S690;
      _M0Lm2vpS649 = _M0L8vpDiv100S688;
      _M0Lm2vmS650 = _M0L8vmDiv100S689;
      _M0L6_2atmpS1705 = _M0Lm7removedS672;
      _M0Lm7removedS672 = _M0L6_2atmpS1705 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1718 = _M0Lm2vpS649;
      uint64_t _M0L7vpDiv10S692 = _M0L6_2atmpS1718 / 10ull;
      uint64_t _M0L6_2atmpS1717 = _M0Lm2vmS650;
      uint64_t _M0L7vmDiv10S693 = _M0L6_2atmpS1717 / 10ull;
      uint64_t _M0L6_2atmpS1716;
      uint64_t _M0L7vrDiv10S695;
      uint64_t _M0L6_2atmpS1715;
      int32_t _M0L6_2atmpS1712;
      int32_t _M0L6_2atmpS1714;
      int32_t _M0L6_2atmpS1713;
      int32_t _M0L7vrMod10S696;
      int32_t _M0L6_2atmpS1711;
      if (_M0L7vpDiv10S692 <= _M0L7vmDiv10S693) {
        break;
      }
      _M0L6_2atmpS1716 = _M0Lm2vrS648;
      _M0L7vrDiv10S695 = _M0L6_2atmpS1716 / 10ull;
      _M0L6_2atmpS1715 = _M0Lm2vrS648;
      _M0L6_2atmpS1712 = (int32_t)_M0L6_2atmpS1715;
      _M0L6_2atmpS1714 = (int32_t)_M0L7vrDiv10S695;
      _M0L6_2atmpS1713 = 10 * _M0L6_2atmpS1714;
      _M0L7vrMod10S696 = _M0L6_2atmpS1712 - _M0L6_2atmpS1713;
      _M0Lm7roundUpS687 = _M0L7vrMod10S696 >= 5;
      _M0Lm2vrS648 = _M0L7vrDiv10S695;
      _M0Lm2vpS649 = _M0L7vpDiv10S692;
      _M0Lm2vmS650 = _M0L7vmDiv10S693;
      _M0L6_2atmpS1711 = _M0Lm7removedS672;
      _M0Lm7removedS672 = _M0L6_2atmpS1711 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1719 = _M0Lm2vrS648;
    _M0L6_2atmpS1722 = _M0Lm2vrS648;
    _M0L6_2atmpS1723 = _M0Lm2vmS650;
    _M0L6_2atmpS1721
    = _M0L6_2atmpS1722 == _M0L6_2atmpS1723 || _M0Lm7roundUpS687;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1720 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1721);
    _M0Lm6outputS674 = _M0L6_2atmpS1719 + _M0L6_2atmpS1720;
  }
  _M0L6_2atmpS1727 = _M0Lm3e10S651;
  _M0L6_2atmpS1728 = _M0Lm7removedS672;
  _M0L3expS697 = _M0L6_2atmpS1727 + _M0L6_2atmpS1728;
  _M0L6_2atmpS1726 = _M0Lm6outputS674;
  _block_2185
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2185)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2185->$0 = _M0L6_2atmpS1726;
  _block_2185->$1 = _M0L3expS697;
  return _block_2185;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS640) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS640) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS639) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS639) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS638) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS638) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS637) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS637 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS637 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS637 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS637 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS637 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS637 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS637 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS637 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS637 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS637 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS637 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS637 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS637 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS637 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS637 >= 100ull) {
    return 3;
  }
  if (_M0L1vS637 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS620) {
  int32_t _M0L6_2atmpS1627;
  int32_t _M0L6_2atmpS1626;
  int32_t _M0L4baseS619;
  int32_t _M0L5base2S621;
  int32_t _M0L6offsetS622;
  int32_t _M0L6_2atmpS1625;
  uint64_t _M0L4mul0S623;
  int32_t _M0L6_2atmpS1624;
  int32_t _M0L6_2atmpS1623;
  uint64_t _M0L4mul1S624;
  uint64_t _M0L1mS625;
  struct _M0TPB7Umul128 _M0L7_2abindS626;
  uint64_t _M0L7_2alow1S627;
  uint64_t _M0L8_2ahigh1S628;
  struct _M0TPB7Umul128 _M0L7_2abindS629;
  uint64_t _M0L7_2alow0S630;
  uint64_t _M0L8_2ahigh0S631;
  uint64_t _M0L3sumS632;
  uint64_t _M0Lm5high1S633;
  int32_t _M0L6_2atmpS1621;
  int32_t _M0L6_2atmpS1622;
  int32_t _M0L5deltaS634;
  uint64_t _M0L6_2atmpS1620;
  uint64_t _M0L6_2atmpS1612;
  int32_t _M0L6_2atmpS1619;
  uint32_t _M0L6_2atmpS1616;
  int32_t _M0L6_2atmpS1618;
  int32_t _M0L6_2atmpS1617;
  uint32_t _M0L6_2atmpS1615;
  uint32_t _M0L6_2atmpS1614;
  uint64_t _M0L6_2atmpS1613;
  uint64_t _M0L1aS635;
  uint64_t _M0L6_2atmpS1611;
  uint64_t _M0L1bS636;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1627 = _M0L1iS620 + 26;
  _M0L6_2atmpS1626 = _M0L6_2atmpS1627 - 1;
  _M0L4baseS619 = _M0L6_2atmpS1626 / 26;
  _M0L5base2S621 = _M0L4baseS619 * 26;
  _M0L6offsetS622 = _M0L5base2S621 - _M0L1iS620;
  _M0L6_2atmpS1625 = _M0L4baseS619 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S623
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1625);
  _M0L6_2atmpS1624 = _M0L4baseS619 * 2;
  _M0L6_2atmpS1623 = _M0L6_2atmpS1624 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S624
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1623);
  if (_M0L6offsetS622 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S623, .$1 = _M0L4mul1S624};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS625
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS622);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS626 = _M0FPB7umul128(_M0L1mS625, _M0L4mul1S624);
  _M0L7_2alow1S627 = _M0L7_2abindS626.$0;
  _M0L8_2ahigh1S628 = _M0L7_2abindS626.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS629 = _M0FPB7umul128(_M0L1mS625, _M0L4mul0S623);
  _M0L7_2alow0S630 = _M0L7_2abindS629.$0;
  _M0L8_2ahigh0S631 = _M0L7_2abindS629.$1;
  _M0L3sumS632 = _M0L8_2ahigh0S631 + _M0L7_2alow1S627;
  _M0Lm5high1S633 = _M0L8_2ahigh1S628;
  if (_M0L3sumS632 < _M0L8_2ahigh0S631) {
    uint64_t _M0L6_2atmpS1610 = _M0Lm5high1S633;
    _M0Lm5high1S633 = _M0L6_2atmpS1610 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1621 = _M0FPB8pow5bits(_M0L5base2S621);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1622 = _M0FPB8pow5bits(_M0L1iS620);
  _M0L5deltaS634 = _M0L6_2atmpS1621 - _M0L6_2atmpS1622;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1620
  = _M0FPB13shiftright128(_M0L7_2alow0S630, _M0L3sumS632, _M0L5deltaS634);
  _M0L6_2atmpS1612 = _M0L6_2atmpS1620 + 1ull;
  _M0L6_2atmpS1619 = _M0L1iS620 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1616
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1619);
  _M0L6_2atmpS1618 = _M0L1iS620 % 16;
  _M0L6_2atmpS1617 = _M0L6_2atmpS1618 << 1;
  _M0L6_2atmpS1615 = _M0L6_2atmpS1616 >> (_M0L6_2atmpS1617 & 31);
  _M0L6_2atmpS1614 = _M0L6_2atmpS1615 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1613 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1614);
  _M0L1aS635 = _M0L6_2atmpS1612 + _M0L6_2atmpS1613;
  _M0L6_2atmpS1611 = _M0Lm5high1S633;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS636
  = _M0FPB13shiftright128(_M0L3sumS632, _M0L6_2atmpS1611, _M0L5deltaS634);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS635, .$1 = _M0L1bS636};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS602) {
  int32_t _M0L4baseS601;
  int32_t _M0L5base2S603;
  int32_t _M0L6offsetS604;
  int32_t _M0L6_2atmpS1609;
  uint64_t _M0L4mul0S605;
  int32_t _M0L6_2atmpS1608;
  int32_t _M0L6_2atmpS1607;
  uint64_t _M0L4mul1S606;
  uint64_t _M0L1mS607;
  struct _M0TPB7Umul128 _M0L7_2abindS608;
  uint64_t _M0L7_2alow1S609;
  uint64_t _M0L8_2ahigh1S610;
  struct _M0TPB7Umul128 _M0L7_2abindS611;
  uint64_t _M0L7_2alow0S612;
  uint64_t _M0L8_2ahigh0S613;
  uint64_t _M0L3sumS614;
  uint64_t _M0Lm5high1S615;
  int32_t _M0L6_2atmpS1605;
  int32_t _M0L6_2atmpS1606;
  int32_t _M0L5deltaS616;
  uint64_t _M0L6_2atmpS1597;
  int32_t _M0L6_2atmpS1604;
  uint32_t _M0L6_2atmpS1601;
  int32_t _M0L6_2atmpS1603;
  int32_t _M0L6_2atmpS1602;
  uint32_t _M0L6_2atmpS1600;
  uint32_t _M0L6_2atmpS1599;
  uint64_t _M0L6_2atmpS1598;
  uint64_t _M0L1aS617;
  uint64_t _M0L6_2atmpS1596;
  uint64_t _M0L1bS618;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS601 = _M0L1iS602 / 26;
  _M0L5base2S603 = _M0L4baseS601 * 26;
  _M0L6offsetS604 = _M0L1iS602 - _M0L5base2S603;
  _M0L6_2atmpS1609 = _M0L4baseS601 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S605
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1609);
  _M0L6_2atmpS1608 = _M0L4baseS601 * 2;
  _M0L6_2atmpS1607 = _M0L6_2atmpS1608 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S606
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1607);
  if (_M0L6offsetS604 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S605, .$1 = _M0L4mul1S606};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS607
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS604);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS608 = _M0FPB7umul128(_M0L1mS607, _M0L4mul1S606);
  _M0L7_2alow1S609 = _M0L7_2abindS608.$0;
  _M0L8_2ahigh1S610 = _M0L7_2abindS608.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS611 = _M0FPB7umul128(_M0L1mS607, _M0L4mul0S605);
  _M0L7_2alow0S612 = _M0L7_2abindS611.$0;
  _M0L8_2ahigh0S613 = _M0L7_2abindS611.$1;
  _M0L3sumS614 = _M0L8_2ahigh0S613 + _M0L7_2alow1S609;
  _M0Lm5high1S615 = _M0L8_2ahigh1S610;
  if (_M0L3sumS614 < _M0L8_2ahigh0S613) {
    uint64_t _M0L6_2atmpS1595 = _M0Lm5high1S615;
    _M0Lm5high1S615 = _M0L6_2atmpS1595 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1605 = _M0FPB8pow5bits(_M0L1iS602);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1606 = _M0FPB8pow5bits(_M0L5base2S603);
  _M0L5deltaS616 = _M0L6_2atmpS1605 - _M0L6_2atmpS1606;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1597
  = _M0FPB13shiftright128(_M0L7_2alow0S612, _M0L3sumS614, _M0L5deltaS616);
  _M0L6_2atmpS1604 = _M0L1iS602 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1601
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1604);
  _M0L6_2atmpS1603 = _M0L1iS602 % 16;
  _M0L6_2atmpS1602 = _M0L6_2atmpS1603 << 1;
  _M0L6_2atmpS1600 = _M0L6_2atmpS1601 >> (_M0L6_2atmpS1602 & 31);
  _M0L6_2atmpS1599 = _M0L6_2atmpS1600 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1598 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1599);
  _M0L1aS617 = _M0L6_2atmpS1597 + _M0L6_2atmpS1598;
  _M0L6_2atmpS1596 = _M0Lm5high1S615;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS618
  = _M0FPB13shiftright128(_M0L3sumS614, _M0L6_2atmpS1596, _M0L5deltaS616);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS617, .$1 = _M0L1bS618};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS575,
  struct _M0TPB8Pow5Pair _M0L3mulS572,
  int32_t _M0L1jS588,
  int32_t _M0L7mmShiftS590
) {
  uint64_t _M0L7_2amul0S571;
  uint64_t _M0L7_2amul1S573;
  uint64_t _M0L1mS574;
  struct _M0TPB7Umul128 _M0L7_2abindS576;
  uint64_t _M0L5_2aloS577;
  uint64_t _M0L6_2atmpS578;
  struct _M0TPB7Umul128 _M0L7_2abindS579;
  uint64_t _M0L6_2alo2S580;
  uint64_t _M0L6_2ahi2S581;
  uint64_t _M0L3midS582;
  uint64_t _M0L6_2atmpS1594;
  uint64_t _M0L2hiS583;
  uint64_t _M0L3lo2S584;
  uint64_t _M0L6_2atmpS1592;
  uint64_t _M0L6_2atmpS1593;
  uint64_t _M0L4mid2S585;
  uint64_t _M0L6_2atmpS1591;
  uint64_t _M0L3hi2S586;
  int32_t _M0L6_2atmpS1590;
  int32_t _M0L6_2atmpS1589;
  uint64_t _M0L2vpS587;
  uint64_t _M0Lm2vmS589;
  int32_t _M0L6_2atmpS1588;
  int32_t _M0L6_2atmpS1587;
  uint64_t _M0L2vrS600;
  uint64_t _M0L6_2atmpS1586;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S571 = _M0L3mulS572.$0;
  _M0L7_2amul1S573 = _M0L3mulS572.$1;
  _M0L1mS574 = _M0L1mS575 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS576 = _M0FPB7umul128(_M0L1mS574, _M0L7_2amul0S571);
  _M0L5_2aloS577 = _M0L7_2abindS576.$0;
  _M0L6_2atmpS578 = _M0L7_2abindS576.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS579 = _M0FPB7umul128(_M0L1mS574, _M0L7_2amul1S573);
  _M0L6_2alo2S580 = _M0L7_2abindS579.$0;
  _M0L6_2ahi2S581 = _M0L7_2abindS579.$1;
  _M0L3midS582 = _M0L6_2atmpS578 + _M0L6_2alo2S580;
  if (_M0L3midS582 < _M0L6_2atmpS578) {
    _M0L6_2atmpS1594 = 1ull;
  } else {
    _M0L6_2atmpS1594 = 0ull;
  }
  _M0L2hiS583 = _M0L6_2ahi2S581 + _M0L6_2atmpS1594;
  _M0L3lo2S584 = _M0L5_2aloS577 + _M0L7_2amul0S571;
  _M0L6_2atmpS1592 = _M0L3midS582 + _M0L7_2amul1S573;
  if (_M0L3lo2S584 < _M0L5_2aloS577) {
    _M0L6_2atmpS1593 = 1ull;
  } else {
    _M0L6_2atmpS1593 = 0ull;
  }
  _M0L4mid2S585 = _M0L6_2atmpS1592 + _M0L6_2atmpS1593;
  if (_M0L4mid2S585 < _M0L3midS582) {
    _M0L6_2atmpS1591 = 1ull;
  } else {
    _M0L6_2atmpS1591 = 0ull;
  }
  _M0L3hi2S586 = _M0L2hiS583 + _M0L6_2atmpS1591;
  _M0L6_2atmpS1590 = _M0L1jS588 - 64;
  _M0L6_2atmpS1589 = _M0L6_2atmpS1590 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS587
  = _M0FPB13shiftright128(_M0L4mid2S585, _M0L3hi2S586, _M0L6_2atmpS1589);
  _M0Lm2vmS589 = 0ull;
  if (_M0L7mmShiftS590) {
    uint64_t _M0L3lo3S591 = _M0L5_2aloS577 - _M0L7_2amul0S571;
    uint64_t _M0L6_2atmpS1576 = _M0L3midS582 - _M0L7_2amul1S573;
    uint64_t _M0L6_2atmpS1577;
    uint64_t _M0L4mid3S592;
    uint64_t _M0L6_2atmpS1575;
    uint64_t _M0L3hi3S593;
    int32_t _M0L6_2atmpS1574;
    int32_t _M0L6_2atmpS1573;
    if (_M0L5_2aloS577 < _M0L3lo3S591) {
      _M0L6_2atmpS1577 = 1ull;
    } else {
      _M0L6_2atmpS1577 = 0ull;
    }
    _M0L4mid3S592 = _M0L6_2atmpS1576 - _M0L6_2atmpS1577;
    if (_M0L3midS582 < _M0L4mid3S592) {
      _M0L6_2atmpS1575 = 1ull;
    } else {
      _M0L6_2atmpS1575 = 0ull;
    }
    _M0L3hi3S593 = _M0L2hiS583 - _M0L6_2atmpS1575;
    _M0L6_2atmpS1574 = _M0L1jS588 - 64;
    _M0L6_2atmpS1573 = _M0L6_2atmpS1574 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS589
    = _M0FPB13shiftright128(_M0L4mid3S592, _M0L3hi3S593, _M0L6_2atmpS1573);
  } else {
    uint64_t _M0L3lo3S594 = _M0L5_2aloS577 + _M0L5_2aloS577;
    uint64_t _M0L6_2atmpS1584 = _M0L3midS582 + _M0L3midS582;
    uint64_t _M0L6_2atmpS1585;
    uint64_t _M0L4mid3S595;
    uint64_t _M0L6_2atmpS1582;
    uint64_t _M0L6_2atmpS1583;
    uint64_t _M0L3hi3S596;
    uint64_t _M0L3lo4S597;
    uint64_t _M0L6_2atmpS1580;
    uint64_t _M0L6_2atmpS1581;
    uint64_t _M0L4mid4S598;
    uint64_t _M0L6_2atmpS1579;
    uint64_t _M0L3hi4S599;
    int32_t _M0L6_2atmpS1578;
    if (_M0L3lo3S594 < _M0L5_2aloS577) {
      _M0L6_2atmpS1585 = 1ull;
    } else {
      _M0L6_2atmpS1585 = 0ull;
    }
    _M0L4mid3S595 = _M0L6_2atmpS1584 + _M0L6_2atmpS1585;
    _M0L6_2atmpS1582 = _M0L2hiS583 + _M0L2hiS583;
    if (_M0L4mid3S595 < _M0L3midS582) {
      _M0L6_2atmpS1583 = 1ull;
    } else {
      _M0L6_2atmpS1583 = 0ull;
    }
    _M0L3hi3S596 = _M0L6_2atmpS1582 + _M0L6_2atmpS1583;
    _M0L3lo4S597 = _M0L3lo3S594 - _M0L7_2amul0S571;
    _M0L6_2atmpS1580 = _M0L4mid3S595 - _M0L7_2amul1S573;
    if (_M0L3lo3S594 < _M0L3lo4S597) {
      _M0L6_2atmpS1581 = 1ull;
    } else {
      _M0L6_2atmpS1581 = 0ull;
    }
    _M0L4mid4S598 = _M0L6_2atmpS1580 - _M0L6_2atmpS1581;
    if (_M0L4mid3S595 < _M0L4mid4S598) {
      _M0L6_2atmpS1579 = 1ull;
    } else {
      _M0L6_2atmpS1579 = 0ull;
    }
    _M0L3hi4S599 = _M0L3hi3S596 - _M0L6_2atmpS1579;
    _M0L6_2atmpS1578 = _M0L1jS588 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS589
    = _M0FPB13shiftright128(_M0L4mid4S598, _M0L3hi4S599, _M0L6_2atmpS1578);
  }
  _M0L6_2atmpS1588 = _M0L1jS588 - 64;
  _M0L6_2atmpS1587 = _M0L6_2atmpS1588 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS600
  = _M0FPB13shiftright128(_M0L3midS582, _M0L2hiS583, _M0L6_2atmpS1587);
  _M0L6_2atmpS1586 = _M0Lm2vmS589;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS600,
                                                .$1 = _M0L2vpS587,
                                                .$2 = _M0L6_2atmpS1586};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS569,
  int32_t _M0L1pS570
) {
  uint64_t _M0L6_2atmpS1572;
  uint64_t _M0L6_2atmpS1571;
  uint64_t _M0L6_2atmpS1570;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1572 = 1ull << (_M0L1pS570 & 63);
  _M0L6_2atmpS1571 = _M0L6_2atmpS1572 - 1ull;
  _M0L6_2atmpS1570 = _M0L5valueS569 & _M0L6_2atmpS1571;
  return _M0L6_2atmpS1570 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS567,
  int32_t _M0L1pS568
) {
  int32_t _M0L6_2atmpS1569;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1569 = _M0FPB10pow5Factor(_M0L5valueS567);
  return _M0L6_2atmpS1569 >= _M0L1pS568;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS562) {
  uint64_t _M0L6_2atmpS1560;
  uint64_t _M0L6_2atmpS1561;
  uint64_t _M0L6_2atmpS1562;
  uint64_t _M0L6_2atmpS1563;
  uint64_t _M0L6_2atmpS1568;
  int32_t _M0L5countS563;
  uint64_t _M0L1vS564;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1560 = _M0L5valueS562 % 5ull;
  if (_M0L6_2atmpS1560 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1561 = _M0L5valueS562 % 25ull;
  if (_M0L6_2atmpS1561 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1562 = _M0L5valueS562 % 125ull;
  if (_M0L6_2atmpS1562 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1563 = _M0L5valueS562 % 625ull;
  if (_M0L6_2atmpS1563 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1568 = _M0L5valueS562 / 625ull;
  _M0L5countS563 = 4;
  _M0L1vS564 = _M0L6_2atmpS1568;
  while (1) {
    if (_M0L1vS564 > 0ull) {
      uint64_t _M0L6_2atmpS1564 = _M0L1vS564 % 5ull;
      int32_t _M0L6_2atmpS1565;
      uint64_t _M0L6_2atmpS1566;
      if (_M0L6_2atmpS1564 != 0ull) {
        return _M0L5countS563;
      }
      _M0L6_2atmpS1565 = _M0L5countS563 + 1;
      _M0L6_2atmpS1566 = _M0L1vS564 / 5ull;
      _M0L5countS563 = _M0L6_2atmpS1565;
      _M0L1vS564 = _M0L6_2atmpS1566;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS566;
      moonbit_string_t _M0L6_2atmpS1567;
      int32_t _result_2187;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS566
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS566, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS566, _M0L5valueS562);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1567
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS566);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS566);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2187 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1567);
      moonbit_decref_cycle_free(_M0L6_2atmpS1567);
      return _result_2187;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS561,
  uint64_t _M0L2hiS559,
  int32_t _M0L4distS560
) {
  int32_t _M0L6_2atmpS1559;
  uint64_t _M0L6_2atmpS1557;
  uint64_t _M0L6_2atmpS1558;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1559 = 64 - _M0L4distS560;
  _M0L6_2atmpS1557 = _M0L2hiS559 << (_M0L6_2atmpS1559 & 63);
  _M0L6_2atmpS1558 = _M0L2loS561 >> (_M0L4distS560 & 63);
  return _M0L6_2atmpS1557 | _M0L6_2atmpS1558;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS549,
  uint64_t _M0L1bS552
) {
  uint64_t _M0L3aLoS548;
  uint64_t _M0L3aHiS550;
  uint64_t _M0L3bLoS551;
  uint64_t _M0L3bHiS553;
  uint64_t _M0L1xS554;
  uint64_t _M0L6_2atmpS1555;
  uint64_t _M0L6_2atmpS1556;
  uint64_t _M0L1yS555;
  uint64_t _M0L6_2atmpS1553;
  uint64_t _M0L6_2atmpS1554;
  uint64_t _M0L1zS556;
  uint64_t _M0L6_2atmpS1551;
  uint64_t _M0L6_2atmpS1552;
  uint64_t _M0L6_2atmpS1549;
  uint64_t _M0L6_2atmpS1550;
  uint64_t _M0L1wS557;
  uint64_t _M0L2loS558;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS548 = _M0L1aS549 & 4294967295ull;
  _M0L3aHiS550 = _M0L1aS549 >> 32;
  _M0L3bLoS551 = _M0L1bS552 & 4294967295ull;
  _M0L3bHiS553 = _M0L1bS552 >> 32;
  _M0L1xS554 = _M0L3aLoS548 * _M0L3bLoS551;
  _M0L6_2atmpS1555 = _M0L3aHiS550 * _M0L3bLoS551;
  _M0L6_2atmpS1556 = _M0L1xS554 >> 32;
  _M0L1yS555 = _M0L6_2atmpS1555 + _M0L6_2atmpS1556;
  _M0L6_2atmpS1553 = _M0L3aLoS548 * _M0L3bHiS553;
  _M0L6_2atmpS1554 = _M0L1yS555 & 4294967295ull;
  _M0L1zS556 = _M0L6_2atmpS1553 + _M0L6_2atmpS1554;
  _M0L6_2atmpS1551 = _M0L3aHiS550 * _M0L3bHiS553;
  _M0L6_2atmpS1552 = _M0L1yS555 >> 32;
  _M0L6_2atmpS1549 = _M0L6_2atmpS1551 + _M0L6_2atmpS1552;
  _M0L6_2atmpS1550 = _M0L1zS556 >> 32;
  _M0L1wS557 = _M0L6_2atmpS1549 + _M0L6_2atmpS1550;
  _M0L2loS558 = _M0L1aS549 * _M0L1bS552;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS558, .$1 = _M0L1wS557};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS546,
  int32_t _M0L4fromS543,
  int32_t _M0L2toS542
) {
  int32_t _M0L3lenS541;
  int32_t _M0L6_2atmpS1548;
  uint16_t* _M0L6bufferS544;
  int32_t _M0L1iS545;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS541 = _M0L2toS542 - _M0L4fromS543;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1548 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS544
  = (uint16_t*)moonbit_make_string(_M0L3lenS541, _M0L6_2atmpS1548);
  _M0L1iS545 = 0;
  while (1) {
    if (_M0L1iS545 < _M0L3lenS541) {
      int32_t _M0L6_2atmpS1546 = _M0L4fromS543 + _M0L1iS545;
      int32_t _M0L6_2atmpS1545;
      int32_t _M0L6_2atmpS1544;
      int32_t _M0L6_2atmpS1547;
      if (
        _M0L6_2atmpS1546 < 0
        || _M0L6_2atmpS1546 >= Moonbit_array_length(_M0L5bytesS546)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1545 = (int32_t)_M0L5bytesS546[_M0L6_2atmpS1546];
      _M0L6_2atmpS1544 = (uint16_t)_M0L6_2atmpS1545;
      if (
        _M0L1iS545 < 0 || _M0L1iS545 >= Moonbit_array_length(_M0L6bufferS544)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS544[_M0L1iS545] = _M0L6_2atmpS1544;
      _M0L6_2atmpS1547 = _M0L1iS545 + 1;
      _M0L1iS545 = _M0L6_2atmpS1547;
      continue;
    }
    break;
  }
  return _M0L6bufferS544;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS540) {
  int32_t _M0L6_2atmpS1543;
  uint32_t _M0L6_2atmpS1542;
  uint32_t _M0L6_2atmpS1541;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1543 = _M0L1eS540 * 78913;
  _M0L6_2atmpS1542 = *(uint32_t*)&_M0L6_2atmpS1543;
  _M0L6_2atmpS1541 = _M0L6_2atmpS1542 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1541;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS539) {
  int32_t _M0L6_2atmpS1540;
  uint32_t _M0L6_2atmpS1539;
  uint32_t _M0L6_2atmpS1538;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1540 = _M0L1eS539 * 732923;
  _M0L6_2atmpS1539 = *(uint32_t*)&_M0L6_2atmpS1540;
  _M0L6_2atmpS1538 = _M0L6_2atmpS1539 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1538;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS537,
  int32_t _M0L8exponentS538,
  int32_t _M0L8mantissaS535
) {
  moonbit_string_t _M0L1sS536;
  moonbit_string_t _result_2190;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS535) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS537) {
    _M0L1sS536 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS536 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS538) {
    moonbit_string_t _result_2189;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2189
    = moonbit_add_string(_M0L1sS536, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS536);
    return _result_2189;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2190
  = moonbit_add_string(_M0L1sS536, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS536);
  return _result_2190;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS534) {
  int32_t _M0L6_2atmpS1537;
  uint32_t _M0L6_2atmpS1536;
  uint32_t _M0L6_2atmpS1535;
  int32_t _M0L6_2atmpS1534;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1537 = _M0L1eS534 * 1217359;
  _M0L6_2atmpS1536 = *(uint32_t*)&_M0L6_2atmpS1537;
  _M0L6_2atmpS1535 = _M0L6_2atmpS1536 >> 19;
  _M0L6_2atmpS1534 = *(int32_t*)&_M0L6_2atmpS1535;
  return _M0L6_2atmpS1534 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS533) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS533 != _M0L4selfS533) {
    return 0;
  } else if (_M0L4selfS533 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS533 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS533;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS532) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS532 != _M0L4selfS532) {
    return 0ll;
  } else if (_M0L4selfS532 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS532 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS532;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS529
) {
  float* _M0L6_2atmpS1531;
  struct _M0TPB5ArrayGfE* _block_2191;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1531 = (float*)moonbit_make_float_array_raw(_M0L3lenS529);
  _block_2191
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2191->$0 = _M0L6_2atmpS1531;
  _block_2191->$1 = _M0L3lenS529;
  return _block_2191;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS530
) {
  uint8_t* _M0L6_2atmpS1532;
  struct _M0TPB5ArrayGbE* _block_2192;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1532 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS530);
  _block_2192
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2192)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_2192->$0 = _M0L6_2atmpS1532;
  _block_2192->$1 = _M0L3lenS530;
  return _block_2192;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS531
) {
  int32_t* _M0L6_2atmpS1533;
  struct _M0TPB5ArrayGiE* _block_2193;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1533 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS531);
  _block_2193
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2193)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2193->$0 = _M0L6_2atmpS1533;
  _block_2193->$1 = _M0L3lenS531;
  return _block_2193;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS525,
  int32_t _M0L5indexS526
) {
  uint64_t* _M0L6_2atmpS1529;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1529 = _M0L4selfS525;
  if (
    _M0L5indexS526 < 0
    || _M0L5indexS526 >= Moonbit_array_length(_M0L6_2atmpS1529)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1529[_M0L5indexS526];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint32_t* _M0L6_2atmpS1530;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1530 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS1530)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1530[_M0L5indexS528];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS524
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS524, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS523) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS523, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS522) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS522;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS510,
  int32_t _M0L5valueS512
) {
  int32_t _M0L3lenS1501;
  int32_t* _M0L6_2atmpS1503;
  int32_t _M0L6_2atmpS1502;
  int32_t _M0L6lengthS511;
  int32_t* _M0L3bufS1506;
  int32_t _M0L6_2atmpS1507;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1501 = _M0L4selfS510->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1503 = _M0MPC15array5Array6bufferGiE(_M0L4selfS510);
  _M0L6_2atmpS1502 = Moonbit_array_length(_M0L6_2atmpS1503);
  moonbit_decref_cycle_free(_M0L6_2atmpS1503);
  if (_M0L3lenS1501 == _M0L6_2atmpS1502) {
    int32_t _M0L3lenS1505 = _M0L4selfS510->$1;
    int32_t _M0L6_2atmpS1504 = _M0L3lenS1505 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS510, _M0L6_2atmpS1504);
  }
  _M0L6lengthS511 = _M0L4selfS510->$1;
  _M0L3bufS1506 = _M0L4selfS510->$0;
  _M0L3bufS1506[_M0L6lengthS511] = _M0L5valueS512;
  _M0L6_2atmpS1507 = _M0L6lengthS511 + 1;
  _M0L4selfS510->$1 = _M0L6_2atmpS1507;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS513,
  float _M0L5valueS515
) {
  int32_t _M0L3lenS1508;
  float* _M0L6_2atmpS1510;
  int32_t _M0L6_2atmpS1509;
  int32_t _M0L6lengthS514;
  float* _M0L3bufS1513;
  int32_t _M0L6_2atmpS1514;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1508 = _M0L4selfS513->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1510 = _M0MPC15array5Array6bufferGfE(_M0L4selfS513);
  _M0L6_2atmpS1509 = Moonbit_array_length(_M0L6_2atmpS1510);
  moonbit_decref_cycle_free(_M0L6_2atmpS1510);
  if (_M0L3lenS1508 == _M0L6_2atmpS1509) {
    int32_t _M0L3lenS1512 = _M0L4selfS513->$1;
    int32_t _M0L6_2atmpS1511 = _M0L3lenS1512 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS513, _M0L6_2atmpS1511);
  }
  _M0L6lengthS514 = _M0L4selfS513->$1;
  _M0L3bufS1513 = _M0L4selfS513->$0;
  _M0L3bufS1513[_M0L6lengthS514] = _M0L5valueS515;
  _M0L6_2atmpS1514 = _M0L6lengthS514 + 1;
  _M0L4selfS513->$1 = _M0L6_2atmpS1514;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS516,
  moonbit_string_t _M0L5valueS518
) {
  int32_t _M0L3lenS1515;
  moonbit_string_t* _M0L6_2atmpS1517;
  int32_t _M0L6_2atmpS1516;
  int32_t _M0L6lengthS517;
  moonbit_string_t* _M0L3bufS1520;
  moonbit_string_t _M0L6_2aoldS2099;
  int32_t _M0L6_2atmpS1521;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1515 = _M0L4selfS516->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1517 = _M0MPC15array5Array6bufferGsE(_M0L4selfS516);
  _M0L6_2atmpS1516 = Moonbit_array_length(_M0L6_2atmpS1517);
  moonbit_decref_cycle_free(_M0L6_2atmpS1517);
  if (_M0L3lenS1515 == _M0L6_2atmpS1516) {
    int32_t _M0L3lenS1519 = _M0L4selfS516->$1;
    int32_t _M0L6_2atmpS1518 = _M0L3lenS1519 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS516, _M0L6_2atmpS1518);
  }
  _M0L6lengthS517 = _M0L4selfS516->$1;
  _M0L3bufS1520 = _M0L4selfS516->$0;
  _M0L6_2aoldS2099 = (moonbit_string_t)_M0L3bufS1520[_M0L6lengthS517];
  moonbit_decref_cycle_free(_M0L6_2aoldS2099);
  _M0L3bufS1520[_M0L6lengthS517] = _M0L5valueS518;
  _M0L6_2atmpS1521 = _M0L6lengthS517 + 1;
  _M0L4selfS516->$1 = _M0L6_2atmpS1521;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS519,
  struct _M0TUsiE* _M0L5valueS521
) {
  int32_t _M0L3lenS1522;
  struct _M0TUsiE** _M0L6_2atmpS1524;
  int32_t _M0L6_2atmpS1523;
  int32_t _M0L6lengthS520;
  struct _M0TUsiE** _M0L3bufS1527;
  struct _M0TUsiE* _M0L6_2aoldS2100;
  int32_t _M0L6_2atmpS1528;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1522 = _M0L4selfS519->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1524 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS519);
  _M0L6_2atmpS1523 = Moonbit_array_length(_M0L6_2atmpS1524);
  moonbit_decref_cycle_free(_M0L6_2atmpS1524);
  if (_M0L3lenS1522 == _M0L6_2atmpS1523) {
    int32_t _M0L3lenS1526 = _M0L4selfS519->$1;
    int32_t _M0L6_2atmpS1525 = _M0L3lenS1526 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS519, _M0L6_2atmpS1525);
  }
  _M0L6lengthS520 = _M0L4selfS519->$1;
  _M0L3bufS1527 = _M0L4selfS519->$0;
  _M0L6_2aoldS2100 = (struct _M0TUsiE*)_M0L3bufS1527[_M0L6lengthS520];
  if (_M0L6_2aoldS2100) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2100);
  }
  _M0L3bufS1527[_M0L6lengthS520] = _M0L5valueS521;
  _M0L6_2atmpS1528 = _M0L6lengthS520 + 1;
  _M0L4selfS519->$1 = _M0L6_2atmpS1528;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS495,
  int32_t _M0L8requiredS497
) {
  int32_t _M0L8old__capS494;
  int32_t _M0L3lenS1497;
  int32_t _M0L8new__capS496;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS494 = _M0MPC15array5Array8capacityGiE(_M0L4selfS495);
  _M0L3lenS1497 = _M0L4selfS495->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS496
  = _M0FPB23array__growth__capacity(_M0L8old__capS494, _M0L3lenS1497, _M0L8requiredS497);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS495, _M0L8new__capS496);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS499,
  int32_t _M0L8requiredS501
) {
  int32_t _M0L8old__capS498;
  int32_t _M0L3lenS1498;
  int32_t _M0L8new__capS500;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS498 = _M0MPC15array5Array8capacityGfE(_M0L4selfS499);
  _M0L3lenS1498 = _M0L4selfS499->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS500
  = _M0FPB23array__growth__capacity(_M0L8old__capS498, _M0L3lenS1498, _M0L8requiredS501);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS499, _M0L8new__capS500);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS503,
  int32_t _M0L8requiredS505
) {
  int32_t _M0L8old__capS502;
  int32_t _M0L3lenS1499;
  int32_t _M0L8new__capS504;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS502 = _M0MPC15array5Array8capacityGsE(_M0L4selfS503);
  _M0L3lenS1499 = _M0L4selfS503->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS504
  = _M0FPB23array__growth__capacity(_M0L8old__capS502, _M0L3lenS1499, _M0L8requiredS505);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS503, _M0L8new__capS504);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS507,
  int32_t _M0L8requiredS509
) {
  int32_t _M0L8old__capS506;
  int32_t _M0L3lenS1500;
  int32_t _M0L8new__capS508;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS506 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS507);
  _M0L3lenS1500 = _M0L4selfS507->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS508
  = _M0FPB23array__growth__capacity(_M0L8old__capS506, _M0L3lenS1500, _M0L8requiredS509);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS507, _M0L8new__capS508);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS471,
  int32_t _M0L13new__capacityS474
) {
  int32_t* _M0L8old__bufS470;
  int32_t _M0L3lenS472;
  int32_t _M0L9copy__lenS473;
  int32_t* _M0L8new__bufS475;
  int32_t* _M0L6_2aoldS2101;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS470 = _M0L4selfS471->$0;
  _M0L3lenS472 = _M0L4selfS471->$1;
  if (_M0L3lenS472 < _M0L13new__capacityS474) {
    _M0L9copy__lenS473 = _M0L3lenS472;
  } else {
    _M0L9copy__lenS473 = _M0L13new__capacityS474;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS470);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS475
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS470, _M0L13new__capacityS474, _M0L9copy__lenS473, 0, 0);
  _M0L6_2aoldS2101 = _M0L4selfS471->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2101);
  _M0L4selfS471->$0 = _M0L8new__bufS475;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS477,
  int32_t _M0L13new__capacityS480
) {
  float* _M0L8old__bufS476;
  int32_t _M0L3lenS478;
  int32_t _M0L9copy__lenS479;
  float* _M0L8new__bufS481;
  float* _M0L6_2aoldS2102;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS476 = _M0L4selfS477->$0;
  _M0L3lenS478 = _M0L4selfS477->$1;
  if (_M0L3lenS478 < _M0L13new__capacityS480) {
    _M0L9copy__lenS479 = _M0L3lenS478;
  } else {
    _M0L9copy__lenS479 = _M0L13new__capacityS480;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS476);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS481
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS476, _M0L13new__capacityS480, _M0L9copy__lenS479, 0, 0);
  _M0L6_2aoldS2102 = _M0L4selfS477->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2102);
  _M0L4selfS477->$0 = _M0L8new__bufS481;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS483,
  int32_t _M0L13new__capacityS486
) {
  moonbit_string_t* _M0L8old__bufS482;
  int32_t _M0L3lenS484;
  int32_t _M0L9copy__lenS485;
  moonbit_string_t* _M0L8new__bufS487;
  moonbit_string_t* _M0L6_2aoldS2103;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS482 = _M0L4selfS483->$0;
  _M0L3lenS484 = _M0L4selfS483->$1;
  if (_M0L3lenS484 < _M0L13new__capacityS486) {
    _M0L9copy__lenS485 = _M0L3lenS484;
  } else {
    _M0L9copy__lenS485 = _M0L13new__capacityS486;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS482);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS487
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS482, _M0L13new__capacityS486, _M0L9copy__lenS485, 0, 0);
  _M0L6_2aoldS2103 = _M0L4selfS483->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2103);
  _M0L4selfS483->$0 = _M0L8new__bufS487;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS489,
  int32_t _M0L13new__capacityS492
) {
  struct _M0TUsiE** _M0L8old__bufS488;
  int32_t _M0L3lenS490;
  int32_t _M0L9copy__lenS491;
  struct _M0TUsiE** _M0L8new__bufS493;
  struct _M0TUsiE** _M0L6_2aoldS2104;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS488 = _M0L4selfS489->$0;
  _M0L3lenS490 = _M0L4selfS489->$1;
  if (_M0L3lenS490 < _M0L13new__capacityS492) {
    _M0L9copy__lenS491 = _M0L3lenS490;
  } else {
    _M0L9copy__lenS491 = _M0L13new__capacityS492;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS488);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS493
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS488, _M0L13new__capacityS492, _M0L9copy__lenS491, 0, 0);
  _M0L6_2aoldS2104 = _M0L4selfS489->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2104);
  _M0L4selfS489->$0 = _M0L8new__bufS493;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS466
) {
  int32_t* _M0L6_2atmpS1493;
  int32_t _result_2194;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1493 = _M0MPC15array5Array6bufferGiE(_M0L4selfS466);
  _result_2194 = Moonbit_array_length(_M0L6_2atmpS1493);
  moonbit_decref_cycle_free(_M0L6_2atmpS1493);
  return _result_2194;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS467
) {
  float* _M0L6_2atmpS1494;
  int32_t _result_2195;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1494 = _M0MPC15array5Array6bufferGfE(_M0L4selfS467);
  _result_2195 = Moonbit_array_length(_M0L6_2atmpS1494);
  moonbit_decref_cycle_free(_M0L6_2atmpS1494);
  return _result_2195;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS468
) {
  moonbit_string_t* _M0L6_2atmpS1495;
  int32_t _result_2196;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1495 = _M0MPC15array5Array6bufferGsE(_M0L4selfS468);
  _result_2196 = Moonbit_array_length(_M0L6_2atmpS1495);
  moonbit_decref_cycle_free(_M0L6_2atmpS1495);
  return _result_2196;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS469
) {
  struct _M0TUsiE** _M0L6_2atmpS1496;
  int32_t _result_2197;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1496 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS469);
  _result_2197 = Moonbit_array_length(_M0L6_2atmpS1496);
  moonbit_decref_cycle_free(_M0L6_2atmpS1496);
  return _result_2197;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS462,
  int32_t _M0L3lenS460,
  int32_t _M0L8requiredS459
) {
  int32_t _M0L5startS461;
  int32_t _M0L5spaceS463;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS459 < _M0L3lenS460) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
  }
  if (_M0L7currentS462 == 0) {
    _M0L5startS461 = 8;
  } else {
    _M0L5startS461 = _M0L7currentS462;
  }
  _M0L5spaceS463 = _M0L5startS461;
  while (1) {
    if (_M0L5spaceS463 < _M0L8requiredS459) {
      int32_t _M0L4nextS464 = _M0L5spaceS463 * 2;
      if (_M0L4nextS464 <= _M0L5spaceS463) {
        return _M0L8requiredS459;
      }
      _M0L5spaceS463 = _M0L4nextS464;
      continue;
    } else {
      return _M0L5spaceS463;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS457) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS457->$1;
}

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS458) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS458->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS2105;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2105 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2105);
  return _M0L8_2afieldS2105;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS453) {
  uint8_t* _M0L8_2afieldS2106;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2106 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2106);
  return _M0L8_2afieldS2106;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS454) {
  int32_t* _M0L8_2afieldS2107;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2107 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2107);
  return _M0L8_2afieldS2107;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS2108;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2108 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2108);
  return _M0L8_2afieldS2108;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS2109;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2109 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2109);
  return _M0L8_2afieldS2109;
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
  int32_t _M0L3endS1491;
  int32_t _M0L5startS1492;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1490;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1483;
  int32_t _M0L6_2atmpS1482;
  int32_t _if__result_2199;
  uint16_t* _M0L4dataS1484;
  int32_t _M0L3lenS1485;
  moonbit_string_t _M0L6_2atmpS1486;
  int32_t _M0L6_2atmpS1487;
  int32_t _M0L3lenS1489;
  int32_t _M0L6_2atmpS1488;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1491 = _M0L3strS448.$2;
  _M0L5startS1492 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1491 - _M0L5startS1492;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1490 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1490 + _M0L8str__lenS447;
  _M0L4dataS1483 = _M0L4selfS450->$0;
  _M0L6_2atmpS1482 = Moonbit_array_length(_M0L4dataS1483);
  if (_M0L8requiredS449 > _M0L6_2atmpS1482) {
    _if__result_2199 = 1;
  } else {
    int32_t _M0L3lenS1481 = _M0L4selfS450->$1;
    _if__result_2199 = _M0L8requiredS449 < _M0L3lenS1481;
  }
  if (_if__result_2199) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1484 = _M0L4selfS450->$0;
  _M0L3lenS1485 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1484);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1486 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1487 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1484, _M0L3lenS1485, _M0L6_2atmpS1486, _M0L6_2atmpS1487, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1484);
  moonbit_decref_cycle_free(_M0L6_2atmpS1486);
  _M0L3lenS1489 = _M0L4selfS450->$1;
  _M0L6_2atmpS1488 = _M0L3lenS1489 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1488;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2200;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1480;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1479;
  moonbit_string_t _result_2201;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1478 = Moonbit_array_length(_M0L3strS444);
    _if__result_2200 = _M0L3endS443 == _M0L6_2atmpS1478;
  } else {
    _if__result_2200 = 0;
  }
  if (_if__result_2200) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1480 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1480, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1479 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2201
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1479, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1479);
  return _result_2201;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2202;
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
      int32_t _M0L6_2atmpS1477 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2202 = _M0L6_2atmpS1477 <= _M0L3lenS436;
    } else {
      _if__result_2202 = 0;
    }
  } else {
    _if__result_2202 = 0;
  }
  if (_if__result_2202) {
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
  int32_t _M0L6_2atmpS1476;
  int32_t _M0L6_2atmpS1475;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1474;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1476 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1475 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1476;
  _M0L2e1S422 = _M0L6_2atmpS1475 - 1;
  _M0L6_2atmpS1474 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1474 - 1;
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
        int32_t _M0L6_2atmpS1471 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1470 = (int32_t)_M0L6_2atmpS1471;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1470;
        uint32_t _M0L6_2atmpS1466 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1465;
        int32_t _M0L6_2atmpS1467;
        uint32_t _M0L6_2atmpS1469;
        int32_t _M0L6_2atmpS1468;
        int32_t _M0L6_2atmpS1472;
        int32_t _M0L6_2atmpS1473;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1465 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1466);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1465;
        _M0L6_2atmpS1467 = _M0L1jS433 + 1;
        _M0L6_2atmpS1469 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1468 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1469);
        if (
          _M0L6_2atmpS1467 < 0
          || _M0L6_2atmpS1467 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1467] = _M0L6_2atmpS1468;
        _M0L6_2atmpS1472 = _M0L1iS432 + 1;
        _M0L6_2atmpS1473 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1472;
        _M0L1jS433 = _M0L6_2atmpS1473;
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
  int32_t _M0L6_2atmpS1464;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1464 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1464 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1463 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1463;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1460;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1460 = 1;
      } else {
        _M0L6_2atmpS1460 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1460;
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
      int32_t _M0L6_2atmpS1461;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1461 = 1;
      } else {
        _M0L6_2atmpS1461 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1461;
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
      int32_t _M0L6_2atmpS1462;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1462 = 1;
      } else {
        _M0L6_2atmpS1462 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1462;
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
  int32_t _M0L6_2atmpS1459;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1459 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1459;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1436 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1436;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1435 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1434 = 48 + _M0L6_2atmpS1435;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1434;
      int32_t _M0L6_2atmpS1433 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1432 = 48 + _M0L6_2atmpS1433;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1432;
      int32_t _M0L6_2atmpS1431 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1430 = 48 + _M0L6_2atmpS1431;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1430;
      int32_t _M0L6_2atmpS1429 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1428 = 48 + _M0L6_2atmpS1429;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1428;
      int32_t _M0L6_2atmpS1420 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1419 = _M0L6_2atmpS1420 - 4;
      int32_t _M0L6_2atmpS1422;
      int32_t _M0L6_2atmpS1421;
      int32_t _M0L6_2atmpS1424;
      int32_t _M0L6_2atmpS1423;
      int32_t _M0L6_2atmpS1426;
      int32_t _M0L6_2atmpS1425;
      int32_t _M0L6_2atmpS1427;
      _M0L6bufferS381[_M0L6_2atmpS1419] = _M0L6d1__hiS377;
      _M0L6_2atmpS1422 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1421 = _M0L6_2atmpS1422 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1421] = _M0L6d1__loS378;
      _M0L6_2atmpS1424 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1423 = _M0L6_2atmpS1424 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1423] = _M0L6d2__hiS379;
      _M0L6_2atmpS1426 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1425 = _M0L6_2atmpS1426 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1425] = _M0L6d2__loS380;
      _M0L6_2atmpS1427 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1427;
      continue;
    } else {
      int32_t _M0L6_2atmpS1458 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1458;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1445 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1444 = 48 + _M0L6_2atmpS1445;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1444;
          int32_t _M0L6_2atmpS1443 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1442 = 48 + _M0L6_2atmpS1443;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1442;
          int32_t _M0L6_2atmpS1438 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1437 = _M0L6_2atmpS1438 - 2;
          int32_t _M0L6_2atmpS1440;
          int32_t _M0L6_2atmpS1439;
          int32_t _M0L6_2atmpS1441;
          _M0L6bufferS381[_M0L6_2atmpS1437] = _M0L5d__hiS388;
          _M0L6_2atmpS1440 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1439 = _M0L6_2atmpS1440 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1439] = _M0L5d__loS389;
          _M0L6_2atmpS1441 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1441;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1453 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1452 = 48 + _M0L6_2atmpS1453;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1452;
          int32_t _M0L6_2atmpS1451 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1450 = 48 + _M0L6_2atmpS1451;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1450;
          int32_t _M0L6_2atmpS1447 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1446 = _M0L6_2atmpS1447 - 2;
          int32_t _M0L6_2atmpS1449;
          int32_t _M0L6_2atmpS1448;
          _M0L6bufferS381[_M0L6_2atmpS1446] = _M0L5d__hiS391;
          _M0L6_2atmpS1449 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1448 = _M0L6_2atmpS1449 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1448] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1457 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1454 = _M0L6_2atmpS1457 - 1;
          int32_t _M0L6_2atmpS1456 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1455 = (uint16_t)_M0L6_2atmpS1456;
          _M0L6bufferS381[_M0L6_2atmpS1454] = _M0L6_2atmpS1455;
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
  int32_t _M0L6_2atmpS1404;
  int32_t _M0L6_2atmpS1403;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1404 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1403 = _M0L5radixS355 & _M0L6_2atmpS1404;
  if (_M0L6_2atmpS1403 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1411;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1411 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1411;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1410 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1410;
        int32_t _M0L6_2atmpS1407 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1405 = _M0L6_2atmpS1407 - 1;
        int32_t _M0L6_2atmpS1406 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1408;
        uint64_t _M0L6_2atmpS1409;
        _M0L6bufferS361[_M0L6_2atmpS1405] = _M0L6_2atmpS1406;
        _M0L6_2atmpS1408 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1409 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1408;
        _M0L1nS359 = _M0L6_2atmpS1409;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1418 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1418;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1417 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1416 = _M0L1nS367 - _M0L6_2atmpS1417;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1416;
        int32_t _M0L6_2atmpS1414 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1412 = _M0L6_2atmpS1414 - 1;
        int32_t _M0L6_2atmpS1413 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1415;
        _M0L6bufferS361[_M0L6_2atmpS1412] = _M0L6_2atmpS1413;
        _M0L6_2atmpS1415 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1415;
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
  int32_t _M0L6_2atmpS1402;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1402 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1402;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1399 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1399;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1393 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1391 = _M0L6_2atmpS1393 - 2;
      int32_t _M0L6_2atmpS1392 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1396;
      int32_t _M0L6_2atmpS1394;
      int32_t _M0L6_2atmpS1395;
      int32_t _M0L6_2atmpS1397;
      uint64_t _M0L6_2atmpS1398;
      _M0L6bufferS348[_M0L6_2atmpS1391] = _M0L6_2atmpS1392;
      _M0L6_2atmpS1396 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1394 = _M0L6_2atmpS1396 - 1;
      _M0L6_2atmpS1395
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1394] = _M0L6_2atmpS1395;
      _M0L6_2atmpS1397 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1398 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1397;
      _M0L1nS344 = _M0L6_2atmpS1398;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1401 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1401;
      int32_t _M0L6_2atmpS1400 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1400;
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
      uint64_t _M0L6_2atmpS1389 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1390 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1389;
      _M0L5countS341 = _M0L6_2atmpS1390;
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
    int32_t _M0L6_2atmpS1388;
    int32_t _M0L6_2atmpS1387;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1388 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1387 = _M0L6_2atmpS1388 / 4;
    return _M0L6_2atmpS1387 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1386 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1386;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1383;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1383 = 1;
      } else {
        _M0L6_2atmpS1383 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1383;
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
      int32_t _M0L6_2atmpS1384;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1384 = 1;
      } else {
        _M0L6_2atmpS1384 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1384;
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
      int32_t _M0L6_2atmpS1385;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1385 = 1;
      } else {
        _M0L6_2atmpS1385 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1385;
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
      uint32_t _M0L6_2atmpS1381 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1382 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1381;
      _M0L5countS315 = _M0L6_2atmpS1382;
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
    int32_t _M0L6_2atmpS1380;
    int32_t _M0L6_2atmpS1379;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1380 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1379 = _M0L6_2atmpS1380 / 4;
    return _M0L6_2atmpS1379 + 1;
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
  int32_t _M0L6_2atmpS1378;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1378 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1378;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1355 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1355;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1354 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1353 = 48 + _M0L6_2atmpS1354;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1353;
      int32_t _M0L6_2atmpS1352 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1351 = 48 + _M0L6_2atmpS1352;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1351;
      int32_t _M0L6_2atmpS1350 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1349 = 48 + _M0L6_2atmpS1350;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1349;
      int32_t _M0L6_2atmpS1348 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1347 = 48 + _M0L6_2atmpS1348;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1347;
      int32_t _M0L6_2atmpS1339 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1338 = _M0L6_2atmpS1339 - 4;
      int32_t _M0L6_2atmpS1341;
      int32_t _M0L6_2atmpS1340;
      int32_t _M0L6_2atmpS1343;
      int32_t _M0L6_2atmpS1342;
      int32_t _M0L6_2atmpS1345;
      int32_t _M0L6_2atmpS1344;
      int32_t _M0L6_2atmpS1346;
      _M0L6bufferS294[_M0L6_2atmpS1338] = _M0L6d1__hiS290;
      _M0L6_2atmpS1341 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1340 = _M0L6_2atmpS1341 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1340] = _M0L6d1__loS291;
      _M0L6_2atmpS1343 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1342 = _M0L6_2atmpS1343 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1342] = _M0L6d2__hiS292;
      _M0L6_2atmpS1345 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1344 = _M0L6_2atmpS1345 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1344] = _M0L6d2__loS293;
      _M0L6_2atmpS1346 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1346;
      continue;
    } else {
      int32_t _M0L6_2atmpS1377 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1377;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1364 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1363 = 48 + _M0L6_2atmpS1364;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1363;
          int32_t _M0L6_2atmpS1362 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1361 = 48 + _M0L6_2atmpS1362;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1361;
          int32_t _M0L6_2atmpS1357 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1356 = _M0L6_2atmpS1357 - 2;
          int32_t _M0L6_2atmpS1359;
          int32_t _M0L6_2atmpS1358;
          int32_t _M0L6_2atmpS1360;
          _M0L6bufferS294[_M0L6_2atmpS1356] = _M0L5d__hiS301;
          _M0L6_2atmpS1359 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1358 = _M0L6_2atmpS1359 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1358] = _M0L5d__loS302;
          _M0L6_2atmpS1360 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1360;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1372 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1371 = 48 + _M0L6_2atmpS1372;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1371;
          int32_t _M0L6_2atmpS1370 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1369 = 48 + _M0L6_2atmpS1370;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1369;
          int32_t _M0L6_2atmpS1366 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1365 = _M0L6_2atmpS1366 - 2;
          int32_t _M0L6_2atmpS1368;
          int32_t _M0L6_2atmpS1367;
          _M0L6bufferS294[_M0L6_2atmpS1365] = _M0L5d__hiS304;
          _M0L6_2atmpS1368 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1367 = _M0L6_2atmpS1368 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1367] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1376 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1373 = _M0L6_2atmpS1376 - 1;
          int32_t _M0L6_2atmpS1375 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1374 = (uint16_t)_M0L6_2atmpS1375;
          _M0L6bufferS294[_M0L6_2atmpS1373] = _M0L6_2atmpS1374;
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
  int32_t _M0L6_2atmpS1323;
  int32_t _M0L6_2atmpS1322;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1323 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1322 = _M0L5radixS268 & _M0L6_2atmpS1323;
  if (_M0L6_2atmpS1322 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1330;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1330 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1330;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1329 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1329;
        int32_t _M0L6_2atmpS1326 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1324 = _M0L6_2atmpS1326 - 1;
        int32_t _M0L6_2atmpS1325 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1327;
        uint32_t _M0L6_2atmpS1328;
        _M0L6bufferS274[_M0L6_2atmpS1324] = _M0L6_2atmpS1325;
        _M0L6_2atmpS1327 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1328 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1327;
        _M0L1nS272 = _M0L6_2atmpS1328;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1337 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1337;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1336 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1335 = _M0L1nS280 - _M0L6_2atmpS1336;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1335;
        int32_t _M0L6_2atmpS1333 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1331 = _M0L6_2atmpS1333 - 1;
        int32_t _M0L6_2atmpS1332 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1334;
        _M0L6bufferS274[_M0L6_2atmpS1331] = _M0L6_2atmpS1332;
        _M0L6_2atmpS1334 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1334;
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
  int32_t _M0L6_2atmpS1321;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1321 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1321;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1318 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1318;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1312 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1310 = _M0L6_2atmpS1312 - 2;
      int32_t _M0L6_2atmpS1311 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1315;
      int32_t _M0L6_2atmpS1313;
      int32_t _M0L6_2atmpS1314;
      int32_t _M0L6_2atmpS1316;
      uint32_t _M0L6_2atmpS1317;
      _M0L6bufferS261[_M0L6_2atmpS1310] = _M0L6_2atmpS1311;
      _M0L6_2atmpS1315 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1313 = _M0L6_2atmpS1315 - 1;
      _M0L6_2atmpS1314
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1313] = _M0L6_2atmpS1314;
      _M0L6_2atmpS1316 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1317 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1316;
      _M0L1nS257 = _M0L6_2atmpS1317;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1320 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1320;
      int32_t _M0L6_2atmpS1319 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1319;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1309;
  moonbit_string_t _result_2216;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1309
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1309);
  if (_M0L6_2atmpS1309.$1) {
    moonbit_decref(_M0L6_2atmpS1309.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2216 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2216;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1306;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1306 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1306);
  moonbit_decref_cycle_free(_M0L6_2atmpS1306);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1307;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1307 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1307);
  moonbit_decref_cycle_free(_M0L6_2atmpS1307);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1308;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1308 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1308);
  moonbit_decref_cycle_free(_M0L6_2atmpS1308);
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
  moonbit_string_t _M0L8_2afieldS2110;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2110 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2110);
  return _M0L8_2afieldS2110;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1305;
  int64_t _M0L6_2atmpS1304;
  struct _M0TPC16string10StringView _M0L6_2atmpS1303;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1305 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1304 = (int64_t)_M0L6_2atmpS1305;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1303
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1304);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1303);
  moonbit_decref_cycle_free(_M0L6_2atmpS1303.$0);
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
  int32_t _M0L6_2atmpS1287;
  int32_t _if__result_2217;
  int32_t _M0L6_2atmpS1295;
  int32_t _if__result_2218;
  int32_t _M0L6_2atmpS1297;
  int32_t _M0L6_2atmpS1298;
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
  _M0L6_2atmpS1287 = _M0Lm2loS236;
  if (_M0L6_2atmpS1287 > 0) {
    int32_t _M0L6_2atmpS1286 = _M0Lm2loS236;
    if (_M0L6_2atmpS1286 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1285 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1284 = _M0L4selfS235[_M0L6_2atmpS1285];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1284)) {
        int32_t _M0L6_2atmpS1283 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1282 = _M0L6_2atmpS1283 - 1;
        int32_t _M0L6_2atmpS1281 = _M0L4selfS235[_M0L6_2atmpS1282];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2217
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1281);
      } else {
        _if__result_2217 = 0;
      }
    } else {
      _if__result_2217 = 0;
    }
  } else {
    _if__result_2217 = 0;
  }
  if (_if__result_2217) {
    int32_t _M0L6_2atmpS1288 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1288 + 1;
  }
  _M0L6_2atmpS1295 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1295 > 0) {
    int32_t _M0L6_2atmpS1294 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1294 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1293 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1292 = _M0L4selfS235[_M0L6_2atmpS1293];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1292)) {
        int32_t _M0L6_2atmpS1291 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1290 = _M0L6_2atmpS1291 - 1;
        int32_t _M0L6_2atmpS1289 = _M0L4selfS235[_M0L6_2atmpS1290];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2218
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1289);
      } else {
        _if__result_2218 = 0;
      }
    } else {
      _if__result_2218 = 0;
    }
  } else {
    _if__result_2218 = 0;
  }
  if (_if__result_2218) {
    int32_t _M0L6_2atmpS1296 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1296 - 1;
  }
  _M0L6_2atmpS1297 = _M0Lm2loS236;
  _M0L6_2atmpS1298 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1297 >= _M0L6_2atmpS1298) {
    int32_t _M0L6_2atmpS1299 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1300 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1299,
                                                 .$2 = _M0L6_2atmpS1300};
  } else {
    int32_t _M0L6_2atmpS1301 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1302 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1301,
                                                 .$2 = _M0L6_2atmpS1302};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1280;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1280
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1280);
  if (_M0L6_2atmpS1280.$1) {
    moonbit_decref(_M0L6_2atmpS1280.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1279;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1279
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1279);
  if (_M0L6_2atmpS1279.$1) {
    moonbit_decref(_M0L6_2atmpS1279.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1278;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1278 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1278;
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
  int32_t _M0L6_2atmpS1277;
  struct _M0TPC16string10StringView _M0L6_2atmpS1275;
  struct _M0TPB6Logger _M0L6_2atmpS1276;
  moonbit_string_t _result_2219;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1277 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1275
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1277
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1276
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1275, _M0L6_2atmpS1276, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1275.$0);
  if (_M0L6_2atmpS1276.$1) {
    moonbit_decref(_M0L6_2atmpS1276.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2219 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2219;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1273;
  int32_t _M0L5startS1274;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1273 = _M0L4selfS218.$2;
  _M0L5startS1274 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1273 - _M0L5startS1274;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 49, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1270;
    int32_t _M0L5startS1272;
    int32_t _M0L6_2atmpS1271;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1254;
    int32_t _M0L6_2atmpS1255;
    int32_t _M0L6_2atmpS1256;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1270 = _M0L4selfS218.$0;
    _M0L5startS1272 = _M0L4selfS218.$1;
    _M0L6_2atmpS1271 = _M0L5startS1272 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1270[_M0L6_2atmpS1271];
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
        int32_t _M0L6_2atmpS1257;
        int32_t _M0L6_2atmpS1258;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1257 = _M0L1iS220 + 1;
        _M0L6_2atmpS1258 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1257;
        _M0L3segS221 = _M0L6_2atmpS1258;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1259;
        int32_t _M0L6_2atmpS1260;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1259 = _M0L1iS220 + 1;
        _M0L6_2atmpS1260 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1259;
        _M0L3segS221 = _M0L6_2atmpS1260;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1261;
        int32_t _M0L6_2atmpS1262;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1261 = _M0L1iS220 + 1;
        _M0L6_2atmpS1262 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1261;
        _M0L3segS221 = _M0L6_2atmpS1262;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1263;
        int32_t _M0L6_2atmpS1264;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1263 = _M0L1iS220 + 1;
        _M0L6_2atmpS1264 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1263;
        _M0L3segS221 = _M0L6_2atmpS1264;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1266;
          moonbit_string_t _M0L6_2atmpS1265;
          int32_t _M0L6_2atmpS1267;
          int32_t _M0L6_2atmpS1268;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1266 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1265 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1266);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1265);
          moonbit_decref_cycle_free(_M0L6_2atmpS1265);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1267 = _M0L1iS220 + 1;
          _M0L6_2atmpS1268 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1267;
          _M0L3segS221 = _M0L6_2atmpS1268;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1269 = _M0L1iS220 + 1;
          int32_t _tmp_2222 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1269;
          _M0L3segS221 = _tmp_2222;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2221;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1254 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1254);
    _M0L6_2atmpS1255 = _M0L1iS220 + 1;
    _M0L6_2atmpS1256 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1255;
    _M0L3segS221 = _M0L6_2atmpS1256;
    continue;
    joinlet_2221:;
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
    int64_t _M0L6_2atmpS1253 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1252;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1252
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1253);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1252);
    moonbit_decref_cycle_free(_M0L6_2atmpS1252.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1250;
  int32_t _M0L5startS1251;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1228;
  int32_t _if__result_2223;
  int32_t _M0L6_2atmpS1238;
  int32_t _if__result_2224;
  int32_t _M0L6_2atmpS1240;
  int32_t _M0L6_2atmpS1241;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1250 = _M0L4selfS201.$2;
  _M0L5startS1251 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1250 - _M0L5startS1251;
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
  _M0L6_2atmpS1228 = _M0Lm2loS202;
  if (_M0L6_2atmpS1228 > 0) {
    int32_t _M0L6_2atmpS1227 = _M0Lm2loS202;
    if (_M0L6_2atmpS1227 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1226 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1225 = _M0L4baseS209 + _M0L6_2atmpS1226;
      int32_t _M0L6_2atmpS1224 = _M0L3strS208[_M0L6_2atmpS1225];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1224)) {
        int32_t _M0L6_2atmpS1223 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1222 = _M0L4baseS209 + _M0L6_2atmpS1223;
        int32_t _M0L6_2atmpS1221 = _M0L6_2atmpS1222 - 1;
        int32_t _M0L6_2atmpS1220 = _M0L3strS208[_M0L6_2atmpS1221];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2223
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1220);
      } else {
        _if__result_2223 = 0;
      }
    } else {
      _if__result_2223 = 0;
    }
  } else {
    _if__result_2223 = 0;
  }
  if (_if__result_2223) {
    int32_t _M0L6_2atmpS1229 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1229 + 1;
  }
  _M0L6_2atmpS1238 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1238 > 0) {
    int32_t _M0L6_2atmpS1237 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1237 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1236 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1235 = _M0L4baseS209 + _M0L6_2atmpS1236;
      int32_t _M0L6_2atmpS1234 = _M0L3strS208[_M0L6_2atmpS1235];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1234)) {
        int32_t _M0L6_2atmpS1233 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1232 = _M0L4baseS209 + _M0L6_2atmpS1233;
        int32_t _M0L6_2atmpS1231 = _M0L6_2atmpS1232 - 1;
        int32_t _M0L6_2atmpS1230 = _M0L3strS208[_M0L6_2atmpS1231];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2224
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1230);
      } else {
        _if__result_2224 = 0;
      }
    } else {
      _if__result_2224 = 0;
    }
  } else {
    _if__result_2224 = 0;
  }
  if (_if__result_2224) {
    int32_t _M0L6_2atmpS1239 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1239 - 1;
  }
  _M0L6_2atmpS1240 = _M0Lm2loS202;
  _M0L6_2atmpS1241 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1240 >= _M0L6_2atmpS1241) {
    int32_t _M0L6_2atmpS1245 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1242 = _M0L4baseS209 + _M0L6_2atmpS1245;
    int32_t _M0L6_2atmpS1244 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1243 = _M0L4baseS209 + _M0L6_2atmpS1244;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1242,
                                                 .$2 = _M0L6_2atmpS1243};
  } else {
    int32_t _M0L6_2atmpS1249 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1246 = _M0L4baseS209 + _M0L6_2atmpS1249;
    int32_t _M0L6_2atmpS1248 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1247 = _M0L4baseS209 + _M0L6_2atmpS1248;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1246,
                                                 .$2 = _M0L6_2atmpS1247};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1217;
  int32_t _M0L6_2atmpS1216;
  int32_t _M0L6_2atmpS1219;
  int32_t _M0L6_2atmpS1218;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1215;
  moonbit_string_t _result_2225;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1217 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1216
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1217);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1216);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1219 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1218
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1219);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1218);
  _M0L6_2atmpS1215 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2225 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1215);
  moonbit_decref_cycle_free(_M0L6_2atmpS1215);
  return _result_2225;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1212;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1212 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1212);
  } else {
    int32_t _M0L6_2atmpS1214;
    int32_t _M0L6_2atmpS1213;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1214 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1213 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1214, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1213);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1210;
  int32_t _M0L6_2atmpS1211;
  int32_t _M0L6_2atmpS1209;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1210 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1211 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1209 = _M0L6_2atmpS1210 - _M0L6_2atmpS1211;
  return _M0L6_2atmpS1209 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1207;
  int32_t _M0L6_2atmpS1208;
  int32_t _M0L6_2atmpS1206;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1207 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1208 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1206 = _M0L6_2atmpS1207 % _M0L6_2atmpS1208;
  return _M0L6_2atmpS1206 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1204;
  int32_t _M0L6_2atmpS1205;
  int32_t _M0L6_2atmpS1203;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1204 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1205 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1203 = _M0L6_2atmpS1204 / _M0L6_2atmpS1205;
  return _M0L6_2atmpS1203 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1201;
  int32_t _M0L6_2atmpS1202;
  int32_t _M0L6_2atmpS1200;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1201 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1202 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1200 = _M0L6_2atmpS1201 + _M0L6_2atmpS1202;
  return _M0L6_2atmpS1200 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1199;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1199 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1199;
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
  int32_t _M0L3lenS1198;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1193;
  int32_t _M0L6_2atmpS1192;
  int32_t _if__result_2226;
  uint16_t* _M0L4dataS1194;
  int32_t _M0L3lenS1195;
  int32_t _M0L3lenS1197;
  int32_t _M0L6_2atmpS1196;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1198 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1198 + _M0L8str__lenS182;
  _M0L4dataS1193 = _M0L4selfS185->$0;
  _M0L6_2atmpS1192 = Moonbit_array_length(_M0L4dataS1193);
  if (_M0L8requiredS184 > _M0L6_2atmpS1192) {
    _if__result_2226 = 1;
  } else {
    int32_t _M0L3lenS1191 = _M0L4selfS185->$1;
    _if__result_2226 = _M0L8requiredS184 < _M0L3lenS1191;
  }
  if (_if__result_2226) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1194 = _M0L4selfS185->$0;
  _M0L3lenS1195 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1194);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1194, _M0L3lenS1195, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1194);
  _M0L3lenS1197 = _M0L4selfS185->$1;
  _M0L6_2atmpS1196 = _M0L3lenS1197 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1196;
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
      int32_t _M0L6_2atmpS1188 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1189;
      int32_t _M0L6_2atmpS1190;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1188;
      _M0L6_2atmpS1189 = _M0L1iS176 + 1;
      _M0L6_2atmpS1190 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1189;
      _M0L1jS177 = _M0L6_2atmpS1190;
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
    int32_t _M0L3lenS1159 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1161 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1160 = Moonbit_array_length(_M0L4dataS1161);
    uint16_t* _M0L4dataS1164;
    int32_t _M0L3lenS1165;
    int32_t _M0L6_2atmpS1166;
    int32_t _M0L3lenS1168;
    int32_t _M0L6_2atmpS1167;
    if (_M0L3lenS1159 >= _M0L6_2atmpS1160) {
      int32_t _M0L3lenS1163 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1162 = _M0L3lenS1163 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1162);
    }
    _M0L4dataS1164 = _M0L4selfS171->$0;
    _M0L3lenS1165 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1164);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1166 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1165 < 0
      || _M0L3lenS1165 >= Moonbit_array_length(_M0L4dataS1164)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1164[_M0L3lenS1165] = _M0L6_2atmpS1166;
    moonbit_decref_cycle_free(_M0L4dataS1164);
    _M0L3lenS1168 = _M0L4selfS171->$1;
    _M0L6_2atmpS1167 = _M0L3lenS1168 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1167;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1172 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1170 = Moonbit_array_length(_M0L4dataS1172);
    int32_t _M0L3lenS1171 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1169 = _M0L6_2atmpS1170 - _M0L3lenS1171;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1175;
    int32_t _M0L3lenS1176;
    uint32_t _M0L6_2atmpS1179;
    uint32_t _M0L6_2atmpS1178;
    int32_t _M0L6_2atmpS1177;
    uint16_t* _M0L4dataS1180;
    int32_t _M0L3lenS1185;
    int32_t _M0L6_2atmpS1181;
    uint32_t _M0L6_2atmpS1184;
    uint32_t _M0L6_2atmpS1183;
    int32_t _M0L6_2atmpS1182;
    int32_t _M0L3lenS1187;
    int32_t _M0L6_2atmpS1186;
    if (_M0L6_2atmpS1169 < 2) {
      int32_t _M0L3lenS1174 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1173 = _M0L3lenS1174 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1173);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1175 = _M0L4selfS171->$0;
    _M0L3lenS1176 = _M0L4selfS171->$1;
    _M0L6_2atmpS1179 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1178 = 55296u + _M0L6_2atmpS1179;
    moonbit_incref_cycle_free(_M0L4dataS1175);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1177 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1178);
    if (
      _M0L3lenS1176 < 0
      || _M0L3lenS1176 >= Moonbit_array_length(_M0L4dataS1175)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1175[_M0L3lenS1176] = _M0L6_2atmpS1177;
    moonbit_decref_cycle_free(_M0L4dataS1175);
    _M0L4dataS1180 = _M0L4selfS171->$0;
    _M0L3lenS1185 = _M0L4selfS171->$1;
    _M0L6_2atmpS1181 = _M0L3lenS1185 + 1;
    _M0L6_2atmpS1184 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1183 = 56320u + _M0L6_2atmpS1184;
    moonbit_incref_cycle_free(_M0L4dataS1180);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1182 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1183);
    if (
      _M0L6_2atmpS1181 < 0
      || _M0L6_2atmpS1181 >= Moonbit_array_length(_M0L4dataS1180)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1180[_M0L6_2atmpS1181] = _M0L6_2atmpS1182;
    moonbit_decref_cycle_free(_M0L4dataS1180);
    _M0L3lenS1187 = _M0L4selfS171->$1;
    _M0L6_2atmpS1186 = _M0L3lenS1187 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1186;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1158;
  int32_t _M0L6_2atmpS1156;
  int32_t _M0L3lenS1157;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1153;
  int32_t _M0L6_2atmpS1154;
  int32_t _M0L3lenS1155;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2111;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1158 = _M0L4selfS166->$0;
  _M0L6_2atmpS1156 = Moonbit_array_length(_M0L4dataS1158);
  _M0L3lenS1157 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1156, _M0L3lenS1157, _M0L8requiredS167);
  _M0L4dataS1153 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1153);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1154 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1155 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1153, _M0L13new__capacityS165, _M0L6_2atmpS1154, _M0L3lenS1155, 0, 0);
  _M0L6_2aoldS2111 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2111);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
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
  int32_t _M0L6_2atmpS1152;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1152 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1152;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1151;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1151 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1151;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1142;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1142 = _M0L4selfS155->$1;
  if (_M0L3lenS1142 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1143 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1145 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1144 = Moonbit_array_length(_M0L4dataS1145);
    if (_M0L3lenS1143 == _M0L6_2atmpS1144) {
      uint16_t* _M0L4dataS1146 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1146);
      return _M0L4dataS1146;
    } else {
      uint16_t* _M0L4dataS1147 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1148 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1149;
      int32_t _M0L3lenS1150;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1147);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1149 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1150 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1147, _M0L3lenS1148, _M0L6_2atmpS1149, _M0L3lenS1150, 0, 0);
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
  int32_t _if__result_2229;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1138 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1139 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1138 <= _M0L6_2atmpS1139) {
            int32_t _M0L6_2atmpS1137 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2229 = _M0L6_2atmpS1137 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2229 = 0;
          }
        } else {
          _if__result_2229 = 0;
        }
      } else {
        _if__result_2229 = 0;
      }
    } else {
      _if__result_2229 = 0;
    }
  } else {
    _if__result_2229 = 0;
  }
  if (_if__result_2229) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1141;
    moonbit_string_t _M0L6_2atmpS1140;
    uint16_t* _result_2230;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    _M0L6_2atmpS1141 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1141);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1140
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2230 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1140);
    moonbit_decref_cycle_free(_M0L6_2atmpS1140);
    return _result_2230;
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
  struct _M0TPB13StringBuilder* _block_2231;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1136 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1136 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2231
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2231)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2231->$0 = _M0L4dataS140;
  _block_2231->$1 = 0;
  return _block_2231;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1135;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1135 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1135;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2232;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1116 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1117;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1117
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
          if (_M0L6_2atmpS1116 <= _M0L6_2atmpS1117) {
            int32_t _M0L6_2atmpS1115 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2232 = _M0L6_2atmpS1115 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2232 = 0;
          }
        } else {
          _if__result_2232 = 0;
        }
      } else {
        _if__result_2232 = 0;
      }
    } else {
      _if__result_2232 = 0;
    }
  } else {
    _if__result_2232 = 0;
  }
  if (_if__result_2232) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1119;
    moonbit_string_t _M0L6_2atmpS1118;
    int32_t* _result_2233;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1119 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1118
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2233
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1118);
    moonbit_decref_cycle_free(_M0L6_2atmpS1118);
    return _result_2233;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2234;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1121 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1122;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1122
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS123);
          if (_M0L6_2atmpS1121 <= _M0L6_2atmpS1122) {
            int32_t _M0L6_2atmpS1120 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2234 = _M0L6_2atmpS1120 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2234 = 0;
          }
        } else {
          _if__result_2234 = 0;
        }
      } else {
        _if__result_2234 = 0;
      }
    } else {
      _if__result_2234 = 0;
    }
  } else {
    _if__result_2234 = 0;
  }
  if (_if__result_2234) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS123, _M0L13allocate__lenS119, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1124;
    moonbit_string_t _M0L6_2atmpS1123;
    float* _result_2235;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1124 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1124);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1123
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2235
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1123);
    moonbit_decref_cycle_free(_M0L6_2atmpS1123);
    return _result_2235;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2236;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1126 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1127;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1127
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
          if (_M0L6_2atmpS1126 <= _M0L6_2atmpS1127) {
            int32_t _M0L6_2atmpS1125 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2236 = _M0L6_2atmpS1125 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2236 = 0;
          }
        } else {
          _if__result_2236 = 0;
        }
      } else {
        _if__result_2236 = 0;
      }
    } else {
      _if__result_2236 = 0;
    }
  } else {
    _if__result_2236 = 0;
  }
  if (_if__result_2236) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1129;
    moonbit_string_t _M0L6_2atmpS1128;
    moonbit_string_t* _result_2237;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1129 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1128
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2237
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1128);
    moonbit_decref_cycle_free(_M0L6_2atmpS1128);
    return _result_2237;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2238;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1131 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1132;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1132
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
          if (_M0L6_2atmpS1131 <= _M0L6_2atmpS1132) {
            int32_t _M0L6_2atmpS1130 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2238 = _M0L6_2atmpS1130 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2238 = 0;
          }
        } else {
          _if__result_2238 = 0;
        }
      } else {
        _if__result_2238 = 0;
      }
    } else {
      _if__result_2238 = 0;
    }
  } else {
    _if__result_2238 = 0;
  }
  if (_if__result_2238) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS131, 0, _M0L3srcS135, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1134;
    moonbit_string_t _M0L6_2atmpS1133;
    struct _M0TUsiE** _result_2239;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1134 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1133
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2239
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1133);
    moonbit_decref_cycle_free(_M0L6_2atmpS1133);
    return _result_2239;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1112;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1112
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1112);
  if (_M0L6_2atmpS1112.$1) {
    moonbit_decref(_M0L6_2atmpS1112.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1113;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1113
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1113);
  if (_M0L6_2atmpS1113.$1) {
    moonbit_decref(_M0L6_2atmpS1113.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1114;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1114
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1114);
  if (_M0L6_2atmpS1114.$1) {
    moonbit_decref(_M0L6_2atmpS1114.$1);
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

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS92,
  int32_t _M0L13allocate__lenS90,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS91,
  int32_t _M0L9blit__lenS94
) {
  float* _M0L3dstS89;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS89 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS90);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS89, _M0L11dst__offsetS91, _M0L3srcS92, _M0L11src__offsetS93, _M0L9blit__lenS94);
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
        int32_t _M0L6_2atmpS1067 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1069 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1068;
        int32_t _M0L6_2atmpS1070;
        if (
          _M0L6_2atmpS1069 < 0
          || _M0L6_2atmpS1069 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1068 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1069];
        if (
          _M0L6_2atmpS1067 < 0
          || _M0L6_2atmpS1067 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1067] = _M0L6_2atmpS1068;
        _M0L6_2atmpS1070 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1070;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1075 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1075;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1071 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1073 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1072;
        int32_t _M0L6_2atmpS1074;
        if (
          _M0L6_2atmpS1073 < 0
          || _M0L6_2atmpS1073 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1072 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1073];
        if (
          _M0L6_2atmpS1071 < 0
          || _M0L6_2atmpS1071 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1071] = _M0L6_2atmpS1072;
        _M0L6_2atmpS1074 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1074;
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
        int32_t _M0L6_2atmpS1076 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1078 = _M0L11src__offsetS30 + _M0L1iS31;
        int32_t _M0L6_2atmpS1077;
        int32_t _M0L6_2atmpS1079;
        if (
          _M0L6_2atmpS1078 < 0
          || _M0L6_2atmpS1078 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1077 = (int32_t)_M0L3srcS28[_M0L6_2atmpS1078];
        if (
          _M0L6_2atmpS1076 < 0
          || _M0L6_2atmpS1076 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1076] = _M0L6_2atmpS1077;
        _M0L6_2atmpS1079 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1079;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1084 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1084;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1080 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1082 = _M0L11src__offsetS30 + _M0L1iS34;
        int32_t _M0L6_2atmpS1081;
        int32_t _M0L6_2atmpS1083;
        if (
          _M0L6_2atmpS1082 < 0
          || _M0L6_2atmpS1082 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1081 = (int32_t)_M0L3srcS28[_M0L6_2atmpS1082];
        if (
          _M0L6_2atmpS1080 < 0
          || _M0L6_2atmpS1080 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1080] = _M0L6_2atmpS1081;
        _M0L6_2atmpS1083 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1083;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  float* _M0L3srcS37,
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
        int32_t _M0L6_2atmpS1085 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1087 = _M0L11src__offsetS39 + _M0L1iS40;
        float _M0L6_2atmpS1086;
        int32_t _M0L6_2atmpS1088;
        if (
          _M0L6_2atmpS1087 < 0
          || _M0L6_2atmpS1087 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1086 = (float)_M0L3srcS37[_M0L6_2atmpS1087];
        if (
          _M0L6_2atmpS1085 < 0
          || _M0L6_2atmpS1085 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1085] = _M0L6_2atmpS1086;
        _M0L6_2atmpS1088 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1088;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1093 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1093;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1089 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1091 = _M0L11src__offsetS39 + _M0L1iS43;
        float _M0L6_2atmpS1090;
        int32_t _M0L6_2atmpS1092;
        if (
          _M0L6_2atmpS1091 < 0
          || _M0L6_2atmpS1091 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1090 = (float)_M0L3srcS37[_M0L6_2atmpS1091];
        if (
          _M0L6_2atmpS1089 < 0
          || _M0L6_2atmpS1089 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1089] = _M0L6_2atmpS1090;
        _M0L6_2atmpS1092 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1092;
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
        int32_t _M0L6_2atmpS1094 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1096 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS1095;
        moonbit_string_t _M0L6_2aoldS2112;
        int32_t _M0L6_2atmpS1097;
        if (
          _M0L6_2atmpS1096 < 0
          || _M0L6_2atmpS1096 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1095 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1096];
        if (
          _M0L6_2atmpS1094 < 0
          || _M0L6_2atmpS1094 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2112 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1094];
        moonbit_incref_cycle_free(_M0L6_2atmpS1095);
        moonbit_decref_cycle_free(_M0L6_2aoldS2112);
        _M0L3dstS45[_M0L6_2atmpS1094] = _M0L6_2atmpS1095;
        _M0L6_2atmpS1097 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1097;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1102 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1102;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1098 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1100 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1099;
        moonbit_string_t _M0L6_2aoldS2113;
        int32_t _M0L6_2atmpS1101;
        if (
          _M0L6_2atmpS1100 < 0
          || _M0L6_2atmpS1100 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1099 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1100];
        if (
          _M0L6_2atmpS1098 < 0
          || _M0L6_2atmpS1098 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2113 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1098];
        moonbit_incref_cycle_free(_M0L6_2atmpS1099);
        moonbit_decref_cycle_free(_M0L6_2aoldS2113);
        _M0L3dstS45[_M0L6_2atmpS1098] = _M0L6_2atmpS1099;
        _M0L6_2atmpS1101 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1101;
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
        int32_t _M0L6_2atmpS1103 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1105 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS1104;
        struct _M0TUsiE* _M0L6_2aoldS2114;
        int32_t _M0L6_2atmpS1106;
        if (
          _M0L6_2atmpS1105 < 0
          || _M0L6_2atmpS1105 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1104 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1105];
        if (
          _M0L6_2atmpS1103 < 0
          || _M0L6_2atmpS1103 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2114 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1103];
        if (_M0L6_2atmpS1104) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1104);
        }
        if (_M0L6_2aoldS2114) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2114);
        }
        _M0L3dstS54[_M0L6_2atmpS1103] = _M0L6_2atmpS1104;
        _M0L6_2atmpS1106 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1106;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1111 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1111;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1107 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1109 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1108;
        struct _M0TUsiE* _M0L6_2aoldS2115;
        int32_t _M0L6_2atmpS1110;
        if (
          _M0L6_2atmpS1109 < 0
          || _M0L6_2atmpS1109 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1108 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1109];
        if (
          _M0L6_2atmpS1107 < 0
          || _M0L6_2atmpS1107 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2115 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1107];
        if (_M0L6_2atmpS1108) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1108);
        }
        if (_M0L6_2aoldS2115) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2115);
        }
        _M0L3dstS54[_M0L6_2atmpS1107] = _M0L6_2atmpS1108;
        _M0L6_2atmpS1110 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1110;
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS15) {
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_30.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_31.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1038) {
  switch (Moonbit_object_tag(_M0L4_2aeS1038)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1038);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_34.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1062,
  struct _M0TPB4Show _M0L8_2aparamS1061
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1060 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1062;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1060, _M0L8_2aparamS1061);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1059,
  struct _M0TPB4Show _M0L8_2aparamS1058
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1057 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1059;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1057, _M0L8_2aparamS1058);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1056,
  int32_t _M0L8_2aparamS1055
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1054 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1056;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1054, _M0L8_2aparamS1055);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1053,
  struct _M0TPC16string10StringView _M0L8_2aparamS1052
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1051 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1053;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1051, _M0L8_2aparamS1052);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1050,
  moonbit_string_t _M0L8_2aparamS1047,
  int32_t _M0L8_2aparamS1048,
  int32_t _M0L8_2aparamS1049
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1046 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1050;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1046, _M0L8_2aparamS1047, _M0L8_2aparamS1048, _M0L8_2aparamS1049);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1045,
  moonbit_string_t _M0L8_2aparamS1044
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1043 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1045;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1043, _M0L8_2aparamS1044);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1066;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1031;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1032;
  int32_t _M0L7_2abindS1033;
  struct _M0TUsiE** _M0L7_2abindS1034;
  int32_t _M0L6_2acntS2120;
  int32_t _M0L2__S1035;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1066
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1031
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1031)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L12async__testsS1031->$0 = _M0L6_2atmpS1066;
  _M0L12async__testsS1031->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1032
  = _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1033 = _M0L7_2abindS1032->$1;
  _M0L7_2abindS1034 = _M0L7_2abindS1032->$0;
  _M0L6_2acntS2120
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1032));
  if (_M0L6_2acntS2120 > 1) {
    int32_t _M0L11_2anew__cntS2121 = _M0L6_2acntS2120 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1032), _M0L11_2anew__cntS2121);
    moonbit_incref_cycle_free(_M0L7_2abindS1034);
  } else if (_M0L6_2acntS2120 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1032);
  }
  _M0L2__S1035 = 0;
  while (1) {
    if (_M0L2__S1035 < _M0L7_2abindS1033) {
      struct _M0TUsiE* _M0L3argS1036 =
        (struct _M0TUsiE*)_M0L7_2abindS1034[_M0L2__S1035];
      moonbit_string_t _M0L6_2atmpS1063 = _M0L3argS1036->$0;
      int32_t _M0L6_2atmpS1064 = _M0L3argS1036->$1;
      int32_t _M0L6_2atmpS1065;
      moonbit_incref_cycle_free(_M0L6_2atmpS1063);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples22hetrec__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1031, _M0L6_2atmpS1063, _M0L6_2atmpS1064);
      moonbit_decref_cycle_free(_M0L6_2atmpS1063);
      _M0L6_2atmpS1065 = _M0L2__S1035 + 1;
      _M0L2__S1035 = _M0L6_2atmpS1065;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1034);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hetrec\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples22hetrec__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples22hetrec__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1031);
  moonbit_decref_cycle_free(_M0L12async__testsS1031);
  moonbit_flush_cycles();
  return 0;
}