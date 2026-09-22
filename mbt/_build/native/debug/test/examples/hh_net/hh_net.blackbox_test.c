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

struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23hh__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154;

struct _M0TUdiE;

struct _M0TPB5ArrayGbE;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TP26RiantR8snn__mbt2HH;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23hh__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TUddE;

struct _M0TP26RiantR8snn__mbt11HHParameter;

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

struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH {
  struct _M0TP26RiantR8snn__mbt2HH* $0;
  struct _M0TP26RiantR8snn__mbt2HH* $1;
  moonbit_string_t $2;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $3;
  
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

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23hh__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
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

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGbE {
  uint8_t* $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
  int32_t $1;
  
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

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23hh__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TUddE {
  double $0;
  double $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1161(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1154(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1149(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1126(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1119(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples23hh__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt20forward__hh__synapse(
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH*
);

struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH* _M0MP26RiantR8snn__mbt16SpikingSynapseHH6random(
  struct _M0TP26RiantR8snn__mbt2HH*,
  struct _M0TP26RiantR8snn__mbt2HH*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt2HH* _M0MP26RiantR8snn__mbt2HH3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11HHParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11HHParameter* _M0MP26RiantR8snn__mbt11HHParameter3new(
  
);

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH*,
  float
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

double _M0FPC14math2ln(double);

#define _M0FPC14math3cos cos

#define _M0FPC14math3sin sin

struct _M0TUdiE* _M0FPC14math5frexp(double);

struct _M0TUdiE* _M0FPC14math9normalize(double);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

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

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

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
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_19 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_12 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 104, 104, 95, 110, 101, 116, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_32 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

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
} const moonbit_string_literal_30 =
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
} const moonbit_string_literal_27 =
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
} const moonbit_string_literal_16 =
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
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[111]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 110, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 104, 104, 95, 110, 101, 116, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 
    114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1161$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1161
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[70] =
  {
    sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149)
    / 4, 1,
    offsetof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149, $1)
    / 4
    * 2,
    sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154)
    / 4, 1,
    offsetof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH, $3) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt2HH) / 4, 9,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2HH, $9) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2381
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1182,
  moonbit_string_t _M0L8filenameS1151,
  int32_t _M0L5indexS1153
) {
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149* _closure_2413;
  struct _M0TWEu* _M0L13handle__startS1149;
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154* _closure_2414;
  struct _M0TWssbEu* _M0L14handle__resultS1154;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1161;
  void* _M0L11_2atry__errS1176;
  struct moonbit_result_0 _tmp_2416;
  int32_t _handle__error__result_2417;
  int32_t _M0L6_2atmpS2369;
  void* _M0L3errS1177;
  moonbit_string_t _M0L4nameS1179;
  struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1180;
  moonbit_string_t _M0L7_2anameS1181;
  int32_t _M0L6_2acntS2407;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1151);
  _closure_2413
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149*)moonbit_malloc(sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149));
  Moonbit_object_header(_closure_2413)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2413->code
  = &_M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1149;
  _closure_2413->$0 = _M0L5indexS1153;
  _closure_2413->$1 = _M0L8filenameS1151;
  _M0L13handle__startS1149 = (struct _M0TWEu*)_closure_2413;
  moonbit_incref_cycle_free(_M0L8filenameS1151);
  _closure_2414
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154*)moonbit_malloc(sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154));
  Moonbit_object_header(_closure_2414)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2414->code
  = &_M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1154;
  _closure_2414->$0 = _M0L5indexS1153;
  _closure_2414->$1 = _M0L8filenameS1151;
  _M0L14handle__resultS1154 = (struct _M0TWssbEu*)_closure_2414;
  _M0L17error__to__stringS1161
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1161$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2416
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1182, _M0L8filenameS1151, _M0L5indexS1153, _M0L13handle__startS1149, _M0L14handle__resultS1154, _M0L17error__to__stringS1161);
  if (_tmp_2416.tag) {
    int32_t const _M0L5_2aokS2378 = _tmp_2416.data.ok;
    _handle__error__result_2417 = _M0L5_2aokS2378;
  } else {
    void* const _M0L6_2aerrS2379 = _tmp_2416.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
    moonbit_decref_cycle_free(_M0L13handle__startS1149);
    _M0L11_2atry__errS1176 = _M0L6_2aerrS2379;
    goto join_1175;
  }
  if (_handle__error__result_2417) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
    moonbit_decref_cycle_free(_M0L13handle__startS1149);
    _M0L6_2atmpS2369 = 1;
  } else {
    struct moonbit_result_0 _tmp_2418;
    int32_t _handle__error__result_2419;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2418
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1182, _M0L8filenameS1151, _M0L5indexS1153, _M0L13handle__startS1149, _M0L14handle__resultS1154, _M0L17error__to__stringS1161);
    if (_tmp_2418.tag) {
      int32_t const _M0L5_2aokS2376 = _tmp_2418.data.ok;
      _handle__error__result_2419 = _M0L5_2aokS2376;
    } else {
      void* const _M0L6_2aerrS2377 = _tmp_2418.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
      moonbit_decref_cycle_free(_M0L13handle__startS1149);
      _M0L11_2atry__errS1176 = _M0L6_2aerrS2377;
      goto join_1175;
    }
    if (_handle__error__result_2419) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
      moonbit_decref_cycle_free(_M0L13handle__startS1149);
      _M0L6_2atmpS2369 = 1;
    } else {
      struct moonbit_result_0 _tmp_2420;
      int32_t _handle__error__result_2421;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2420
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1182, _M0L8filenameS1151, _M0L5indexS1153, _M0L13handle__startS1149, _M0L14handle__resultS1154, _M0L17error__to__stringS1161);
      if (_tmp_2420.tag) {
        int32_t const _M0L5_2aokS2374 = _tmp_2420.data.ok;
        _handle__error__result_2421 = _M0L5_2aokS2374;
      } else {
        void* const _M0L6_2aerrS2375 = _tmp_2420.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
        moonbit_decref_cycle_free(_M0L13handle__startS1149);
        _M0L11_2atry__errS1176 = _M0L6_2aerrS2375;
        goto join_1175;
      }
      if (_handle__error__result_2421) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
        moonbit_decref_cycle_free(_M0L13handle__startS1149);
        _M0L6_2atmpS2369 = 1;
      } else {
        struct moonbit_result_0 _tmp_2422;
        int32_t _handle__error__result_2423;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2422
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1182, _M0L8filenameS1151, _M0L5indexS1153, _M0L13handle__startS1149, _M0L14handle__resultS1154, _M0L17error__to__stringS1161);
        if (_tmp_2422.tag) {
          int32_t const _M0L5_2aokS2372 = _tmp_2422.data.ok;
          _handle__error__result_2423 = _M0L5_2aokS2372;
        } else {
          void* const _M0L6_2aerrS2373 = _tmp_2422.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
          moonbit_decref_cycle_free(_M0L13handle__startS1149);
          _M0L11_2atry__errS1176 = _M0L6_2aerrS2373;
          goto join_1175;
        }
        if (_handle__error__result_2423) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
          moonbit_decref_cycle_free(_M0L13handle__startS1149);
          _M0L6_2atmpS2369 = 1;
        } else {
          struct moonbit_result_0 _tmp_2424;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2424
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1182, _M0L8filenameS1151, _M0L5indexS1153, _M0L13handle__startS1149, _M0L14handle__resultS1154, _M0L17error__to__stringS1161);
          moonbit_decref_cycle_free(_M0L13handle__startS1149);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1161);
          if (_tmp_2424.tag) {
            int32_t const _M0L5_2aokS2370 = _tmp_2424.data.ok;
            _M0L6_2atmpS2369 = _M0L5_2aokS2370;
          } else {
            void* const _M0L6_2aerrS2371 = _tmp_2424.data.err;
            _M0L11_2atry__errS1176 = _M0L6_2aerrS2371;
            goto join_1175;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2369) {
    void* _M0L126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2380 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2380)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2380)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1176
    = _M0L126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2380;
    goto join_1175;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1154);
  }
  goto joinlet_2415;
  join_1175:;
  _M0L3errS1177 = _M0L11_2atry__errS1176;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1180
  = (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1177;
  _M0L7_2anameS1181 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1180->$0;
  _M0L6_2acntS2407
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1180));
  if (_M0L6_2acntS2407 > 1) {
    int32_t _M0L11_2anew__cntS2408 = _M0L6_2acntS2407 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1180), _M0L11_2anew__cntS2408);
    moonbit_incref_cycle_free(_M0L7_2anameS1181);
  } else if (_M0L6_2acntS2407 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1180);
  }
  _M0L4nameS1179 = _M0L7_2anameS1181;
  goto join_1178;
  goto joinlet_2425;
  join_1178:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1154(_M0L14handle__resultS1154, _M0L4nameS1179, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1154);
  moonbit_decref_cycle_free(_M0L4nameS1179);
  joinlet_2425:;
  joinlet_2415:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1161(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2368,
  void* _M0L3errS1162
) {
  void* _M0L1eS1164;
  moonbit_string_t _M0L1eS1166;
  moonbit_string_t _result_2428;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1162)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1167 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1162;
      moonbit_string_t _M0L4_2aeS1168 = _M0L10_2aFailureS1167->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1168);
      _M0L1eS1166 = _M0L4_2aeS1168;
      goto join_1165;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1169 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1162;
      moonbit_string_t _M0L4_2aeS1170 = _M0L15_2aInspectErrorS1169->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1170);
      _M0L1eS1166 = _M0L4_2aeS1170;
      goto join_1165;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1171 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1162;
      moonbit_string_t _M0L4_2aeS1172 = _M0L16_2aSnapshotErrorS1171->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1172);
      _M0L1eS1166 = _M0L4_2aeS1172;
      goto join_1165;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1173 =
        (struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1162;
      moonbit_string_t _M0L4_2aeS1174 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1173->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1174);
      _M0L1eS1166 = _M0L4_2aeS1174;
      goto join_1165;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1162);
      _M0L1eS1164 = _M0L3errS1162;
      goto join_1163;
      break;
    }
  }
  join_1165:;
  return _M0L1eS1166;
  join_1163:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2428 = _M0FP15Error10to__string(_M0L1eS1164);
  moonbit_decref_cycle_free(_M0L1eS1164);
  return _result_2428;
}

int32_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1154(
  struct _M0TWssbEu* _M0L6_2aenvS2365,
  moonbit_string_t _M0L10__testnameS1155,
  moonbit_string_t _M0L7messageS1156,
  int32_t _M0L7skippedS1157
) {
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154* _M0L14_2acasted__envS2366;
  moonbit_string_t _M0L8filenameS1151;
  int32_t _M0L5indexS1153;
  moonbit_string_t _M0L10file__nameS1158;
  moonbit_string_t _M0L7messageS1159;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1160;
  moonbit_string_t _M0L6_2atmpS2367;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2366
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1154*)_M0L6_2aenvS2365;
  _M0L8filenameS1151 = _M0L14_2acasted__envS2366->$1;
  _M0L5indexS1153 = _M0L14_2acasted__envS2366->$0;
  if (!_M0L7skippedS1157 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1158
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1151, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1159
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1156, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1160
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1160, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1160, _M0L10file__nameS1158);
  moonbit_decref_cycle_free(_M0L10file__nameS1158);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1160, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1160, _M0L5indexS1153);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1160, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1160, _M0L7messageS1159);
  moonbit_decref_cycle_free(_M0L7messageS1159);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1160, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2367
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1160);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1160);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2367);
  moonbit_decref_cycle_free(_M0L6_2atmpS2367);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1149(
  struct _M0TWEu* _M0L6_2aenvS2362
) {
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149* _M0L14_2acasted__envS2363;
  moonbit_string_t _M0L8filenameS1151;
  int32_t _M0L5indexS1153;
  moonbit_string_t _M0L10file__nameS1150;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1152;
  moonbit_string_t _M0L6_2atmpS2364;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2363
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fhh__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1149*)_M0L6_2aenvS2362;
  _M0L8filenameS1151 = _M0L14_2acasted__envS2363->$1;
  _M0L5indexS1153 = _M0L14_2acasted__envS2363->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1150
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1151, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1152
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1152, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1152, _M0L10file__nameS1150);
  moonbit_decref_cycle_free(_M0L10file__nameS1150);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1152, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1152, _M0L5indexS1153);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1152, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2364
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1152);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1152);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2364);
  moonbit_decref_cycle_free(_M0L6_2atmpS2364);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1119;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1126;
  struct _M0TUsiE** _M0L6_2atmpS2361;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1133;
  moonbit_string_t* _M0L9cli__argsS1134;
  moonbit_string_t _M0L6_2atmpS2360;
  moonbit_string_t _M0L6_2atmpS2359;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1135;
  int32_t _M0L7_2abindS1136;
  moonbit_string_t* _M0L7_2abindS1137;
  int32_t _M0L6_2acntS2409;
  int32_t _M0L2__S1138;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1119 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1126 = 0;
  _M0L6_2atmpS2361 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1133
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1133)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1133->$0 = _M0L6_2atmpS2361;
  _M0L16file__and__indexS1133->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1134
  = _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1134)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2360 = (moonbit_string_t)_M0L9cli__argsS1134[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2360);
  moonbit_decref_cycle_free(_M0L9cli__argsS1134);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2359
  = _M0MP46RiantR8snn__mbt8examples23hh__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2360);
  moonbit_decref_cycle_free(_M0L6_2atmpS2360);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1135
  = _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1126(_M0L51moonbit__test__driver__internal__split__mbt__stringS1126, _M0L6_2atmpS2359, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2359);
  _M0L7_2abindS1136 = _M0L10test__argsS1135->$1;
  _M0L7_2abindS1137 = _M0L10test__argsS1135->$0;
  _M0L6_2acntS2409
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1135));
  if (_M0L6_2acntS2409 > 1) {
    int32_t _M0L11_2anew__cntS2410 = _M0L6_2acntS2409 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1135), _M0L11_2anew__cntS2410);
    moonbit_incref_cycle_free(_M0L7_2abindS1137);
  } else if (_M0L6_2acntS2409 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1135);
  }
  _M0L2__S1138 = 0;
  while (1) {
    if (_M0L2__S1138 < _M0L7_2abindS1136) {
      moonbit_string_t _M0L3argS1139 =
        (moonbit_string_t)_M0L7_2abindS1137[_M0L2__S1138];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1140;
      moonbit_string_t _M0L4fileS1141;
      moonbit_string_t _M0L5rangeS1142;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1143;
      moonbit_string_t _M0L6_2atmpS2357;
      int32_t _M0L5startS1144;
      moonbit_string_t _M0L6_2atmpS2356;
      int32_t _M0L3endS1145;
      int32_t _M0L1iS1146;
      int32_t _M0L6_2atmpS2358;
      moonbit_incref_cycle_free(_M0L3argS1139);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1140
      = _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1126(_M0L51moonbit__test__driver__internal__split__mbt__stringS1126, _M0L3argS1139, 58);
      moonbit_decref_cycle_free(_M0L3argS1139);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1141
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1140, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1142
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1140, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1140);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1143
      = _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1126(_M0L51moonbit__test__driver__internal__split__mbt__stringS1126, _M0L5rangeS1142, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1142);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2357
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1143, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1144
      = _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1119(_M0L45moonbit__test__driver__internal__parse__int__S1119, _M0L6_2atmpS2357);
      moonbit_decref_cycle_free(_M0L6_2atmpS2357);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2356
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1143, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1143);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1145
      = _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1119(_M0L45moonbit__test__driver__internal__parse__int__S1119, _M0L6_2atmpS2356);
      moonbit_decref_cycle_free(_M0L6_2atmpS2356);
      _M0L1iS1146 = _M0L5startS1144;
      while (1) {
        if (_M0L1iS1146 < _M0L3endS1145) {
          struct _M0TUsiE* _M0L8_2atupleS2354;
          int32_t _M0L6_2atmpS2355;
          moonbit_incref_cycle_free(_M0L4fileS1141);
          _M0L8_2atupleS2354
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2354)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2354->$0 = _M0L4fileS1141;
          _M0L8_2atupleS2354->$1 = _M0L1iS1146;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1133, _M0L8_2atupleS2354);
          _M0L6_2atmpS2355 = _M0L1iS1146 + 1;
          _M0L1iS1146 = _M0L6_2atmpS2355;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1141);
        }
        break;
      }
      _M0L6_2atmpS2358 = _M0L2__S1138 + 1;
      _M0L2__S1138 = _M0L6_2atmpS2358;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1137);
    }
    break;
  }
  return _M0L16file__and__indexS1133;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1126(
  int32_t _M0L6_2aenvS2335,
  moonbit_string_t _M0L1sS1127,
  int32_t _M0L3sepS1128
) {
  moonbit_string_t* _M0L6_2atmpS2353;
  struct _M0TPB5ArrayGsE* _M0L3resS1129;
  struct _M0TPB8MutLocalGiE* _M0L1iS1130;
  struct _M0TPB8MutLocalGiE* _M0L5startS1131;
  int32_t _M0L3valS2348;
  int32_t _M0L6_2atmpS2349;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2353 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1129
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1129)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1129->$0 = _M0L6_2atmpS2353;
  _M0L3resS1129->$1 = 0;
  _M0L1iS1130
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1130)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1130->$0 = 0;
  _M0L5startS1131
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1131)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1131->$0 = 0;
  while (1) {
    int32_t _M0L3valS2336 = _M0L1iS1130->$0;
    int32_t _M0L6_2atmpS2337 = Moonbit_array_length(_M0L1sS1127);
    if (_M0L3valS2336 < _M0L6_2atmpS2337) {
      int32_t _M0L3valS2340 = _M0L1iS1130->$0;
      int32_t _M0L6_2atmpS2339;
      int32_t _M0L6_2atmpS2338;
      int32_t _M0L3valS2347;
      int32_t _M0L6_2atmpS2346;
      if (
        _M0L3valS2340 < 0
        || _M0L3valS2340 >= Moonbit_array_length(_M0L1sS1127)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2339 = _M0L1sS1127[_M0L3valS2340];
      _M0L6_2atmpS2338 = _M0L6_2atmpS2339;
      if (_M0L6_2atmpS2338 == _M0L3sepS1128) {
        int32_t _M0L3valS2342 = _M0L5startS1131->$0;
        int32_t _M0L3valS2343 = _M0L1iS1130->$0;
        moonbit_string_t _M0L6_2atmpS2341;
        int32_t _M0L3valS2345;
        int32_t _M0L6_2atmpS2344;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2341
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1127, _M0L3valS2342, _M0L3valS2343);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1129, _M0L6_2atmpS2341);
        _M0L3valS2345 = _M0L1iS1130->$0;
        _M0L6_2atmpS2344 = _M0L3valS2345 + 1;
        _M0L5startS1131->$0 = _M0L6_2atmpS2344;
      }
      _M0L3valS2347 = _M0L1iS1130->$0;
      _M0L6_2atmpS2346 = _M0L3valS2347 + 1;
      _M0L1iS1130->$0 = _M0L6_2atmpS2346;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1130);
    }
    break;
  }
  _M0L3valS2348 = _M0L5startS1131->$0;
  _M0L6_2atmpS2349 = Moonbit_array_length(_M0L1sS1127);
  if (_M0L3valS2348 < _M0L6_2atmpS2349) {
    int32_t _M0L3valS2351 = _M0L5startS1131->$0;
    int32_t _M0L6_2atmpS2352;
    moonbit_string_t _M0L6_2atmpS2350;
    moonbit_decref_cycle_free(_M0L5startS1131);
    _M0L6_2atmpS2352 = Moonbit_array_length(_M0L1sS1127);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2350
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1127, _M0L3valS2351, _M0L6_2atmpS2352);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1129, _M0L6_2atmpS2350);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1131);
  }
  return _M0L3resS1129;
}

int32_t _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1119(
  int32_t _M0L6_2aenvS2328,
  moonbit_string_t _M0L1sS1120
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1121;
  int32_t _M0L3lenS1122;
  int32_t _M0L7_2abindS1123;
  int32_t _M0L1iS1124;
  int32_t _result_2433;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1121
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1121)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1121->$0 = 0;
  _M0L3lenS1122 = Moonbit_array_length(_M0L1sS1120);
  _M0L7_2abindS1123 = 0;
  _M0L1iS1124 = _M0L7_2abindS1123;
  while (1) {
    if (_M0L1iS1124 < _M0L3lenS1122) {
      int32_t _M0L3valS2333 = _M0L3resS1121->$0;
      int32_t _M0L6_2atmpS2330 = _M0L3valS2333 * 10;
      int32_t _M0L6_2atmpS2332;
      int32_t _M0L6_2atmpS2331;
      int32_t _M0L6_2atmpS2329;
      int32_t _M0L6_2atmpS2334;
      if (
        _M0L1iS1124 < 0 || _M0L1iS1124 >= Moonbit_array_length(_M0L1sS1120)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2332 = _M0L1sS1120[_M0L1iS1124];
      _M0L6_2atmpS2331 = _M0L6_2atmpS2332 - 48;
      _M0L6_2atmpS2329 = _M0L6_2atmpS2330 + _M0L6_2atmpS2331;
      _M0L3resS1121->$0 = _M0L6_2atmpS2329;
      _M0L6_2atmpS2334 = _M0L1iS1124 + 1;
      _M0L1iS1124 = _M0L6_2atmpS2334;
      continue;
    }
    break;
  }
  _result_2433 = _M0L3resS1121->$0;
  moonbit_decref_cycle_free(_M0L3resS1121);
  return _result_2433;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples23hh__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1118
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1118);
  return _M0L4selfS1118;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1088,
  moonbit_string_t _M0L12_2adiscard__S1089,
  int32_t _M0L12_2adiscard__S1090,
  struct _M0TWEu* _M0L12_2adiscard__S1091,
  struct _M0TWssbEu* _M0L12_2adiscard__S1092,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1093
) {
  struct moonbit_result_0 _result_2434;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2434.tag = 1;
  _result_2434.data.ok = 0;
  return _result_2434;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1094,
  moonbit_string_t _M0L12_2adiscard__S1095,
  int32_t _M0L12_2adiscard__S1096,
  struct _M0TWEu* _M0L12_2adiscard__S1097,
  struct _M0TWssbEu* _M0L12_2adiscard__S1098,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1099
) {
  struct moonbit_result_0 _result_2435;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2435.tag = 1;
  _result_2435.data.ok = 0;
  return _result_2435;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1100,
  moonbit_string_t _M0L12_2adiscard__S1101,
  int32_t _M0L12_2adiscard__S1102,
  struct _M0TWEu* _M0L12_2adiscard__S1103,
  struct _M0TWssbEu* _M0L12_2adiscard__S1104,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1105
) {
  struct moonbit_result_0 _result_2436;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2436.tag = 1;
  _result_2436.data.ok = 0;
  return _result_2436;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1106,
  moonbit_string_t _M0L12_2adiscard__S1107,
  int32_t _M0L12_2adiscard__S1108,
  struct _M0TWEu* _M0L12_2adiscard__S1109,
  struct _M0TWssbEu* _M0L12_2adiscard__S1110,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1111
) {
  struct moonbit_result_0 _result_2437;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2437.tag = 1;
  _result_2437.data.ok = 0;
  return _result_2437;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1112,
  moonbit_string_t _M0L12_2adiscard__S1113,
  int32_t _M0L12_2adiscard__S1114,
  struct _M0TWEu* _M0L12_2adiscard__S1115,
  struct _M0TWssbEu* _M0L12_2adiscard__S1116,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1117
) {
  struct moonbit_result_0 _result_2438;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2438.tag = 1;
  _result_2438.data.ok = 0;
  return _result_2438;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1087
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20forward__hh__synapse(
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH* _M0L1cS1058
) {
  moonbit_string_t _M0L3symS2325;
  struct _M0TPB5ArrayGfE* _M0L6targetS1057;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2322;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L3preS2324;
  struct _M0TPB5ArrayGbE* _M0L4fireS2323;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_hh.mbt"
  _M0L3symS2325 = _M0L1cS1058->$2;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_hh.mbt"
  if (
    _M0L3symS2325 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2325)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2325, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2325) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2HH* _M0L4postS2326 = _M0L1cS1058->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2382 = _M0L4postS2326->$8;
    moonbit_incref_cycle_free(_M0L8_2afieldS2382);
    _M0L6targetS1057 = _M0L8_2afieldS2382;
  } else {
    struct _M0TP26RiantR8snn__mbt2HH* _M0L4postS2327 = _M0L1cS1058->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2383 = _M0L4postS2327->$9;
    moonbit_incref_cycle_free(_M0L8_2afieldS2383);
    _M0L6targetS1057 = _M0L8_2afieldS2383;
  }
  _M0L6matrixS2322 = _M0L1cS1058->$3;
  _M0L3preS2324 = _M0L1cS1058->$0;
  _M0L4fireS2323 = _M0L3preS2324->$6;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_hh.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2322, _M0L4fireS2323, _M0L6targetS1057);
  moonbit_decref_cycle_free(_M0L6targetS1057);
  return 0;
}

struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH* _M0MP26RiantR8snn__mbt16SpikingSynapseHH6random(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L3preS1050,
  struct _M0TP26RiantR8snn__mbt2HH* _M0L4postS1051,
  moonbit_string_t _M0L3symS1056,
  float _M0L2muS1052,
  float _M0L5sigmaS1053,
  float _M0L1pS1054,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1055
) {
  int32_t _M0L1nS2320;
  int32_t _M0L1nS2321;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1049;
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH* _block_2439;
  #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_hh.mbt"
  _M0L1nS2320 = _M0L3preS1050->$1;
  _M0L1nS2321 = _M0L4postS1051->$1;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_hh.mbt"
  _M0L6matrixS1049
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2320, _M0L1nS2321, _M0L2muS1052, _M0L5sigmaS1053, _M0L1pS1054, _M0L3rngS1055);
  moonbit_incref_cycle_free(_M0L3preS1050);
  moonbit_incref_cycle_free(_M0L4postS1051);
  moonbit_incref_cycle_free(_M0L3symS1056);
  _block_2439
  = (struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseHH));
  Moonbit_object_header(_block_2439)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2439->$0 = _M0L3preS1050;
  _block_2439->$1 = _M0L4postS1051;
  _block_2439->$2 = _M0L3symS1056;
  _block_2439->$3 = _M0L6matrixS1049;
  return _block_2439;
}

struct _M0TP26RiantR8snn__mbt2HH* _M0MP26RiantR8snn__mbt2HH3new(
  int32_t _M0L1nS1028,
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L5paramS1029,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1035
) {
  float _M0L2elS2319;
  struct _M0TPB5ArrayGfE* _M0L1vS1027;
  int32_t _M0L7_2abindS1030;
  int32_t _M0L1kS1031;
  struct _M0TPB5ArrayGfE* _M0L1mS1038;
  struct _M0TPB5ArrayGfE* _M0L7n__gateS1039;
  struct _M0TPB5ArrayGfE* _M0L1hS1040;
  struct _M0TPB5ArrayGbE* _M0L4fireS1041;
  struct _M0TPB5ArrayGfE* _M0L1iS1042;
  struct _M0TPB5ArrayGfE* _M0L2geS1043;
  struct _M0TPB5ArrayGfE* _M0L2giS1044;
  int32_t _M0L7_2abindS1045;
  int32_t _M0L3__kS1046;
  struct _M0TP26RiantR8snn__mbt2HH* _block_2443;
  #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L2elS2319 = _M0L5paramS1029->$2;
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1vS1027 = _M0MPC15array5Array4makeGfE(_M0L1nS1028, _M0L2elS2319);
  _M0L7_2abindS1030 = 0;
  _M0L1kS1031 = _M0L7_2abindS1030;
  while (1) {
    if (_M0L1kS1031 < _M0L1nS1028) {
      double _M0L2z1S1033;
      struct _M0TUddE* _M0L7_2abindS1034;
      double _M0L5_2az1S1036;
      float _M0L2elS2313;
      float _M0L6_2atmpS2316;
      float _M0L6_2atmpS2315;
      float _M0L6_2atmpS2314;
      float _M0L6_2atmpS2312;
      int32_t _M0L6_2atmpS2317;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L7_2abindS1034 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS1035);
      _M0L5_2az1S1036 = _M0L7_2abindS1034->$0;
      moonbit_decref_cycle_free(_M0L7_2abindS1034);
      _M0L2z1S1033 = _M0L5_2az1S1036;
      goto join_1032;
      goto joinlet_2441;
      join_1032:;
      _M0L2elS2313 = _M0L5paramS1029->$2;
      _M0L6_2atmpS2316 = (float)_M0L2z1S1033;
      _M0L6_2atmpS2315 = _M0L6_2atmpS2316 - 0x1p+0f;
      _M0L6_2atmpS2314 = 0x1.4p+2f * _M0L6_2atmpS2315;
      _M0L6_2atmpS2312 = _M0L2elS2313 + _M0L6_2atmpS2314;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1027, _M0L1kS1031, _M0L6_2atmpS2312);
      joinlet_2441:;
      _M0L6_2atmpS2317 = _M0L1kS1031 + 1;
      _M0L1kS1031 = _M0L6_2atmpS2317;
      continue;
    }
    break;
  }
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1mS1038 = _M0MPC15array5Array4makeGfE(_M0L1nS1028, 0x0p+0f);
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L7n__gateS1039 = _M0MPC15array5Array4makeGfE(_M0L1nS1028, 0x0p+0f);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1hS1040 = _M0MPC15array5Array4makeGfE(_M0L1nS1028, 0x1p+0f);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L4fireS1041 = _M0MPC15array5Array4makeGbE(_M0L1nS1028, 0);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1iS1042 = _M0MPC15array5Array4makeGfE(_M0L1nS1028, 0x0p+0f);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L2geS1043 = _M0MPC15array5Array4makeGfE(_M0L1nS1028, 0x0p+0f);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L2giS1044 = _M0MPC15array5Array4makeGfE(_M0L1nS1028, 0x0p+0f);
  _M0L7_2abindS1045 = 0;
  _M0L3__kS1046 = _M0L7_2abindS1045;
  while (1) {
    if (_M0L3__kS1046 < _M0L1nS1028) {
      struct _M0TUddE* _M0L6_2atmpS2384;
      int32_t _M0L6_2atmpS2318;
      #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2384 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS1035);
      moonbit_decref_cycle_free(_M0L6_2atmpS2384);
      _M0L6_2atmpS2318 = _M0L3__kS1046 + 1;
      _M0L3__kS1046 = _M0L6_2atmpS2318;
      continue;
    }
    break;
  }
  moonbit_incref_cycle_free(_M0L5paramS1029);
  _block_2443
  = (struct _M0TP26RiantR8snn__mbt2HH*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2HH));
  Moonbit_object_header(_block_2443)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2443->$0 = _M0L5paramS1029;
  _block_2443->$1 = _M0L1nS1028;
  _block_2443->$2 = _M0L1vS1027;
  _block_2443->$3 = _M0L1mS1038;
  _block_2443->$4 = _M0L7n__gateS1039;
  _block_2443->$5 = _M0L1hS1040;
  _block_2443->$6 = _M0L4fireS1041;
  _block_2443->$7 = _M0L1iS1042;
  _block_2443->$8 = _M0L2geS1043;
  _block_2443->$9 = _M0L2giS1044;
  return _block_2443;
}

