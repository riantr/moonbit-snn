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

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TUdiE;

struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt2HH;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TWEu;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

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

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902 {
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

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TPB5ArrayGbE {
  uint8_t* $0;
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

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TPB5ArrayGsE {
  moonbit_string_t* $0;
  int32_t $1;
  
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

struct _M0TPB19MulShiftAll64Result {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  
};

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS914(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS907(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS902(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS879(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S872(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP46RiantR8snn__mbt8examples10hh__neuron7run__hh(
  struct _M0TP26RiantR8snn__mbt2HH*,
  int32_t,
  float
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

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
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

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

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
} const moonbit_string_literal_12 =
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
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 102, 97, 
    108, 115, 101, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[116]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 115, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 104, 104, 95, 110, 101, 117, 114, 
    111, 110, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 
    107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
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
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[114]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 113, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 104, 104, 95, 110, 101, 117, 114, 
    111, 110, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 
    115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
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
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS914$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS914
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[53] =
  {
    sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902)
    / 4, 1,
    offsetof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902, $1)
    / 4
    * 2,
    sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907)
    / 4, 1,
    offsetof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2009
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS935,
  moonbit_string_t _M0L8filenameS904,
  int32_t _M0L5indexS906
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902* _closure_2032;
  struct _M0TWEu* _M0L13handle__startS902;
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907* _closure_2033;
  struct _M0TWssbEu* _M0L14handle__resultS907;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS914;
  void* _M0L11_2atry__errS929;
  struct moonbit_result_0 _tmp_2035;
  int32_t _handle__error__result_2036;
  int32_t _M0L6_2atmpS1997;
  void* _M0L3errS930;
  moonbit_string_t _M0L4nameS932;
  struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS933;
  moonbit_string_t _M0L7_2anameS934;
  int32_t _M0L6_2acntS2026;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS904);
  _closure_2032
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902*)moonbit_malloc(sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902));
  Moonbit_object_header(_closure_2032)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2032->code
  = &_M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS902;
  _closure_2032->$0 = _M0L5indexS906;
  _closure_2032->$1 = _M0L8filenameS904;
  _M0L13handle__startS902 = (struct _M0TWEu*)_closure_2032;
  moonbit_incref_cycle_free(_M0L8filenameS904);
  _closure_2033
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907*)moonbit_malloc(sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907));
  Moonbit_object_header(_closure_2033)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2033->code
  = &_M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS907;
  _closure_2033->$0 = _M0L5indexS906;
  _closure_2033->$1 = _M0L8filenameS904;
  _M0L14handle__resultS907 = (struct _M0TWssbEu*)_closure_2033;
  _M0L17error__to__stringS914
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS914$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2035
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS935, _M0L8filenameS904, _M0L5indexS906, _M0L13handle__startS902, _M0L14handle__resultS907, _M0L17error__to__stringS914);
  if (_tmp_2035.tag) {
    int32_t const _M0L5_2aokS2006 = _tmp_2035.data.ok;
    _handle__error__result_2036 = _M0L5_2aokS2006;
  } else {
    void* const _M0L6_2aerrS2007 = _tmp_2035.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS914);
    moonbit_decref_cycle_free(_M0L13handle__startS902);
    _M0L11_2atry__errS929 = _M0L6_2aerrS2007;
    goto join_928;
  }
  if (_handle__error__result_2036) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS914);
    moonbit_decref_cycle_free(_M0L13handle__startS902);
    _M0L6_2atmpS1997 = 1;
  } else {
    struct moonbit_result_0 _tmp_2037;
    int32_t _handle__error__result_2038;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2037
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS935, _M0L8filenameS904, _M0L5indexS906, _M0L13handle__startS902, _M0L14handle__resultS907, _M0L17error__to__stringS914);
    if (_tmp_2037.tag) {
      int32_t const _M0L5_2aokS2004 = _tmp_2037.data.ok;
      _handle__error__result_2038 = _M0L5_2aokS2004;
    } else {
      void* const _M0L6_2aerrS2005 = _tmp_2037.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS914);
      moonbit_decref_cycle_free(_M0L13handle__startS902);
      _M0L11_2atry__errS929 = _M0L6_2aerrS2005;
      goto join_928;
    }
    if (_handle__error__result_2038) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS914);
      moonbit_decref_cycle_free(_M0L13handle__startS902);
      _M0L6_2atmpS1997 = 1;
    } else {
      struct moonbit_result_0 _tmp_2039;
      int32_t _handle__error__result_2040;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2039
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS935, _M0L8filenameS904, _M0L5indexS906, _M0L13handle__startS902, _M0L14handle__resultS907, _M0L17error__to__stringS914);
      if (_tmp_2039.tag) {
        int32_t const _M0L5_2aokS2002 = _tmp_2039.data.ok;
        _handle__error__result_2040 = _M0L5_2aokS2002;
      } else {
        void* const _M0L6_2aerrS2003 = _tmp_2039.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS914);
        moonbit_decref_cycle_free(_M0L13handle__startS902);
        _M0L11_2atry__errS929 = _M0L6_2aerrS2003;
        goto join_928;
      }
      if (_handle__error__result_2040) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS914);
        moonbit_decref_cycle_free(_M0L13handle__startS902);
        _M0L6_2atmpS1997 = 1;
      } else {
        struct moonbit_result_0 _tmp_2041;
        int32_t _handle__error__result_2042;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2041
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS935, _M0L8filenameS904, _M0L5indexS906, _M0L13handle__startS902, _M0L14handle__resultS907, _M0L17error__to__stringS914);
        if (_tmp_2041.tag) {
          int32_t const _M0L5_2aokS2000 = _tmp_2041.data.ok;
          _handle__error__result_2042 = _M0L5_2aokS2000;
        } else {
          void* const _M0L6_2aerrS2001 = _tmp_2041.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS914);
          moonbit_decref_cycle_free(_M0L13handle__startS902);
          _M0L11_2atry__errS929 = _M0L6_2aerrS2001;
          goto join_928;
        }
        if (_handle__error__result_2042) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS914);
          moonbit_decref_cycle_free(_M0L13handle__startS902);
          _M0L6_2atmpS1997 = 1;
        } else {
          struct moonbit_result_0 _tmp_2043;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2043
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS935, _M0L8filenameS904, _M0L5indexS906, _M0L13handle__startS902, _M0L14handle__resultS907, _M0L17error__to__stringS914);
          moonbit_decref_cycle_free(_M0L13handle__startS902);
          moonbit_decref_cycle_free(_M0L17error__to__stringS914);
          if (_tmp_2043.tag) {
            int32_t const _M0L5_2aokS1998 = _tmp_2043.data.ok;
            _M0L6_2atmpS1997 = _M0L5_2aokS1998;
          } else {
            void* const _M0L6_2aerrS1999 = _tmp_2043.data.err;
            _M0L11_2atry__errS929 = _M0L6_2aerrS1999;
            goto join_928;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1997) {
    void* _M0L129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2008 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2008)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2008)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS929
    = _M0L129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2008;
    goto join_928;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS907);
  }
  goto joinlet_2034;
  join_928:;
  _M0L3errS930 = _M0L11_2atry__errS929;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS933
  = (struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS930;
  _M0L7_2anameS934 = _M0L36_2aMoonBitTestDriverInternalSkipTestS933->$0;
  _M0L6_2acntS2026
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS933));
  if (_M0L6_2acntS2026 > 1) {
    int32_t _M0L11_2anew__cntS2027 = _M0L6_2acntS2026 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS933), _M0L11_2anew__cntS2027);
    moonbit_incref_cycle_free(_M0L7_2anameS934);
  } else if (_M0L6_2acntS2026 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS933);
  }
  _M0L4nameS932 = _M0L7_2anameS934;
  goto join_931;
  goto joinlet_2044;
  join_931:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS907(_M0L14handle__resultS907, _M0L4nameS932, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS907);
  moonbit_decref_cycle_free(_M0L4nameS932);
  joinlet_2044:;
  joinlet_2034:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS914(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1996,
  void* _M0L3errS915
) {
  void* _M0L1eS917;
  moonbit_string_t _M0L1eS919;
  moonbit_string_t _result_2047;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS915)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS920 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS915;
      moonbit_string_t _M0L4_2aeS921 = _M0L10_2aFailureS920->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS921);
      _M0L1eS919 = _M0L4_2aeS921;
      goto join_918;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS922 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS915;
      moonbit_string_t _M0L4_2aeS923 = _M0L15_2aInspectErrorS922->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS923);
      _M0L1eS919 = _M0L4_2aeS923;
      goto join_918;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS924 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS915;
      moonbit_string_t _M0L4_2aeS925 = _M0L16_2aSnapshotErrorS924->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS925);
      _M0L1eS919 = _M0L4_2aeS925;
      goto join_918;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS926 =
        (struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS915;
      moonbit_string_t _M0L4_2aeS927 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS926->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS927);
      _M0L1eS919 = _M0L4_2aeS927;
      goto join_918;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS915);
      _M0L1eS917 = _M0L3errS915;
      goto join_916;
      break;
    }
  }
  join_918:;
  return _M0L1eS919;
  join_916:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2047 = _M0FP15Error10to__string(_M0L1eS917);
  moonbit_decref_cycle_free(_M0L1eS917);
  return _result_2047;
}

int32_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS907(
  struct _M0TWssbEu* _M0L6_2aenvS1993,
  moonbit_string_t _M0L10__testnameS908,
  moonbit_string_t _M0L7messageS909,
  int32_t _M0L7skippedS910
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907* _M0L14_2acasted__envS1994;
  moonbit_string_t _M0L8filenameS904;
  int32_t _M0L5indexS906;
  moonbit_string_t _M0L10file__nameS911;
  moonbit_string_t _M0L7messageS912;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS913;
  moonbit_string_t _M0L6_2atmpS1995;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1994
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c907*)_M0L6_2aenvS1993;
  _M0L8filenameS904 = _M0L14_2acasted__envS1994->$1;
  _M0L5indexS906 = _M0L14_2acasted__envS1994->$0;
  if (!_M0L7skippedS910 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS911
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS904, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS912
  = _M0MPC16string6String14escape_2einner(_M0L7messageS909, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS913
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS913, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS913, _M0L10file__nameS911);
  moonbit_decref_cycle_free(_M0L10file__nameS911);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS913, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS913, _M0L5indexS906);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS913, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS913, _M0L7messageS912);
  moonbit_decref_cycle_free(_M0L7messageS912);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS913, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1995
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS913);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS913);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1995);
  moonbit_decref_cycle_free(_M0L6_2atmpS1995);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS902(
  struct _M0TWEu* _M0L6_2aenvS1990
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902* _M0L14_2acasted__envS1991;
  moonbit_string_t _M0L8filenameS904;
  int32_t _M0L5indexS906;
  moonbit_string_t _M0L10file__nameS903;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS905;
  moonbit_string_t _M0L6_2atmpS1992;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1991
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fhh__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c902*)_M0L6_2aenvS1990;
  _M0L8filenameS904 = _M0L14_2acasted__envS1991->$1;
  _M0L5indexS906 = _M0L14_2acasted__envS1991->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS903
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS904, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS905
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS905, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS905, _M0L10file__nameS903);
  moonbit_decref_cycle_free(_M0L10file__nameS903);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS905, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS905, _M0L5indexS906);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS905, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1992
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS905);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS905);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1992);
  moonbit_decref_cycle_free(_M0L6_2atmpS1992);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S872;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS879;
  struct _M0TUsiE** _M0L6_2atmpS1989;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS886;
  moonbit_string_t* _M0L9cli__argsS887;
  moonbit_string_t _M0L6_2atmpS1988;
  moonbit_string_t _M0L6_2atmpS1987;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS888;
  int32_t _M0L7_2abindS889;
  moonbit_string_t* _M0L7_2abindS890;
  int32_t _M0L6_2acntS2028;
  int32_t _M0L2__S891;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S872 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS879 = 0;
  _M0L6_2atmpS1989 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS886
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS886)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS886->$0 = _M0L6_2atmpS1989;
  _M0L16file__and__indexS886->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS887
  = _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS887)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1988 = (moonbit_string_t)_M0L9cli__argsS887[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1988);
  moonbit_decref_cycle_free(_M0L9cli__argsS887);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1987
  = _M0MP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1988);
  moonbit_decref_cycle_free(_M0L6_2atmpS1988);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS888
  = _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS879(_M0L51moonbit__test__driver__internal__split__mbt__stringS879, _M0L6_2atmpS1987, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1987);
  _M0L7_2abindS889 = _M0L10test__argsS888->$1;
  _M0L7_2abindS890 = _M0L10test__argsS888->$0;
  _M0L6_2acntS2028
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS888));
  if (_M0L6_2acntS2028 > 1) {
    int32_t _M0L11_2anew__cntS2029 = _M0L6_2acntS2028 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS888), _M0L11_2anew__cntS2029);
    moonbit_incref_cycle_free(_M0L7_2abindS890);
  } else if (_M0L6_2acntS2028 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS888);
  }
  _M0L2__S891 = 0;
  while (1) {
    if (_M0L2__S891 < _M0L7_2abindS889) {
      moonbit_string_t _M0L3argS892 =
        (moonbit_string_t)_M0L7_2abindS890[_M0L2__S891];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS893;
      moonbit_string_t _M0L4fileS894;
      moonbit_string_t _M0L5rangeS895;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS896;
      moonbit_string_t _M0L6_2atmpS1985;
      int32_t _M0L5startS897;
      moonbit_string_t _M0L6_2atmpS1984;
      int32_t _M0L3endS898;
      int32_t _M0L1iS899;
      int32_t _M0L6_2atmpS1986;
      moonbit_incref_cycle_free(_M0L3argS892);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS893
      = _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS879(_M0L51moonbit__test__driver__internal__split__mbt__stringS879, _M0L3argS892, 58);
      moonbit_decref_cycle_free(_M0L3argS892);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS894
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS893, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS895
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS893, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS893);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS896
      = _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS879(_M0L51moonbit__test__driver__internal__split__mbt__stringS879, _M0L5rangeS895, 45);
      moonbit_decref_cycle_free(_M0L5rangeS895);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1985
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS896, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS897
      = _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S872(_M0L45moonbit__test__driver__internal__parse__int__S872, _M0L6_2atmpS1985);
      moonbit_decref_cycle_free(_M0L6_2atmpS1985);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1984
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS896, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS896);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS898
      = _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S872(_M0L45moonbit__test__driver__internal__parse__int__S872, _M0L6_2atmpS1984);
      moonbit_decref_cycle_free(_M0L6_2atmpS1984);
      _M0L1iS899 = _M0L5startS897;
      while (1) {
        if (_M0L1iS899 < _M0L3endS898) {
          struct _M0TUsiE* _M0L8_2atupleS1982;
          int32_t _M0L6_2atmpS1983;
          moonbit_incref_cycle_free(_M0L4fileS894);
          _M0L8_2atupleS1982
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1982)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1982->$0 = _M0L4fileS894;
          _M0L8_2atupleS1982->$1 = _M0L1iS899;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS886, _M0L8_2atupleS1982);
          _M0L6_2atmpS1983 = _M0L1iS899 + 1;
          _M0L1iS899 = _M0L6_2atmpS1983;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS894);
        }
        break;
      }
      _M0L6_2atmpS1986 = _M0L2__S891 + 1;
      _M0L2__S891 = _M0L6_2atmpS1986;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS890);
    }
    break;
  }
  return _M0L16file__and__indexS886;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS879(
  int32_t _M0L6_2aenvS1963,
  moonbit_string_t _M0L1sS880,
  int32_t _M0L3sepS881
) {
  moonbit_string_t* _M0L6_2atmpS1981;
  struct _M0TPB5ArrayGsE* _M0L3resS882;
  struct _M0TPB8MutLocalGiE* _M0L1iS883;
  struct _M0TPB8MutLocalGiE* _M0L5startS884;
  int32_t _M0L3valS1976;
  int32_t _M0L6_2atmpS1977;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1981 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS882
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS882)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS882->$0 = _M0L6_2atmpS1981;
  _M0L3resS882->$1 = 0;
  _M0L1iS883
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS883)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS883->$0 = 0;
  _M0L5startS884
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS884)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS884->$0 = 0;
  while (1) {
    int32_t _M0L3valS1964 = _M0L1iS883->$0;
    int32_t _M0L6_2atmpS1965 = Moonbit_array_length(_M0L1sS880);
    if (_M0L3valS1964 < _M0L6_2atmpS1965) {
      int32_t _M0L3valS1968 = _M0L1iS883->$0;
      int32_t _M0L6_2atmpS1967;
      int32_t _M0L6_2atmpS1966;
      int32_t _M0L3valS1975;
      int32_t _M0L6_2atmpS1974;
      if (
        _M0L3valS1968 < 0
        || _M0L3valS1968 >= Moonbit_array_length(_M0L1sS880)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1967 = _M0L1sS880[_M0L3valS1968];
      _M0L6_2atmpS1966 = _M0L6_2atmpS1967;
      if (_M0L6_2atmpS1966 == _M0L3sepS881) {
        int32_t _M0L3valS1970 = _M0L5startS884->$0;
        int32_t _M0L3valS1971 = _M0L1iS883->$0;
        moonbit_string_t _M0L6_2atmpS1969;
        int32_t _M0L3valS1973;
        int32_t _M0L6_2atmpS1972;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1969
        = _M0MPC16string6String17unsafe__substring(_M0L1sS880, _M0L3valS1970, _M0L3valS1971);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS882, _M0L6_2atmpS1969);
        _M0L3valS1973 = _M0L1iS883->$0;
        _M0L6_2atmpS1972 = _M0L3valS1973 + 1;
        _M0L5startS884->$0 = _M0L6_2atmpS1972;
      }
      _M0L3valS1975 = _M0L1iS883->$0;
      _M0L6_2atmpS1974 = _M0L3valS1975 + 1;
      _M0L1iS883->$0 = _M0L6_2atmpS1974;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS883);
    }
    break;
  }
  _M0L3valS1976 = _M0L5startS884->$0;
  _M0L6_2atmpS1977 = Moonbit_array_length(_M0L1sS880);
  if (_M0L3valS1976 < _M0L6_2atmpS1977) {
    int32_t _M0L3valS1979 = _M0L5startS884->$0;
    int32_t _M0L6_2atmpS1980;
    moonbit_string_t _M0L6_2atmpS1978;
    moonbit_decref_cycle_free(_M0L5startS884);
    _M0L6_2atmpS1980 = Moonbit_array_length(_M0L1sS880);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1978
    = _M0MPC16string6String17unsafe__substring(_M0L1sS880, _M0L3valS1979, _M0L6_2atmpS1980);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS882, _M0L6_2atmpS1978);
  } else {
    moonbit_decref_cycle_free(_M0L5startS884);
  }
  return _M0L3resS882;
}

int32_t _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S872(
  int32_t _M0L6_2aenvS1956,
  moonbit_string_t _M0L1sS873
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS874;
  int32_t _M0L3lenS875;
  int32_t _M0L7_2abindS876;
  int32_t _M0L1iS877;
  int32_t _result_2052;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS874
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS874)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS874->$0 = 0;
  _M0L3lenS875 = Moonbit_array_length(_M0L1sS873);
  _M0L7_2abindS876 = 0;
  _M0L1iS877 = _M0L7_2abindS876;
  while (1) {
    if (_M0L1iS877 < _M0L3lenS875) {
      int32_t _M0L3valS1961 = _M0L3resS874->$0;
      int32_t _M0L6_2atmpS1958 = _M0L3valS1961 * 10;
      int32_t _M0L6_2atmpS1960;
      int32_t _M0L6_2atmpS1959;
      int32_t _M0L6_2atmpS1957;
      int32_t _M0L6_2atmpS1962;
      if (_M0L1iS877 < 0 || _M0L1iS877 >= Moonbit_array_length(_M0L1sS873)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1960 = _M0L1sS873[_M0L1iS877];
      _M0L6_2atmpS1959 = _M0L6_2atmpS1960 - 48;
      _M0L6_2atmpS1957 = _M0L6_2atmpS1958 + _M0L6_2atmpS1959;
      _M0L3resS874->$0 = _M0L6_2atmpS1957;
      _M0L6_2atmpS1962 = _M0L1iS877 + 1;
      _M0L1iS877 = _M0L6_2atmpS1962;
      continue;
    }
    break;
  }
  _result_2052 = _M0L3resS874->$0;
  moonbit_decref_cycle_free(_M0L3resS874);
  return _result_2052;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS871
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS871);
  return _M0L4selfS871;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S841,
  moonbit_string_t _M0L12_2adiscard__S842,
  int32_t _M0L12_2adiscard__S843,
  struct _M0TWEu* _M0L12_2adiscard__S844,
  struct _M0TWssbEu* _M0L12_2adiscard__S845,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S846
) {
  struct moonbit_result_0 _result_2053;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2053.tag = 1;
  _result_2053.data.ok = 0;
  return _result_2053;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S847,
  moonbit_string_t _M0L12_2adiscard__S848,
  int32_t _M0L12_2adiscard__S849,
  struct _M0TWEu* _M0L12_2adiscard__S850,
  struct _M0TWssbEu* _M0L12_2adiscard__S851,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S852
) {
  struct moonbit_result_0 _result_2054;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2054.tag = 1;
  _result_2054.data.ok = 0;
  return _result_2054;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S853,
  moonbit_string_t _M0L12_2adiscard__S854,
  int32_t _M0L12_2adiscard__S855,
  struct _M0TWEu* _M0L12_2adiscard__S856,
  struct _M0TWssbEu* _M0L12_2adiscard__S857,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S858
) {
  struct moonbit_result_0 _result_2055;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2055.tag = 1;
  _result_2055.data.ok = 0;
  return _result_2055;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S859,
  moonbit_string_t _M0L12_2adiscard__S860,
  int32_t _M0L12_2adiscard__S861,
  struct _M0TWEu* _M0L12_2adiscard__S862,
  struct _M0TWssbEu* _M0L12_2adiscard__S863,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S864
) {
  struct moonbit_result_0 _result_2056;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2056.tag = 1;
  _result_2056.data.ok = 0;
  return _result_2056;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S865,
  moonbit_string_t _M0L12_2adiscard__S866,
  int32_t _M0L12_2adiscard__S867,
  struct _M0TWEu* _M0L12_2adiscard__S868,
  struct _M0TWssbEu* _M0L12_2adiscard__S869,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S870
) {
  struct moonbit_result_0 _result_2057;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2057.tag = 1;
  _result_2057.data.ok = 0;
  return _result_2057;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S840
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples10hh__neuron7run__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L3popS830,
  int32_t _M0L12total__stepsS828,
  float _M0L2dtS831
) {
  struct _M0TPB8MutLocalGiE* _M0L6spikesS825;
  int32_t _M0L7_2abindS826;
  int32_t _M0L2__S827;
  int32_t _result_2060;
  #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\main.mbt"
  _M0L6spikesS825
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L6spikesS825)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6spikesS825->$0 = 0;
  _M0L7_2abindS826 = 0;
  _M0L2__S827 = _M0L7_2abindS826;
  while (1) {
    if (_M0L2__S827 < _M0L12total__stepsS828) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1954 = _M0L3popS830->$6;
      int32_t _M0L10prev__fireS829;
      struct _M0TPB5ArrayGbE* _M0L4fireS1951;
      int32_t _if__result_2059;
      int32_t _M0L6_2atmpS1955;
      #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\main.mbt"
      _M0L10prev__fireS829 = _M0MPC15array5Array2atGbE(_M0L4fireS1954, 0);
      #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\main.mbt"
      _M0FP26RiantR8snn__mbt8step__hh(_M0L3popS830, _M0L2dtS831);
      _M0L4fireS1951 = _M0L3popS830->$6;
      #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\main.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1951, 0)) {
        _if__result_2059 = !_M0L10prev__fireS829;
      } else {
        _if__result_2059 = 0;
      }
      if (_if__result_2059) {
        int32_t _M0L3valS1953 = _M0L6spikesS825->$0;
        int32_t _M0L6_2atmpS1952 = _M0L3valS1953 + 1;
        _M0L6spikesS825->$0 = _M0L6_2atmpS1952;
      }
      _M0L6_2atmpS1955 = _M0L2__S827 + 1;
      _M0L2__S827 = _M0L6_2atmpS1955;
      continue;
    }
    break;
  }
  _result_2060 = _M0L6spikesS825->$0;
  moonbit_decref_cycle_free(_M0L6spikesS825);
  return _result_2060;
}

struct _M0TP26RiantR8snn__mbt2HH* _M0MP26RiantR8snn__mbt2HH3new(
  int32_t _M0L1nS804,
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L5paramS805,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS811
) {
  float _M0L2elS1950;
  struct _M0TPB5ArrayGfE* _M0L1vS803;
  int32_t _M0L7_2abindS806;
  int32_t _M0L1kS807;
  struct _M0TPB5ArrayGfE* _M0L1mS814;
  struct _M0TPB5ArrayGfE* _M0L7n__gateS815;
  struct _M0TPB5ArrayGfE* _M0L1hS816;
  struct _M0TPB5ArrayGbE* _M0L4fireS817;
  struct _M0TPB5ArrayGfE* _M0L1iS818;
  struct _M0TPB5ArrayGfE* _M0L2geS819;
  struct _M0TPB5ArrayGfE* _M0L2giS820;
  int32_t _M0L7_2abindS821;
  int32_t _M0L3__kS822;
  struct _M0TP26RiantR8snn__mbt2HH* _block_2064;
  #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L2elS1950 = _M0L5paramS805->$2;
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1vS803 = _M0MPC15array5Array4makeGfE(_M0L1nS804, _M0L2elS1950);
  _M0L7_2abindS806 = 0;
  _M0L1kS807 = _M0L7_2abindS806;
  while (1) {
    if (_M0L1kS807 < _M0L1nS804) {
      double _M0L2z1S809;
      struct _M0TUddE* _M0L7_2abindS810;
      double _M0L5_2az1S812;
      float _M0L2elS1944;
      float _M0L6_2atmpS1947;
      float _M0L6_2atmpS1946;
      float _M0L6_2atmpS1945;
      float _M0L6_2atmpS1943;
      int32_t _M0L6_2atmpS1948;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L7_2abindS810 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS811);
      _M0L5_2az1S812 = _M0L7_2abindS810->$0;
      moonbit_decref_cycle_free(_M0L7_2abindS810);
      _M0L2z1S809 = _M0L5_2az1S812;
      goto join_808;
      goto joinlet_2062;
      join_808:;
      _M0L2elS1944 = _M0L5paramS805->$2;
      _M0L6_2atmpS1947 = (float)_M0L2z1S809;
      _M0L6_2atmpS1946 = _M0L6_2atmpS1947 - 0x1p+0f;
      _M0L6_2atmpS1945 = 0x1.4p+2f * _M0L6_2atmpS1946;
      _M0L6_2atmpS1943 = _M0L2elS1944 + _M0L6_2atmpS1945;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS803, _M0L1kS807, _M0L6_2atmpS1943);
      joinlet_2062:;
      _M0L6_2atmpS1948 = _M0L1kS807 + 1;
      _M0L1kS807 = _M0L6_2atmpS1948;
      continue;
    }
    break;
  }
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1mS814 = _M0MPC15array5Array4makeGfE(_M0L1nS804, 0x0p+0f);
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L7n__gateS815 = _M0MPC15array5Array4makeGfE(_M0L1nS804, 0x0p+0f);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1hS816 = _M0MPC15array5Array4makeGfE(_M0L1nS804, 0x1p+0f);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L4fireS817 = _M0MPC15array5Array4makeGbE(_M0L1nS804, 0);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1iS818 = _M0MPC15array5Array4makeGfE(_M0L1nS804, 0x0p+0f);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L2geS819 = _M0MPC15array5Array4makeGfE(_M0L1nS804, 0x0p+0f);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L2giS820 = _M0MPC15array5Array4makeGfE(_M0L1nS804, 0x0p+0f);
  _M0L7_2abindS821 = 0;
  _M0L3__kS822 = _M0L7_2abindS821;
  while (1) {
    if (_M0L3__kS822 < _M0L1nS804) {
      struct _M0TUddE* _M0L6_2atmpS2010;
      int32_t _M0L6_2atmpS1949;
      #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS2010 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS811);
      moonbit_decref_cycle_free(_M0L6_2atmpS2010);
      _M0L6_2atmpS1949 = _M0L3__kS822 + 1;
      _M0L3__kS822 = _M0L6_2atmpS1949;
      continue;
    }
    break;
  }
  moonbit_incref_cycle_free(_M0L5paramS805);
  _block_2064
  = (struct _M0TP26RiantR8snn__mbt2HH*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2HH));
  Moonbit_object_header(_block_2064)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2064->$0 = _M0L5paramS805;
  _block_2064->$1 = _M0L1nS804;
  _block_2064->$2 = _M0L1vS803;
  _block_2064->$3 = _M0L1mS814;
  _block_2064->$4 = _M0L7n__gateS815;
  _block_2064->$5 = _M0L1hS816;
  _block_2064->$6 = _M0L4fireS817;
  _block_2064->$7 = _M0L1iS818;
  _block_2064->$8 = _M0L2geS819;
  _block_2064->$9 = _M0L2giS820;
  return _block_2064;
}

struct _M0TP26RiantR8snn__mbt11HHParameter* _M0MP26RiantR8snn__mbt11HHParameter3new(
  
) {
  float _M0L6_2atmpS1941;
  float _M0L6_2atmpS1942;
  float _M0L2cmS801;
  float _M0L6_2atmpS1940;
  float _M0L6_2atmpS1938;
  float _M0L6_2atmpS1939;
  float _M0L2glS802;
  float _M0L6_2atmpS1937;
  float _M0L6_2atmpS1935;
  float _M0L6_2atmpS1936;
  float _M0L6_2atmpS1930;
  float _M0L6_2atmpS1934;
  float _M0L6_2atmpS1932;
  float _M0L6_2atmpS1933;
  float _M0L6_2atmpS1931;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _block_2065;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L6_2atmpS1941 = 0x1.e848p+19f * 0x1p+0f;
  _M0L6_2atmpS1942 = 0x1.388p+14f * 0x1.5798ee2308c3ap-27f;
  _M0L2cmS801 = _M0L6_2atmpS1941 * _M0L6_2atmpS1942;
  _M0L6_2atmpS1940 = 0x1.a36e2eb1c432dp-15f * 0x1.dcd65p+29f;
  _M0L6_2atmpS1938 = _M0L6_2atmpS1940 * 0x1p+0f;
  _M0L6_2atmpS1939 = 0x1.388p+14f * 0x1.5798ee2308c3ap-27f;
  _M0L2glS802 = _M0L6_2atmpS1938 * _M0L6_2atmpS1939;
  _M0L6_2atmpS1937 = 0x1.9p+6f * 0x1.e848p+19f;
  _M0L6_2atmpS1935 = _M0L6_2atmpS1937 * 0x1p+0f;
  _M0L6_2atmpS1936 = 0x1.388p+14f * 0x1.5798ee2308c3ap-27f;
  _M0L6_2atmpS1930 = _M0L6_2atmpS1935 * _M0L6_2atmpS1936;
  _M0L6_2atmpS1934 = 0x1.ep+4f * 0x1.e848p+19f;
  _M0L6_2atmpS1932 = _M0L6_2atmpS1934 * 0x1p+0f;
  _M0L6_2atmpS1933 = 0x1.388p+14f * 0x1.5798ee2308c3ap-27f;
  _M0L6_2atmpS1931 = _M0L6_2atmpS1932 * _M0L6_2atmpS1933;
  _block_2065
  = (struct _M0TP26RiantR8snn__mbt11HHParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11HHParameter));
  Moonbit_object_header(_block_2065)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2065->$0 = _M0L2cmS801;
  _block_2065->$1 = _M0L2glS802;
  _block_2065->$2 = -0x1.04p+6f;
  _block_2065->$3 = -0x1.68p+6f;
  _block_2065->$4 = 0x1.9p+5f;
  _block_2065->$5 = _M0L6_2atmpS1930;
  _block_2065->$6 = _M0L6_2atmpS1931;
  _block_2065->$7 = -0x1.f8p+5f;
  _block_2065->$8 = 0x1.4p+2f;
  _block_2065->$9 = 0x1.4p+3f;
  _block_2065->$10 = 0x0p+0f;
  _block_2065->$11 = -0x1.4p+6f;
  return _block_2065;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS758,
  float _M0L2dtS784
) {
  int32_t _M0L1nS757;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S759;
  float _M0L2cmS760;
  float _M0L2glS761;
  float _M0L2elS762;
  float _M0L2ekS763;
  float _M0L2enS764;
  float _M0L2gnS765;
  float _M0L2gkS766;
  float _M0L2vtS767;
  float _M0L6tau__eS768;
  float _M0L6tau__iS769;
  float _M0L4e__eS770;
  float _M0L4e__iS771;
  int32_t _M0L7_2abindS772;
  int32_t _M0L1iS773;
  int32_t _M0L7_2abindS798;
  int32_t _M0L1iS799;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS757 = _M0L1pS758->$1;
  _M0L3p__S759 = _M0L1pS758->$0;
  _M0L2cmS760 = _M0L3p__S759->$0;
  _M0L2glS761 = _M0L3p__S759->$1;
  _M0L2elS762 = _M0L3p__S759->$2;
  _M0L2ekS763 = _M0L3p__S759->$3;
  _M0L2enS764 = _M0L3p__S759->$4;
  _M0L2gnS765 = _M0L3p__S759->$5;
  _M0L2gkS766 = _M0L3p__S759->$6;
  _M0L2vtS767 = _M0L3p__S759->$7;
  _M0L6tau__eS768 = _M0L3p__S759->$8;
  _M0L6tau__iS769 = _M0L3p__S759->$9;
  _M0L4e__eS770 = _M0L3p__S759->$10;
  _M0L4e__iS771 = _M0L3p__S759->$11;
  _M0L7_2abindS772 = 0;
  _M0L1iS773 = _M0L7_2abindS772;
  while (1) {
    if (_M0L1iS773 < _M0L1nS757) {
      struct _M0TPB5ArrayGfE* _M0L1vS1923 = _M0L1pS758->$2;
      float _M0L1vS774;
      struct _M0TPB5ArrayGfE* _M0L1mS1922;
      float _M0L1mS775;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS1921;
      float _M0L2nnS776;
      struct _M0TPB5ArrayGfE* _M0L1hS1920;
      float _M0L1hS777;
      struct _M0TPB5ArrayGfE* _M0L2geS1919;
      float _M0L2geS778;
      struct _M0TPB5ArrayGfE* _M0L2giS1918;
      float _M0L2giS779;
      struct _M0TPB5ArrayGbE* _M0L4fireS1821;
      float _M0L6_2atmpS1917;
      float _M0L7am__numS780;
      float _M0L6_2atmpS1916;
      float _M0L7bm__numS781;
      float _M0L6_2atmpS1911;
      float _M0L6_2atmpS1910;
      float _M0L6_2atmpS1909;
      float _M0L2amS782;
      float _M0L6_2atmpS1904;
      float _M0L6_2atmpS1903;
      float _M0L6_2atmpS1902;
      float _M0L2bmS783;
      struct _M0TPB5ArrayGfE* _M0L1mS1822;
      float _M0L6_2atmpS1828;
      float _M0L6_2atmpS1826;
      float _M0L6_2atmpS1827;
      float _M0L6_2atmpS1825;
      float _M0L6_2atmpS1824;
      float _M0L6_2atmpS1823;
      float _M0L6_2atmpS1901;
      float _M0L7an__numS785;
      float _M0L6_2atmpS1896;
      float _M0L6_2atmpS1895;
      float _M0L6_2atmpS1894;
      float _M0L2anS786;
      float _M0L6_2atmpS1893;
      float _M0L6_2atmpS1892;
      float _M0L6_2atmpS1891;
      float _M0L6_2atmpS1890;
      float _M0L2bnS787;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS1829;
      float _M0L6_2atmpS1835;
      float _M0L6_2atmpS1833;
      float _M0L6_2atmpS1834;
      float _M0L6_2atmpS1832;
      float _M0L6_2atmpS1831;
      float _M0L6_2atmpS1830;
      float _M0L6_2atmpS1889;
      float _M0L6_2atmpS1888;
      float _M0L6_2atmpS1887;
      float _M0L6_2atmpS1886;
      float _M0L2ahS788;
      float _M0L6_2atmpS1885;
      float _M0L6_2atmpS1884;
      float _M0L6_2atmpS1883;
      float _M0L6_2atmpS1882;
      float _M0L9bh__denomS789;
      float _M0L2bhS790;
      struct _M0TPB5ArrayGfE* _M0L1hS1836;
      float _M0L6_2atmpS1842;
      float _M0L6_2atmpS1840;
      float _M0L6_2atmpS1841;
      float _M0L6_2atmpS1839;
      float _M0L6_2atmpS1838;
      float _M0L6_2atmpS1837;
      struct _M0TPB5ArrayGfE* _M0L1mS1881;
      float _M0L6m__newS791;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS1880;
      float _M0L6n__newS792;
      struct _M0TPB5ArrayGfE* _M0L1hS1879;
      float _M0L6h__newS793;
      float _M0L6_2atmpS1878;
      float _M0L6_2atmpS1877;
      float _M0L3m3hS794;
      float _M0L6_2atmpS1876;
      float _M0L6_2atmpS1875;
      float _M0L2n4S795;
      struct _M0TPB5ArrayGfE* _M0L1iS1874;
      float _M0L6_2atmpS1871;
      float _M0L6_2atmpS1873;
      float _M0L6_2atmpS1872;
      float _M0L6_2atmpS1868;
      float _M0L6_2atmpS1870;
      float _M0L6_2atmpS1869;
      float _M0L6_2atmpS1865;
      float _M0L6_2atmpS1867;
      float _M0L6_2atmpS1866;
      float _M0L6_2atmpS1861;
      float _M0L6_2atmpS1863;
      float _M0L6_2atmpS1864;
      float _M0L6_2atmpS1862;
      float _M0L6_2atmpS1857;
      float _M0L6_2atmpS1859;
      float _M0L6_2atmpS1860;
      float _M0L6_2atmpS1858;
      float _M0L7currentS796;
      struct _M0TPB5ArrayGfE* _M0L1vS1843;
      float _M0L6_2atmpS1846;
      float _M0L6_2atmpS1845;
      float _M0L6_2atmpS1844;
      struct _M0TPB5ArrayGfE* _M0L2geS1847;
      float _M0L6_2atmpS1851;
      float _M0L6_2atmpS1850;
      float _M0L6_2atmpS1849;
      float _M0L6_2atmpS1848;
      struct _M0TPB5ArrayGfE* _M0L2giS1852;
      float _M0L6_2atmpS1856;
      float _M0L6_2atmpS1855;
      float _M0L6_2atmpS1854;
      float _M0L6_2atmpS1853;
      int32_t _M0L6_2atmpS1924;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS774 = _M0MPC15array5Array2atGfE(_M0L1vS1923, _M0L1iS773);
      _M0L1mS1922 = _M0L1pS758->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS775 = _M0MPC15array5Array2atGfE(_M0L1mS1922, _M0L1iS773);
      _M0L7n__gateS1921 = _M0L1pS758->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS776 = _M0MPC15array5Array2atGfE(_M0L7n__gateS1921, _M0L1iS773);
      _M0L1hS1920 = _M0L1pS758->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS777 = _M0MPC15array5Array2atGfE(_M0L1hS1920, _M0L1iS773);
      _M0L2geS1919 = _M0L1pS758->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS778 = _M0MPC15array5Array2atGfE(_M0L2geS1919, _M0L1iS773);
      _M0L2giS1918 = _M0L1pS758->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS779 = _M0MPC15array5Array2atGfE(_M0L2giS1918, _M0L1iS773);
      _M0L4fireS1821 = _M0L1pS758->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1821, _M0L1iS773, 0);
      _M0L6_2atmpS1917 = 0x1.ap+3f - _M0L1vS774;
      _M0L7am__numS780 = _M0L6_2atmpS1917 + _M0L2vtS767;
      _M0L6_2atmpS1916 = _M0L1vS774 - _M0L2vtS767;
      _M0L7bm__numS781 = _M0L6_2atmpS1916 - 0x1.4p+5f;
      _M0L6_2atmpS1911 = _M0L7am__numS780 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS1910 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1911);
      _M0L6_2atmpS1909 = _M0L6_2atmpS1910 - 0x1p+0f;
      if (_M0L6_2atmpS1909 != 0x0p+0f) {
        float _M0L6_2atmpS1912 = 0x1.47ae147ae147bp-2f * _M0L7am__numS780;
        float _M0L6_2atmpS1915 = _M0L7am__numS780 / 0x1p+2f;
        float _M0L6_2atmpS1914;
        float _M0L6_2atmpS1913;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS1914 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1915);
        _M0L6_2atmpS1913 = _M0L6_2atmpS1914 - 0x1p+0f;
        _M0L2amS782 = _M0L6_2atmpS1912 / _M0L6_2atmpS1913;
      } else {
        _M0L2amS782 = 0x0p+0f;
      }
      _M0L6_2atmpS1904 = _M0L7bm__numS781 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS1903 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1904);
      _M0L6_2atmpS1902 = _M0L6_2atmpS1903 - 0x1p+0f;
      if (_M0L6_2atmpS1902 != 0x0p+0f) {
        float _M0L6_2atmpS1905 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS781;
        float _M0L6_2atmpS1908 = _M0L7bm__numS781 / 0x1.4p+2f;
        float _M0L6_2atmpS1907;
        float _M0L6_2atmpS1906;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS1907 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1908);
        _M0L6_2atmpS1906 = _M0L6_2atmpS1907 - 0x1p+0f;
        _M0L2bmS783 = _M0L6_2atmpS1905 / _M0L6_2atmpS1906;
      } else {
        _M0L2bmS783 = 0x0p+0f;
      }
      _M0L1mS1822 = _M0L1pS758->$3;
      _M0L6_2atmpS1828 = 0x1p+0f - _M0L1mS775;
      _M0L6_2atmpS1826 = _M0L2amS782 * _M0L6_2atmpS1828;
      _M0L6_2atmpS1827 = _M0L2bmS783 * _M0L1mS775;
      _M0L6_2atmpS1825 = _M0L6_2atmpS1826 - _M0L6_2atmpS1827;
      _M0L6_2atmpS1824 = _M0L2dtS784 * _M0L6_2atmpS1825;
      _M0L6_2atmpS1823 = _M0L1mS775 + _M0L6_2atmpS1824;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS1822, _M0L1iS773, _M0L6_2atmpS1823);
      _M0L6_2atmpS1901 = 0x1.ep+3f - _M0L1vS774;
      _M0L7an__numS785 = _M0L6_2atmpS1901 + _M0L2vtS767;
      _M0L6_2atmpS1896 = _M0L7an__numS785 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS1895 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1896);
      _M0L6_2atmpS1894 = _M0L6_2atmpS1895 - 0x1p+0f;
      if (_M0L6_2atmpS1894 != 0x0p+0f) {
        float _M0L6_2atmpS1897 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS785;
        float _M0L6_2atmpS1900 = _M0L7an__numS785 / 0x1.4p+2f;
        float _M0L6_2atmpS1899;
        float _M0L6_2atmpS1898;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS1899 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1900);
        _M0L6_2atmpS1898 = _M0L6_2atmpS1899 - 0x1p+0f;
        _M0L2anS786 = _M0L6_2atmpS1897 / _M0L6_2atmpS1898;
      } else {
        _M0L2anS786 = 0x0p+0f;
      }
      _M0L6_2atmpS1893 = 0x1.4p+3f - _M0L1vS774;
      _M0L6_2atmpS1892 = _M0L6_2atmpS1893 + _M0L2vtS767;
      _M0L6_2atmpS1891 = _M0L6_2atmpS1892 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS1890 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1891);
      _M0L2bnS787 = 0x1p-1f * _M0L6_2atmpS1890;
      _M0L7n__gateS1829 = _M0L1pS758->$4;
      _M0L6_2atmpS1835 = 0x1p+0f - _M0L2nnS776;
      _M0L6_2atmpS1833 = _M0L2anS786 * _M0L6_2atmpS1835;
      _M0L6_2atmpS1834 = _M0L2bnS787 * _M0L2nnS776;
      _M0L6_2atmpS1832 = _M0L6_2atmpS1833 - _M0L6_2atmpS1834;
      _M0L6_2atmpS1831 = _M0L2dtS784 * _M0L6_2atmpS1832;
      _M0L6_2atmpS1830 = _M0L2nnS776 + _M0L6_2atmpS1831;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS1829, _M0L1iS773, _M0L6_2atmpS1830);
      _M0L6_2atmpS1889 = 0x1.1p+4f - _M0L1vS774;
      _M0L6_2atmpS1888 = _M0L6_2atmpS1889 + _M0L2vtS767;
      _M0L6_2atmpS1887 = _M0L6_2atmpS1888 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS1886 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1887);
      _M0L2ahS788 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS1886;
      _M0L6_2atmpS1885 = 0x1.4p+5f - _M0L1vS774;
      _M0L6_2atmpS1884 = _M0L6_2atmpS1885 + _M0L2vtS767;
      _M0L6_2atmpS1883 = _M0L6_2atmpS1884 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS1882 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1883);
      _M0L9bh__denomS789 = 0x1p+0f + _M0L6_2atmpS1882;
      if (_M0L9bh__denomS789 != 0x0p+0f) {
        _M0L2bhS790 = 0x1p+2f / _M0L9bh__denomS789;
      } else {
        _M0L2bhS790 = 0x0p+0f;
      }
      _M0L1hS1836 = _M0L1pS758->$5;
      _M0L6_2atmpS1842 = 0x1p+0f - _M0L1hS777;
      _M0L6_2atmpS1840 = _M0L2ahS788 * _M0L6_2atmpS1842;
      _M0L6_2atmpS1841 = _M0L2bhS790 * _M0L1hS777;
      _M0L6_2atmpS1839 = _M0L6_2atmpS1840 - _M0L6_2atmpS1841;
      _M0L6_2atmpS1838 = _M0L2dtS784 * _M0L6_2atmpS1839;
      _M0L6_2atmpS1837 = _M0L1hS777 + _M0L6_2atmpS1838;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS1836, _M0L1iS773, _M0L6_2atmpS1837);
      _M0L1mS1881 = _M0L1pS758->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS791 = _M0MPC15array5Array2atGfE(_M0L1mS1881, _M0L1iS773);
      _M0L7n__gateS1880 = _M0L1pS758->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS792
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS1880, _M0L1iS773);
      _M0L1hS1879 = _M0L1pS758->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS793 = _M0MPC15array5Array2atGfE(_M0L1hS1879, _M0L1iS773);
      _M0L6_2atmpS1878 = _M0L6m__newS791 * _M0L6m__newS791;
      _M0L6_2atmpS1877 = _M0L6_2atmpS1878 * _M0L6m__newS791;
      _M0L3m3hS794 = _M0L6_2atmpS1877 * _M0L6h__newS793;
      _M0L6_2atmpS1876 = _M0L6n__newS792 * _M0L6n__newS792;
      _M0L6_2atmpS1875 = _M0L6_2atmpS1876 * _M0L6n__newS792;
      _M0L2n4S795 = _M0L6_2atmpS1875 * _M0L6n__newS792;
      _M0L1iS1874 = _M0L1pS758->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS1871 = _M0MPC15array5Array2atGfE(_M0L1iS1874, _M0L1iS773);
      _M0L6_2atmpS1873 = _M0L2elS762 - _M0L1vS774;
      _M0L6_2atmpS1872 = _M0L2glS761 * _M0L6_2atmpS1873;
      _M0L6_2atmpS1868 = _M0L6_2atmpS1871 + _M0L6_2atmpS1872;
      _M0L6_2atmpS1870 = _M0L4e__eS770 - _M0L1vS774;
      _M0L6_2atmpS1869 = _M0L2geS778 * _M0L6_2atmpS1870;
      _M0L6_2atmpS1865 = _M0L6_2atmpS1868 + _M0L6_2atmpS1869;
      _M0L6_2atmpS1867 = _M0L4e__iS771 - _M0L1vS774;
      _M0L6_2atmpS1866 = _M0L2giS779 * _M0L6_2atmpS1867;
      _M0L6_2atmpS1861 = _M0L6_2atmpS1865 + _M0L6_2atmpS1866;
      _M0L6_2atmpS1863 = _M0L2gnS765 * _M0L3m3hS794;
      _M0L6_2atmpS1864 = _M0L2enS764 - _M0L1vS774;
      _M0L6_2atmpS1862 = _M0L6_2atmpS1863 * _M0L6_2atmpS1864;
      _M0L6_2atmpS1857 = _M0L6_2atmpS1861 + _M0L6_2atmpS1862;
      _M0L6_2atmpS1859 = _M0L2gkS766 * _M0L2n4S795;
      _M0L6_2atmpS1860 = _M0L2ekS763 - _M0L1vS774;
      _M0L6_2atmpS1858 = _M0L6_2atmpS1859 * _M0L6_2atmpS1860;
      _M0L7currentS796 = _M0L6_2atmpS1857 + _M0L6_2atmpS1858;
      _M0L1vS1843 = _M0L1pS758->$2;
      _M0L6_2atmpS1846 = _M0L2dtS784 / _M0L2cmS760;
      _M0L6_2atmpS1845 = _M0L6_2atmpS1846 * _M0L7currentS796;
      _M0L6_2atmpS1844 = _M0L1vS774 + _M0L6_2atmpS1845;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1843, _M0L1iS773, _M0L6_2atmpS1844);
      _M0L2geS1847 = _M0L1pS758->$8;
      _M0L6_2atmpS1851 = -_M0L2geS778;
      _M0L6_2atmpS1850 = _M0L6_2atmpS1851 / _M0L6tau__eS768;
      _M0L6_2atmpS1849 = _M0L2dtS784 * _M0L6_2atmpS1850;
      _M0L6_2atmpS1848 = _M0L2geS778 + _M0L6_2atmpS1849;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1847, _M0L1iS773, _M0L6_2atmpS1848);
      _M0L2giS1852 = _M0L1pS758->$9;
      _M0L6_2atmpS1856 = -_M0L2giS779;
      _M0L6_2atmpS1855 = _M0L6_2atmpS1856 / _M0L6tau__iS769;
      _M0L6_2atmpS1854 = _M0L2dtS784 * _M0L6_2atmpS1855;
      _M0L6_2atmpS1853 = _M0L2giS779 + _M0L6_2atmpS1854;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1852, _M0L1iS773, _M0L6_2atmpS1853);
      _M0L6_2atmpS1924 = _M0L1iS773 + 1;
      _M0L1iS773 = _M0L6_2atmpS1924;
      continue;
    }
    break;
  }
  _M0L7_2abindS798 = 0;
  _M0L1iS799 = _M0L7_2abindS798;
  while (1) {
    if (_M0L1iS799 < _M0L1nS757) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1925 = _M0L1pS758->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS1928 = _M0L1pS758->$2;
      float _M0L6_2atmpS1927;
      int32_t _M0L6_2atmpS1926;
      int32_t _M0L6_2atmpS1929;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS1927 = _M0MPC15array5Array2atGfE(_M0L1vS1928, _M0L1iS799);
      _M0L6_2atmpS1926 = _M0L6_2atmpS1927 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1925, _M0L1iS799, _M0L6_2atmpS1926);
      _M0L6_2atmpS1929 = _M0L1iS799 + 1;
      _M0L1iS799 = _M0L6_2atmpS1929;
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS755
) {
  struct _M0TUmmmmE* _M0L1sS754;
  uint64_t _M0L6_2atmpS1820;
  struct _M0TUmmmmE* _M0L1tS756;
  uint64_t _M0L6_2atmpS1816;
  uint64_t _M0L6_2atmpS1817;
  uint64_t _M0L6_2atmpS1818;
  uint64_t _M0L6_2atmpS1819;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2068;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS754 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS755);
  _M0L6_2atmpS1820 = _M0L1sS754->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS756 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1820);
  _M0L6_2atmpS1816 = _M0L1sS754->$0;
  _M0L6_2atmpS1817 = _M0L1sS754->$1;
  _M0L6_2atmpS1818 = _M0L1sS754->$2;
  moonbit_decref_cycle_free(_M0L1sS754);
  _M0L6_2atmpS1819 = _M0L1tS756->$0;
  moonbit_decref_cycle_free(_M0L1tS756);
  _block_2068
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2068)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2068->$0 = _M0L6_2atmpS1816;
  _block_2068->$1 = _M0L6_2atmpS1817;
  _block_2068->$2 = _M0L6_2atmpS1818;
  _block_2068->$3 = _M0L6_2atmpS1819;
  return _block_2068;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS746) {
  uint64_t _M0L2s1S745;
  uint64_t _M0L2z1S747;
  uint64_t _M0L2s2S748;
  uint64_t _M0L2z2S749;
  uint64_t _M0L2s3S750;
  uint64_t _M0L2z3S751;
  uint64_t _M0L2s4S752;
  uint64_t _M0L2z4S753;
  struct _M0TUmmmmE* _block_2069;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S745 = _M0L4seedS746 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S747 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S745);
  _M0L2s2S748 = _M0L2s1S745 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S749 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S748);
  _M0L2s3S750 = _M0L2s2S748 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S751 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S750);
  _M0L2s4S752 = _M0L2s3S750 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S753 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S752);
  _block_2069 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2069)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2069->$0 = _M0L2z1S747;
  _block_2069->$1 = _M0L2z2S749;
  _block_2069->$2 = _M0L2z3S751;
  _block_2069->$3 = _M0L2z4S753;
  return _block_2069;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS743) {
  uint64_t _M0L6_2atmpS1815;
  uint64_t _M0L6_2atmpS1814;
  uint64_t _M0L1zS742;
  uint64_t _M0L6_2atmpS1813;
  uint64_t _M0L6_2atmpS1812;
  uint64_t _M0L1zS744;
  uint64_t _M0L6_2atmpS1811;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1815 = _M0L1zS743 >> 30;
  _M0L6_2atmpS1814 = _M0L1zS743 ^ _M0L6_2atmpS1815;
  _M0L1zS742 = _M0L6_2atmpS1814 * 13787848793156543929ull;
  _M0L6_2atmpS1813 = _M0L1zS742 >> 27;
  _M0L6_2atmpS1812 = _M0L1zS742 ^ _M0L6_2atmpS1813;
  _M0L1zS744 = _M0L6_2atmpS1812 * 10723151780598845931ull;
  _M0L6_2atmpS1811 = _M0L1zS744 >> 31;
  return _M0L1zS744 ^ _M0L6_2atmpS1811;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS737
) {
  double _M0L2u1S736;
  double _M0L8u1__safeS738;
  double _M0L2u2S739;
  double _M0L6_2atmpS1810;
  double _M0L6_2atmpS1809;
  double _M0L1rS740;
  double _M0L5thetaS741;
  double _M0L6_2atmpS1808;
  double _M0L6_2atmpS1805;
  double _M0L6_2atmpS1807;
  double _M0L6_2atmpS1806;
  struct _M0TUddE* _block_2070;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S736 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS737);
  if (_M0L2u1S736 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS738 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS738 = _M0L2u1S736;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S739 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS737);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1810 = _M0FPC14math2ln(_M0L8u1__safeS738);
  _M0L6_2atmpS1809 = -0x1p+1 * _M0L6_2atmpS1810;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS740 = sqrt(_M0L6_2atmpS1809);
  _M0L5thetaS741 = 0x1.921fb54442d18p+2 * _M0L2u2S739;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1808 = _M0FPC14math3cos(_M0L5thetaS741);
  _M0L6_2atmpS1805 = _M0L1rS740 * _M0L6_2atmpS1808;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1807 = _M0FPC14math3sin(_M0L5thetaS741);
  _M0L6_2atmpS1806 = _M0L1rS740 * _M0L6_2atmpS1807;
  _block_2070 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2070)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2070->$0 = _M0L6_2atmpS1805;
  _block_2070->$1 = _M0L6_2atmpS1806;
  return _block_2070;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS734
) {
  uint64_t _M0L1uS733;
  uint64_t _M0L4bitsS735;
  double _M0L6_2atmpS1804;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS733 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS734);
  _M0L4bitsS735 = _M0L1uS733 >> 11;
  _M0L6_2atmpS1804 = (double)_M0L4bitsS735;
  return _M0L6_2atmpS1804 * 0x1p-53;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS726
) {
  uint64_t _M0L2s0S725;
  uint64_t _M0L2s1S727;
  uint64_t _M0L2s2S728;
  uint64_t _M0L2s3S729;
  uint64_t _M0L3tmpS730;
  uint64_t _M0L6_2atmpS1803;
  uint64_t _M0L3resS731;
  uint64_t _M0L1tS732;
  uint64_t _M0L6_2atmpS1793;
  uint64_t _M0L6_2atmpS1794;
  uint64_t _M0L2s2S1796;
  uint64_t _M0L6_2atmpS1795;
  uint64_t _M0L2s3S1798;
  uint64_t _M0L6_2atmpS1797;
  uint64_t _M0L2s2S1800;
  uint64_t _M0L6_2atmpS1799;
  uint64_t _M0L2s3S1802;
  uint64_t _M0L6_2atmpS1801;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S725 = _M0L1rS726->$0;
  _M0L2s1S727 = _M0L1rS726->$1;
  _M0L2s2S728 = _M0L1rS726->$2;
  _M0L2s3S729 = _M0L1rS726->$3;
  _M0L3tmpS730 = _M0L2s0S725 + _M0L2s3S729;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1803 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS730, 23);
  _M0L3resS731 = _M0L6_2atmpS1803 + _M0L2s0S725;
  _M0L1tS732 = _M0L2s1S727 << 17;
  _M0L6_2atmpS1793 = _M0L2s2S728 ^ _M0L2s0S725;
  _M0L1rS726->$2 = _M0L6_2atmpS1793;
  _M0L6_2atmpS1794 = _M0L2s3S729 ^ _M0L2s1S727;
  _M0L1rS726->$3 = _M0L6_2atmpS1794;
  _M0L2s2S1796 = _M0L1rS726->$2;
  _M0L6_2atmpS1795 = _M0L2s1S727 ^ _M0L2s2S1796;
  _M0L1rS726->$1 = _M0L6_2atmpS1795;
  _M0L2s3S1798 = _M0L1rS726->$3;
  _M0L6_2atmpS1797 = _M0L2s0S725 ^ _M0L2s3S1798;
  _M0L1rS726->$0 = _M0L6_2atmpS1797;
  _M0L2s2S1800 = _M0L1rS726->$2;
  _M0L6_2atmpS1799 = _M0L2s2S1800 ^ _M0L1tS732;
  _M0L1rS726->$2 = _M0L6_2atmpS1799;
  _M0L2s3S1802 = _M0L1rS726->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1801 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1802, 45);
  _M0L1rS726->$3 = _M0L6_2atmpS1801;
  return _M0L3resS731;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS723, int32_t _M0L1kS724) {
  uint64_t _M0L6_2atmpS1790;
  int32_t _M0L6_2atmpS1792;
  uint64_t _M0L6_2atmpS1791;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1790 = _M0L1xS723 << (_M0L1kS724 & 63);
  _M0L6_2atmpS1792 = 64 - _M0L1kS724;
  _M0L6_2atmpS1791 = _M0L1xS723 >> (_M0L6_2atmpS1792 & 63);
  return _M0L6_2atmpS1790 | _M0L6_2atmpS1791;
}