struct _M0TP26RiantR8snn__mbt11HHParameter* _M0MP26RiantR8snn__mbt11HHParameter3new(
  
) {
  float _M0L6_2atmpS2310;
  float _M0L6_2atmpS2311;
  float _M0L2cmS1025;
  float _M0L6_2atmpS2309;
  float _M0L6_2atmpS2307;
  float _M0L6_2atmpS2308;
  float _M0L2glS1026;
  float _M0L6_2atmpS2306;
  float _M0L6_2atmpS2304;
  float _M0L6_2atmpS2305;
  float _M0L6_2atmpS2299;
  float _M0L6_2atmpS2303;
  float _M0L6_2atmpS2301;
  float _M0L6_2atmpS2302;
  float _M0L6_2atmpS2300;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _block_2444;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L6_2atmpS2310 = 0x1.e848p+19f * 0x1p+0f;
  _M0L6_2atmpS2311 = 0x1.388p+14f * 0x1.5798ee2308c3ap-27f;
  _M0L2cmS1025 = _M0L6_2atmpS2310 * _M0L6_2atmpS2311;
  _M0L6_2atmpS2309 = 0x1.a36e2eb1c432dp-15f * 0x1.dcd65p+29f;
  _M0L6_2atmpS2307 = _M0L6_2atmpS2309 * 0x1p+0f;
  _M0L6_2atmpS2308 = 0x1.388p+14f * 0x1.5798ee2308c3ap-27f;
  _M0L2glS1026 = _M0L6_2atmpS2307 * _M0L6_2atmpS2308;
  _M0L6_2atmpS2306 = 0x1.9p+6f * 0x1.e848p+19f;
  _M0L6_2atmpS2304 = _M0L6_2atmpS2306 * 0x1p+0f;
  _M0L6_2atmpS2305 = 0x1.388p+14f * 0x1.5798ee2308c3ap-27f;
  _M0L6_2atmpS2299 = _M0L6_2atmpS2304 * _M0L6_2atmpS2305;
  _M0L6_2atmpS2303 = 0x1.ep+4f * 0x1.e848p+19f;
  _M0L6_2atmpS2301 = _M0L6_2atmpS2303 * 0x1p+0f;
  _M0L6_2atmpS2302 = 0x1.388p+14f * 0x1.5798ee2308c3ap-27f;
  _M0L6_2atmpS2300 = _M0L6_2atmpS2301 * _M0L6_2atmpS2302;
  _block_2444
  = (struct _M0TP26RiantR8snn__mbt11HHParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11HHParameter));
  Moonbit_object_header(_block_2444)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2444->$0 = _M0L2cmS1025;
  _block_2444->$1 = _M0L2glS1026;
  _block_2444->$2 = -0x1.04p+6f;
  _block_2444->$3 = -0x1.68p+6f;
  _block_2444->$4 = 0x1.9p+5f;
  _block_2444->$5 = _M0L6_2atmpS2299;
  _block_2444->$6 = _M0L6_2atmpS2300;
  _block_2444->$7 = -0x1.f8p+5f;
  _block_2444->$8 = 0x1.4p+2f;
  _block_2444->$9 = 0x1.4p+3f;
  _block_2444->$10 = 0x0p+0f;
  _block_2444->$11 = -0x1.4p+6f;
  return _block_2444;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS982,
  float _M0L2dtS1008
) {
  int32_t _M0L1nS981;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S983;
  float _M0L2cmS984;
  float _M0L2glS985;
  float _M0L2elS986;
  float _M0L2ekS987;
  float _M0L2enS988;
  float _M0L2gnS989;
  float _M0L2gkS990;
  float _M0L2vtS991;
  float _M0L6tau__eS992;
  float _M0L6tau__iS993;
  float _M0L4e__eS994;
  float _M0L4e__iS995;
  int32_t _M0L7_2abindS996;
  int32_t _M0L1iS997;
  int32_t _M0L7_2abindS1022;
  int32_t _M0L1iS1023;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS981 = _M0L1pS982->$1;
  _M0L3p__S983 = _M0L1pS982->$0;
  _M0L2cmS984 = _M0L3p__S983->$0;
  _M0L2glS985 = _M0L3p__S983->$1;
  _M0L2elS986 = _M0L3p__S983->$2;
  _M0L2ekS987 = _M0L3p__S983->$3;
  _M0L2enS988 = _M0L3p__S983->$4;
  _M0L2gnS989 = _M0L3p__S983->$5;
  _M0L2gkS990 = _M0L3p__S983->$6;
  _M0L2vtS991 = _M0L3p__S983->$7;
  _M0L6tau__eS992 = _M0L3p__S983->$8;
  _M0L6tau__iS993 = _M0L3p__S983->$9;
  _M0L4e__eS994 = _M0L3p__S983->$10;
  _M0L4e__iS995 = _M0L3p__S983->$11;
  _M0L7_2abindS996 = 0;
  _M0L1iS997 = _M0L7_2abindS996;
  while (1) {
    if (_M0L1iS997 < _M0L1nS981) {
      struct _M0TPB5ArrayGfE* _M0L1vS2292 = _M0L1pS982->$2;
      float _M0L1vS998;
      struct _M0TPB5ArrayGfE* _M0L1mS2291;
      float _M0L1mS999;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS2290;
      float _M0L2nnS1000;
      struct _M0TPB5ArrayGfE* _M0L1hS2289;
      float _M0L1hS1001;
      struct _M0TPB5ArrayGfE* _M0L2geS2288;
      float _M0L2geS1002;
      struct _M0TPB5ArrayGfE* _M0L2giS2287;
      float _M0L2giS1003;
      struct _M0TPB5ArrayGbE* _M0L4fireS2190;
      float _M0L6_2atmpS2286;
      float _M0L7am__numS1004;
      float _M0L6_2atmpS2285;
      float _M0L7bm__numS1005;
      float _M0L6_2atmpS2280;
      float _M0L6_2atmpS2279;
      float _M0L6_2atmpS2278;
      float _M0L2amS1006;
      float _M0L6_2atmpS2273;
      float _M0L6_2atmpS2272;
      float _M0L6_2atmpS2271;
      float _M0L2bmS1007;
      struct _M0TPB5ArrayGfE* _M0L1mS2191;
      float _M0L6_2atmpS2197;
      float _M0L6_2atmpS2195;
      float _M0L6_2atmpS2196;
      float _M0L6_2atmpS2194;
      float _M0L6_2atmpS2193;
      float _M0L6_2atmpS2192;
      float _M0L6_2atmpS2270;
      float _M0L7an__numS1009;
      float _M0L6_2atmpS2265;
      float _M0L6_2atmpS2264;
      float _M0L6_2atmpS2263;
      float _M0L2anS1010;
      float _M0L6_2atmpS2262;
      float _M0L6_2atmpS2261;
      float _M0L6_2atmpS2260;
      float _M0L6_2atmpS2259;
      float _M0L2bnS1011;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS2198;
      float _M0L6_2atmpS2204;
      float _M0L6_2atmpS2202;
      float _M0L6_2atmpS2203;
      float _M0L6_2atmpS2201;
      float _M0L6_2atmpS2200;
      float _M0L6_2atmpS2199;
      float _M0L6_2atmpS2258;
      float _M0L6_2atmpS2257;
      float _M0L6_2atmpS2256;
      float _M0L6_2atmpS2255;
      float _M0L2ahS1012;
      float _M0L6_2atmpS2254;
      float _M0L6_2atmpS2253;
      float _M0L6_2atmpS2252;
      float _M0L6_2atmpS2251;
      float _M0L9bh__denomS1013;
      float _M0L2bhS1014;
      struct _M0TPB5ArrayGfE* _M0L1hS2205;
      float _M0L6_2atmpS2211;
      float _M0L6_2atmpS2209;
      float _M0L6_2atmpS2210;
      float _M0L6_2atmpS2208;
      float _M0L6_2atmpS2207;
      float _M0L6_2atmpS2206;
      struct _M0TPB5ArrayGfE* _M0L1mS2250;
      float _M0L6m__newS1015;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS2249;
      float _M0L6n__newS1016;
      struct _M0TPB5ArrayGfE* _M0L1hS2248;
      float _M0L6h__newS1017;
      float _M0L6_2atmpS2247;
      float _M0L6_2atmpS2246;
      float _M0L3m3hS1018;
      float _M0L6_2atmpS2245;
      float _M0L6_2atmpS2244;
      float _M0L2n4S1019;
      struct _M0TPB5ArrayGfE* _M0L1iS2243;
      float _M0L6_2atmpS2240;
      float _M0L6_2atmpS2242;
      float _M0L6_2atmpS2241;
      float _M0L6_2atmpS2237;
      float _M0L6_2atmpS2239;
      float _M0L6_2atmpS2238;
      float _M0L6_2atmpS2234;
      float _M0L6_2atmpS2236;
      float _M0L6_2atmpS2235;
      float _M0L6_2atmpS2230;
      float _M0L6_2atmpS2232;
      float _M0L6_2atmpS2233;
      float _M0L6_2atmpS2231;
      float _M0L6_2atmpS2226;
      float _M0L6_2atmpS2228;
      float _M0L6_2atmpS2229;
      float _M0L6_2atmpS2227;
      float _M0L7currentS1020;
      struct _M0TPB5ArrayGfE* _M0L1vS2212;
      float _M0L6_2atmpS2215;
      float _M0L6_2atmpS2214;
      float _M0L6_2atmpS2213;
      struct _M0TPB5ArrayGfE* _M0L2geS2216;
      float _M0L6_2atmpS2220;
      float _M0L6_2atmpS2219;
      float _M0L6_2atmpS2218;
      float _M0L6_2atmpS2217;
      struct _M0TPB5ArrayGfE* _M0L2giS2221;
      float _M0L6_2atmpS2225;
      float _M0L6_2atmpS2224;
      float _M0L6_2atmpS2223;
      float _M0L6_2atmpS2222;
      int32_t _M0L6_2atmpS2293;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS998 = _M0MPC15array5Array2atGfE(_M0L1vS2292, _M0L1iS997);
      _M0L1mS2291 = _M0L1pS982->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS999 = _M0MPC15array5Array2atGfE(_M0L1mS2291, _M0L1iS997);
      _M0L7n__gateS2290 = _M0L1pS982->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1000 = _M0MPC15array5Array2atGfE(_M0L7n__gateS2290, _M0L1iS997);
      _M0L1hS2289 = _M0L1pS982->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1001 = _M0MPC15array5Array2atGfE(_M0L1hS2289, _M0L1iS997);
      _M0L2geS2288 = _M0L1pS982->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1002 = _M0MPC15array5Array2atGfE(_M0L2geS2288, _M0L1iS997);
      _M0L2giS2287 = _M0L1pS982->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1003 = _M0MPC15array5Array2atGfE(_M0L2giS2287, _M0L1iS997);
      _M0L4fireS2190 = _M0L1pS982->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2190, _M0L1iS997, 0);
      _M0L6_2atmpS2286 = 0x1.ap+3f - _M0L1vS998;
      _M0L7am__numS1004 = _M0L6_2atmpS2286 + _M0L2vtS991;
      _M0L6_2atmpS2285 = _M0L1vS998 - _M0L2vtS991;
      _M0L7bm__numS1005 = _M0L6_2atmpS2285 - 0x1.4p+5f;
      _M0L6_2atmpS2280 = _M0L7am__numS1004 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2279 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2280);
      _M0L6_2atmpS2278 = _M0L6_2atmpS2279 - 0x1p+0f;
      if (_M0L6_2atmpS2278 != 0x0p+0f) {
        float _M0L6_2atmpS2281 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1004;
        float _M0L6_2atmpS2284 = _M0L7am__numS1004 / 0x1p+2f;
        float _M0L6_2atmpS2283;
        float _M0L6_2atmpS2282;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS2283 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2284);
        _M0L6_2atmpS2282 = _M0L6_2atmpS2283 - 0x1p+0f;
        _M0L2amS1006 = _M0L6_2atmpS2281 / _M0L6_2atmpS2282;
      } else {
        _M0L2amS1006 = 0x0p+0f;
      }
      _M0L6_2atmpS2273 = _M0L7bm__numS1005 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2272 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2273);
      _M0L6_2atmpS2271 = _M0L6_2atmpS2272 - 0x1p+0f;
      if (_M0L6_2atmpS2271 != 0x0p+0f) {
        float _M0L6_2atmpS2274 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1005;
        float _M0L6_2atmpS2277 = _M0L7bm__numS1005 / 0x1.4p+2f;
        float _M0L6_2atmpS2276;
        float _M0L6_2atmpS2275;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS2276 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2277);
        _M0L6_2atmpS2275 = _M0L6_2atmpS2276 - 0x1p+0f;
        _M0L2bmS1007 = _M0L6_2atmpS2274 / _M0L6_2atmpS2275;
      } else {
        _M0L2bmS1007 = 0x0p+0f;
      }
      _M0L1mS2191 = _M0L1pS982->$3;
      _M0L6_2atmpS2197 = 0x1p+0f - _M0L1mS999;
      _M0L6_2atmpS2195 = _M0L2amS1006 * _M0L6_2atmpS2197;
      _M0L6_2atmpS2196 = _M0L2bmS1007 * _M0L1mS999;
      _M0L6_2atmpS2194 = _M0L6_2atmpS2195 - _M0L6_2atmpS2196;
      _M0L6_2atmpS2193 = _M0L2dtS1008 * _M0L6_2atmpS2194;
      _M0L6_2atmpS2192 = _M0L1mS999 + _M0L6_2atmpS2193;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS2191, _M0L1iS997, _M0L6_2atmpS2192);
      _M0L6_2atmpS2270 = 0x1.ep+3f - _M0L1vS998;
      _M0L7an__numS1009 = _M0L6_2atmpS2270 + _M0L2vtS991;
      _M0L6_2atmpS2265 = _M0L7an__numS1009 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2264 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2265);
      _M0L6_2atmpS2263 = _M0L6_2atmpS2264 - 0x1p+0f;
      if (_M0L6_2atmpS2263 != 0x0p+0f) {
        float _M0L6_2atmpS2266 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1009;
        float _M0L6_2atmpS2269 = _M0L7an__numS1009 / 0x1.4p+2f;
        float _M0L6_2atmpS2268;
        float _M0L6_2atmpS2267;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS2268 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2269);
        _M0L6_2atmpS2267 = _M0L6_2atmpS2268 - 0x1p+0f;
        _M0L2anS1010 = _M0L6_2atmpS2266 / _M0L6_2atmpS2267;
      } else {
        _M0L2anS1010 = 0x0p+0f;
      }
      _M0L6_2atmpS2262 = 0x1.4p+3f - _M0L1vS998;
      _M0L6_2atmpS2261 = _M0L6_2atmpS2262 + _M0L2vtS991;
      _M0L6_2atmpS2260 = _M0L6_2atmpS2261 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2259 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2260);
      _M0L2bnS1011 = 0x1p-1f * _M0L6_2atmpS2259;
      _M0L7n__gateS2198 = _M0L1pS982->$4;
      _M0L6_2atmpS2204 = 0x1p+0f - _M0L2nnS1000;
      _M0L6_2atmpS2202 = _M0L2anS1010 * _M0L6_2atmpS2204;
      _M0L6_2atmpS2203 = _M0L2bnS1011 * _M0L2nnS1000;
      _M0L6_2atmpS2201 = _M0L6_2atmpS2202 - _M0L6_2atmpS2203;
      _M0L6_2atmpS2200 = _M0L2dtS1008 * _M0L6_2atmpS2201;
      _M0L6_2atmpS2199 = _M0L2nnS1000 + _M0L6_2atmpS2200;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS2198, _M0L1iS997, _M0L6_2atmpS2199);
      _M0L6_2atmpS2258 = 0x1.1p+4f - _M0L1vS998;
      _M0L6_2atmpS2257 = _M0L6_2atmpS2258 + _M0L2vtS991;
      _M0L6_2atmpS2256 = _M0L6_2atmpS2257 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2255 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2256);
      _M0L2ahS1012 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS2255;
      _M0L6_2atmpS2254 = 0x1.4p+5f - _M0L1vS998;
      _M0L6_2atmpS2253 = _M0L6_2atmpS2254 + _M0L2vtS991;
      _M0L6_2atmpS2252 = _M0L6_2atmpS2253 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2251 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2252);
      _M0L9bh__denomS1013 = 0x1p+0f + _M0L6_2atmpS2251;
      if (_M0L9bh__denomS1013 != 0x0p+0f) {
        _M0L2bhS1014 = 0x1p+2f / _M0L9bh__denomS1013;
      } else {
        _M0L2bhS1014 = 0x0p+0f;
      }
      _M0L1hS2205 = _M0L1pS982->$5;
      _M0L6_2atmpS2211 = 0x1p+0f - _M0L1hS1001;
      _M0L6_2atmpS2209 = _M0L2ahS1012 * _M0L6_2atmpS2211;
      _M0L6_2atmpS2210 = _M0L2bhS1014 * _M0L1hS1001;
      _M0L6_2atmpS2208 = _M0L6_2atmpS2209 - _M0L6_2atmpS2210;
      _M0L6_2atmpS2207 = _M0L2dtS1008 * _M0L6_2atmpS2208;
      _M0L6_2atmpS2206 = _M0L1hS1001 + _M0L6_2atmpS2207;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS2205, _M0L1iS997, _M0L6_2atmpS2206);
      _M0L1mS2250 = _M0L1pS982->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1015 = _M0MPC15array5Array2atGfE(_M0L1mS2250, _M0L1iS997);
      _M0L7n__gateS2249 = _M0L1pS982->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1016
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS2249, _M0L1iS997);
      _M0L1hS2248 = _M0L1pS982->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1017 = _M0MPC15array5Array2atGfE(_M0L1hS2248, _M0L1iS997);
      _M0L6_2atmpS2247 = _M0L6m__newS1015 * _M0L6m__newS1015;
      _M0L6_2atmpS2246 = _M0L6_2atmpS2247 * _M0L6m__newS1015;
      _M0L3m3hS1018 = _M0L6_2atmpS2246 * _M0L6h__newS1017;
      _M0L6_2atmpS2245 = _M0L6n__newS1016 * _M0L6n__newS1016;
      _M0L6_2atmpS2244 = _M0L6_2atmpS2245 * _M0L6n__newS1016;
      _M0L2n4S1019 = _M0L6_2atmpS2244 * _M0L6n__newS1016;
      _M0L1iS2243 = _M0L1pS982->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2240 = _M0MPC15array5Array2atGfE(_M0L1iS2243, _M0L1iS997);
      _M0L6_2atmpS2242 = _M0L2elS986 - _M0L1vS998;
      _M0L6_2atmpS2241 = _M0L2glS985 * _M0L6_2atmpS2242;
      _M0L6_2atmpS2237 = _M0L6_2atmpS2240 + _M0L6_2atmpS2241;
      _M0L6_2atmpS2239 = _M0L4e__eS994 - _M0L1vS998;
      _M0L6_2atmpS2238 = _M0L2geS1002 * _M0L6_2atmpS2239;
      _M0L6_2atmpS2234 = _M0L6_2atmpS2237 + _M0L6_2atmpS2238;
      _M0L6_2atmpS2236 = _M0L4e__iS995 - _M0L1vS998;
      _M0L6_2atmpS2235 = _M0L2giS1003 * _M0L6_2atmpS2236;
      _M0L6_2atmpS2230 = _M0L6_2atmpS2234 + _M0L6_2atmpS2235;
      _M0L6_2atmpS2232 = _M0L2gnS989 * _M0L3m3hS1018;
      _M0L6_2atmpS2233 = _M0L2enS988 - _M0L1vS998;
      _M0L6_2atmpS2231 = _M0L6_2atmpS2232 * _M0L6_2atmpS2233;
      _M0L6_2atmpS2226 = _M0L6_2atmpS2230 + _M0L6_2atmpS2231;
      _M0L6_2atmpS2228 = _M0L2gkS990 * _M0L2n4S1019;
      _M0L6_2atmpS2229 = _M0L2ekS987 - _M0L1vS998;
      _M0L6_2atmpS2227 = _M0L6_2atmpS2228 * _M0L6_2atmpS2229;
      _M0L7currentS1020 = _M0L6_2atmpS2226 + _M0L6_2atmpS2227;
      _M0L1vS2212 = _M0L1pS982->$2;
      _M0L6_2atmpS2215 = _M0L2dtS1008 / _M0L2cmS984;
      _M0L6_2atmpS2214 = _M0L6_2atmpS2215 * _M0L7currentS1020;
      _M0L6_2atmpS2213 = _M0L1vS998 + _M0L6_2atmpS2214;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2212, _M0L1iS997, _M0L6_2atmpS2213);
      _M0L2geS2216 = _M0L1pS982->$8;
      _M0L6_2atmpS2220 = -_M0L2geS1002;
      _M0L6_2atmpS2219 = _M0L6_2atmpS2220 / _M0L6tau__eS992;
      _M0L6_2atmpS2218 = _M0L2dtS1008 * _M0L6_2atmpS2219;
      _M0L6_2atmpS2217 = _M0L2geS1002 + _M0L6_2atmpS2218;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2216, _M0L1iS997, _M0L6_2atmpS2217);
      _M0L2giS2221 = _M0L1pS982->$9;
      _M0L6_2atmpS2225 = -_M0L2giS1003;
      _M0L6_2atmpS2224 = _M0L6_2atmpS2225 / _M0L6tau__iS993;
      _M0L6_2atmpS2223 = _M0L2dtS1008 * _M0L6_2atmpS2224;
      _M0L6_2atmpS2222 = _M0L2giS1003 + _M0L6_2atmpS2223;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2221, _M0L1iS997, _M0L6_2atmpS2222);
      _M0L6_2atmpS2293 = _M0L1iS997 + 1;
      _M0L1iS997 = _M0L6_2atmpS2293;
      continue;
    }
    break;
  }
  _M0L7_2abindS1022 = 0;
  _M0L1iS1023 = _M0L7_2abindS1022;
  while (1) {
    if (_M0L1iS1023 < _M0L1nS981) {
      struct _M0TPB5ArrayGbE* _M0L4fireS2294 = _M0L1pS982->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS2297 = _M0L1pS982->$2;
      float _M0L6_2atmpS2296;
      int32_t _M0L6_2atmpS2295;
      int32_t _M0L6_2atmpS2298;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2296 = _M0MPC15array5Array2atGfE(_M0L1vS2297, _M0L1iS1023);
      _M0L6_2atmpS2295 = _M0L6_2atmpS2296 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2294, _M0L1iS1023, _M0L6_2atmpS2295);
      _M0L6_2atmpS2298 = _M0L1iS1023 + 1;
      _M0L1iS1023 = _M0L6_2atmpS2298;
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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS969,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS972,
  struct _M0TPB5ArrayGfE* _M0L7post__gS978
) {
  int32_t _M0L4rowsS968;
  int32_t _M0L7_2abindS970;
  int32_t _M0L1iS971;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS968 = _M0L1mS969->$0;
  _M0L7_2abindS970 = 0;
  _M0L1iS971 = _M0L7_2abindS970;
  while (1) {
    if (_M0L1iS971 < _M0L4rowsS968) {
      int32_t _M0L6_2atmpS2189;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS972, _M0L1iS971)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2188 = _M0L1mS969->$2;
        int32_t _M0L5startS973;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2186;
        int32_t _M0L6_2atmpS2187;
        int32_t _M0L3endS974;
        int32_t _M0L1kS975;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS973
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2188, _M0L1iS971);
        _M0L6rowptrS2186 = _M0L1mS969->$2;
        _M0L6_2atmpS2187 = _M0L1iS971 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS974
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2186, _M0L6_2atmpS2187);
        _M0L1kS975 = _M0L5startS973;
        while (1) {
          if (_M0L1kS975 < _M0L3endS974) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS2184 = _M0L1mS969->$3;
            int32_t _M0L9post__idxS976;
            struct _M0TPB5ArrayGfE* _M0L4valsS2183;
            float _M0L1wS977;
            float _M0L6_2atmpS2182;
            float _M0L6_2atmpS2181;
            int32_t _M0L6_2atmpS2185;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS976
            = _M0MPC15array5Array2atGiE(_M0L6colptrS2184, _M0L1kS975);
            _M0L4valsS2183 = _M0L1mS969->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS977
            = _M0MPC15array5Array2atGfE(_M0L4valsS2183, _M0L1kS975);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS2182
            = _M0MPC15array5Array2atGfE(_M0L7post__gS978, _M0L9post__idxS976);
            _M0L6_2atmpS2181 = _M0L6_2atmpS2182 + _M0L1wS977;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS978, _M0L9post__idxS976, _M0L6_2atmpS2181);
            _M0L6_2atmpS2185 = _M0L1kS975 + 1;
            _M0L1kS975 = _M0L6_2atmpS2185;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2189 = _M0L1iS971 + 1;
      _M0L1iS971 = _M0L6_2atmpS2189;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS962,
  int32_t _M0L4colsS963,
  float _M0L2muS964,
  float _M0L5sigmaS965,
  float _M0L1pS966,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS967
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS962, _M0L4colsS963, _M0L2muS964, _M0L5sigmaS965, _M0L1pS966, 0, _M0L3rngS967);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS876,
  int32_t _M0L4colsS880,
  float _M0L2muS886,
  float _M0L5sigmaS887,
  float _M0L1pS899,
  int32_t _M0L4ruleS893,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS889
) {
  float* _M0L6_2atmpS2180;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2179;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS875;
  int32_t _M0L7_2abindS877;
  int32_t _M0L1iS878;
  int32_t _M0L6_2atmpS2178;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS952;
  int32_t* _M0L6_2atmpS2177;
  struct _M0TPB5ArrayGiE* _M0L6colptrS953;
  float* _M0L6_2atmpS2176;
  struct _M0TPB5ArrayGfE* _M0L4valsS954;
  int32_t _M0L7_2abindS955;
  int32_t _M0L1iS956;
  int32_t _M0L6_2atmpS2175;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2468;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2180 = moonbit_empty_float_array;
  _M0L6_2atmpS2179
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2179)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 35, 0);
  _M0L6_2atmpS2179->$0 = _M0L6_2atmpS2180;
  _M0L6_2atmpS2179->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS875
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS876, _M0L6_2atmpS2179);
  _M0L7_2abindS877 = 0;
  _M0L1iS878 = _M0L7_2abindS877;
  while (1) {
    if (_M0L1iS878 < _M0L4rowsS876) {
      struct _M0TPB5ArrayGfE* _M0L3rowS879;
      int32_t _M0L7_2abindS881;
      int32_t _M0L1jS882;
      int32_t _M0L6_2atmpS2131;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS879 = _M0MPC15array5Array4makeGfE(_M0L4colsS880, 0x0p+0f);
      _M0L7_2abindS881 = 0;
      _M0L1jS882 = _M0L7_2abindS881;
      while (1) {
        if (_M0L1jS882 < _M0L4colsS880) {
          double _M0L2z1S884;
          struct _M0TUddE* _M0L7_2abindS888;
          double _M0L5_2az1S890;
          float _M0L6_2atmpS2129;
          float _M0L6_2atmpS2128;
          float _M0L1wS885;
          int32_t _M0L6_2atmpS2130;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS888
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS889);
          _M0L5_2az1S890 = _M0L7_2abindS888->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS888);
          _M0L2z1S884 = _M0L5_2az1S890;
          goto join_883;
          goto joinlet_2451;
          join_883:;
          _M0L6_2atmpS2129 = (float)_M0L2z1S884;
          _M0L6_2atmpS2128 = _M0L5sigmaS887 * _M0L6_2atmpS2129;
          _M0L1wS885 = _M0L2muS886 + _M0L6_2atmpS2128;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS879, _M0L1jS882, _M0L1wS885);
          joinlet_2451:;
          _M0L6_2atmpS2130 = _M0L1jS882 + 1;
          _M0L1jS882 = _M0L6_2atmpS2130;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS878, _M0L3rowS879);
      _M0L6_2atmpS2131 = _M0L1iS878 + 1;
      _M0L1iS878 = _M0L6_2atmpS2131;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS893) {
    case 0: {
      int32_t _M0L7_2abindS894 = 0;
      int32_t _M0L1iS895 = _M0L7_2abindS894;
      while (1) {
        if (_M0L1iS895 < _M0L4rowsS876) {
          int32_t _M0L7_2abindS896 = 0;
          int32_t _M0L1jS897 = _M0L7_2abindS896;
          int32_t _M0L6_2atmpS2134;
          while (1) {
            if (_M0L1jS897 < _M0L4colsS880) {
              float _M0L1uS898;
              int32_t _M0L6_2atmpS2133;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS898 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS889);
              if (_M0L1uS898 >= _M0L1pS899) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2132;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2132
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS895);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2132, _M0L1jS897, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2132);
              }
              _M0L6_2atmpS2133 = _M0L1jS897 + 1;
              _M0L1jS897 = _M0L6_2atmpS2133;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2134 = _M0L1iS895 + 1;
          _M0L1iS895 = _M0L6_2atmpS2134;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2152 = (float)_M0L4rowsS876;
      float _M0L6_2atmpS2151 = _M0L6_2atmpS2152 * _M0L1pS899;
      int32_t _M0L7n__keepS902;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS902 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2151);
      if (_M0L7n__keepS902 > 0 && _M0L7n__keepS902 <= _M0L4rowsS876) {
        int32_t _M0L7_2abindS903 = 0;
        int32_t _M0L1jS904 = _M0L7_2abindS903;
        while (1) {
          if (_M0L1jS904 < _M0L4colsS880) {
            int32_t* _M0L6_2atmpS2146 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS905 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS906;
            int32_t _M0L1kS907;
            int32_t _M0L7n__dropS909;
            int32_t _M0L7_2abindS910;
            int32_t _M0L1kS911;
            int32_t _M0L7_2abindS917;
            int32_t _M0L1kS918;
            int32_t _M0L6_2atmpS2147;
            Moonbit_object_header(_M0L8pre__idxS905)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 38, 0);
            _M0L8pre__idxS905->$0 = _M0L6_2atmpS2146;
            _M0L8pre__idxS905->$1 = 0;
            _M0L7_2abindS906 = 0;
            _M0L1kS907 = _M0L7_2abindS906;
            while (1) {
              if (_M0L1kS907 < _M0L4rowsS876) {
                int32_t _M0L6_2atmpS2135;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS905, _M0L1kS907);
                _M0L6_2atmpS2135 = _M0L1kS907 + 1;
                _M0L1kS907 = _M0L6_2atmpS2135;
                continue;
              }
              break;
            }
            _M0L7n__dropS909 = _M0L4rowsS876 - _M0L7n__keepS902;
            _M0L7_2abindS910 = 0;
            _M0L1kS911 = _M0L7_2abindS910;
            while (1) {
              if (_M0L1kS911 < _M0L7n__dropS909) {
                float _M0L1uS912;
                float _M0L6_2atmpS2139;
                float _M0L6_2atmpS2141;
                float _M0L6_2atmpS2140;
                float _M0L6_2atmpS2138;
                int32_t _M0L6_2atmpS2137;
                int32_t _M0L6r__idxS913;
                int32_t _M0L10r__clampedS914;
                int32_t _M0L3tmpS915;
                int32_t _M0L6_2atmpS2136;
                int32_t _M0L6_2atmpS2142;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS912 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS889);
                _M0L6_2atmpS2139 = (float)_M0L4rowsS876;
                _M0L6_2atmpS2141 = (float)_M0L1kS911;
                _M0L6_2atmpS2140 = _M0L6_2atmpS2141 * _M0L1uS912;
                _M0L6_2atmpS2138 = _M0L6_2atmpS2139 - _M0L6_2atmpS2140;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2137
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2138);
                _M0L6r__idxS913 = _M0L1kS911 + _M0L6_2atmpS2137;
                if (_M0L6r__idxS913 >= _M0L4rowsS876) {
                  _M0L10r__clampedS914 = _M0L4rowsS876 - 1;
                } else {
                  _M0L10r__clampedS914 = _M0L6r__idxS913;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS915
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS905, _M0L1kS911);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2136
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS905, _M0L10r__clampedS914);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS905, _M0L1kS911, _M0L6_2atmpS2136);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS905, _M0L10r__clampedS914, _M0L3tmpS915);
                _M0L6_2atmpS2142 = _M0L1kS911 + 1;
                _M0L1kS911 = _M0L6_2atmpS2142;
                continue;
              }
              break;
            }
            _M0L7_2abindS917 = 0;
            _M0L1kS918 = _M0L7_2abindS917;
            while (1) {
              if (_M0L1kS918 < _M0L7n__dropS909) {
                int32_t _M0L6_2atmpS2144;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2143;
                int32_t _M0L6_2atmpS2145;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2144
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS905, _M0L1kS918);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2143
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L6_2atmpS2144);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2143, _M0L1jS904, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2143);
                _M0L6_2atmpS2145 = _M0L1kS918 + 1;
                _M0L1kS918 = _M0L6_2atmpS2145;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS905);
              }
              break;
            }
            _M0L6_2atmpS2147 = _M0L1jS904 + 1;
            _M0L1jS904 = _M0L6_2atmpS2147;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS902 == 0) {
        int32_t _M0L7_2abindS921 = 0;
        int32_t _M0L1iS922 = _M0L7_2abindS921;
        while (1) {
          if (_M0L1iS922 < _M0L4rowsS876) {
            int32_t _M0L7_2abindS923 = 0;
            int32_t _M0L1jS924 = _M0L7_2abindS923;
            int32_t _M0L6_2atmpS2150;
            while (1) {
              if (_M0L1jS924 < _M0L4colsS880) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2148;
                int32_t _M0L6_2atmpS2149;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2148
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS922);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2148, _M0L1jS924, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2148);
                _M0L6_2atmpS2149 = _M0L1jS924 + 1;
                _M0L1jS924 = _M0L6_2atmpS2149;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2150 = _M0L1iS922 + 1;
            _M0L1iS922 = _M0L6_2atmpS2150;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2170 = (float)_M0L4colsS880;
      float _M0L6_2atmpS2169 = _M0L6_2atmpS2170 * _M0L1pS899;
      int32_t _M0L7n__keepS927;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS927 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2169);
      if (_M0L7n__keepS927 > 0 && _M0L7n__keepS927 <= _M0L4colsS880) {
        int32_t _M0L7_2abindS928 = 0;
        int32_t _M0L1iS929 = _M0L7_2abindS928;
        while (1) {
          if (_M0L1iS929 < _M0L4rowsS876) {
            int32_t* _M0L6_2atmpS2164 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS930 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS931;
            int32_t _M0L1kS932;
            int32_t _M0L7n__dropS934;
            int32_t _M0L7_2abindS935;
            int32_t _M0L1kS936;
            int32_t _M0L7_2abindS942;
            int32_t _M0L1kS943;
            int32_t _M0L6_2atmpS2165;
            Moonbit_object_header(_M0L9post__idxS930)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 38, 0);
            _M0L9post__idxS930->$0 = _M0L6_2atmpS2164;
            _M0L9post__idxS930->$1 = 0;
            _M0L7_2abindS931 = 0;
            _M0L1kS932 = _M0L7_2abindS931;
            while (1) {
              if (_M0L1kS932 < _M0L4colsS880) {
                int32_t _M0L6_2atmpS2153;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS930, _M0L1kS932);
                _M0L6_2atmpS2153 = _M0L1kS932 + 1;
                _M0L1kS932 = _M0L6_2atmpS2153;
                continue;
              }
              break;
            }
            _M0L7n__dropS934 = _M0L4colsS880 - _M0L7n__keepS927;
            _M0L7_2abindS935 = 0;
            _M0L1kS936 = _M0L7_2abindS935;
            while (1) {
              if (_M0L1kS936 < _M0L7n__dropS934) {
                float _M0L1uS937;
                float _M0L6_2atmpS2157;
                float _M0L6_2atmpS2159;
                float _M0L6_2atmpS2158;
                float _M0L6_2atmpS2156;
                int32_t _M0L6_2atmpS2155;
                int32_t _M0L6r__idxS938;
                int32_t _M0L10r__clampedS939;
                int32_t _M0L3tmpS940;
                int32_t _M0L6_2atmpS2154;
                int32_t _M0L6_2atmpS2160;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS937 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS889);
                _M0L6_2atmpS2157 = (float)_M0L4colsS880;
                _M0L6_2atmpS2159 = (float)_M0L1kS936;
                _M0L6_2atmpS2158 = _M0L6_2atmpS2159 * _M0L1uS937;
                _M0L6_2atmpS2156 = _M0L6_2atmpS2157 - _M0L6_2atmpS2158;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2155
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2156);
                _M0L6r__idxS938 = _M0L1kS936 + _M0L6_2atmpS2155;
                if (_M0L6r__idxS938 >= _M0L4colsS880) {
                  _M0L10r__clampedS939 = _M0L4colsS880 - 1;
                } else {
                  _M0L10r__clampedS939 = _M0L6r__idxS938;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS940
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS930, _M0L1kS936);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2154
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS930, _M0L10r__clampedS939);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS930, _M0L1kS936, _M0L6_2atmpS2154);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS930, _M0L10r__clampedS939, _M0L3tmpS940);
                _M0L6_2atmpS2160 = _M0L1kS936 + 1;
                _M0L1kS936 = _M0L6_2atmpS2160;
                continue;
              }
              break;
            }
            _M0L7_2abindS942 = 0;
            _M0L1kS943 = _M0L7_2abindS942;
            while (1) {
              if (_M0L1kS943 < _M0L7n__dropS934) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2161;
                int32_t _M0L6_2atmpS2162;
                int32_t _M0L6_2atmpS2163;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2161
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS929);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2162
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS930, _M0L1kS943);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2161, _M0L6_2atmpS2162, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2161);
                _M0L6_2atmpS2163 = _M0L1kS943 + 1;
                _M0L1kS943 = _M0L6_2atmpS2163;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS930);
              }
              break;
            }
            _M0L6_2atmpS2165 = _M0L1iS929 + 1;
            _M0L1iS929 = _M0L6_2atmpS2165;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS927 == 0) {
        int32_t _M0L7_2abindS946 = 0;
        int32_t _M0L1iS947 = _M0L7_2abindS946;
        while (1) {
          if (_M0L1iS947 < _M0L4rowsS876) {
            int32_t _M0L7_2abindS948 = 0;
            int32_t _M0L1jS949 = _M0L7_2abindS948;
            int32_t _M0L6_2atmpS2168;
            while (1) {
              if (_M0L1jS949 < _M0L4colsS880) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2166;
                int32_t _M0L6_2atmpS2167;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2166
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS947);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2166, _M0L1jS949, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2166);
                _M0L6_2atmpS2167 = _M0L1jS949 + 1;
                _M0L1jS949 = _M0L6_2atmpS2167;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2168 = _M0L1iS947 + 1;
            _M0L1iS947 = _M0L6_2atmpS2168;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2178 = _M0L4rowsS876 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS952 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2178, 0);
  _M0L6_2atmpS2177 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS953
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS953)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 38, 0);
  _M0L6colptrS953->$0 = _M0L6_2atmpS2177;
  _M0L6colptrS953->$1 = 0;
  _M0L6_2atmpS2176 = moonbit_empty_float_array;
  _M0L4valsS954
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS954)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 35, 0);
  _M0L4valsS954->$0 = _M0L6_2atmpS2176;
  _M0L4valsS954->$1 = 0;
  _M0L7_2abindS955 = 0;
  _M0L1iS956 = _M0L7_2abindS955;
  while (1) {
    if (_M0L1iS956 < _M0L4rowsS876) {
      int32_t _M0L6_2atmpS2171;
      int32_t _M0L7_2abindS957;
      int32_t _M0L1jS958;
      int32_t _M0L6_2atmpS2174;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2171 = _M0MPC15array5Array6lengthGfE(_M0L4valsS954);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS952, _M0L1iS956, _M0L6_2atmpS2171);
      _M0L7_2abindS957 = 0;
      _M0L1jS958 = _M0L7_2abindS957;
      while (1) {
        if (_M0L1jS958 < _M0L4colsS880) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2172;
          float _M0L1vS959;
          int32_t _M0L6_2atmpS2173;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2172
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS875, _M0L1iS956);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS959
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2172, _M0L1jS958);
          moonbit_decref_cycle_free(_M0L6_2atmpS2172);
          if (_M0L1vS959 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS953, _M0L1jS958);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS954, _M0L1vS959);
          }
          _M0L6_2atmpS2173 = _M0L1jS958 + 1;
          _M0L1jS958 = _M0L6_2atmpS2173;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2174 = _M0L1iS956 + 1;
      _M0L1iS956 = _M0L6_2atmpS2174;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS875);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2175 = _M0MPC15array5Array6lengthGfE(_M0L4valsS954);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS952, _M0L4rowsS876, _M0L6_2atmpS2175);
  _block_2468
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2468)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 41, 0);
  _block_2468->$0 = _M0L4rowsS876;
  _block_2468->$1 = _M0L4colsS880;
  _block_2468->$2 = _M0L6rowptrS952;
  _block_2468->$3 = _M0L6colptrS953;
  _block_2468->$4 = _M0L4valsS954;
  return _block_2468;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS874
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS2127;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS2127 = _M0L1mS874->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS2127);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS872
) {
  struct _M0TUmmmmE* _M0L1sS871;
  uint64_t _M0L6_2atmpS2126;
  struct _M0TUmmmmE* _M0L1tS873;
  uint64_t _M0L6_2atmpS2122;
  uint64_t _M0L6_2atmpS2123;
  uint64_t _M0L6_2atmpS2124;
  uint64_t _M0L6_2atmpS2125;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2469;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS871 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS872);
  _M0L6_2atmpS2126 = _M0L1sS871->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS873 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2126);
  _M0L6_2atmpS2122 = _M0L1sS871->$0;
  _M0L6_2atmpS2123 = _M0L1sS871->$1;
  _M0L6_2atmpS2124 = _M0L1sS871->$2;
  moonbit_decref_cycle_free(_M0L1sS871);
  _M0L6_2atmpS2125 = _M0L1tS873->$0;
  moonbit_decref_cycle_free(_M0L1tS873);
  _block_2469
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2469)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2469->$0 = _M0L6_2atmpS2122;
  _block_2469->$1 = _M0L6_2atmpS2123;
  _block_2469->$2 = _M0L6_2atmpS2124;
  _block_2469->$3 = _M0L6_2atmpS2125;
  return _block_2469;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS863) {
  uint64_t _M0L2s1S862;
  uint64_t _M0L2z1S864;
  uint64_t _M0L2s2S865;
  uint64_t _M0L2z2S866;
  uint64_t _M0L2s3S867;
  uint64_t _M0L2z3S868;
  uint64_t _M0L2s4S869;
  uint64_t _M0L2z4S870;
  struct _M0TUmmmmE* _block_2470;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S862 = _M0L4seedS863 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S864 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S862);
  _M0L2s2S865 = _M0L2s1S862 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S866 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S865);
  _M0L2s3S867 = _M0L2s2S865 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S868 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S867);
  _M0L2s4S869 = _M0L2s3S867 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S870 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S869);
  _block_2470 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2470)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2470->$0 = _M0L2z1S864;
  _block_2470->$1 = _M0L2z2S866;
  _block_2470->$2 = _M0L2z3S868;
  _block_2470->$3 = _M0L2z4S870;
  return _block_2470;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS860) {
  uint64_t _M0L6_2atmpS2121;
  uint64_t _M0L6_2atmpS2120;
  uint64_t _M0L1zS859;
  uint64_t _M0L6_2atmpS2119;
  uint64_t _M0L6_2atmpS2118;
  uint64_t _M0L1zS861;
  uint64_t _M0L6_2atmpS2117;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2121 = _M0L1zS860 >> 30;
  _M0L6_2atmpS2120 = _M0L1zS860 ^ _M0L6_2atmpS2121;
  _M0L1zS859 = _M0L6_2atmpS2120 * 13787848793156543929ull;
  _M0L6_2atmpS2119 = _M0L1zS859 >> 27;
  _M0L6_2atmpS2118 = _M0L1zS859 ^ _M0L6_2atmpS2119;
  _M0L1zS861 = _M0L6_2atmpS2118 * 10723151780598845931ull;
  _M0L6_2atmpS2117 = _M0L1zS861 >> 31;
  return _M0L1zS861 ^ _M0L6_2atmpS2117;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS854
) {
  double _M0L2u1S853;
  double _M0L8u1__safeS855;
  double _M0L2u2S856;
  double _M0L6_2atmpS2116;
  double _M0L6_2atmpS2115;
  double _M0L1rS857;
  double _M0L5thetaS858;
  double _M0L6_2atmpS2114;
  double _M0L6_2atmpS2111;
  double _M0L6_2atmpS2113;
  double _M0L6_2atmpS2112;
  struct _M0TUddE* _block_2471;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S853 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS854);
  if (_M0L2u1S853 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS855 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS855 = _M0L2u1S853;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S856 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS854);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2116 = _M0FPC14math2ln(_M0L8u1__safeS855);
  _M0L6_2atmpS2115 = -0x1p+1 * _M0L6_2atmpS2116;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS857 = sqrt(_M0L6_2atmpS2115);
  _M0L5thetaS858 = 0x1.921fb54442d18p+2 * _M0L2u2S856;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2114 = _M0FPC14math3cos(_M0L5thetaS858);
  _M0L6_2atmpS2111 = _M0L1rS857 * _M0L6_2atmpS2114;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2113 = _M0FPC14math3sin(_M0L5thetaS858);
  _M0L6_2atmpS2112 = _M0L1rS857 * _M0L6_2atmpS2113;
  _block_2471 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2471)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2471->$0 = _M0L6_2atmpS2111;
  _block_2471->$1 = _M0L6_2atmpS2112;
  return _block_2471;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS851
) {
  uint64_t _M0L1uS850;
  uint64_t _M0L4bitsS852;
  double _M0L6_2atmpS2110;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS850 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS851);
  _M0L4bitsS852 = _M0L1uS850 >> 11;
  _M0L6_2atmpS2110 = (double)_M0L4bitsS852;
  return _M0L6_2atmpS2110 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS848
) {
  uint32_t _M0L1uS847;
  uint32_t _M0L4bitsS849;
  double _M0L6_2atmpS2109;
  double _M0L6_2atmpS2108;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS847 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS848);
  _M0L4bitsS849 = _M0L1uS847 >> 8;
  _M0L6_2atmpS2109 = (double)_M0L4bitsS849;
  _M0L6_2atmpS2108 = _M0L6_2atmpS2109 * 0x1p-24;
  return (float)_M0L6_2atmpS2108;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS846
) {
  uint64_t _M0L1uS845;
  uint64_t _M0L6_2atmpS2107;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS845 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS846);
  _M0L6_2atmpS2107 = _M0L1uS845 >> 32;
  return (uint32_t)_M0L6_2atmpS2107;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS838
) {
  uint64_t _M0L2s0S837;
  uint64_t _M0L2s1S839;
  uint64_t _M0L2s2S840;
  uint64_t _M0L2s3S841;
  uint64_t _M0L3tmpS842;
  uint64_t _M0L6_2atmpS2106;
  uint64_t _M0L3resS843;
  uint64_t _M0L1tS844;
  uint64_t _M0L6_2atmpS2096;
  uint64_t _M0L6_2atmpS2097;
  uint64_t _M0L2s2S2099;
  uint64_t _M0L6_2atmpS2098;
  uint64_t _M0L2s3S2101;
  uint64_t _M0L6_2atmpS2100;
  uint64_t _M0L2s2S2103;
  uint64_t _M0L6_2atmpS2102;
  uint64_t _M0L2s3S2105;
  uint64_t _M0L6_2atmpS2104;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S837 = _M0L1rS838->$0;
  _M0L2s1S839 = _M0L1rS838->$1;
  _M0L2s2S840 = _M0L1rS838->$2;
  _M0L2s3S841 = _M0L1rS838->$3;
  _M0L3tmpS842 = _M0L2s0S837 + _M0L2s3S841;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2106 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS842, 23);
  _M0L3resS843 = _M0L6_2atmpS2106 + _M0L2s0S837;
  _M0L1tS844 = _M0L2s1S839 << 17;
  _M0L6_2atmpS2096 = _M0L2s2S840 ^ _M0L2s0S837;
  _M0L1rS838->$2 = _M0L6_2atmpS2096;
  _M0L6_2atmpS2097 = _M0L2s3S841 ^ _M0L2s1S839;
  _M0L1rS838->$3 = _M0L6_2atmpS2097;
  _M0L2s2S2099 = _M0L1rS838->$2;
  _M0L6_2atmpS2098 = _M0L2s1S839 ^ _M0L2s2S2099;
  _M0L1rS838->$1 = _M0L6_2atmpS2098;
  _M0L2s3S2101 = _M0L1rS838->$3;
  _M0L6_2atmpS2100 = _M0L2s0S837 ^ _M0L2s3S2101;
  _M0L1rS838->$0 = _M0L6_2atmpS2100;
  _M0L2s2S2103 = _M0L1rS838->$2;
  _M0L6_2atmpS2102 = _M0L2s2S2103 ^ _M0L1tS844;
  _M0L1rS838->$2 = _M0L6_2atmpS2102;
  _M0L2s3S2105 = _M0L1rS838->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2104 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2105, 45);
  _M0L1rS838->$3 = _M0L6_2atmpS2104;
  return _M0L3resS843;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS835, int32_t _M0L1kS836) {
  uint64_t _M0L6_2atmpS2093;
  int32_t _M0L6_2atmpS2095;
  uint64_t _M0L6_2atmpS2094;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2093 = _M0L1xS835 << (_M0L1kS836 & 63);
  _M0L6_2atmpS2095 = 64 - _M0L1kS836;
  _M0L6_2atmpS2094 = _M0L1xS835 >> (_M0L6_2atmpS2095 & 63);
  return _M0L6_2atmpS2093 | _M0L6_2atmpS2094;
}