double _M0FPC14math2ln(double _M0L1xS709) {
  struct _M0TUdiE* _M0L7_2abindS710;
  double _M0L5_2af1S711;
  int32_t _M0L5_2akiS712;
  double _M0L1fS714;
  double _M0L1kS715;
  double _M0L6_2atmpS1783;
  double _M0L1sS716;
  double _M0L2s2S717;
  double _M0L2s4S718;
  double _M0L6_2atmpS1782;
  double _M0L6_2atmpS1781;
  double _M0L6_2atmpS1780;
  double _M0L6_2atmpS1779;
  double _M0L6_2atmpS1778;
  double _M0L6_2atmpS1777;
  double _M0L2t1S719;
  double _M0L6_2atmpS1776;
  double _M0L6_2atmpS1775;
  double _M0L6_2atmpS1774;
  double _M0L6_2atmpS1773;
  double _M0L2t2S720;
  double _M0L1rS721;
  double _M0L6_2atmpS1772;
  double _M0L4hfsqS722;
  double _M0L6_2atmpS1765;
  double _M0L6_2atmpS1771;
  double _M0L6_2atmpS1769;
  double _M0L6_2atmpS1770;
  double _M0L6_2atmpS1768;
  double _M0L6_2atmpS1767;
  double _M0L6_2atmpS1766;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS709 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS709)
      || _M0MPC16double6Double7is__inf(_M0L1xS709)
    ) {
      return _M0L1xS709;
    } else if (_M0L1xS709 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS710 = _M0FPC14math5frexp(_M0L1xS709);
  _M0L5_2af1S711 = _M0L7_2abindS710->$0;
  _M0L5_2akiS712 = _M0L7_2abindS710->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS710);
  if (_M0L5_2af1S711 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1787 = _M0L5_2af1S711 * 0x1p+1;
    double _M0L6_2atmpS1784 = _M0L6_2atmpS1787 - 0x1p+0;
    int32_t _M0L6_2atmpS1786 = _M0L5_2akiS712 - 1;
    double _M0L6_2atmpS1785 = (double)_M0L6_2atmpS1786;
    _M0L1fS714 = _M0L6_2atmpS1784;
    _M0L1kS715 = _M0L6_2atmpS1785;
    goto join_713;
  } else {
    double _M0L6_2atmpS1788 = _M0L5_2af1S711 - 0x1p+0;
    double _M0L6_2atmpS1789 = (double)_M0L5_2akiS712;
    _M0L1fS714 = _M0L6_2atmpS1788;
    _M0L1kS715 = _M0L6_2atmpS1789;
    goto join_713;
  }
  join_713:;
  _M0L6_2atmpS1783 = 0x1p+1 + _M0L1fS714;
  _M0L1sS716 = _M0L1fS714 / _M0L6_2atmpS1783;
  _M0L2s2S717 = _M0L1sS716 * _M0L1sS716;
  _M0L2s4S718 = _M0L2s2S717 * _M0L2s2S717;
  _M0L6_2atmpS1782 = _M0L2s4S718 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1781 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1782;
  _M0L6_2atmpS1780 = _M0L2s4S718 * _M0L6_2atmpS1781;
  _M0L6_2atmpS1779 = 0x1.2492494229359p-2 + _M0L6_2atmpS1780;
  _M0L6_2atmpS1778 = _M0L2s4S718 * _M0L6_2atmpS1779;
  _M0L6_2atmpS1777 = 0x1.5555555555593p-1 + _M0L6_2atmpS1778;
  _M0L2t1S719 = _M0L2s2S717 * _M0L6_2atmpS1777;
  _M0L6_2atmpS1776 = _M0L2s4S718 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1775 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1776;
  _M0L6_2atmpS1774 = _M0L2s4S718 * _M0L6_2atmpS1775;
  _M0L6_2atmpS1773 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1774;
  _M0L2t2S720 = _M0L2s4S718 * _M0L6_2atmpS1773;
  _M0L1rS721 = _M0L2t1S719 + _M0L2t2S720;
  _M0L6_2atmpS1772 = 0x1p-1 * _M0L1fS714;
  _M0L4hfsqS722 = _M0L6_2atmpS1772 * _M0L1fS714;
  _M0L6_2atmpS1765 = _M0L1kS715 * 0x1.62e42feep-1;
  _M0L6_2atmpS1771 = _M0L4hfsqS722 + _M0L1rS721;
  _M0L6_2atmpS1769 = _M0L1sS716 * _M0L6_2atmpS1771;
  _M0L6_2atmpS1770 = _M0L1kS715 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1768 = _M0L6_2atmpS1769 + _M0L6_2atmpS1770;
  _M0L6_2atmpS1767 = _M0L4hfsqS722 - _M0L6_2atmpS1768;
  _M0L6_2atmpS1766 = _M0L6_2atmpS1767 - _M0L1fS714;
  return _M0L6_2atmpS1765 - _M0L6_2atmpS1766;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS702) {
  struct _M0TUdiE* _M0L7_2abindS703;
  double _M0L10_2anorm__fS704;
  int32_t _M0L6_2aexpS705;
  uint64_t _M0L1uS706;
  uint64_t _M0L6_2atmpS1764;
  uint64_t _M0L6_2atmpS1763;
  int32_t _M0L6_2atmpS1762;
  int32_t _M0L6_2atmpS1761;
  int32_t _M0L3expS707;
  uint64_t _M0L6_2atmpS1760;
  uint64_t _M0L6_2atmpS1759;
  uint64_t _M0L6_2atmpS1758;
  double _M0L4fracS708;
  struct _M0TUdiE* _block_2073;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS702 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS702)
    || _M0MPC16double6Double7is__nan(_M0L1fS702)
  ) {
    struct _M0TUdiE* _block_2072 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2072)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2072->$0 = _M0L1fS702;
    _block_2072->$1 = 0;
    return _block_2072;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS703 = _M0FPC14math9normalize(_M0L1fS702);
  _M0L10_2anorm__fS704 = _M0L7_2abindS703->$0;
  _M0L6_2aexpS705 = _M0L7_2abindS703->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS703);
  _M0L1uS706 = *(int64_t*)&_M0L10_2anorm__fS704;
  _M0L6_2atmpS1764 = _M0L1uS706 >> 52;
  _M0L6_2atmpS1763 = _M0L6_2atmpS1764 & 2047ull;
  _M0L6_2atmpS1762 = (int32_t)_M0L6_2atmpS1763;
  _M0L6_2atmpS1761 = _M0L6_2aexpS705 + _M0L6_2atmpS1762;
  _M0L3expS707 = _M0L6_2atmpS1761 - 1022;
  _M0L6_2atmpS1760 = ~9218868437227405312ull;
  _M0L6_2atmpS1759 = _M0L1uS706 & _M0L6_2atmpS1760;
  _M0L6_2atmpS1758 = _M0L6_2atmpS1759 | 4602678819172646912ull;
  _M0L4fracS708 = *(double*)&_M0L6_2atmpS1758;
  _block_2073 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2073)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2073->$0 = _M0L4fracS708;
  _block_2073->$1 = _M0L3expS707;
  return _block_2073;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS701) {
  double _M0L6_2atmpS1755;
  struct _M0TUdiE* _block_2075;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1755 = fabs(_M0L1fS701);
  if (_M0L6_2atmpS1755 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1757 = (double)4503599627370496ll;
    double _M0L6_2atmpS1756 = _M0L1fS701 * _M0L6_2atmpS1757;
    struct _M0TUdiE* _block_2074 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2074)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2074->$0 = _M0L6_2atmpS1756;
    _block_2074->$1 = -52;
    return _block_2074;
  }
  _block_2075 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2075)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2075->$0 = _M0L1fS701;
  _block_2075->$1 = 0;
  return _block_2075;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS700) {
  double _M0L6_2atmpS1754;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1754 = (double)_M0L4selfS700;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1754);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS699) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS699 != _M0L4selfS699) {
    return 0;
  } else if (_M0L4selfS699 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS699 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS699;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS690,
  float _M0L4elemS692
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS689;
  int32_t _M0L1iS691;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS689 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS690);
  _M0L1iS691 = 0;
  while (1) {
    if (_M0L1iS691 < _M0L3lenS690) {
      float* _M0L3bufS1750 = _M0L3arrS689->$0;
      int32_t _M0L6_2atmpS1751;
      _M0L3bufS1750[_M0L1iS691] = _M0L4elemS692;
      _M0L6_2atmpS1751 = _M0L1iS691 + 1;
      _M0L1iS691 = _M0L6_2atmpS1751;
      continue;
    }
    break;
  }
  return _M0L3arrS689;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS695,
  int32_t _M0L4elemS697
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS694;
  int32_t _M0L1iS696;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS694 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS695);
  _M0L1iS696 = 0;
  while (1) {
    if (_M0L1iS696 < _M0L3lenS695) {
      uint8_t* _M0L3bufS1752 = _M0L3arrS694->$0;
      int32_t _M0L6_2atmpS1753;
      _M0L3bufS1752[_M0L1iS696] = _M0L4elemS697;
      _M0L6_2atmpS1753 = _M0L1iS696 + 1;
      _M0L1iS696 = _M0L6_2atmpS1753;
      continue;
    }
    break;
  }
  return _M0L3arrS694;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS682,
  int32_t _M0L5indexS683,
  float _M0L5valueS684
) {
  int32_t _M0L3lenS681;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS681 = _M0L4selfS682->$1;
  if (_M0L5indexS683 >= 0 && _M0L5indexS683 < _M0L3lenS681) {
    float* _M0L6_2atmpS1748;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1748 = _M0MPC15array5Array6bufferGfE(_M0L4selfS682);
    _M0L6_2atmpS1748[_M0L5indexS683] = _M0L5valueS684;
    moonbit_decref_cycle_free(_M0L6_2atmpS1748);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS686,
  int32_t _M0L5indexS687,
  int32_t _M0L5valueS688
) {
  int32_t _M0L3lenS685;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS685 = _M0L4selfS686->$1;
  if (_M0L5indexS687 >= 0 && _M0L5indexS687 < _M0L3lenS685) {
    uint8_t* _M0L6_2atmpS1749;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1749 = _M0MPC15array5Array6bufferGbE(_M0L4selfS686);
    _M0L6_2atmpS1749[_M0L5indexS687] = _M0L5valueS688;
    moonbit_decref_cycle_free(_M0L6_2atmpS1749);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS673,
  int32_t _M0L5indexS674
) {
  int32_t _M0L3lenS672;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS672 = _M0L4selfS673->$1;
  if (_M0L5indexS674 >= 0 && _M0L5indexS674 < _M0L3lenS672) {
    float* _M0L6_2atmpS1745;
    float _result_2078;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1745 = _M0MPC15array5Array6bufferGfE(_M0L4selfS673);
    _result_2078 = (float)_M0L6_2atmpS1745[_M0L5indexS674];
    moonbit_decref_cycle_free(_M0L6_2atmpS1745);
    return _result_2078;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS676,
  int32_t _M0L5indexS677
) {
  int32_t _M0L3lenS675;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS675 = _M0L4selfS676->$1;
  if (_M0L5indexS677 >= 0 && _M0L5indexS677 < _M0L3lenS675) {
    uint8_t* _M0L6_2atmpS1746;
    int32_t _result_2079;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1746 = _M0MPC15array5Array6bufferGbE(_M0L4selfS676);
    _result_2079 = (int32_t)_M0L6_2atmpS1746[_M0L5indexS677];
    moonbit_decref_cycle_free(_M0L6_2atmpS1746);
    return _result_2079;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS679,
  int32_t _M0L5indexS680
) {
  int32_t _M0L3lenS678;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS678 = _M0L4selfS679->$1;
  if (_M0L5indexS680 >= 0 && _M0L5indexS680 < _M0L3lenS678) {
    moonbit_string_t* _M0L6_2atmpS1747;
    moonbit_string_t _M0L6_2atmpS2011;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1747 = _M0MPC15array5Array6bufferGsE(_M0L4selfS679);
    _M0L6_2atmpS2011 = (moonbit_string_t)_M0L6_2atmpS1747[_M0L5indexS680];
    moonbit_incref_cycle_free(_M0L6_2atmpS2011);
    moonbit_decref_cycle_free(_M0L6_2atmpS1747);
    return _M0L6_2atmpS2011;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS671) {
  moonbit_string_t _M0L6_2atmpS1744;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1744 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS671);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1744);
  moonbit_decref_cycle_free(_M0L6_2atmpS1744);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS670) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS670);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS669) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS669 > _M0FPB18double__max__value
         || _M0L4selfS669 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS668) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS668 != _M0L4selfS668;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS653) {
  uint64_t _M0L4bitsS656;
  uint64_t _M0L6_2atmpS1743;
  uint64_t _M0L6_2atmpS1742;
  int32_t _M0L8ieeeSignS657;
  uint64_t _M0L12ieeeMantissaS658;
  uint64_t _M0L6_2atmpS1741;
  uint64_t _M0L6_2atmpS1740;
  int32_t _M0L12ieeeExponentS659;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS660;
  struct _M0TPB17FloatingDecimal64* _M0L1vS661;
  moonbit_string_t _result_2081;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS653 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS653 >= -0x1p+53 && _M0L3valS653 <= 0x1p+53) {
    if (_M0L3valS653 >= -0x1p+31 && _M0L3valS653 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS654;
      double _M0L6_2atmpS1729;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS654 = _M0MPC16double6Double7to__int(_M0L3valS653);
      _M0L6_2atmpS1729 = (double)_M0L1iS654;
      if (_M0L6_2atmpS1729 == _M0L3valS653) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS654, 10);
      }
    } else {
      int64_t _M0L1iS655;
      double _M0L6_2atmpS1730;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS655 = _M0MPC16double6Double9to__int64(_M0L3valS653);
      _M0L6_2atmpS1730 = (double)_M0L1iS655;
      if (_M0L6_2atmpS1730 == _M0L3valS653) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS655, 10);
      }
    }
  }
  _M0L4bitsS656 = *(int64_t*)&_M0L3valS653;
  _M0L6_2atmpS1743 = _M0L4bitsS656 >> 63;
  _M0L6_2atmpS1742 = _M0L6_2atmpS1743 & 1ull;
  _M0L8ieeeSignS657 = _M0L6_2atmpS1742 != 0ull;
  _M0L12ieeeMantissaS658 = _M0L4bitsS656 & 4503599627370495ull;
  _M0L6_2atmpS1741 = _M0L4bitsS656 >> 52;
  _M0L6_2atmpS1740 = _M0L6_2atmpS1741 & 2047ull;
  _M0L12ieeeExponentS659 = (int32_t)_M0L6_2atmpS1740;
  if (
    _M0L12ieeeExponentS659 == 2047
    || _M0L12ieeeExponentS659 == 0 && _M0L12ieeeMantissaS658 == 0ull
  ) {
    int32_t _M0L6_2atmpS1731 = _M0L12ieeeExponentS659 != 0;
    int32_t _M0L6_2atmpS1732 = _M0L12ieeeMantissaS658 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS657, _M0L6_2atmpS1731, _M0L6_2atmpS1732);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS660
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS658, _M0L12ieeeExponentS659);
  if (_M0L7_2abindS660 == 0) {
    uint32_t _M0L6_2atmpS1733;
    if (_M0L7_2abindS660) {
      moonbit_decref_cycle_free(_M0L7_2abindS660);
    }
    _M0L6_2atmpS1733 = *(uint32_t*)&_M0L12ieeeExponentS659;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS661 = _M0FPB3d2d(_M0L12ieeeMantissaS658, _M0L6_2atmpS1733);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS662 = _M0L7_2abindS660;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS663 = _M0L7_2aSomeS662;
    struct _M0TPB17FloatingDecimal64* _M0L1xS664 = _M0L4_2afS663;
    while (1) {
      uint64_t _M0L8mantissaS1739 = _M0L1xS664->$0;
      uint64_t _M0L1qS665 = _M0L8mantissaS1739 / 10ull;
      uint64_t _M0L8mantissaS1737 = _M0L1xS664->$0;
      uint64_t _M0L6_2atmpS1738 = 10ull * _M0L1qS665;
      uint64_t _M0L1rS666 = _M0L8mantissaS1737 - _M0L6_2atmpS1738;
      int32_t _M0L8exponentS1736;
      int32_t _M0L6_2atmpS1735;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1734;
      if (_M0L1rS666 != 0ull) {
        _M0L1vS661 = _M0L1xS664;
        break;
      }
      _M0L8exponentS1736 = _M0L1xS664->$1;
      moonbit_decref_cycle_free(_M0L1xS664);
      _M0L6_2atmpS1735 = _M0L8exponentS1736 + 1;
      _M0L6_2atmpS1734
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1734)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1734->$0 = _M0L1qS665;
      _M0L6_2atmpS1734->$1 = _M0L6_2atmpS1735;
      _M0L1xS664 = _M0L6_2atmpS1734;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2081 = _M0FPB9to__chars(_M0L1vS661, _M0L8ieeeSignS657);
  moonbit_decref_cycle_free(_M0L1vS661);
  return _result_2081;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS648,
  int32_t _M0L12ieeeExponentS650
) {
  uint64_t _M0L2m2S647;
  int32_t _M0L6_2atmpS1728;
  int32_t _M0L2e2S649;
  int32_t _M0L6_2atmpS1727;
  uint64_t _M0L6_2atmpS1726;
  uint64_t _M0L4maskS651;
  uint64_t _M0L8fractionS652;
  int32_t _M0L6_2atmpS1725;
  uint64_t _M0L6_2atmpS1724;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1723;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S647 = 4503599627370496ull | _M0L12ieeeMantissaS648;
  _M0L6_2atmpS1728 = _M0L12ieeeExponentS650 - 1023;
  _M0L2e2S649 = _M0L6_2atmpS1728 - 52;
  if (_M0L2e2S649 > 0) {
    return 0;
  }
  if (_M0L2e2S649 < -52) {
    return 0;
  }
  _M0L6_2atmpS1727 = -_M0L2e2S649;
  _M0L6_2atmpS1726 = 1ull << (_M0L6_2atmpS1727 & 63);
  _M0L4maskS651 = _M0L6_2atmpS1726 - 1ull;
  _M0L8fractionS652 = _M0L2m2S647 & _M0L4maskS651;
  if (_M0L8fractionS652 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1725 = -_M0L2e2S649;
  _M0L6_2atmpS1724 = _M0L2m2S647 >> (_M0L6_2atmpS1725 & 63);
  _M0L6_2atmpS1723
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1723)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1723->$0 = _M0L6_2atmpS1724;
  _M0L6_2atmpS1723->$1 = 0;
  return _M0L6_2atmpS1723;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS615,
  int32_t _M0L4signS613
) {
  moonbit_bytes_t _M0L6resultS611;
  int32_t _M0Lm5indexS612;
  uint64_t _M0L6outputS614;
  int32_t _M0L7olengthS616;
  int32_t _M0L8exponentS1722;
  int32_t _M0L6_2atmpS1721;
  int32_t _M0Lm3expS617;
  int32_t _M0L6_2atmpS1720;
  int32_t _M0L6_2atmpS1718;
  int32_t _M0L18scientificNotationS618;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS611 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS612 = 0;
  if (_M0L4signS613) {
    int32_t _M0L6_2atmpS1592 = _M0Lm5indexS612;
    int32_t _M0L6_2atmpS1593;
    if (
      _M0L6_2atmpS1592 < 0
      || _M0L6_2atmpS1592 >= Moonbit_array_length(_M0L6resultS611)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS611[_M0L6_2atmpS1592] = 45;
    _M0L6_2atmpS1593 = _M0Lm5indexS612;
    _M0Lm5indexS612 = _M0L6_2atmpS1593 + 1;
  }
  _M0L6outputS614 = _M0L1vS615->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS616 = _M0FPB17decimal__length17(_M0L6outputS614);
  _M0L8exponentS1722 = _M0L1vS615->$1;
  _M0L6_2atmpS1721 = _M0L8exponentS1722 + _M0L7olengthS616;
  _M0Lm3expS617 = _M0L6_2atmpS1721 - 1;
  _M0L6_2atmpS1720 = _M0Lm3expS617;
  if (_M0L6_2atmpS1720 >= -6) {
    int32_t _M0L6_2atmpS1719 = _M0Lm3expS617;
    _M0L6_2atmpS1718 = _M0L6_2atmpS1719 < 21;
  } else {
    _M0L6_2atmpS1718 = 0;
  }
  _M0L18scientificNotationS618 = !_M0L6_2atmpS1718;
  if (_M0L18scientificNotationS618) {
    int32_t _M0L7_2abindS619 = _M0L7olengthS616 - 1;
    uint64_t _M0L6outputS620;
    int32_t _M0L1iS621 = 0;
    uint64_t _M0L6outputS622 = _M0L6outputS614;
    int32_t _M0L6_2atmpS1594;
    int32_t _M0L6_2atmpS1598;
    int32_t _M0L6_2atmpS1597;
    int32_t _M0L6_2atmpS1596;
    int32_t _M0L6_2atmpS1595;
    int32_t _M0L6_2atmpS1602;
    int32_t _M0L6_2atmpS1603;
    int32_t _M0L6_2atmpS1604;
    int32_t _M0L6_2atmpS1605;
    int32_t _M0L6_2atmpS1606;
    int32_t _M0L6_2atmpS1612;
    int32_t _M0L6_2atmpS1645;
    moonbit_string_t _result_2083;
    while (1) {
      if (_M0L1iS621 < _M0L7_2abindS619) {
        uint64_t _M0L1cS623 = _M0L6outputS622 % 10ull;
        int32_t _M0L6_2atmpS1651 = _M0Lm5indexS612;
        int32_t _M0L6_2atmpS1650 = _M0L6_2atmpS1651 + _M0L7olengthS616;
        int32_t _M0L6_2atmpS1646 = _M0L6_2atmpS1650 - _M0L1iS621;
        int32_t _M0L6_2atmpS1649 = (int32_t)_M0L1cS623;
        int32_t _M0L6_2atmpS1648 = 48 + _M0L6_2atmpS1649;
        int32_t _M0L6_2atmpS1647 = _M0L6_2atmpS1648 & 0xff;
        int32_t _M0L6_2atmpS1652;
        uint64_t _M0L6_2atmpS1653;
        if (
          _M0L6_2atmpS1646 < 0
          || _M0L6_2atmpS1646 >= Moonbit_array_length(_M0L6resultS611)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS611[_M0L6_2atmpS1646] = _M0L6_2atmpS1647;
        _M0L6_2atmpS1652 = _M0L1iS621 + 1;
        _M0L6_2atmpS1653 = _M0L6outputS622 / 10ull;
        _M0L1iS621 = _M0L6_2atmpS1652;
        _M0L6outputS622 = _M0L6_2atmpS1653;
        continue;
      } else {
        _M0L6outputS620 = _M0L6outputS622;
      }
      break;
    }
    _M0L6_2atmpS1594 = _M0Lm5indexS612;
    _M0L6_2atmpS1598 = (int32_t)_M0L6outputS620;
    _M0L6_2atmpS1597 = _M0L6_2atmpS1598 % 10;
    _M0L6_2atmpS1596 = 48 + _M0L6_2atmpS1597;
    _M0L6_2atmpS1595 = _M0L6_2atmpS1596 & 0xff;
    if (
      _M0L6_2atmpS1594 < 0
      || _M0L6_2atmpS1594 >= Moonbit_array_length(_M0L6resultS611)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS611[_M0L6_2atmpS1594] = _M0L6_2atmpS1595;
    if (_M0L7olengthS616 > 1) {
      int32_t _M0L6_2atmpS1600 = _M0Lm5indexS612;
      int32_t _M0L6_2atmpS1599 = _M0L6_2atmpS1600 + 1;
      if (
        _M0L6_2atmpS1599 < 0
        || _M0L6_2atmpS1599 >= Moonbit_array_length(_M0L6resultS611)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS611[_M0L6_2atmpS1599] = 46;
    } else {
      int32_t _M0L6_2atmpS1601 = _M0Lm5indexS612;
      _M0Lm5indexS612 = _M0L6_2atmpS1601 - 1;
    }
    _M0L6_2atmpS1602 = _M0Lm5indexS612;
    _M0L6_2atmpS1603 = _M0L7olengthS616 + 1;
    _M0Lm5indexS612 = _M0L6_2atmpS1602 + _M0L6_2atmpS1603;
    _M0L6_2atmpS1604 = _M0Lm5indexS612;
    if (
      _M0L6_2atmpS1604 < 0
      || _M0L6_2atmpS1604 >= Moonbit_array_length(_M0L6resultS611)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS611[_M0L6_2atmpS1604] = 101;
    _M0L6_2atmpS1605 = _M0Lm5indexS612;
    _M0Lm5indexS612 = _M0L6_2atmpS1605 + 1;
    _M0L6_2atmpS1606 = _M0Lm3expS617;
    if (_M0L6_2atmpS1606 < 0) {
      int32_t _M0L6_2atmpS1607 = _M0Lm5indexS612;
      int32_t _M0L6_2atmpS1608;
      int32_t _M0L6_2atmpS1609;
      if (
        _M0L6_2atmpS1607 < 0
        || _M0L6_2atmpS1607 >= Moonbit_array_length(_M0L6resultS611)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS611[_M0L6_2atmpS1607] = 45;
      _M0L6_2atmpS1608 = _M0Lm5indexS612;
      _M0Lm5indexS612 = _M0L6_2atmpS1608 + 1;
      _M0L6_2atmpS1609 = _M0Lm3expS617;
      _M0Lm3expS617 = -_M0L6_2atmpS1609;
    } else {
      int32_t _M0L6_2atmpS1610 = _M0Lm5indexS612;
      int32_t _M0L6_2atmpS1611;
      if (
        _M0L6_2atmpS1610 < 0
        || _M0L6_2atmpS1610 >= Moonbit_array_length(_M0L6resultS611)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS611[_M0L6_2atmpS1610] = 43;
      _M0L6_2atmpS1611 = _M0Lm5indexS612;
      _M0Lm5indexS612 = _M0L6_2atmpS1611 + 1;
    }
    _M0L6_2atmpS1612 = _M0Lm3expS617;
    if (_M0L6_2atmpS1612 >= 100) {
      int32_t _M0L6_2atmpS1628 = _M0Lm3expS617;
      int32_t _M0L1aS625 = _M0L6_2atmpS1628 / 100;
      int32_t _M0L6_2atmpS1627 = _M0Lm3expS617;
      int32_t _M0L6_2atmpS1626 = _M0L6_2atmpS1627 / 10;
      int32_t _M0L1bS626 = _M0L6_2atmpS1626 % 10;
      int32_t _M0L6_2atmpS1625 = _M0Lm3expS617;
      int32_t _M0L1cS627 = _M0L6_2atmpS1625 % 10;
      int32_t _M0L6_2atmpS1613 = _M0Lm5indexS612;
      int32_t _M0L6_2atmpS1615 = 48 + _M0L1aS625;
      int32_t _M0L6_2atmpS1614 = _M0L6_2atmpS1615 & 0xff;
      int32_t _M0L6_2atmpS1619;
      int32_t _M0L6_2atmpS1616;
      int32_t _M0L6_2atmpS1618;
      int32_t _M0L6_2atmpS1617;
      int32_t _M0L6_2atmpS1623;
      int32_t _M0L6_2atmpS1620;
      int32_t _M0L6_2atmpS1622;
      int32_t _M0L6_2atmpS1621;
      int32_t _M0L6_2atmpS1624;
      if (
        _M0L6_2atmpS1613 < 0
        || _M0L6_2atmpS1613 >= Moonbit_array_length(_M0L6resultS611)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS611[_M0L6_2atmpS1613] = _M0L6_2atmpS1614;
      _M0L6_2atmpS1619 = _M0Lm5indexS612;
      _M0L6_2atmpS1616 = _M0L6_2atmpS1619 + 1;
      _M0L6_2atmpS1618 = 48 + _M0L1bS626;
      _M0L6_2atmpS1617 = _M0L6_2atmpS1618 & 0xff;
      if (
        _M0L6_2atmpS1616 < 0
        || _M0L6_2atmpS1616 >= Moonbit_array_length(_M0L6resultS611)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS611[_M0L6_2atmpS1616] = _M0L6_2atmpS1617;
      _M0L6_2atmpS1623 = _M0Lm5indexS612;
      _M0L6_2atmpS1620 = _M0L6_2atmpS1623 + 2;
      _M0L6_2atmpS1622 = 48 + _M0L1cS627;
      _M0L6_2atmpS1621 = _M0L6_2atmpS1622 & 0xff;
      if (
        _M0L6_2atmpS1620 < 0
        || _M0L6_2atmpS1620 >= Moonbit_array_length(_M0L6resultS611)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS611[_M0L6_2atmpS1620] = _M0L6_2atmpS1621;
      _M0L6_2atmpS1624 = _M0Lm5indexS612;
      _M0Lm5indexS612 = _M0L6_2atmpS1624 + 3;
    } else {
      int32_t _M0L6_2atmpS1629 = _M0Lm3expS617;
      if (_M0L6_2atmpS1629 >= 10) {
        int32_t _M0L6_2atmpS1639 = _M0Lm3expS617;
        int32_t _M0L1aS628 = _M0L6_2atmpS1639 / 10;
        int32_t _M0L6_2atmpS1638 = _M0Lm3expS617;
        int32_t _M0L1bS629 = _M0L6_2atmpS1638 % 10;
        int32_t _M0L6_2atmpS1630 = _M0Lm5indexS612;
        int32_t _M0L6_2atmpS1632 = 48 + _M0L1aS628;
        int32_t _M0L6_2atmpS1631 = _M0L6_2atmpS1632 & 0xff;
        int32_t _M0L6_2atmpS1636;
        int32_t _M0L6_2atmpS1633;
        int32_t _M0L6_2atmpS1635;
        int32_t _M0L6_2atmpS1634;
        int32_t _M0L6_2atmpS1637;
        if (
          _M0L6_2atmpS1630 < 0
          || _M0L6_2atmpS1630 >= Moonbit_array_length(_M0L6resultS611)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS611[_M0L6_2atmpS1630] = _M0L6_2atmpS1631;
        _M0L6_2atmpS1636 = _M0Lm5indexS612;
        _M0L6_2atmpS1633 = _M0L6_2atmpS1636 + 1;
        _M0L6_2atmpS1635 = 48 + _M0L1bS629;
        _M0L6_2atmpS1634 = _M0L6_2atmpS1635 & 0xff;
        if (
          _M0L6_2atmpS1633 < 0
          || _M0L6_2atmpS1633 >= Moonbit_array_length(_M0L6resultS611)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS611[_M0L6_2atmpS1633] = _M0L6_2atmpS1634;
        _M0L6_2atmpS1637 = _M0Lm5indexS612;
        _M0Lm5indexS612 = _M0L6_2atmpS1637 + 2;
      } else {
        int32_t _M0L6_2atmpS1640 = _M0Lm5indexS612;
        int32_t _M0L6_2atmpS1643 = _M0Lm3expS617;
        int32_t _M0L6_2atmpS1642 = 48 + _M0L6_2atmpS1643;
        int32_t _M0L6_2atmpS1641 = _M0L6_2atmpS1642 & 0xff;
        int32_t _M0L6_2atmpS1644;
        if (
          _M0L6_2atmpS1640 < 0
          || _M0L6_2atmpS1640 >= Moonbit_array_length(_M0L6resultS611)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS611[_M0L6_2atmpS1640] = _M0L6_2atmpS1641;
        _M0L6_2atmpS1644 = _M0Lm5indexS612;
        _M0Lm5indexS612 = _M0L6_2atmpS1644 + 1;
      }
    }
    _M0L6_2atmpS1645 = _M0Lm5indexS612;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2083
    = _M0FPB19string__from__bytes(_M0L6resultS611, 0, _M0L6_2atmpS1645);
    moonbit_decref_cycle_free(_M0L6resultS611);
    return _result_2083;
  } else {
    int32_t _M0L6_2atmpS1654 = _M0Lm3expS617;
    int32_t _M0L6_2atmpS1717;
    moonbit_string_t _result_2089;
    if (_M0L6_2atmpS1654 < 0) {
      int32_t _M0L6_2atmpS1655 = _M0Lm5indexS612;
      int32_t _M0L6_2atmpS1657;
      int32_t _M0L6_2atmpS1656;
      int32_t _M0L6_2atmpS1658;
      int32_t _M0L1iS630;
      int32_t _M0L6_2atmpS1673;
      int32_t _M0L6_2atmpS1675;
      int32_t _M0L6_2atmpS1674;
      int32_t _M0L7currentS632;
      int32_t _M0L1iS633;
      uint64_t _M0L6outputS634;
      if (
        _M0L6_2atmpS1655 < 0
        || _M0L6_2atmpS1655 >= Moonbit_array_length(_M0L6resultS611)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS611[_M0L6_2atmpS1655] = 48;
      _M0L6_2atmpS1657 = _M0Lm5indexS612;
      _M0L6_2atmpS1656 = _M0L6_2atmpS1657 + 1;
      if (
        _M0L6_2atmpS1656 < 0
        || _M0L6_2atmpS1656 >= Moonbit_array_length(_M0L6resultS611)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS611[_M0L6_2atmpS1656] = 46;
      _M0L6_2atmpS1658 = _M0Lm5indexS612;
      _M0Lm5indexS612 = _M0L6_2atmpS1658 + 2;
      _M0L1iS630 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1659 = _M0Lm3expS617;
        if (_M0L1iS630 > _M0L6_2atmpS1659) {
          int32_t _M0L6_2atmpS1662 = _M0Lm5indexS612;
          int32_t _M0L6_2atmpS1661 = _M0L6_2atmpS1662 - _M0L1iS630;
          int32_t _M0L6_2atmpS1660 = _M0L6_2atmpS1661 - 1;
          int32_t _M0L6_2atmpS1663;
          if (
            _M0L6_2atmpS1660 < 0
            || _M0L6_2atmpS1660 >= Moonbit_array_length(_M0L6resultS611)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS611[_M0L6_2atmpS1660] = 48;
          _M0L6_2atmpS1663 = _M0L1iS630 - 1;
          _M0L1iS630 = _M0L6_2atmpS1663;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1673 = _M0Lm5indexS612;
      _M0L6_2atmpS1675 = _M0Lm3expS617;
      _M0L6_2atmpS1674 = -1 - _M0L6_2atmpS1675;
      _M0L7currentS632 = _M0L6_2atmpS1673 + _M0L6_2atmpS1674;
      _M0L1iS633 = 0;
      _M0L6outputS634 = _M0L6outputS614;
      while (1) {
        if (_M0L1iS633 < _M0L7olengthS616) {
          int32_t _M0L6_2atmpS1670 = _M0L7currentS632 + _M0L7olengthS616;
          int32_t _M0L6_2atmpS1669 = _M0L6_2atmpS1670 - _M0L1iS633;
          int32_t _M0L6_2atmpS1664 = _M0L6_2atmpS1669 - 1;
          uint64_t _M0L6_2atmpS1668 = _M0L6outputS634 % 10ull;
          int32_t _M0L6_2atmpS1667 = (int32_t)_M0L6_2atmpS1668;
          int32_t _M0L6_2atmpS1666 = 48 + _M0L6_2atmpS1667;
          int32_t _M0L6_2atmpS1665 = _M0L6_2atmpS1666 & 0xff;
          int32_t _M0L6_2atmpS1671;
          uint64_t _M0L6_2atmpS1672;
          if (
            _M0L6_2atmpS1664 < 0
            || _M0L6_2atmpS1664 >= Moonbit_array_length(_M0L6resultS611)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS611[_M0L6_2atmpS1664] = _M0L6_2atmpS1665;
          _M0L6_2atmpS1671 = _M0L1iS633 + 1;
          _M0L6_2atmpS1672 = _M0L6outputS634 / 10ull;
          _M0L1iS633 = _M0L6_2atmpS1671;
          _M0L6outputS634 = _M0L6_2atmpS1672;
          continue;
        }
        break;
      }
      _M0Lm5indexS612 = _M0L7currentS632 + _M0L7olengthS616;
    } else {
      int32_t _M0L6_2atmpS1677 = _M0Lm3expS617;
      int32_t _M0L6_2atmpS1676 = _M0L6_2atmpS1677 + 1;
      if (_M0L6_2atmpS1676 >= _M0L7olengthS616) {
        int32_t _M0L1iS636 = 0;
        uint64_t _M0L6outputS637 = _M0L6outputS614;
        int32_t _M0L6_2atmpS1688;
        int32_t _M0L6_2atmpS1693;
        int32_t _M0L7_2abindS639;
        int32_t _M0L1iS640;
        int32_t _M0L6_2atmpS1694;
        int32_t _M0L6_2atmpS1697;
        int32_t _M0L6_2atmpS1696;
        int32_t _M0L6_2atmpS1695;
        while (1) {
          if (_M0L1iS636 < _M0L7olengthS616) {
            int32_t _M0L6_2atmpS1685 = _M0Lm5indexS612;
            int32_t _M0L6_2atmpS1684 = _M0L6_2atmpS1685 + _M0L7olengthS616;
            int32_t _M0L6_2atmpS1683 = _M0L6_2atmpS1684 - _M0L1iS636;
            int32_t _M0L6_2atmpS1678 = _M0L6_2atmpS1683 - 1;
            uint64_t _M0L6_2atmpS1682 = _M0L6outputS637 % 10ull;
            int32_t _M0L6_2atmpS1681 = (int32_t)_M0L6_2atmpS1682;
            int32_t _M0L6_2atmpS1680 = 48 + _M0L6_2atmpS1681;
            int32_t _M0L6_2atmpS1679 = _M0L6_2atmpS1680 & 0xff;
            int32_t _M0L6_2atmpS1686;
            uint64_t _M0L6_2atmpS1687;
            if (
              _M0L6_2atmpS1678 < 0
              || _M0L6_2atmpS1678 >= Moonbit_array_length(_M0L6resultS611)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS611[_M0L6_2atmpS1678] = _M0L6_2atmpS1679;
            _M0L6_2atmpS1686 = _M0L1iS636 + 1;
            _M0L6_2atmpS1687 = _M0L6outputS637 / 10ull;
            _M0L1iS636 = _M0L6_2atmpS1686;
            _M0L6outputS637 = _M0L6_2atmpS1687;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1688 = _M0Lm5indexS612;
        _M0Lm5indexS612 = _M0L6_2atmpS1688 + _M0L7olengthS616;
        _M0L6_2atmpS1693 = _M0Lm3expS617;
        _M0L7_2abindS639 = _M0L6_2atmpS1693 + 1;
        _M0L1iS640 = _M0L7olengthS616;
        while (1) {
          if (_M0L1iS640 < _M0L7_2abindS639) {
            int32_t _M0L6_2atmpS1691 = _M0Lm5indexS612;
            int32_t _M0L6_2atmpS1690 = _M0L6_2atmpS1691 + _M0L1iS640;
            int32_t _M0L6_2atmpS1689 = _M0L6_2atmpS1690 - _M0L7olengthS616;
            int32_t _M0L6_2atmpS1692;
            if (
              _M0L6_2atmpS1689 < 0
              || _M0L6_2atmpS1689 >= Moonbit_array_length(_M0L6resultS611)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS611[_M0L6_2atmpS1689] = 48;
            _M0L6_2atmpS1692 = _M0L1iS640 + 1;
            _M0L1iS640 = _M0L6_2atmpS1692;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1694 = _M0Lm5indexS612;
        _M0L6_2atmpS1697 = _M0Lm3expS617;
        _M0L6_2atmpS1696 = _M0L6_2atmpS1697 + 1;
        _M0L6_2atmpS1695 = _M0L6_2atmpS1696 - _M0L7olengthS616;
        _M0Lm5indexS612 = _M0L6_2atmpS1694 + _M0L6_2atmpS1695;
      } else {
        int32_t _M0L6_2atmpS1714 = _M0Lm5indexS612;
        int32_t _M0L6_2atmpS1713 = _M0L6_2atmpS1714 + 1;
        int32_t _M0L1iS642 = 0;
        int32_t _M0L7currentS643 = _M0L6_2atmpS1713;
        uint64_t _M0L6outputS644 = _M0L6outputS614;
        int32_t _M0L6_2atmpS1715;
        int32_t _M0L6_2atmpS1716;
        while (1) {
          if (_M0L1iS642 < _M0L7olengthS616) {
            int32_t _M0L6_2atmpS1709 = _M0L7olengthS616 - _M0L1iS642;
            int32_t _M0L6_2atmpS1707 = _M0L6_2atmpS1709 - 1;
            int32_t _M0L6_2atmpS1708 = _M0Lm3expS617;
            int32_t _M0L7currentS645;
            int32_t _M0L6_2atmpS1704;
            int32_t _M0L6_2atmpS1703;
            int32_t _M0L6_2atmpS1698;
            uint64_t _M0L6_2atmpS1702;
            int32_t _M0L6_2atmpS1701;
            int32_t _M0L6_2atmpS1700;
            int32_t _M0L6_2atmpS1699;
            int32_t _M0L6_2atmpS1705;
            uint64_t _M0L6_2atmpS1706;
            if (_M0L6_2atmpS1707 == _M0L6_2atmpS1708) {
              int32_t _M0L6_2atmpS1712 = _M0L7currentS643 + _M0L7olengthS616;
              int32_t _M0L6_2atmpS1711 = _M0L6_2atmpS1712 - _M0L1iS642;
              int32_t _M0L6_2atmpS1710 = _M0L6_2atmpS1711 - 1;
              if (
                _M0L6_2atmpS1710 < 0
                || _M0L6_2atmpS1710 >= Moonbit_array_length(_M0L6resultS611)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS611[_M0L6_2atmpS1710] = 46;
              _M0L7currentS645 = _M0L7currentS643 - 1;
            } else {
              _M0L7currentS645 = _M0L7currentS643;
            }
            _M0L6_2atmpS1704 = _M0L7currentS645 + _M0L7olengthS616;
            _M0L6_2atmpS1703 = _M0L6_2atmpS1704 - _M0L1iS642;
            _M0L6_2atmpS1698 = _M0L6_2atmpS1703 - 1;
            _M0L6_2atmpS1702 = _M0L6outputS644 % 10ull;
            _M0L6_2atmpS1701 = (int32_t)_M0L6_2atmpS1702;
            _M0L6_2atmpS1700 = 48 + _M0L6_2atmpS1701;
            _M0L6_2atmpS1699 = _M0L6_2atmpS1700 & 0xff;
            if (
              _M0L6_2atmpS1698 < 0
              || _M0L6_2atmpS1698 >= Moonbit_array_length(_M0L6resultS611)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS611[_M0L6_2atmpS1698] = _M0L6_2atmpS1699;
            _M0L6_2atmpS1705 = _M0L1iS642 + 1;
            _M0L6_2atmpS1706 = _M0L6outputS644 / 10ull;
            _M0L1iS642 = _M0L6_2atmpS1705;
            _M0L7currentS643 = _M0L7currentS645;
            _M0L6outputS644 = _M0L6_2atmpS1706;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1715 = _M0Lm5indexS612;
        _M0L6_2atmpS1716 = _M0L7olengthS616 + 1;
        _M0Lm5indexS612 = _M0L6_2atmpS1715 + _M0L6_2atmpS1716;
      }
    }
    _M0L6_2atmpS1717 = _M0Lm5indexS612;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2089
    = _M0FPB19string__from__bytes(_M0L6resultS611, 0, _M0L6_2atmpS1717);
    moonbit_decref_cycle_free(_M0L6resultS611);
    return _result_2089;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS557,
  uint32_t _M0L12ieeeExponentS556
) {
  int32_t _M0Lm2e2S554;
  uint64_t _M0Lm2m2S555;
  uint64_t _M0L6_2atmpS1591;
  uint64_t _M0L6_2atmpS1590;
  int32_t _M0L4evenS558;
  uint64_t _M0L6_2atmpS1589;
  uint64_t _M0L2mvS559;
  int32_t _M0L7mmShiftS560;
  uint64_t _M0Lm2vrS561;
  uint64_t _M0Lm2vpS562;
  uint64_t _M0Lm2vmS563;
  int32_t _M0Lm3e10S564;
  int32_t _M0Lm17vmIsTrailingZerosS565;
  int32_t _M0Lm17vrIsTrailingZerosS566;
  int32_t _M0L6_2atmpS1491;
  int32_t _M0Lm7removedS585;
  int32_t _M0Lm16lastRemovedDigitS586;
  uint64_t _M0Lm6outputS587;
  int32_t _M0L6_2atmpS1587;
  int32_t _M0L6_2atmpS1588;
  int32_t _M0L3expS610;
  uint64_t _M0L6_2atmpS1586;
  struct _M0TPB17FloatingDecimal64* _block_2095;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S554 = 0;
  _M0Lm2m2S555 = 0ull;
  if (_M0L12ieeeExponentS556 == 0u) {
    _M0Lm2e2S554 = -1076;
    _M0Lm2m2S555 = _M0L12ieeeMantissaS557;
  } else {
    int32_t _M0L6_2atmpS1490 = *(int32_t*)&_M0L12ieeeExponentS556;
    int32_t _M0L6_2atmpS1489 = _M0L6_2atmpS1490 - 1023;
    int32_t _M0L6_2atmpS1488 = _M0L6_2atmpS1489 - 52;
    _M0Lm2e2S554 = _M0L6_2atmpS1488 - 2;
    _M0Lm2m2S555 = 4503599627370496ull | _M0L12ieeeMantissaS557;
  }
  _M0L6_2atmpS1591 = _M0Lm2m2S555;
  _M0L6_2atmpS1590 = _M0L6_2atmpS1591 & 1ull;
  _M0L4evenS558 = _M0L6_2atmpS1590 == 0ull;
  _M0L6_2atmpS1589 = _M0Lm2m2S555;
  _M0L2mvS559 = 4ull * _M0L6_2atmpS1589;
  _M0L7mmShiftS560
  = _M0L12ieeeMantissaS557 != 0ull || _M0L12ieeeExponentS556 <= 1u;
  _M0Lm2vrS561 = 0ull;
  _M0Lm2vpS562 = 0ull;
  _M0Lm2vmS563 = 0ull;
  _M0Lm3e10S564 = 0;
  _M0Lm17vmIsTrailingZerosS565 = 0;
  _M0Lm17vrIsTrailingZerosS566 = 0;
  _M0L6_2atmpS1491 = _M0Lm2e2S554;
  if (_M0L6_2atmpS1491 >= 0) {
    int32_t _M0L6_2atmpS1513 = _M0Lm2e2S554;
    int32_t _M0L6_2atmpS1509;
    int32_t _M0L6_2atmpS1512;
    int32_t _M0L6_2atmpS1511;
    int32_t _M0L6_2atmpS1510;
    int32_t _M0L1qS567;
    int32_t _M0L6_2atmpS1508;
    int32_t _M0L6_2atmpS1507;
    int32_t _M0L1kS568;
    int32_t _M0L6_2atmpS1506;
    int32_t _M0L6_2atmpS1505;
    int32_t _M0L6_2atmpS1504;
    int32_t _M0L1iS569;
    struct _M0TPB8Pow5Pair _M0L4pow5S570;
    uint64_t _M0L6_2atmpS1503;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS571;
    uint64_t _M0L8_2avrOutS572;
    uint64_t _M0L8_2avpOutS573;
    uint64_t _M0L8_2avmOutS574;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1509 = _M0FPB9log10Pow2(_M0L6_2atmpS1513);
    _M0L6_2atmpS1512 = _M0Lm2e2S554;
    _M0L6_2atmpS1511 = _M0L6_2atmpS1512 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1510 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1511);
    _M0L1qS567 = _M0L6_2atmpS1509 - _M0L6_2atmpS1510;
    _M0Lm3e10S564 = _M0L1qS567;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1508 = _M0FPB8pow5bits(_M0L1qS567);
    _M0L6_2atmpS1507 = 125 + _M0L6_2atmpS1508;
    _M0L1kS568 = _M0L6_2atmpS1507 - 1;
    _M0L6_2atmpS1506 = _M0Lm2e2S554;
    _M0L6_2atmpS1505 = -_M0L6_2atmpS1506;
    _M0L6_2atmpS1504 = _M0L6_2atmpS1505 + _M0L1qS567;
    _M0L1iS569 = _M0L6_2atmpS1504 + _M0L1kS568;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S570 = _M0FPB22double__computeInvPow5(_M0L1qS567);
    _M0L6_2atmpS1503 = _M0Lm2m2S555;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS571
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1503, _M0L4pow5S570, _M0L1iS569, _M0L7mmShiftS560);
    _M0L8_2avrOutS572 = _M0L7_2abindS571.$0;
    _M0L8_2avpOutS573 = _M0L7_2abindS571.$1;
    _M0L8_2avmOutS574 = _M0L7_2abindS571.$2;
    _M0Lm2vrS561 = _M0L8_2avrOutS572;
    _M0Lm2vpS562 = _M0L8_2avpOutS573;
    _M0Lm2vmS563 = _M0L8_2avmOutS574;
    if (_M0L1qS567 <= 21) {
      int32_t _M0L6_2atmpS1499 = (int32_t)_M0L2mvS559;
      uint64_t _M0L6_2atmpS1502 = _M0L2mvS559 / 5ull;
      int32_t _M0L6_2atmpS1501 = (int32_t)_M0L6_2atmpS1502;
      int32_t _M0L6_2atmpS1500 = 5 * _M0L6_2atmpS1501;
      int32_t _M0L6mvMod5S575 = _M0L6_2atmpS1499 - _M0L6_2atmpS1500;
      if (_M0L6mvMod5S575 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS566
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS559, _M0L1qS567);
      } else if (_M0L4evenS558) {
        uint64_t _M0L6_2atmpS1493 = _M0L2mvS559 - 1ull;
        uint64_t _M0L6_2atmpS1494;
        uint64_t _M0L6_2atmpS1492;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1494 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS560);
        _M0L6_2atmpS1492 = _M0L6_2atmpS1493 - _M0L6_2atmpS1494;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS565
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1492, _M0L1qS567);
      } else {
        uint64_t _M0L6_2atmpS1495 = _M0Lm2vpS562;
        uint64_t _M0L6_2atmpS1498 = _M0L2mvS559 + 2ull;
        int32_t _M0L6_2atmpS1497;
        uint64_t _M0L6_2atmpS1496;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1497
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1498, _M0L1qS567);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1496 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1497);
        _M0Lm2vpS562 = _M0L6_2atmpS1495 - _M0L6_2atmpS1496;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1527 = _M0Lm2e2S554;
    int32_t _M0L6_2atmpS1526 = -_M0L6_2atmpS1527;
    int32_t _M0L6_2atmpS1521;
    int32_t _M0L6_2atmpS1525;
    int32_t _M0L6_2atmpS1524;
    int32_t _M0L6_2atmpS1523;
    int32_t _M0L6_2atmpS1522;
    int32_t _M0L1qS576;
    int32_t _M0L6_2atmpS1514;
    int32_t _M0L6_2atmpS1520;
    int32_t _M0L6_2atmpS1519;
    int32_t _M0L1iS577;
    int32_t _M0L6_2atmpS1518;
    int32_t _M0L1kS578;
    int32_t _M0L1jS579;
    struct _M0TPB8Pow5Pair _M0L4pow5S580;
    uint64_t _M0L6_2atmpS1517;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS581;
    uint64_t _M0L8_2avrOutS582;
    uint64_t _M0L8_2avpOutS583;
    uint64_t _M0L8_2avmOutS584;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1521 = _M0FPB9log10Pow5(_M0L6_2atmpS1526);
    _M0L6_2atmpS1525 = _M0Lm2e2S554;
    _M0L6_2atmpS1524 = -_M0L6_2atmpS1525;
    _M0L6_2atmpS1523 = _M0L6_2atmpS1524 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1522 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1523);
    _M0L1qS576 = _M0L6_2atmpS1521 - _M0L6_2atmpS1522;
    _M0L6_2atmpS1514 = _M0Lm2e2S554;
    _M0Lm3e10S564 = _M0L1qS576 + _M0L6_2atmpS1514;
    _M0L6_2atmpS1520 = _M0Lm2e2S554;
    _M0L6_2atmpS1519 = -_M0L6_2atmpS1520;
    _M0L1iS577 = _M0L6_2atmpS1519 - _M0L1qS576;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1518 = _M0FPB8pow5bits(_M0L1iS577);
    _M0L1kS578 = _M0L6_2atmpS1518 - 125;
    _M0L1jS579 = _M0L1qS576 - _M0L1kS578;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S580 = _M0FPB19double__computePow5(_M0L1iS577);
    _M0L6_2atmpS1517 = _M0Lm2m2S555;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS581
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1517, _M0L4pow5S580, _M0L1jS579, _M0L7mmShiftS560);
    _M0L8_2avrOutS582 = _M0L7_2abindS581.$0;
    _M0L8_2avpOutS583 = _M0L7_2abindS581.$1;
    _M0L8_2avmOutS584 = _M0L7_2abindS581.$2;
    _M0Lm2vrS561 = _M0L8_2avrOutS582;
    _M0Lm2vpS562 = _M0L8_2avpOutS583;
    _M0Lm2vmS563 = _M0L8_2avmOutS584;
    if (_M0L1qS576 <= 1) {
      _M0Lm17vrIsTrailingZerosS566 = 1;
      if (_M0L4evenS558) {
        int32_t _M0L6_2atmpS1515;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1515 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS560);
        _M0Lm17vmIsTrailingZerosS565 = _M0L6_2atmpS1515 == 1;
      } else {
        uint64_t _M0L6_2atmpS1516 = _M0Lm2vpS562;
        _M0Lm2vpS562 = _M0L6_2atmpS1516 - 1ull;
      }
    } else if (_M0L1qS576 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS566
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS559, _M0L1qS576);
    }
  }
  _M0Lm7removedS585 = 0;
  _M0Lm16lastRemovedDigitS586 = 0;
  _M0Lm6outputS587 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS565 || _M0Lm17vrIsTrailingZerosS566) {
    int32_t _if__result_2092;
    uint64_t _M0L6_2atmpS1557;
    uint64_t _M0L6_2atmpS1563;
    uint64_t _M0L6_2atmpS1564;
    int32_t _if__result_2093;
    int32_t _M0L6_2atmpS1560;
    int64_t _M0L6_2atmpS1559;
    uint64_t _M0L6_2atmpS1558;
    while (1) {
      uint64_t _M0L6_2atmpS1540 = _M0Lm2vpS562;
      uint64_t _M0L7vpDiv10S588 = _M0L6_2atmpS1540 / 10ull;
      uint64_t _M0L6_2atmpS1539 = _M0Lm2vmS563;
      uint64_t _M0L7vmDiv10S589 = _M0L6_2atmpS1539 / 10ull;
      uint64_t _M0L6_2atmpS1538;
      int32_t _M0L6_2atmpS1535;
      int32_t _M0L6_2atmpS1537;
      int32_t _M0L6_2atmpS1536;
      int32_t _M0L7vmMod10S591;
      uint64_t _M0L6_2atmpS1534;
      uint64_t _M0L7vrDiv10S592;
      uint64_t _M0L6_2atmpS1533;
      int32_t _M0L6_2atmpS1530;
      int32_t _M0L6_2atmpS1532;
      int32_t _M0L6_2atmpS1531;
      int32_t _M0L7vrMod10S593;
      int32_t _M0L6_2atmpS1529;
      if (_M0L7vpDiv10S588 <= _M0L7vmDiv10S589) {
        break;
      }
      _M0L6_2atmpS1538 = _M0Lm2vmS563;
      _M0L6_2atmpS1535 = (int32_t)_M0L6_2atmpS1538;
      _M0L6_2atmpS1537 = (int32_t)_M0L7vmDiv10S589;
      _M0L6_2atmpS1536 = 10 * _M0L6_2atmpS1537;
      _M0L7vmMod10S591 = _M0L6_2atmpS1535 - _M0L6_2atmpS1536;
      _M0L6_2atmpS1534 = _M0Lm2vrS561;
      _M0L7vrDiv10S592 = _M0L6_2atmpS1534 / 10ull;
      _M0L6_2atmpS1533 = _M0Lm2vrS561;
      _M0L6_2atmpS1530 = (int32_t)_M0L6_2atmpS1533;
      _M0L6_2atmpS1532 = (int32_t)_M0L7vrDiv10S592;
      _M0L6_2atmpS1531 = 10 * _M0L6_2atmpS1532;
      _M0L7vrMod10S593 = _M0L6_2atmpS1530 - _M0L6_2atmpS1531;
      _M0Lm17vmIsTrailingZerosS565
      = _M0Lm17vmIsTrailingZerosS565 && _M0L7vmMod10S591 == 0;
      if (_M0Lm17vrIsTrailingZerosS566) {
        int32_t _M0L6_2atmpS1528 = _M0Lm16lastRemovedDigitS586;
        _M0Lm17vrIsTrailingZerosS566 = _M0L6_2atmpS1528 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS566 = 0;
      }
      _M0Lm16lastRemovedDigitS586 = _M0L7vrMod10S593;
      _M0Lm2vrS561 = _M0L7vrDiv10S592;
      _M0Lm2vpS562 = _M0L7vpDiv10S588;
      _M0Lm2vmS563 = _M0L7vmDiv10S589;
      _M0L6_2atmpS1529 = _M0Lm7removedS585;
      _M0Lm7removedS585 = _M0L6_2atmpS1529 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS565) {
      while (1) {
        uint64_t _M0L6_2atmpS1553 = _M0Lm2vmS563;
        uint64_t _M0L7vmDiv10S594 = _M0L6_2atmpS1553 / 10ull;
        uint64_t _M0L6_2atmpS1552 = _M0Lm2vmS563;
        int32_t _M0L6_2atmpS1549 = (int32_t)_M0L6_2atmpS1552;
        int32_t _M0L6_2atmpS1551 = (int32_t)_M0L7vmDiv10S594;
        int32_t _M0L6_2atmpS1550 = 10 * _M0L6_2atmpS1551;
        int32_t _M0L7vmMod10S595 = _M0L6_2atmpS1549 - _M0L6_2atmpS1550;
        uint64_t _M0L6_2atmpS1548;
        uint64_t _M0L7vpDiv10S597;
        uint64_t _M0L6_2atmpS1547;
        uint64_t _M0L7vrDiv10S598;
        uint64_t _M0L6_2atmpS1546;
        int32_t _M0L6_2atmpS1543;
        int32_t _M0L6_2atmpS1545;
        int32_t _M0L6_2atmpS1544;
        int32_t _M0L7vrMod10S599;
        int32_t _M0L6_2atmpS1542;
        if (_M0L7vmMod10S595 != 0) {
          break;
        }
        _M0L6_2atmpS1548 = _M0Lm2vpS562;
        _M0L7vpDiv10S597 = _M0L6_2atmpS1548 / 10ull;
        _M0L6_2atmpS1547 = _M0Lm2vrS561;
        _M0L7vrDiv10S598 = _M0L6_2atmpS1547 / 10ull;
        _M0L6_2atmpS1546 = _M0Lm2vrS561;
        _M0L6_2atmpS1543 = (int32_t)_M0L6_2atmpS1546;
        _M0L6_2atmpS1545 = (int32_t)_M0L7vrDiv10S598;
        _M0L6_2atmpS1544 = 10 * _M0L6_2atmpS1545;
        _M0L7vrMod10S599 = _M0L6_2atmpS1543 - _M0L6_2atmpS1544;
        if (_M0Lm17vrIsTrailingZerosS566) {
          int32_t _M0L6_2atmpS1541 = _M0Lm16lastRemovedDigitS586;
          _M0Lm17vrIsTrailingZerosS566 = _M0L6_2atmpS1541 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS566 = 0;
        }
        _M0Lm16lastRemovedDigitS586 = _M0L7vrMod10S599;
        _M0Lm2vrS561 = _M0L7vrDiv10S598;
        _M0Lm2vpS562 = _M0L7vpDiv10S597;
        _M0Lm2vmS563 = _M0L7vmDiv10S594;
        _M0L6_2atmpS1542 = _M0Lm7removedS585;
        _M0Lm7removedS585 = _M0L6_2atmpS1542 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS566) {
      int32_t _M0L6_2atmpS1556 = _M0Lm16lastRemovedDigitS586;
      if (_M0L6_2atmpS1556 == 5) {
        uint64_t _M0L6_2atmpS1555 = _M0Lm2vrS561;
        uint64_t _M0L6_2atmpS1554 = _M0L6_2atmpS1555 % 2ull;
        _if__result_2092 = _M0L6_2atmpS1554 == 0ull;
      } else {
        _if__result_2092 = 0;
      }
    } else {
      _if__result_2092 = 0;
    }
    if (_if__result_2092) {
      _M0Lm16lastRemovedDigitS586 = 4;
    }
    _M0L6_2atmpS1557 = _M0Lm2vrS561;
    _M0L6_2atmpS1563 = _M0Lm2vrS561;
    _M0L6_2atmpS1564 = _M0Lm2vmS563;
    if (_M0L6_2atmpS1563 == _M0L6_2atmpS1564) {
      if (!_M0L4evenS558) {
        _if__result_2093 = 1;
      } else {
        int32_t _M0L6_2atmpS1562 = _M0Lm17vmIsTrailingZerosS565;
        _if__result_2093 = !_M0L6_2atmpS1562;
      }
    } else {
      _if__result_2093 = 0;
    }
    if (_if__result_2093) {
      _M0L6_2atmpS1560 = 1;
    } else {
      int32_t _M0L6_2atmpS1561 = _M0Lm16lastRemovedDigitS586;
      _M0L6_2atmpS1560 = _M0L6_2atmpS1561 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1559 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1560);
    _M0L6_2atmpS1558 = *(uint64_t*)&_M0L6_2atmpS1559;
    _M0Lm6outputS587 = _M0L6_2atmpS1557 + _M0L6_2atmpS1558;
  } else {
    int32_t _M0Lm7roundUpS600 = 0;
    uint64_t _M0L6_2atmpS1585 = _M0Lm2vpS562;
    uint64_t _M0L8vpDiv100S601 = _M0L6_2atmpS1585 / 100ull;
    uint64_t _M0L6_2atmpS1584 = _M0Lm2vmS563;
    uint64_t _M0L8vmDiv100S602 = _M0L6_2atmpS1584 / 100ull;
    uint64_t _M0L6_2atmpS1579;
    uint64_t _M0L6_2atmpS1582;
    uint64_t _M0L6_2atmpS1583;
    int32_t _M0L6_2atmpS1581;
    uint64_t _M0L6_2atmpS1580;
    if (_M0L8vpDiv100S601 > _M0L8vmDiv100S602) {
      uint64_t _M0L6_2atmpS1570 = _M0Lm2vrS561;
      uint64_t _M0L8vrDiv100S603 = _M0L6_2atmpS1570 / 100ull;
      uint64_t _M0L6_2atmpS1569 = _M0Lm2vrS561;
      int32_t _M0L6_2atmpS1566 = (int32_t)_M0L6_2atmpS1569;
      int32_t _M0L6_2atmpS1568 = (int32_t)_M0L8vrDiv100S603;
      int32_t _M0L6_2atmpS1567 = 100 * _M0L6_2atmpS1568;
      int32_t _M0L8vrMod100S604 = _M0L6_2atmpS1566 - _M0L6_2atmpS1567;
      int32_t _M0L6_2atmpS1565;
      _M0Lm7roundUpS600 = _M0L8vrMod100S604 >= 50;
      _M0Lm2vrS561 = _M0L8vrDiv100S603;
      _M0Lm2vpS562 = _M0L8vpDiv100S601;
      _M0Lm2vmS563 = _M0L8vmDiv100S602;
      _M0L6_2atmpS1565 = _M0Lm7removedS585;
      _M0Lm7removedS585 = _M0L6_2atmpS1565 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1578 = _M0Lm2vpS562;
      uint64_t _M0L7vpDiv10S605 = _M0L6_2atmpS1578 / 10ull;
      uint64_t _M0L6_2atmpS1577 = _M0Lm2vmS563;
      uint64_t _M0L7vmDiv10S606 = _M0L6_2atmpS1577 / 10ull;
      uint64_t _M0L6_2atmpS1576;
      uint64_t _M0L7vrDiv10S608;
      uint64_t _M0L6_2atmpS1575;
      int32_t _M0L6_2atmpS1572;
      int32_t _M0L6_2atmpS1574;
      int32_t _M0L6_2atmpS1573;
      int32_t _M0L7vrMod10S609;
      int32_t _M0L6_2atmpS1571;
      if (_M0L7vpDiv10S605 <= _M0L7vmDiv10S606) {
        break;
      }
      _M0L6_2atmpS1576 = _M0Lm2vrS561;
      _M0L7vrDiv10S608 = _M0L6_2atmpS1576 / 10ull;
      _M0L6_2atmpS1575 = _M0Lm2vrS561;
      _M0L6_2atmpS1572 = (int32_t)_M0L6_2atmpS1575;
      _M0L6_2atmpS1574 = (int32_t)_M0L7vrDiv10S608;
      _M0L6_2atmpS1573 = 10 * _M0L6_2atmpS1574;
      _M0L7vrMod10S609 = _M0L6_2atmpS1572 - _M0L6_2atmpS1573;
      _M0Lm7roundUpS600 = _M0L7vrMod10S609 >= 5;
      _M0Lm2vrS561 = _M0L7vrDiv10S608;
      _M0Lm2vpS562 = _M0L7vpDiv10S605;
      _M0Lm2vmS563 = _M0L7vmDiv10S606;
      _M0L6_2atmpS1571 = _M0Lm7removedS585;
      _M0Lm7removedS585 = _M0L6_2atmpS1571 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1579 = _M0Lm2vrS561;
    _M0L6_2atmpS1582 = _M0Lm2vrS561;
    _M0L6_2atmpS1583 = _M0Lm2vmS563;
    _M0L6_2atmpS1581
    = _M0L6_2atmpS1582 == _M0L6_2atmpS1583 || _M0Lm7roundUpS600;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1580 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1581);
    _M0Lm6outputS587 = _M0L6_2atmpS1579 + _M0L6_2atmpS1580;
  }
  _M0L6_2atmpS1587 = _M0Lm3e10S564;
  _M0L6_2atmpS1588 = _M0Lm7removedS585;
  _M0L3expS610 = _M0L6_2atmpS1587 + _M0L6_2atmpS1588;
  _M0L6_2atmpS1586 = _M0Lm6outputS587;
  _block_2095
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2095)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2095->$0 = _M0L6_2atmpS1586;
  _block_2095->$1 = _M0L3expS610;
  return _block_2095;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS553) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS553) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS552) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS552) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS551) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS551) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS550) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS550 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS550 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS550 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS550 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS550 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS550 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS550 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS550 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS550 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS550 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS550 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS550 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS550 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS550 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS550 >= 100ull) {
    return 3;
  }
  if (_M0L1vS550 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS533) {
  int32_t _M0L6_2atmpS1487;
  int32_t _M0L6_2atmpS1486;
  int32_t _M0L4baseS532;
  int32_t _M0L5base2S534;
  int32_t _M0L6offsetS535;
  int32_t _M0L6_2atmpS1485;
  uint64_t _M0L4mul0S536;
  int32_t _M0L6_2atmpS1484;
  int32_t _M0L6_2atmpS1483;
  uint64_t _M0L4mul1S537;
  uint64_t _M0L1mS538;
  struct _M0TPB7Umul128 _M0L7_2abindS539;
  uint64_t _M0L7_2alow1S540;
  uint64_t _M0L8_2ahigh1S541;
  struct _M0TPB7Umul128 _M0L7_2abindS542;
  uint64_t _M0L7_2alow0S543;
  uint64_t _M0L8_2ahigh0S544;
  uint64_t _M0L3sumS545;
  uint64_t _M0Lm5high1S546;
  int32_t _M0L6_2atmpS1481;
  int32_t _M0L6_2atmpS1482;
  int32_t _M0L5deltaS547;
  uint64_t _M0L6_2atmpS1480;
  uint64_t _M0L6_2atmpS1472;
  int32_t _M0L6_2atmpS1479;
  uint32_t _M0L6_2atmpS1476;
  int32_t _M0L6_2atmpS1478;
  int32_t _M0L6_2atmpS1477;
  uint32_t _M0L6_2atmpS1475;
  uint32_t _M0L6_2atmpS1474;
  uint64_t _M0L6_2atmpS1473;
  uint64_t _M0L1aS548;
  uint64_t _M0L6_2atmpS1471;
  uint64_t _M0L1bS549;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1487 = _M0L1iS533 + 26;
  _M0L6_2atmpS1486 = _M0L6_2atmpS1487 - 1;
  _M0L4baseS532 = _M0L6_2atmpS1486 / 26;
  _M0L5base2S534 = _M0L4baseS532 * 26;
  _M0L6offsetS535 = _M0L5base2S534 - _M0L1iS533;
  _M0L6_2atmpS1485 = _M0L4baseS532 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S536
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1485);
  _M0L6_2atmpS1484 = _M0L4baseS532 * 2;
  _M0L6_2atmpS1483 = _M0L6_2atmpS1484 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S537
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1483);
  if (_M0L6offsetS535 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S536, .$1 = _M0L4mul1S537};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS538
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS535);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS539 = _M0FPB7umul128(_M0L1mS538, _M0L4mul1S537);
  _M0L7_2alow1S540 = _M0L7_2abindS539.$0;
  _M0L8_2ahigh1S541 = _M0L7_2abindS539.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS542 = _M0FPB7umul128(_M0L1mS538, _M0L4mul0S536);
  _M0L7_2alow0S543 = _M0L7_2abindS542.$0;
  _M0L8_2ahigh0S544 = _M0L7_2abindS542.$1;
  _M0L3sumS545 = _M0L8_2ahigh0S544 + _M0L7_2alow1S540;
  _M0Lm5high1S546 = _M0L8_2ahigh1S541;
  if (_M0L3sumS545 < _M0L8_2ahigh0S544) {
    uint64_t _M0L6_2atmpS1470 = _M0Lm5high1S546;
    _M0Lm5high1S546 = _M0L6_2atmpS1470 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1481 = _M0FPB8pow5bits(_M0L5base2S534);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1482 = _M0FPB8pow5bits(_M0L1iS533);
  _M0L5deltaS547 = _M0L6_2atmpS1481 - _M0L6_2atmpS1482;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1480
  = _M0FPB13shiftright128(_M0L7_2alow0S543, _M0L3sumS545, _M0L5deltaS547);
  _M0L6_2atmpS1472 = _M0L6_2atmpS1480 + 1ull;
  _M0L6_2atmpS1479 = _M0L1iS533 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1476
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1479);
  _M0L6_2atmpS1478 = _M0L1iS533 % 16;
  _M0L6_2atmpS1477 = _M0L6_2atmpS1478 << 1;
  _M0L6_2atmpS1475 = _M0L6_2atmpS1476 >> (_M0L6_2atmpS1477 & 31);
  _M0L6_2atmpS1474 = _M0L6_2atmpS1475 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1473 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1474);
  _M0L1aS548 = _M0L6_2atmpS1472 + _M0L6_2atmpS1473;
  _M0L6_2atmpS1471 = _M0Lm5high1S546;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS549
  = _M0FPB13shiftright128(_M0L3sumS545, _M0L6_2atmpS1471, _M0L5deltaS547);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS548, .$1 = _M0L1bS549};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS515) {
  int32_t _M0L4baseS514;
  int32_t _M0L5base2S516;
  int32_t _M0L6offsetS517;
  int32_t _M0L6_2atmpS1469;
  uint64_t _M0L4mul0S518;
  int32_t _M0L6_2atmpS1468;
  int32_t _M0L6_2atmpS1467;
  uint64_t _M0L4mul1S519;
  uint64_t _M0L1mS520;
  struct _M0TPB7Umul128 _M0L7_2abindS521;
  uint64_t _M0L7_2alow1S522;
  uint64_t _M0L8_2ahigh1S523;
  struct _M0TPB7Umul128 _M0L7_2abindS524;
  uint64_t _M0L7_2alow0S525;
  uint64_t _M0L8_2ahigh0S526;
  uint64_t _M0L3sumS527;
  uint64_t _M0Lm5high1S528;
  int32_t _M0L6_2atmpS1465;
  int32_t _M0L6_2atmpS1466;
  int32_t _M0L5deltaS529;
  uint64_t _M0L6_2atmpS1457;
  int32_t _M0L6_2atmpS1464;
  uint32_t _M0L6_2atmpS1461;
  int32_t _M0L6_2atmpS1463;
  int32_t _M0L6_2atmpS1462;
  uint32_t _M0L6_2atmpS1460;
  uint32_t _M0L6_2atmpS1459;
  uint64_t _M0L6_2atmpS1458;
  uint64_t _M0L1aS530;
  uint64_t _M0L6_2atmpS1456;
  uint64_t _M0L1bS531;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS514 = _M0L1iS515 / 26;
  _M0L5base2S516 = _M0L4baseS514 * 26;
  _M0L6offsetS517 = _M0L1iS515 - _M0L5base2S516;
  _M0L6_2atmpS1469 = _M0L4baseS514 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S518
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1469);
  _M0L6_2atmpS1468 = _M0L4baseS514 * 2;
  _M0L6_2atmpS1467 = _M0L6_2atmpS1468 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S519
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1467);
  if (_M0L6offsetS517 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S518, .$1 = _M0L4mul1S519};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS520
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS517);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS521 = _M0FPB7umul128(_M0L1mS520, _M0L4mul1S519);
  _M0L7_2alow1S522 = _M0L7_2abindS521.$0;
  _M0L8_2ahigh1S523 = _M0L7_2abindS521.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS524 = _M0FPB7umul128(_M0L1mS520, _M0L4mul0S518);
  _M0L7_2alow0S525 = _M0L7_2abindS524.$0;
  _M0L8_2ahigh0S526 = _M0L7_2abindS524.$1;
  _M0L3sumS527 = _M0L8_2ahigh0S526 + _M0L7_2alow1S522;
  _M0Lm5high1S528 = _M0L8_2ahigh1S523;
  if (_M0L3sumS527 < _M0L8_2ahigh0S526) {
    uint64_t _M0L6_2atmpS1455 = _M0Lm5high1S528;
    _M0Lm5high1S528 = _M0L6_2atmpS1455 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1465 = _M0FPB8pow5bits(_M0L1iS515);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1466 = _M0FPB8pow5bits(_M0L5base2S516);
  _M0L5deltaS529 = _M0L6_2atmpS1465 - _M0L6_2atmpS1466;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1457
  = _M0FPB13shiftright128(_M0L7_2alow0S525, _M0L3sumS527, _M0L5deltaS529);
  _M0L6_2atmpS1464 = _M0L1iS515 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1461
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1464);
  _M0L6_2atmpS1463 = _M0L1iS515 % 16;
  _M0L6_2atmpS1462 = _M0L6_2atmpS1463 << 1;
  _M0L6_2atmpS1460 = _M0L6_2atmpS1461 >> (_M0L6_2atmpS1462 & 31);
  _M0L6_2atmpS1459 = _M0L6_2atmpS1460 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1458 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1459);
  _M0L1aS530 = _M0L6_2atmpS1457 + _M0L6_2atmpS1458;
  _M0L6_2atmpS1456 = _M0Lm5high1S528;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS531
  = _M0FPB13shiftright128(_M0L3sumS527, _M0L6_2atmpS1456, _M0L5deltaS529);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS530, .$1 = _M0L1bS531};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS488,
  struct _M0TPB8Pow5Pair _M0L3mulS485,
  int32_t _M0L1jS501,
  int32_t _M0L7mmShiftS503
) {
  uint64_t _M0L7_2amul0S484;
  uint64_t _M0L7_2amul1S486;
  uint64_t _M0L1mS487;
  struct _M0TPB7Umul128 _M0L7_2abindS489;
  uint64_t _M0L5_2aloS490;
  uint64_t _M0L6_2atmpS491;
  struct _M0TPB7Umul128 _M0L7_2abindS492;
  uint64_t _M0L6_2alo2S493;
  uint64_t _M0L6_2ahi2S494;
  uint64_t _M0L3midS495;
  uint64_t _M0L6_2atmpS1454;
  uint64_t _M0L2hiS496;
  uint64_t _M0L3lo2S497;
  uint64_t _M0L6_2atmpS1452;
  uint64_t _M0L6_2atmpS1453;
  uint64_t _M0L4mid2S498;
  uint64_t _M0L6_2atmpS1451;
  uint64_t _M0L3hi2S499;
  int32_t _M0L6_2atmpS1450;
  int32_t _M0L6_2atmpS1449;
  uint64_t _M0L2vpS500;
  uint64_t _M0Lm2vmS502;
  int32_t _M0L6_2atmpS1448;
  int32_t _M0L6_2atmpS1447;
  uint64_t _M0L2vrS513;
  uint64_t _M0L6_2atmpS1446;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S484 = _M0L3mulS485.$0;
  _M0L7_2amul1S486 = _M0L3mulS485.$1;
  _M0L1mS487 = _M0L1mS488 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS489 = _M0FPB7umul128(_M0L1mS487, _M0L7_2amul0S484);
  _M0L5_2aloS490 = _M0L7_2abindS489.$0;
  _M0L6_2atmpS491 = _M0L7_2abindS489.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS492 = _M0FPB7umul128(_M0L1mS487, _M0L7_2amul1S486);
  _M0L6_2alo2S493 = _M0L7_2abindS492.$0;
  _M0L6_2ahi2S494 = _M0L7_2abindS492.$1;
  _M0L3midS495 = _M0L6_2atmpS491 + _M0L6_2alo2S493;
  if (_M0L3midS495 < _M0L6_2atmpS491) {
    _M0L6_2atmpS1454 = 1ull;
  } else {
    _M0L6_2atmpS1454 = 0ull;
  }
  _M0L2hiS496 = _M0L6_2ahi2S494 + _M0L6_2atmpS1454;
  _M0L3lo2S497 = _M0L5_2aloS490 + _M0L7_2amul0S484;
  _M0L6_2atmpS1452 = _M0L3midS495 + _M0L7_2amul1S486;
  if (_M0L3lo2S497 < _M0L5_2aloS490) {
    _M0L6_2atmpS1453 = 1ull;
  } else {
    _M0L6_2atmpS1453 = 0ull;
  }
  _M0L4mid2S498 = _M0L6_2atmpS1452 + _M0L6_2atmpS1453;
  if (_M0L4mid2S498 < _M0L3midS495) {
    _M0L6_2atmpS1451 = 1ull;
  } else {
    _M0L6_2atmpS1451 = 0ull;
  }
  _M0L3hi2S499 = _M0L2hiS496 + _M0L6_2atmpS1451;
  _M0L6_2atmpS1450 = _M0L1jS501 - 64;
  _M0L6_2atmpS1449 = _M0L6_2atmpS1450 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS500
  = _M0FPB13shiftright128(_M0L4mid2S498, _M0L3hi2S499, _M0L6_2atmpS1449);
  _M0Lm2vmS502 = 0ull;
  if (_M0L7mmShiftS503) {
    uint64_t _M0L3lo3S504 = _M0L5_2aloS490 - _M0L7_2amul0S484;
    uint64_t _M0L6_2atmpS1436 = _M0L3midS495 - _M0L7_2amul1S486;
    uint64_t _M0L6_2atmpS1437;
    uint64_t _M0L4mid3S505;
    uint64_t _M0L6_2atmpS1435;
    uint64_t _M0L3hi3S506;
    int32_t _M0L6_2atmpS1434;
    int32_t _M0L6_2atmpS1433;
    if (_M0L5_2aloS490 < _M0L3lo3S504) {
      _M0L6_2atmpS1437 = 1ull;
    } else {
      _M0L6_2atmpS1437 = 0ull;
    }
    _M0L4mid3S505 = _M0L6_2atmpS1436 - _M0L6_2atmpS1437;
    if (_M0L3midS495 < _M0L4mid3S505) {
      _M0L6_2atmpS1435 = 1ull;
    } else {
      _M0L6_2atmpS1435 = 0ull;
    }
    _M0L3hi3S506 = _M0L2hiS496 - _M0L6_2atmpS1435;
    _M0L6_2atmpS1434 = _M0L1jS501 - 64;
    _M0L6_2atmpS1433 = _M0L6_2atmpS1434 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS502
    = _M0FPB13shiftright128(_M0L4mid3S505, _M0L3hi3S506, _M0L6_2atmpS1433);
  } else {
    uint64_t _M0L3lo3S507 = _M0L5_2aloS490 + _M0L5_2aloS490;
    uint64_t _M0L6_2atmpS1444 = _M0L3midS495 + _M0L3midS495;
    uint64_t _M0L6_2atmpS1445;
    uint64_t _M0L4mid3S508;
    uint64_t _M0L6_2atmpS1442;
    uint64_t _M0L6_2atmpS1443;
    uint64_t _M0L3hi3S509;
    uint64_t _M0L3lo4S510;
    uint64_t _M0L6_2atmpS1440;
    uint64_t _M0L6_2atmpS1441;
    uint64_t _M0L4mid4S511;
    uint64_t _M0L6_2atmpS1439;
    uint64_t _M0L3hi4S512;
    int32_t _M0L6_2atmpS1438;
    if (_M0L3lo3S507 < _M0L5_2aloS490) {
      _M0L6_2atmpS1445 = 1ull;
    } else {
      _M0L6_2atmpS1445 = 0ull;
    }
    _M0L4mid3S508 = _M0L6_2atmpS1444 + _M0L6_2atmpS1445;
    _M0L6_2atmpS1442 = _M0L2hiS496 + _M0L2hiS496;
    if (_M0L4mid3S508 < _M0L3midS495) {
      _M0L6_2atmpS1443 = 1ull;
    } else {
      _M0L6_2atmpS1443 = 0ull;
    }
    _M0L3hi3S509 = _M0L6_2atmpS1442 + _M0L6_2atmpS1443;
    _M0L3lo4S510 = _M0L3lo3S507 - _M0L7_2amul0S484;
    _M0L6_2atmpS1440 = _M0L4mid3S508 - _M0L7_2amul1S486;
    if (_M0L3lo3S507 < _M0L3lo4S510) {
      _M0L6_2atmpS1441 = 1ull;
    } else {
      _M0L6_2atmpS1441 = 0ull;
    }
    _M0L4mid4S511 = _M0L6_2atmpS1440 - _M0L6_2atmpS1441;
    if (_M0L4mid3S508 < _M0L4mid4S511) {
      _M0L6_2atmpS1439 = 1ull;
    } else {
      _M0L6_2atmpS1439 = 0ull;
    }
    _M0L3hi4S512 = _M0L3hi3S509 - _M0L6_2atmpS1439;
    _M0L6_2atmpS1438 = _M0L1jS501 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS502
    = _M0FPB13shiftright128(_M0L4mid4S511, _M0L3hi4S512, _M0L6_2atmpS1438);
  }
  _M0L6_2atmpS1448 = _M0L1jS501 - 64;
  _M0L6_2atmpS1447 = _M0L6_2atmpS1448 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS513
  = _M0FPB13shiftright128(_M0L3midS495, _M0L2hiS496, _M0L6_2atmpS1447);
  _M0L6_2atmpS1446 = _M0Lm2vmS502;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS513,
                                                .$1 = _M0L2vpS500,
                                                .$2 = _M0L6_2atmpS1446};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS482,
  int32_t _M0L1pS483
) {
  uint64_t _M0L6_2atmpS1432;
  uint64_t _M0L6_2atmpS1431;
  uint64_t _M0L6_2atmpS1430;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1432 = 1ull << (_M0L1pS483 & 63);
  _M0L6_2atmpS1431 = _M0L6_2atmpS1432 - 1ull;
  _M0L6_2atmpS1430 = _M0L5valueS482 & _M0L6_2atmpS1431;
  return _M0L6_2atmpS1430 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS480,
  int32_t _M0L1pS481
) {
  int32_t _M0L6_2atmpS1429;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1429 = _M0FPB10pow5Factor(_M0L5valueS480);
  return _M0L6_2atmpS1429 >= _M0L1pS481;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS475) {
  uint64_t _M0L6_2atmpS1420;
  uint64_t _M0L6_2atmpS1421;
  uint64_t _M0L6_2atmpS1422;
  uint64_t _M0L6_2atmpS1423;
  uint64_t _M0L6_2atmpS1428;
  int32_t _M0L5countS476;
  uint64_t _M0L1vS477;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1420 = _M0L5valueS475 % 5ull;
  if (_M0L6_2atmpS1420 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1421 = _M0L5valueS475 % 25ull;
  if (_M0L6_2atmpS1421 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1422 = _M0L5valueS475 % 125ull;
  if (_M0L6_2atmpS1422 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1423 = _M0L5valueS475 % 625ull;
  if (_M0L6_2atmpS1423 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1428 = _M0L5valueS475 / 625ull;
  _M0L5countS476 = 4;
  _M0L1vS477 = _M0L6_2atmpS1428;
  while (1) {
    if (_M0L1vS477 > 0ull) {
      uint64_t _M0L6_2atmpS1424 = _M0L1vS477 % 5ull;
      int32_t _M0L6_2atmpS1425;
      uint64_t _M0L6_2atmpS1426;
      if (_M0L6_2atmpS1424 != 0ull) {
        return _M0L5countS476;
      }
      _M0L6_2atmpS1425 = _M0L5countS476 + 1;
      _M0L6_2atmpS1426 = _M0L1vS477 / 5ull;
      _M0L5countS476 = _M0L6_2atmpS1425;
      _M0L1vS477 = _M0L6_2atmpS1426;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS479;
      moonbit_string_t _M0L6_2atmpS1427;
      int32_t _result_2097;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS479
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS479, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS479, _M0L5valueS475);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1427
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS479);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS479);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2097 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1427);
      moonbit_decref_cycle_free(_M0L6_2atmpS1427);
      return _result_2097;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS474,
  uint64_t _M0L2hiS472,
  int32_t _M0L4distS473
) {
  int32_t _M0L6_2atmpS1419;
  uint64_t _M0L6_2atmpS1417;
  uint64_t _M0L6_2atmpS1418;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1419 = 64 - _M0L4distS473;
  _M0L6_2atmpS1417 = _M0L2hiS472 << (_M0L6_2atmpS1419 & 63);
  _M0L6_2atmpS1418 = _M0L2loS474 >> (_M0L4distS473 & 63);
  return _M0L6_2atmpS1417 | _M0L6_2atmpS1418;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS462,
  uint64_t _M0L1bS465
) {
  uint64_t _M0L3aLoS461;
  uint64_t _M0L3aHiS463;
  uint64_t _M0L3bLoS464;
  uint64_t _M0L3bHiS466;
  uint64_t _M0L1xS467;
  uint64_t _M0L6_2atmpS1415;
  uint64_t _M0L6_2atmpS1416;
  uint64_t _M0L1yS468;
  uint64_t _M0L6_2atmpS1413;
  uint64_t _M0L6_2atmpS1414;
  uint64_t _M0L1zS469;
  uint64_t _M0L6_2atmpS1411;
  uint64_t _M0L6_2atmpS1412;
  uint64_t _M0L6_2atmpS1409;
  uint64_t _M0L6_2atmpS1410;
  uint64_t _M0L1wS470;
  uint64_t _M0L2loS471;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS461 = _M0L1aS462 & 4294967295ull;
  _M0L3aHiS463 = _M0L1aS462 >> 32;
  _M0L3bLoS464 = _M0L1bS465 & 4294967295ull;
  _M0L3bHiS466 = _M0L1bS465 >> 32;
  _M0L1xS467 = _M0L3aLoS461 * _M0L3bLoS464;
  _M0L6_2atmpS1415 = _M0L3aHiS463 * _M0L3bLoS464;
  _M0L6_2atmpS1416 = _M0L1xS467 >> 32;
  _M0L1yS468 = _M0L6_2atmpS1415 + _M0L6_2atmpS1416;
  _M0L6_2atmpS1413 = _M0L3aLoS461 * _M0L3bHiS466;
  _M0L6_2atmpS1414 = _M0L1yS468 & 4294967295ull;
  _M0L1zS469 = _M0L6_2atmpS1413 + _M0L6_2atmpS1414;
  _M0L6_2atmpS1411 = _M0L3aHiS463 * _M0L3bHiS466;
  _M0L6_2atmpS1412 = _M0L1yS468 >> 32;
  _M0L6_2atmpS1409 = _M0L6_2atmpS1411 + _M0L6_2atmpS1412;
  _M0L6_2atmpS1410 = _M0L1zS469 >> 32;
  _M0L1wS470 = _M0L6_2atmpS1409 + _M0L6_2atmpS1410;
  _M0L2loS471 = _M0L1aS462 * _M0L1bS465;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS471, .$1 = _M0L1wS470};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS459,
  int32_t _M0L4fromS456,
  int32_t _M0L2toS455
) {
  int32_t _M0L3lenS454;
  int32_t _M0L6_2atmpS1408;
  uint16_t* _M0L6bufferS457;
  int32_t _M0L1iS458;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS454 = _M0L2toS455 - _M0L4fromS456;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1408 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS457
  = (uint16_t*)moonbit_make_string(_M0L3lenS454, _M0L6_2atmpS1408);
  _M0L1iS458 = 0;
  while (1) {
    if (_M0L1iS458 < _M0L3lenS454) {
      int32_t _M0L6_2atmpS1406 = _M0L4fromS456 + _M0L1iS458;
      int32_t _M0L6_2atmpS1405;
      int32_t _M0L6_2atmpS1404;
      int32_t _M0L6_2atmpS1407;
      if (
        _M0L6_2atmpS1406 < 0
        || _M0L6_2atmpS1406 >= Moonbit_array_length(_M0L5bytesS459)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1405 = (int32_t)_M0L5bytesS459[_M0L6_2atmpS1406];
      _M0L6_2atmpS1404 = (uint16_t)_M0L6_2atmpS1405;
      if (
        _M0L1iS458 < 0 || _M0L1iS458 >= Moonbit_array_length(_M0L6bufferS457)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS457[_M0L1iS458] = _M0L6_2atmpS1404;
      _M0L6_2atmpS1407 = _M0L1iS458 + 1;
      _M0L1iS458 = _M0L6_2atmpS1407;
      continue;
    }
    break;
  }
  return _M0L6bufferS457;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS453) {
  int32_t _M0L6_2atmpS1403;
  uint32_t _M0L6_2atmpS1402;
  uint32_t _M0L6_2atmpS1401;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1403 = _M0L1eS453 * 78913;
  _M0L6_2atmpS1402 = *(uint32_t*)&_M0L6_2atmpS1403;
  _M0L6_2atmpS1401 = _M0L6_2atmpS1402 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1401;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS452) {
  int32_t _M0L6_2atmpS1400;
  uint32_t _M0L6_2atmpS1399;
  uint32_t _M0L6_2atmpS1398;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1400 = _M0L1eS452 * 732923;
  _M0L6_2atmpS1399 = *(uint32_t*)&_M0L6_2atmpS1400;
  _M0L6_2atmpS1398 = _M0L6_2atmpS1399 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1398;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS450,
  int32_t _M0L8exponentS451,
  int32_t _M0L8mantissaS448
) {
  moonbit_string_t _M0L1sS449;
  moonbit_string_t _result_2100;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS448) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS450) {
    _M0L1sS449 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS449 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS451) {
    moonbit_string_t _result_2099;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2099
    = moonbit_add_string(_M0L1sS449, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS449);
    return _result_2099;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2100
  = moonbit_add_string(_M0L1sS449, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS449);
  return _result_2100;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS447) {
  int32_t _M0L6_2atmpS1397;
  uint32_t _M0L6_2atmpS1396;
  uint32_t _M0L6_2atmpS1395;
  int32_t _M0L6_2atmpS1394;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1397 = _M0L1eS447 * 1217359;
  _M0L6_2atmpS1396 = *(uint32_t*)&_M0L6_2atmpS1397;
  _M0L6_2atmpS1395 = _M0L6_2atmpS1396 >> 19;
  _M0L6_2atmpS1394 = *(int32_t*)&_M0L6_2atmpS1395;
  return _M0L6_2atmpS1394 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS446) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS446 != _M0L4selfS446) {
    return 0;
  } else if (_M0L4selfS446 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS446 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS446;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS445) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS445 != _M0L4selfS445) {
    return 0ll;
  } else if (_M0L4selfS445 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS445 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS445;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS443
) {
  float* _M0L6_2atmpS1392;
  struct _M0TPB5ArrayGfE* _block_2101;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1392 = (float*)moonbit_make_float_array_raw(_M0L3lenS443);
  _block_2101
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2101)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _block_2101->$0 = _M0L6_2atmpS1392;
  _block_2101->$1 = _M0L3lenS443;
  return _block_2101;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS444
) {
  uint8_t* _M0L6_2atmpS1393;
  struct _M0TPB5ArrayGbE* _block_2102;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1393 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS444);
  _block_2102
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2102)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
  _block_2102->$0 = _M0L6_2atmpS1393;
  _block_2102->$1 = _M0L3lenS444;
  return _block_2102;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS439,
  int32_t _M0L5indexS440
) {
  uint64_t* _M0L6_2atmpS1390;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1390 = _M0L4selfS439;
  if (
    _M0L5indexS440 < 0
    || _M0L5indexS440 >= Moonbit_array_length(_M0L6_2atmpS1390)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1390[_M0L5indexS440];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS441,
  int32_t _M0L5indexS442
) {
  uint32_t* _M0L6_2atmpS1391;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1391 = _M0L4selfS441;
  if (
    _M0L5indexS442 < 0
    || _M0L5indexS442 >= Moonbit_array_length(_M0L6_2atmpS1391)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1391[_M0L5indexS442];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS438
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS438, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS437) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS437, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS436) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS436) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS435) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS435;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS429,
  moonbit_string_t _M0L5valueS431
) {
  int32_t _M0L3lenS1376;
  moonbit_string_t* _M0L6_2atmpS1378;
  int32_t _M0L6_2atmpS1377;
  int32_t _M0L6lengthS430;
  moonbit_string_t* _M0L3bufS1381;
  moonbit_string_t _M0L6_2aoldS2012;
  int32_t _M0L6_2atmpS1382;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1376 = _M0L4selfS429->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1378 = _M0MPC15array5Array6bufferGsE(_M0L4selfS429);
  _M0L6_2atmpS1377 = Moonbit_array_length(_M0L6_2atmpS1378);
  moonbit_decref_cycle_free(_M0L6_2atmpS1378);
  if (_M0L3lenS1376 == _M0L6_2atmpS1377) {
    int32_t _M0L3lenS1380 = _M0L4selfS429->$1;
    int32_t _M0L6_2atmpS1379 = _M0L3lenS1380 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS429, _M0L6_2atmpS1379);
  }
  _M0L6lengthS430 = _M0L4selfS429->$1;
  _M0L3bufS1381 = _M0L4selfS429->$0;
  _M0L6_2aoldS2012 = (moonbit_string_t)_M0L3bufS1381[_M0L6lengthS430];
  moonbit_decref_cycle_free(_M0L6_2aoldS2012);
  _M0L3bufS1381[_M0L6lengthS430] = _M0L5valueS431;
  _M0L6_2atmpS1382 = _M0L6lengthS430 + 1;
  _M0L4selfS429->$1 = _M0L6_2atmpS1382;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS432,
  struct _M0TUsiE* _M0L5valueS434
) {
  int32_t _M0L3lenS1383;
  struct _M0TUsiE** _M0L6_2atmpS1385;
  int32_t _M0L6_2atmpS1384;
  int32_t _M0L6lengthS433;
  struct _M0TUsiE** _M0L3bufS1388;
  struct _M0TUsiE* _M0L6_2aoldS2013;
  int32_t _M0L6_2atmpS1389;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1383 = _M0L4selfS432->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1385 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS432);
  _M0L6_2atmpS1384 = Moonbit_array_length(_M0L6_2atmpS1385);
  moonbit_decref_cycle_free(_M0L6_2atmpS1385);
  if (_M0L3lenS1383 == _M0L6_2atmpS1384) {
    int32_t _M0L3lenS1387 = _M0L4selfS432->$1;
    int32_t _M0L6_2atmpS1386 = _M0L3lenS1387 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS432, _M0L6_2atmpS1386);
  }
  _M0L6lengthS433 = _M0L4selfS432->$1;
  _M0L3bufS1388 = _M0L4selfS432->$0;
  _M0L6_2aoldS2013 = (struct _M0TUsiE*)_M0L3bufS1388[_M0L6lengthS433];
  if (_M0L6_2aoldS2013) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2013);
  }
  _M0L3bufS1388[_M0L6lengthS433] = _M0L5valueS434;
  _M0L6_2atmpS1389 = _M0L6lengthS433 + 1;
  _M0L4selfS432->$1 = _M0L6_2atmpS1389;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS422,
  int32_t _M0L8requiredS424
) {
  int32_t _M0L8old__capS421;
  int32_t _M0L3lenS1374;
  int32_t _M0L8new__capS423;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS421 = _M0MPC15array5Array8capacityGsE(_M0L4selfS422);
  _M0L3lenS1374 = _M0L4selfS422->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS423
  = _M0FPB23array__growth__capacity(_M0L8old__capS421, _M0L3lenS1374, _M0L8requiredS424);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS422, _M0L8new__capS423);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS426,
  int32_t _M0L8requiredS428
) {
  int32_t _M0L8old__capS425;
  int32_t _M0L3lenS1375;
  int32_t _M0L8new__capS427;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS425 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS426);
  _M0L3lenS1375 = _M0L4selfS426->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS427
  = _M0FPB23array__growth__capacity(_M0L8old__capS425, _M0L3lenS1375, _M0L8requiredS428);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS426, _M0L8new__capS427);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS410,
  int32_t _M0L13new__capacityS413
) {
  moonbit_string_t* _M0L8old__bufS409;
  int32_t _M0L3lenS411;
  int32_t _M0L9copy__lenS412;
  moonbit_string_t* _M0L8new__bufS414;
  moonbit_string_t* _M0L6_2aoldS2014;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS409 = _M0L4selfS410->$0;
  _M0L3lenS411 = _M0L4selfS410->$1;
  if (_M0L3lenS411 < _M0L13new__capacityS413) {
    _M0L9copy__lenS412 = _M0L3lenS411;
  } else {
    _M0L9copy__lenS412 = _M0L13new__capacityS413;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS409);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS414
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS409, _M0L13new__capacityS413, _M0L9copy__lenS412, 0, 0);
  _M0L6_2aoldS2014 = _M0L4selfS410->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2014);
  _M0L4selfS410->$0 = _M0L8new__bufS414;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS416,
  int32_t _M0L13new__capacityS419
) {
  struct _M0TUsiE** _M0L8old__bufS415;
  int32_t _M0L3lenS417;
  int32_t _M0L9copy__lenS418;
  struct _M0TUsiE** _M0L8new__bufS420;
  struct _M0TUsiE** _M0L6_2aoldS2015;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS415 = _M0L4selfS416->$0;
  _M0L3lenS417 = _M0L4selfS416->$1;
  if (_M0L3lenS417 < _M0L13new__capacityS419) {
    _M0L9copy__lenS418 = _M0L3lenS417;
  } else {
    _M0L9copy__lenS418 = _M0L13new__capacityS419;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS415);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS420
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS415, _M0L13new__capacityS419, _M0L9copy__lenS418, 0, 0);
  _M0L6_2aoldS2015 = _M0L4selfS416->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2015);
  _M0L4selfS416->$0 = _M0L8new__bufS420;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS407
) {
  moonbit_string_t* _M0L6_2atmpS1372;
  int32_t _result_2103;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1372 = _M0MPC15array5Array6bufferGsE(_M0L4selfS407);
  _result_2103 = Moonbit_array_length(_M0L6_2atmpS1372);
  moonbit_decref_cycle_free(_M0L6_2atmpS1372);
  return _result_2103;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS408
) {
  struct _M0TUsiE** _M0L6_2atmpS1373;
  int32_t _result_2104;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1373 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS408);
  _result_2104 = Moonbit_array_length(_M0L6_2atmpS1373);
  moonbit_decref_cycle_free(_M0L6_2atmpS1373);
  return _result_2104;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS403,
  int32_t _M0L3lenS401,
  int32_t _M0L8requiredS400
) {
  int32_t _M0L5startS402;
  int32_t _M0L5spaceS404;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS400 < _M0L3lenS401) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_17.data);
  }
  if (_M0L7currentS403 == 0) {
    _M0L5startS402 = 8;
  } else {
    _M0L5startS402 = _M0L7currentS403;
  }
  _M0L5spaceS404 = _M0L5startS402;
  while (1) {
    if (_M0L5spaceS404 < _M0L8requiredS400) {
      int32_t _M0L4nextS405 = _M0L5spaceS404 * 2;
      if (_M0L4nextS405 <= _M0L5spaceS404) {
        return _M0L8requiredS400;
      }
      _M0L5spaceS404 = _M0L4nextS405;
      continue;
    } else {
      return _M0L5spaceS404;
    }
    break;
  }
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS396) {
  float* _M0L8_2afieldS2016;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2016 = _M0L4selfS396->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2016);
  return _M0L8_2afieldS2016;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS397) {
  uint8_t* _M0L8_2afieldS2017;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2017 = _M0L4selfS397->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2017);
  return _M0L8_2afieldS2017;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS398
) {
  moonbit_string_t* _M0L8_2afieldS2018;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2018 = _M0L4selfS398->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2018);
  return _M0L8_2afieldS2018;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS399
) {
  struct _M0TUsiE** _M0L8_2afieldS2019;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2019 = _M0L4selfS399->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2019);
  return _M0L8_2afieldS2019;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS395
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS395);
  return _M0L4selfS395;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS394,
  struct _M0TPC16string10StringView _M0L3strS392
) {
  int32_t _M0L3endS1370;
  int32_t _M0L5startS1371;
  int32_t _M0L8str__lenS391;
  int32_t _M0L3lenS1369;
  int32_t _M0L8requiredS393;
  uint16_t* _M0L4dataS1362;
  int32_t _M0L6_2atmpS1361;
  int32_t _if__result_2106;
  uint16_t* _M0L4dataS1363;
  int32_t _M0L3lenS1364;
  moonbit_string_t _M0L6_2atmpS1365;
  int32_t _M0L6_2atmpS1366;
  int32_t _M0L3lenS1368;
  int32_t _M0L6_2atmpS1367;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1370 = _M0L3strS392.$2;
  _M0L5startS1371 = _M0L3strS392.$1;
  _M0L8str__lenS391 = _M0L3endS1370 - _M0L5startS1371;
  if (_M0L8str__lenS391 == 0) {
    return 0;
  }
  _M0L3lenS1369 = _M0L4selfS394->$1;
  _M0L8requiredS393 = _M0L3lenS1369 + _M0L8str__lenS391;
  _M0L4dataS1362 = _M0L4selfS394->$0;
  _M0L6_2atmpS1361 = Moonbit_array_length(_M0L4dataS1362);
  if (_M0L8requiredS393 > _M0L6_2atmpS1361) {
    _if__result_2106 = 1;
  } else {
    int32_t _M0L3lenS1360 = _M0L4selfS394->$1;
    _if__result_2106 = _M0L8requiredS393 < _M0L3lenS1360;
  }
  if (_if__result_2106) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS394, _M0L8requiredS393);
  }
  _M0L4dataS1363 = _M0L4selfS394->$0;
  _M0L3lenS1364 = _M0L4selfS394->$1;
  moonbit_incref_cycle_free(_M0L4dataS1363);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1365 = _M0MPC16string10StringView4data(_M0L3strS392);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1366 = _M0MPC16string10StringView13start__offset(_M0L3strS392);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1363, _M0L3lenS1364, _M0L6_2atmpS1365, _M0L6_2atmpS1366, _M0L8str__lenS391);
  moonbit_decref_cycle_free(_M0L4dataS1363);
  moonbit_decref_cycle_free(_M0L6_2atmpS1365);
  _M0L3lenS1368 = _M0L4selfS394->$1;
  _M0L6_2atmpS1367 = _M0L3lenS1368 + _M0L8str__lenS391;
  _M0L4selfS394->$1 = _M0L6_2atmpS1367;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS388,
  int32_t _M0L5startS386,
  int32_t _M0L3endS387
) {
  int32_t _if__result_2107;
  int32_t _M0L3lenS389;
  int32_t _M0L6_2atmpS1359;
  moonbit_bytes_t _M0L5bytesS390;
  moonbit_bytes_t _M0L6_2atmpS1358;
  moonbit_string_t _result_2108;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS386 == 0) {
    int32_t _M0L6_2atmpS1357 = Moonbit_array_length(_M0L3strS388);
    _if__result_2107 = _M0L3endS387 == _M0L6_2atmpS1357;
  } else {
    _if__result_2107 = 0;
  }
  if (_if__result_2107) {
    moonbit_incref_cycle_free(_M0L3strS388);
    return _M0L3strS388;
  }
  _M0L3lenS389 = _M0L3endS387 - _M0L5startS386;
  _M0L6_2atmpS1359 = _M0L3lenS389 * 2;
  _M0L5bytesS390 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1359, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS390, 0, _M0L3strS388, _M0L5startS386, _M0L3lenS389);
  _M0L6_2atmpS1358 = _M0L5bytesS390;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2108
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1358, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1358);
  return _result_2108;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS381,
  int32_t _M0L6offsetS385,
  int64_t _M0L6lengthS383
) {
  int32_t _M0L3lenS380;
  int32_t _M0L6lengthS382;
  int32_t _if__result_2109;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS380 = Moonbit_array_length(_M0L4selfS381);
  if (_M0L6lengthS383 == 4294967296ll) {
    _M0L6lengthS382 = _M0L3lenS380 - _M0L6offsetS385;
  } else {
    int64_t _M0L7_2aSomeS384 = _M0L6lengthS383;
    _M0L6lengthS382 = (int32_t)_M0L7_2aSomeS384;
  }
  if (_M0L6offsetS385 >= 0) {
    if (_M0L6lengthS382 >= 0) {
      int32_t _M0L6_2atmpS1356 = _M0L6offsetS385 + _M0L6lengthS382;
      _if__result_2109 = _M0L6_2atmpS1356 <= _M0L3lenS380;
    } else {
      _if__result_2109 = 0;
    }
  } else {
    _if__result_2109 = 0;
  }
  if (_if__result_2109) {
    moonbit_incref_cycle_free(_M0L4selfS381);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS381, _M0L6offsetS385, _M0L6lengthS382);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS372,
  int32_t _M0L13bytes__offsetS367,
  moonbit_string_t _M0L3strS374,
  int32_t _M0L11str__offsetS370,
  int32_t _M0L6lengthS368
) {
  int32_t _M0L6_2atmpS1355;
  int32_t _M0L6_2atmpS1354;
  int32_t _M0L2e1S366;
  int32_t _M0L6_2atmpS1353;
  int32_t _M0L2e2S369;
  int32_t _M0L4len1S371;
  int32_t _M0L4len2S373;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1355 = _M0L6lengthS368 * 2;
  _M0L6_2atmpS1354 = _M0L13bytes__offsetS367 + _M0L6_2atmpS1355;
  _M0L2e1S366 = _M0L6_2atmpS1354 - 1;
  _M0L6_2atmpS1353 = _M0L11str__offsetS370 + _M0L6lengthS368;
  _M0L2e2S369 = _M0L6_2atmpS1353 - 1;
  _M0L4len1S371 = Moonbit_array_length(_M0L4selfS372);
  _M0L4len2S373 = Moonbit_array_length(_M0L3strS374);
  if (
    _M0L6lengthS368 >= 0
    && _M0L13bytes__offsetS367 >= 0
    && _M0L2e1S366 < _M0L4len1S371
    && _M0L11str__offsetS370 >= 0
    && _M0L2e2S369 < _M0L4len2S373
  ) {
    int32_t _M0L16end__str__offsetS375 =
      _M0L11str__offsetS370 + _M0L6lengthS368;
    int32_t _M0L1iS376 = _M0L11str__offsetS370;
    int32_t _M0L1jS377 = _M0L13bytes__offsetS367;
    while (1) {
      if (_M0L1iS376 < _M0L16end__str__offsetS375) {
        int32_t _M0L6_2atmpS1350 = _M0L3strS374[_M0L1iS376];
        int32_t _M0L6_2atmpS1349 = (int32_t)_M0L6_2atmpS1350;
        uint32_t _M0L1cS378 = *(uint32_t*)&_M0L6_2atmpS1349;
        uint32_t _M0L6_2atmpS1345 = _M0L1cS378 & 255u;
        int32_t _M0L6_2atmpS1344;
        int32_t _M0L6_2atmpS1346;
        uint32_t _M0L6_2atmpS1348;
        int32_t _M0L6_2atmpS1347;
        int32_t _M0L6_2atmpS1351;
        int32_t _M0L6_2atmpS1352;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1344 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1345);
        if (
          _M0L1jS377 < 0 || _M0L1jS377 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L1jS377] = _M0L6_2atmpS1344;
        _M0L6_2atmpS1346 = _M0L1jS377 + 1;
        _M0L6_2atmpS1348 = _M0L1cS378 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1347 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1348);
        if (
          _M0L6_2atmpS1346 < 0
          || _M0L6_2atmpS1346 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L6_2atmpS1346] = _M0L6_2atmpS1347;
        _M0L6_2atmpS1351 = _M0L1iS376 + 1;
        _M0L6_2atmpS1352 = _M0L1jS377 + 2;
        _M0L1iS376 = _M0L6_2atmpS1351;
        _M0L1jS377 = _M0L6_2atmpS1352;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS365) {
  int32_t _M0L6_2atmpS1343;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1343 = *(int32_t*)&_M0L4selfS365;
  return _M0L6_2atmpS1343 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS357,
  int32_t _M0L5radixS356
) {
  uint16_t* _M0L6bufferS358;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS356 < 2 || _M0L5radixS356 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS357 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  switch (_M0L5radixS356) {
    case 10: {
      int32_t _M0L3lenS359;
      uint16_t* _M0L6bufferS360;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS359 = _M0FPB12dec__count64(_M0L4selfS357);
      _M0L6bufferS360 = (uint16_t*)moonbit_make_string(_M0L3lenS359, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS360, _M0L4selfS357, 0, _M0L3lenS359);
      _M0L6bufferS358 = _M0L6bufferS360;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS361;
      uint16_t* _M0L6bufferS362;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS361 = _M0FPB12hex__count64(_M0L4selfS357);
      _M0L6bufferS362 = (uint16_t*)moonbit_make_string(_M0L3lenS361, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS362, _M0L4selfS357, 0, _M0L3lenS361);
      _M0L6bufferS358 = _M0L6bufferS362;
      break;
    }
    default: {
      int32_t _M0L3lenS363;
      uint16_t* _M0L6bufferS364;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS363 = _M0FPB14radix__count64(_M0L4selfS357, _M0L5radixS356);
      _M0L6bufferS364 = (uint16_t*)moonbit_make_string(_M0L3lenS363, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS364, _M0L4selfS357, 0, _M0L3lenS363, _M0L5radixS356);
      _M0L6bufferS358 = _M0L6bufferS364;
      break;
    }
  }
  return _M0L6bufferS358;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS340,
  int32_t _M0L5radixS339
) {
  int32_t _M0L12is__negativeS341;
  uint64_t _M0L3numS342;
  uint16_t* _M0L6bufferS343;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS339 < 2 || _M0L5radixS339 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS340 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS341 = _M0L4selfS340 < 0ll;
  if (_M0L12is__negativeS341) {
    int64_t _M0L6_2atmpS1342 = -_M0L4selfS340;
    _M0L3numS342 = *(uint64_t*)&_M0L6_2atmpS1342;
  } else {
    _M0L3numS342 = *(uint64_t*)&_M0L4selfS340;
  }
  switch (_M0L5radixS339) {
    case 10: {
      int32_t _M0L10digit__lenS344;
      int32_t _M0L6_2atmpS1339;
      int32_t _M0L10total__lenS345;
      uint16_t* _M0L6bufferS346;
      int32_t _M0L12digit__startS347;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS344 = _M0FPB12dec__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1339 = 1;
      } else {
        _M0L6_2atmpS1339 = 0;
      }
      _M0L10total__lenS345 = _M0L10digit__lenS344 + _M0L6_2atmpS1339;
      _M0L6bufferS346
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS345, 0);
      if (_M0L12is__negativeS341) {
        _M0L12digit__startS347 = 1;
      } else {
        _M0L12digit__startS347 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS346, _M0L3numS342, _M0L12digit__startS347, _M0L10total__lenS345);
      _M0L6bufferS343 = _M0L6bufferS346;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS348;
      int32_t _M0L6_2atmpS1340;
      int32_t _M0L10total__lenS349;
      uint16_t* _M0L6bufferS350;
      int32_t _M0L12digit__startS351;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS348 = _M0FPB12hex__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1340 = 1;
      } else {
        _M0L6_2atmpS1340 = 0;
      }
      _M0L10total__lenS349 = _M0L10digit__lenS348 + _M0L6_2atmpS1340;
      _M0L6bufferS350
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS349, 0);
      if (_M0L12is__negativeS341) {
        _M0L12digit__startS351 = 1;
      } else {
        _M0L12digit__startS351 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS350, _M0L3numS342, _M0L12digit__startS351, _M0L10total__lenS349);
      _M0L6bufferS343 = _M0L6bufferS350;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS352;
      int32_t _M0L6_2atmpS1341;
      int32_t _M0L10total__lenS353;
      uint16_t* _M0L6bufferS354;
      int32_t _M0L12digit__startS355;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS352
      = _M0FPB14radix__count64(_M0L3numS342, _M0L5radixS339);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1341 = 1;
      } else {
        _M0L6_2atmpS1341 = 0;
      }
      _M0L10total__lenS353 = _M0L10digit__lenS352 + _M0L6_2atmpS1341;
      _M0L6bufferS354
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS353, 0);
      if (_M0L12is__negativeS341) {
        _M0L12digit__startS355 = 1;
      } else {
        _M0L12digit__startS355 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS354, _M0L3numS342, _M0L12digit__startS355, _M0L10total__lenS353, _M0L5radixS339);
      _M0L6bufferS343 = _M0L6bufferS354;
      break;
    }
  }
  if (_M0L12is__negativeS341) {
    _M0L6bufferS343[0] = 45;
  }
  return _M0L6bufferS343;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS325,
  uint64_t _M0L3numS337,
  int32_t _M0L12digit__startS326,
  int32_t _M0L10total__lenS338
) {
  int32_t _M0L6_2atmpS1338;
  uint64_t _M0L3numS315;
  int32_t _M0L6offsetS316;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1338 = _M0L10total__lenS338 - _M0L12digit__startS326;
  _M0L3numS315 = _M0L3numS337;
  _M0L6offsetS316 = _M0L6_2atmpS1338;
  while (1) {
    if (_M0L3numS315 >= 10000ull) {
      uint64_t _M0L1tS317 = _M0L3numS315 / 10000ull;
      uint64_t _M0L6_2atmpS1315 = _M0L3numS315 % 10000ull;
      int32_t _M0L1rS318 = (int32_t)_M0L6_2atmpS1315;
      int32_t _M0L2d1S319 = _M0L1rS318 / 100;
      int32_t _M0L2d2S320 = _M0L1rS318 % 100;
      int32_t _M0L6_2atmpS1314 = _M0L2d1S319 / 10;
      int32_t _M0L6_2atmpS1313 = 48 + _M0L6_2atmpS1314;
      int32_t _M0L6d1__hiS321 = (uint16_t)_M0L6_2atmpS1313;
      int32_t _M0L6_2atmpS1312 = _M0L2d1S319 % 10;
      int32_t _M0L6_2atmpS1311 = 48 + _M0L6_2atmpS1312;
      int32_t _M0L6d1__loS322 = (uint16_t)_M0L6_2atmpS1311;
      int32_t _M0L6_2atmpS1310 = _M0L2d2S320 / 10;
      int32_t _M0L6_2atmpS1309 = 48 + _M0L6_2atmpS1310;
      int32_t _M0L6d2__hiS323 = (uint16_t)_M0L6_2atmpS1309;
      int32_t _M0L6_2atmpS1308 = _M0L2d2S320 % 10;
      int32_t _M0L6_2atmpS1307 = 48 + _M0L6_2atmpS1308;
      int32_t _M0L6d2__loS324 = (uint16_t)_M0L6_2atmpS1307;
      int32_t _M0L6_2atmpS1299 = _M0L12digit__startS326 + _M0L6offsetS316;
      int32_t _M0L6_2atmpS1298 = _M0L6_2atmpS1299 - 4;
      int32_t _M0L6_2atmpS1301;
      int32_t _M0L6_2atmpS1300;
      int32_t _M0L6_2atmpS1303;
      int32_t _M0L6_2atmpS1302;
      int32_t _M0L6_2atmpS1305;
      int32_t _M0L6_2atmpS1304;
      int32_t _M0L6_2atmpS1306;
      _M0L6bufferS325[_M0L6_2atmpS1298] = _M0L6d1__hiS321;
      _M0L6_2atmpS1301 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1300 = _M0L6_2atmpS1301 - 3;
      _M0L6bufferS325[_M0L6_2atmpS1300] = _M0L6d1__loS322;
      _M0L6_2atmpS1303 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1302 = _M0L6_2atmpS1303 - 2;
      _M0L6bufferS325[_M0L6_2atmpS1302] = _M0L6d2__hiS323;
      _M0L6_2atmpS1305 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1304 = _M0L6_2atmpS1305 - 1;
      _M0L6bufferS325[_M0L6_2atmpS1304] = _M0L6d2__loS324;
      _M0L6_2atmpS1306 = _M0L6offsetS316 - 4;
      _M0L3numS315 = _M0L1tS317;
      _M0L6offsetS316 = _M0L6_2atmpS1306;
      continue;
    } else {
      int32_t _M0L6_2atmpS1337 = (int32_t)_M0L3numS315;
      int32_t _M0L9remainingS328 = _M0L6_2atmpS1337;
      int32_t _M0L6offsetS329 = _M0L6offsetS316;
      while (1) {
        if (_M0L9remainingS328 >= 100) {
          int32_t _M0L1tS330 = _M0L9remainingS328 / 100;
          int32_t _M0L1dS331 = _M0L9remainingS328 % 100;
          int32_t _M0L6_2atmpS1324 = _M0L1dS331 / 10;
          int32_t _M0L6_2atmpS1323 = 48 + _M0L6_2atmpS1324;
          int32_t _M0L5d__hiS332 = (uint16_t)_M0L6_2atmpS1323;
          int32_t _M0L6_2atmpS1322 = _M0L1dS331 % 10;
          int32_t _M0L6_2atmpS1321 = 48 + _M0L6_2atmpS1322;
          int32_t _M0L5d__loS333 = (uint16_t)_M0L6_2atmpS1321;
          int32_t _M0L6_2atmpS1317 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1316 = _M0L6_2atmpS1317 - 2;
          int32_t _M0L6_2atmpS1319;
          int32_t _M0L6_2atmpS1318;
          int32_t _M0L6_2atmpS1320;
          _M0L6bufferS325[_M0L6_2atmpS1316] = _M0L5d__hiS332;
          _M0L6_2atmpS1319 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1318 = _M0L6_2atmpS1319 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1318] = _M0L5d__loS333;
          _M0L6_2atmpS1320 = _M0L6offsetS329 - 2;
          _M0L9remainingS328 = _M0L1tS330;
          _M0L6offsetS329 = _M0L6_2atmpS1320;
          continue;
        } else if (_M0L9remainingS328 >= 10) {
          int32_t _M0L6_2atmpS1332 = _M0L9remainingS328 / 10;
          int32_t _M0L6_2atmpS1331 = 48 + _M0L6_2atmpS1332;
          int32_t _M0L5d__hiS335 = (uint16_t)_M0L6_2atmpS1331;
          int32_t _M0L6_2atmpS1330 = _M0L9remainingS328 % 10;
          int32_t _M0L6_2atmpS1329 = 48 + _M0L6_2atmpS1330;
          int32_t _M0L5d__loS336 = (uint16_t)_M0L6_2atmpS1329;
          int32_t _M0L6_2atmpS1326 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1325 = _M0L6_2atmpS1326 - 2;
          int32_t _M0L6_2atmpS1328;
          int32_t _M0L6_2atmpS1327;
          _M0L6bufferS325[_M0L6_2atmpS1325] = _M0L5d__hiS335;
          _M0L6_2atmpS1328 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1327 = _M0L6_2atmpS1328 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1327] = _M0L5d__loS336;
        } else {
          int32_t _M0L6_2atmpS1336 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1333 = _M0L6_2atmpS1336 - 1;
          int32_t _M0L6_2atmpS1335 = 48 + _M0L9remainingS328;
          int32_t _M0L6_2atmpS1334 = (uint16_t)_M0L6_2atmpS1335;
          _M0L6bufferS325[_M0L6_2atmpS1333] = _M0L6_2atmpS1334;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS305,
  uint64_t _M0L3numS309,
  int32_t _M0L12digit__startS306,
  int32_t _M0L10total__lenS308,
  int32_t _M0L5radixS299
) {
  uint64_t _M0L4baseS298;
  int32_t _M0L6_2atmpS1283;
  int32_t _M0L6_2atmpS1282;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS298 = _M0MPC13int3Int10to__uint64(_M0L5radixS299);
  _M0L6_2atmpS1283 = _M0L5radixS299 - 1;
  _M0L6_2atmpS1282 = _M0L5radixS299 & _M0L6_2atmpS1283;
  if (_M0L6_2atmpS1282 == 0) {
    int32_t _M0L5shiftS300;
    uint64_t _M0L4maskS301;
    int32_t _M0L6_2atmpS1290;
    int32_t _M0L6offsetS302;
    uint64_t _M0L1nS303;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS300 = moonbit_ctz32(_M0L5radixS299);
    _M0L4maskS301 = _M0L4baseS298 - 1ull;
    _M0L6_2atmpS1290 = _M0L10total__lenS308 - _M0L12digit__startS306;
    _M0L6offsetS302 = _M0L6_2atmpS1290;
    _M0L1nS303 = _M0L3numS309;
    while (1) {
      if (_M0L1nS303 > 0ull) {
        uint64_t _M0L6_2atmpS1289 = _M0L1nS303 & _M0L4maskS301;
        int32_t _M0L5digitS304 = (int32_t)_M0L6_2atmpS1289;
        int32_t _M0L6_2atmpS1286 = _M0L12digit__startS306 + _M0L6offsetS302;
        int32_t _M0L6_2atmpS1284 = _M0L6_2atmpS1286 - 1;
        int32_t _M0L6_2atmpS1285 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS304];
        int32_t _M0L6_2atmpS1287;
        uint64_t _M0L6_2atmpS1288;
        _M0L6bufferS305[_M0L6_2atmpS1284] = _M0L6_2atmpS1285;
        _M0L6_2atmpS1287 = _M0L6offsetS302 - 1;
        _M0L6_2atmpS1288 = _M0L1nS303 >> (_M0L5shiftS300 & 63);
        _M0L6offsetS302 = _M0L6_2atmpS1287;
        _M0L1nS303 = _M0L6_2atmpS1288;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1297 = _M0L10total__lenS308 - _M0L12digit__startS306;
    int32_t _M0L6offsetS310 = _M0L6_2atmpS1297;
    uint64_t _M0L1nS311 = _M0L3numS309;
    while (1) {
      if (_M0L1nS311 > 0ull) {
        uint64_t _M0L1qS312 = _M0L1nS311 / _M0L4baseS298;
        uint64_t _M0L6_2atmpS1296 = _M0L1qS312 * _M0L4baseS298;
        uint64_t _M0L6_2atmpS1295 = _M0L1nS311 - _M0L6_2atmpS1296;
        int32_t _M0L5digitS313 = (int32_t)_M0L6_2atmpS1295;
        int32_t _M0L6_2atmpS1293 = _M0L12digit__startS306 + _M0L6offsetS310;
        int32_t _M0L6_2atmpS1291 = _M0L6_2atmpS1293 - 1;
        int32_t _M0L6_2atmpS1292 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS313];
        int32_t _M0L6_2atmpS1294;
        _M0L6bufferS305[_M0L6_2atmpS1291] = _M0L6_2atmpS1292;
        _M0L6_2atmpS1294 = _M0L6offsetS310 - 1;
        _M0L6offsetS310 = _M0L6_2atmpS1294;
        _M0L1nS311 = _M0L1qS312;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS292,
  uint64_t _M0L3numS297,
  int32_t _M0L12digit__startS293,
  int32_t _M0L10total__lenS296
) {
  int32_t _M0L6_2atmpS1281;
  int32_t _M0L6offsetS287;
  uint64_t _M0L1nS288;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1281 = _M0L10total__lenS296 - _M0L12digit__startS293;
  _M0L6offsetS287 = _M0L6_2atmpS1281;
  _M0L1nS288 = _M0L3numS297;
  while (1) {
    if (_M0L6offsetS287 >= 2) {
      uint64_t _M0L6_2atmpS1278 = _M0L1nS288 & 255ull;
      int32_t _M0L9byte__valS289 = (int32_t)_M0L6_2atmpS1278;
      int32_t _M0L2hiS290 = _M0L9byte__valS289 / 16;
      int32_t _M0L2loS291 = _M0L9byte__valS289 % 16;
      int32_t _M0L6_2atmpS1272 = _M0L12digit__startS293 + _M0L6offsetS287;
      int32_t _M0L6_2atmpS1270 = _M0L6_2atmpS1272 - 2;
      int32_t _M0L6_2atmpS1271 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L2hiS290];
      int32_t _M0L6_2atmpS1275;
      int32_t _M0L6_2atmpS1273;
      int32_t _M0L6_2atmpS1274;
      int32_t _M0L6_2atmpS1276;
      uint64_t _M0L6_2atmpS1277;
      _M0L6bufferS292[_M0L6_2atmpS1270] = _M0L6_2atmpS1271;
      _M0L6_2atmpS1275 = _M0L12digit__startS293 + _M0L6offsetS287;
      _M0L6_2atmpS1273 = _M0L6_2atmpS1275 - 1;
      _M0L6_2atmpS1274
      = ((moonbit_string_t)moonbit_string_literal_19.data)[
        _M0L2loS291
      ];
      _M0L6bufferS292[_M0L6_2atmpS1273] = _M0L6_2atmpS1274;
      _M0L6_2atmpS1276 = _M0L6offsetS287 - 2;
      _M0L6_2atmpS1277 = _M0L1nS288 >> 8;
      _M0L6offsetS287 = _M0L6_2atmpS1276;
      _M0L1nS288 = _M0L6_2atmpS1277;
      continue;
    } else if (_M0L6offsetS287 == 1) {
      uint64_t _M0L6_2atmpS1280 = _M0L1nS288 & 15ull;
      int32_t _M0L6nibbleS295 = (int32_t)_M0L6_2atmpS1280;
      int32_t _M0L6_2atmpS1279 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L6nibbleS295];
      _M0L6bufferS292[_M0L12digit__startS293] = _M0L6_2atmpS1279;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS281,
  int32_t _M0L5radixS283
) {
  uint64_t _M0L4baseS282;
  uint64_t _M0L3numS284;
  int32_t _M0L5countS285;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS281 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS282 = _M0MPC13int3Int10to__uint64(_M0L5radixS283);
  _M0L3numS284 = _M0L5valueS281;
  _M0L5countS285 = 0;
  while (1) {
    if (_M0L3numS284 > 0ull) {
      uint64_t _M0L6_2atmpS1268 = _M0L3numS284 / _M0L4baseS282;
      int32_t _M0L6_2atmpS1269 = _M0L5countS285 + 1;
      _M0L3numS284 = _M0L6_2atmpS1268;
      _M0L5countS285 = _M0L6_2atmpS1269;
      continue;
    } else {
      return _M0L5countS285;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS279) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS279 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS280;
    int32_t _M0L6_2atmpS1267;
    int32_t _M0L6_2atmpS1266;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS280 = moonbit_clz64(_M0L5valueS279);
    _M0L6_2atmpS1267 = 63 - _M0L14leading__zerosS280;
    _M0L6_2atmpS1266 = _M0L6_2atmpS1267 / 4;
    return _M0L6_2atmpS1266 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS278) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS278 >= 10000000000ull) {
    if (_M0L5valueS278 >= 100000000000000ull) {
      if (_M0L5valueS278 >= 10000000000000000ull) {
        if (_M0L5valueS278 >= 1000000000000000000ull) {
          if (_M0L5valueS278 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS278 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS278 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS278 >= 1000000000000ull) {
      if (_M0L5valueS278 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS278 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS278 >= 100000ull) {
    if (_M0L5valueS278 >= 10000000ull) {
      if (_M0L5valueS278 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS278 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS278 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS278 >= 1000ull) {
    if (_M0L5valueS278 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS278 >= 100ull) {
    return 3;
  } else if (_M0L5valueS278 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS262,
  int32_t _M0L5radixS261
) {
  int32_t _M0L12is__negativeS263;
  uint32_t _M0L3numS264;
  uint16_t* _M0L6bufferS265;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS261 < 2 || _M0L5radixS261 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS262 == 0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS263 = _M0L4selfS262 < 0;
  if (_M0L12is__negativeS263) {
    int32_t _M0L6_2atmpS1265 = -_M0L4selfS262;
    _M0L3numS264 = *(uint32_t*)&_M0L6_2atmpS1265;
  } else {
    _M0L3numS264 = *(uint32_t*)&_M0L4selfS262;
  }
  switch (_M0L5radixS261) {
    case 10: {
      int32_t _M0L10digit__lenS266;
      int32_t _M0L6_2atmpS1262;
      int32_t _M0L10total__lenS267;
      uint16_t* _M0L6bufferS268;
      int32_t _M0L12digit__startS269;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS266 = _M0FPB12dec__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1262 = 1;
      } else {
        _M0L6_2atmpS1262 = 0;
      }
      _M0L10total__lenS267 = _M0L10digit__lenS266 + _M0L6_2atmpS1262;
      _M0L6bufferS268
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS267, 0);
      if (_M0L12is__negativeS263) {
        _M0L12digit__startS269 = 1;
      } else {
        _M0L12digit__startS269 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS268, _M0L3numS264, _M0L12digit__startS269, _M0L10total__lenS267);
      _M0L6bufferS265 = _M0L6bufferS268;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS270;
      int32_t _M0L6_2atmpS1263;
      int32_t _M0L10total__lenS271;
      uint16_t* _M0L6bufferS272;
      int32_t _M0L12digit__startS273;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS270 = _M0FPB12hex__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1263 = 1;
      } else {
        _M0L6_2atmpS1263 = 0;
      }
      _M0L10total__lenS271 = _M0L10digit__lenS270 + _M0L6_2atmpS1263;
      _M0L6bufferS272
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS271, 0);
      if (_M0L12is__negativeS263) {
        _M0L12digit__startS273 = 1;
      } else {
        _M0L12digit__startS273 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS272, _M0L3numS264, _M0L12digit__startS273, _M0L10total__lenS271);
      _M0L6bufferS265 = _M0L6bufferS272;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS274;
      int32_t _M0L6_2atmpS1264;
      int32_t _M0L10total__lenS275;
      uint16_t* _M0L6bufferS276;
      int32_t _M0L12digit__startS277;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS274
      = _M0FPB14radix__count32(_M0L3numS264, _M0L5radixS261);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1264 = 1;
      } else {
        _M0L6_2atmpS1264 = 0;
      }
      _M0L10total__lenS275 = _M0L10digit__lenS274 + _M0L6_2atmpS1264;
      _M0L6bufferS276
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS275, 0);
      if (_M0L12is__negativeS263) {
        _M0L12digit__startS277 = 1;
      } else {
        _M0L12digit__startS277 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS276, _M0L3numS264, _M0L12digit__startS277, _M0L10total__lenS275, _M0L5radixS261);
      _M0L6bufferS265 = _M0L6bufferS276;
      break;
    }
  }
  if (_M0L12is__negativeS263) {
    _M0L6bufferS265[0] = 45;
  }
  return _M0L6bufferS265;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS255,
  int32_t _M0L5radixS257
) {
  uint32_t _M0L4baseS256;
  uint32_t _M0L3numS258;
  int32_t _M0L5countS259;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS255 == 0u) {
    return 1;
  }
  _M0L4baseS256 = *(uint32_t*)&_M0L5radixS257;
  _M0L3numS258 = _M0L5valueS255;
  _M0L5countS259 = 0;
  while (1) {
    if (_M0L3numS258 > 0u) {
      uint32_t _M0L6_2atmpS1260 = _M0L3numS258 / _M0L4baseS256;
      int32_t _M0L6_2atmpS1261 = _M0L5countS259 + 1;
      _M0L3numS258 = _M0L6_2atmpS1260;
      _M0L5countS259 = _M0L6_2atmpS1261;
      continue;
    } else {
      return _M0L5countS259;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS253) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS253 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS254;
    int32_t _M0L6_2atmpS1259;
    int32_t _M0L6_2atmpS1258;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS254 = moonbit_clz32(_M0L5valueS253);
    _M0L6_2atmpS1259 = 31 - _M0L14leading__zerosS254;
    _M0L6_2atmpS1258 = _M0L6_2atmpS1259 / 4;
    return _M0L6_2atmpS1258 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS252) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS252 >= 100000u) {
    if (_M0L5valueS252 >= 10000000u) {
      if (_M0L5valueS252 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS252 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS252 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS252 >= 1000u) {
    if (_M0L5valueS252 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS252 >= 100u) {
    return 3;
  } else if (_M0L5valueS252 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS238,
  uint32_t _M0L3numS250,
  int32_t _M0L12digit__startS239,
  int32_t _M0L10total__lenS251
) {
  int32_t _M0L6_2atmpS1257;
  uint32_t _M0L3numS228;
  int32_t _M0L6offsetS229;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1257 = _M0L10total__lenS251 - _M0L12digit__startS239;
  _M0L3numS228 = _M0L3numS250;
  _M0L6offsetS229 = _M0L6_2atmpS1257;
  while (1) {
    if (_M0L3numS228 >= 10000u) {
      uint32_t _M0L1tS230 = _M0L3numS228 / 10000u;
      uint32_t _M0L6_2atmpS1234 = _M0L3numS228 % 10000u;
      int32_t _M0L1rS231 = *(int32_t*)&_M0L6_2atmpS1234;
      int32_t _M0L2d1S232 = _M0L1rS231 / 100;
      int32_t _M0L2d2S233 = _M0L1rS231 % 100;
      int32_t _M0L6_2atmpS1233 = _M0L2d1S232 / 10;
      int32_t _M0L6_2atmpS1232 = 48 + _M0L6_2atmpS1233;
      int32_t _M0L6d1__hiS234 = (uint16_t)_M0L6_2atmpS1232;
      int32_t _M0L6_2atmpS1231 = _M0L2d1S232 % 10;
      int32_t _M0L6_2atmpS1230 = 48 + _M0L6_2atmpS1231;
      int32_t _M0L6d1__loS235 = (uint16_t)_M0L6_2atmpS1230;
      int32_t _M0L6_2atmpS1229 = _M0L2d2S233 / 10;
      int32_t _M0L6_2atmpS1228 = 48 + _M0L6_2atmpS1229;
      int32_t _M0L6d2__hiS236 = (uint16_t)_M0L6_2atmpS1228;
      int32_t _M0L6_2atmpS1227 = _M0L2d2S233 % 10;
      int32_t _M0L6_2atmpS1226 = 48 + _M0L6_2atmpS1227;
      int32_t _M0L6d2__loS237 = (uint16_t)_M0L6_2atmpS1226;
      int32_t _M0L6_2atmpS1218 = _M0L12digit__startS239 + _M0L6offsetS229;
      int32_t _M0L6_2atmpS1217 = _M0L6_2atmpS1218 - 4;
      int32_t _M0L6_2atmpS1220;
      int32_t _M0L6_2atmpS1219;
      int32_t _M0L6_2atmpS1222;
      int32_t _M0L6_2atmpS1221;
      int32_t _M0L6_2atmpS1224;
      int32_t _M0L6_2atmpS1223;
      int32_t _M0L6_2atmpS1225;
      _M0L6bufferS238[_M0L6_2atmpS1217] = _M0L6d1__hiS234;
      _M0L6_2atmpS1220 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1219 = _M0L6_2atmpS1220 - 3;
      _M0L6bufferS238[_M0L6_2atmpS1219] = _M0L6d1__loS235;
      _M0L6_2atmpS1222 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1221 = _M0L6_2atmpS1222 - 2;
      _M0L6bufferS238[_M0L6_2atmpS1221] = _M0L6d2__hiS236;
      _M0L6_2atmpS1224 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1223 = _M0L6_2atmpS1224 - 1;
      _M0L6bufferS238[_M0L6_2atmpS1223] = _M0L6d2__loS237;
      _M0L6_2atmpS1225 = _M0L6offsetS229 - 4;
      _M0L3numS228 = _M0L1tS230;
      _M0L6offsetS229 = _M0L6_2atmpS1225;
      continue;
    } else {
      int32_t _M0L6_2atmpS1256 = *(int32_t*)&_M0L3numS228;
      int32_t _M0L9remainingS241 = _M0L6_2atmpS1256;
      int32_t _M0L6offsetS242 = _M0L6offsetS229;
      while (1) {
        if (_M0L9remainingS241 >= 100) {
          int32_t _M0L1tS243 = _M0L9remainingS241 / 100;
          int32_t _M0L1dS244 = _M0L9remainingS241 % 100;
          int32_t _M0L6_2atmpS1243 = _M0L1dS244 / 10;
          int32_t _M0L6_2atmpS1242 = 48 + _M0L6_2atmpS1243;
          int32_t _M0L5d__hiS245 = (uint16_t)_M0L6_2atmpS1242;
          int32_t _M0L6_2atmpS1241 = _M0L1dS244 % 10;
          int32_t _M0L6_2atmpS1240 = 48 + _M0L6_2atmpS1241;
          int32_t _M0L5d__loS246 = (uint16_t)_M0L6_2atmpS1240;
          int32_t _M0L6_2atmpS1236 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1235 = _M0L6_2atmpS1236 - 2;
          int32_t _M0L6_2atmpS1238;
          int32_t _M0L6_2atmpS1237;
          int32_t _M0L6_2atmpS1239;
          _M0L6bufferS238[_M0L6_2atmpS1235] = _M0L5d__hiS245;
          _M0L6_2atmpS1238 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1237 = _M0L6_2atmpS1238 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1237] = _M0L5d__loS246;
          _M0L6_2atmpS1239 = _M0L6offsetS242 - 2;
          _M0L9remainingS241 = _M0L1tS243;
          _M0L6offsetS242 = _M0L6_2atmpS1239;
          continue;
        } else if (_M0L9remainingS241 >= 10) {
          int32_t _M0L6_2atmpS1251 = _M0L9remainingS241 / 10;
          int32_t _M0L6_2atmpS1250 = 48 + _M0L6_2atmpS1251;
          int32_t _M0L5d__hiS248 = (uint16_t)_M0L6_2atmpS1250;
          int32_t _M0L6_2atmpS1249 = _M0L9remainingS241 % 10;
          int32_t _M0L6_2atmpS1248 = 48 + _M0L6_2atmpS1249;
          int32_t _M0L5d__loS249 = (uint16_t)_M0L6_2atmpS1248;
          int32_t _M0L6_2atmpS1245 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1244 = _M0L6_2atmpS1245 - 2;
          int32_t _M0L6_2atmpS1247;
          int32_t _M0L6_2atmpS1246;
          _M0L6bufferS238[_M0L6_2atmpS1244] = _M0L5d__hiS248;
          _M0L6_2atmpS1247 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1246 = _M0L6_2atmpS1247 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1246] = _M0L5d__loS249;
        } else {
          int32_t _M0L6_2atmpS1255 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1252 = _M0L6_2atmpS1255 - 1;
          int32_t _M0L6_2atmpS1254 = 48 + _M0L9remainingS241;
          int32_t _M0L6_2atmpS1253 = (uint16_t)_M0L6_2atmpS1254;
          _M0L6bufferS238[_M0L6_2atmpS1252] = _M0L6_2atmpS1253;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS218,
  uint32_t _M0L3numS222,
  int32_t _M0L12digit__startS219,
  int32_t _M0L10total__lenS221,
  int32_t _M0L5radixS212
) {
  uint32_t _M0L4baseS211;
  int32_t _M0L6_2atmpS1202;
  int32_t _M0L6_2atmpS1201;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS211 = *(uint32_t*)&_M0L5radixS212;
  _M0L6_2atmpS1202 = _M0L5radixS212 - 1;
  _M0L6_2atmpS1201 = _M0L5radixS212 & _M0L6_2atmpS1202;
  if (_M0L6_2atmpS1201 == 0) {
    int32_t _M0L5shiftS213;
    uint32_t _M0L4maskS214;
    int32_t _M0L6_2atmpS1209;
    int32_t _M0L6offsetS215;
    uint32_t _M0L1nS216;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS213 = moonbit_ctz32(_M0L5radixS212);
    _M0L4maskS214 = _M0L4baseS211 - 1u;
    _M0L6_2atmpS1209 = _M0L10total__lenS221 - _M0L12digit__startS219;
    _M0L6offsetS215 = _M0L6_2atmpS1209;
    _M0L1nS216 = _M0L3numS222;
    while (1) {
      if (_M0L1nS216 > 0u) {
        uint32_t _M0L6_2atmpS1208 = _M0L1nS216 & _M0L4maskS214;
        int32_t _M0L5digitS217 = *(int32_t*)&_M0L6_2atmpS1208;
        int32_t _M0L6_2atmpS1205 = _M0L12digit__startS219 + _M0L6offsetS215;
        int32_t _M0L6_2atmpS1203 = _M0L6_2atmpS1205 - 1;
        int32_t _M0L6_2atmpS1204 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS217];
        int32_t _M0L6_2atmpS1206;
        uint32_t _M0L6_2atmpS1207;
        _M0L6bufferS218[_M0L6_2atmpS1203] = _M0L6_2atmpS1204;
        _M0L6_2atmpS1206 = _M0L6offsetS215 - 1;
        _M0L6_2atmpS1207 = _M0L1nS216 >> (_M0L5shiftS213 & 31);
        _M0L6offsetS215 = _M0L6_2atmpS1206;
        _M0L1nS216 = _M0L6_2atmpS1207;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1216 = _M0L10total__lenS221 - _M0L12digit__startS219;
    int32_t _M0L6offsetS223 = _M0L6_2atmpS1216;
    uint32_t _M0L1nS224 = _M0L3numS222;
    while (1) {
      if (_M0L1nS224 > 0u) {
        uint32_t _M0L1qS225 = _M0L1nS224 / _M0L4baseS211;
        uint32_t _M0L6_2atmpS1215 = _M0L1qS225 * _M0L4baseS211;
        uint32_t _M0L6_2atmpS1214 = _M0L1nS224 - _M0L6_2atmpS1215;
        int32_t _M0L5digitS226 = *(int32_t*)&_M0L6_2atmpS1214;
        int32_t _M0L6_2atmpS1212 = _M0L12digit__startS219 + _M0L6offsetS223;
        int32_t _M0L6_2atmpS1210 = _M0L6_2atmpS1212 - 1;
        int32_t _M0L6_2atmpS1211 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS226];
        int32_t _M0L6_2atmpS1213;
        _M0L6bufferS218[_M0L6_2atmpS1210] = _M0L6_2atmpS1211;
        _M0L6_2atmpS1213 = _M0L6offsetS223 - 1;
        _M0L6offsetS223 = _M0L6_2atmpS1213;
        _M0L1nS224 = _M0L1qS225;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS205,
  uint32_t _M0L3numS210,
  int32_t _M0L12digit__startS206,
  int32_t _M0L10total__lenS209
) {
  int32_t _M0L6_2atmpS1200;
  int32_t _M0L6offsetS200;
  uint32_t _M0L1nS201;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1200 = _M0L10total__lenS209 - _M0L12digit__startS206;
  _M0L6offsetS200 = _M0L6_2atmpS1200;
  _M0L1nS201 = _M0L3numS210;
  while (1) {
    if (_M0L6offsetS200 >= 2) {
      uint32_t _M0L6_2atmpS1197 = _M0L1nS201 & 255u;
      int32_t _M0L9byte__valS202 = *(int32_t*)&_M0L6_2atmpS1197;
      int32_t _M0L2hiS203 = _M0L9byte__valS202 / 16;
      int32_t _M0L2loS204 = _M0L9byte__valS202 % 16;
      int32_t _M0L6_2atmpS1191 = _M0L12digit__startS206 + _M0L6offsetS200;
      int32_t _M0L6_2atmpS1189 = _M0L6_2atmpS1191 - 2;
      int32_t _M0L6_2atmpS1190 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L2hiS203];
      int32_t _M0L6_2atmpS1194;
      int32_t _M0L6_2atmpS1192;
      int32_t _M0L6_2atmpS1193;
      int32_t _M0L6_2atmpS1195;
      uint32_t _M0L6_2atmpS1196;
      _M0L6bufferS205[_M0L6_2atmpS1189] = _M0L6_2atmpS1190;
      _M0L6_2atmpS1194 = _M0L12digit__startS206 + _M0L6offsetS200;
      _M0L6_2atmpS1192 = _M0L6_2atmpS1194 - 1;
      _M0L6_2atmpS1193
      = ((moonbit_string_t)moonbit_string_literal_19.data)[
        _M0L2loS204
      ];
      _M0L6bufferS205[_M0L6_2atmpS1192] = _M0L6_2atmpS1193;
      _M0L6_2atmpS1195 = _M0L6offsetS200 - 2;
      _M0L6_2atmpS1196 = _M0L1nS201 >> 8;
      _M0L6offsetS200 = _M0L6_2atmpS1195;
      _M0L1nS201 = _M0L6_2atmpS1196;
      continue;
    } else if (_M0L6offsetS200 == 1) {
      uint32_t _M0L6_2atmpS1199 = _M0L1nS201 & 15u;
      int32_t _M0L6nibbleS208 = *(int32_t*)&_M0L6_2atmpS1199;
      int32_t _M0L6_2atmpS1198 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L6nibbleS208];
      _M0L6bufferS205[_M0L12digit__startS206] = _M0L6_2atmpS1198;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS199
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS198;
  struct _M0TPB6Logger _M0L6_2atmpS1188;
  moonbit_string_t _result_2123;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS198);
  _M0L6_2atmpS1188
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS198
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS199, _M0L6_2atmpS1188);
  if (_M0L6_2atmpS1188.$1) {
    moonbit_decref(_M0L6_2atmpS1188.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2123 = _M0MPB13StringBuilder10to__string(_M0L6loggerS198);
  moonbit_decref_cycle_free(_M0L6loggerS198);
  return _result_2123;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS193,
  struct _M0TPB6Logger _M0L6loggerS192
) {
  moonbit_string_t _M0L6_2atmpS1185;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1185 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS193);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS192.$0->$method_0(_M0L6loggerS192.$1, _M0L6_2atmpS1185);
  moonbit_decref_cycle_free(_M0L6_2atmpS1185);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS195,
  struct _M0TPB6Logger _M0L6loggerS194
) {
  moonbit_string_t _M0L6_2atmpS1186;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1186 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS195);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS194.$0->$method_0(_M0L6loggerS194.$1, _M0L6_2atmpS1186);
  moonbit_decref_cycle_free(_M0L6_2atmpS1186);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS197,
  struct _M0TPB6Logger _M0L6loggerS196
) {
  moonbit_string_t _M0L6_2atmpS1187;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1187 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS197);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS196.$0->$method_0(_M0L6loggerS196.$1, _M0L6_2atmpS1187);
  moonbit_decref_cycle_free(_M0L6_2atmpS1187);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS191
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS191.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS190
) {
  moonbit_string_t _M0L8_2afieldS2020;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2020 = _M0L4selfS190.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2020);
  return _M0L8_2afieldS2020;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS186,
  moonbit_string_t _M0L5valueS187,
  int32_t _M0L5startS188,
  int32_t _M0L3lenS189
) {
  int32_t _M0L6_2atmpS1184;
  int64_t _M0L6_2atmpS1183;
  struct _M0TPC16string10StringView _M0L6_2atmpS1182;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1184 = _M0L5startS188 + _M0L3lenS189;
  _M0L6_2atmpS1183 = (int64_t)_M0L6_2atmpS1184;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1182
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS187, _M0L5startS188, _M0L6_2atmpS1183);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS186, _M0L6_2atmpS1182);
  moonbit_decref_cycle_free(_M0L6_2atmpS1182.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS179,
  int32_t _M0L5startS181,
  int64_t _M0L3endS183
) {
  int32_t _M0L3lenS178;
  int32_t _M0Lm2loS180;
  int32_t _M0Lm2hiS182;
  int32_t _M0L6_2atmpS1166;
  int32_t _if__result_2124;
  int32_t _M0L6_2atmpS1174;
  int32_t _if__result_2125;
  int32_t _M0L6_2atmpS1176;
  int32_t _M0L6_2atmpS1177;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS178 = Moonbit_array_length(_M0L4selfS179);
  if (_M0L5startS181 < 0) {
    _M0Lm2loS180 = 0;
  } else if (_M0L5startS181 > _M0L3lenS178) {
    _M0Lm2loS180 = _M0L3lenS178;
  } else {
    _M0Lm2loS180 = _M0L5startS181;
  }
  if (_M0L3endS183 == 4294967296ll) {
    _M0Lm2hiS182 = _M0L3lenS178;
  } else {
    int64_t _M0L7_2aSomeS184 = _M0L3endS183;
    int32_t _M0L4_2aeS185 = (int32_t)_M0L7_2aSomeS184;
    if (_M0L4_2aeS185 < 0) {
      _M0Lm2hiS182 = 0;
    } else if (_M0L4_2aeS185 > _M0L3lenS178) {
      _M0Lm2hiS182 = _M0L3lenS178;
    } else {
      _M0Lm2hiS182 = _M0L4_2aeS185;
    }
  }
  _M0L6_2atmpS1166 = _M0Lm2loS180;
  if (_M0L6_2atmpS1166 > 0) {
    int32_t _M0L6_2atmpS1165 = _M0Lm2loS180;
    if (_M0L6_2atmpS1165 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1164 = _M0Lm2loS180;
      int32_t _M0L6_2atmpS1163 = _M0L4selfS179[_M0L6_2atmpS1164];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1163)) {
        int32_t _M0L6_2atmpS1162 = _M0Lm2loS180;
        int32_t _M0L6_2atmpS1161 = _M0L6_2atmpS1162 - 1;
        int32_t _M0L6_2atmpS1160 = _M0L4selfS179[_M0L6_2atmpS1161];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2124
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1160);
      } else {
        _if__result_2124 = 0;
      }
    } else {
      _if__result_2124 = 0;
    }
  } else {
    _if__result_2124 = 0;
  }
  if (_if__result_2124) {
    int32_t _M0L6_2atmpS1167 = _M0Lm2loS180;
    _M0Lm2loS180 = _M0L6_2atmpS1167 + 1;
  }
  _M0L6_2atmpS1174 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1174 > 0) {
    int32_t _M0L6_2atmpS1173 = _M0Lm2hiS182;
    if (_M0L6_2atmpS1173 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1172 = _M0Lm2hiS182;
      int32_t _M0L6_2atmpS1171 = _M0L4selfS179[_M0L6_2atmpS1172];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1171)) {
        int32_t _M0L6_2atmpS1170 = _M0Lm2hiS182;
        int32_t _M0L6_2atmpS1169 = _M0L6_2atmpS1170 - 1;
        int32_t _M0L6_2atmpS1168 = _M0L4selfS179[_M0L6_2atmpS1169];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2125
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1168);
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
    int32_t _M0L6_2atmpS1175 = _M0Lm2hiS182;
    _M0Lm2hiS182 = _M0L6_2atmpS1175 - 1;
  }
  _M0L6_2atmpS1176 = _M0Lm2loS180;
  _M0L6_2atmpS1177 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1176 >= _M0L6_2atmpS1177) {
    int32_t _M0L6_2atmpS1178 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1179 = _M0Lm2loS180;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1178,
                                                 .$2 = _M0L6_2atmpS1179};
  } else {
    int32_t _M0L6_2atmpS1180 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1181 = _M0Lm2hiS182;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1180,
                                                 .$2 = _M0L6_2atmpS1181};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS177,
  struct _M0TPB4Show _M0L4showS176
) {
  struct _M0TPB6Logger _M0L6_2atmpS1159;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS177);
  _M0L6_2atmpS1159
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS177
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS176.$0->$method_0(_M0L4showS176.$1, _M0L6_2atmpS1159);
  if (_M0L6_2atmpS1159.$1) {
    moonbit_decref(_M0L6_2atmpS1159.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS175,
  struct _M0TPB4Show _M0L4showS174
) {
  struct _M0TPB6Logger _M0L6_2atmpS1158;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS175);
  _M0L6_2atmpS1158
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS175
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS174.$0->$method_0(_M0L4showS174.$1, _M0L6_2atmpS1158);
  if (_M0L6_2atmpS1158.$1) {
    moonbit_decref(_M0L6_2atmpS1158.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS173) {
  int64_t _M0L6_2atmpS1157;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1157 = (int64_t)_M0L4selfS173;
  return *(uint64_t*)&_M0L6_2atmpS1157;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS171,
  int32_t _M0L5quoteS172
) {
  struct _M0TPB13StringBuilder* _M0L3bufS170;
  int32_t _M0L6_2atmpS1156;
  struct _M0TPC16string10StringView _M0L6_2atmpS1154;
  struct _M0TPB6Logger _M0L6_2atmpS1155;
  moonbit_string_t _result_2126;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1156 = Moonbit_array_length(_M0L4selfS171);
  moonbit_incref_cycle_free(_M0L4selfS171);
  _M0L6_2atmpS1154
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS171, .$1 = 0, .$2 = _M0L6_2atmpS1156
  };
  moonbit_incref_cycle_free(_M0L3bufS170);
  _M0L6_2atmpS1155
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS170
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1154, _M0L6_2atmpS1155, _M0L5quoteS172);
  moonbit_decref_cycle_free(_M0L6_2atmpS1154.$0);
  if (_M0L6_2atmpS1155.$1) {
    moonbit_decref(_M0L6_2atmpS1155.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2126 = _M0MPB13StringBuilder10to__string(_M0L3bufS170);
  moonbit_decref_cycle_free(_M0L3bufS170);
  return _result_2126;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS162,
  struct _M0TPB6Logger _M0L6loggerS160,
  int32_t _M0L5quoteS159
) {
  int32_t _M0L3endS1152;
  int32_t _M0L5startS1153;
  int32_t _M0L3lenS161;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS163;
  int32_t _M0L1iS164;
  int32_t _M0L3segS165;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS159) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 34);
  }
  _M0L3endS1152 = _M0L4selfS162.$2;
  _M0L5startS1153 = _M0L4selfS162.$1;
  _M0L3lenS161 = _M0L3endS1152 - _M0L5startS1153;
  moonbit_incref_cycle_free(_M0L4selfS162.$0);
  if (_M0L6loggerS160.$1) {
    moonbit_incref(_M0L6loggerS160.$1);
  }
  _M0L6_2aenvS163
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2aenvS163->$0 = _M0L4selfS162;
  _M0L6_2aenvS163->$1 = _M0L6loggerS160;
  _M0L1iS164 = 0;
  _M0L3segS165 = 0;
  _2afor_166:;
  while (1) {
    moonbit_string_t _M0L3strS1149;
    int32_t _M0L5startS1151;
    int32_t _M0L6_2atmpS1150;
    int32_t _M0L4codeS167;
    int32_t _M0L1cS169;
    int32_t _M0L6_2atmpS1133;
    int32_t _M0L6_2atmpS1134;
    int32_t _M0L6_2atmpS1135;
    if (_M0L1iS164 >= _M0L3lenS161) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
      moonbit_decref_cycle_free(_M0L6_2aenvS163);
      break;
    }
    _M0L3strS1149 = _M0L4selfS162.$0;
    _M0L5startS1151 = _M0L4selfS162.$1;
    _M0L6_2atmpS1150 = _M0L5startS1151 + _M0L1iS164;
    _M0L4codeS167 = _M0L3strS1149[_M0L6_2atmpS1150];
    switch (_M0L4codeS167) {
      case 34: {
        _M0L1cS169 = _M0L4codeS167;
        goto join_168;
        break;
      }
      
      case 92: {
        _M0L1cS169 = _M0L4codeS167;
        goto join_168;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1136;
        int32_t _M0L6_2atmpS1137;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1136 = _M0L1iS164 + 1;
        _M0L6_2atmpS1137 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1136;
        _M0L3segS165 = _M0L6_2atmpS1137;
        goto _2afor_166;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1138;
        int32_t _M0L6_2atmpS1139;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1138 = _M0L1iS164 + 1;
        _M0L6_2atmpS1139 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1138;
        _M0L3segS165 = _M0L6_2atmpS1139;
        goto _2afor_166;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1140;
        int32_t _M0L6_2atmpS1141;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1140 = _M0L1iS164 + 1;
        _M0L6_2atmpS1141 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1140;
        _M0L3segS165 = _M0L6_2atmpS1141;
        goto _2afor_166;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1142;
        int32_t _M0L6_2atmpS1143;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1142 = _M0L1iS164 + 1;
        _M0L6_2atmpS1143 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1142;
        _M0L3segS165 = _M0L6_2atmpS1143;
        goto _2afor_166;
        break;
      }
      default: {
        if (_M0L4codeS167 < 32) {
          int32_t _M0L6_2atmpS1145;
          moonbit_string_t _M0L6_2atmpS1144;
          int32_t _M0L6_2atmpS1146;
          int32_t _M0L6_2atmpS1147;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_24.data);
          _M0L6_2atmpS1145 = _M0L4codeS167 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1144 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1145);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, _M0L6_2atmpS1144);
          moonbit_decref_cycle_free(_M0L6_2atmpS1144);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1146 = _M0L1iS164 + 1;
          _M0L6_2atmpS1147 = _M0L1iS164 + 1;
          _M0L1iS164 = _M0L6_2atmpS1146;
          _M0L3segS165 = _M0L6_2atmpS1147;
          goto _2afor_166;
        } else {
          int32_t _M0L6_2atmpS1148 = _M0L1iS164 + 1;
          int32_t _tmp_2129 = _M0L3segS165;
          _M0L1iS164 = _M0L6_2atmpS1148;
          _M0L3segS165 = _tmp_2129;
          goto _2afor_166;
        }
        break;
      }
    }
    goto joinlet_2128;
    join_168:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1133 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS169);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, _M0L6_2atmpS1133);
    _M0L6_2atmpS1134 = _M0L1iS164 + 1;
    _M0L6_2atmpS1135 = _M0L1iS164 + 1;
    _M0L1iS164 = _M0L6_2atmpS1134;
    _M0L3segS165 = _M0L6_2atmpS1135;
    continue;
    joinlet_2128:;
    break;
  }
  if (_M0L5quoteS159) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS155,
  int32_t _M0L3segS158,
  int32_t _M0L1iS157
) {
  struct _M0TPB6Logger _M0L6loggerS154;
  struct _M0TPC16string10StringView _M0L4selfS156;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS154 = _M0L6_2aenvS155->$1;
  _M0L4selfS156 = _M0L6_2aenvS155->$0;
  if (_M0L1iS157 > _M0L3segS158) {
    int64_t _M0L6_2atmpS1132 = (int64_t)_M0L1iS157;
    struct _M0TPC16string10StringView _M0L6_2atmpS1131;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1131
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS156, _M0L3segS158, _M0L6_2atmpS1132);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS154.$0->$method_2(_M0L6loggerS154.$1, _M0L6_2atmpS1131);
    moonbit_decref_cycle_free(_M0L6_2atmpS1131.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS145,
  int32_t _M0L5startS147,
  int64_t _M0L3endS149
) {
  int32_t _M0L3endS1129;
  int32_t _M0L5startS1130;
  int32_t _M0L3lenS144;
  int32_t _M0Lm2loS146;
  int32_t _M0Lm2hiS148;
  moonbit_string_t _M0L3strS152;
  int32_t _M0L4baseS153;
  int32_t _M0L6_2atmpS1107;
  int32_t _if__result_2130;
  int32_t _M0L6_2atmpS1117;
  int32_t _if__result_2131;
  int32_t _M0L6_2atmpS1119;
  int32_t _M0L6_2atmpS1120;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1129 = _M0L4selfS145.$2;
  _M0L5startS1130 = _M0L4selfS145.$1;
  _M0L3lenS144 = _M0L3endS1129 - _M0L5startS1130;
  if (_M0L5startS147 < 0) {
    _M0Lm2loS146 = 0;
  } else if (_M0L5startS147 > _M0L3lenS144) {
    _M0Lm2loS146 = _M0L3lenS144;
  } else {
    _M0Lm2loS146 = _M0L5startS147;
  }
  if (_M0L3endS149 == 4294967296ll) {
    _M0Lm2hiS148 = _M0L3lenS144;
  } else {
    int64_t _M0L7_2aSomeS150 = _M0L3endS149;
    int32_t _M0L4_2aeS151 = (int32_t)_M0L7_2aSomeS150;
    if (_M0L4_2aeS151 < 0) {
      _M0Lm2hiS148 = 0;
    } else if (_M0L4_2aeS151 > _M0L3lenS144) {
      _M0Lm2hiS148 = _M0L3lenS144;
    } else {
      _M0Lm2hiS148 = _M0L4_2aeS151;
    }
  }
  _M0L3strS152 = _M0L4selfS145.$0;
  _M0L4baseS153 = _M0L4selfS145.$1;
  _M0L6_2atmpS1107 = _M0Lm2loS146;
  if (_M0L6_2atmpS1107 > 0) {
    int32_t _M0L6_2atmpS1106 = _M0Lm2loS146;
    if (_M0L6_2atmpS1106 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS1105 = _M0Lm2loS146;
      int32_t _M0L6_2atmpS1104 = _M0L4baseS153 + _M0L6_2atmpS1105;
      int32_t _M0L6_2atmpS1103 = _M0L3strS152[_M0L6_2atmpS1104];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1103)) {
        int32_t _M0L6_2atmpS1102 = _M0Lm2loS146;
        int32_t _M0L6_2atmpS1101 = _M0L4baseS153 + _M0L6_2atmpS1102;
        int32_t _M0L6_2atmpS1100 = _M0L6_2atmpS1101 - 1;
        int32_t _M0L6_2atmpS1099 = _M0L3strS152[_M0L6_2atmpS1100];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2130
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1099);
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
    int32_t _M0L6_2atmpS1108 = _M0Lm2loS146;
    _M0Lm2loS146 = _M0L6_2atmpS1108 + 1;
  }
  _M0L6_2atmpS1117 = _M0Lm2hiS148;
  if (_M0L6_2atmpS1117 > 0) {
    int32_t _M0L6_2atmpS1116 = _M0Lm2hiS148;
    if (_M0L6_2atmpS1116 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS1115 = _M0Lm2hiS148;
      int32_t _M0L6_2atmpS1114 = _M0L4baseS153 + _M0L6_2atmpS1115;
      int32_t _M0L6_2atmpS1113 = _M0L3strS152[_M0L6_2atmpS1114];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1113)) {
        int32_t _M0L6_2atmpS1112 = _M0Lm2hiS148;
        int32_t _M0L6_2atmpS1111 = _M0L4baseS153 + _M0L6_2atmpS1112;
        int32_t _M0L6_2atmpS1110 = _M0L6_2atmpS1111 - 1;
        int32_t _M0L6_2atmpS1109 = _M0L3strS152[_M0L6_2atmpS1110];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2131
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1109);
      } else {
        _if__result_2131 = 0;
      }
    } else {
      _if__result_2131 = 0;
    }
  } else {
    _if__result_2131 = 0;
  }
  if (_if__result_2131) {
    int32_t _M0L6_2atmpS1118 = _M0Lm2hiS148;
    _M0Lm2hiS148 = _M0L6_2atmpS1118 - 1;
  }
  _M0L6_2atmpS1119 = _M0Lm2loS146;
  _M0L6_2atmpS1120 = _M0Lm2hiS148;
  if (_M0L6_2atmpS1119 >= _M0L6_2atmpS1120) {
    int32_t _M0L6_2atmpS1124 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1121 = _M0L4baseS153 + _M0L6_2atmpS1124;
    int32_t _M0L6_2atmpS1123 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1122 = _M0L4baseS153 + _M0L6_2atmpS1123;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS1121,
                                                 .$2 = _M0L6_2atmpS1122};
  } else {
    int32_t _M0L6_2atmpS1128 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1125 = _M0L4baseS153 + _M0L6_2atmpS1128;
    int32_t _M0L6_2atmpS1127 = _M0Lm2hiS148;
    int32_t _M0L6_2atmpS1126 = _M0L4baseS153 + _M0L6_2atmpS1127;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS1125,
                                                 .$2 = _M0L6_2atmpS1126};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS143) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS142;
  int32_t _M0L6_2atmpS1096;
  int32_t _M0L6_2atmpS1095;
  int32_t _M0L6_2atmpS1098;
  int32_t _M0L6_2atmpS1097;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1094;
  moonbit_string_t _result_2132;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS142 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1096 = _M0IPC14byte4BytePB3Div3div(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1095
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1096);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS1095);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1098 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1097
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1098);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS1097);
  _M0L6_2atmpS1094 = _M0L7_2aselfS142;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2132 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1094);
  moonbit_decref_cycle_free(_M0L6_2atmpS1094);
  return _result_2132;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS141) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS141 < 10) {
    int32_t _M0L6_2atmpS1091;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1091 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1091);
  } else {
    int32_t _M0L6_2atmpS1093;
    int32_t _M0L6_2atmpS1092;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1093 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1092 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1093, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1092);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS139,
  int32_t _M0L4thatS140
) {
  int32_t _M0L6_2atmpS1089;
  int32_t _M0L6_2atmpS1090;
  int32_t _M0L6_2atmpS1088;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1089 = (int32_t)_M0L4selfS139;
  _M0L6_2atmpS1090 = (int32_t)_M0L4thatS140;
  _M0L6_2atmpS1088 = _M0L6_2atmpS1089 - _M0L6_2atmpS1090;
  return _M0L6_2atmpS1088 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS137,
  int32_t _M0L4thatS138
) {
  int32_t _M0L6_2atmpS1086;
  int32_t _M0L6_2atmpS1087;
  int32_t _M0L6_2atmpS1085;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1086 = (int32_t)_M0L4selfS137;
  _M0L6_2atmpS1087 = (int32_t)_M0L4thatS138;
  _M0L6_2atmpS1085 = _M0L6_2atmpS1086 % _M0L6_2atmpS1087;
  return _M0L6_2atmpS1085 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS135,
  int32_t _M0L4thatS136
) {
  int32_t _M0L6_2atmpS1083;
  int32_t _M0L6_2atmpS1084;
  int32_t _M0L6_2atmpS1082;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1083 = (int32_t)_M0L4selfS135;
  _M0L6_2atmpS1084 = (int32_t)_M0L4thatS136;
  _M0L6_2atmpS1082 = _M0L6_2atmpS1083 / _M0L6_2atmpS1084;
  return _M0L6_2atmpS1082 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS133,
  int32_t _M0L4thatS134
) {
  int32_t _M0L6_2atmpS1080;
  int32_t _M0L6_2atmpS1081;
  int32_t _M0L6_2atmpS1079;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1080 = (int32_t)_M0L4selfS133;
  _M0L6_2atmpS1081 = (int32_t)_M0L4thatS134;
  _M0L6_2atmpS1079 = _M0L6_2atmpS1080 + _M0L6_2atmpS1081;
  return _M0L6_2atmpS1079 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS132) {
  int32_t _M0L6_2atmpS1078;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1078 = (int32_t)_M0L4selfS132;
  return _M0L6_2atmpS1078;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS131) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS131 >= 56320 && _M0L4selfS131 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS130) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS130 >= 55296 && _M0L4selfS130 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS129,
  moonbit_string_t _M0L3strS127
) {
  int32_t _M0L8str__lenS126;
  int32_t _M0L3lenS1077;
  int32_t _M0L8requiredS128;
  uint16_t* _M0L4dataS1072;
  int32_t _M0L6_2atmpS1071;
  int32_t _if__result_2133;
  uint16_t* _M0L4dataS1073;
  int32_t _M0L3lenS1074;
  int32_t _M0L3lenS1076;
  int32_t _M0L6_2atmpS1075;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS126 = Moonbit_array_length(_M0L3strS127);
  if (_M0L8str__lenS126 == 0) {
    return 0;
  }
  _M0L3lenS1077 = _M0L4selfS129->$1;
  _M0L8requiredS128 = _M0L3lenS1077 + _M0L8str__lenS126;
  _M0L4dataS1072 = _M0L4selfS129->$0;
  _M0L6_2atmpS1071 = Moonbit_array_length(_M0L4dataS1072);
  if (_M0L8requiredS128 > _M0L6_2atmpS1071) {
    _if__result_2133 = 1;
  } else {
    int32_t _M0L3lenS1070 = _M0L4selfS129->$1;
    _if__result_2133 = _M0L8requiredS128 < _M0L3lenS1070;
  }
  if (_if__result_2133) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS129, _M0L8requiredS128);
  }
  _M0L4dataS1073 = _M0L4selfS129->$0;
  _M0L3lenS1074 = _M0L4selfS129->$1;
  moonbit_incref_cycle_free(_M0L4dataS1073);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1073, _M0L3lenS1074, _M0L3strS127, 0, _M0L8str__lenS126);
  moonbit_decref_cycle_free(_M0L4dataS1073);
  _M0L3lenS1076 = _M0L4selfS129->$1;
  _M0L6_2atmpS1075 = _M0L3lenS1076 + _M0L8str__lenS126;
  _M0L4selfS129->$1 = _M0L6_2atmpS1075;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS122,
  int32_t _M0L11dst__offsetS125,
  moonbit_string_t _M0L3strS123,
  int32_t _M0L11str__offsetS118,
  int32_t _M0L3lenS119
) {
  int32_t _M0L16end__str__offsetS117;
  int32_t _M0L1iS120;
  int32_t _M0L1jS121;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS117 = _M0L11str__offsetS118 + _M0L3lenS119;
  _M0L1iS120 = _M0L11str__offsetS118;
  _M0L1jS121 = _M0L11dst__offsetS125;
  while (1) {
    if (_M0L1iS120 < _M0L16end__str__offsetS117) {
      int32_t _M0L6_2atmpS1067 = _M0L3strS123[_M0L1iS120];
      int32_t _M0L6_2atmpS1068;
      int32_t _M0L6_2atmpS1069;
      _M0L4selfS122[_M0L1jS121] = _M0L6_2atmpS1067;
      _M0L6_2atmpS1068 = _M0L1iS120 + 1;
      _M0L6_2atmpS1069 = _M0L1jS121 + 1;
      _M0L1iS120 = _M0L6_2atmpS1068;
      _M0L1jS121 = _M0L6_2atmpS1069;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS115,
  int32_t _M0L2chS114
) {
  uint32_t _M0L4codeS113;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS113 = _M0MPC14char4Char8to__uint(_M0L2chS114);
  if (_M0L4codeS113 <= 65535u) {
    int32_t _M0L3lenS1038 = _M0L4selfS115->$1;
    uint16_t* _M0L4dataS1040 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS1039 = Moonbit_array_length(_M0L4dataS1040);
    uint16_t* _M0L4dataS1043;
    int32_t _M0L3lenS1044;
    int32_t _M0L6_2atmpS1045;
    int32_t _M0L3lenS1047;
    int32_t _M0L6_2atmpS1046;
    if (_M0L3lenS1038 >= _M0L6_2atmpS1039) {
      int32_t _M0L3lenS1042 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS1041 = _M0L3lenS1042 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS1041);
    }
    _M0L4dataS1043 = _M0L4selfS115->$0;
    _M0L3lenS1044 = _M0L4selfS115->$1;
    moonbit_incref_cycle_free(_M0L4dataS1043);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1045 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS113);
    if (
      _M0L3lenS1044 < 0
      || _M0L3lenS1044 >= Moonbit_array_length(_M0L4dataS1043)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1043[_M0L3lenS1044] = _M0L6_2atmpS1045;
    moonbit_decref_cycle_free(_M0L4dataS1043);
    _M0L3lenS1047 = _M0L4selfS115->$1;
    _M0L6_2atmpS1046 = _M0L3lenS1047 + 1;
    _M0L4selfS115->$1 = _M0L6_2atmpS1046;
  } else if (_M0L4codeS113 <= 1114111u) {
    uint16_t* _M0L4dataS1051 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS1049 = Moonbit_array_length(_M0L4dataS1051);
    int32_t _M0L3lenS1050 = _M0L4selfS115->$1;
    int32_t _M0L6_2atmpS1048 = _M0L6_2atmpS1049 - _M0L3lenS1050;
    uint32_t _M0L4codeS116;
    uint16_t* _M0L4dataS1054;
    int32_t _M0L3lenS1055;
    uint32_t _M0L6_2atmpS1058;
    uint32_t _M0L6_2atmpS1057;
    int32_t _M0L6_2atmpS1056;
    uint16_t* _M0L4dataS1059;
    int32_t _M0L3lenS1064;
    int32_t _M0L6_2atmpS1060;
    uint32_t _M0L6_2atmpS1063;
    uint32_t _M0L6_2atmpS1062;
    int32_t _M0L6_2atmpS1061;
    int32_t _M0L3lenS1066;
    int32_t _M0L6_2atmpS1065;
    if (_M0L6_2atmpS1048 < 2) {
      int32_t _M0L3lenS1053 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS1052 = _M0L3lenS1053 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS1052);
    }
    _M0L4codeS116 = _M0L4codeS113 - 65536u;
    _M0L4dataS1054 = _M0L4selfS115->$0;
    _M0L3lenS1055 = _M0L4selfS115->$1;
    _M0L6_2atmpS1058 = _M0L4codeS116 >> 10;
    _M0L6_2atmpS1057 = 55296u + _M0L6_2atmpS1058;
    moonbit_incref_cycle_free(_M0L4dataS1054);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1056 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1057);
    if (
      _M0L3lenS1055 < 0
      || _M0L3lenS1055 >= Moonbit_array_length(_M0L4dataS1054)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1054[_M0L3lenS1055] = _M0L6_2atmpS1056;
    moonbit_decref_cycle_free(_M0L4dataS1054);
    _M0L4dataS1059 = _M0L4selfS115->$0;
    _M0L3lenS1064 = _M0L4selfS115->$1;
    _M0L6_2atmpS1060 = _M0L3lenS1064 + 1;
    _M0L6_2atmpS1063 = _M0L4codeS116 & 1023u;
    _M0L6_2atmpS1062 = 56320u + _M0L6_2atmpS1063;
    moonbit_incref_cycle_free(_M0L4dataS1059);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1061 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1062);
    if (
      _M0L6_2atmpS1060 < 0
      || _M0L6_2atmpS1060 >= Moonbit_array_length(_M0L4dataS1059)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1059[_M0L6_2atmpS1060] = _M0L6_2atmpS1061;
    moonbit_decref_cycle_free(_M0L4dataS1059);
    _M0L3lenS1066 = _M0L4selfS115->$1;
    _M0L6_2atmpS1065 = _M0L3lenS1066 + 2;
    _M0L4selfS115->$1 = _M0L6_2atmpS1065;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_25.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L8requiredS111
) {
  uint16_t* _M0L4dataS1037;
  int32_t _M0L6_2atmpS1035;
  int32_t _M0L3lenS1036;
  int32_t _M0L13new__capacityS109;
  uint16_t* _M0L4dataS1032;
  int32_t _M0L6_2atmpS1033;
  int32_t _M0L3lenS1034;
  uint16_t* _M0L9new__dataS112;
  uint16_t* _M0L6_2aoldS2021;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1037 = _M0L4selfS110->$0;
  _M0L6_2atmpS1035 = Moonbit_array_length(_M0L4dataS1037);
  _M0L3lenS1036 = _M0L4selfS110->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS109
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1035, _M0L3lenS1036, _M0L8requiredS111);
  _M0L4dataS1032 = _M0L4selfS110->$0;
  moonbit_incref_cycle_free(_M0L4dataS1032);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1033 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1034 = _M0L4selfS110->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS112
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1032, _M0L13new__capacityS109, _M0L6_2atmpS1033, _M0L3lenS1034, 0, 0);
  _M0L6_2aoldS2021 = _M0L4selfS110->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2021);
  _M0L4selfS110->$0 = _M0L9new__dataS112;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS108,
  int32_t _M0L3lenS104,
  int32_t _M0L8requiredS103
) {
  int32_t _M0L5spaceS105;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS103 < _M0L3lenS104) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  _M0L5spaceS105 = _M0L7currentS108;
  while (1) {
    if (_M0L5spaceS105 < _M0L8requiredS103) {
      int32_t _M0L4nextS106 = _M0L5spaceS105 * 2;
      if (_M0L4nextS106 <= _M0L5spaceS105) {
        return _M0L8requiredS103;
      }
      _M0L5spaceS105 = _M0L4nextS106;
      continue;
    } else {
      return _M0L5spaceS105;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS102) {
  int32_t _M0L6_2atmpS1031;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1031 = *(int32_t*)&_M0L4selfS102;
  return (uint16_t)_M0L6_2atmpS1031;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS101) {
  int32_t _M0L6_2atmpS1030;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1030 = _M0L4selfS101;
  return *(uint32_t*)&_M0L6_2atmpS1030;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS99
) {
  int32_t _M0L3lenS1021;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1021 = _M0L4selfS99->$1;
  if (_M0L3lenS1021 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1022 = _M0L4selfS99->$1;
    uint16_t* _M0L4dataS1024 = _M0L4selfS99->$0;
    int32_t _M0L6_2atmpS1023 = Moonbit_array_length(_M0L4dataS1024);
    if (_M0L3lenS1022 == _M0L6_2atmpS1023) {
      uint16_t* _M0L4dataS1025 = _M0L4selfS99->$0;
      moonbit_incref_cycle_free(_M0L4dataS1025);
      return _M0L4dataS1025;
    } else {
      uint16_t* _M0L4dataS1026 = _M0L4selfS99->$0;
      int32_t _M0L3lenS1027 = _M0L4selfS99->$1;
      int32_t _M0L6_2atmpS1028;
      int32_t _M0L3lenS1029;
      uint16_t* _M0L4dataS100;
      moonbit_incref_cycle_free(_M0L4dataS1026);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1028 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1029 = _M0L4selfS99->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS100
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1026, _M0L3lenS1027, _M0L6_2atmpS1028, _M0L3lenS1029, 0, 0);
      return _M0L4dataS100;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS96,
  int32_t _M0L13allocate__lenS92,
  int32_t _M0L4initS97,
  int32_t _M0L3lenS93,
  int32_t _M0L11src__offsetS94,
  int32_t _M0L11dst__offsetS95
) {
  int32_t _if__result_2136;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS92 >= 0) {
    if (_M0L3lenS93 >= 0) {
      if (_M0L11src__offsetS94 >= 0) {
        if (_M0L11dst__offsetS95 >= 0) {
          int32_t _M0L6_2atmpS1017 = _M0L11src__offsetS94 + _M0L3lenS93;
          int32_t _M0L6_2atmpS1018 = Moonbit_array_length(_M0L3srcS96);
          if (_M0L6_2atmpS1017 <= _M0L6_2atmpS1018) {
            int32_t _M0L6_2atmpS1016 = _M0L11dst__offsetS95 + _M0L3lenS93;
            _if__result_2136 = _M0L6_2atmpS1016 <= _M0L13allocate__lenS92;
          } else {
            _if__result_2136 = 0;
          }
        } else {
          _if__result_2136 = 0;
        }
      } else {
        _if__result_2136 = 0;
      }
    } else {
      _if__result_2136 = 0;
    }
  } else {
    _if__result_2136 = 0;
  }
  if (_if__result_2136) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS96, _M0L13allocate__lenS92, _M0L4initS97, _M0L11src__offsetS94, _M0L11dst__offsetS95, _M0L3lenS93);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS98;
    int32_t _M0L6_2atmpS1020;
    moonbit_string_t _M0L6_2atmpS1019;
    uint16_t* _result_2137;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS98
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L13allocate__lenS92);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L11src__offsetS94);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L11dst__offsetS95);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L3lenS93);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_31.data);
    _M0L6_2atmpS1020 = Moonbit_array_length(_M0L3srcS96);
    moonbit_decref_cycle_free(_M0L3srcS96);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L6_2atmpS1020);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1019
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS98);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS98);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2137 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1019);
    moonbit_decref_cycle_free(_M0L6_2atmpS1019);
    return _result_2137;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS89,
  int32_t _M0L13allocate__lenS86,
  int32_t _M0L4initS87,
  int32_t _M0L11src__offsetS90,
  int32_t _M0L11dst__offsetS88,
  int32_t _M0L9blit__lenS91
) {
  uint16_t* _M0L3dstS85;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS85
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS86, _M0L4initS87);
  moonbit_incref_cycle_free(_M0L3dstS85);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS85, _M0L11dst__offsetS88, _M0L3srcS89, _M0L11src__offsetS90, _M0L9blit__lenS91, sizeof(uint16_t));
  return _M0L3dstS85;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS83
) {
  int32_t _M0L7initialS82;
  uint16_t* _M0L4dataS84;
  struct _M0TPB13StringBuilder* _block_2138;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS83 < 1) {
    _M0L7initialS82 = 1;
  } else {
    int32_t _M0L6_2atmpS1015 = _M0L10size__hintS83 + 1;
    _M0L7initialS82 = _M0L6_2atmpS1015 / 2;
  }
  _M0L4dataS84 = (uint16_t*)moonbit_make_string(_M0L7initialS82, 0);
  _block_2138
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2138)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 47, 0);
  _block_2138->$0 = _M0L4dataS84;
  _block_2138->$1 = 0;
  return _block_2138;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS81) {
  int32_t _M0L6_2atmpS1014;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1014 = (int32_t)_M0L4selfS81;
  return _M0L6_2atmpS1014;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS73,
  int32_t _M0L13allocate__lenS69,
  int32_t _M0L3lenS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS72
) {
  int32_t _if__result_2139;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS69 >= 0) {
    if (_M0L3lenS70 >= 0) {
      if (_M0L11src__offsetS71 >= 0) {
        if (_M0L11dst__offsetS72 >= 0) {
          int32_t _M0L6_2atmpS1005 = _M0L11src__offsetS71 + _M0L3lenS70;
          int32_t _M0L6_2atmpS1006;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1006
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
          if (_M0L6_2atmpS1005 <= _M0L6_2atmpS1006) {
            int32_t _M0L6_2atmpS1004 = _M0L11dst__offsetS72 + _M0L3lenS70;
            _if__result_2139 = _M0L6_2atmpS1004 <= _M0L13allocate__lenS69;
          } else {
            _if__result_2139 = 0;
          }
        } else {
          _if__result_2139 = 0;
        }
      } else {
        _if__result_2139 = 0;
      }
    } else {
      _if__result_2139 = 0;
    }
  } else {
    _if__result_2139 = 0;
  }
  if (_if__result_2139) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS69, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS73, _M0L11src__offsetS71, _M0L11dst__offsetS72, _M0L3lenS70);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS74;
    int32_t _M0L6_2atmpS1008;
    moonbit_string_t _M0L6_2atmpS1007;
    moonbit_string_t* _result_2140;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS74
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L13allocate__lenS69);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L11src__offsetS71);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L11dst__offsetS72);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L3lenS70);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1008 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
    moonbit_decref_cycle_free(_M0L3srcS73);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L6_2atmpS1008);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1007
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS74);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS74);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2140
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1007);
    moonbit_decref_cycle_free(_M0L6_2atmpS1007);
    return _result_2140;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS79,
  int32_t _M0L13allocate__lenS75,
  int32_t _M0L3lenS76,
  int32_t _M0L11src__offsetS77,
  int32_t _M0L11dst__offsetS78
) {
  int32_t _if__result_2141;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS75 >= 0) {
    if (_M0L3lenS76 >= 0) {
      if (_M0L11src__offsetS77 >= 0) {
        if (_M0L11dst__offsetS78 >= 0) {
          int32_t _M0L6_2atmpS1010 = _M0L11src__offsetS77 + _M0L3lenS76;
          int32_t _M0L6_2atmpS1011;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1011
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
          if (_M0L6_2atmpS1010 <= _M0L6_2atmpS1011) {
            int32_t _M0L6_2atmpS1009 = _M0L11dst__offsetS78 + _M0L3lenS76;
            _if__result_2141 = _M0L6_2atmpS1009 <= _M0L13allocate__lenS75;
          } else {
            _if__result_2141 = 0;
          }
        } else {
          _if__result_2141 = 0;
        }
      } else {
        _if__result_2141 = 0;
      }
    } else {
      _if__result_2141 = 0;
    }
  } else {
    _if__result_2141 = 0;
  }
  if (_if__result_2141) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS75, 0, _M0L3srcS79, _M0L11src__offsetS77, _M0L11dst__offsetS78, _M0L3lenS76);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS80;
    int32_t _M0L6_2atmpS1013;
    moonbit_string_t _M0L6_2atmpS1012;
    struct _M0TUsiE** _result_2142;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS80
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L13allocate__lenS75);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L11src__offsetS77);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L11dst__offsetS78);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L3lenS76);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1013 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
    moonbit_decref_cycle_free(_M0L3srcS79);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L6_2atmpS1013);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1012
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS80);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS80);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2142
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1012);
    moonbit_decref_cycle_free(_M0L6_2atmpS1012);
    return _result_2142;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS64,
  moonbit_string_t _M0L3objS63
) {
  struct _M0TPB6Logger _M0L6_2atmpS1001;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS64);
  _M0L6_2atmpS1001
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS64
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS63, _M0L6_2atmpS1001);
  if (_M0L6_2atmpS1001.$1) {
    moonbit_decref(_M0L6_2atmpS1001.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS66,
  int32_t _M0L3objS65
) {
  struct _M0TPB6Logger _M0L6_2atmpS1002;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS66);
  _M0L6_2atmpS1002
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS66
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS65, _M0L6_2atmpS1002);
  if (_M0L6_2atmpS1002.$1) {
    moonbit_decref(_M0L6_2atmpS1002.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  uint64_t _M0L3objS67
) {
  struct _M0TPB6Logger _M0L6_2atmpS1003;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS68);
  _M0L6_2atmpS1003
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS68
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS67, _M0L6_2atmpS1003);
  if (_M0L6_2atmpS1003.$1) {
    moonbit_decref(_M0L6_2atmpS1003.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS54,
  int32_t _M0L13allocate__lenS52,
  int32_t _M0L11src__offsetS55,
  int32_t _M0L11dst__offsetS53,
  int32_t _M0L9blit__lenS56
) {
  moonbit_string_t* _M0L3dstS51;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS51
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS52, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS51, _M0L11dst__offsetS53, _M0L3srcS54, _M0L11src__offsetS55, _M0L9blit__lenS56);
  moonbit_decref_cycle_free(_M0L3srcS54);
  return _M0L3dstS51;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS60,
  int32_t _M0L13allocate__lenS58,
  int32_t _M0L11src__offsetS61,
  int32_t _M0L11dst__offsetS59,
  int32_t _M0L9blit__lenS62
) {
  struct _M0TUsiE** _M0L3dstS57;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS57
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS58, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS57, _M0L11dst__offsetS59, _M0L3srcS60, _M0L11src__offsetS61, _M0L9blit__lenS62);
  moonbit_decref_cycle_free(_M0L3srcS60);
  return _M0L3dstS57;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS41,
  int32_t _M0L11dst__offsetS42,
  moonbit_string_t* _M0L3srcS43,
  int32_t _M0L11src__offsetS44,
  int32_t _M0L3lenS45
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS43);
  moonbit_incref_cycle_free(_M0L3dstS41);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS41, _M0L11dst__offsetS42, _M0L3srcS43, _M0L11src__offsetS44, _M0L3lenS45);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS46,
  int32_t _M0L11dst__offsetS47,
  struct _M0TUsiE** _M0L3srcS48,
  int32_t _M0L11src__offsetS49,
  int32_t _M0L3lenS50
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS48);
  moonbit_incref_cycle_free(_M0L3dstS46);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS46, _M0L11dst__offsetS47, _M0L3srcS48, _M0L11src__offsetS49, _M0L3lenS50);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS14,
  int32_t _M0L11dst__offsetS16,
  uint16_t* _M0L3srcS15,
  int32_t _M0L11src__offsetS17,
  int32_t _M0L3lenS19
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS14 == _M0L3srcS15 && _M0L11dst__offsetS16 < _M0L11src__offsetS17
  ) {
    int32_t _M0L1iS18 = 0;
    while (1) {
      if (_M0L1iS18 < _M0L3lenS19) {
        int32_t _M0L6_2atmpS974 = _M0L11dst__offsetS16 + _M0L1iS18;
        int32_t _M0L6_2atmpS976 = _M0L11src__offsetS17 + _M0L1iS18;
        int32_t _M0L6_2atmpS975;
        int32_t _M0L6_2atmpS977;
        if (
          _M0L6_2atmpS976 < 0
          || _M0L6_2atmpS976 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS975 = (int32_t)_M0L3srcS15[_M0L6_2atmpS976];
        if (
          _M0L6_2atmpS974 < 0
          || _M0L6_2atmpS974 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS974] = _M0L6_2atmpS975;
        _M0L6_2atmpS977 = _M0L1iS18 + 1;
        _M0L1iS18 = _M0L6_2atmpS977;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS982 = _M0L3lenS19 - 1;
    int32_t _M0L1iS21 = _M0L6_2atmpS982;
    while (1) {
      if (_M0L1iS21 >= 0) {
        int32_t _M0L6_2atmpS978 = _M0L11dst__offsetS16 + _M0L1iS21;
        int32_t _M0L6_2atmpS980 = _M0L11src__offsetS17 + _M0L1iS21;
        int32_t _M0L6_2atmpS979;
        int32_t _M0L6_2atmpS981;
        if (
          _M0L6_2atmpS980 < 0
          || _M0L6_2atmpS980 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS979 = (int32_t)_M0L3srcS15[_M0L6_2atmpS980];
        if (
          _M0L6_2atmpS978 < 0
          || _M0L6_2atmpS978 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS978] = _M0L6_2atmpS979;
        _M0L6_2atmpS981 = _M0L1iS21 - 1;
        _M0L1iS21 = _M0L6_2atmpS981;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS23,
  int32_t _M0L11dst__offsetS25,
  moonbit_string_t* _M0L3srcS24,
  int32_t _M0L11src__offsetS26,
  int32_t _M0L3lenS28
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS23 == _M0L3srcS24 && _M0L11dst__offsetS25 < _M0L11src__offsetS26
  ) {
    int32_t _M0L1iS27 = 0;
    while (1) {
      if (_M0L1iS27 < _M0L3lenS28) {
        int32_t _M0L6_2atmpS983 = _M0L11dst__offsetS25 + _M0L1iS27;
        int32_t _M0L6_2atmpS985 = _M0L11src__offsetS26 + _M0L1iS27;
        moonbit_string_t _M0L6_2atmpS984;
        moonbit_string_t _M0L6_2aoldS2022;
        int32_t _M0L6_2atmpS986;
        if (
          _M0L6_2atmpS985 < 0
          || _M0L6_2atmpS985 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS984 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS985];
        if (
          _M0L6_2atmpS983 < 0
          || _M0L6_2atmpS983 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2022 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS983];
        moonbit_incref_cycle_free(_M0L6_2atmpS984);
        moonbit_decref_cycle_free(_M0L6_2aoldS2022);
        _M0L3dstS23[_M0L6_2atmpS983] = _M0L6_2atmpS984;
        _M0L6_2atmpS986 = _M0L1iS27 + 1;
        _M0L1iS27 = _M0L6_2atmpS986;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS991 = _M0L3lenS28 - 1;
    int32_t _M0L1iS30 = _M0L6_2atmpS991;
    while (1) {
      if (_M0L1iS30 >= 0) {
        int32_t _M0L6_2atmpS987 = _M0L11dst__offsetS25 + _M0L1iS30;
        int32_t _M0L6_2atmpS989 = _M0L11src__offsetS26 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS988;
        moonbit_string_t _M0L6_2aoldS2023;
        int32_t _M0L6_2atmpS990;
        if (
          _M0L6_2atmpS989 < 0
          || _M0L6_2atmpS989 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS988 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS989];
        if (
          _M0L6_2atmpS987 < 0
          || _M0L6_2atmpS987 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2023 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS987];
        moonbit_incref_cycle_free(_M0L6_2atmpS988);
        moonbit_decref_cycle_free(_M0L6_2aoldS2023);
        _M0L3dstS23[_M0L6_2atmpS987] = _M0L6_2atmpS988;
        _M0L6_2atmpS990 = _M0L1iS30 - 1;
        _M0L1iS30 = _M0L6_2atmpS990;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS32,
  int32_t _M0L11dst__offsetS34,
  struct _M0TUsiE** _M0L3srcS33,
  int32_t _M0L11src__offsetS35,
  int32_t _M0L3lenS37
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS32 == _M0L3srcS33 && _M0L11dst__offsetS34 < _M0L11src__offsetS35
  ) {
    int32_t _M0L1iS36 = 0;
    while (1) {
      if (_M0L1iS36 < _M0L3lenS37) {
        int32_t _M0L6_2atmpS992 = _M0L11dst__offsetS34 + _M0L1iS36;
        int32_t _M0L6_2atmpS994 = _M0L11src__offsetS35 + _M0L1iS36;
        struct _M0TUsiE* _M0L6_2atmpS993;
        struct _M0TUsiE* _M0L6_2aoldS2024;
        int32_t _M0L6_2atmpS995;
        if (
          _M0L6_2atmpS994 < 0
          || _M0L6_2atmpS994 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS993 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS994];
        if (
          _M0L6_2atmpS992 < 0
          || _M0L6_2atmpS992 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2024 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS992];
        if (_M0L6_2atmpS993) {
          moonbit_incref_cycle_free(_M0L6_2atmpS993);
        }
        if (_M0L6_2aoldS2024) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2024);
        }
        _M0L3dstS32[_M0L6_2atmpS992] = _M0L6_2atmpS993;
        _M0L6_2atmpS995 = _M0L1iS36 + 1;
        _M0L1iS36 = _M0L6_2atmpS995;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1000 = _M0L3lenS37 - 1;
    int32_t _M0L1iS39 = _M0L6_2atmpS1000;
    while (1) {
      if (_M0L1iS39 >= 0) {
        int32_t _M0L6_2atmpS996 = _M0L11dst__offsetS34 + _M0L1iS39;
        int32_t _M0L6_2atmpS998 = _M0L11src__offsetS35 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS997;
        struct _M0TUsiE* _M0L6_2aoldS2025;
        int32_t _M0L6_2atmpS999;
        if (
          _M0L6_2atmpS998 < 0
          || _M0L6_2atmpS998 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS997 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS998];
        if (
          _M0L6_2atmpS996 < 0
          || _M0L6_2atmpS996 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2025 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS996];
        if (_M0L6_2atmpS997) {
          moonbit_incref_cycle_free(_M0L6_2atmpS997);
        }
        if (_M0L6_2aoldS2025) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2025);
        }
        _M0L3dstS32[_M0L6_2atmpS996] = _M0L6_2atmpS997;
        _M0L6_2atmpS999 = _M0L1iS39 - 1;
        _M0L1iS39 = _M0L6_2atmpS999;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS12) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS12);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS13
) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS13);
}

int32_t _M0IPB7FailurePB4Show6output(
  void* _M0L10_2ax__6387S8,
  struct _M0TPB6Logger _M0L10_2ax__6388S11
) {
  struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS9;
  moonbit_string_t _M0L15_2a_2aarg__6389S10;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2aFailureS9
  = (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L10_2ax__6387S8;
  _M0L15_2a_2aarg__6389S10 = _M0L10_2aFailureS9->$0;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S11.$0->$method_0(_M0L10_2ax__6388S11.$1, (moonbit_string_t)moonbit_string_literal_32.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S11, _M0L15_2a_2aarg__6389S10);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S11.$0->$method_0(_M0L10_2ax__6388S11.$1, (moonbit_string_t)moonbit_string_literal_33.data);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger _M0L4selfS7,
  moonbit_string_t _M0L3objS6
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS6, _M0L4selfS7);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS943) {
  switch (Moonbit_object_tag(_M0L4_2aeS943)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_34.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS943);
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
  void* _M0L11_2aobj__ptrS969,
  struct _M0TPB4Show _M0L8_2aparamS968
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS967 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS969;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS967, _M0L8_2aparamS968);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS966,
  struct _M0TPB4Show _M0L8_2aparamS965
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS964 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS966;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS964, _M0L8_2aparamS965);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS963,
  int32_t _M0L8_2aparamS962
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS961 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS963;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS961, _M0L8_2aparamS962);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS960,
  struct _M0TPC16string10StringView _M0L8_2aparamS959
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS958 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS960;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS958, _M0L8_2aparamS959);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS957,
  moonbit_string_t _M0L8_2aparamS954,
  int32_t _M0L8_2aparamS955,
  int32_t _M0L8_2aparamS956
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS953 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS957;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS953, _M0L8_2aparamS954, _M0L8_2aparamS955, _M0L8_2aparamS956);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS952,
  moonbit_string_t _M0L8_2aparamS951
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS950 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS952;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS950, _M0L8_2aparamS951);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2149 = 9218868437227405311ll;
  int64_t _tmp_2150;
  int64_t _tmp_2151;
  int64_t _tmp_2152;
  int64_t _tmp_2153;
  _M0FPB18double__max__value = *(double*)&_tmp_2149;
  _tmp_2150 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2150;
  _tmp_2151 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2151;
  _tmp_2152 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2152;
  _tmp_2153 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2153;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS973;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS936;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS937;
  int32_t _M0L7_2abindS938;
  struct _M0TUsiE** _M0L7_2abindS939;
  int32_t _M0L6_2acntS2030;
  int32_t _M0L2__S940;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS973
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS936
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS936)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 50, 0);
  _M0L12async__testsS936->$0 = _M0L6_2atmpS973;
  _M0L12async__testsS936->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS937
  = _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS938 = _M0L7_2abindS937->$1;
  _M0L7_2abindS939 = _M0L7_2abindS937->$0;
  _M0L6_2acntS2030
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS937));
  if (_M0L6_2acntS2030 > 1) {
    int32_t _M0L11_2anew__cntS2031 = _M0L6_2acntS2030 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS937), _M0L11_2anew__cntS2031);
    moonbit_incref_cycle_free(_M0L7_2abindS939);
  } else if (_M0L6_2acntS2030 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS937);
  }
  _M0L2__S940 = 0;
  while (1) {
    if (_M0L2__S940 < _M0L7_2abindS938) {
      struct _M0TUsiE* _M0L3argS941 =
        (struct _M0TUsiE*)_M0L7_2abindS939[_M0L2__S940];
      moonbit_string_t _M0L6_2atmpS970 = _M0L3argS941->$0;
      int32_t _M0L6_2atmpS971 = _M0L3argS941->$1;
      int32_t _M0L6_2atmpS972;
      moonbit_incref_cycle_free(_M0L6_2atmpS970);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS936, _M0L6_2atmpS970, _M0L6_2atmpS971);
      moonbit_decref_cycle_free(_M0L6_2atmpS970);
      _M0L6_2atmpS972 = _M0L2__S940 + 1;
      _M0L2__S940 = _M0L6_2atmpS972;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS939);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\hh_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26hh__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS936);
  moonbit_decref_cycle_free(_M0L12async__testsS936);
  moonbit_flush_cycles();
  return 0;
}