double _M0FPC14math2ln(double _M0L1xS821) {
  struct _M0TUdiE* _M0L7_2abindS822;
  double _M0L5_2af1S823;
  int32_t _M0L5_2akiS824;
  double _M0L1fS826;
  double _M0L1kS827;
  double _M0L6_2atmpS2086;
  double _M0L1sS828;
  double _M0L2s2S829;
  double _M0L2s4S830;
  double _M0L6_2atmpS2085;
  double _M0L6_2atmpS2084;
  double _M0L6_2atmpS2083;
  double _M0L6_2atmpS2082;
  double _M0L6_2atmpS2081;
  double _M0L6_2atmpS2080;
  double _M0L2t1S831;
  double _M0L6_2atmpS2079;
  double _M0L6_2atmpS2078;
  double _M0L6_2atmpS2077;
  double _M0L6_2atmpS2076;
  double _M0L2t2S832;
  double _M0L1rS833;
  double _M0L6_2atmpS2075;
  double _M0L4hfsqS834;
  double _M0L6_2atmpS2068;
  double _M0L6_2atmpS2074;
  double _M0L6_2atmpS2072;
  double _M0L6_2atmpS2073;
  double _M0L6_2atmpS2071;
  double _M0L6_2atmpS2070;
  double _M0L6_2atmpS2069;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS821 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS821)
      || _M0MPC16double6Double7is__inf(_M0L1xS821)
    ) {
      return _M0L1xS821;
    } else if (_M0L1xS821 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS822 = _M0FPC14math5frexp(_M0L1xS821);
  _M0L5_2af1S823 = _M0L7_2abindS822->$0;
  _M0L5_2akiS824 = _M0L7_2abindS822->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS822);
  if (_M0L5_2af1S823 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2090 = _M0L5_2af1S823 * 0x1p+1;
    double _M0L6_2atmpS2087 = _M0L6_2atmpS2090 - 0x1p+0;
    int32_t _M0L6_2atmpS2089 = _M0L5_2akiS824 - 1;
    double _M0L6_2atmpS2088 = (double)_M0L6_2atmpS2089;
    _M0L1fS826 = _M0L6_2atmpS2087;
    _M0L1kS827 = _M0L6_2atmpS2088;
    goto join_825;
  } else {
    double _M0L6_2atmpS2091 = _M0L5_2af1S823 - 0x1p+0;
    double _M0L6_2atmpS2092 = (double)_M0L5_2akiS824;
    _M0L1fS826 = _M0L6_2atmpS2091;
    _M0L1kS827 = _M0L6_2atmpS2092;
    goto join_825;
  }
  join_825:;
  _M0L6_2atmpS2086 = 0x1p+1 + _M0L1fS826;
  _M0L1sS828 = _M0L1fS826 / _M0L6_2atmpS2086;
  _M0L2s2S829 = _M0L1sS828 * _M0L1sS828;
  _M0L2s4S830 = _M0L2s2S829 * _M0L2s2S829;
  _M0L6_2atmpS2085 = _M0L2s4S830 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2084 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2085;
  _M0L6_2atmpS2083 = _M0L2s4S830 * _M0L6_2atmpS2084;
  _M0L6_2atmpS2082 = 0x1.2492494229359p-2 + _M0L6_2atmpS2083;
  _M0L6_2atmpS2081 = _M0L2s4S830 * _M0L6_2atmpS2082;
  _M0L6_2atmpS2080 = 0x1.5555555555593p-1 + _M0L6_2atmpS2081;
  _M0L2t1S831 = _M0L2s2S829 * _M0L6_2atmpS2080;
  _M0L6_2atmpS2079 = _M0L2s4S830 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2078 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2079;
  _M0L6_2atmpS2077 = _M0L2s4S830 * _M0L6_2atmpS2078;
  _M0L6_2atmpS2076 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2077;
  _M0L2t2S832 = _M0L2s4S830 * _M0L6_2atmpS2076;
  _M0L1rS833 = _M0L2t1S831 + _M0L2t2S832;
  _M0L6_2atmpS2075 = 0x1p-1 * _M0L1fS826;
  _M0L4hfsqS834 = _M0L6_2atmpS2075 * _M0L1fS826;
  _M0L6_2atmpS2068 = _M0L1kS827 * 0x1.62e42feep-1;
  _M0L6_2atmpS2074 = _M0L4hfsqS834 + _M0L1rS833;
  _M0L6_2atmpS2072 = _M0L1sS828 * _M0L6_2atmpS2074;
  _M0L6_2atmpS2073 = _M0L1kS827 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2071 = _M0L6_2atmpS2072 + _M0L6_2atmpS2073;
  _M0L6_2atmpS2070 = _M0L4hfsqS834 - _M0L6_2atmpS2071;
  _M0L6_2atmpS2069 = _M0L6_2atmpS2070 - _M0L1fS826;
  return _M0L6_2atmpS2068 - _M0L6_2atmpS2069;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS814) {
  struct _M0TUdiE* _M0L7_2abindS815;
  double _M0L10_2anorm__fS816;
  int32_t _M0L6_2aexpS817;
  uint64_t _M0L1uS818;
  uint64_t _M0L6_2atmpS2067;
  uint64_t _M0L6_2atmpS2066;
  int32_t _M0L6_2atmpS2065;
  int32_t _M0L6_2atmpS2064;
  int32_t _M0L3expS819;
  uint64_t _M0L6_2atmpS2063;
  uint64_t _M0L6_2atmpS2062;
  uint64_t _M0L6_2atmpS2061;
  double _M0L4fracS820;
  struct _M0TUdiE* _block_2474;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS814 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS814)
    || _M0MPC16double6Double7is__nan(_M0L1fS814)
  ) {
    struct _M0TUdiE* _block_2473 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2473)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2473->$0 = _M0L1fS814;
    _block_2473->$1 = 0;
    return _block_2473;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS815 = _M0FPC14math9normalize(_M0L1fS814);
  _M0L10_2anorm__fS816 = _M0L7_2abindS815->$0;
  _M0L6_2aexpS817 = _M0L7_2abindS815->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS815);
  _M0L1uS818 = *(int64_t*)&_M0L10_2anorm__fS816;
  _M0L6_2atmpS2067 = _M0L1uS818 >> 52;
  _M0L6_2atmpS2066 = _M0L6_2atmpS2067 & 2047ull;
  _M0L6_2atmpS2065 = (int32_t)_M0L6_2atmpS2066;
  _M0L6_2atmpS2064 = _M0L6_2aexpS817 + _M0L6_2atmpS2065;
  _M0L3expS819 = _M0L6_2atmpS2064 - 1022;
  _M0L6_2atmpS2063 = ~9218868437227405312ull;
  _M0L6_2atmpS2062 = _M0L1uS818 & _M0L6_2atmpS2063;
  _M0L6_2atmpS2061 = _M0L6_2atmpS2062 | 4602678819172646912ull;
  _M0L4fracS820 = *(double*)&_M0L6_2atmpS2061;
  _block_2474 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2474)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2474->$0 = _M0L4fracS820;
  _block_2474->$1 = _M0L3expS819;
  return _block_2474;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS813) {
  double _M0L6_2atmpS2058;
  struct _M0TUdiE* _block_2476;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2058 = fabs(_M0L1fS813);
  if (_M0L6_2atmpS2058 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2060 = (double)4503599627370496ll;
    double _M0L6_2atmpS2059 = _M0L1fS813 * _M0L6_2atmpS2060;
    struct _M0TUdiE* _block_2475 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2475)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2475->$0 = _M0L6_2atmpS2059;
    _block_2475->$1 = -52;
    return _block_2475;
  }
  _block_2476 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2476)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2476->$0 = _M0L1fS813;
  _block_2476->$1 = 0;
  return _block_2476;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS812) {
  double _M0L6_2atmpS2057;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2057 = (double)_M0L4selfS812;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2057);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS811) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS811 != _M0L4selfS811) {
    return 0;
  } else if (_M0L4selfS811 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS811 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS811;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS792,
  float _M0L4elemS794
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS791;
  int32_t _M0L1iS793;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS791 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS792);
  _M0L1iS793 = 0;
  while (1) {
    if (_M0L1iS793 < _M0L3lenS792) {
      float* _M0L3bufS2049 = _M0L3arrS791->$0;
      int32_t _M0L6_2atmpS2050;
      _M0L3bufS2049[_M0L1iS793] = _M0L4elemS794;
      _M0L6_2atmpS2050 = _M0L1iS793 + 1;
      _M0L1iS793 = _M0L6_2atmpS2050;
      continue;
    }
    break;
  }
  return _M0L3arrS791;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS797,
  int32_t _M0L4elemS799
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS796;
  int32_t _M0L1iS798;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS796 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS797);
  _M0L1iS798 = 0;
  while (1) {
    if (_M0L1iS798 < _M0L3lenS797) {
      uint8_t* _M0L3bufS2051 = _M0L3arrS796->$0;
      int32_t _M0L6_2atmpS2052;
      _M0L3bufS2051[_M0L1iS798] = _M0L4elemS799;
      _M0L6_2atmpS2052 = _M0L1iS798 + 1;
      _M0L1iS798 = _M0L6_2atmpS2052;
      continue;
    }
    break;
  }
  return _M0L3arrS796;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS802,
  struct _M0TPB5ArrayGfE* _M0L4elemS804
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS801;
  int32_t _M0L1iS803;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS801
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS802);
  _M0L1iS803 = 0;
  while (1) {
    if (_M0L1iS803 < _M0L3lenS802) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2053 = _M0L3arrS801->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2385 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2053[_M0L1iS803];
      int32_t _M0L6_2atmpS2054;
      moonbit_incref_cycle_free(_M0L4elemS804);
      if (_M0L6_2aoldS2385) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2385);
      }
      _M0L3bufS2053[_M0L1iS803] = _M0L4elemS804;
      _M0L6_2atmpS2054 = _M0L1iS803 + 1;
      _M0L1iS803 = _M0L6_2atmpS2054;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS804);
    }
    break;
  }
  return _M0L3arrS801;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS807,
  int32_t _M0L4elemS809
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS806;
  int32_t _M0L1iS808;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS806 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS807);
  _M0L1iS808 = 0;
  while (1) {
    if (_M0L1iS808 < _M0L3lenS807) {
      int32_t* _M0L3bufS2055 = _M0L3arrS806->$0;
      int32_t _M0L6_2atmpS2056;
      _M0L3bufS2055[_M0L1iS808] = _M0L4elemS809;
      _M0L6_2atmpS2056 = _M0L1iS808 + 1;
      _M0L1iS808 = _M0L6_2atmpS2056;
      continue;
    }
    break;
  }
  return _M0L3arrS806;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS776,
  int32_t _M0L5indexS777,
  float _M0L5valueS778
) {
  int32_t _M0L3lenS775;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS775 = _M0L4selfS776->$1;
  if (_M0L5indexS777 >= 0 && _M0L5indexS777 < _M0L3lenS775) {
    float* _M0L6_2atmpS2045;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2045 = _M0MPC15array5Array6bufferGfE(_M0L4selfS776);
    _M0L6_2atmpS2045[_M0L5indexS777] = _M0L5valueS778;
    moonbit_decref_cycle_free(_M0L6_2atmpS2045);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS780,
  int32_t _M0L5indexS781,
  int32_t _M0L5valueS782
) {
  int32_t _M0L3lenS779;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS779 = _M0L4selfS780->$1;
  if (_M0L5indexS781 >= 0 && _M0L5indexS781 < _M0L3lenS779) {
    uint8_t* _M0L6_2atmpS2046;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2046 = _M0MPC15array5Array6bufferGbE(_M0L4selfS780);
    _M0L6_2atmpS2046[_M0L5indexS781] = _M0L5valueS782;
    moonbit_decref_cycle_free(_M0L6_2atmpS2046);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS784,
  int32_t _M0L5indexS785,
  struct _M0TPB5ArrayGfE* _M0L5valueS786
) {
  int32_t _M0L3lenS783;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS783 = _M0L4selfS784->$1;
  if (_M0L5indexS785 >= 0 && _M0L5indexS785 < _M0L3lenS783) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2047;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2386;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2047
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS784);
    _M0L6_2aoldS2386
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2047[_M0L5indexS785];
    if (_M0L6_2aoldS2386) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2386);
    }
    _M0L6_2atmpS2047[_M0L5indexS785] = _M0L5valueS786;
    moonbit_decref_cycle_free(_M0L6_2atmpS2047);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS786);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS788,
  int32_t _M0L5indexS789,
  int32_t _M0L5valueS790
) {
  int32_t _M0L3lenS787;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS787 = _M0L4selfS788->$1;
  if (_M0L5indexS789 >= 0 && _M0L5indexS789 < _M0L3lenS787) {
    int32_t* _M0L6_2atmpS2048;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2048 = _M0MPC15array5Array6bufferGiE(_M0L4selfS788);
    _M0L6_2atmpS2048[_M0L5indexS789] = _M0L5valueS790;
    moonbit_decref_cycle_free(_M0L6_2atmpS2048);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS761,
  int32_t _M0L5indexS762
) {
  int32_t _M0L3lenS760;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS760 = _M0L4selfS761->$1;
  if (_M0L5indexS762 >= 0 && _M0L5indexS762 < _M0L3lenS760) {
    float* _M0L6_2atmpS2040;
    float _result_2481;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2040 = _M0MPC15array5Array6bufferGfE(_M0L4selfS761);
    _result_2481 = (float)_M0L6_2atmpS2040[_M0L5indexS762];
    moonbit_decref_cycle_free(_M0L6_2atmpS2040);
    return _result_2481;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS764,
  int32_t _M0L5indexS765
) {
  int32_t _M0L3lenS763;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS763 = _M0L4selfS764->$1;
  if (_M0L5indexS765 >= 0 && _M0L5indexS765 < _M0L3lenS763) {
    moonbit_string_t* _M0L6_2atmpS2041;
    moonbit_string_t _M0L6_2atmpS2387;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2041 = _M0MPC15array5Array6bufferGsE(_M0L4selfS764);
    _M0L6_2atmpS2387 = (moonbit_string_t)_M0L6_2atmpS2041[_M0L5indexS765];
    moonbit_incref_cycle_free(_M0L6_2atmpS2387);
    moonbit_decref_cycle_free(_M0L6_2atmpS2041);
    return _M0L6_2atmpS2387;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS767,
  int32_t _M0L5indexS768
) {
  int32_t _M0L3lenS766;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS766 = _M0L4selfS767->$1;
  if (_M0L5indexS768 >= 0 && _M0L5indexS768 < _M0L3lenS766) {
    uint8_t* _M0L6_2atmpS2042;
    int32_t _result_2482;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2042 = _M0MPC15array5Array6bufferGbE(_M0L4selfS767);
    _result_2482 = (int32_t)_M0L6_2atmpS2042[_M0L5indexS768];
    moonbit_decref_cycle_free(_M0L6_2atmpS2042);
    return _result_2482;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS770,
  int32_t _M0L5indexS771
) {
  int32_t _M0L3lenS769;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS769 = _M0L4selfS770->$1;
  if (_M0L5indexS771 >= 0 && _M0L5indexS771 < _M0L3lenS769) {
    int32_t* _M0L6_2atmpS2043;
    int32_t _result_2483;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2043 = _M0MPC15array5Array6bufferGiE(_M0L4selfS770);
    _result_2483 = (int32_t)_M0L6_2atmpS2043[_M0L5indexS771];
    moonbit_decref_cycle_free(_M0L6_2atmpS2043);
    return _result_2483;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS773,
  int32_t _M0L5indexS774
) {
  int32_t _M0L3lenS772;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS772 = _M0L4selfS773->$1;
  if (_M0L5indexS774 >= 0 && _M0L5indexS774 < _M0L3lenS772) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2044;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2388;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2044
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS773);
    _M0L6_2atmpS2388
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2044[_M0L5indexS774];
    if (_M0L6_2atmpS2388) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2388);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2044);
    return _M0L6_2atmpS2388;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS759) {
  moonbit_string_t _M0L6_2atmpS2039;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2039 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS759);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2039);
  moonbit_decref_cycle_free(_M0L6_2atmpS2039);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS758) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS758);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS757) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS757 > _M0FPB18double__max__value
         || _M0L4selfS757 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS756) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS756 != _M0L4selfS756;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS741) {
  uint64_t _M0L4bitsS744;
  uint64_t _M0L6_2atmpS2038;
  uint64_t _M0L6_2atmpS2037;
  int32_t _M0L8ieeeSignS745;
  uint64_t _M0L12ieeeMantissaS746;
  uint64_t _M0L6_2atmpS2036;
  uint64_t _M0L6_2atmpS2035;
  int32_t _M0L12ieeeExponentS747;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS748;
  struct _M0TPB17FloatingDecimal64* _M0L1vS749;
  moonbit_string_t _result_2485;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS741 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_10.data;
  }
  if (_M0L3valS741 >= -0x1p+53 && _M0L3valS741 <= 0x1p+53) {
    if (_M0L3valS741 >= -0x1p+31 && _M0L3valS741 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS742;
      double _M0L6_2atmpS2024;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS742 = _M0MPC16double6Double7to__int(_M0L3valS741);
      _M0L6_2atmpS2024 = (double)_M0L1iS742;
      if (_M0L6_2atmpS2024 == _M0L3valS741) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS742, 10);
      }
    } else {
      int64_t _M0L1iS743;
      double _M0L6_2atmpS2025;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS743 = _M0MPC16double6Double9to__int64(_M0L3valS741);
      _M0L6_2atmpS2025 = (double)_M0L1iS743;
      if (_M0L6_2atmpS2025 == _M0L3valS741) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS743, 10);
      }
    }
  }
  _M0L4bitsS744 = *(int64_t*)&_M0L3valS741;
  _M0L6_2atmpS2038 = _M0L4bitsS744 >> 63;
  _M0L6_2atmpS2037 = _M0L6_2atmpS2038 & 1ull;
  _M0L8ieeeSignS745 = _M0L6_2atmpS2037 != 0ull;
  _M0L12ieeeMantissaS746 = _M0L4bitsS744 & 4503599627370495ull;
  _M0L6_2atmpS2036 = _M0L4bitsS744 >> 52;
  _M0L6_2atmpS2035 = _M0L6_2atmpS2036 & 2047ull;
  _M0L12ieeeExponentS747 = (int32_t)_M0L6_2atmpS2035;
  if (
    _M0L12ieeeExponentS747 == 2047
    || _M0L12ieeeExponentS747 == 0 && _M0L12ieeeMantissaS746 == 0ull
  ) {
    int32_t _M0L6_2atmpS2026 = _M0L12ieeeExponentS747 != 0;
    int32_t _M0L6_2atmpS2027 = _M0L12ieeeMantissaS746 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS745, _M0L6_2atmpS2026, _M0L6_2atmpS2027);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS748
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS746, _M0L12ieeeExponentS747);
  if (_M0L7_2abindS748 == 0) {
    uint32_t _M0L6_2atmpS2028;
    if (_M0L7_2abindS748) {
      moonbit_decref_cycle_free(_M0L7_2abindS748);
    }
    _M0L6_2atmpS2028 = *(uint32_t*)&_M0L12ieeeExponentS747;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS749 = _M0FPB3d2d(_M0L12ieeeMantissaS746, _M0L6_2atmpS2028);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS750 = _M0L7_2abindS748;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS751 = _M0L7_2aSomeS750;
    struct _M0TPB17FloatingDecimal64* _M0L1xS752 = _M0L4_2afS751;
    while (1) {
      uint64_t _M0L8mantissaS2034 = _M0L1xS752->$0;
      uint64_t _M0L1qS753 = _M0L8mantissaS2034 / 10ull;
      uint64_t _M0L8mantissaS2032 = _M0L1xS752->$0;
      uint64_t _M0L6_2atmpS2033 = 10ull * _M0L1qS753;
      uint64_t _M0L1rS754 = _M0L8mantissaS2032 - _M0L6_2atmpS2033;
      int32_t _M0L8exponentS2031;
      int32_t _M0L6_2atmpS2030;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2029;
      if (_M0L1rS754 != 0ull) {
        _M0L1vS749 = _M0L1xS752;
        break;
      }
      _M0L8exponentS2031 = _M0L1xS752->$1;
      moonbit_decref_cycle_free(_M0L1xS752);
      _M0L6_2atmpS2030 = _M0L8exponentS2031 + 1;
      _M0L6_2atmpS2029
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2029)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2029->$0 = _M0L1qS753;
      _M0L6_2atmpS2029->$1 = _M0L6_2atmpS2030;
      _M0L1xS752 = _M0L6_2atmpS2029;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2485 = _M0FPB9to__chars(_M0L1vS749, _M0L8ieeeSignS745);
  moonbit_decref_cycle_free(_M0L1vS749);
  return _result_2485;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS736,
  int32_t _M0L12ieeeExponentS738
) {
  uint64_t _M0L2m2S735;
  int32_t _M0L6_2atmpS2023;
  int32_t _M0L2e2S737;
  int32_t _M0L6_2atmpS2022;
  uint64_t _M0L6_2atmpS2021;
  uint64_t _M0L4maskS739;
  uint64_t _M0L8fractionS740;
  int32_t _M0L6_2atmpS2020;
  uint64_t _M0L6_2atmpS2019;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2018;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S735 = 4503599627370496ull | _M0L12ieeeMantissaS736;
  _M0L6_2atmpS2023 = _M0L12ieeeExponentS738 - 1023;
  _M0L2e2S737 = _M0L6_2atmpS2023 - 52;
  if (_M0L2e2S737 > 0) {
    return 0;
  }
  if (_M0L2e2S737 < -52) {
    return 0;
  }
  _M0L6_2atmpS2022 = -_M0L2e2S737;
  _M0L6_2atmpS2021 = 1ull << (_M0L6_2atmpS2022 & 63);
  _M0L4maskS739 = _M0L6_2atmpS2021 - 1ull;
  _M0L8fractionS740 = _M0L2m2S735 & _M0L4maskS739;
  if (_M0L8fractionS740 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2020 = -_M0L2e2S737;
  _M0L6_2atmpS2019 = _M0L2m2S735 >> (_M0L6_2atmpS2020 & 63);
  _M0L6_2atmpS2018
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2018)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2018->$0 = _M0L6_2atmpS2019;
  _M0L6_2atmpS2018->$1 = 0;
  return _M0L6_2atmpS2018;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS703,
  int32_t _M0L4signS701
) {
  moonbit_bytes_t _M0L6resultS699;
  int32_t _M0Lm5indexS700;
  uint64_t _M0L6outputS702;
  int32_t _M0L7olengthS704;
  int32_t _M0L8exponentS2017;
  int32_t _M0L6_2atmpS2016;
  int32_t _M0Lm3expS705;
  int32_t _M0L6_2atmpS2015;
  int32_t _M0L6_2atmpS2013;
  int32_t _M0L18scientificNotationS706;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS699 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS700 = 0;
  if (_M0L4signS701) {
    int32_t _M0L6_2atmpS1887 = _M0Lm5indexS700;
    int32_t _M0L6_2atmpS1888;
    if (
      _M0L6_2atmpS1887 < 0
      || _M0L6_2atmpS1887 >= Moonbit_array_length(_M0L6resultS699)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS699[_M0L6_2atmpS1887] = 45;
    _M0L6_2atmpS1888 = _M0Lm5indexS700;
    _M0Lm5indexS700 = _M0L6_2atmpS1888 + 1;
  }
  _M0L6outputS702 = _M0L1vS703->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS704 = _M0FPB17decimal__length17(_M0L6outputS702);
  _M0L8exponentS2017 = _M0L1vS703->$1;
  _M0L6_2atmpS2016 = _M0L8exponentS2017 + _M0L7olengthS704;
  _M0Lm3expS705 = _M0L6_2atmpS2016 - 1;
  _M0L6_2atmpS2015 = _M0Lm3expS705;
  if (_M0L6_2atmpS2015 >= -6) {
    int32_t _M0L6_2atmpS2014 = _M0Lm3expS705;
    _M0L6_2atmpS2013 = _M0L6_2atmpS2014 < 21;
  } else {
    _M0L6_2atmpS2013 = 0;
  }
  _M0L18scientificNotationS706 = !_M0L6_2atmpS2013;
  if (_M0L18scientificNotationS706) {
    int32_t _M0L7_2abindS707 = _M0L7olengthS704 - 1;
    uint64_t _M0L6outputS708;
    int32_t _M0L1iS709 = 0;
    uint64_t _M0L6outputS710 = _M0L6outputS702;
    int32_t _M0L6_2atmpS1889;
    int32_t _M0L6_2atmpS1893;
    int32_t _M0L6_2atmpS1892;
    int32_t _M0L6_2atmpS1891;
    int32_t _M0L6_2atmpS1890;
    int32_t _M0L6_2atmpS1897;
    int32_t _M0L6_2atmpS1898;
    int32_t _M0L6_2atmpS1899;
    int32_t _M0L6_2atmpS1900;
    int32_t _M0L6_2atmpS1901;
    int32_t _M0L6_2atmpS1907;
    int32_t _M0L6_2atmpS1940;
    moonbit_string_t _result_2487;
    while (1) {
      if (_M0L1iS709 < _M0L7_2abindS707) {
        uint64_t _M0L1cS711 = _M0L6outputS710 % 10ull;
        int32_t _M0L6_2atmpS1946 = _M0Lm5indexS700;
        int32_t _M0L6_2atmpS1945 = _M0L6_2atmpS1946 + _M0L7olengthS704;
        int32_t _M0L6_2atmpS1941 = _M0L6_2atmpS1945 - _M0L1iS709;
        int32_t _M0L6_2atmpS1944 = (int32_t)_M0L1cS711;
        int32_t _M0L6_2atmpS1943 = 48 + _M0L6_2atmpS1944;
        int32_t _M0L6_2atmpS1942 = _M0L6_2atmpS1943 & 0xff;
        int32_t _M0L6_2atmpS1947;
        uint64_t _M0L6_2atmpS1948;
        if (
          _M0L6_2atmpS1941 < 0
          || _M0L6_2atmpS1941 >= Moonbit_array_length(_M0L6resultS699)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS699[_M0L6_2atmpS1941] = _M0L6_2atmpS1942;
        _M0L6_2atmpS1947 = _M0L1iS709 + 1;
        _M0L6_2atmpS1948 = _M0L6outputS710 / 10ull;
        _M0L1iS709 = _M0L6_2atmpS1947;
        _M0L6outputS710 = _M0L6_2atmpS1948;
        continue;
      } else {
        _M0L6outputS708 = _M0L6outputS710;
      }
      break;
    }
    _M0L6_2atmpS1889 = _M0Lm5indexS700;
    _M0L6_2atmpS1893 = (int32_t)_M0L6outputS708;
    _M0L6_2atmpS1892 = _M0L6_2atmpS1893 % 10;
    _M0L6_2atmpS1891 = 48 + _M0L6_2atmpS1892;
    _M0L6_2atmpS1890 = _M0L6_2atmpS1891 & 0xff;
    if (
      _M0L6_2atmpS1889 < 0
      || _M0L6_2atmpS1889 >= Moonbit_array_length(_M0L6resultS699)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS699[_M0L6_2atmpS1889] = _M0L6_2atmpS1890;
    if (_M0L7olengthS704 > 1) {
      int32_t _M0L6_2atmpS1895 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1894 = _M0L6_2atmpS1895 + 1;
      if (
        _M0L6_2atmpS1894 < 0
        || _M0L6_2atmpS1894 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1894] = 46;
    } else {
      int32_t _M0L6_2atmpS1896 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1896 - 1;
    }
    _M0L6_2atmpS1897 = _M0Lm5indexS700;
    _M0L6_2atmpS1898 = _M0L7olengthS704 + 1;
    _M0Lm5indexS700 = _M0L6_2atmpS1897 + _M0L6_2atmpS1898;
    _M0L6_2atmpS1899 = _M0Lm5indexS700;
    if (
      _M0L6_2atmpS1899 < 0
      || _M0L6_2atmpS1899 >= Moonbit_array_length(_M0L6resultS699)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS699[_M0L6_2atmpS1899] = 101;
    _M0L6_2atmpS1900 = _M0Lm5indexS700;
    _M0Lm5indexS700 = _M0L6_2atmpS1900 + 1;
    _M0L6_2atmpS1901 = _M0Lm3expS705;
    if (_M0L6_2atmpS1901 < 0) {
      int32_t _M0L6_2atmpS1902 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1903;
      int32_t _M0L6_2atmpS1904;
      if (
        _M0L6_2atmpS1902 < 0
        || _M0L6_2atmpS1902 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1902] = 45;
      _M0L6_2atmpS1903 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1903 + 1;
      _M0L6_2atmpS1904 = _M0Lm3expS705;
      _M0Lm3expS705 = -_M0L6_2atmpS1904;
    } else {
      int32_t _M0L6_2atmpS1905 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1906;
      if (
        _M0L6_2atmpS1905 < 0
        || _M0L6_2atmpS1905 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1905] = 43;
      _M0L6_2atmpS1906 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1906 + 1;
    }
    _M0L6_2atmpS1907 = _M0Lm3expS705;
    if (_M0L6_2atmpS1907 >= 100) {
      int32_t _M0L6_2atmpS1923 = _M0Lm3expS705;
      int32_t _M0L1aS713 = _M0L6_2atmpS1923 / 100;
      int32_t _M0L6_2atmpS1922 = _M0Lm3expS705;
      int32_t _M0L6_2atmpS1921 = _M0L6_2atmpS1922 / 10;
      int32_t _M0L1bS714 = _M0L6_2atmpS1921 % 10;
      int32_t _M0L6_2atmpS1920 = _M0Lm3expS705;
      int32_t _M0L1cS715 = _M0L6_2atmpS1920 % 10;
      int32_t _M0L6_2atmpS1908 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1910 = 48 + _M0L1aS713;
      int32_t _M0L6_2atmpS1909 = _M0L6_2atmpS1910 & 0xff;
      int32_t _M0L6_2atmpS1914;
      int32_t _M0L6_2atmpS1911;
      int32_t _M0L6_2atmpS1913;
      int32_t _M0L6_2atmpS1912;
      int32_t _M0L6_2atmpS1918;
      int32_t _M0L6_2atmpS1915;
      int32_t _M0L6_2atmpS1917;
      int32_t _M0L6_2atmpS1916;
      int32_t _M0L6_2atmpS1919;
      if (
        _M0L6_2atmpS1908 < 0
        || _M0L6_2atmpS1908 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1908] = _M0L6_2atmpS1909;
      _M0L6_2atmpS1914 = _M0Lm5indexS700;
      _M0L6_2atmpS1911 = _M0L6_2atmpS1914 + 1;
      _M0L6_2atmpS1913 = 48 + _M0L1bS714;
      _M0L6_2atmpS1912 = _M0L6_2atmpS1913 & 0xff;
      if (
        _M0L6_2atmpS1911 < 0
        || _M0L6_2atmpS1911 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1911] = _M0L6_2atmpS1912;
      _M0L6_2atmpS1918 = _M0Lm5indexS700;
      _M0L6_2atmpS1915 = _M0L6_2atmpS1918 + 2;
      _M0L6_2atmpS1917 = 48 + _M0L1cS715;
      _M0L6_2atmpS1916 = _M0L6_2atmpS1917 & 0xff;
      if (
        _M0L6_2atmpS1915 < 0
        || _M0L6_2atmpS1915 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1915] = _M0L6_2atmpS1916;
      _M0L6_2atmpS1919 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1919 + 3;
    } else {
      int32_t _M0L6_2atmpS1924 = _M0Lm3expS705;
      if (_M0L6_2atmpS1924 >= 10) {
        int32_t _M0L6_2atmpS1934 = _M0Lm3expS705;
        int32_t _M0L1aS716 = _M0L6_2atmpS1934 / 10;
        int32_t _M0L6_2atmpS1933 = _M0Lm3expS705;
        int32_t _M0L1bS717 = _M0L6_2atmpS1933 % 10;
        int32_t _M0L6_2atmpS1925 = _M0Lm5indexS700;
        int32_t _M0L6_2atmpS1927 = 48 + _M0L1aS716;
        int32_t _M0L6_2atmpS1926 = _M0L6_2atmpS1927 & 0xff;
        int32_t _M0L6_2atmpS1931;
        int32_t _M0L6_2atmpS1928;
        int32_t _M0L6_2atmpS1930;
        int32_t _M0L6_2atmpS1929;
        int32_t _M0L6_2atmpS1932;
        if (
          _M0L6_2atmpS1925 < 0
          || _M0L6_2atmpS1925 >= Moonbit_array_length(_M0L6resultS699)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS699[_M0L6_2atmpS1925] = _M0L6_2atmpS1926;
        _M0L6_2atmpS1931 = _M0Lm5indexS700;
        _M0L6_2atmpS1928 = _M0L6_2atmpS1931 + 1;
        _M0L6_2atmpS1930 = 48 + _M0L1bS717;
        _M0L6_2atmpS1929 = _M0L6_2atmpS1930 & 0xff;
        if (
          _M0L6_2atmpS1928 < 0
          || _M0L6_2atmpS1928 >= Moonbit_array_length(_M0L6resultS699)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS699[_M0L6_2atmpS1928] = _M0L6_2atmpS1929;
        _M0L6_2atmpS1932 = _M0Lm5indexS700;
        _M0Lm5indexS700 = _M0L6_2atmpS1932 + 2;
      } else {
        int32_t _M0L6_2atmpS1935 = _M0Lm5indexS700;
        int32_t _M0L6_2atmpS1938 = _M0Lm3expS705;
        int32_t _M0L6_2atmpS1937 = 48 + _M0L6_2atmpS1938;
        int32_t _M0L6_2atmpS1936 = _M0L6_2atmpS1937 & 0xff;
        int32_t _M0L6_2atmpS1939;
        if (
          _M0L6_2atmpS1935 < 0
          || _M0L6_2atmpS1935 >= Moonbit_array_length(_M0L6resultS699)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS699[_M0L6_2atmpS1935] = _M0L6_2atmpS1936;
        _M0L6_2atmpS1939 = _M0Lm5indexS700;
        _M0Lm5indexS700 = _M0L6_2atmpS1939 + 1;
      }
    }
    _M0L6_2atmpS1940 = _M0Lm5indexS700;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2487
    = _M0FPB19string__from__bytes(_M0L6resultS699, 0, _M0L6_2atmpS1940);
    moonbit_decref_cycle_free(_M0L6resultS699);
    return _result_2487;
  } else {
    int32_t _M0L6_2atmpS1949 = _M0Lm3expS705;
    int32_t _M0L6_2atmpS2012;
    moonbit_string_t _result_2493;
    if (_M0L6_2atmpS1949 < 0) {
      int32_t _M0L6_2atmpS1950 = _M0Lm5indexS700;
      int32_t _M0L6_2atmpS1952;
      int32_t _M0L6_2atmpS1951;
      int32_t _M0L6_2atmpS1953;
      int32_t _M0L1iS718;
      int32_t _M0L6_2atmpS1968;
      int32_t _M0L6_2atmpS1970;
      int32_t _M0L6_2atmpS1969;
      int32_t _M0L7currentS720;
      int32_t _M0L1iS721;
      uint64_t _M0L6outputS722;
      if (
        _M0L6_2atmpS1950 < 0
        || _M0L6_2atmpS1950 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1950] = 48;
      _M0L6_2atmpS1952 = _M0Lm5indexS700;
      _M0L6_2atmpS1951 = _M0L6_2atmpS1952 + 1;
      if (
        _M0L6_2atmpS1951 < 0
        || _M0L6_2atmpS1951 >= Moonbit_array_length(_M0L6resultS699)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS699[_M0L6_2atmpS1951] = 46;
      _M0L6_2atmpS1953 = _M0Lm5indexS700;
      _M0Lm5indexS700 = _M0L6_2atmpS1953 + 2;
      _M0L1iS718 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1954 = _M0Lm3expS705;
        if (_M0L1iS718 > _M0L6_2atmpS1954) {
          int32_t _M0L6_2atmpS1957 = _M0Lm5indexS700;
          int32_t _M0L6_2atmpS1956 = _M0L6_2atmpS1957 - _M0L1iS718;
          int32_t _M0L6_2atmpS1955 = _M0L6_2atmpS1956 - 1;
          int32_t _M0L6_2atmpS1958;
          if (
            _M0L6_2atmpS1955 < 0
            || _M0L6_2atmpS1955 >= Moonbit_array_length(_M0L6resultS699)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS699[_M0L6_2atmpS1955] = 48;
          _M0L6_2atmpS1958 = _M0L1iS718 - 1;
          _M0L1iS718 = _M0L6_2atmpS1958;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1968 = _M0Lm5indexS700;
      _M0L6_2atmpS1970 = _M0Lm3expS705;
      _M0L6_2atmpS1969 = -1 - _M0L6_2atmpS1970;
      _M0L7currentS720 = _M0L6_2atmpS1968 + _M0L6_2atmpS1969;
      _M0L1iS721 = 0;
      _M0L6outputS722 = _M0L6outputS702;
      while (1) {
        if (_M0L1iS721 < _M0L7olengthS704) {
          int32_t _M0L6_2atmpS1965 = _M0L7currentS720 + _M0L7olengthS704;
          int32_t _M0L6_2atmpS1964 = _M0L6_2atmpS1965 - _M0L1iS721;
          int32_t _M0L6_2atmpS1959 = _M0L6_2atmpS1964 - 1;
          uint64_t _M0L6_2atmpS1963 = _M0L6outputS722 % 10ull;
          int32_t _M0L6_2atmpS1962 = (int32_t)_M0L6_2atmpS1963;
          int32_t _M0L6_2atmpS1961 = 48 + _M0L6_2atmpS1962;
          int32_t _M0L6_2atmpS1960 = _M0L6_2atmpS1961 & 0xff;
          int32_t _M0L6_2atmpS1966;
          uint64_t _M0L6_2atmpS1967;
          if (
            _M0L6_2atmpS1959 < 0
            || _M0L6_2atmpS1959 >= Moonbit_array_length(_M0L6resultS699)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS699[_M0L6_2atmpS1959] = _M0L6_2atmpS1960;
          _M0L6_2atmpS1966 = _M0L1iS721 + 1;
          _M0L6_2atmpS1967 = _M0L6outputS722 / 10ull;
          _M0L1iS721 = _M0L6_2atmpS1966;
          _M0L6outputS722 = _M0L6_2atmpS1967;
          continue;
        }
        break;
      }
      _M0Lm5indexS700 = _M0L7currentS720 + _M0L7olengthS704;
    } else {
      int32_t _M0L6_2atmpS1972 = _M0Lm3expS705;
      int32_t _M0L6_2atmpS1971 = _M0L6_2atmpS1972 + 1;
      if (_M0L6_2atmpS1971 >= _M0L7olengthS704) {
        int32_t _M0L1iS724 = 0;
        uint64_t _M0L6outputS725 = _M0L6outputS702;
        int32_t _M0L6_2atmpS1983;
        int32_t _M0L6_2atmpS1988;
        int32_t _M0L7_2abindS727;
        int32_t _M0L1iS728;
        int32_t _M0L6_2atmpS1989;
        int32_t _M0L6_2atmpS1992;
        int32_t _M0L6_2atmpS1991;
        int32_t _M0L6_2atmpS1990;
        while (1) {
          if (_M0L1iS724 < _M0L7olengthS704) {
            int32_t _M0L6_2atmpS1980 = _M0Lm5indexS700;
            int32_t _M0L6_2atmpS1979 = _M0L6_2atmpS1980 + _M0L7olengthS704;
            int32_t _M0L6_2atmpS1978 = _M0L6_2atmpS1979 - _M0L1iS724;
            int32_t _M0L6_2atmpS1973 = _M0L6_2atmpS1978 - 1;
            uint64_t _M0L6_2atmpS1977 = _M0L6outputS725 % 10ull;
            int32_t _M0L6_2atmpS1976 = (int32_t)_M0L6_2atmpS1977;
            int32_t _M0L6_2atmpS1975 = 48 + _M0L6_2atmpS1976;
            int32_t _M0L6_2atmpS1974 = _M0L6_2atmpS1975 & 0xff;
            int32_t _M0L6_2atmpS1981;
            uint64_t _M0L6_2atmpS1982;
            if (
              _M0L6_2atmpS1973 < 0
              || _M0L6_2atmpS1973 >= Moonbit_array_length(_M0L6resultS699)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS699[_M0L6_2atmpS1973] = _M0L6_2atmpS1974;
            _M0L6_2atmpS1981 = _M0L1iS724 + 1;
            _M0L6_2atmpS1982 = _M0L6outputS725 / 10ull;
            _M0L1iS724 = _M0L6_2atmpS1981;
            _M0L6outputS725 = _M0L6_2atmpS1982;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1983 = _M0Lm5indexS700;
        _M0Lm5indexS700 = _M0L6_2atmpS1983 + _M0L7olengthS704;
        _M0L6_2atmpS1988 = _M0Lm3expS705;
        _M0L7_2abindS727 = _M0L6_2atmpS1988 + 1;
        _M0L1iS728 = _M0L7olengthS704;
        while (1) {
          if (_M0L1iS728 < _M0L7_2abindS727) {
            int32_t _M0L6_2atmpS1986 = _M0Lm5indexS700;
            int32_t _M0L6_2atmpS1985 = _M0L6_2atmpS1986 + _M0L1iS728;
            int32_t _M0L6_2atmpS1984 = _M0L6_2atmpS1985 - _M0L7olengthS704;
            int32_t _M0L6_2atmpS1987;
            if (
              _M0L6_2atmpS1984 < 0
              || _M0L6_2atmpS1984 >= Moonbit_array_length(_M0L6resultS699)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS699[_M0L6_2atmpS1984] = 48;
            _M0L6_2atmpS1987 = _M0L1iS728 + 1;
            _M0L1iS728 = _M0L6_2atmpS1987;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1989 = _M0Lm5indexS700;
        _M0L6_2atmpS1992 = _M0Lm3expS705;
        _M0L6_2atmpS1991 = _M0L6_2atmpS1992 + 1;
        _M0L6_2atmpS1990 = _M0L6_2atmpS1991 - _M0L7olengthS704;
        _M0Lm5indexS700 = _M0L6_2atmpS1989 + _M0L6_2atmpS1990;
      } else {
        int32_t _M0L6_2atmpS2009 = _M0Lm5indexS700;
        int32_t _M0L6_2atmpS2008 = _M0L6_2atmpS2009 + 1;
        int32_t _M0L1iS730 = 0;
        int32_t _M0L7currentS731 = _M0L6_2atmpS2008;
        uint64_t _M0L6outputS732 = _M0L6outputS702;
        int32_t _M0L6_2atmpS2010;
        int32_t _M0L6_2atmpS2011;
        while (1) {
          if (_M0L1iS730 < _M0L7olengthS704) {
            int32_t _M0L6_2atmpS2004 = _M0L7olengthS704 - _M0L1iS730;
            int32_t _M0L6_2atmpS2002 = _M0L6_2atmpS2004 - 1;
            int32_t _M0L6_2atmpS2003 = _M0Lm3expS705;
            int32_t _M0L7currentS733;
            int32_t _M0L6_2atmpS1999;
            int32_t _M0L6_2atmpS1998;
            int32_t _M0L6_2atmpS1993;
            uint64_t _M0L6_2atmpS1997;
            int32_t _M0L6_2atmpS1996;
            int32_t _M0L6_2atmpS1995;
            int32_t _M0L6_2atmpS1994;
            int32_t _M0L6_2atmpS2000;
            uint64_t _M0L6_2atmpS2001;
            if (_M0L6_2atmpS2002 == _M0L6_2atmpS2003) {
              int32_t _M0L6_2atmpS2007 = _M0L7currentS731 + _M0L7olengthS704;
              int32_t _M0L6_2atmpS2006 = _M0L6_2atmpS2007 - _M0L1iS730;
              int32_t _M0L6_2atmpS2005 = _M0L6_2atmpS2006 - 1;
              if (
                _M0L6_2atmpS2005 < 0
                || _M0L6_2atmpS2005 >= Moonbit_array_length(_M0L6resultS699)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS699[_M0L6_2atmpS2005] = 46;
              _M0L7currentS733 = _M0L7currentS731 - 1;
            } else {
              _M0L7currentS733 = _M0L7currentS731;
            }
            _M0L6_2atmpS1999 = _M0L7currentS733 + _M0L7olengthS704;
            _M0L6_2atmpS1998 = _M0L6_2atmpS1999 - _M0L1iS730;
            _M0L6_2atmpS1993 = _M0L6_2atmpS1998 - 1;
            _M0L6_2atmpS1997 = _M0L6outputS732 % 10ull;
            _M0L6_2atmpS1996 = (int32_t)_M0L6_2atmpS1997;
            _M0L6_2atmpS1995 = 48 + _M0L6_2atmpS1996;
            _M0L6_2atmpS1994 = _M0L6_2atmpS1995 & 0xff;
            if (
              _M0L6_2atmpS1993 < 0
              || _M0L6_2atmpS1993 >= Moonbit_array_length(_M0L6resultS699)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS699[_M0L6_2atmpS1993] = _M0L6_2atmpS1994;
            _M0L6_2atmpS2000 = _M0L1iS730 + 1;
            _M0L6_2atmpS2001 = _M0L6outputS732 / 10ull;
            _M0L1iS730 = _M0L6_2atmpS2000;
            _M0L7currentS731 = _M0L7currentS733;
            _M0L6outputS732 = _M0L6_2atmpS2001;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2010 = _M0Lm5indexS700;
        _M0L6_2atmpS2011 = _M0L7olengthS704 + 1;
        _M0Lm5indexS700 = _M0L6_2atmpS2010 + _M0L6_2atmpS2011;
      }
    }
    _M0L6_2atmpS2012 = _M0Lm5indexS700;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2493
    = _M0FPB19string__from__bytes(_M0L6resultS699, 0, _M0L6_2atmpS2012);
    moonbit_decref_cycle_free(_M0L6resultS699);
    return _result_2493;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS645,
  uint32_t _M0L12ieeeExponentS644
) {
  int32_t _M0Lm2e2S642;
  uint64_t _M0Lm2m2S643;
  uint64_t _M0L6_2atmpS1886;
  uint64_t _M0L6_2atmpS1885;
  int32_t _M0L4evenS646;
  uint64_t _M0L6_2atmpS1884;
  uint64_t _M0L2mvS647;
  int32_t _M0L7mmShiftS648;
  uint64_t _M0Lm2vrS649;
  uint64_t _M0Lm2vpS650;
  uint64_t _M0Lm2vmS651;
  int32_t _M0Lm3e10S652;
  int32_t _M0Lm17vmIsTrailingZerosS653;
  int32_t _M0Lm17vrIsTrailingZerosS654;
  int32_t _M0L6_2atmpS1786;
  int32_t _M0Lm7removedS673;
  int32_t _M0Lm16lastRemovedDigitS674;
  uint64_t _M0Lm6outputS675;
  int32_t _M0L6_2atmpS1882;
  int32_t _M0L6_2atmpS1883;
  int32_t _M0L3expS698;
  uint64_t _M0L6_2atmpS1881;
  struct _M0TPB17FloatingDecimal64* _block_2499;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S642 = 0;
  _M0Lm2m2S643 = 0ull;
  if (_M0L12ieeeExponentS644 == 0u) {
    _M0Lm2e2S642 = -1076;
    _M0Lm2m2S643 = _M0L12ieeeMantissaS645;
  } else {
    int32_t _M0L6_2atmpS1785 = *(int32_t*)&_M0L12ieeeExponentS644;
    int32_t _M0L6_2atmpS1784 = _M0L6_2atmpS1785 - 1023;
    int32_t _M0L6_2atmpS1783 = _M0L6_2atmpS1784 - 52;
    _M0Lm2e2S642 = _M0L6_2atmpS1783 - 2;
    _M0Lm2m2S643 = 4503599627370496ull | _M0L12ieeeMantissaS645;
  }
  _M0L6_2atmpS1886 = _M0Lm2m2S643;
  _M0L6_2atmpS1885 = _M0L6_2atmpS1886 & 1ull;
  _M0L4evenS646 = _M0L6_2atmpS1885 == 0ull;
  _M0L6_2atmpS1884 = _M0Lm2m2S643;
  _M0L2mvS647 = 4ull * _M0L6_2atmpS1884;
  _M0L7mmShiftS648
  = _M0L12ieeeMantissaS645 != 0ull || _M0L12ieeeExponentS644 <= 1u;
  _M0Lm2vrS649 = 0ull;
  _M0Lm2vpS650 = 0ull;
  _M0Lm2vmS651 = 0ull;
  _M0Lm3e10S652 = 0;
  _M0Lm17vmIsTrailingZerosS653 = 0;
  _M0Lm17vrIsTrailingZerosS654 = 0;
  _M0L6_2atmpS1786 = _M0Lm2e2S642;
  if (_M0L6_2atmpS1786 >= 0) {
    int32_t _M0L6_2atmpS1808 = _M0Lm2e2S642;
    int32_t _M0L6_2atmpS1804;
    int32_t _M0L6_2atmpS1807;
    int32_t _M0L6_2atmpS1806;
    int32_t _M0L6_2atmpS1805;
    int32_t _M0L1qS655;
    int32_t _M0L6_2atmpS1803;
    int32_t _M0L6_2atmpS1802;
    int32_t _M0L1kS656;
    int32_t _M0L6_2atmpS1801;
    int32_t _M0L6_2atmpS1800;
    int32_t _M0L6_2atmpS1799;
    int32_t _M0L1iS657;
    struct _M0TPB8Pow5Pair _M0L4pow5S658;
    uint64_t _M0L6_2atmpS1798;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS659;
    uint64_t _M0L8_2avrOutS660;
    uint64_t _M0L8_2avpOutS661;
    uint64_t _M0L8_2avmOutS662;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1804 = _M0FPB9log10Pow2(_M0L6_2atmpS1808);
    _M0L6_2atmpS1807 = _M0Lm2e2S642;
    _M0L6_2atmpS1806 = _M0L6_2atmpS1807 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1805 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1806);
    _M0L1qS655 = _M0L6_2atmpS1804 - _M0L6_2atmpS1805;
    _M0Lm3e10S652 = _M0L1qS655;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1803 = _M0FPB8pow5bits(_M0L1qS655);
    _M0L6_2atmpS1802 = 125 + _M0L6_2atmpS1803;
    _M0L1kS656 = _M0L6_2atmpS1802 - 1;
    _M0L6_2atmpS1801 = _M0Lm2e2S642;
    _M0L6_2atmpS1800 = -_M0L6_2atmpS1801;
    _M0L6_2atmpS1799 = _M0L6_2atmpS1800 + _M0L1qS655;
    _M0L1iS657 = _M0L6_2atmpS1799 + _M0L1kS656;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S658 = _M0FPB22double__computeInvPow5(_M0L1qS655);
    _M0L6_2atmpS1798 = _M0Lm2m2S643;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS659
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1798, _M0L4pow5S658, _M0L1iS657, _M0L7mmShiftS648);
    _M0L8_2avrOutS660 = _M0L7_2abindS659.$0;
    _M0L8_2avpOutS661 = _M0L7_2abindS659.$1;
    _M0L8_2avmOutS662 = _M0L7_2abindS659.$2;
    _M0Lm2vrS649 = _M0L8_2avrOutS660;
    _M0Lm2vpS650 = _M0L8_2avpOutS661;
    _M0Lm2vmS651 = _M0L8_2avmOutS662;
    if (_M0L1qS655 <= 21) {
      int32_t _M0L6_2atmpS1794 = (int32_t)_M0L2mvS647;
      uint64_t _M0L6_2atmpS1797 = _M0L2mvS647 / 5ull;
      int32_t _M0L6_2atmpS1796 = (int32_t)_M0L6_2atmpS1797;
      int32_t _M0L6_2atmpS1795 = 5 * _M0L6_2atmpS1796;
      int32_t _M0L6mvMod5S663 = _M0L6_2atmpS1794 - _M0L6_2atmpS1795;
      if (_M0L6mvMod5S663 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS654
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS647, _M0L1qS655);
      } else if (_M0L4evenS646) {
        uint64_t _M0L6_2atmpS1788 = _M0L2mvS647 - 1ull;
        uint64_t _M0L6_2atmpS1789;
        uint64_t _M0L6_2atmpS1787;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1789 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS648);
        _M0L6_2atmpS1787 = _M0L6_2atmpS1788 - _M0L6_2atmpS1789;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS653
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1787, _M0L1qS655);
      } else {
        uint64_t _M0L6_2atmpS1790 = _M0Lm2vpS650;
        uint64_t _M0L6_2atmpS1793 = _M0L2mvS647 + 2ull;
        int32_t _M0L6_2atmpS1792;
        uint64_t _M0L6_2atmpS1791;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1792
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1793, _M0L1qS655);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1791 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1792);
        _M0Lm2vpS650 = _M0L6_2atmpS1790 - _M0L6_2atmpS1791;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1822 = _M0Lm2e2S642;
    int32_t _M0L6_2atmpS1821 = -_M0L6_2atmpS1822;
    int32_t _M0L6_2atmpS1816;
    int32_t _M0L6_2atmpS1820;
    int32_t _M0L6_2atmpS1819;
    int32_t _M0L6_2atmpS1818;
    int32_t _M0L6_2atmpS1817;
    int32_t _M0L1qS664;
    int32_t _M0L6_2atmpS1809;
    int32_t _M0L6_2atmpS1815;
    int32_t _M0L6_2atmpS1814;
    int32_t _M0L1iS665;
    int32_t _M0L6_2atmpS1813;
    int32_t _M0L1kS666;
    int32_t _M0L1jS667;
    struct _M0TPB8Pow5Pair _M0L4pow5S668;
    uint64_t _M0L6_2atmpS1812;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS669;
    uint64_t _M0L8_2avrOutS670;
    uint64_t _M0L8_2avpOutS671;
    uint64_t _M0L8_2avmOutS672;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1816 = _M0FPB9log10Pow5(_M0L6_2atmpS1821);
    _M0L6_2atmpS1820 = _M0Lm2e2S642;
    _M0L6_2atmpS1819 = -_M0L6_2atmpS1820;
    _M0L6_2atmpS1818 = _M0L6_2atmpS1819 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1817 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1818);
    _M0L1qS664 = _M0L6_2atmpS1816 - _M0L6_2atmpS1817;
    _M0L6_2atmpS1809 = _M0Lm2e2S642;
    _M0Lm3e10S652 = _M0L1qS664 + _M0L6_2atmpS1809;
    _M0L6_2atmpS1815 = _M0Lm2e2S642;
    _M0L6_2atmpS1814 = -_M0L6_2atmpS1815;
    _M0L1iS665 = _M0L6_2atmpS1814 - _M0L1qS664;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1813 = _M0FPB8pow5bits(_M0L1iS665);
    _M0L1kS666 = _M0L6_2atmpS1813 - 125;
    _M0L1jS667 = _M0L1qS664 - _M0L1kS666;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S668 = _M0FPB19double__computePow5(_M0L1iS665);
    _M0L6_2atmpS1812 = _M0Lm2m2S643;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS669
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1812, _M0L4pow5S668, _M0L1jS667, _M0L7mmShiftS648);
    _M0L8_2avrOutS670 = _M0L7_2abindS669.$0;
    _M0L8_2avpOutS671 = _M0L7_2abindS669.$1;
    _M0L8_2avmOutS672 = _M0L7_2abindS669.$2;
    _M0Lm2vrS649 = _M0L8_2avrOutS670;
    _M0Lm2vpS650 = _M0L8_2avpOutS671;
    _M0Lm2vmS651 = _M0L8_2avmOutS672;
    if (_M0L1qS664 <= 1) {
      _M0Lm17vrIsTrailingZerosS654 = 1;
      if (_M0L4evenS646) {
        int32_t _M0L6_2atmpS1810;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1810 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS648);
        _M0Lm17vmIsTrailingZerosS653 = _M0L6_2atmpS1810 == 1;
      } else {
        uint64_t _M0L6_2atmpS1811 = _M0Lm2vpS650;
        _M0Lm2vpS650 = _M0L6_2atmpS1811 - 1ull;
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
    int32_t _if__result_2496;
    uint64_t _M0L6_2atmpS1852;
    uint64_t _M0L6_2atmpS1858;
    uint64_t _M0L6_2atmpS1859;
    int32_t _if__result_2497;
    int32_t _M0L6_2atmpS1855;
    int64_t _M0L6_2atmpS1854;
    uint64_t _M0L6_2atmpS1853;
    while (1) {
      uint64_t _M0L6_2atmpS1835 = _M0Lm2vpS650;
      uint64_t _M0L7vpDiv10S676 = _M0L6_2atmpS1835 / 10ull;
      uint64_t _M0L6_2atmpS1834 = _M0Lm2vmS651;
      uint64_t _M0L7vmDiv10S677 = _M0L6_2atmpS1834 / 10ull;
      uint64_t _M0L6_2atmpS1833;
      int32_t _M0L6_2atmpS1830;
      int32_t _M0L6_2atmpS1832;
      int32_t _M0L6_2atmpS1831;
      int32_t _M0L7vmMod10S679;
      uint64_t _M0L6_2atmpS1829;
      uint64_t _M0L7vrDiv10S680;
      uint64_t _M0L6_2atmpS1828;
      int32_t _M0L6_2atmpS1825;
      int32_t _M0L6_2atmpS1827;
      int32_t _M0L6_2atmpS1826;
      int32_t _M0L7vrMod10S681;
      int32_t _M0L6_2atmpS1824;
      if (_M0L7vpDiv10S676 <= _M0L7vmDiv10S677) {
        break;
      }
      _M0L6_2atmpS1833 = _M0Lm2vmS651;
      _M0L6_2atmpS1830 = (int32_t)_M0L6_2atmpS1833;
      _M0L6_2atmpS1832 = (int32_t)_M0L7vmDiv10S677;
      _M0L6_2atmpS1831 = 10 * _M0L6_2atmpS1832;
      _M0L7vmMod10S679 = _M0L6_2atmpS1830 - _M0L6_2atmpS1831;
      _M0L6_2atmpS1829 = _M0Lm2vrS649;
      _M0L7vrDiv10S680 = _M0L6_2atmpS1829 / 10ull;
      _M0L6_2atmpS1828 = _M0Lm2vrS649;
      _M0L6_2atmpS1825 = (int32_t)_M0L6_2atmpS1828;
      _M0L6_2atmpS1827 = (int32_t)_M0L7vrDiv10S680;
      _M0L6_2atmpS1826 = 10 * _M0L6_2atmpS1827;
      _M0L7vrMod10S681 = _M0L6_2atmpS1825 - _M0L6_2atmpS1826;
      _M0Lm17vmIsTrailingZerosS653
      = _M0Lm17vmIsTrailingZerosS653 && _M0L7vmMod10S679 == 0;
      if (_M0Lm17vrIsTrailingZerosS654) {
        int32_t _M0L6_2atmpS1823 = _M0Lm16lastRemovedDigitS674;
        _M0Lm17vrIsTrailingZerosS654 = _M0L6_2atmpS1823 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS654 = 0;
      }
      _M0Lm16lastRemovedDigitS674 = _M0L7vrMod10S681;
      _M0Lm2vrS649 = _M0L7vrDiv10S680;
      _M0Lm2vpS650 = _M0L7vpDiv10S676;
      _M0Lm2vmS651 = _M0L7vmDiv10S677;
      _M0L6_2atmpS1824 = _M0Lm7removedS673;
      _M0Lm7removedS673 = _M0L6_2atmpS1824 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS653) {
      while (1) {
        uint64_t _M0L6_2atmpS1848 = _M0Lm2vmS651;
        uint64_t _M0L7vmDiv10S682 = _M0L6_2atmpS1848 / 10ull;
        uint64_t _M0L6_2atmpS1847 = _M0Lm2vmS651;
        int32_t _M0L6_2atmpS1844 = (int32_t)_M0L6_2atmpS1847;
        int32_t _M0L6_2atmpS1846 = (int32_t)_M0L7vmDiv10S682;
        int32_t _M0L6_2atmpS1845 = 10 * _M0L6_2atmpS1846;
        int32_t _M0L7vmMod10S683 = _M0L6_2atmpS1844 - _M0L6_2atmpS1845;
        uint64_t _M0L6_2atmpS1843;
        uint64_t _M0L7vpDiv10S685;
        uint64_t _M0L6_2atmpS1842;
        uint64_t _M0L7vrDiv10S686;
        uint64_t _M0L6_2atmpS1841;
        int32_t _M0L6_2atmpS1838;
        int32_t _M0L6_2atmpS1840;
        int32_t _M0L6_2atmpS1839;
        int32_t _M0L7vrMod10S687;
        int32_t _M0L6_2atmpS1837;
        if (_M0L7vmMod10S683 != 0) {
          break;
        }
        _M0L6_2atmpS1843 = _M0Lm2vpS650;
        _M0L7vpDiv10S685 = _M0L6_2atmpS1843 / 10ull;
        _M0L6_2atmpS1842 = _M0Lm2vrS649;
        _M0L7vrDiv10S686 = _M0L6_2atmpS1842 / 10ull;
        _M0L6_2atmpS1841 = _M0Lm2vrS649;
        _M0L6_2atmpS1838 = (int32_t)_M0L6_2atmpS1841;
        _M0L6_2atmpS1840 = (int32_t)_M0L7vrDiv10S686;
        _M0L6_2atmpS1839 = 10 * _M0L6_2atmpS1840;
        _M0L7vrMod10S687 = _M0L6_2atmpS1838 - _M0L6_2atmpS1839;
        if (_M0Lm17vrIsTrailingZerosS654) {
          int32_t _M0L6_2atmpS1836 = _M0Lm16lastRemovedDigitS674;
          _M0Lm17vrIsTrailingZerosS654 = _M0L6_2atmpS1836 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS654 = 0;
        }
        _M0Lm16lastRemovedDigitS674 = _M0L7vrMod10S687;
        _M0Lm2vrS649 = _M0L7vrDiv10S686;
        _M0Lm2vpS650 = _M0L7vpDiv10S685;
        _M0Lm2vmS651 = _M0L7vmDiv10S682;
        _M0L6_2atmpS1837 = _M0Lm7removedS673;
        _M0Lm7removedS673 = _M0L6_2atmpS1837 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS654) {
      int32_t _M0L6_2atmpS1851 = _M0Lm16lastRemovedDigitS674;
      if (_M0L6_2atmpS1851 == 5) {
        uint64_t _M0L6_2atmpS1850 = _M0Lm2vrS649;
        uint64_t _M0L6_2atmpS1849 = _M0L6_2atmpS1850 % 2ull;
        _if__result_2496 = _M0L6_2atmpS1849 == 0ull;
      } else {
        _if__result_2496 = 0;
      }
    } else {
      _if__result_2496 = 0;
    }
    if (_if__result_2496) {
      _M0Lm16lastRemovedDigitS674 = 4;
    }
    _M0L6_2atmpS1852 = _M0Lm2vrS649;
    _M0L6_2atmpS1858 = _M0Lm2vrS649;
    _M0L6_2atmpS1859 = _M0Lm2vmS651;
    if (_M0L6_2atmpS1858 == _M0L6_2atmpS1859) {
      if (!_M0L4evenS646) {
        _if__result_2497 = 1;
      } else {
        int32_t _M0L6_2atmpS1857 = _M0Lm17vmIsTrailingZerosS653;
        _if__result_2497 = !_M0L6_2atmpS1857;
      }
    } else {
      _if__result_2497 = 0;
    }
    if (_if__result_2497) {
      _M0L6_2atmpS1855 = 1;
    } else {
      int32_t _M0L6_2atmpS1856 = _M0Lm16lastRemovedDigitS674;
      _M0L6_2atmpS1855 = _M0L6_2atmpS1856 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1854 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1855);
    _M0L6_2atmpS1853 = *(uint64_t*)&_M0L6_2atmpS1854;
    _M0Lm6outputS675 = _M0L6_2atmpS1852 + _M0L6_2atmpS1853;
  } else {
    int32_t _M0Lm7roundUpS688 = 0;
    uint64_t _M0L6_2atmpS1880 = _M0Lm2vpS650;
    uint64_t _M0L8vpDiv100S689 = _M0L6_2atmpS1880 / 100ull;
    uint64_t _M0L6_2atmpS1879 = _M0Lm2vmS651;
    uint64_t _M0L8vmDiv100S690 = _M0L6_2atmpS1879 / 100ull;
    uint64_t _M0L6_2atmpS1874;
    uint64_t _M0L6_2atmpS1877;
    uint64_t _M0L6_2atmpS1878;
    int32_t _M0L6_2atmpS1876;
    uint64_t _M0L6_2atmpS1875;
    if (_M0L8vpDiv100S689 > _M0L8vmDiv100S690) {
      uint64_t _M0L6_2atmpS1865 = _M0Lm2vrS649;
      uint64_t _M0L8vrDiv100S691 = _M0L6_2atmpS1865 / 100ull;
      uint64_t _M0L6_2atmpS1864 = _M0Lm2vrS649;
      int32_t _M0L6_2atmpS1861 = (int32_t)_M0L6_2atmpS1864;
      int32_t _M0L6_2atmpS1863 = (int32_t)_M0L8vrDiv100S691;
      int32_t _M0L6_2atmpS1862 = 100 * _M0L6_2atmpS1863;
      int32_t _M0L8vrMod100S692 = _M0L6_2atmpS1861 - _M0L6_2atmpS1862;
      int32_t _M0L6_2atmpS1860;
      _M0Lm7roundUpS688 = _M0L8vrMod100S692 >= 50;
      _M0Lm2vrS649 = _M0L8vrDiv100S691;
      _M0Lm2vpS650 = _M0L8vpDiv100S689;
      _M0Lm2vmS651 = _M0L8vmDiv100S690;
      _M0L6_2atmpS1860 = _M0Lm7removedS673;
      _M0Lm7removedS673 = _M0L6_2atmpS1860 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1873 = _M0Lm2vpS650;
      uint64_t _M0L7vpDiv10S693 = _M0L6_2atmpS1873 / 10ull;
      uint64_t _M0L6_2atmpS1872 = _M0Lm2vmS651;
      uint64_t _M0L7vmDiv10S694 = _M0L6_2atmpS1872 / 10ull;
      uint64_t _M0L6_2atmpS1871;
      uint64_t _M0L7vrDiv10S696;
      uint64_t _M0L6_2atmpS1870;
      int32_t _M0L6_2atmpS1867;
      int32_t _M0L6_2atmpS1869;
      int32_t _M0L6_2atmpS1868;
      int32_t _M0L7vrMod10S697;
      int32_t _M0L6_2atmpS1866;
      if (_M0L7vpDiv10S693 <= _M0L7vmDiv10S694) {
        break;
      }
      _M0L6_2atmpS1871 = _M0Lm2vrS649;
      _M0L7vrDiv10S696 = _M0L6_2atmpS1871 / 10ull;
      _M0L6_2atmpS1870 = _M0Lm2vrS649;
      _M0L6_2atmpS1867 = (int32_t)_M0L6_2atmpS1870;
      _M0L6_2atmpS1869 = (int32_t)_M0L7vrDiv10S696;
      _M0L6_2atmpS1868 = 10 * _M0L6_2atmpS1869;
      _M0L7vrMod10S697 = _M0L6_2atmpS1867 - _M0L6_2atmpS1868;
      _M0Lm7roundUpS688 = _M0L7vrMod10S697 >= 5;
      _M0Lm2vrS649 = _M0L7vrDiv10S696;
      _M0Lm2vpS650 = _M0L7vpDiv10S693;
      _M0Lm2vmS651 = _M0L7vmDiv10S694;
      _M0L6_2atmpS1866 = _M0Lm7removedS673;
      _M0Lm7removedS673 = _M0L6_2atmpS1866 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1874 = _M0Lm2vrS649;
    _M0L6_2atmpS1877 = _M0Lm2vrS649;
    _M0L6_2atmpS1878 = _M0Lm2vmS651;
    _M0L6_2atmpS1876
    = _M0L6_2atmpS1877 == _M0L6_2atmpS1878 || _M0Lm7roundUpS688;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1875 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1876);
    _M0Lm6outputS675 = _M0L6_2atmpS1874 + _M0L6_2atmpS1875;
  }
  _M0L6_2atmpS1882 = _M0Lm3e10S652;
  _M0L6_2atmpS1883 = _M0Lm7removedS673;
  _M0L3expS698 = _M0L6_2atmpS1882 + _M0L6_2atmpS1883;
  _M0L6_2atmpS1881 = _M0Lm6outputS675;
  _block_2499
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2499)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2499->$0 = _M0L6_2atmpS1881;
  _block_2499->$1 = _M0L3expS698;
  return _block_2499;
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
  int32_t _M0L6_2atmpS1782;
  int32_t _M0L6_2atmpS1781;
  int32_t _M0L4baseS620;
  int32_t _M0L5base2S622;
  int32_t _M0L6offsetS623;
  int32_t _M0L6_2atmpS1780;
  uint64_t _M0L4mul0S624;
  int32_t _M0L6_2atmpS1779;
  int32_t _M0L6_2atmpS1778;
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
  int32_t _M0L6_2atmpS1776;
  int32_t _M0L6_2atmpS1777;
  int32_t _M0L5deltaS635;
  uint64_t _M0L6_2atmpS1775;
  uint64_t _M0L6_2atmpS1767;
  int32_t _M0L6_2atmpS1774;
  uint32_t _M0L6_2atmpS1771;
  int32_t _M0L6_2atmpS1773;
  int32_t _M0L6_2atmpS1772;
  uint32_t _M0L6_2atmpS1770;
  uint32_t _M0L6_2atmpS1769;
  uint64_t _M0L6_2atmpS1768;
  uint64_t _M0L1aS636;
  uint64_t _M0L6_2atmpS1766;
  uint64_t _M0L1bS637;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1782 = _M0L1iS621 + 26;
  _M0L6_2atmpS1781 = _M0L6_2atmpS1782 - 1;
  _M0L4baseS620 = _M0L6_2atmpS1781 / 26;
  _M0L5base2S622 = _M0L4baseS620 * 26;
  _M0L6offsetS623 = _M0L5base2S622 - _M0L1iS621;
  _M0L6_2atmpS1780 = _M0L4baseS620 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S624
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1780);
  _M0L6_2atmpS1779 = _M0L4baseS620 * 2;
  _M0L6_2atmpS1778 = _M0L6_2atmpS1779 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S625
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1778);
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
    uint64_t _M0L6_2atmpS1765 = _M0Lm5high1S634;
    _M0Lm5high1S634 = _M0L6_2atmpS1765 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1776 = _M0FPB8pow5bits(_M0L5base2S622);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1777 = _M0FPB8pow5bits(_M0L1iS621);
  _M0L5deltaS635 = _M0L6_2atmpS1776 - _M0L6_2atmpS1777;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1775
  = _M0FPB13shiftright128(_M0L7_2alow0S631, _M0L3sumS633, _M0L5deltaS635);
  _M0L6_2atmpS1767 = _M0L6_2atmpS1775 + 1ull;
  _M0L6_2atmpS1774 = _M0L1iS621 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1771
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1774);
  _M0L6_2atmpS1773 = _M0L1iS621 % 16;
  _M0L6_2atmpS1772 = _M0L6_2atmpS1773 << 1;
  _M0L6_2atmpS1770 = _M0L6_2atmpS1771 >> (_M0L6_2atmpS1772 & 31);
  _M0L6_2atmpS1769 = _M0L6_2atmpS1770 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1768 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1769);
  _M0L1aS636 = _M0L6_2atmpS1767 + _M0L6_2atmpS1768;
  _M0L6_2atmpS1766 = _M0Lm5high1S634;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS637
  = _M0FPB13shiftright128(_M0L3sumS633, _M0L6_2atmpS1766, _M0L5deltaS635);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS636, .$1 = _M0L1bS637};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS603) {
  int32_t _M0L4baseS602;
  int32_t _M0L5base2S604;
  int32_t _M0L6offsetS605;
  int32_t _M0L6_2atmpS1764;
  uint64_t _M0L4mul0S606;
  int32_t _M0L6_2atmpS1763;
  int32_t _M0L6_2atmpS1762;
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
  int32_t _M0L6_2atmpS1760;
  int32_t _M0L6_2atmpS1761;
  int32_t _M0L5deltaS617;
  uint64_t _M0L6_2atmpS1752;
  int32_t _M0L6_2atmpS1759;
  uint32_t _M0L6_2atmpS1756;
  int32_t _M0L6_2atmpS1758;
  int32_t _M0L6_2atmpS1757;
  uint32_t _M0L6_2atmpS1755;
  uint32_t _M0L6_2atmpS1754;
  uint64_t _M0L6_2atmpS1753;
  uint64_t _M0L1aS618;
  uint64_t _M0L6_2atmpS1751;
  uint64_t _M0L1bS619;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS602 = _M0L1iS603 / 26;
  _M0L5base2S604 = _M0L4baseS602 * 26;
  _M0L6offsetS605 = _M0L1iS603 - _M0L5base2S604;
  _M0L6_2atmpS1764 = _M0L4baseS602 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S606
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1764);
  _M0L6_2atmpS1763 = _M0L4baseS602 * 2;
  _M0L6_2atmpS1762 = _M0L6_2atmpS1763 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S607
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1762);
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
    uint64_t _M0L6_2atmpS1750 = _M0Lm5high1S616;
    _M0Lm5high1S616 = _M0L6_2atmpS1750 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1760 = _M0FPB8pow5bits(_M0L1iS603);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1761 = _M0FPB8pow5bits(_M0L5base2S604);
  _M0L5deltaS617 = _M0L6_2atmpS1760 - _M0L6_2atmpS1761;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1752
  = _M0FPB13shiftright128(_M0L7_2alow0S613, _M0L3sumS615, _M0L5deltaS617);
  _M0L6_2atmpS1759 = _M0L1iS603 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1756
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1759);
  _M0L6_2atmpS1758 = _M0L1iS603 % 16;
  _M0L6_2atmpS1757 = _M0L6_2atmpS1758 << 1;
  _M0L6_2atmpS1755 = _M0L6_2atmpS1756 >> (_M0L6_2atmpS1757 & 31);
  _M0L6_2atmpS1754 = _M0L6_2atmpS1755 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1753 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1754);
  _M0L1aS618 = _M0L6_2atmpS1752 + _M0L6_2atmpS1753;
  _M0L6_2atmpS1751 = _M0Lm5high1S616;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS619
  = _M0FPB13shiftright128(_M0L3sumS615, _M0L6_2atmpS1751, _M0L5deltaS617);
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
  uint64_t _M0L6_2atmpS1749;
  uint64_t _M0L2hiS584;
  uint64_t _M0L3lo2S585;
  uint64_t _M0L6_2atmpS1747;
  uint64_t _M0L6_2atmpS1748;
  uint64_t _M0L4mid2S586;
  uint64_t _M0L6_2atmpS1746;
  uint64_t _M0L3hi2S587;
  int32_t _M0L6_2atmpS1745;
  int32_t _M0L6_2atmpS1744;
  uint64_t _M0L2vpS588;
  uint64_t _M0Lm2vmS590;
  int32_t _M0L6_2atmpS1743;
  int32_t _M0L6_2atmpS1742;
  uint64_t _M0L2vrS601;
  uint64_t _M0L6_2atmpS1741;
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
    _M0L6_2atmpS1749 = 1ull;
  } else {
    _M0L6_2atmpS1749 = 0ull;
  }
  _M0L2hiS584 = _M0L6_2ahi2S582 + _M0L6_2atmpS1749;
  _M0L3lo2S585 = _M0L5_2aloS578 + _M0L7_2amul0S572;
  _M0L6_2atmpS1747 = _M0L3midS583 + _M0L7_2amul1S574;
  if (_M0L3lo2S585 < _M0L5_2aloS578) {
    _M0L6_2atmpS1748 = 1ull;
  } else {
    _M0L6_2atmpS1748 = 0ull;
  }
  _M0L4mid2S586 = _M0L6_2atmpS1747 + _M0L6_2atmpS1748;
  if (_M0L4mid2S586 < _M0L3midS583) {
    _M0L6_2atmpS1746 = 1ull;
  } else {
    _M0L6_2atmpS1746 = 0ull;
  }
  _M0L3hi2S587 = _M0L2hiS584 + _M0L6_2atmpS1746;
  _M0L6_2atmpS1745 = _M0L1jS589 - 64;
  _M0L6_2atmpS1744 = _M0L6_2atmpS1745 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS588
  = _M0FPB13shiftright128(_M0L4mid2S586, _M0L3hi2S587, _M0L6_2atmpS1744);
  _M0Lm2vmS590 = 0ull;
  if (_M0L7mmShiftS591) {
    uint64_t _M0L3lo3S592 = _M0L5_2aloS578 - _M0L7_2amul0S572;
    uint64_t _M0L6_2atmpS1731 = _M0L3midS583 - _M0L7_2amul1S574;
    uint64_t _M0L6_2atmpS1732;
    uint64_t _M0L4mid3S593;
    uint64_t _M0L6_2atmpS1730;
    uint64_t _M0L3hi3S594;
    int32_t _M0L6_2atmpS1729;
    int32_t _M0L6_2atmpS1728;
    if (_M0L5_2aloS578 < _M0L3lo3S592) {
      _M0L6_2atmpS1732 = 1ull;
    } else {
      _M0L6_2atmpS1732 = 0ull;
    }
    _M0L4mid3S593 = _M0L6_2atmpS1731 - _M0L6_2atmpS1732;
    if (_M0L3midS583 < _M0L4mid3S593) {
      _M0L6_2atmpS1730 = 1ull;
    } else {
      _M0L6_2atmpS1730 = 0ull;
    }
    _M0L3hi3S594 = _M0L2hiS584 - _M0L6_2atmpS1730;
    _M0L6_2atmpS1729 = _M0L1jS589 - 64;
    _M0L6_2atmpS1728 = _M0L6_2atmpS1729 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS590
    = _M0FPB13shiftright128(_M0L4mid3S593, _M0L3hi3S594, _M0L6_2atmpS1728);
  } else {
    uint64_t _M0L3lo3S595 = _M0L5_2aloS578 + _M0L5_2aloS578;
    uint64_t _M0L6_2atmpS1739 = _M0L3midS583 + _M0L3midS583;
    uint64_t _M0L6_2atmpS1740;
    uint64_t _M0L4mid3S596;
    uint64_t _M0L6_2atmpS1737;
    uint64_t _M0L6_2atmpS1738;
    uint64_t _M0L3hi3S597;
    uint64_t _M0L3lo4S598;
    uint64_t _M0L6_2atmpS1735;
    uint64_t _M0L6_2atmpS1736;
    uint64_t _M0L4mid4S599;
    uint64_t _M0L6_2atmpS1734;
    uint64_t _M0L3hi4S600;
    int32_t _M0L6_2atmpS1733;
    if (_M0L3lo3S595 < _M0L5_2aloS578) {
      _M0L6_2atmpS1740 = 1ull;
    } else {
      _M0L6_2atmpS1740 = 0ull;
    }
    _M0L4mid3S596 = _M0L6_2atmpS1739 + _M0L6_2atmpS1740;
    _M0L6_2atmpS1737 = _M0L2hiS584 + _M0L2hiS584;
    if (_M0L4mid3S596 < _M0L3midS583) {
      _M0L6_2atmpS1738 = 1ull;
    } else {
      _M0L6_2atmpS1738 = 0ull;
    }
    _M0L3hi3S597 = _M0L6_2atmpS1737 + _M0L6_2atmpS1738;
    _M0L3lo4S598 = _M0L3lo3S595 - _M0L7_2amul0S572;
    _M0L6_2atmpS1735 = _M0L4mid3S596 - _M0L7_2amul1S574;
    if (_M0L3lo3S595 < _M0L3lo4S598) {
      _M0L6_2atmpS1736 = 1ull;
    } else {
      _M0L6_2atmpS1736 = 0ull;
    }
    _M0L4mid4S599 = _M0L6_2atmpS1735 - _M0L6_2atmpS1736;
    if (_M0L4mid3S596 < _M0L4mid4S599) {
      _M0L6_2atmpS1734 = 1ull;
    } else {
      _M0L6_2atmpS1734 = 0ull;
    }
    _M0L3hi4S600 = _M0L3hi3S597 - _M0L6_2atmpS1734;
    _M0L6_2atmpS1733 = _M0L1jS589 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS590
    = _M0FPB13shiftright128(_M0L4mid4S599, _M0L3hi4S600, _M0L6_2atmpS1733);
  }
  _M0L6_2atmpS1743 = _M0L1jS589 - 64;
  _M0L6_2atmpS1742 = _M0L6_2atmpS1743 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS601
  = _M0FPB13shiftright128(_M0L3midS583, _M0L2hiS584, _M0L6_2atmpS1742);
  _M0L6_2atmpS1741 = _M0Lm2vmS590;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS601,
                                                .$1 = _M0L2vpS588,
                                                .$2 = _M0L6_2atmpS1741};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS570,
  int32_t _M0L1pS571
) {
  uint64_t _M0L6_2atmpS1727;
  uint64_t _M0L6_2atmpS1726;
  uint64_t _M0L6_2atmpS1725;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1727 = 1ull << (_M0L1pS571 & 63);
  _M0L6_2atmpS1726 = _M0L6_2atmpS1727 - 1ull;
  _M0L6_2atmpS1725 = _M0L5valueS570 & _M0L6_2atmpS1726;
  return _M0L6_2atmpS1725 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS568,
  int32_t _M0L1pS569
) {
  int32_t _M0L6_2atmpS1724;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1724 = _M0FPB10pow5Factor(_M0L5valueS568);
  return _M0L6_2atmpS1724 >= _M0L1pS569;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS563) {
  uint64_t _M0L6_2atmpS1715;
  uint64_t _M0L6_2atmpS1716;
  uint64_t _M0L6_2atmpS1717;
  uint64_t _M0L6_2atmpS1718;
  uint64_t _M0L6_2atmpS1723;
  int32_t _M0L5countS564;
  uint64_t _M0L1vS565;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1715 = _M0L5valueS563 % 5ull;
  if (_M0L6_2atmpS1715 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1716 = _M0L5valueS563 % 25ull;
  if (_M0L6_2atmpS1716 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1717 = _M0L5valueS563 % 125ull;
  if (_M0L6_2atmpS1717 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1718 = _M0L5valueS563 % 625ull;
  if (_M0L6_2atmpS1718 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1723 = _M0L5valueS563 / 625ull;
  _M0L5countS564 = 4;
  _M0L1vS565 = _M0L6_2atmpS1723;
  while (1) {
    if (_M0L1vS565 > 0ull) {
      uint64_t _M0L6_2atmpS1719 = _M0L1vS565 % 5ull;
      int32_t _M0L6_2atmpS1720;
      uint64_t _M0L6_2atmpS1721;
      if (_M0L6_2atmpS1719 != 0ull) {
        return _M0L5countS564;
      }
      _M0L6_2atmpS1720 = _M0L5countS564 + 1;
      _M0L6_2atmpS1721 = _M0L1vS565 / 5ull;
      _M0L5countS564 = _M0L6_2atmpS1720;
      _M0L1vS565 = _M0L6_2atmpS1721;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS567;
      moonbit_string_t _M0L6_2atmpS1722;
      int32_t _result_2501;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS567
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS567, (moonbit_string_t)moonbit_string_literal_11.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS567, _M0L5valueS563);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1722
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS567);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS567);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2501 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1722);
      moonbit_decref_cycle_free(_M0L6_2atmpS1722);
      return _result_2501;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS562,
  uint64_t _M0L2hiS560,
  int32_t _M0L4distS561
) {
  int32_t _M0L6_2atmpS1714;
  uint64_t _M0L6_2atmpS1712;
  uint64_t _M0L6_2atmpS1713;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1714 = 64 - _M0L4distS561;
  _M0L6_2atmpS1712 = _M0L2hiS560 << (_M0L6_2atmpS1714 & 63);
  _M0L6_2atmpS1713 = _M0L2loS562 >> (_M0L4distS561 & 63);
  return _M0L6_2atmpS1712 | _M0L6_2atmpS1713;
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
  uint64_t _M0L6_2atmpS1710;
  uint64_t _M0L6_2atmpS1711;
  uint64_t _M0L1yS556;
  uint64_t _M0L6_2atmpS1708;
  uint64_t _M0L6_2atmpS1709;
  uint64_t _M0L1zS557;
  uint64_t _M0L6_2atmpS1706;
  uint64_t _M0L6_2atmpS1707;
  uint64_t _M0L6_2atmpS1704;
  uint64_t _M0L6_2atmpS1705;
  uint64_t _M0L1wS558;
  uint64_t _M0L2loS559;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS549 = _M0L1aS550 & 4294967295ull;
  _M0L3aHiS551 = _M0L1aS550 >> 32;
  _M0L3bLoS552 = _M0L1bS553 & 4294967295ull;
  _M0L3bHiS554 = _M0L1bS553 >> 32;
  _M0L1xS555 = _M0L3aLoS549 * _M0L3bLoS552;
  _M0L6_2atmpS1710 = _M0L3aHiS551 * _M0L3bLoS552;
  _M0L6_2atmpS1711 = _M0L1xS555 >> 32;
  _M0L1yS556 = _M0L6_2atmpS1710 + _M0L6_2atmpS1711;
  _M0L6_2atmpS1708 = _M0L3aLoS549 * _M0L3bHiS554;
  _M0L6_2atmpS1709 = _M0L1yS556 & 4294967295ull;
  _M0L1zS557 = _M0L6_2atmpS1708 + _M0L6_2atmpS1709;
  _M0L6_2atmpS1706 = _M0L3aHiS551 * _M0L3bHiS554;
  _M0L6_2atmpS1707 = _M0L1yS556 >> 32;
  _M0L6_2atmpS1704 = _M0L6_2atmpS1706 + _M0L6_2atmpS1707;
  _M0L6_2atmpS1705 = _M0L1zS557 >> 32;
  _M0L1wS558 = _M0L6_2atmpS1704 + _M0L6_2atmpS1705;
  _M0L2loS559 = _M0L1aS550 * _M0L1bS553;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS559, .$1 = _M0L1wS558};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS547,
  int32_t _M0L4fromS544,
  int32_t _M0L2toS543
) {
  int32_t _M0L3lenS542;
  int32_t _M0L6_2atmpS1703;
  uint16_t* _M0L6bufferS545;
  int32_t _M0L1iS546;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS542 = _M0L2toS543 - _M0L4fromS544;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1703 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS545
  = (uint16_t*)moonbit_make_string(_M0L3lenS542, _M0L6_2atmpS1703);
  _M0L1iS546 = 0;
  while (1) {
    if (_M0L1iS546 < _M0L3lenS542) {
      int32_t _M0L6_2atmpS1701 = _M0L4fromS544 + _M0L1iS546;
      int32_t _M0L6_2atmpS1700;
      int32_t _M0L6_2atmpS1699;
      int32_t _M0L6_2atmpS1702;
      if (
        _M0L6_2atmpS1701 < 0
        || _M0L6_2atmpS1701 >= Moonbit_array_length(_M0L5bytesS547)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1700 = (int32_t)_M0L5bytesS547[_M0L6_2atmpS1701];
      _M0L6_2atmpS1699 = (uint16_t)_M0L6_2atmpS1700;
      if (
        _M0L1iS546 < 0 || _M0L1iS546 >= Moonbit_array_length(_M0L6bufferS545)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS545[_M0L1iS546] = _M0L6_2atmpS1699;
      _M0L6_2atmpS1702 = _M0L1iS546 + 1;
      _M0L1iS546 = _M0L6_2atmpS1702;
      continue;
    }
    break;
  }
  return _M0L6bufferS545;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS541) {
  int32_t _M0L6_2atmpS1698;
  uint32_t _M0L6_2atmpS1697;
  uint32_t _M0L6_2atmpS1696;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1698 = _M0L1eS541 * 78913;
  _M0L6_2atmpS1697 = *(uint32_t*)&_M0L6_2atmpS1698;
  _M0L6_2atmpS1696 = _M0L6_2atmpS1697 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1696;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS540) {
  int32_t _M0L6_2atmpS1695;
  uint32_t _M0L6_2atmpS1694;
  uint32_t _M0L6_2atmpS1693;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1695 = _M0L1eS540 * 732923;
  _M0L6_2atmpS1694 = *(uint32_t*)&_M0L6_2atmpS1695;
  _M0L6_2atmpS1693 = _M0L6_2atmpS1694 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1693;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS538,
  int32_t _M0L8exponentS539,
  int32_t _M0L8mantissaS536
) {
  moonbit_string_t _M0L1sS537;
  moonbit_string_t _result_2504;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS536) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L4signS538) {
    _M0L1sS537 = (moonbit_string_t)moonbit_string_literal_13.data;
  } else {
    _M0L1sS537 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS539) {
    moonbit_string_t _result_2503;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2503
    = moonbit_add_string(_M0L1sS537, (moonbit_string_t)moonbit_string_literal_14.data);
    moonbit_decref_cycle_free(_M0L1sS537);
    return _result_2503;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2504
  = moonbit_add_string(_M0L1sS537, (moonbit_string_t)moonbit_string_literal_15.data);
  moonbit_decref_cycle_free(_M0L1sS537);
  return _result_2504;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS535) {
  int32_t _M0L6_2atmpS1692;
  uint32_t _M0L6_2atmpS1691;
  uint32_t _M0L6_2atmpS1690;
  int32_t _M0L6_2atmpS1689;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1692 = _M0L1eS535 * 1217359;
  _M0L6_2atmpS1691 = *(uint32_t*)&_M0L6_2atmpS1692;
  _M0L6_2atmpS1690 = _M0L6_2atmpS1691 >> 19;
  _M0L6_2atmpS1689 = *(int32_t*)&_M0L6_2atmpS1690;
  return _M0L6_2atmpS1689 + 1;
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
  int32_t _M0L3lenS529
) {
  float* _M0L6_2atmpS1685;
  struct _M0TPB5ArrayGfE* _block_2505;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1685 = (float*)moonbit_make_float_array_raw(_M0L3lenS529);
  _block_2505
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2505)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 35, 0);
  _block_2505->$0 = _M0L6_2atmpS1685;
  _block_2505->$1 = _M0L3lenS529;
  return _block_2505;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS530
) {
  uint8_t* _M0L6_2atmpS1686;
  struct _M0TPB5ArrayGbE* _block_2506;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1686 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS530);
  _block_2506
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2506)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 46, 0);
  _block_2506->$0 = _M0L6_2atmpS1686;
  _block_2506->$1 = _M0L3lenS530;
  return _block_2506;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS531
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1687;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2507;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1687
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS531, 0);
  _block_2507
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2507)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 49, 0);
  _block_2507->$0 = _M0L6_2atmpS1687;
  _block_2507->$1 = _M0L3lenS531;
  return _block_2507;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS532
) {
  int32_t* _M0L6_2atmpS1688;
  struct _M0TPB5ArrayGiE* _block_2508;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1688 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS532);
  _block_2508
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2508)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 38, 0);
  _block_2508->$0 = _M0L6_2atmpS1688;
  _block_2508->$1 = _M0L3lenS532;
  return _block_2508;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS525,
  int32_t _M0L5indexS526
) {
  uint64_t* _M0L6_2atmpS1683;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1683 = _M0L4selfS525;
  if (
    _M0L5indexS526 < 0
    || _M0L5indexS526 >= Moonbit_array_length(_M0L6_2atmpS1683)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1683[_M0L5indexS526];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint32_t* _M0L6_2atmpS1684;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1684 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS1684)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1684[_M0L5indexS528];
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

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS510,
  moonbit_string_t _M0L5valueS512
) {
  int32_t _M0L3lenS1655;
  moonbit_string_t* _M0L6_2atmpS1657;
  int32_t _M0L6_2atmpS1656;
  int32_t _M0L6lengthS511;
  moonbit_string_t* _M0L3bufS1660;
  moonbit_string_t _M0L6_2aoldS2389;
  int32_t _M0L6_2atmpS1661;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1655 = _M0L4selfS510->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1657 = _M0MPC15array5Array6bufferGsE(_M0L4selfS510);
  _M0L6_2atmpS1656 = Moonbit_array_length(_M0L6_2atmpS1657);
  moonbit_decref_cycle_free(_M0L6_2atmpS1657);
  if (_M0L3lenS1655 == _M0L6_2atmpS1656) {
    int32_t _M0L3lenS1659 = _M0L4selfS510->$1;
    int32_t _M0L6_2atmpS1658 = _M0L3lenS1659 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS510, _M0L6_2atmpS1658);
  }
  _M0L6lengthS511 = _M0L4selfS510->$1;
  _M0L3bufS1660 = _M0L4selfS510->$0;
  _M0L6_2aoldS2389 = (moonbit_string_t)_M0L3bufS1660[_M0L6lengthS511];
  moonbit_decref_cycle_free(_M0L6_2aoldS2389);
  _M0L3bufS1660[_M0L6lengthS511] = _M0L5valueS512;
  _M0L6_2atmpS1661 = _M0L6lengthS511 + 1;
  _M0L4selfS510->$1 = _M0L6_2atmpS1661;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS513,
  struct _M0TUsiE* _M0L5valueS515
) {
  int32_t _M0L3lenS1662;
  struct _M0TUsiE** _M0L6_2atmpS1664;
  int32_t _M0L6_2atmpS1663;
  int32_t _M0L6lengthS514;
  struct _M0TUsiE** _M0L3bufS1667;
  struct _M0TUsiE* _M0L6_2aoldS2390;
  int32_t _M0L6_2atmpS1668;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1662 = _M0L4selfS513->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1664 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS513);
  _M0L6_2atmpS1663 = Moonbit_array_length(_M0L6_2atmpS1664);
  moonbit_decref_cycle_free(_M0L6_2atmpS1664);
  if (_M0L3lenS1662 == _M0L6_2atmpS1663) {
    int32_t _M0L3lenS1666 = _M0L4selfS513->$1;
    int32_t _M0L6_2atmpS1665 = _M0L3lenS1666 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS513, _M0L6_2atmpS1665);
  }
  _M0L6lengthS514 = _M0L4selfS513->$1;
  _M0L3bufS1667 = _M0L4selfS513->$0;
  _M0L6_2aoldS2390 = (struct _M0TUsiE*)_M0L3bufS1667[_M0L6lengthS514];
  if (_M0L6_2aoldS2390) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2390);
  }
  _M0L3bufS1667[_M0L6lengthS514] = _M0L5valueS515;
  _M0L6_2atmpS1668 = _M0L6lengthS514 + 1;
  _M0L4selfS513->$1 = _M0L6_2atmpS1668;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS516,
  int32_t _M0L5valueS518
) {
  int32_t _M0L3lenS1669;
  int32_t* _M0L6_2atmpS1671;
  int32_t _M0L6_2atmpS1670;
  int32_t _M0L6lengthS517;
  int32_t* _M0L3bufS1674;
  int32_t _M0L6_2atmpS1675;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1669 = _M0L4selfS516->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1671 = _M0MPC15array5Array6bufferGiE(_M0L4selfS516);
  _M0L6_2atmpS1670 = Moonbit_array_length(_M0L6_2atmpS1671);
  moonbit_decref_cycle_free(_M0L6_2atmpS1671);
  if (_M0L3lenS1669 == _M0L6_2atmpS1670) {
    int32_t _M0L3lenS1673 = _M0L4selfS516->$1;
    int32_t _M0L6_2atmpS1672 = _M0L3lenS1673 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS516, _M0L6_2atmpS1672);
  }
  _M0L6lengthS517 = _M0L4selfS516->$1;
  _M0L3bufS1674 = _M0L4selfS516->$0;
  _M0L3bufS1674[_M0L6lengthS517] = _M0L5valueS518;
  _M0L6_2atmpS1675 = _M0L6lengthS517 + 1;
  _M0L4selfS516->$1 = _M0L6_2atmpS1675;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS519,
  float _M0L5valueS521
) {
  int32_t _M0L3lenS1676;
  float* _M0L6_2atmpS1678;
  int32_t _M0L6_2atmpS1677;
  int32_t _M0L6lengthS520;
  float* _M0L3bufS1681;
  int32_t _M0L6_2atmpS1682;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1676 = _M0L4selfS519->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1678 = _M0MPC15array5Array6bufferGfE(_M0L4selfS519);
  _M0L6_2atmpS1677 = Moonbit_array_length(_M0L6_2atmpS1678);
  moonbit_decref_cycle_free(_M0L6_2atmpS1678);
  if (_M0L3lenS1676 == _M0L6_2atmpS1677) {
    int32_t _M0L3lenS1680 = _M0L4selfS519->$1;
    int32_t _M0L6_2atmpS1679 = _M0L3lenS1680 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS519, _M0L6_2atmpS1679);
  }
  _M0L6lengthS520 = _M0L4selfS519->$1;
  _M0L3bufS1681 = _M0L4selfS519->$0;
  _M0L3bufS1681[_M0L6lengthS520] = _M0L5valueS521;
  _M0L6_2atmpS1682 = _M0L6lengthS520 + 1;
  _M0L4selfS519->$1 = _M0L6_2atmpS1682;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS495,
  int32_t _M0L8requiredS497
) {
  int32_t _M0L8old__capS494;
  int32_t _M0L3lenS1651;
  int32_t _M0L8new__capS496;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS494 = _M0MPC15array5Array8capacityGsE(_M0L4selfS495);
  _M0L3lenS1651 = _M0L4selfS495->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS496
  = _M0FPB23array__growth__capacity(_M0L8old__capS494, _M0L3lenS1651, _M0L8requiredS497);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS495, _M0L8new__capS496);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS499,
  int32_t _M0L8requiredS501
) {
  int32_t _M0L8old__capS498;
  int32_t _M0L3lenS1652;
  int32_t _M0L8new__capS500;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS498 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS499);
  _M0L3lenS1652 = _M0L4selfS499->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS500
  = _M0FPB23array__growth__capacity(_M0L8old__capS498, _M0L3lenS1652, _M0L8requiredS501);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS499, _M0L8new__capS500);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS503,
  int32_t _M0L8requiredS505
) {
  int32_t _M0L8old__capS502;
  int32_t _M0L3lenS1653;
  int32_t _M0L8new__capS504;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS502 = _M0MPC15array5Array8capacityGiE(_M0L4selfS503);
  _M0L3lenS1653 = _M0L4selfS503->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS504
  = _M0FPB23array__growth__capacity(_M0L8old__capS502, _M0L3lenS1653, _M0L8requiredS505);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS503, _M0L8new__capS504);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS507,
  int32_t _M0L8requiredS509
) {
  int32_t _M0L8old__capS506;
  int32_t _M0L3lenS1654;
  int32_t _M0L8new__capS508;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS506 = _M0MPC15array5Array8capacityGfE(_M0L4selfS507);
  _M0L3lenS1654 = _M0L4selfS507->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS508
  = _M0FPB23array__growth__capacity(_M0L8old__capS506, _M0L3lenS1654, _M0L8requiredS509);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS507, _M0L8new__capS508);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS471,
  int32_t _M0L13new__capacityS474
) {
  moonbit_string_t* _M0L8old__bufS470;
  int32_t _M0L3lenS472;
  int32_t _M0L9copy__lenS473;
  moonbit_string_t* _M0L8new__bufS475;
  moonbit_string_t* _M0L6_2aoldS2391;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS470, _M0L13new__capacityS474, _M0L9copy__lenS473, 0, 0);
  _M0L6_2aoldS2391 = _M0L4selfS471->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2391);
  _M0L4selfS471->$0 = _M0L8new__bufS475;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS477,
  int32_t _M0L13new__capacityS480
) {
  struct _M0TUsiE** _M0L8old__bufS476;
  int32_t _M0L3lenS478;
  int32_t _M0L9copy__lenS479;
  struct _M0TUsiE** _M0L8new__bufS481;
  struct _M0TUsiE** _M0L6_2aoldS2392;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS476, _M0L13new__capacityS480, _M0L9copy__lenS479, 0, 0);
  _M0L6_2aoldS2392 = _M0L4selfS477->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2392);
  _M0L4selfS477->$0 = _M0L8new__bufS481;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS483,
  int32_t _M0L13new__capacityS486
) {
  int32_t* _M0L8old__bufS482;
  int32_t _M0L3lenS484;
  int32_t _M0L9copy__lenS485;
  int32_t* _M0L8new__bufS487;
  int32_t* _M0L6_2aoldS2393;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS482, _M0L13new__capacityS486, _M0L9copy__lenS485, 0, 0);
  _M0L6_2aoldS2393 = _M0L4selfS483->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2393);
  _M0L4selfS483->$0 = _M0L8new__bufS487;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS489,
  int32_t _M0L13new__capacityS492
) {
  float* _M0L8old__bufS488;
  int32_t _M0L3lenS490;
  int32_t _M0L9copy__lenS491;
  float* _M0L8new__bufS493;
  float* _M0L6_2aoldS2394;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS488, _M0L13new__capacityS492, _M0L9copy__lenS491, 0, 0);
  _M0L6_2aoldS2394 = _M0L4selfS489->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2394);
  _M0L4selfS489->$0 = _M0L8new__bufS493;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS466
) {
  moonbit_string_t* _M0L6_2atmpS1647;
  int32_t _result_2509;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1647 = _M0MPC15array5Array6bufferGsE(_M0L4selfS466);
  _result_2509 = Moonbit_array_length(_M0L6_2atmpS1647);
  moonbit_decref_cycle_free(_M0L6_2atmpS1647);
  return _result_2509;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS467
) {
  struct _M0TUsiE** _M0L6_2atmpS1648;
  int32_t _result_2510;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1648 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS467);
  _result_2510 = Moonbit_array_length(_M0L6_2atmpS1648);
  moonbit_decref_cycle_free(_M0L6_2atmpS1648);
  return _result_2510;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS468
) {
  int32_t* _M0L6_2atmpS1649;
  int32_t _result_2511;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1649 = _M0MPC15array5Array6bufferGiE(_M0L4selfS468);
  _result_2511 = Moonbit_array_length(_M0L6_2atmpS1649);
  moonbit_decref_cycle_free(_M0L6_2atmpS1649);
  return _result_2511;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS469
) {
  float* _M0L6_2atmpS1650;
  int32_t _result_2512;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1650 = _M0MPC15array5Array6bufferGfE(_M0L4selfS469);
  _result_2512 = Moonbit_array_length(_M0L6_2atmpS1650);
  moonbit_decref_cycle_free(_M0L6_2atmpS1650);
  return _result_2512;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
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

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS458) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS458->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS2395;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2395 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2395);
  return _M0L8_2afieldS2395;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS453) {
  uint8_t* _M0L8_2afieldS2396;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2396 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2396);
  return _M0L8_2afieldS2396;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS454
) {
  moonbit_string_t* _M0L8_2afieldS2397;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2397 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2397);
  return _M0L8_2afieldS2397;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS455
) {
  struct _M0TUsiE** _M0L8_2afieldS2398;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2398 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2398);
  return _M0L8_2afieldS2398;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS456) {
  int32_t* _M0L8_2afieldS2399;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2399 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2399);
  return _M0L8_2afieldS2399;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS457
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2400;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2400 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2400);
  return _M0L8_2afieldS2400;
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
  int32_t _M0L3endS1645;
  int32_t _M0L5startS1646;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1644;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1637;
  int32_t _M0L6_2atmpS1636;
  int32_t _if__result_2514;
  uint16_t* _M0L4dataS1638;
  int32_t _M0L3lenS1639;
  moonbit_string_t _M0L6_2atmpS1640;
  int32_t _M0L6_2atmpS1641;
  int32_t _M0L3lenS1643;
  int32_t _M0L6_2atmpS1642;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1645 = _M0L3strS448.$2;
  _M0L5startS1646 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1645 - _M0L5startS1646;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1644 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1644 + _M0L8str__lenS447;
  _M0L4dataS1637 = _M0L4selfS450->$0;
  _M0L6_2atmpS1636 = Moonbit_array_length(_M0L4dataS1637);
  if (_M0L8requiredS449 > _M0L6_2atmpS1636) {
    _if__result_2514 = 1;
  } else {
    int32_t _M0L3lenS1635 = _M0L4selfS450->$1;
    _if__result_2514 = _M0L8requiredS449 < _M0L3lenS1635;
  }
  if (_if__result_2514) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1638 = _M0L4selfS450->$0;
  _M0L3lenS1639 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1638);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1640 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1641 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1638, _M0L3lenS1639, _M0L6_2atmpS1640, _M0L6_2atmpS1641, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1638);
  moonbit_decref_cycle_free(_M0L6_2atmpS1640);
  _M0L3lenS1643 = _M0L4selfS450->$1;
  _M0L6_2atmpS1642 = _M0L3lenS1643 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1642;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2515;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1634;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1633;
  moonbit_string_t _result_2516;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1632 = Moonbit_array_length(_M0L3strS444);
    _if__result_2515 = _M0L3endS443 == _M0L6_2atmpS1632;
  } else {
    _if__result_2515 = 0;
  }
  if (_if__result_2515) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1634 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1634, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1633 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2516
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1633, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1633);
  return _result_2516;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2517;
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
      int32_t _M0L6_2atmpS1631 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2517 = _M0L6_2atmpS1631 <= _M0L3lenS436;
    } else {
      _if__result_2517 = 0;
    }
  } else {
    _if__result_2517 = 0;
  }
  if (_if__result_2517) {
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
  int32_t _M0L6_2atmpS1630;
  int32_t _M0L6_2atmpS1629;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1628;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1630 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1629 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1630;
  _M0L2e1S422 = _M0L6_2atmpS1629 - 1;
  _M0L6_2atmpS1628 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1628 - 1;
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
        int32_t _M0L6_2atmpS1625 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1624 = (int32_t)_M0L6_2atmpS1625;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1624;
        uint32_t _M0L6_2atmpS1620 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1619;
        int32_t _M0L6_2atmpS1621;
        uint32_t _M0L6_2atmpS1623;
        int32_t _M0L6_2atmpS1622;
        int32_t _M0L6_2atmpS1626;
        int32_t _M0L6_2atmpS1627;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1619 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1620);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1619;
        _M0L6_2atmpS1621 = _M0L1jS433 + 1;
        _M0L6_2atmpS1623 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1622 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1623);
        if (
          _M0L6_2atmpS1621 < 0
          || _M0L6_2atmpS1621 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1621] = _M0L6_2atmpS1622;
        _M0L6_2atmpS1626 = _M0L1iS432 + 1;
        _M0L6_2atmpS1627 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1626;
        _M0L1jS433 = _M0L6_2atmpS1627;
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
  int32_t _M0L6_2atmpS1618;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1618 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1618 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_17.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_10.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_17.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_10.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1617 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1617;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1614;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1614 = 1;
      } else {
        _M0L6_2atmpS1614 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1614;
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
      int32_t _M0L6_2atmpS1615;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1615 = 1;
      } else {
        _M0L6_2atmpS1615 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1615;
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
      int32_t _M0L6_2atmpS1616;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1616 = 1;
      } else {
        _M0L6_2atmpS1616 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1616;
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
  int32_t _M0L6_2atmpS1613;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1613 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1613;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1590 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1590;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1589 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1588 = 48 + _M0L6_2atmpS1589;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1588;
      int32_t _M0L6_2atmpS1587 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1586 = 48 + _M0L6_2atmpS1587;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1586;
      int32_t _M0L6_2atmpS1585 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1584 = 48 + _M0L6_2atmpS1585;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1584;
      int32_t _M0L6_2atmpS1583 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1582 = 48 + _M0L6_2atmpS1583;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1582;
      int32_t _M0L6_2atmpS1574 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1573 = _M0L6_2atmpS1574 - 4;
      int32_t _M0L6_2atmpS1576;
      int32_t _M0L6_2atmpS1575;
      int32_t _M0L6_2atmpS1578;
      int32_t _M0L6_2atmpS1577;
      int32_t _M0L6_2atmpS1580;
      int32_t _M0L6_2atmpS1579;
      int32_t _M0L6_2atmpS1581;
      _M0L6bufferS381[_M0L6_2atmpS1573] = _M0L6d1__hiS377;
      _M0L6_2atmpS1576 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1575 = _M0L6_2atmpS1576 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1575] = _M0L6d1__loS378;
      _M0L6_2atmpS1578 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1577 = _M0L6_2atmpS1578 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1577] = _M0L6d2__hiS379;
      _M0L6_2atmpS1580 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1579 = _M0L6_2atmpS1580 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1579] = _M0L6d2__loS380;
      _M0L6_2atmpS1581 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1581;
      continue;
    } else {
      int32_t _M0L6_2atmpS1612 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1612;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1599 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1598 = 48 + _M0L6_2atmpS1599;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1598;
          int32_t _M0L6_2atmpS1597 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1596 = 48 + _M0L6_2atmpS1597;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1596;
          int32_t _M0L6_2atmpS1592 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1591 = _M0L6_2atmpS1592 - 2;
          int32_t _M0L6_2atmpS1594;
          int32_t _M0L6_2atmpS1593;
          int32_t _M0L6_2atmpS1595;
          _M0L6bufferS381[_M0L6_2atmpS1591] = _M0L5d__hiS388;
          _M0L6_2atmpS1594 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1593 = _M0L6_2atmpS1594 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1593] = _M0L5d__loS389;
          _M0L6_2atmpS1595 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1595;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1607 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1606 = 48 + _M0L6_2atmpS1607;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1606;
          int32_t _M0L6_2atmpS1605 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1604 = 48 + _M0L6_2atmpS1605;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1604;
          int32_t _M0L6_2atmpS1601 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1600 = _M0L6_2atmpS1601 - 2;
          int32_t _M0L6_2atmpS1603;
          int32_t _M0L6_2atmpS1602;
          _M0L6bufferS381[_M0L6_2atmpS1600] = _M0L5d__hiS391;
          _M0L6_2atmpS1603 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1602 = _M0L6_2atmpS1603 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1602] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1611 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1608 = _M0L6_2atmpS1611 - 1;
          int32_t _M0L6_2atmpS1610 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1609 = (uint16_t)_M0L6_2atmpS1610;
          _M0L6bufferS381[_M0L6_2atmpS1608] = _M0L6_2atmpS1609;
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
  int32_t _M0L6_2atmpS1558;
  int32_t _M0L6_2atmpS1557;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1558 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1557 = _M0L5radixS355 & _M0L6_2atmpS1558;
  if (_M0L6_2atmpS1557 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1565;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1565 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1565;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1564 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1564;
        int32_t _M0L6_2atmpS1561 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1559 = _M0L6_2atmpS1561 - 1;
        int32_t _M0L6_2atmpS1560 =
          ((moonbit_string_t)moonbit_string_literal_18.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1562;
        uint64_t _M0L6_2atmpS1563;
        _M0L6bufferS361[_M0L6_2atmpS1559] = _M0L6_2atmpS1560;
        _M0L6_2atmpS1562 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1563 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1562;
        _M0L1nS359 = _M0L6_2atmpS1563;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1572 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1572;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1571 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1570 = _M0L1nS367 - _M0L6_2atmpS1571;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1570;
        int32_t _M0L6_2atmpS1568 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1566 = _M0L6_2atmpS1568 - 1;
        int32_t _M0L6_2atmpS1567 =
          ((moonbit_string_t)moonbit_string_literal_18.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1569;
        _M0L6bufferS361[_M0L6_2atmpS1566] = _M0L6_2atmpS1567;
        _M0L6_2atmpS1569 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1569;
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
  int32_t _M0L6_2atmpS1556;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1556 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1556;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1553 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1553;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1547 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1545 = _M0L6_2atmpS1547 - 2;
      int32_t _M0L6_2atmpS1546 =
        ((moonbit_string_t)moonbit_string_literal_18.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1550;
      int32_t _M0L6_2atmpS1548;
      int32_t _M0L6_2atmpS1549;
      int32_t _M0L6_2atmpS1551;
      uint64_t _M0L6_2atmpS1552;
      _M0L6bufferS348[_M0L6_2atmpS1545] = _M0L6_2atmpS1546;
      _M0L6_2atmpS1550 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1548 = _M0L6_2atmpS1550 - 1;
      _M0L6_2atmpS1549
      = ((moonbit_string_t)moonbit_string_literal_18.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1548] = _M0L6_2atmpS1549;
      _M0L6_2atmpS1551 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1552 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1551;
      _M0L1nS344 = _M0L6_2atmpS1552;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1555 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1555;
      int32_t _M0L6_2atmpS1554 =
        ((moonbit_string_t)moonbit_string_literal_18.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1554;
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
      uint64_t _M0L6_2atmpS1543 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1544 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1543;
      _M0L5countS341 = _M0L6_2atmpS1544;
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
    int32_t _M0L6_2atmpS1542;
    int32_t _M0L6_2atmpS1541;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1542 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1541 = _M0L6_2atmpS1542 / 4;
    return _M0L6_2atmpS1541 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_17.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_10.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1540 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1540;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1537;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1537 = 1;
      } else {
        _M0L6_2atmpS1537 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1537;
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
      int32_t _M0L6_2atmpS1538;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1538 = 1;
      } else {
        _M0L6_2atmpS1538 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1538;
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
      int32_t _M0L6_2atmpS1539;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1539 = 1;
      } else {
        _M0L6_2atmpS1539 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1539;
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
      uint32_t _M0L6_2atmpS1535 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1536 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1535;
      _M0L5countS315 = _M0L6_2atmpS1536;
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
    int32_t _M0L6_2atmpS1534;
    int32_t _M0L6_2atmpS1533;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1534 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1533 = _M0L6_2atmpS1534 / 4;
    return _M0L6_2atmpS1533 + 1;
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
  int32_t _M0L6_2atmpS1532;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1532 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1532;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1509 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1509;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1508 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1507 = 48 + _M0L6_2atmpS1508;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1507;
      int32_t _M0L6_2atmpS1506 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1505 = 48 + _M0L6_2atmpS1506;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1505;
      int32_t _M0L6_2atmpS1504 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1503 = 48 + _M0L6_2atmpS1504;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1503;
      int32_t _M0L6_2atmpS1502 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1501 = 48 + _M0L6_2atmpS1502;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1501;
      int32_t _M0L6_2atmpS1493 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1492 = _M0L6_2atmpS1493 - 4;
      int32_t _M0L6_2atmpS1495;
      int32_t _M0L6_2atmpS1494;
      int32_t _M0L6_2atmpS1497;
      int32_t _M0L6_2atmpS1496;
      int32_t _M0L6_2atmpS1499;
      int32_t _M0L6_2atmpS1498;
      int32_t _M0L6_2atmpS1500;
      _M0L6bufferS294[_M0L6_2atmpS1492] = _M0L6d1__hiS290;
      _M0L6_2atmpS1495 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1494 = _M0L6_2atmpS1495 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1494] = _M0L6d1__loS291;
      _M0L6_2atmpS1497 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1496 = _M0L6_2atmpS1497 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1496] = _M0L6d2__hiS292;
      _M0L6_2atmpS1499 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1498 = _M0L6_2atmpS1499 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1498] = _M0L6d2__loS293;
      _M0L6_2atmpS1500 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1500;
      continue;
    } else {
      int32_t _M0L6_2atmpS1531 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1531;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1518 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1517 = 48 + _M0L6_2atmpS1518;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1517;
          int32_t _M0L6_2atmpS1516 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1515 = 48 + _M0L6_2atmpS1516;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1515;
          int32_t _M0L6_2atmpS1511 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1510 = _M0L6_2atmpS1511 - 2;
          int32_t _M0L6_2atmpS1513;
          int32_t _M0L6_2atmpS1512;
          int32_t _M0L6_2atmpS1514;
          _M0L6bufferS294[_M0L6_2atmpS1510] = _M0L5d__hiS301;
          _M0L6_2atmpS1513 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1512 = _M0L6_2atmpS1513 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1512] = _M0L5d__loS302;
          _M0L6_2atmpS1514 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1514;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1526 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1525 = 48 + _M0L6_2atmpS1526;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1525;
          int32_t _M0L6_2atmpS1524 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1523 = 48 + _M0L6_2atmpS1524;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1523;
          int32_t _M0L6_2atmpS1520 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1519 = _M0L6_2atmpS1520 - 2;
          int32_t _M0L6_2atmpS1522;
          int32_t _M0L6_2atmpS1521;
          _M0L6bufferS294[_M0L6_2atmpS1519] = _M0L5d__hiS304;
          _M0L6_2atmpS1522 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1521 = _M0L6_2atmpS1522 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1521] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1530 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1527 = _M0L6_2atmpS1530 - 1;
          int32_t _M0L6_2atmpS1529 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1528 = (uint16_t)_M0L6_2atmpS1529;
          _M0L6bufferS294[_M0L6_2atmpS1527] = _M0L6_2atmpS1528;
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
  int32_t _M0L6_2atmpS1477;
  int32_t _M0L6_2atmpS1476;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1477 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1476 = _M0L5radixS268 & _M0L6_2atmpS1477;
  if (_M0L6_2atmpS1476 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1484;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1484 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1484;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1483 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1483;
        int32_t _M0L6_2atmpS1480 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1478 = _M0L6_2atmpS1480 - 1;
        int32_t _M0L6_2atmpS1479 =
          ((moonbit_string_t)moonbit_string_literal_18.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1481;
        uint32_t _M0L6_2atmpS1482;
        _M0L6bufferS274[_M0L6_2atmpS1478] = _M0L6_2atmpS1479;
        _M0L6_2atmpS1481 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1482 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1481;
        _M0L1nS272 = _M0L6_2atmpS1482;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1491 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1491;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1490 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1489 = _M0L1nS280 - _M0L6_2atmpS1490;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1489;
        int32_t _M0L6_2atmpS1487 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1485 = _M0L6_2atmpS1487 - 1;
        int32_t _M0L6_2atmpS1486 =
          ((moonbit_string_t)moonbit_string_literal_18.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1488;
        _M0L6bufferS274[_M0L6_2atmpS1485] = _M0L6_2atmpS1486;
        _M0L6_2atmpS1488 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1488;
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
  int32_t _M0L6_2atmpS1475;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1475 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1475;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1472 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1472;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1466 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1464 = _M0L6_2atmpS1466 - 2;
      int32_t _M0L6_2atmpS1465 =
        ((moonbit_string_t)moonbit_string_literal_18.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1469;
      int32_t _M0L6_2atmpS1467;
      int32_t _M0L6_2atmpS1468;
      int32_t _M0L6_2atmpS1470;
      uint32_t _M0L6_2atmpS1471;
      _M0L6bufferS261[_M0L6_2atmpS1464] = _M0L6_2atmpS1465;
      _M0L6_2atmpS1469 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1467 = _M0L6_2atmpS1469 - 1;
      _M0L6_2atmpS1468
      = ((moonbit_string_t)moonbit_string_literal_18.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1467] = _M0L6_2atmpS1468;
      _M0L6_2atmpS1470 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1471 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1470;
      _M0L1nS257 = _M0L6_2atmpS1471;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1474 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1474;
      int32_t _M0L6_2atmpS1473 =
        ((moonbit_string_t)moonbit_string_literal_18.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1473;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1463;
  moonbit_string_t _result_2531;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1463
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1463);
  if (_M0L6_2atmpS1463.$1) {
    moonbit_decref(_M0L6_2atmpS1463.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2531 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2531;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1460;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1460 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1460);
  moonbit_decref_cycle_free(_M0L6_2atmpS1460);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1461;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1461 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1461);
  moonbit_decref_cycle_free(_M0L6_2atmpS1461);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1462;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1462 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1462);
  moonbit_decref_cycle_free(_M0L6_2atmpS1462);
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
  moonbit_string_t _M0L8_2afieldS2401;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2401 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2401);
  return _M0L8_2afieldS2401;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1459;
  int64_t _M0L6_2atmpS1458;
  struct _M0TPC16string10StringView _M0L6_2atmpS1457;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1459 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1458 = (int64_t)_M0L6_2atmpS1459;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1457
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1458);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1457);
  moonbit_decref_cycle_free(_M0L6_2atmpS1457.$0);
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
  int32_t _M0L6_2atmpS1441;
  int32_t _if__result_2532;
  int32_t _M0L6_2atmpS1449;
  int32_t _if__result_2533;
  int32_t _M0L6_2atmpS1451;
  int32_t _M0L6_2atmpS1452;
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
  _M0L6_2atmpS1441 = _M0Lm2loS236;
  if (_M0L6_2atmpS1441 > 0) {
    int32_t _M0L6_2atmpS1440 = _M0Lm2loS236;
    if (_M0L6_2atmpS1440 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1439 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1438 = _M0L4selfS235[_M0L6_2atmpS1439];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1438)) {
        int32_t _M0L6_2atmpS1437 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1436 = _M0L6_2atmpS1437 - 1;
        int32_t _M0L6_2atmpS1435 = _M0L4selfS235[_M0L6_2atmpS1436];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2532
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1435);
      } else {
        _if__result_2532 = 0;
      }
    } else {
      _if__result_2532 = 0;
    }
  } else {
    _if__result_2532 = 0;
  }
  if (_if__result_2532) {
    int32_t _M0L6_2atmpS1442 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1442 + 1;
  }
  _M0L6_2atmpS1449 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1449 > 0) {
    int32_t _M0L6_2atmpS1448 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1448 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1447 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1446 = _M0L4selfS235[_M0L6_2atmpS1447];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1446)) {
        int32_t _M0L6_2atmpS1445 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1444 = _M0L6_2atmpS1445 - 1;
        int32_t _M0L6_2atmpS1443 = _M0L4selfS235[_M0L6_2atmpS1444];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2533
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1443);
      } else {
        _if__result_2533 = 0;
      }
    } else {
      _if__result_2533 = 0;
    }
  } else {
    _if__result_2533 = 0;
  }
  if (_if__result_2533) {
    int32_t _M0L6_2atmpS1450 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1450 - 1;
  }
  _M0L6_2atmpS1451 = _M0Lm2loS236;
  _M0L6_2atmpS1452 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1451 >= _M0L6_2atmpS1452) {
    int32_t _M0L6_2atmpS1453 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1454 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1453,
                                                 .$2 = _M0L6_2atmpS1454};
  } else {
    int32_t _M0L6_2atmpS1455 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1456 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1455,
                                                 .$2 = _M0L6_2atmpS1456};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1434;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1434
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1434);
  if (_M0L6_2atmpS1434.$1) {
    moonbit_decref(_M0L6_2atmpS1434.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1433;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1433
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1433);
  if (_M0L6_2atmpS1433.$1) {
    moonbit_decref(_M0L6_2atmpS1433.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1432;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1432 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1432;
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
  int32_t _M0L6_2atmpS1431;
  struct _M0TPC16string10StringView _M0L6_2atmpS1429;
  struct _M0TPB6Logger _M0L6_2atmpS1430;
  moonbit_string_t _result_2534;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1431 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1429
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1431
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1430
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1429, _M0L6_2atmpS1430, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1429.$0);
  if (_M0L6_2atmpS1430.$1) {
    moonbit_decref(_M0L6_2atmpS1430.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2534 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2534;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1427;
  int32_t _M0L5startS1428;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1427 = _M0L4selfS218.$2;
  _M0L5startS1428 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1427 - _M0L5startS1428;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1424;
    int32_t _M0L5startS1426;
    int32_t _M0L6_2atmpS1425;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1408;
    int32_t _M0L6_2atmpS1409;
    int32_t _M0L6_2atmpS1410;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1424 = _M0L4selfS218.$0;
    _M0L5startS1426 = _M0L4selfS218.$1;
    _M0L6_2atmpS1425 = _M0L5startS1426 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1424[_M0L6_2atmpS1425];
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
        int32_t _M0L6_2atmpS1411;
        int32_t _M0L6_2atmpS1412;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1411 = _M0L1iS220 + 1;
        _M0L6_2atmpS1412 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1411;
        _M0L3segS221 = _M0L6_2atmpS1412;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1413;
        int32_t _M0L6_2atmpS1414;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1413 = _M0L1iS220 + 1;
        _M0L6_2atmpS1414 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1413;
        _M0L3segS221 = _M0L6_2atmpS1414;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1415;
        int32_t _M0L6_2atmpS1416;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1415 = _M0L1iS220 + 1;
        _M0L6_2atmpS1416 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1415;
        _M0L3segS221 = _M0L6_2atmpS1416;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1417;
        int32_t _M0L6_2atmpS1418;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1417 = _M0L1iS220 + 1;
        _M0L6_2atmpS1418 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1417;
        _M0L3segS221 = _M0L6_2atmpS1418;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1420;
          moonbit_string_t _M0L6_2atmpS1419;
          int32_t _M0L6_2atmpS1421;
          int32_t _M0L6_2atmpS1422;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
          _M0L6_2atmpS1420 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1419 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1420);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1419);
          moonbit_decref_cycle_free(_M0L6_2atmpS1419);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1421 = _M0L1iS220 + 1;
          _M0L6_2atmpS1422 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1421;
          _M0L3segS221 = _M0L6_2atmpS1422;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1423 = _M0L1iS220 + 1;
          int32_t _tmp_2537 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1423;
          _M0L3segS221 = _tmp_2537;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2536;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1408 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1408);
    _M0L6_2atmpS1409 = _M0L1iS220 + 1;
    _M0L6_2atmpS1410 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1409;
    _M0L3segS221 = _M0L6_2atmpS1410;
    continue;
    joinlet_2536:;
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
    int64_t _M0L6_2atmpS1407 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1406;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1406
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1407);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1406);
    moonbit_decref_cycle_free(_M0L6_2atmpS1406.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1404;
  int32_t _M0L5startS1405;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1382;
  int32_t _if__result_2538;
  int32_t _M0L6_2atmpS1392;
  int32_t _if__result_2539;
  int32_t _M0L6_2atmpS1394;
  int32_t _M0L6_2atmpS1395;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1404 = _M0L4selfS201.$2;
  _M0L5startS1405 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1404 - _M0L5startS1405;
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
  _M0L6_2atmpS1382 = _M0Lm2loS202;
  if (_M0L6_2atmpS1382 > 0) {
    int32_t _M0L6_2atmpS1381 = _M0Lm2loS202;
    if (_M0L6_2atmpS1381 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1380 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1379 = _M0L4baseS209 + _M0L6_2atmpS1380;
      int32_t _M0L6_2atmpS1378 = _M0L3strS208[_M0L6_2atmpS1379];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1378)) {
        int32_t _M0L6_2atmpS1377 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1376 = _M0L4baseS209 + _M0L6_2atmpS1377;
        int32_t _M0L6_2atmpS1375 = _M0L6_2atmpS1376 - 1;
        int32_t _M0L6_2atmpS1374 = _M0L3strS208[_M0L6_2atmpS1375];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2538
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1374);
      } else {
        _if__result_2538 = 0;
      }
    } else {
      _if__result_2538 = 0;
    }
  } else {
    _if__result_2538 = 0;
  }
  if (_if__result_2538) {
    int32_t _M0L6_2atmpS1383 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1383 + 1;
  }
  _M0L6_2atmpS1392 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1392 > 0) {
    int32_t _M0L6_2atmpS1391 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1391 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1390 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1389 = _M0L4baseS209 + _M0L6_2atmpS1390;
      int32_t _M0L6_2atmpS1388 = _M0L3strS208[_M0L6_2atmpS1389];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1388)) {
        int32_t _M0L6_2atmpS1387 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1386 = _M0L4baseS209 + _M0L6_2atmpS1387;
        int32_t _M0L6_2atmpS1385 = _M0L6_2atmpS1386 - 1;
        int32_t _M0L6_2atmpS1384 = _M0L3strS208[_M0L6_2atmpS1385];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2539
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1384);
      } else {
        _if__result_2539 = 0;
      }
    } else {
      _if__result_2539 = 0;
    }
  } else {
    _if__result_2539 = 0;
  }
  if (_if__result_2539) {
    int32_t _M0L6_2atmpS1393 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1393 - 1;
  }
  _M0L6_2atmpS1394 = _M0Lm2loS202;
  _M0L6_2atmpS1395 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1394 >= _M0L6_2atmpS1395) {
    int32_t _M0L6_2atmpS1399 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1396 = _M0L4baseS209 + _M0L6_2atmpS1399;
    int32_t _M0L6_2atmpS1398 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1397 = _M0L4baseS209 + _M0L6_2atmpS1398;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1396,
                                                 .$2 = _M0L6_2atmpS1397};
  } else {
    int32_t _M0L6_2atmpS1403 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1400 = _M0L4baseS209 + _M0L6_2atmpS1403;
    int32_t _M0L6_2atmpS1402 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1401 = _M0L4baseS209 + _M0L6_2atmpS1402;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1400,
                                                 .$2 = _M0L6_2atmpS1401};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1371;
  int32_t _M0L6_2atmpS1370;
  int32_t _M0L6_2atmpS1373;
  int32_t _M0L6_2atmpS1372;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1369;
  moonbit_string_t _result_2540;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1371 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1370
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1371);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1370);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1373 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1372
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1373);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1372);
  _M0L6_2atmpS1369 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2540 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1369);
  moonbit_decref_cycle_free(_M0L6_2atmpS1369);
  return _result_2540;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1366;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1366 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1366);
  } else {
    int32_t _M0L6_2atmpS1368;
    int32_t _M0L6_2atmpS1367;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1368 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1367 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1368, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1367);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1364;
  int32_t _M0L6_2atmpS1365;
  int32_t _M0L6_2atmpS1363;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1364 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1365 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1363 = _M0L6_2atmpS1364 - _M0L6_2atmpS1365;
  return _M0L6_2atmpS1363 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1361;
  int32_t _M0L6_2atmpS1362;
  int32_t _M0L6_2atmpS1360;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1361 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1362 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1360 = _M0L6_2atmpS1361 % _M0L6_2atmpS1362;
  return _M0L6_2atmpS1360 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1358;
  int32_t _M0L6_2atmpS1359;
  int32_t _M0L6_2atmpS1357;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1358 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1359 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1357 = _M0L6_2atmpS1358 / _M0L6_2atmpS1359;
  return _M0L6_2atmpS1357 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1355;
  int32_t _M0L6_2atmpS1356;
  int32_t _M0L6_2atmpS1354;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1355 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1356 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1354 = _M0L6_2atmpS1355 + _M0L6_2atmpS1356;
  return _M0L6_2atmpS1354 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1353;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1353 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1353;
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
  int32_t _M0L3lenS1352;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1347;
  int32_t _M0L6_2atmpS1346;
  int32_t _if__result_2541;
  uint16_t* _M0L4dataS1348;
  int32_t _M0L3lenS1349;
  int32_t _M0L3lenS1351;
  int32_t _M0L6_2atmpS1350;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1352 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1352 + _M0L8str__lenS182;
  _M0L4dataS1347 = _M0L4selfS185->$0;
  _M0L6_2atmpS1346 = Moonbit_array_length(_M0L4dataS1347);
  if (_M0L8requiredS184 > _M0L6_2atmpS1346) {
    _if__result_2541 = 1;
  } else {
    int32_t _M0L3lenS1345 = _M0L4selfS185->$1;
    _if__result_2541 = _M0L8requiredS184 < _M0L3lenS1345;
  }
  if (_if__result_2541) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1348 = _M0L4selfS185->$0;
  _M0L3lenS1349 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1348);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1348, _M0L3lenS1349, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1348);
  _M0L3lenS1351 = _M0L4selfS185->$1;
  _M0L6_2atmpS1350 = _M0L3lenS1351 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1350;
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
      int32_t _M0L6_2atmpS1342 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1343;
      int32_t _M0L6_2atmpS1344;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1342;
      _M0L6_2atmpS1343 = _M0L1iS176 + 1;
      _M0L6_2atmpS1344 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1343;
      _M0L1jS177 = _M0L6_2atmpS1344;
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
    int32_t _M0L3lenS1313 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1315 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1314 = Moonbit_array_length(_M0L4dataS1315);
    uint16_t* _M0L4dataS1318;
    int32_t _M0L3lenS1319;
    int32_t _M0L6_2atmpS1320;
    int32_t _M0L3lenS1322;
    int32_t _M0L6_2atmpS1321;
    if (_M0L3lenS1313 >= _M0L6_2atmpS1314) {
      int32_t _M0L3lenS1317 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1316 = _M0L3lenS1317 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1316);
    }
    _M0L4dataS1318 = _M0L4selfS171->$0;
    _M0L3lenS1319 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1318);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1320 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1319 < 0
      || _M0L3lenS1319 >= Moonbit_array_length(_M0L4dataS1318)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1318[_M0L3lenS1319] = _M0L6_2atmpS1320;
    moonbit_decref_cycle_free(_M0L4dataS1318);
    _M0L3lenS1322 = _M0L4selfS171->$1;
    _M0L6_2atmpS1321 = _M0L3lenS1322 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1321;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1326 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1324 = Moonbit_array_length(_M0L4dataS1326);
    int32_t _M0L3lenS1325 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1323 = _M0L6_2atmpS1324 - _M0L3lenS1325;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1329;
    int32_t _M0L3lenS1330;
    uint32_t _M0L6_2atmpS1333;
    uint32_t _M0L6_2atmpS1332;
    int32_t _M0L6_2atmpS1331;
    uint16_t* _M0L4dataS1334;
    int32_t _M0L3lenS1339;
    int32_t _M0L6_2atmpS1335;
    uint32_t _M0L6_2atmpS1338;
    uint32_t _M0L6_2atmpS1337;
    int32_t _M0L6_2atmpS1336;
    int32_t _M0L3lenS1341;
    int32_t _M0L6_2atmpS1340;
    if (_M0L6_2atmpS1323 < 2) {
      int32_t _M0L3lenS1328 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1327 = _M0L3lenS1328 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1327);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1329 = _M0L4selfS171->$0;
    _M0L3lenS1330 = _M0L4selfS171->$1;
    _M0L6_2atmpS1333 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1332 = 55296u + _M0L6_2atmpS1333;
    moonbit_incref_cycle_free(_M0L4dataS1329);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1331 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1332);
    if (
      _M0L3lenS1330 < 0
      || _M0L3lenS1330 >= Moonbit_array_length(_M0L4dataS1329)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1329[_M0L3lenS1330] = _M0L6_2atmpS1331;
    moonbit_decref_cycle_free(_M0L4dataS1329);
    _M0L4dataS1334 = _M0L4selfS171->$0;
    _M0L3lenS1339 = _M0L4selfS171->$1;
    _M0L6_2atmpS1335 = _M0L3lenS1339 + 1;
    _M0L6_2atmpS1338 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1337 = 56320u + _M0L6_2atmpS1338;
    moonbit_incref_cycle_free(_M0L4dataS1334);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1336 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1337);
    if (
      _M0L6_2atmpS1335 < 0
      || _M0L6_2atmpS1335 >= Moonbit_array_length(_M0L4dataS1334)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1334[_M0L6_2atmpS1335] = _M0L6_2atmpS1336;
    moonbit_decref_cycle_free(_M0L4dataS1334);
    _M0L3lenS1341 = _M0L4selfS171->$1;
    _M0L6_2atmpS1340 = _M0L3lenS1341 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1340;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1312;
  int32_t _M0L6_2atmpS1310;
  int32_t _M0L3lenS1311;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1307;
  int32_t _M0L6_2atmpS1308;
  int32_t _M0L3lenS1309;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2402;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1312 = _M0L4selfS166->$0;
  _M0L6_2atmpS1310 = Moonbit_array_length(_M0L4dataS1312);
  _M0L3lenS1311 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1310, _M0L3lenS1311, _M0L8requiredS167);
  _M0L4dataS1307 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1307);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1308 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1309 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1307, _M0L13new__capacityS165, _M0L6_2atmpS1308, _M0L3lenS1309, 0, 0);
  _M0L6_2aoldS2402 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2402);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_25.data);
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
  int32_t _M0L6_2atmpS1306;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1306 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1306;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1305;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1305 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1305;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1296;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1296 = _M0L4selfS155->$1;
  if (_M0L3lenS1296 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1297 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1299 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1298 = Moonbit_array_length(_M0L4dataS1299);
    if (_M0L3lenS1297 == _M0L6_2atmpS1298) {
      uint16_t* _M0L4dataS1300 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1300);
      return _M0L4dataS1300;
    } else {
      uint16_t* _M0L4dataS1301 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1302 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1303;
      int32_t _M0L3lenS1304;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1301);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1303 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1304 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1301, _M0L3lenS1302, _M0L6_2atmpS1303, _M0L3lenS1304, 0, 0);
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
  int32_t _if__result_2544;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1292 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1293 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1292 <= _M0L6_2atmpS1293) {
            int32_t _M0L6_2atmpS1291 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2544 = _M0L6_2atmpS1291 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2544 = 0;
          }
        } else {
          _if__result_2544 = 0;
        }
      } else {
        _if__result_2544 = 0;
      }
    } else {
      _if__result_2544 = 0;
    }
  } else {
    _if__result_2544 = 0;
  }
  if (_if__result_2544) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1295;
    moonbit_string_t _M0L6_2atmpS1294;
    uint16_t* _result_2545;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_30.data);
    _M0L6_2atmpS1295 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1295);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1294
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2545 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1294);
    moonbit_decref_cycle_free(_M0L6_2atmpS1294);
    return _result_2545;
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
  struct _M0TPB13StringBuilder* _block_2546;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1290 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1290 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2546
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2546)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 64, 0);
  _block_2546->$0 = _M0L4dataS140;
  _block_2546->$1 = 0;
  return _block_2546;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1289;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1289 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1289;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2547;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1270 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1271;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1271
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS1270 <= _M0L6_2atmpS1271) {
            int32_t _M0L6_2atmpS1269 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2547 = _M0L6_2atmpS1269 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2547 = 0;
          }
        } else {
          _if__result_2547 = 0;
        }
      } else {
        _if__result_2547 = 0;
      }
    } else {
      _if__result_2547 = 0;
    }
  } else {
    _if__result_2547 = 0;
  }
  if (_if__result_2547) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1273;
    moonbit_string_t _M0L6_2atmpS1272;
    moonbit_string_t* _result_2548;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1273 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1273);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1272
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2548
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1272);
    moonbit_decref_cycle_free(_M0L6_2atmpS1272);
    return _result_2548;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2549;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1275 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1276;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1276
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS1275 <= _M0L6_2atmpS1276) {
            int32_t _M0L6_2atmpS1274 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2549 = _M0L6_2atmpS1274 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2549 = 0;
          }
        } else {
          _if__result_2549 = 0;
        }
      } else {
        _if__result_2549 = 0;
      }
    } else {
      _if__result_2549 = 0;
    }
  } else {
    _if__result_2549 = 0;
  }
  if (_if__result_2549) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1278;
    moonbit_string_t _M0L6_2atmpS1277;
    struct _M0TUsiE** _result_2550;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1278 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1278);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1277
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2550
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1277);
    moonbit_decref_cycle_free(_M0L6_2atmpS1277);
    return _result_2550;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2551;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1280 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1281;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1281
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
          if (_M0L6_2atmpS1280 <= _M0L6_2atmpS1281) {
            int32_t _M0L6_2atmpS1279 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2551 = _M0L6_2atmpS1279 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2551 = 0;
          }
        } else {
          _if__result_2551 = 0;
        }
      } else {
        _if__result_2551 = 0;
      }
    } else {
      _if__result_2551 = 0;
    }
  } else {
    _if__result_2551 = 0;
  }
  if (_if__result_2551) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1283;
    moonbit_string_t _M0L6_2atmpS1282;
    int32_t* _result_2552;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1283 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1283);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1282
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2552
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1282);
    moonbit_decref_cycle_free(_M0L6_2atmpS1282);
    return _result_2552;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2553;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1285 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1286;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1286
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS1285 <= _M0L6_2atmpS1286) {
            int32_t _M0L6_2atmpS1284 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2553 = _M0L6_2atmpS1284 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2553 = 0;
          }
        } else {
          _if__result_2553 = 0;
        }
      } else {
        _if__result_2553 = 0;
      }
    } else {
      _if__result_2553 = 0;
    }
  } else {
    _if__result_2553 = 0;
  }
  if (_if__result_2553) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1288;
    moonbit_string_t _M0L6_2atmpS1287;
    float* _result_2554;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1288 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1288);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1287
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2554
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1287);
    moonbit_decref_cycle_free(_M0L6_2atmpS1287);
    return _result_2554;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1266;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1266
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1266);
  if (_M0L6_2atmpS1266.$1) {
    moonbit_decref(_M0L6_2atmpS1266.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1267;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1267
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1267);
  if (_M0L6_2atmpS1267.$1) {
    moonbit_decref(_M0L6_2atmpS1267.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1268;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1268
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1268);
  if (_M0L6_2atmpS1268.$1) {
    moonbit_decref(_M0L6_2atmpS1268.$1);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS63,
  int32_t _M0L11dst__offsetS64,
  moonbit_string_t* _M0L3srcS65,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  struct _M0TUsiE** _M0L3srcS70,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  int32_t* _M0L3srcS75,
  int32_t _M0L11src__offsetS76,
  int32_t _M0L3lenS77
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS75);
  moonbit_incref_cycle_free(_M0L3dstS73);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS73, _M0L11dst__offsetS74, _M0L3srcS75, _M0L11src__offsetS76, _M0L3lenS77, sizeof(int32_t));
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
        int32_t _M0L6_2atmpS1221 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1223 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1222;
        int32_t _M0L6_2atmpS1224;
        if (
          _M0L6_2atmpS1223 < 0
          || _M0L6_2atmpS1223 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1222 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1223];
        if (
          _M0L6_2atmpS1221 < 0
          || _M0L6_2atmpS1221 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1221] = _M0L6_2atmpS1222;
        _M0L6_2atmpS1224 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1224;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1229 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1229;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1225 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1227 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1226;
        int32_t _M0L6_2atmpS1228;
        if (
          _M0L6_2atmpS1227 < 0
          || _M0L6_2atmpS1227 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1226 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1227];
        if (
          _M0L6_2atmpS1225 < 0
          || _M0L6_2atmpS1225 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1225] = _M0L6_2atmpS1226;
        _M0L6_2atmpS1228 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1228;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS27,
  int32_t _M0L11dst__offsetS29,
  moonbit_string_t* _M0L3srcS28,
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
        int32_t _M0L6_2atmpS1230 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1232 = _M0L11src__offsetS30 + _M0L1iS31;
        moonbit_string_t _M0L6_2atmpS1231;
        moonbit_string_t _M0L6_2aoldS2403;
        int32_t _M0L6_2atmpS1233;
        if (
          _M0L6_2atmpS1232 < 0
          || _M0L6_2atmpS1232 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1231 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1232];
        if (
          _M0L6_2atmpS1230 < 0
          || _M0L6_2atmpS1230 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2403 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1230];
        moonbit_incref_cycle_free(_M0L6_2atmpS1231);
        moonbit_decref_cycle_free(_M0L6_2aoldS2403);
        _M0L3dstS27[_M0L6_2atmpS1230] = _M0L6_2atmpS1231;
        _M0L6_2atmpS1233 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1233;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1238 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1238;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1234 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1236 = _M0L11src__offsetS30 + _M0L1iS34;
        moonbit_string_t _M0L6_2atmpS1235;
        moonbit_string_t _M0L6_2aoldS2404;
        int32_t _M0L6_2atmpS1237;
        if (
          _M0L6_2atmpS1236 < 0
          || _M0L6_2atmpS1236 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1235 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1236];
        if (
          _M0L6_2atmpS1234 < 0
          || _M0L6_2atmpS1234 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2404 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1234];
        moonbit_incref_cycle_free(_M0L6_2atmpS1235);
        moonbit_decref_cycle_free(_M0L6_2aoldS2404);
        _M0L3dstS27[_M0L6_2atmpS1234] = _M0L6_2atmpS1235;
        _M0L6_2atmpS1237 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1237;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  struct _M0TUsiE** _M0L3srcS37,
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
        int32_t _M0L6_2atmpS1239 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1241 = _M0L11src__offsetS39 + _M0L1iS40;
        struct _M0TUsiE* _M0L6_2atmpS1240;
        struct _M0TUsiE* _M0L6_2aoldS2405;
        int32_t _M0L6_2atmpS1242;
        if (
          _M0L6_2atmpS1241 < 0
          || _M0L6_2atmpS1241 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1240 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1241];
        if (
          _M0L6_2atmpS1239 < 0
          || _M0L6_2atmpS1239 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2405 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1239];
        if (_M0L6_2atmpS1240) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1240);
        }
        if (_M0L6_2aoldS2405) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2405);
        }
        _M0L3dstS36[_M0L6_2atmpS1239] = _M0L6_2atmpS1240;
        _M0L6_2atmpS1242 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1242;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1247 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1247;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1243 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1245 = _M0L11src__offsetS39 + _M0L1iS43;
        struct _M0TUsiE* _M0L6_2atmpS1244;
        struct _M0TUsiE* _M0L6_2aoldS2406;
        int32_t _M0L6_2atmpS1246;
        if (
          _M0L6_2atmpS1245 < 0
          || _M0L6_2atmpS1245 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1244 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1245];
        if (
          _M0L6_2atmpS1243 < 0
          || _M0L6_2atmpS1243 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2406 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1243];
        if (_M0L6_2atmpS1244) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1244);
        }
        if (_M0L6_2aoldS2406) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2406);
        }
        _M0L3dstS36[_M0L6_2atmpS1243] = _M0L6_2atmpS1244;
        _M0L6_2atmpS1246 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1246;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  int32_t* _M0L3srcS46,
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
        int32_t _M0L6_2atmpS1248 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1250 = _M0L11src__offsetS48 + _M0L1iS49;
        int32_t _M0L6_2atmpS1249;
        int32_t _M0L6_2atmpS1251;
        if (
          _M0L6_2atmpS1250 < 0
          || _M0L6_2atmpS1250 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1249 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1250];
        if (
          _M0L6_2atmpS1248 < 0
          || _M0L6_2atmpS1248 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1248] = _M0L6_2atmpS1249;
        _M0L6_2atmpS1251 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1251;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1256 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1256;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1252 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1254 = _M0L11src__offsetS48 + _M0L1iS52;
        int32_t _M0L6_2atmpS1253;
        int32_t _M0L6_2atmpS1255;
        if (
          _M0L6_2atmpS1254 < 0
          || _M0L6_2atmpS1254 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1253 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1254];
        if (
          _M0L6_2atmpS1252 < 0
          || _M0L6_2atmpS1252 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1252] = _M0L6_2atmpS1253;
        _M0L6_2atmpS1255 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1255;
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
        int32_t _M0L6_2atmpS1257 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1259 = _M0L11src__offsetS57 + _M0L1iS58;
        float _M0L6_2atmpS1258;
        int32_t _M0L6_2atmpS1260;
        if (
          _M0L6_2atmpS1259 < 0
          || _M0L6_2atmpS1259 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1258 = (float)_M0L3srcS55[_M0L6_2atmpS1259];
        if (
          _M0L6_2atmpS1257 < 0
          || _M0L6_2atmpS1257 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1257] = _M0L6_2atmpS1258;
        _M0L6_2atmpS1260 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1260;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1265 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1265;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1261 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1263 = _M0L11src__offsetS57 + _M0L1iS61;
        float _M0L6_2atmpS1262;
        int32_t _M0L6_2atmpS1264;
        if (
          _M0L6_2atmpS1263 < 0
          || _M0L6_2atmpS1263 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1262 = (float)_M0L3srcS55[_M0L6_2atmpS1263];
        if (
          _M0L6_2atmpS1261 < 0
          || _M0L6_2atmpS1261 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1261] = _M0L6_2atmpS1262;
        _M0L6_2atmpS1264 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1264;
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_31.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_32.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1190) {
  switch (Moonbit_object_tag(_M0L4_2aeS1190)) {
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
      break;
    }
    
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_34.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1190);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1216,
  struct _M0TPB4Show _M0L8_2aparamS1215
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1214 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1216;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1214, _M0L8_2aparamS1215);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1213,
  struct _M0TPB4Show _M0L8_2aparamS1212
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1211 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1213;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1211, _M0L8_2aparamS1212);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1210,
  int32_t _M0L8_2aparamS1209
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1208 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1210;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1208, _M0L8_2aparamS1209);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1207,
  struct _M0TPC16string10StringView _M0L8_2aparamS1206
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1205 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1207;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1205, _M0L8_2aparamS1206);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1204,
  moonbit_string_t _M0L8_2aparamS1201,
  int32_t _M0L8_2aparamS1202,
  int32_t _M0L8_2aparamS1203
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1200 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1204;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1200, _M0L8_2aparamS1201, _M0L8_2aparamS1202, _M0L8_2aparamS1203);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1199,
  moonbit_string_t _M0L8_2aparamS1198
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1197 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1199;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1197, _M0L8_2aparamS1198);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2565 = 9218868437227405311ll;
  int64_t _tmp_2566;
  int64_t _tmp_2567;
  int64_t _tmp_2568;
  int64_t _tmp_2569;
  _M0FPB18double__max__value = *(double*)&_tmp_2565;
  _tmp_2566 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2566;
  _tmp_2567 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2567;
  _tmp_2568 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2568;
  _tmp_2569 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2569;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1220;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1183;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1184;
  int32_t _M0L7_2abindS1185;
  struct _M0TUsiE** _M0L7_2abindS1186;
  int32_t _M0L6_2acntS2411;
  int32_t _M0L2__S1187;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1220
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1183
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1183)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 67, 0);
  _M0L12async__testsS1183->$0 = _M0L6_2atmpS1220;
  _M0L12async__testsS1183->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1184
  = _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1185 = _M0L7_2abindS1184->$1;
  _M0L7_2abindS1186 = _M0L7_2abindS1184->$0;
  _M0L6_2acntS2411
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1184));
  if (_M0L6_2acntS2411 > 1) {
    int32_t _M0L11_2anew__cntS2412 = _M0L6_2acntS2411 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1184), _M0L11_2anew__cntS2412);
    moonbit_incref_cycle_free(_M0L7_2abindS1186);
  } else if (_M0L6_2acntS2411 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1184);
  }
  _M0L2__S1187 = 0;
  while (1) {
    if (_M0L2__S1187 < _M0L7_2abindS1185) {
      struct _M0TUsiE* _M0L3argS1188 =
        (struct _M0TUsiE*)_M0L7_2abindS1186[_M0L2__S1187];
      moonbit_string_t _M0L6_2atmpS1217 = _M0L3argS1188->$0;
      int32_t _M0L6_2atmpS1218 = _M0L3argS1188->$1;
      int32_t _M0L6_2atmpS1219;
      moonbit_incref_cycle_free(_M0L6_2atmpS1217);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples23hh__net__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1183, _M0L6_2atmpS1217, _M0L6_2atmpS1218);
      moonbit_decref_cycle_free(_M0L6_2atmpS1217);
      _M0L6_2atmpS1219 = _M0L2__S1187 + 1;
      _M0L2__S1187 = _M0L6_2atmpS1219;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1186);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples23hh__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23hh__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1183);
  moonbit_decref_cycle_free(_M0L12async__testsS1183);
  moonbit_flush_cycles();
  return 0;
}