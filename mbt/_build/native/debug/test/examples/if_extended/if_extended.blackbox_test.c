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

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28if__extended__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TP26RiantR8snn__mbt10ExtendedIF;

struct _M0TPB8MutLocalGiE;

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866;

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28if__extended__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TWEu;

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

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28if__extended__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TP26RiantR8snn__mbt10ExtendedIF {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGbE* $7;
  struct _M0TPB5ArrayGfE* $8;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861 {
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

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter {
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

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28if__extended__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok {
  int32_t $0;
  
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

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS873(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS866(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS861(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS838(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S831(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples28if__extended__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt23integrate__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF*,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*,
  float
);

int32_t _M0FP26RiantR8snn__mbt28update__neuron__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF*,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*,
  float
);

int32_t _M0FP26RiantR8snn__mbt30update__synapses__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF*,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*,
  float
);

struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0MP26RiantR8snn__mbt10ExtendedIF3new(
  int64_t,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*
);

struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0MP26RiantR8snn__mbt10ExtendedIF11new_2einner(
  int32_t,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*
);

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0MP26RiantR8snn__mbt19ExtendedIFParameter6custom(
  float,
  float,
  float,
  float,
  float,
  float,
  float,
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0MP26RiantR8snn__mbt19ExtendedIFParameter3new(
  
);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGbE(
  int32_t,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t,
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

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGfE(
  struct _M0TPB13StringBuilder*,
  float
);

int32_t _M0MPB13StringBuilder13write__objectGbE(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder*,
  moonbit_string_t
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

moonbit_string_t* moonbit_rt_get_cli_args();

struct { int32_t rc; uint32_t meta; uint16_t const data[118]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 117, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 102, 95, 101, 120, 116, 101, 
    110, 100, 101, 100, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 
    116, 0
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
} const moonbit_string_literal_37 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[116]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 115, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 102, 95, 101, 120, 116, 101, 
    110, 100, 101, 100, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
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
} const moonbit_string_literal_35 =
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

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS873$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS873
  };

uint32_t const moonbit_layout_table_data[52] =
  {
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861, $1)
    / 4
    * 2,
    sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866)
    / 4, 1,
    offsetof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt10ExtendedIF) / 4, 8,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $8) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1847
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS894,
  moonbit_string_t _M0L8filenameS863,
  int32_t _M0L5indexS865
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861* _closure_1869;
  struct _M0TWEu* _M0L13handle__startS861;
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866* _closure_1870;
  struct _M0TWssbEu* _M0L14handle__resultS866;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS873;
  void* _M0L11_2atry__errS888;
  struct moonbit_result_0 _tmp_1872;
  int32_t _handle__error__result_1873;
  int32_t _M0L6_2atmpS1835;
  void* _M0L3errS889;
  moonbit_string_t _M0L4nameS891;
  struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS892;
  moonbit_string_t _M0L7_2anameS893;
  int32_t _M0L6_2acntS1863;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS863);
  _closure_1869
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861));
  Moonbit_object_header(_closure_1869)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1869->code
  = &_M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS861;
  _closure_1869->$0 = _M0L5indexS865;
  _closure_1869->$1 = _M0L8filenameS863;
  _M0L13handle__startS861 = (struct _M0TWEu*)_closure_1869;
  moonbit_incref_cycle_free(_M0L8filenameS863);
  _closure_1870
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866*)moonbit_malloc(sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866));
  Moonbit_object_header(_closure_1870)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1870->code
  = &_M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS866;
  _closure_1870->$0 = _M0L5indexS865;
  _closure_1870->$1 = _M0L8filenameS863;
  _M0L14handle__resultS866 = (struct _M0TWssbEu*)_closure_1870;
  _M0L17error__to__stringS873
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS873$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1872
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS894, _M0L8filenameS863, _M0L5indexS865, _M0L13handle__startS861, _M0L14handle__resultS866, _M0L17error__to__stringS873);
  if (_tmp_1872.tag) {
    int32_t const _M0L5_2aokS1844 = _tmp_1872.data.ok;
    _handle__error__result_1873 = _M0L5_2aokS1844;
  } else {
    void* const _M0L6_2aerrS1845 = _tmp_1872.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS873);
    moonbit_decref_cycle_free(_M0L13handle__startS861);
    _M0L11_2atry__errS888 = _M0L6_2aerrS1845;
    goto join_887;
  }
  if (_handle__error__result_1873) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS873);
    moonbit_decref_cycle_free(_M0L13handle__startS861);
    _M0L6_2atmpS1835 = 1;
  } else {
    struct moonbit_result_0 _tmp_1874;
    int32_t _handle__error__result_1875;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1874
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS894, _M0L8filenameS863, _M0L5indexS865, _M0L13handle__startS861, _M0L14handle__resultS866, _M0L17error__to__stringS873);
    if (_tmp_1874.tag) {
      int32_t const _M0L5_2aokS1842 = _tmp_1874.data.ok;
      _handle__error__result_1875 = _M0L5_2aokS1842;
    } else {
      void* const _M0L6_2aerrS1843 = _tmp_1874.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS873);
      moonbit_decref_cycle_free(_M0L13handle__startS861);
      _M0L11_2atry__errS888 = _M0L6_2aerrS1843;
      goto join_887;
    }
    if (_handle__error__result_1875) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS873);
      moonbit_decref_cycle_free(_M0L13handle__startS861);
      _M0L6_2atmpS1835 = 1;
    } else {
      struct moonbit_result_0 _tmp_1876;
      int32_t _handle__error__result_1877;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1876
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS894, _M0L8filenameS863, _M0L5indexS865, _M0L13handle__startS861, _M0L14handle__resultS866, _M0L17error__to__stringS873);
      if (_tmp_1876.tag) {
        int32_t const _M0L5_2aokS1840 = _tmp_1876.data.ok;
        _handle__error__result_1877 = _M0L5_2aokS1840;
      } else {
        void* const _M0L6_2aerrS1841 = _tmp_1876.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS873);
        moonbit_decref_cycle_free(_M0L13handle__startS861);
        _M0L11_2atry__errS888 = _M0L6_2aerrS1841;
        goto join_887;
      }
      if (_handle__error__result_1877) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS873);
        moonbit_decref_cycle_free(_M0L13handle__startS861);
        _M0L6_2atmpS1835 = 1;
      } else {
        struct moonbit_result_0 _tmp_1878;
        int32_t _handle__error__result_1879;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1878
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS894, _M0L8filenameS863, _M0L5indexS865, _M0L13handle__startS861, _M0L14handle__resultS866, _M0L17error__to__stringS873);
        if (_tmp_1878.tag) {
          int32_t const _M0L5_2aokS1838 = _tmp_1878.data.ok;
          _handle__error__result_1879 = _M0L5_2aokS1838;
        } else {
          void* const _M0L6_2aerrS1839 = _tmp_1878.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS873);
          moonbit_decref_cycle_free(_M0L13handle__startS861);
          _M0L11_2atry__errS888 = _M0L6_2aerrS1839;
          goto join_887;
        }
        if (_handle__error__result_1879) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS873);
          moonbit_decref_cycle_free(_M0L13handle__startS861);
          _M0L6_2atmpS1835 = 1;
        } else {
          struct moonbit_result_0 _tmp_1880;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1880
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS894, _M0L8filenameS863, _M0L5indexS865, _M0L13handle__startS861, _M0L14handle__resultS866, _M0L17error__to__stringS873);
          moonbit_decref_cycle_free(_M0L13handle__startS861);
          moonbit_decref_cycle_free(_M0L17error__to__stringS873);
          if (_tmp_1880.tag) {
            int32_t const _M0L5_2aokS1836 = _tmp_1880.data.ok;
            _M0L6_2atmpS1835 = _M0L5_2aokS1836;
          } else {
            void* const _M0L6_2aerrS1837 = _tmp_1880.data.err;
            _M0L11_2atry__errS888 = _M0L6_2aerrS1837;
            goto join_887;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1835) {
    void* _M0L131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1846 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1846)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1846)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS888
    = _M0L131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1846;
    goto join_887;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS866);
  }
  goto joinlet_1871;
  join_887:;
  _M0L3errS889 = _M0L11_2atry__errS888;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS892
  = (struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS889;
  _M0L7_2anameS893 = _M0L36_2aMoonBitTestDriverInternalSkipTestS892->$0;
  _M0L6_2acntS1863
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS892));
  if (_M0L6_2acntS1863 > 1) {
    int32_t _M0L11_2anew__cntS1864 = _M0L6_2acntS1863 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS892), _M0L11_2anew__cntS1864);
    moonbit_incref_cycle_free(_M0L7_2anameS893);
  } else if (_M0L6_2acntS1863 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS892);
  }
  _M0L4nameS891 = _M0L7_2anameS893;
  goto join_890;
  goto joinlet_1881;
  join_890:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS866(_M0L14handle__resultS866, _M0L4nameS891, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS866);
  moonbit_decref_cycle_free(_M0L4nameS891);
  joinlet_1881:;
  joinlet_1871:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS873(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1834,
  void* _M0L3errS874
) {
  void* _M0L1eS876;
  moonbit_string_t _M0L1eS878;
  moonbit_string_t _result_1884;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS874)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS879 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS874;
      moonbit_string_t _M0L4_2aeS880 = _M0L10_2aFailureS879->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS880);
      _M0L1eS878 = _M0L4_2aeS880;
      goto join_877;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS881 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS874;
      moonbit_string_t _M0L4_2aeS882 = _M0L15_2aInspectErrorS881->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS882);
      _M0L1eS878 = _M0L4_2aeS882;
      goto join_877;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS883 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS874;
      moonbit_string_t _M0L4_2aeS884 = _M0L16_2aSnapshotErrorS883->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS884);
      _M0L1eS878 = _M0L4_2aeS884;
      goto join_877;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS885 =
        (struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS874;
      moonbit_string_t _M0L4_2aeS886 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS885->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS886);
      _M0L1eS878 = _M0L4_2aeS886;
      goto join_877;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS874);
      _M0L1eS876 = _M0L3errS874;
      goto join_875;
      break;
    }
  }
  join_877:;
  return _M0L1eS878;
  join_875:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _result_1884 = _M0FP15Error10to__string(_M0L1eS876);
  moonbit_decref_cycle_free(_M0L1eS876);
  return _result_1884;
}

int32_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS866(
  struct _M0TWssbEu* _M0L6_2aenvS1831,
  moonbit_string_t _M0L10__testnameS867,
  moonbit_string_t _M0L7messageS868,
  int32_t _M0L7skippedS869
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866* _M0L14_2acasted__envS1832;
  moonbit_string_t _M0L8filenameS863;
  int32_t _M0L5indexS865;
  moonbit_string_t _M0L10file__nameS870;
  moonbit_string_t _M0L7messageS871;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS872;
  moonbit_string_t _M0L6_2atmpS1833;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1832
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c866*)_M0L6_2aenvS1831;
  _M0L8filenameS863 = _M0L14_2acasted__envS1832->$1;
  _M0L5indexS865 = _M0L14_2acasted__envS1832->$0;
  if (!_M0L7skippedS869 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS870
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS863, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS871
  = _M0MPC16string6String14escape_2einner(_M0L7messageS868, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS872
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS872, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS872, _M0L10file__nameS870);
  moonbit_decref_cycle_free(_M0L10file__nameS870);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS872, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS872, _M0L5indexS865);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS872, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS872, _M0L7messageS871);
  moonbit_decref_cycle_free(_M0L7messageS871);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS872, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1833
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS872);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS872);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1833);
  moonbit_decref_cycle_free(_M0L6_2atmpS1833);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS861(
  struct _M0TWEu* _M0L6_2aenvS1828
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861* _M0L14_2acasted__envS1829;
  moonbit_string_t _M0L8filenameS863;
  int32_t _M0L5indexS865;
  moonbit_string_t _M0L10file__nameS862;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS864;
  moonbit_string_t _M0L6_2atmpS1830;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1829
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__extended__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c861*)_M0L6_2aenvS1828;
  _M0L8filenameS863 = _M0L14_2acasted__envS1829->$1;
  _M0L5indexS865 = _M0L14_2acasted__envS1829->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS862
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS863, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS864
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS864, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS864, _M0L10file__nameS862);
  moonbit_decref_cycle_free(_M0L10file__nameS862);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS864, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS864, _M0L5indexS865);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS864, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1830
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS864);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS864);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1830);
  moonbit_decref_cycle_free(_M0L6_2atmpS1830);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S831;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS838;
  struct _M0TUsiE** _M0L6_2atmpS1827;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS845;
  moonbit_string_t* _M0L9cli__argsS846;
  moonbit_string_t _M0L6_2atmpS1826;
  moonbit_string_t _M0L6_2atmpS1825;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS847;
  int32_t _M0L7_2abindS848;
  moonbit_string_t* _M0L7_2abindS849;
  int32_t _M0L6_2acntS1865;
  int32_t _M0L2__S850;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S831 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS838 = 0;
  _M0L6_2atmpS1827 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS845
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS845)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS845->$0 = _M0L6_2atmpS1827;
  _M0L16file__and__indexS845->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS846
  = _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS846)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1826 = (moonbit_string_t)_M0L9cli__argsS846[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1826);
  moonbit_decref_cycle_free(_M0L9cli__argsS846);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1825
  = _M0MP46RiantR8snn__mbt8examples28if__extended__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1826);
  moonbit_decref_cycle_free(_M0L6_2atmpS1826);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS847
  = _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS838(_M0L51moonbit__test__driver__internal__split__mbt__stringS838, _M0L6_2atmpS1825, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1825);
  _M0L7_2abindS848 = _M0L10test__argsS847->$1;
  _M0L7_2abindS849 = _M0L10test__argsS847->$0;
  _M0L6_2acntS1865
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS847));
  if (_M0L6_2acntS1865 > 1) {
    int32_t _M0L11_2anew__cntS1866 = _M0L6_2acntS1865 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS847), _M0L11_2anew__cntS1866);
    moonbit_incref_cycle_free(_M0L7_2abindS849);
  } else if (_M0L6_2acntS1865 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS847);
  }
  _M0L2__S850 = 0;
  while (1) {
    if (_M0L2__S850 < _M0L7_2abindS848) {
      moonbit_string_t _M0L3argS851 =
        (moonbit_string_t)_M0L7_2abindS849[_M0L2__S850];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS852;
      moonbit_string_t _M0L4fileS853;
      moonbit_string_t _M0L5rangeS854;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS855;
      moonbit_string_t _M0L6_2atmpS1823;
      int32_t _M0L5startS856;
      moonbit_string_t _M0L6_2atmpS1822;
      int32_t _M0L3endS857;
      int32_t _M0L1iS858;
      int32_t _M0L6_2atmpS1824;
      moonbit_incref_cycle_free(_M0L3argS851);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS852
      = _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS838(_M0L51moonbit__test__driver__internal__split__mbt__stringS838, _M0L3argS851, 58);
      moonbit_decref_cycle_free(_M0L3argS851);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS853
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS852, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS854
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS852, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS852);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS855
      = _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS838(_M0L51moonbit__test__driver__internal__split__mbt__stringS838, _M0L5rangeS854, 45);
      moonbit_decref_cycle_free(_M0L5rangeS854);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1823
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS855, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS856
      = _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S831(_M0L45moonbit__test__driver__internal__parse__int__S831, _M0L6_2atmpS1823);
      moonbit_decref_cycle_free(_M0L6_2atmpS1823);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1822
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS855, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS855);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS857
      = _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S831(_M0L45moonbit__test__driver__internal__parse__int__S831, _M0L6_2atmpS1822);
      moonbit_decref_cycle_free(_M0L6_2atmpS1822);
      _M0L1iS858 = _M0L5startS856;
      while (1) {
        if (_M0L1iS858 < _M0L3endS857) {
          struct _M0TUsiE* _M0L8_2atupleS1820;
          int32_t _M0L6_2atmpS1821;
          moonbit_incref_cycle_free(_M0L4fileS853);
          _M0L8_2atupleS1820
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1820)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1820->$0 = _M0L4fileS853;
          _M0L8_2atupleS1820->$1 = _M0L1iS858;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS845, _M0L8_2atupleS1820);
          _M0L6_2atmpS1821 = _M0L1iS858 + 1;
          _M0L1iS858 = _M0L6_2atmpS1821;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS853);
        }
        break;
      }
      _M0L6_2atmpS1824 = _M0L2__S850 + 1;
      _M0L2__S850 = _M0L6_2atmpS1824;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS849);
    }
    break;
  }
  return _M0L16file__and__indexS845;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS838(
  int32_t _M0L6_2aenvS1801,
  moonbit_string_t _M0L1sS839,
  int32_t _M0L3sepS840
) {
  moonbit_string_t* _M0L6_2atmpS1819;
  struct _M0TPB5ArrayGsE* _M0L3resS841;
  struct _M0TPB8MutLocalGiE* _M0L1iS842;
  struct _M0TPB8MutLocalGiE* _M0L5startS843;
  int32_t _M0L3valS1814;
  int32_t _M0L6_2atmpS1815;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1819 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS841
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS841)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS841->$0 = _M0L6_2atmpS1819;
  _M0L3resS841->$1 = 0;
  _M0L1iS842
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS842)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS842->$0 = 0;
  _M0L5startS843
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS843)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS843->$0 = 0;
  while (1) {
    int32_t _M0L3valS1802 = _M0L1iS842->$0;
    int32_t _M0L6_2atmpS1803 = Moonbit_array_length(_M0L1sS839);
    if (_M0L3valS1802 < _M0L6_2atmpS1803) {
      int32_t _M0L3valS1806 = _M0L1iS842->$0;
      int32_t _M0L6_2atmpS1805;
      int32_t _M0L6_2atmpS1804;
      int32_t _M0L3valS1813;
      int32_t _M0L6_2atmpS1812;
      if (
        _M0L3valS1806 < 0
        || _M0L3valS1806 >= Moonbit_array_length(_M0L1sS839)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1805 = _M0L1sS839[_M0L3valS1806];
      _M0L6_2atmpS1804 = _M0L6_2atmpS1805;
      if (_M0L6_2atmpS1804 == _M0L3sepS840) {
        int32_t _M0L3valS1808 = _M0L5startS843->$0;
        int32_t _M0L3valS1809 = _M0L1iS842->$0;
        moonbit_string_t _M0L6_2atmpS1807;
        int32_t _M0L3valS1811;
        int32_t _M0L6_2atmpS1810;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1807
        = _M0MPC16string6String17unsafe__substring(_M0L1sS839, _M0L3valS1808, _M0L3valS1809);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS841, _M0L6_2atmpS1807);
        _M0L3valS1811 = _M0L1iS842->$0;
        _M0L6_2atmpS1810 = _M0L3valS1811 + 1;
        _M0L5startS843->$0 = _M0L6_2atmpS1810;
      }
      _M0L3valS1813 = _M0L1iS842->$0;
      _M0L6_2atmpS1812 = _M0L3valS1813 + 1;
      _M0L1iS842->$0 = _M0L6_2atmpS1812;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS842);
    }
    break;
  }
  _M0L3valS1814 = _M0L5startS843->$0;
  _M0L6_2atmpS1815 = Moonbit_array_length(_M0L1sS839);
  if (_M0L3valS1814 < _M0L6_2atmpS1815) {
    int32_t _M0L3valS1817 = _M0L5startS843->$0;
    int32_t _M0L6_2atmpS1818;
    moonbit_string_t _M0L6_2atmpS1816;
    moonbit_decref_cycle_free(_M0L5startS843);
    _M0L6_2atmpS1818 = Moonbit_array_length(_M0L1sS839);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1816
    = _M0MPC16string6String17unsafe__substring(_M0L1sS839, _M0L3valS1817, _M0L6_2atmpS1818);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS841, _M0L6_2atmpS1816);
  } else {
    moonbit_decref_cycle_free(_M0L5startS843);
  }
  return _M0L3resS841;
}

int32_t _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S831(
  int32_t _M0L6_2aenvS1794,
  moonbit_string_t _M0L1sS832
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS833;
  int32_t _M0L3lenS834;
  int32_t _M0L7_2abindS835;
  int32_t _M0L1iS836;
  int32_t _result_1889;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS833
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS833)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS833->$0 = 0;
  _M0L3lenS834 = Moonbit_array_length(_M0L1sS832);
  _M0L7_2abindS835 = 0;
  _M0L1iS836 = _M0L7_2abindS835;
  while (1) {
    if (_M0L1iS836 < _M0L3lenS834) {
      int32_t _M0L3valS1799 = _M0L3resS833->$0;
      int32_t _M0L6_2atmpS1796 = _M0L3valS1799 * 10;
      int32_t _M0L6_2atmpS1798;
      int32_t _M0L6_2atmpS1797;
      int32_t _M0L6_2atmpS1795;
      int32_t _M0L6_2atmpS1800;
      if (_M0L1iS836 < 0 || _M0L1iS836 >= Moonbit_array_length(_M0L1sS832)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1798 = _M0L1sS832[_M0L1iS836];
      _M0L6_2atmpS1797 = _M0L6_2atmpS1798 - 48;
      _M0L6_2atmpS1795 = _M0L6_2atmpS1796 + _M0L6_2atmpS1797;
      _M0L3resS833->$0 = _M0L6_2atmpS1795;
      _M0L6_2atmpS1800 = _M0L1iS836 + 1;
      _M0L1iS836 = _M0L6_2atmpS1800;
      continue;
    }
    break;
  }
  _result_1889 = _M0L3resS833->$0;
  moonbit_decref_cycle_free(_M0L3resS833);
  return _result_1889;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples28if__extended__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS830
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS830);
  return _M0L4selfS830;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S800,
  moonbit_string_t _M0L12_2adiscard__S801,
  int32_t _M0L12_2adiscard__S802,
  struct _M0TWEu* _M0L12_2adiscard__S803,
  struct _M0TWssbEu* _M0L12_2adiscard__S804,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S805
) {
  struct moonbit_result_0 _result_1890;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _result_1890.tag = 1;
  _result_1890.data.ok = 0;
  return _result_1890;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S806,
  moonbit_string_t _M0L12_2adiscard__S807,
  int32_t _M0L12_2adiscard__S808,
  struct _M0TWEu* _M0L12_2adiscard__S809,
  struct _M0TWssbEu* _M0L12_2adiscard__S810,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S811
) {
  struct moonbit_result_0 _result_1891;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _result_1891.tag = 1;
  _result_1891.data.ok = 0;
  return _result_1891;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S812,
  moonbit_string_t _M0L12_2adiscard__S813,
  int32_t _M0L12_2adiscard__S814,
  struct _M0TWEu* _M0L12_2adiscard__S815,
  struct _M0TWssbEu* _M0L12_2adiscard__S816,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S817
) {
  struct moonbit_result_0 _result_1892;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _result_1892.tag = 1;
  _result_1892.data.ok = 0;
  return _result_1892;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S818,
  moonbit_string_t _M0L12_2adiscard__S819,
  int32_t _M0L12_2adiscard__S820,
  struct _M0TWEu* _M0L12_2adiscard__S821,
  struct _M0TWssbEu* _M0L12_2adiscard__S822,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S823
) {
  struct moonbit_result_0 _result_1893;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _result_1893.tag = 1;
  _result_1893.data.ok = 0;
  return _result_1893;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S824,
  moonbit_string_t _M0L12_2adiscard__S825,
  int32_t _M0L12_2adiscard__S826,
  struct _M0TWEu* _M0L12_2adiscard__S827,
  struct _M0TWssbEu* _M0L12_2adiscard__S828,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S829
) {
  struct moonbit_result_0 _result_1894;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _result_1894.tag = 1;
  _result_1894.data.ok = 0;
  return _result_1894;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S799
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23integrate__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L1pS776,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS777,
  float _M0L2dtS778
) {
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0FP26RiantR8snn__mbt30update__synapses__extended__if(_M0L1pS776, _M0L5paramS777, _M0L2dtS778);
  #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0FP26RiantR8snn__mbt28update__neuron__extended__if(_M0L1pS776, _M0L5paramS777, _M0L2dtS778);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28update__neuron__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L1pS745,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS754,
  float _M0L2dtS764
) {
  int32_t _M0L1nS744;
  struct _M0TPB5ArrayGfE* _M0L1vS746;
  struct _M0TPB5ArrayGfE* _M0L6g__excS747;
  struct _M0TPB5ArrayGfE* _M0L5g__pvS748;
  struct _M0TPB5ArrayGfE* _M0L6g__sstS749;
  struct _M0TPB5ArrayGfE* _M0L4tabsS750;
  struct _M0TPB5ArrayGbE* _M0L4fireS751;
  struct _M0TPB5ArrayGfE* _M0L1iS752;
  float _M0L2cmS753;
  float _M0L2vtS755;
  float _M0L2vrS756;
  float _M0L2elS757;
  float _M0L2glS758;
  float _M0L4e__iS759;
  float _M0L4e__eS760;
  float _M0L8tau__absS761;
  float _M0L5alphaS762;
  float _M0L6_2atmpS1793;
  float _M0L6_2atmpS1792;
  int32_t _M0L11tabs__stepsS763;
  struct _M0TPB8MutLocalGiE* _M0L1kS765;
  #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L1nS744 = _M0L1pS745->$0;
  _M0L1vS746 = _M0L1pS745->$2;
  _M0L6g__excS747 = _M0L1pS745->$3;
  _M0L5g__pvS748 = _M0L1pS745->$4;
  _M0L6g__sstS749 = _M0L1pS745->$5;
  _M0L4tabsS750 = _M0L1pS745->$6;
  _M0L4fireS751 = _M0L1pS745->$7;
  _M0L1iS752 = _M0L1pS745->$8;
  _M0L2cmS753 = _M0L5paramS754->$0;
  _M0L2vtS755 = _M0L5paramS754->$1;
  _M0L2vrS756 = _M0L5paramS754->$2;
  _M0L2elS757 = _M0L5paramS754->$3;
  _M0L2glS758 = _M0L5paramS754->$4;
  _M0L4e__iS759 = _M0L5paramS754->$7;
  _M0L4e__eS760 = _M0L5paramS754->$8;
  _M0L8tau__absS761 = _M0L5paramS754->$9;
  _M0L5alphaS762 = _M0L5paramS754->$10;
  _M0L6_2atmpS1793 = _M0L8tau__absS761 / _M0L2dtS764;
  _M0L6_2atmpS1792 = _M0L6_2atmpS1793 + 0x1p-1f;
  #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L11tabs__stepsS763 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1792);
  _M0L1kS765
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS765)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS765->$0 = 0;
  while (1) {
    int32_t _M0L3valS1749 = _M0L1kS765->$0;
    if (_M0L3valS1749 < _M0L1nS744) {
      int32_t _M0L3valS1791 = _M0L1kS765->$0;
      float _M0L7tabs__kS766;
      int32_t _M0L3valS1790;
      float _M0L4v__kS768;
      int32_t _M0L3valS1789;
      float _M0L4g__eS769;
      int32_t _M0L3valS1788;
      float _M0L4g__pS770;
      int32_t _M0L3valS1787;
      float _M0L4g__sS771;
      int32_t _M0L3valS1786;
      float _M0L4i__kS772;
      float _M0L6_2atmpS1785;
      float _M0L6_2atmpS1782;
      float _M0L6_2atmpS1784;
      float _M0L6_2atmpS1783;
      float _M0L6_2atmpS1779;
      float _M0L6_2atmpS1781;
      float _M0L6_2atmpS1780;
      float _M0L6_2atmpS1776;
      float _M0L6_2atmpS1778;
      float _M0L6_2atmpS1777;
      float _M0L6_2atmpS1770;
      float _M0L6_2atmpS1775;
      float _M0L6_2atmpS1774;
      float _M0L6_2atmpS1772;
      float _M0L6_2atmpS1773;
      float _M0L6_2atmpS1771;
      float _M0L6_2atmpS1769;
      float _M0L6_2atmpS1768;
      float _M0L2dvS773;
      float _M0L6_2atmpS1767;
      float _M0L6v__newS774;
      int32_t _M0L1fS775;
      int32_t _M0L3valS1758;
      float _M0L6_2atmpS1759;
      int32_t _M0L6_2atmpS1757;
      int32_t _M0L3valS1761;
      int32_t _M0L6_2atmpS1760;
      int32_t _M0L3valS1763;
      float _M0L6_2atmpS1764;
      int32_t _M0L6_2atmpS1762;
      int32_t _M0L3valS1766;
      int32_t _M0L6_2atmpS1765;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L7tabs__kS766
      = _M0MPC15array5Array2atGfE(_M0L4tabsS750, _M0L3valS1791);
      if (_M0L7tabs__kS766 > 0x0p+0f) {
        int32_t _M0L3valS1751 = _M0L1kS765->$0;
        int32_t _M0L6_2atmpS1750;
        int32_t _M0L3valS1753;
        float _M0L6_2atmpS1754;
        int32_t _M0L6_2atmpS1752;
        int32_t _M0L3valS1756;
        int32_t _M0L6_2atmpS1755;
        #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
        _M0L6_2atmpS1750
        = _M0MPC15array5Array3setGbE(_M0L4fireS751, _M0L3valS1751, 0);
        _M0L3valS1753 = _M0L1kS765->$0;
        _M0L6_2atmpS1754 = _M0L7tabs__kS766 - 0x1p+0f;
        #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
        _M0L6_2atmpS1752
        = _M0MPC15array5Array3setGfE(_M0L4tabsS750, _M0L3valS1753, _M0L6_2atmpS1754);
        _M0L3valS1756 = _M0L1kS765->$0;
        _M0L6_2atmpS1755 = _M0L3valS1756 + 1;
        _M0L1kS765->$0 = _M0L6_2atmpS1755;
        continue;
      }
      _M0L3valS1790 = _M0L1kS765->$0;
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4v__kS768 = _M0MPC15array5Array2atGfE(_M0L1vS746, _M0L3valS1790);
      _M0L3valS1789 = _M0L1kS765->$0;
      #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4g__eS769
      = _M0MPC15array5Array2atGfE(_M0L6g__excS747, _M0L3valS1789);
      _M0L3valS1788 = _M0L1kS765->$0;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4g__pS770
      = _M0MPC15array5Array2atGfE(_M0L5g__pvS748, _M0L3valS1788);
      _M0L3valS1787 = _M0L1kS765->$0;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4g__sS771
      = _M0MPC15array5Array2atGfE(_M0L6g__sstS749, _M0L3valS1787);
      _M0L3valS1786 = _M0L1kS765->$0;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4i__kS772 = _M0MPC15array5Array2atGfE(_M0L1iS752, _M0L3valS1786);
      _M0L6_2atmpS1785 = _M0L2elS757 - _M0L4v__kS768;
      _M0L6_2atmpS1782 = _M0L2glS758 * _M0L6_2atmpS1785;
      _M0L6_2atmpS1784 = _M0L4e__eS760 - _M0L4v__kS768;
      _M0L6_2atmpS1783 = _M0L4g__eS769 * _M0L6_2atmpS1784;
      _M0L6_2atmpS1779 = _M0L6_2atmpS1782 + _M0L6_2atmpS1783;
      _M0L6_2atmpS1781 = _M0L4e__iS759 - _M0L4v__kS768;
      _M0L6_2atmpS1780 = _M0L4g__pS770 * _M0L6_2atmpS1781;
      _M0L6_2atmpS1776 = _M0L6_2atmpS1779 + _M0L6_2atmpS1780;
      _M0L6_2atmpS1778 = _M0L4e__iS759 - _M0L4v__kS768;
      _M0L6_2atmpS1777 = _M0L4g__sS771 * _M0L6_2atmpS1778;
      _M0L6_2atmpS1770 = _M0L6_2atmpS1776 + _M0L6_2atmpS1777;
      _M0L6_2atmpS1775 = -_M0L5alphaS762;
      _M0L6_2atmpS1774 = _M0L6_2atmpS1775 * _M0L4g__eS769;
      _M0L6_2atmpS1772 = _M0L6_2atmpS1774 * _M0L4g__sS771;
      _M0L6_2atmpS1773 = _M0L4e__eS760 - _M0L4v__kS768;
      _M0L6_2atmpS1771 = _M0L6_2atmpS1772 * _M0L6_2atmpS1773;
      _M0L6_2atmpS1769 = _M0L6_2atmpS1770 + _M0L6_2atmpS1771;
      _M0L6_2atmpS1768 = _M0L6_2atmpS1769 + _M0L4i__kS772;
      _M0L2dvS773 = _M0L6_2atmpS1768 / _M0L2cmS753;
      _M0L6_2atmpS1767 = _M0L2dtS764 * _M0L2dvS773;
      _M0L6v__newS774 = _M0L4v__kS768 + _M0L6_2atmpS1767;
      _M0L1fS775 = _M0L6v__newS774 > _M0L2vtS755;
      _M0L3valS1758 = _M0L1kS765->$0;
      if (_M0L1fS775) {
        _M0L6_2atmpS1759 = _M0L2vrS756;
      } else {
        _M0L6_2atmpS1759 = _M0L6v__newS774;
      }
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1757
      = _M0MPC15array5Array3setGfE(_M0L1vS746, _M0L3valS1758, _M0L6_2atmpS1759);
      _M0L3valS1761 = _M0L1kS765->$0;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1760
      = _M0MPC15array5Array3setGbE(_M0L4fireS751, _M0L3valS1761, _M0L1fS775);
      _M0L3valS1763 = _M0L1kS765->$0;
      if (_M0L1fS775) {
        _M0L6_2atmpS1764 = (float)_M0L11tabs__stepsS763;
      } else {
        _M0L6_2atmpS1764 = 0x0p+0f;
      }
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1762
      = _M0MPC15array5Array3setGfE(_M0L4tabsS750, _M0L3valS1763, _M0L6_2atmpS1764);
      _M0L3valS1766 = _M0L1kS765->$0;
      _M0L6_2atmpS1765 = _M0L3valS1766 + 1;
      _M0L1kS765->$0 = _M0L6_2atmpS1765;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS765);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt30update__synapses__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L1pS734,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS739,
  float _M0L2dtS742
) {
  int32_t _M0L1nS733;
  struct _M0TPB5ArrayGfE* _M0L6g__excS735;
  struct _M0TPB5ArrayGfE* _M0L5g__pvS736;
  struct _M0TPB5ArrayGfE* _M0L6g__sstS737;
  float _M0L6tau__eS738;
  float _M0L6tau__iS740;
  struct _M0TPB8MutLocalGiE* _M0L1kS741;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L1nS733 = _M0L1pS734->$0;
  _M0L6g__excS735 = _M0L1pS734->$3;
  _M0L5g__pvS736 = _M0L1pS734->$4;
  _M0L6g__sstS737 = _M0L1pS734->$5;
  _M0L6tau__eS738 = _M0L5paramS739->$5;
  _M0L6tau__iS740 = _M0L5paramS739->$6;
  _M0L1kS741
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS741)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS741->$0 = 0;
  while (1) {
    int32_t _M0L3valS1716 = _M0L1kS741->$0;
    if (_M0L3valS1716 < _M0L1nS733) {
      int32_t _M0L3valS1718 = _M0L1kS741->$0;
      int32_t _M0L3valS1726 = _M0L1kS741->$0;
      float _M0L6_2atmpS1720;
      int32_t _M0L3valS1725;
      float _M0L6_2atmpS1724;
      float _M0L6_2atmpS1723;
      float _M0L6_2atmpS1722;
      float _M0L6_2atmpS1721;
      float _M0L6_2atmpS1719;
      int32_t _M0L6_2atmpS1717;
      int32_t _M0L3valS1728;
      int32_t _M0L3valS1736;
      float _M0L6_2atmpS1730;
      int32_t _M0L3valS1735;
      float _M0L6_2atmpS1734;
      float _M0L6_2atmpS1733;
      float _M0L6_2atmpS1732;
      float _M0L6_2atmpS1731;
      float _M0L6_2atmpS1729;
      int32_t _M0L6_2atmpS1727;
      int32_t _M0L3valS1738;
      int32_t _M0L3valS1746;
      float _M0L6_2atmpS1740;
      int32_t _M0L3valS1745;
      float _M0L6_2atmpS1744;
      float _M0L6_2atmpS1743;
      float _M0L6_2atmpS1742;
      float _M0L6_2atmpS1741;
      float _M0L6_2atmpS1739;
      int32_t _M0L6_2atmpS1737;
      int32_t _M0L3valS1748;
      int32_t _M0L6_2atmpS1747;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1720
      = _M0MPC15array5Array2atGfE(_M0L6g__excS735, _M0L3valS1726);
      _M0L3valS1725 = _M0L1kS741->$0;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1724
      = _M0MPC15array5Array2atGfE(_M0L6g__excS735, _M0L3valS1725);
      _M0L6_2atmpS1723 = -_M0L6_2atmpS1724;
      _M0L6_2atmpS1722 = _M0L6_2atmpS1723 / _M0L6tau__eS738;
      _M0L6_2atmpS1721 = _M0L2dtS742 * _M0L6_2atmpS1722;
      _M0L6_2atmpS1719 = _M0L6_2atmpS1720 + _M0L6_2atmpS1721;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1717
      = _M0MPC15array5Array3setGfE(_M0L6g__excS735, _M0L3valS1718, _M0L6_2atmpS1719);
      _M0L3valS1728 = _M0L1kS741->$0;
      _M0L3valS1736 = _M0L1kS741->$0;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1730
      = _M0MPC15array5Array2atGfE(_M0L5g__pvS736, _M0L3valS1736);
      _M0L3valS1735 = _M0L1kS741->$0;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1734
      = _M0MPC15array5Array2atGfE(_M0L5g__pvS736, _M0L3valS1735);
      _M0L6_2atmpS1733 = -_M0L6_2atmpS1734;
      _M0L6_2atmpS1732 = _M0L6_2atmpS1733 / _M0L6tau__iS740;
      _M0L6_2atmpS1731 = _M0L2dtS742 * _M0L6_2atmpS1732;
      _M0L6_2atmpS1729 = _M0L6_2atmpS1730 + _M0L6_2atmpS1731;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1727
      = _M0MPC15array5Array3setGfE(_M0L5g__pvS736, _M0L3valS1728, _M0L6_2atmpS1729);
      _M0L3valS1738 = _M0L1kS741->$0;
      _M0L3valS1746 = _M0L1kS741->$0;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1740
      = _M0MPC15array5Array2atGfE(_M0L6g__sstS737, _M0L3valS1746);
      _M0L3valS1745 = _M0L1kS741->$0;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1744
      = _M0MPC15array5Array2atGfE(_M0L6g__sstS737, _M0L3valS1745);
      _M0L6_2atmpS1743 = -_M0L6_2atmpS1744;
      _M0L6_2atmpS1742 = _M0L6_2atmpS1743 / _M0L6tau__iS740;
      _M0L6_2atmpS1741 = _M0L2dtS742 * _M0L6_2atmpS1742;
      _M0L6_2atmpS1739 = _M0L6_2atmpS1740 + _M0L6_2atmpS1741;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1737
      = _M0MPC15array5Array3setGfE(_M0L6g__sstS737, _M0L3valS1738, _M0L6_2atmpS1739);
      _M0L3valS1748 = _M0L1kS741->$0;
      _M0L6_2atmpS1747 = _M0L3valS1748 + 1;
      _M0L1kS741->$0 = _M0L6_2atmpS1747;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS741);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0MP26RiantR8snn__mbt10ExtendedIF3new(
  int64_t _M0L7n_2eoptS728,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L11param_2eoptS731
) {
  int32_t _M0L1nS727;
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS730;
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _result_1897;
  if (_M0L7n_2eoptS728 == 4294967296ll) {
    _M0L1nS727 = 100;
  } else {
    int64_t _M0L7_2aSomeS729 = _M0L7n_2eoptS728;
    _M0L1nS727 = (int32_t)_M0L7_2aSomeS729;
  }
  if (_M0L11param_2eoptS731 == 0) {
    #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
    _M0L5paramS730 = _M0MP26RiantR8snn__mbt19ExtendedIFParameter3new();
  } else {
    struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L7_2aSomeS732 =
      _M0L11param_2eoptS731;
    if (_M0L7_2aSomeS732) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS732);
    }
    _M0L5paramS730 = _M0L7_2aSomeS732;
  }
  _result_1897
  = _M0MP26RiantR8snn__mbt10ExtendedIF11new_2einner(_M0L1nS727, _M0L5paramS730);
  moonbit_decref_cycle_free(_M0L5paramS730);
  return _result_1897;
}

struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0MP26RiantR8snn__mbt10ExtendedIF11new_2einner(
  int32_t _M0L1nS719,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS720
) {
  float _M0L2vrS1715;
  struct _M0TPB5ArrayGfE* _M0L1vS718;
  struct _M0TPB5ArrayGfE* _M0L6g__excS721;
  struct _M0TPB5ArrayGfE* _M0L5g__pvS722;
  struct _M0TPB5ArrayGfE* _M0L6g__sstS723;
  struct _M0TPB5ArrayGfE* _M0L4tabsS724;
  struct _M0TPB5ArrayGbE* _M0L4fireS725;
  struct _M0TPB5ArrayGfE* _M0L1iS726;
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _block_1898;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L2vrS1715 = _M0L5paramS720->$2;
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L1vS718 = _M0MPC15array5Array4makeGfE(_M0L1nS719, _M0L2vrS1715);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L6g__excS721 = _M0MPC15array5Array4makeGfE(_M0L1nS719, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L5g__pvS722 = _M0MPC15array5Array4makeGfE(_M0L1nS719, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L6g__sstS723 = _M0MPC15array5Array4makeGfE(_M0L1nS719, 0x0p+0f);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L4tabsS724 = _M0MPC15array5Array4makeGfE(_M0L1nS719, 0x0p+0f);
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L4fireS725 = _M0MPC15array5Array4makeGbE(_M0L1nS719, 0);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L1iS726 = _M0MPC15array5Array4makeGfE(_M0L1nS719, 0x0p+0f);
  moonbit_incref_cycle_free(_M0L5paramS720);
  _block_1898
  = (struct _M0TP26RiantR8snn__mbt10ExtendedIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt10ExtendedIF));
  Moonbit_object_header(_block_1898)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_1898->$0 = _M0L1nS719;
  _block_1898->$1 = _M0L5paramS720;
  _block_1898->$2 = _M0L1vS718;
  _block_1898->$3 = _M0L6g__excS721;
  _block_1898->$4 = _M0L5g__pvS722;
  _block_1898->$5 = _M0L6g__sstS723;
  _block_1898->$6 = _M0L4tabsS724;
  _block_1898->$7 = _M0L4fireS725;
  _block_1898->$8 = _M0L1iS726;
  return _block_1898;
}

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0MP26RiantR8snn__mbt19ExtendedIFParameter6custom(
  float _M0L2cmS707,
  float _M0L2vtS708,
  float _M0L2vrS709,
  float _M0L2elS710,
  float _M0L2glS711,
  float _M0L6tau__eS712,
  float _M0L6tau__iS713,
  float _M0L4e__iS714,
  float _M0L4e__eS715,
  float _M0L8tau__absS716,
  float _M0L5alphaS717
) {
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _block_1899;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _block_1899
  = (struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter));
  Moonbit_object_header(_block_1899)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1899->$0 = _M0L2cmS707;
  _block_1899->$1 = _M0L2vtS708;
  _block_1899->$2 = _M0L2vrS709;
  _block_1899->$3 = _M0L2elS710;
  _block_1899->$4 = _M0L2glS711;
  _block_1899->$5 = _M0L6tau__eS712;
  _block_1899->$6 = _M0L6tau__iS713;
  _block_1899->$7 = _M0L4e__iS714;
  _block_1899->$8 = _M0L4e__eS715;
  _block_1899->$9 = _M0L8tau__absS716;
  _block_1899->$10 = _M0L5alphaS717;
  return _block_1899;
}

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0MP26RiantR8snn__mbt19ExtendedIFParameter3new(
  
) {
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _block_1900;
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _block_1900
  = (struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter));
  Moonbit_object_header(_block_1900)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1900->$0 = 0x1.f4p+7f;
  _block_1900->$1 = -0x1.4p+5f;
  _block_1900->$2 = -0x1.04p+6f;
  _block_1900->$3 = -0x1.18p+6f;
  _block_1900->$4 = 0x1.4p+3f;
  _block_1900->$5 = 0x1.8p+2f;
  _block_1900->$6 = 0x1.4p+4f;
  _block_1900->$7 = -0x1.2cp+6f;
  _block_1900->$8 = 0x0p+0f;
  _block_1900->$9 = 0x1.4p+2f;
  _block_1900->$10 = 0x0p+0f;
  return _block_1900;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS706) {
  double _M0L6_2atmpS1714;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1714 = (double)_M0L4selfS706;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1714);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS705) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS705 != _M0L4selfS705) {
    return 0;
  } else if (_M0L4selfS705 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS705 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS705;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS696,
  float _M0L4elemS698
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS695;
  int32_t _M0L1iS697;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS695 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS696);
  _M0L1iS697 = 0;
  while (1) {
    if (_M0L1iS697 < _M0L3lenS696) {
      float* _M0L3bufS1710 = _M0L3arrS695->$0;
      int32_t _M0L6_2atmpS1711;
      _M0L3bufS1710[_M0L1iS697] = _M0L4elemS698;
      _M0L6_2atmpS1711 = _M0L1iS697 + 1;
      _M0L1iS697 = _M0L6_2atmpS1711;
      continue;
    }
    break;
  }
  return _M0L3arrS695;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS701,
  int32_t _M0L4elemS703
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS700;
  int32_t _M0L1iS702;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS700 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS701);
  _M0L1iS702 = 0;
  while (1) {
    if (_M0L1iS702 < _M0L3lenS701) {
      uint8_t* _M0L3bufS1712 = _M0L3arrS700->$0;
      int32_t _M0L6_2atmpS1713;
      _M0L3bufS1712[_M0L1iS702] = _M0L4elemS703;
      _M0L6_2atmpS1713 = _M0L1iS702 + 1;
      _M0L1iS702 = _M0L6_2atmpS1713;
      continue;
    }
    break;
  }
  return _M0L3arrS700;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS688,
  int32_t _M0L5indexS689,
  float _M0L5valueS690
) {
  int32_t _M0L3lenS687;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS687 = _M0L4selfS688->$1;
  if (_M0L5indexS689 >= 0 && _M0L5indexS689 < _M0L3lenS687) {
    float* _M0L6_2atmpS1708;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1708 = _M0MPC15array5Array6bufferGfE(_M0L4selfS688);
    _M0L6_2atmpS1708[_M0L5indexS689] = _M0L5valueS690;
    moonbit_decref_cycle_free(_M0L6_2atmpS1708);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS692,
  int32_t _M0L5indexS693,
  int32_t _M0L5valueS694
) {
  int32_t _M0L3lenS691;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS691 = _M0L4selfS692->$1;
  if (_M0L5indexS693 >= 0 && _M0L5indexS693 < _M0L3lenS691) {
    uint8_t* _M0L6_2atmpS1709;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1709 = _M0MPC15array5Array6bufferGbE(_M0L4selfS692);
    _M0L6_2atmpS1709[_M0L5indexS693] = _M0L5valueS694;
    moonbit_decref_cycle_free(_M0L6_2atmpS1709);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS679,
  int32_t _M0L5indexS680
) {
  int32_t _M0L3lenS678;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS678 = _M0L4selfS679->$1;
  if (_M0L5indexS680 >= 0 && _M0L5indexS680 < _M0L3lenS678) {
    uint8_t* _M0L6_2atmpS1705;
    int32_t _result_1903;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1705 = _M0MPC15array5Array6bufferGbE(_M0L4selfS679);
    _result_1903 = (int32_t)_M0L6_2atmpS1705[_M0L5indexS680];
    moonbit_decref_cycle_free(_M0L6_2atmpS1705);
    return _result_1903;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS682,
  int32_t _M0L5indexS683
) {
  int32_t _M0L3lenS681;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS681 = _M0L4selfS682->$1;
  if (_M0L5indexS683 >= 0 && _M0L5indexS683 < _M0L3lenS681) {
    float* _M0L6_2atmpS1706;
    float _result_1904;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1706 = _M0MPC15array5Array6bufferGfE(_M0L4selfS682);
    _result_1904 = (float)_M0L6_2atmpS1706[_M0L5indexS683];
    moonbit_decref_cycle_free(_M0L6_2atmpS1706);
    return _result_1904;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS685,
  int32_t _M0L5indexS686
) {
  int32_t _M0L3lenS684;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS684 = _M0L4selfS685->$1;
  if (_M0L5indexS686 >= 0 && _M0L5indexS686 < _M0L3lenS684) {
    moonbit_string_t* _M0L6_2atmpS1707;
    moonbit_string_t _M0L6_2atmpS1848;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1707 = _M0MPC15array5Array6bufferGsE(_M0L4selfS685);
    _M0L6_2atmpS1848 = (moonbit_string_t)_M0L6_2atmpS1707[_M0L5indexS686];
    moonbit_incref_cycle_free(_M0L6_2atmpS1848);
    moonbit_decref_cycle_free(_M0L6_2atmpS1707);
    return _M0L6_2atmpS1848;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS677) {
  moonbit_string_t _M0L6_2atmpS1704;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1704 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS677);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1704);
  moonbit_decref_cycle_free(_M0L6_2atmpS1704);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS676) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS676);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS661) {
  uint64_t _M0L4bitsS664;
  uint64_t _M0L6_2atmpS1703;
  uint64_t _M0L6_2atmpS1702;
  int32_t _M0L8ieeeSignS665;
  uint64_t _M0L12ieeeMantissaS666;
  uint64_t _M0L6_2atmpS1701;
  uint64_t _M0L6_2atmpS1700;
  int32_t _M0L12ieeeExponentS667;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS668;
  struct _M0TPB17FloatingDecimal64* _M0L1vS669;
  moonbit_string_t _result_1906;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS661 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS661 >= -0x1p+53 && _M0L3valS661 <= 0x1p+53) {
    if (_M0L3valS661 >= -0x1p+31 && _M0L3valS661 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS662;
      double _M0L6_2atmpS1689;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS662 = _M0MPC16double6Double7to__int(_M0L3valS661);
      _M0L6_2atmpS1689 = (double)_M0L1iS662;
      if (_M0L6_2atmpS1689 == _M0L3valS661) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS662, 10);
      }
    } else {
      int64_t _M0L1iS663;
      double _M0L6_2atmpS1690;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS663 = _M0MPC16double6Double9to__int64(_M0L3valS661);
      _M0L6_2atmpS1690 = (double)_M0L1iS663;
      if (_M0L6_2atmpS1690 == _M0L3valS661) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS663, 10);
      }
    }
  }
  _M0L4bitsS664 = *(int64_t*)&_M0L3valS661;
  _M0L6_2atmpS1703 = _M0L4bitsS664 >> 63;
  _M0L6_2atmpS1702 = _M0L6_2atmpS1703 & 1ull;
  _M0L8ieeeSignS665 = _M0L6_2atmpS1702 != 0ull;
  _M0L12ieeeMantissaS666 = _M0L4bitsS664 & 4503599627370495ull;
  _M0L6_2atmpS1701 = _M0L4bitsS664 >> 52;
  _M0L6_2atmpS1700 = _M0L6_2atmpS1701 & 2047ull;
  _M0L12ieeeExponentS667 = (int32_t)_M0L6_2atmpS1700;
  if (
    _M0L12ieeeExponentS667 == 2047
    || _M0L12ieeeExponentS667 == 0 && _M0L12ieeeMantissaS666 == 0ull
  ) {
    int32_t _M0L6_2atmpS1691 = _M0L12ieeeExponentS667 != 0;
    int32_t _M0L6_2atmpS1692 = _M0L12ieeeMantissaS666 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS665, _M0L6_2atmpS1691, _M0L6_2atmpS1692);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS668
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS666, _M0L12ieeeExponentS667);
  if (_M0L7_2abindS668 == 0) {
    uint32_t _M0L6_2atmpS1693;
    if (_M0L7_2abindS668) {
      moonbit_decref_cycle_free(_M0L7_2abindS668);
    }
    _M0L6_2atmpS1693 = *(uint32_t*)&_M0L12ieeeExponentS667;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS669 = _M0FPB3d2d(_M0L12ieeeMantissaS666, _M0L6_2atmpS1693);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS670 = _M0L7_2abindS668;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS671 = _M0L7_2aSomeS670;
    struct _M0TPB17FloatingDecimal64* _M0L1xS672 = _M0L4_2afS671;
    while (1) {
      uint64_t _M0L8mantissaS1699 = _M0L1xS672->$0;
      uint64_t _M0L1qS673 = _M0L8mantissaS1699 / 10ull;
      uint64_t _M0L8mantissaS1697 = _M0L1xS672->$0;
      uint64_t _M0L6_2atmpS1698 = 10ull * _M0L1qS673;
      uint64_t _M0L1rS674 = _M0L8mantissaS1697 - _M0L6_2atmpS1698;
      int32_t _M0L8exponentS1696;
      int32_t _M0L6_2atmpS1695;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1694;
      if (_M0L1rS674 != 0ull) {
        _M0L1vS669 = _M0L1xS672;
        break;
      }
      _M0L8exponentS1696 = _M0L1xS672->$1;
      moonbit_decref_cycle_free(_M0L1xS672);
      _M0L6_2atmpS1695 = _M0L8exponentS1696 + 1;
      _M0L6_2atmpS1694
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1694)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1694->$0 = _M0L1qS673;
      _M0L6_2atmpS1694->$1 = _M0L6_2atmpS1695;
      _M0L1xS672 = _M0L6_2atmpS1694;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1906 = _M0FPB9to__chars(_M0L1vS669, _M0L8ieeeSignS665);
  moonbit_decref_cycle_free(_M0L1vS669);
  return _result_1906;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS656,
  int32_t _M0L12ieeeExponentS658
) {
  uint64_t _M0L2m2S655;
  int32_t _M0L6_2atmpS1688;
  int32_t _M0L2e2S657;
  int32_t _M0L6_2atmpS1687;
  uint64_t _M0L6_2atmpS1686;
  uint64_t _M0L4maskS659;
  uint64_t _M0L8fractionS660;
  int32_t _M0L6_2atmpS1685;
  uint64_t _M0L6_2atmpS1684;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1683;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S655 = 4503599627370496ull | _M0L12ieeeMantissaS656;
  _M0L6_2atmpS1688 = _M0L12ieeeExponentS658 - 1023;
  _M0L2e2S657 = _M0L6_2atmpS1688 - 52;
  if (_M0L2e2S657 > 0) {
    return 0;
  }
  if (_M0L2e2S657 < -52) {
    return 0;
  }
  _M0L6_2atmpS1687 = -_M0L2e2S657;
  _M0L6_2atmpS1686 = 1ull << (_M0L6_2atmpS1687 & 63);
  _M0L4maskS659 = _M0L6_2atmpS1686 - 1ull;
  _M0L8fractionS660 = _M0L2m2S655 & _M0L4maskS659;
  if (_M0L8fractionS660 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1685 = -_M0L2e2S657;
  _M0L6_2atmpS1684 = _M0L2m2S655 >> (_M0L6_2atmpS1685 & 63);
  _M0L6_2atmpS1683
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1683)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1683->$0 = _M0L6_2atmpS1684;
  _M0L6_2atmpS1683->$1 = 0;
  return _M0L6_2atmpS1683;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS623,
  int32_t _M0L4signS621
) {
  moonbit_bytes_t _M0L6resultS619;
  int32_t _M0Lm5indexS620;
  uint64_t _M0L6outputS622;
  int32_t _M0L7olengthS624;
  int32_t _M0L8exponentS1682;
  int32_t _M0L6_2atmpS1681;
  int32_t _M0Lm3expS625;
  int32_t _M0L6_2atmpS1680;
  int32_t _M0L6_2atmpS1678;
  int32_t _M0L18scientificNotationS626;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS619 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS620 = 0;
  if (_M0L4signS621) {
    int32_t _M0L6_2atmpS1552 = _M0Lm5indexS620;
    int32_t _M0L6_2atmpS1553;
    if (
      _M0L6_2atmpS1552 < 0
      || _M0L6_2atmpS1552 >= Moonbit_array_length(_M0L6resultS619)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS619[_M0L6_2atmpS1552] = 45;
    _M0L6_2atmpS1553 = _M0Lm5indexS620;
    _M0Lm5indexS620 = _M0L6_2atmpS1553 + 1;
  }
  _M0L6outputS622 = _M0L1vS623->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS624 = _M0FPB17decimal__length17(_M0L6outputS622);
  _M0L8exponentS1682 = _M0L1vS623->$1;
  _M0L6_2atmpS1681 = _M0L8exponentS1682 + _M0L7olengthS624;
  _M0Lm3expS625 = _M0L6_2atmpS1681 - 1;
  _M0L6_2atmpS1680 = _M0Lm3expS625;
  if (_M0L6_2atmpS1680 >= -6) {
    int32_t _M0L6_2atmpS1679 = _M0Lm3expS625;
    _M0L6_2atmpS1678 = _M0L6_2atmpS1679 < 21;
  } else {
    _M0L6_2atmpS1678 = 0;
  }
  _M0L18scientificNotationS626 = !_M0L6_2atmpS1678;
  if (_M0L18scientificNotationS626) {
    int32_t _M0L7_2abindS627 = _M0L7olengthS624 - 1;
    uint64_t _M0L6outputS628;
    int32_t _M0L1iS629 = 0;
    uint64_t _M0L6outputS630 = _M0L6outputS622;
    int32_t _M0L6_2atmpS1554;
    int32_t _M0L6_2atmpS1558;
    int32_t _M0L6_2atmpS1557;
    int32_t _M0L6_2atmpS1556;
    int32_t _M0L6_2atmpS1555;
    int32_t _M0L6_2atmpS1562;
    int32_t _M0L6_2atmpS1563;
    int32_t _M0L6_2atmpS1564;
    int32_t _M0L6_2atmpS1565;
    int32_t _M0L6_2atmpS1566;
    int32_t _M0L6_2atmpS1572;
    int32_t _M0L6_2atmpS1605;
    moonbit_string_t _result_1908;
    while (1) {
      if (_M0L1iS629 < _M0L7_2abindS627) {
        uint64_t _M0L1cS631 = _M0L6outputS630 % 10ull;
        int32_t _M0L6_2atmpS1611 = _M0Lm5indexS620;
        int32_t _M0L6_2atmpS1610 = _M0L6_2atmpS1611 + _M0L7olengthS624;
        int32_t _M0L6_2atmpS1606 = _M0L6_2atmpS1610 - _M0L1iS629;
        int32_t _M0L6_2atmpS1609 = (int32_t)_M0L1cS631;
        int32_t _M0L6_2atmpS1608 = 48 + _M0L6_2atmpS1609;
        int32_t _M0L6_2atmpS1607 = _M0L6_2atmpS1608 & 0xff;
        int32_t _M0L6_2atmpS1612;
        uint64_t _M0L6_2atmpS1613;
        if (
          _M0L6_2atmpS1606 < 0
          || _M0L6_2atmpS1606 >= Moonbit_array_length(_M0L6resultS619)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS619[_M0L6_2atmpS1606] = _M0L6_2atmpS1607;
        _M0L6_2atmpS1612 = _M0L1iS629 + 1;
        _M0L6_2atmpS1613 = _M0L6outputS630 / 10ull;
        _M0L1iS629 = _M0L6_2atmpS1612;
        _M0L6outputS630 = _M0L6_2atmpS1613;
        continue;
      } else {
        _M0L6outputS628 = _M0L6outputS630;
      }
      break;
    }
    _M0L6_2atmpS1554 = _M0Lm5indexS620;
    _M0L6_2atmpS1558 = (int32_t)_M0L6outputS628;
    _M0L6_2atmpS1557 = _M0L6_2atmpS1558 % 10;
    _M0L6_2atmpS1556 = 48 + _M0L6_2atmpS1557;
    _M0L6_2atmpS1555 = _M0L6_2atmpS1556 & 0xff;
    if (
      _M0L6_2atmpS1554 < 0
      || _M0L6_2atmpS1554 >= Moonbit_array_length(_M0L6resultS619)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS619[_M0L6_2atmpS1554] = _M0L6_2atmpS1555;
    if (_M0L7olengthS624 > 1) {
      int32_t _M0L6_2atmpS1560 = _M0Lm5indexS620;
      int32_t _M0L6_2atmpS1559 = _M0L6_2atmpS1560 + 1;
      if (
        _M0L6_2atmpS1559 < 0
        || _M0L6_2atmpS1559 >= Moonbit_array_length(_M0L6resultS619)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS619[_M0L6_2atmpS1559] = 46;
    } else {
      int32_t _M0L6_2atmpS1561 = _M0Lm5indexS620;
      _M0Lm5indexS620 = _M0L6_2atmpS1561 - 1;
    }
    _M0L6_2atmpS1562 = _M0Lm5indexS620;
    _M0L6_2atmpS1563 = _M0L7olengthS624 + 1;
    _M0Lm5indexS620 = _M0L6_2atmpS1562 + _M0L6_2atmpS1563;
    _M0L6_2atmpS1564 = _M0Lm5indexS620;
    if (
      _M0L6_2atmpS1564 < 0
      || _M0L6_2atmpS1564 >= Moonbit_array_length(_M0L6resultS619)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS619[_M0L6_2atmpS1564] = 101;
    _M0L6_2atmpS1565 = _M0Lm5indexS620;
    _M0Lm5indexS620 = _M0L6_2atmpS1565 + 1;
    _M0L6_2atmpS1566 = _M0Lm3expS625;
    if (_M0L6_2atmpS1566 < 0) {
      int32_t _M0L6_2atmpS1567 = _M0Lm5indexS620;
      int32_t _M0L6_2atmpS1568;
      int32_t _M0L6_2atmpS1569;
      if (
        _M0L6_2atmpS1567 < 0
        || _M0L6_2atmpS1567 >= Moonbit_array_length(_M0L6resultS619)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS619[_M0L6_2atmpS1567] = 45;
      _M0L6_2atmpS1568 = _M0Lm5indexS620;
      _M0Lm5indexS620 = _M0L6_2atmpS1568 + 1;
      _M0L6_2atmpS1569 = _M0Lm3expS625;
      _M0Lm3expS625 = -_M0L6_2atmpS1569;
    } else {
      int32_t _M0L6_2atmpS1570 = _M0Lm5indexS620;
      int32_t _M0L6_2atmpS1571;
      if (
        _M0L6_2atmpS1570 < 0
        || _M0L6_2atmpS1570 >= Moonbit_array_length(_M0L6resultS619)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS619[_M0L6_2atmpS1570] = 43;
      _M0L6_2atmpS1571 = _M0Lm5indexS620;
      _M0Lm5indexS620 = _M0L6_2atmpS1571 + 1;
    }
    _M0L6_2atmpS1572 = _M0Lm3expS625;
    if (_M0L6_2atmpS1572 >= 100) {
      int32_t _M0L6_2atmpS1588 = _M0Lm3expS625;
      int32_t _M0L1aS633 = _M0L6_2atmpS1588 / 100;
      int32_t _M0L6_2atmpS1587 = _M0Lm3expS625;
      int32_t _M0L6_2atmpS1586 = _M0L6_2atmpS1587 / 10;
      int32_t _M0L1bS634 = _M0L6_2atmpS1586 % 10;
      int32_t _M0L6_2atmpS1585 = _M0Lm3expS625;
      int32_t _M0L1cS635 = _M0L6_2atmpS1585 % 10;
      int32_t _M0L6_2atmpS1573 = _M0Lm5indexS620;
      int32_t _M0L6_2atmpS1575 = 48 + _M0L1aS633;
      int32_t _M0L6_2atmpS1574 = _M0L6_2atmpS1575 & 0xff;
      int32_t _M0L6_2atmpS1579;
      int32_t _M0L6_2atmpS1576;
      int32_t _M0L6_2atmpS1578;
      int32_t _M0L6_2atmpS1577;
      int32_t _M0L6_2atmpS1583;
      int32_t _M0L6_2atmpS1580;
      int32_t _M0L6_2atmpS1582;
      int32_t _M0L6_2atmpS1581;
      int32_t _M0L6_2atmpS1584;
      if (
        _M0L6_2atmpS1573 < 0
        || _M0L6_2atmpS1573 >= Moonbit_array_length(_M0L6resultS619)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS619[_M0L6_2atmpS1573] = _M0L6_2atmpS1574;
      _M0L6_2atmpS1579 = _M0Lm5indexS620;
      _M0L6_2atmpS1576 = _M0L6_2atmpS1579 + 1;
      _M0L6_2atmpS1578 = 48 + _M0L1bS634;
      _M0L6_2atmpS1577 = _M0L6_2atmpS1578 & 0xff;
      if (
        _M0L6_2atmpS1576 < 0
        || _M0L6_2atmpS1576 >= Moonbit_array_length(_M0L6resultS619)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS619[_M0L6_2atmpS1576] = _M0L6_2atmpS1577;
      _M0L6_2atmpS1583 = _M0Lm5indexS620;
      _M0L6_2atmpS1580 = _M0L6_2atmpS1583 + 2;
      _M0L6_2atmpS1582 = 48 + _M0L1cS635;
      _M0L6_2atmpS1581 = _M0L6_2atmpS1582 & 0xff;
      if (
        _M0L6_2atmpS1580 < 0
        || _M0L6_2atmpS1580 >= Moonbit_array_length(_M0L6resultS619)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS619[_M0L6_2atmpS1580] = _M0L6_2atmpS1581;
      _M0L6_2atmpS1584 = _M0Lm5indexS620;
      _M0Lm5indexS620 = _M0L6_2atmpS1584 + 3;
    } else {
      int32_t _M0L6_2atmpS1589 = _M0Lm3expS625;
      if (_M0L6_2atmpS1589 >= 10) {
        int32_t _M0L6_2atmpS1599 = _M0Lm3expS625;
        int32_t _M0L1aS636 = _M0L6_2atmpS1599 / 10;
        int32_t _M0L6_2atmpS1598 = _M0Lm3expS625;
        int32_t _M0L1bS637 = _M0L6_2atmpS1598 % 10;
        int32_t _M0L6_2atmpS1590 = _M0Lm5indexS620;
        int32_t _M0L6_2atmpS1592 = 48 + _M0L1aS636;
        int32_t _M0L6_2atmpS1591 = _M0L6_2atmpS1592 & 0xff;
        int32_t _M0L6_2atmpS1596;
        int32_t _M0L6_2atmpS1593;
        int32_t _M0L6_2atmpS1595;
        int32_t _M0L6_2atmpS1594;
        int32_t _M0L6_2atmpS1597;
        if (
          _M0L6_2atmpS1590 < 0
          || _M0L6_2atmpS1590 >= Moonbit_array_length(_M0L6resultS619)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS619[_M0L6_2atmpS1590] = _M0L6_2atmpS1591;
        _M0L6_2atmpS1596 = _M0Lm5indexS620;
        _M0L6_2atmpS1593 = _M0L6_2atmpS1596 + 1;
        _M0L6_2atmpS1595 = 48 + _M0L1bS637;
        _M0L6_2atmpS1594 = _M0L6_2atmpS1595 & 0xff;
        if (
          _M0L6_2atmpS1593 < 0
          || _M0L6_2atmpS1593 >= Moonbit_array_length(_M0L6resultS619)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS619[_M0L6_2atmpS1593] = _M0L6_2atmpS1594;
        _M0L6_2atmpS1597 = _M0Lm5indexS620;
        _M0Lm5indexS620 = _M0L6_2atmpS1597 + 2;
      } else {
        int32_t _M0L6_2atmpS1600 = _M0Lm5indexS620;
        int32_t _M0L6_2atmpS1603 = _M0Lm3expS625;
        int32_t _M0L6_2atmpS1602 = 48 + _M0L6_2atmpS1603;
        int32_t _M0L6_2atmpS1601 = _M0L6_2atmpS1602 & 0xff;
        int32_t _M0L6_2atmpS1604;
        if (
          _M0L6_2atmpS1600 < 0
          || _M0L6_2atmpS1600 >= Moonbit_array_length(_M0L6resultS619)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS619[_M0L6_2atmpS1600] = _M0L6_2atmpS1601;
        _M0L6_2atmpS1604 = _M0Lm5indexS620;
        _M0Lm5indexS620 = _M0L6_2atmpS1604 + 1;
      }
    }
    _M0L6_2atmpS1605 = _M0Lm5indexS620;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1908
    = _M0FPB19string__from__bytes(_M0L6resultS619, 0, _M0L6_2atmpS1605);
    moonbit_decref_cycle_free(_M0L6resultS619);
    return _result_1908;
  } else {
    int32_t _M0L6_2atmpS1614 = _M0Lm3expS625;
    int32_t _M0L6_2atmpS1677;
    moonbit_string_t _result_1914;
    if (_M0L6_2atmpS1614 < 0) {
      int32_t _M0L6_2atmpS1615 = _M0Lm5indexS620;
      int32_t _M0L6_2atmpS1617;
      int32_t _M0L6_2atmpS1616;
      int32_t _M0L6_2atmpS1618;
      int32_t _M0L1iS638;
      int32_t _M0L6_2atmpS1633;
      int32_t _M0L6_2atmpS1635;
      int32_t _M0L6_2atmpS1634;
      int32_t _M0L7currentS640;
      int32_t _M0L1iS641;
      uint64_t _M0L6outputS642;
      if (
        _M0L6_2atmpS1615 < 0
        || _M0L6_2atmpS1615 >= Moonbit_array_length(_M0L6resultS619)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS619[_M0L6_2atmpS1615] = 48;
      _M0L6_2atmpS1617 = _M0Lm5indexS620;
      _M0L6_2atmpS1616 = _M0L6_2atmpS1617 + 1;
      if (
        _M0L6_2atmpS1616 < 0
        || _M0L6_2atmpS1616 >= Moonbit_array_length(_M0L6resultS619)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS619[_M0L6_2atmpS1616] = 46;
      _M0L6_2atmpS1618 = _M0Lm5indexS620;
      _M0Lm5indexS620 = _M0L6_2atmpS1618 + 2;
      _M0L1iS638 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1619 = _M0Lm3expS625;
        if (_M0L1iS638 > _M0L6_2atmpS1619) {
          int32_t _M0L6_2atmpS1622 = _M0Lm5indexS620;
          int32_t _M0L6_2atmpS1621 = _M0L6_2atmpS1622 - _M0L1iS638;
          int32_t _M0L6_2atmpS1620 = _M0L6_2atmpS1621 - 1;
          int32_t _M0L6_2atmpS1623;
          if (
            _M0L6_2atmpS1620 < 0
            || _M0L6_2atmpS1620 >= Moonbit_array_length(_M0L6resultS619)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS619[_M0L6_2atmpS1620] = 48;
          _M0L6_2atmpS1623 = _M0L1iS638 - 1;
          _M0L1iS638 = _M0L6_2atmpS1623;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1633 = _M0Lm5indexS620;
      _M0L6_2atmpS1635 = _M0Lm3expS625;
      _M0L6_2atmpS1634 = -1 - _M0L6_2atmpS1635;
      _M0L7currentS640 = _M0L6_2atmpS1633 + _M0L6_2atmpS1634;
      _M0L1iS641 = 0;
      _M0L6outputS642 = _M0L6outputS622;
      while (1) {
        if (_M0L1iS641 < _M0L7olengthS624) {
          int32_t _M0L6_2atmpS1630 = _M0L7currentS640 + _M0L7olengthS624;
          int32_t _M0L6_2atmpS1629 = _M0L6_2atmpS1630 - _M0L1iS641;
          int32_t _M0L6_2atmpS1624 = _M0L6_2atmpS1629 - 1;
          uint64_t _M0L6_2atmpS1628 = _M0L6outputS642 % 10ull;
          int32_t _M0L6_2atmpS1627 = (int32_t)_M0L6_2atmpS1628;
          int32_t _M0L6_2atmpS1626 = 48 + _M0L6_2atmpS1627;
          int32_t _M0L6_2atmpS1625 = _M0L6_2atmpS1626 & 0xff;
          int32_t _M0L6_2atmpS1631;
          uint64_t _M0L6_2atmpS1632;
          if (
            _M0L6_2atmpS1624 < 0
            || _M0L6_2atmpS1624 >= Moonbit_array_length(_M0L6resultS619)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS619[_M0L6_2atmpS1624] = _M0L6_2atmpS1625;
          _M0L6_2atmpS1631 = _M0L1iS641 + 1;
          _M0L6_2atmpS1632 = _M0L6outputS642 / 10ull;
          _M0L1iS641 = _M0L6_2atmpS1631;
          _M0L6outputS642 = _M0L6_2atmpS1632;
          continue;
        }
        break;
      }
      _M0Lm5indexS620 = _M0L7currentS640 + _M0L7olengthS624;
    } else {
      int32_t _M0L6_2atmpS1637 = _M0Lm3expS625;
      int32_t _M0L6_2atmpS1636 = _M0L6_2atmpS1637 + 1;
      if (_M0L6_2atmpS1636 >= _M0L7olengthS624) {
        int32_t _M0L1iS644 = 0;
        uint64_t _M0L6outputS645 = _M0L6outputS622;
        int32_t _M0L6_2atmpS1648;
        int32_t _M0L6_2atmpS1653;
        int32_t _M0L7_2abindS647;
        int32_t _M0L1iS648;
        int32_t _M0L6_2atmpS1654;
        int32_t _M0L6_2atmpS1657;
        int32_t _M0L6_2atmpS1656;
        int32_t _M0L6_2atmpS1655;
        while (1) {
          if (_M0L1iS644 < _M0L7olengthS624) {
            int32_t _M0L6_2atmpS1645 = _M0Lm5indexS620;
            int32_t _M0L6_2atmpS1644 = _M0L6_2atmpS1645 + _M0L7olengthS624;
            int32_t _M0L6_2atmpS1643 = _M0L6_2atmpS1644 - _M0L1iS644;
            int32_t _M0L6_2atmpS1638 = _M0L6_2atmpS1643 - 1;
            uint64_t _M0L6_2atmpS1642 = _M0L6outputS645 % 10ull;
            int32_t _M0L6_2atmpS1641 = (int32_t)_M0L6_2atmpS1642;
            int32_t _M0L6_2atmpS1640 = 48 + _M0L6_2atmpS1641;
            int32_t _M0L6_2atmpS1639 = _M0L6_2atmpS1640 & 0xff;
            int32_t _M0L6_2atmpS1646;
            uint64_t _M0L6_2atmpS1647;
            if (
              _M0L6_2atmpS1638 < 0
              || _M0L6_2atmpS1638 >= Moonbit_array_length(_M0L6resultS619)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS619[_M0L6_2atmpS1638] = _M0L6_2atmpS1639;
            _M0L6_2atmpS1646 = _M0L1iS644 + 1;
            _M0L6_2atmpS1647 = _M0L6outputS645 / 10ull;
            _M0L1iS644 = _M0L6_2atmpS1646;
            _M0L6outputS645 = _M0L6_2atmpS1647;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1648 = _M0Lm5indexS620;
        _M0Lm5indexS620 = _M0L6_2atmpS1648 + _M0L7olengthS624;
        _M0L6_2atmpS1653 = _M0Lm3expS625;
        _M0L7_2abindS647 = _M0L6_2atmpS1653 + 1;
        _M0L1iS648 = _M0L7olengthS624;
        while (1) {
          if (_M0L1iS648 < _M0L7_2abindS647) {
            int32_t _M0L6_2atmpS1651 = _M0Lm5indexS620;
            int32_t _M0L6_2atmpS1650 = _M0L6_2atmpS1651 + _M0L1iS648;
            int32_t _M0L6_2atmpS1649 = _M0L6_2atmpS1650 - _M0L7olengthS624;
            int32_t _M0L6_2atmpS1652;
            if (
              _M0L6_2atmpS1649 < 0
              || _M0L6_2atmpS1649 >= Moonbit_array_length(_M0L6resultS619)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS619[_M0L6_2atmpS1649] = 48;
            _M0L6_2atmpS1652 = _M0L1iS648 + 1;
            _M0L1iS648 = _M0L6_2atmpS1652;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1654 = _M0Lm5indexS620;
        _M0L6_2atmpS1657 = _M0Lm3expS625;
        _M0L6_2atmpS1656 = _M0L6_2atmpS1657 + 1;
        _M0L6_2atmpS1655 = _M0L6_2atmpS1656 - _M0L7olengthS624;
        _M0Lm5indexS620 = _M0L6_2atmpS1654 + _M0L6_2atmpS1655;
      } else {
        int32_t _M0L6_2atmpS1674 = _M0Lm5indexS620;
        int32_t _M0L6_2atmpS1673 = _M0L6_2atmpS1674 + 1;
        int32_t _M0L1iS650 = 0;
        int32_t _M0L7currentS651 = _M0L6_2atmpS1673;
        uint64_t _M0L6outputS652 = _M0L6outputS622;
        int32_t _M0L6_2atmpS1675;
        int32_t _M0L6_2atmpS1676;
        while (1) {
          if (_M0L1iS650 < _M0L7olengthS624) {
            int32_t _M0L6_2atmpS1669 = _M0L7olengthS624 - _M0L1iS650;
            int32_t _M0L6_2atmpS1667 = _M0L6_2atmpS1669 - 1;
            int32_t _M0L6_2atmpS1668 = _M0Lm3expS625;
            int32_t _M0L7currentS653;
            int32_t _M0L6_2atmpS1664;
            int32_t _M0L6_2atmpS1663;
            int32_t _M0L6_2atmpS1658;
            uint64_t _M0L6_2atmpS1662;
            int32_t _M0L6_2atmpS1661;
            int32_t _M0L6_2atmpS1660;
            int32_t _M0L6_2atmpS1659;
            int32_t _M0L6_2atmpS1665;
            uint64_t _M0L6_2atmpS1666;
            if (_M0L6_2atmpS1667 == _M0L6_2atmpS1668) {
              int32_t _M0L6_2atmpS1672 = _M0L7currentS651 + _M0L7olengthS624;
              int32_t _M0L6_2atmpS1671 = _M0L6_2atmpS1672 - _M0L1iS650;
              int32_t _M0L6_2atmpS1670 = _M0L6_2atmpS1671 - 1;
              if (
                _M0L6_2atmpS1670 < 0
                || _M0L6_2atmpS1670 >= Moonbit_array_length(_M0L6resultS619)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS619[_M0L6_2atmpS1670] = 46;
              _M0L7currentS653 = _M0L7currentS651 - 1;
            } else {
              _M0L7currentS653 = _M0L7currentS651;
            }
            _M0L6_2atmpS1664 = _M0L7currentS653 + _M0L7olengthS624;
            _M0L6_2atmpS1663 = _M0L6_2atmpS1664 - _M0L1iS650;
            _M0L6_2atmpS1658 = _M0L6_2atmpS1663 - 1;
            _M0L6_2atmpS1662 = _M0L6outputS652 % 10ull;
            _M0L6_2atmpS1661 = (int32_t)_M0L6_2atmpS1662;
            _M0L6_2atmpS1660 = 48 + _M0L6_2atmpS1661;
            _M0L6_2atmpS1659 = _M0L6_2atmpS1660 & 0xff;
            if (
              _M0L6_2atmpS1658 < 0
              || _M0L6_2atmpS1658 >= Moonbit_array_length(_M0L6resultS619)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS619[_M0L6_2atmpS1658] = _M0L6_2atmpS1659;
            _M0L6_2atmpS1665 = _M0L1iS650 + 1;
            _M0L6_2atmpS1666 = _M0L6outputS652 / 10ull;
            _M0L1iS650 = _M0L6_2atmpS1665;
            _M0L7currentS651 = _M0L7currentS653;
            _M0L6outputS652 = _M0L6_2atmpS1666;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1675 = _M0Lm5indexS620;
        _M0L6_2atmpS1676 = _M0L7olengthS624 + 1;
        _M0Lm5indexS620 = _M0L6_2atmpS1675 + _M0L6_2atmpS1676;
      }
    }
    _M0L6_2atmpS1677 = _M0Lm5indexS620;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1914
    = _M0FPB19string__from__bytes(_M0L6resultS619, 0, _M0L6_2atmpS1677);
    moonbit_decref_cycle_free(_M0L6resultS619);
    return _result_1914;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS565,
  uint32_t _M0L12ieeeExponentS564
) {
  int32_t _M0Lm2e2S562;
  uint64_t _M0Lm2m2S563;
  uint64_t _M0L6_2atmpS1551;
  uint64_t _M0L6_2atmpS1550;
  int32_t _M0L4evenS566;
  uint64_t _M0L6_2atmpS1549;
  uint64_t _M0L2mvS567;
  int32_t _M0L7mmShiftS568;
  uint64_t _M0Lm2vrS569;
  uint64_t _M0Lm2vpS570;
  uint64_t _M0Lm2vmS571;
  int32_t _M0Lm3e10S572;
  int32_t _M0Lm17vmIsTrailingZerosS573;
  int32_t _M0Lm17vrIsTrailingZerosS574;
  int32_t _M0L6_2atmpS1451;
  int32_t _M0Lm7removedS593;
  int32_t _M0Lm16lastRemovedDigitS594;
  uint64_t _M0Lm6outputS595;
  int32_t _M0L6_2atmpS1547;
  int32_t _M0L6_2atmpS1548;
  int32_t _M0L3expS618;
  uint64_t _M0L6_2atmpS1546;
  struct _M0TPB17FloatingDecimal64* _block_1920;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S562 = 0;
  _M0Lm2m2S563 = 0ull;
  if (_M0L12ieeeExponentS564 == 0u) {
    _M0Lm2e2S562 = -1076;
    _M0Lm2m2S563 = _M0L12ieeeMantissaS565;
  } else {
    int32_t _M0L6_2atmpS1450 = *(int32_t*)&_M0L12ieeeExponentS564;
    int32_t _M0L6_2atmpS1449 = _M0L6_2atmpS1450 - 1023;
    int32_t _M0L6_2atmpS1448 = _M0L6_2atmpS1449 - 52;
    _M0Lm2e2S562 = _M0L6_2atmpS1448 - 2;
    _M0Lm2m2S563 = 4503599627370496ull | _M0L12ieeeMantissaS565;
  }
  _M0L6_2atmpS1551 = _M0Lm2m2S563;
  _M0L6_2atmpS1550 = _M0L6_2atmpS1551 & 1ull;
  _M0L4evenS566 = _M0L6_2atmpS1550 == 0ull;
  _M0L6_2atmpS1549 = _M0Lm2m2S563;
  _M0L2mvS567 = 4ull * _M0L6_2atmpS1549;
  _M0L7mmShiftS568
  = _M0L12ieeeMantissaS565 != 0ull || _M0L12ieeeExponentS564 <= 1u;
  _M0Lm2vrS569 = 0ull;
  _M0Lm2vpS570 = 0ull;
  _M0Lm2vmS571 = 0ull;
  _M0Lm3e10S572 = 0;
  _M0Lm17vmIsTrailingZerosS573 = 0;
  _M0Lm17vrIsTrailingZerosS574 = 0;
  _M0L6_2atmpS1451 = _M0Lm2e2S562;
  if (_M0L6_2atmpS1451 >= 0) {
    int32_t _M0L6_2atmpS1473 = _M0Lm2e2S562;
    int32_t _M0L6_2atmpS1469;
    int32_t _M0L6_2atmpS1472;
    int32_t _M0L6_2atmpS1471;
    int32_t _M0L6_2atmpS1470;
    int32_t _M0L1qS575;
    int32_t _M0L6_2atmpS1468;
    int32_t _M0L6_2atmpS1467;
    int32_t _M0L1kS576;
    int32_t _M0L6_2atmpS1466;
    int32_t _M0L6_2atmpS1465;
    int32_t _M0L6_2atmpS1464;
    int32_t _M0L1iS577;
    struct _M0TPB8Pow5Pair _M0L4pow5S578;
    uint64_t _M0L6_2atmpS1463;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS579;
    uint64_t _M0L8_2avrOutS580;
    uint64_t _M0L8_2avpOutS581;
    uint64_t _M0L8_2avmOutS582;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1469 = _M0FPB9log10Pow2(_M0L6_2atmpS1473);
    _M0L6_2atmpS1472 = _M0Lm2e2S562;
    _M0L6_2atmpS1471 = _M0L6_2atmpS1472 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1470 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1471);
    _M0L1qS575 = _M0L6_2atmpS1469 - _M0L6_2atmpS1470;
    _M0Lm3e10S572 = _M0L1qS575;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1468 = _M0FPB8pow5bits(_M0L1qS575);
    _M0L6_2atmpS1467 = 125 + _M0L6_2atmpS1468;
    _M0L1kS576 = _M0L6_2atmpS1467 - 1;
    _M0L6_2atmpS1466 = _M0Lm2e2S562;
    _M0L6_2atmpS1465 = -_M0L6_2atmpS1466;
    _M0L6_2atmpS1464 = _M0L6_2atmpS1465 + _M0L1qS575;
    _M0L1iS577 = _M0L6_2atmpS1464 + _M0L1kS576;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S578 = _M0FPB22double__computeInvPow5(_M0L1qS575);
    _M0L6_2atmpS1463 = _M0Lm2m2S563;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS579
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1463, _M0L4pow5S578, _M0L1iS577, _M0L7mmShiftS568);
    _M0L8_2avrOutS580 = _M0L7_2abindS579.$0;
    _M0L8_2avpOutS581 = _M0L7_2abindS579.$1;
    _M0L8_2avmOutS582 = _M0L7_2abindS579.$2;
    _M0Lm2vrS569 = _M0L8_2avrOutS580;
    _M0Lm2vpS570 = _M0L8_2avpOutS581;
    _M0Lm2vmS571 = _M0L8_2avmOutS582;
    if (_M0L1qS575 <= 21) {
      int32_t _M0L6_2atmpS1459 = (int32_t)_M0L2mvS567;
      uint64_t _M0L6_2atmpS1462 = _M0L2mvS567 / 5ull;
      int32_t _M0L6_2atmpS1461 = (int32_t)_M0L6_2atmpS1462;
      int32_t _M0L6_2atmpS1460 = 5 * _M0L6_2atmpS1461;
      int32_t _M0L6mvMod5S583 = _M0L6_2atmpS1459 - _M0L6_2atmpS1460;
      if (_M0L6mvMod5S583 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS574
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS567, _M0L1qS575);
      } else if (_M0L4evenS566) {
        uint64_t _M0L6_2atmpS1453 = _M0L2mvS567 - 1ull;
        uint64_t _M0L6_2atmpS1454;
        uint64_t _M0L6_2atmpS1452;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1454 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS568);
        _M0L6_2atmpS1452 = _M0L6_2atmpS1453 - _M0L6_2atmpS1454;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS573
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1452, _M0L1qS575);
      } else {
        uint64_t _M0L6_2atmpS1455 = _M0Lm2vpS570;
        uint64_t _M0L6_2atmpS1458 = _M0L2mvS567 + 2ull;
        int32_t _M0L6_2atmpS1457;
        uint64_t _M0L6_2atmpS1456;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1457
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1458, _M0L1qS575);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1456 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1457);
        _M0Lm2vpS570 = _M0L6_2atmpS1455 - _M0L6_2atmpS1456;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1487 = _M0Lm2e2S562;
    int32_t _M0L6_2atmpS1486 = -_M0L6_2atmpS1487;
    int32_t _M0L6_2atmpS1481;
    int32_t _M0L6_2atmpS1485;
    int32_t _M0L6_2atmpS1484;
    int32_t _M0L6_2atmpS1483;
    int32_t _M0L6_2atmpS1482;
    int32_t _M0L1qS584;
    int32_t _M0L6_2atmpS1474;
    int32_t _M0L6_2atmpS1480;
    int32_t _M0L6_2atmpS1479;
    int32_t _M0L1iS585;
    int32_t _M0L6_2atmpS1478;
    int32_t _M0L1kS586;
    int32_t _M0L1jS587;
    struct _M0TPB8Pow5Pair _M0L4pow5S588;
    uint64_t _M0L6_2atmpS1477;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS589;
    uint64_t _M0L8_2avrOutS590;
    uint64_t _M0L8_2avpOutS591;
    uint64_t _M0L8_2avmOutS592;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1481 = _M0FPB9log10Pow5(_M0L6_2atmpS1486);
    _M0L6_2atmpS1485 = _M0Lm2e2S562;
    _M0L6_2atmpS1484 = -_M0L6_2atmpS1485;
    _M0L6_2atmpS1483 = _M0L6_2atmpS1484 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1482 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1483);
    _M0L1qS584 = _M0L6_2atmpS1481 - _M0L6_2atmpS1482;
    _M0L6_2atmpS1474 = _M0Lm2e2S562;
    _M0Lm3e10S572 = _M0L1qS584 + _M0L6_2atmpS1474;
    _M0L6_2atmpS1480 = _M0Lm2e2S562;
    _M0L6_2atmpS1479 = -_M0L6_2atmpS1480;
    _M0L1iS585 = _M0L6_2atmpS1479 - _M0L1qS584;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1478 = _M0FPB8pow5bits(_M0L1iS585);
    _M0L1kS586 = _M0L6_2atmpS1478 - 125;
    _M0L1jS587 = _M0L1qS584 - _M0L1kS586;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S588 = _M0FPB19double__computePow5(_M0L1iS585);
    _M0L6_2atmpS1477 = _M0Lm2m2S563;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS589
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1477, _M0L4pow5S588, _M0L1jS587, _M0L7mmShiftS568);
    _M0L8_2avrOutS590 = _M0L7_2abindS589.$0;
    _M0L8_2avpOutS591 = _M0L7_2abindS589.$1;
    _M0L8_2avmOutS592 = _M0L7_2abindS589.$2;
    _M0Lm2vrS569 = _M0L8_2avrOutS590;
    _M0Lm2vpS570 = _M0L8_2avpOutS591;
    _M0Lm2vmS571 = _M0L8_2avmOutS592;
    if (_M0L1qS584 <= 1) {
      _M0Lm17vrIsTrailingZerosS574 = 1;
      if (_M0L4evenS566) {
        int32_t _M0L6_2atmpS1475;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1475 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS568);
        _M0Lm17vmIsTrailingZerosS573 = _M0L6_2atmpS1475 == 1;
      } else {
        uint64_t _M0L6_2atmpS1476 = _M0Lm2vpS570;
        _M0Lm2vpS570 = _M0L6_2atmpS1476 - 1ull;
      }
    } else if (_M0L1qS584 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS574
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS567, _M0L1qS584);
    }
  }
  _M0Lm7removedS593 = 0;
  _M0Lm16lastRemovedDigitS594 = 0;
  _M0Lm6outputS595 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS573 || _M0Lm17vrIsTrailingZerosS574) {
    int32_t _if__result_1917;
    uint64_t _M0L6_2atmpS1517;
    uint64_t _M0L6_2atmpS1523;
    uint64_t _M0L6_2atmpS1524;
    int32_t _if__result_1918;
    int32_t _M0L6_2atmpS1520;
    int64_t _M0L6_2atmpS1519;
    uint64_t _M0L6_2atmpS1518;
    while (1) {
      uint64_t _M0L6_2atmpS1500 = _M0Lm2vpS570;
      uint64_t _M0L7vpDiv10S596 = _M0L6_2atmpS1500 / 10ull;
      uint64_t _M0L6_2atmpS1499 = _M0Lm2vmS571;
      uint64_t _M0L7vmDiv10S597 = _M0L6_2atmpS1499 / 10ull;
      uint64_t _M0L6_2atmpS1498;
      int32_t _M0L6_2atmpS1495;
      int32_t _M0L6_2atmpS1497;
      int32_t _M0L6_2atmpS1496;
      int32_t _M0L7vmMod10S599;
      uint64_t _M0L6_2atmpS1494;
      uint64_t _M0L7vrDiv10S600;
      uint64_t _M0L6_2atmpS1493;
      int32_t _M0L6_2atmpS1490;
      int32_t _M0L6_2atmpS1492;
      int32_t _M0L6_2atmpS1491;
      int32_t _M0L7vrMod10S601;
      int32_t _M0L6_2atmpS1489;
      if (_M0L7vpDiv10S596 <= _M0L7vmDiv10S597) {
        break;
      }
      _M0L6_2atmpS1498 = _M0Lm2vmS571;
      _M0L6_2atmpS1495 = (int32_t)_M0L6_2atmpS1498;
      _M0L6_2atmpS1497 = (int32_t)_M0L7vmDiv10S597;
      _M0L6_2atmpS1496 = 10 * _M0L6_2atmpS1497;
      _M0L7vmMod10S599 = _M0L6_2atmpS1495 - _M0L6_2atmpS1496;
      _M0L6_2atmpS1494 = _M0Lm2vrS569;
      _M0L7vrDiv10S600 = _M0L6_2atmpS1494 / 10ull;
      _M0L6_2atmpS1493 = _M0Lm2vrS569;
      _M0L6_2atmpS1490 = (int32_t)_M0L6_2atmpS1493;
      _M0L6_2atmpS1492 = (int32_t)_M0L7vrDiv10S600;
      _M0L6_2atmpS1491 = 10 * _M0L6_2atmpS1492;
      _M0L7vrMod10S601 = _M0L6_2atmpS1490 - _M0L6_2atmpS1491;
      _M0Lm17vmIsTrailingZerosS573
      = _M0Lm17vmIsTrailingZerosS573 && _M0L7vmMod10S599 == 0;
      if (_M0Lm17vrIsTrailingZerosS574) {
        int32_t _M0L6_2atmpS1488 = _M0Lm16lastRemovedDigitS594;
        _M0Lm17vrIsTrailingZerosS574 = _M0L6_2atmpS1488 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS574 = 0;
      }
      _M0Lm16lastRemovedDigitS594 = _M0L7vrMod10S601;
      _M0Lm2vrS569 = _M0L7vrDiv10S600;
      _M0Lm2vpS570 = _M0L7vpDiv10S596;
      _M0Lm2vmS571 = _M0L7vmDiv10S597;
      _M0L6_2atmpS1489 = _M0Lm7removedS593;
      _M0Lm7removedS593 = _M0L6_2atmpS1489 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS573) {
      while (1) {
        uint64_t _M0L6_2atmpS1513 = _M0Lm2vmS571;
        uint64_t _M0L7vmDiv10S602 = _M0L6_2atmpS1513 / 10ull;
        uint64_t _M0L6_2atmpS1512 = _M0Lm2vmS571;
        int32_t _M0L6_2atmpS1509 = (int32_t)_M0L6_2atmpS1512;
        int32_t _M0L6_2atmpS1511 = (int32_t)_M0L7vmDiv10S602;
        int32_t _M0L6_2atmpS1510 = 10 * _M0L6_2atmpS1511;
        int32_t _M0L7vmMod10S603 = _M0L6_2atmpS1509 - _M0L6_2atmpS1510;
        uint64_t _M0L6_2atmpS1508;
        uint64_t _M0L7vpDiv10S605;
        uint64_t _M0L6_2atmpS1507;
        uint64_t _M0L7vrDiv10S606;
        uint64_t _M0L6_2atmpS1506;
        int32_t _M0L6_2atmpS1503;
        int32_t _M0L6_2atmpS1505;
        int32_t _M0L6_2atmpS1504;
        int32_t _M0L7vrMod10S607;
        int32_t _M0L6_2atmpS1502;
        if (_M0L7vmMod10S603 != 0) {
          break;
        }
        _M0L6_2atmpS1508 = _M0Lm2vpS570;
        _M0L7vpDiv10S605 = _M0L6_2atmpS1508 / 10ull;
        _M0L6_2atmpS1507 = _M0Lm2vrS569;
        _M0L7vrDiv10S606 = _M0L6_2atmpS1507 / 10ull;
        _M0L6_2atmpS1506 = _M0Lm2vrS569;
        _M0L6_2atmpS1503 = (int32_t)_M0L6_2atmpS1506;
        _M0L6_2atmpS1505 = (int32_t)_M0L7vrDiv10S606;
        _M0L6_2atmpS1504 = 10 * _M0L6_2atmpS1505;
        _M0L7vrMod10S607 = _M0L6_2atmpS1503 - _M0L6_2atmpS1504;
        if (_M0Lm17vrIsTrailingZerosS574) {
          int32_t _M0L6_2atmpS1501 = _M0Lm16lastRemovedDigitS594;
          _M0Lm17vrIsTrailingZerosS574 = _M0L6_2atmpS1501 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS574 = 0;
        }
        _M0Lm16lastRemovedDigitS594 = _M0L7vrMod10S607;
        _M0Lm2vrS569 = _M0L7vrDiv10S606;
        _M0Lm2vpS570 = _M0L7vpDiv10S605;
        _M0Lm2vmS571 = _M0L7vmDiv10S602;
        _M0L6_2atmpS1502 = _M0Lm7removedS593;
        _M0Lm7removedS593 = _M0L6_2atmpS1502 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS574) {
      int32_t _M0L6_2atmpS1516 = _M0Lm16lastRemovedDigitS594;
      if (_M0L6_2atmpS1516 == 5) {
        uint64_t _M0L6_2atmpS1515 = _M0Lm2vrS569;
        uint64_t _M0L6_2atmpS1514 = _M0L6_2atmpS1515 % 2ull;
        _if__result_1917 = _M0L6_2atmpS1514 == 0ull;
      } else {
        _if__result_1917 = 0;
      }
    } else {
      _if__result_1917 = 0;
    }
    if (_if__result_1917) {
      _M0Lm16lastRemovedDigitS594 = 4;
    }
    _M0L6_2atmpS1517 = _M0Lm2vrS569;
    _M0L6_2atmpS1523 = _M0Lm2vrS569;
    _M0L6_2atmpS1524 = _M0Lm2vmS571;
    if (_M0L6_2atmpS1523 == _M0L6_2atmpS1524) {
      if (!_M0L4evenS566) {
        _if__result_1918 = 1;
      } else {
        int32_t _M0L6_2atmpS1522 = _M0Lm17vmIsTrailingZerosS573;
        _if__result_1918 = !_M0L6_2atmpS1522;
      }
    } else {
      _if__result_1918 = 0;
    }
    if (_if__result_1918) {
      _M0L6_2atmpS1520 = 1;
    } else {
      int32_t _M0L6_2atmpS1521 = _M0Lm16lastRemovedDigitS594;
      _M0L6_2atmpS1520 = _M0L6_2atmpS1521 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1519 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1520);
    _M0L6_2atmpS1518 = *(uint64_t*)&_M0L6_2atmpS1519;
    _M0Lm6outputS595 = _M0L6_2atmpS1517 + _M0L6_2atmpS1518;
  } else {
    int32_t _M0Lm7roundUpS608 = 0;
    uint64_t _M0L6_2atmpS1545 = _M0Lm2vpS570;
    uint64_t _M0L8vpDiv100S609 = _M0L6_2atmpS1545 / 100ull;
    uint64_t _M0L6_2atmpS1544 = _M0Lm2vmS571;
    uint64_t _M0L8vmDiv100S610 = _M0L6_2atmpS1544 / 100ull;
    uint64_t _M0L6_2atmpS1539;
    uint64_t _M0L6_2atmpS1542;
    uint64_t _M0L6_2atmpS1543;
    int32_t _M0L6_2atmpS1541;
    uint64_t _M0L6_2atmpS1540;
    if (_M0L8vpDiv100S609 > _M0L8vmDiv100S610) {
      uint64_t _M0L6_2atmpS1530 = _M0Lm2vrS569;
      uint64_t _M0L8vrDiv100S611 = _M0L6_2atmpS1530 / 100ull;
      uint64_t _M0L6_2atmpS1529 = _M0Lm2vrS569;
      int32_t _M0L6_2atmpS1526 = (int32_t)_M0L6_2atmpS1529;
      int32_t _M0L6_2atmpS1528 = (int32_t)_M0L8vrDiv100S611;
      int32_t _M0L6_2atmpS1527 = 100 * _M0L6_2atmpS1528;
      int32_t _M0L8vrMod100S612 = _M0L6_2atmpS1526 - _M0L6_2atmpS1527;
      int32_t _M0L6_2atmpS1525;
      _M0Lm7roundUpS608 = _M0L8vrMod100S612 >= 50;
      _M0Lm2vrS569 = _M0L8vrDiv100S611;
      _M0Lm2vpS570 = _M0L8vpDiv100S609;
      _M0Lm2vmS571 = _M0L8vmDiv100S610;
      _M0L6_2atmpS1525 = _M0Lm7removedS593;
      _M0Lm7removedS593 = _M0L6_2atmpS1525 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1538 = _M0Lm2vpS570;
      uint64_t _M0L7vpDiv10S613 = _M0L6_2atmpS1538 / 10ull;
      uint64_t _M0L6_2atmpS1537 = _M0Lm2vmS571;
      uint64_t _M0L7vmDiv10S614 = _M0L6_2atmpS1537 / 10ull;
      uint64_t _M0L6_2atmpS1536;
      uint64_t _M0L7vrDiv10S616;
      uint64_t _M0L6_2atmpS1535;
      int32_t _M0L6_2atmpS1532;
      int32_t _M0L6_2atmpS1534;
      int32_t _M0L6_2atmpS1533;
      int32_t _M0L7vrMod10S617;
      int32_t _M0L6_2atmpS1531;
      if (_M0L7vpDiv10S613 <= _M0L7vmDiv10S614) {
        break;
      }
      _M0L6_2atmpS1536 = _M0Lm2vrS569;
      _M0L7vrDiv10S616 = _M0L6_2atmpS1536 / 10ull;
      _M0L6_2atmpS1535 = _M0Lm2vrS569;
      _M0L6_2atmpS1532 = (int32_t)_M0L6_2atmpS1535;
      _M0L6_2atmpS1534 = (int32_t)_M0L7vrDiv10S616;
      _M0L6_2atmpS1533 = 10 * _M0L6_2atmpS1534;
      _M0L7vrMod10S617 = _M0L6_2atmpS1532 - _M0L6_2atmpS1533;
      _M0Lm7roundUpS608 = _M0L7vrMod10S617 >= 5;
      _M0Lm2vrS569 = _M0L7vrDiv10S616;
      _M0Lm2vpS570 = _M0L7vpDiv10S613;
      _M0Lm2vmS571 = _M0L7vmDiv10S614;
      _M0L6_2atmpS1531 = _M0Lm7removedS593;
      _M0Lm7removedS593 = _M0L6_2atmpS1531 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1539 = _M0Lm2vrS569;
    _M0L6_2atmpS1542 = _M0Lm2vrS569;
    _M0L6_2atmpS1543 = _M0Lm2vmS571;
    _M0L6_2atmpS1541
    = _M0L6_2atmpS1542 == _M0L6_2atmpS1543 || _M0Lm7roundUpS608;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1540 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1541);
    _M0Lm6outputS595 = _M0L6_2atmpS1539 + _M0L6_2atmpS1540;
  }
  _M0L6_2atmpS1547 = _M0Lm3e10S572;
  _M0L6_2atmpS1548 = _M0Lm7removedS593;
  _M0L3expS618 = _M0L6_2atmpS1547 + _M0L6_2atmpS1548;
  _M0L6_2atmpS1546 = _M0Lm6outputS595;
  _block_1920
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1920)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1920->$0 = _M0L6_2atmpS1546;
  _block_1920->$1 = _M0L3expS618;
  return _block_1920;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS561) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS561) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS560) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS560) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS559) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS559) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS558) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS558 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS558 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS558 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS558 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS558 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS558 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS558 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS558 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS558 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS558 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS558 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS558 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS558 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS558 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS558 >= 100ull) {
    return 3;
  }
  if (_M0L1vS558 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS541) {
  int32_t _M0L6_2atmpS1447;
  int32_t _M0L6_2atmpS1446;
  int32_t _M0L4baseS540;
  int32_t _M0L5base2S542;
  int32_t _M0L6offsetS543;
  int32_t _M0L6_2atmpS1445;
  uint64_t _M0L4mul0S544;
  int32_t _M0L6_2atmpS1444;
  int32_t _M0L6_2atmpS1443;
  uint64_t _M0L4mul1S545;
  uint64_t _M0L1mS546;
  struct _M0TPB7Umul128 _M0L7_2abindS547;
  uint64_t _M0L7_2alow1S548;
  uint64_t _M0L8_2ahigh1S549;
  struct _M0TPB7Umul128 _M0L7_2abindS550;
  uint64_t _M0L7_2alow0S551;
  uint64_t _M0L8_2ahigh0S552;
  uint64_t _M0L3sumS553;
  uint64_t _M0Lm5high1S554;
  int32_t _M0L6_2atmpS1441;
  int32_t _M0L6_2atmpS1442;
  int32_t _M0L5deltaS555;
  uint64_t _M0L6_2atmpS1440;
  uint64_t _M0L6_2atmpS1432;
  int32_t _M0L6_2atmpS1439;
  uint32_t _M0L6_2atmpS1436;
  int32_t _M0L6_2atmpS1438;
  int32_t _M0L6_2atmpS1437;
  uint32_t _M0L6_2atmpS1435;
  uint32_t _M0L6_2atmpS1434;
  uint64_t _M0L6_2atmpS1433;
  uint64_t _M0L1aS556;
  uint64_t _M0L6_2atmpS1431;
  uint64_t _M0L1bS557;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1447 = _M0L1iS541 + 26;
  _M0L6_2atmpS1446 = _M0L6_2atmpS1447 - 1;
  _M0L4baseS540 = _M0L6_2atmpS1446 / 26;
  _M0L5base2S542 = _M0L4baseS540 * 26;
  _M0L6offsetS543 = _M0L5base2S542 - _M0L1iS541;
  _M0L6_2atmpS1445 = _M0L4baseS540 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S544
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1445);
  _M0L6_2atmpS1444 = _M0L4baseS540 * 2;
  _M0L6_2atmpS1443 = _M0L6_2atmpS1444 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S545
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1443);
  if (_M0L6offsetS543 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S544, .$1 = _M0L4mul1S545};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS546
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS543);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS547 = _M0FPB7umul128(_M0L1mS546, _M0L4mul1S545);
  _M0L7_2alow1S548 = _M0L7_2abindS547.$0;
  _M0L8_2ahigh1S549 = _M0L7_2abindS547.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS550 = _M0FPB7umul128(_M0L1mS546, _M0L4mul0S544);
  _M0L7_2alow0S551 = _M0L7_2abindS550.$0;
  _M0L8_2ahigh0S552 = _M0L7_2abindS550.$1;
  _M0L3sumS553 = _M0L8_2ahigh0S552 + _M0L7_2alow1S548;
  _M0Lm5high1S554 = _M0L8_2ahigh1S549;
  if (_M0L3sumS553 < _M0L8_2ahigh0S552) {
    uint64_t _M0L6_2atmpS1430 = _M0Lm5high1S554;
    _M0Lm5high1S554 = _M0L6_2atmpS1430 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1441 = _M0FPB8pow5bits(_M0L5base2S542);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1442 = _M0FPB8pow5bits(_M0L1iS541);
  _M0L5deltaS555 = _M0L6_2atmpS1441 - _M0L6_2atmpS1442;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1440
  = _M0FPB13shiftright128(_M0L7_2alow0S551, _M0L3sumS553, _M0L5deltaS555);
  _M0L6_2atmpS1432 = _M0L6_2atmpS1440 + 1ull;
  _M0L6_2atmpS1439 = _M0L1iS541 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1436
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1439);
  _M0L6_2atmpS1438 = _M0L1iS541 % 16;
  _M0L6_2atmpS1437 = _M0L6_2atmpS1438 << 1;
  _M0L6_2atmpS1435 = _M0L6_2atmpS1436 >> (_M0L6_2atmpS1437 & 31);
  _M0L6_2atmpS1434 = _M0L6_2atmpS1435 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1433 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1434);
  _M0L1aS556 = _M0L6_2atmpS1432 + _M0L6_2atmpS1433;
  _M0L6_2atmpS1431 = _M0Lm5high1S554;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS557
  = _M0FPB13shiftright128(_M0L3sumS553, _M0L6_2atmpS1431, _M0L5deltaS555);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS556, .$1 = _M0L1bS557};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS523) {
  int32_t _M0L4baseS522;
  int32_t _M0L5base2S524;
  int32_t _M0L6offsetS525;
  int32_t _M0L6_2atmpS1429;
  uint64_t _M0L4mul0S526;
  int32_t _M0L6_2atmpS1428;
  int32_t _M0L6_2atmpS1427;
  uint64_t _M0L4mul1S527;
  uint64_t _M0L1mS528;
  struct _M0TPB7Umul128 _M0L7_2abindS529;
  uint64_t _M0L7_2alow1S530;
  uint64_t _M0L8_2ahigh1S531;
  struct _M0TPB7Umul128 _M0L7_2abindS532;
  uint64_t _M0L7_2alow0S533;
  uint64_t _M0L8_2ahigh0S534;
  uint64_t _M0L3sumS535;
  uint64_t _M0Lm5high1S536;
  int32_t _M0L6_2atmpS1425;
  int32_t _M0L6_2atmpS1426;
  int32_t _M0L5deltaS537;
  uint64_t _M0L6_2atmpS1417;
  int32_t _M0L6_2atmpS1424;
  uint32_t _M0L6_2atmpS1421;
  int32_t _M0L6_2atmpS1423;
  int32_t _M0L6_2atmpS1422;
  uint32_t _M0L6_2atmpS1420;
  uint32_t _M0L6_2atmpS1419;
  uint64_t _M0L6_2atmpS1418;
  uint64_t _M0L1aS538;
  uint64_t _M0L6_2atmpS1416;
  uint64_t _M0L1bS539;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS522 = _M0L1iS523 / 26;
  _M0L5base2S524 = _M0L4baseS522 * 26;
  _M0L6offsetS525 = _M0L1iS523 - _M0L5base2S524;
  _M0L6_2atmpS1429 = _M0L4baseS522 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S526
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1429);
  _M0L6_2atmpS1428 = _M0L4baseS522 * 2;
  _M0L6_2atmpS1427 = _M0L6_2atmpS1428 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S527
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1427);
  if (_M0L6offsetS525 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S526, .$1 = _M0L4mul1S527};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS528
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS525);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS529 = _M0FPB7umul128(_M0L1mS528, _M0L4mul1S527);
  _M0L7_2alow1S530 = _M0L7_2abindS529.$0;
  _M0L8_2ahigh1S531 = _M0L7_2abindS529.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS532 = _M0FPB7umul128(_M0L1mS528, _M0L4mul0S526);
  _M0L7_2alow0S533 = _M0L7_2abindS532.$0;
  _M0L8_2ahigh0S534 = _M0L7_2abindS532.$1;
  _M0L3sumS535 = _M0L8_2ahigh0S534 + _M0L7_2alow1S530;
  _M0Lm5high1S536 = _M0L8_2ahigh1S531;
  if (_M0L3sumS535 < _M0L8_2ahigh0S534) {
    uint64_t _M0L6_2atmpS1415 = _M0Lm5high1S536;
    _M0Lm5high1S536 = _M0L6_2atmpS1415 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1425 = _M0FPB8pow5bits(_M0L1iS523);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1426 = _M0FPB8pow5bits(_M0L5base2S524);
  _M0L5deltaS537 = _M0L6_2atmpS1425 - _M0L6_2atmpS1426;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1417
  = _M0FPB13shiftright128(_M0L7_2alow0S533, _M0L3sumS535, _M0L5deltaS537);
  _M0L6_2atmpS1424 = _M0L1iS523 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1421
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1424);
  _M0L6_2atmpS1423 = _M0L1iS523 % 16;
  _M0L6_2atmpS1422 = _M0L6_2atmpS1423 << 1;
  _M0L6_2atmpS1420 = _M0L6_2atmpS1421 >> (_M0L6_2atmpS1422 & 31);
  _M0L6_2atmpS1419 = _M0L6_2atmpS1420 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1418 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1419);
  _M0L1aS538 = _M0L6_2atmpS1417 + _M0L6_2atmpS1418;
  _M0L6_2atmpS1416 = _M0Lm5high1S536;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS539
  = _M0FPB13shiftright128(_M0L3sumS535, _M0L6_2atmpS1416, _M0L5deltaS537);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS538, .$1 = _M0L1bS539};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS496,
  struct _M0TPB8Pow5Pair _M0L3mulS493,
  int32_t _M0L1jS509,
  int32_t _M0L7mmShiftS511
) {
  uint64_t _M0L7_2amul0S492;
  uint64_t _M0L7_2amul1S494;
  uint64_t _M0L1mS495;
  struct _M0TPB7Umul128 _M0L7_2abindS497;
  uint64_t _M0L5_2aloS498;
  uint64_t _M0L6_2atmpS499;
  struct _M0TPB7Umul128 _M0L7_2abindS500;
  uint64_t _M0L6_2alo2S501;
  uint64_t _M0L6_2ahi2S502;
  uint64_t _M0L3midS503;
  uint64_t _M0L6_2atmpS1414;
  uint64_t _M0L2hiS504;
  uint64_t _M0L3lo2S505;
  uint64_t _M0L6_2atmpS1412;
  uint64_t _M0L6_2atmpS1413;
  uint64_t _M0L4mid2S506;
  uint64_t _M0L6_2atmpS1411;
  uint64_t _M0L3hi2S507;
  int32_t _M0L6_2atmpS1410;
  int32_t _M0L6_2atmpS1409;
  uint64_t _M0L2vpS508;
  uint64_t _M0Lm2vmS510;
  int32_t _M0L6_2atmpS1408;
  int32_t _M0L6_2atmpS1407;
  uint64_t _M0L2vrS521;
  uint64_t _M0L6_2atmpS1406;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S492 = _M0L3mulS493.$0;
  _M0L7_2amul1S494 = _M0L3mulS493.$1;
  _M0L1mS495 = _M0L1mS496 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS497 = _M0FPB7umul128(_M0L1mS495, _M0L7_2amul0S492);
  _M0L5_2aloS498 = _M0L7_2abindS497.$0;
  _M0L6_2atmpS499 = _M0L7_2abindS497.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS500 = _M0FPB7umul128(_M0L1mS495, _M0L7_2amul1S494);
  _M0L6_2alo2S501 = _M0L7_2abindS500.$0;
  _M0L6_2ahi2S502 = _M0L7_2abindS500.$1;
  _M0L3midS503 = _M0L6_2atmpS499 + _M0L6_2alo2S501;
  if (_M0L3midS503 < _M0L6_2atmpS499) {
    _M0L6_2atmpS1414 = 1ull;
  } else {
    _M0L6_2atmpS1414 = 0ull;
  }
  _M0L2hiS504 = _M0L6_2ahi2S502 + _M0L6_2atmpS1414;
  _M0L3lo2S505 = _M0L5_2aloS498 + _M0L7_2amul0S492;
  _M0L6_2atmpS1412 = _M0L3midS503 + _M0L7_2amul1S494;
  if (_M0L3lo2S505 < _M0L5_2aloS498) {
    _M0L6_2atmpS1413 = 1ull;
  } else {
    _M0L6_2atmpS1413 = 0ull;
  }
  _M0L4mid2S506 = _M0L6_2atmpS1412 + _M0L6_2atmpS1413;
  if (_M0L4mid2S506 < _M0L3midS503) {
    _M0L6_2atmpS1411 = 1ull;
  } else {
    _M0L6_2atmpS1411 = 0ull;
  }
  _M0L3hi2S507 = _M0L2hiS504 + _M0L6_2atmpS1411;
  _M0L6_2atmpS1410 = _M0L1jS509 - 64;
  _M0L6_2atmpS1409 = _M0L6_2atmpS1410 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS508
  = _M0FPB13shiftright128(_M0L4mid2S506, _M0L3hi2S507, _M0L6_2atmpS1409);
  _M0Lm2vmS510 = 0ull;
  if (_M0L7mmShiftS511) {
    uint64_t _M0L3lo3S512 = _M0L5_2aloS498 - _M0L7_2amul0S492;
    uint64_t _M0L6_2atmpS1396 = _M0L3midS503 - _M0L7_2amul1S494;
    uint64_t _M0L6_2atmpS1397;
    uint64_t _M0L4mid3S513;
    uint64_t _M0L6_2atmpS1395;
    uint64_t _M0L3hi3S514;
    int32_t _M0L6_2atmpS1394;
    int32_t _M0L6_2atmpS1393;
    if (_M0L5_2aloS498 < _M0L3lo3S512) {
      _M0L6_2atmpS1397 = 1ull;
    } else {
      _M0L6_2atmpS1397 = 0ull;
    }
    _M0L4mid3S513 = _M0L6_2atmpS1396 - _M0L6_2atmpS1397;
    if (_M0L3midS503 < _M0L4mid3S513) {
      _M0L6_2atmpS1395 = 1ull;
    } else {
      _M0L6_2atmpS1395 = 0ull;
    }
    _M0L3hi3S514 = _M0L2hiS504 - _M0L6_2atmpS1395;
    _M0L6_2atmpS1394 = _M0L1jS509 - 64;
    _M0L6_2atmpS1393 = _M0L6_2atmpS1394 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS510
    = _M0FPB13shiftright128(_M0L4mid3S513, _M0L3hi3S514, _M0L6_2atmpS1393);
  } else {
    uint64_t _M0L3lo3S515 = _M0L5_2aloS498 + _M0L5_2aloS498;
    uint64_t _M0L6_2atmpS1404 = _M0L3midS503 + _M0L3midS503;
    uint64_t _M0L6_2atmpS1405;
    uint64_t _M0L4mid3S516;
    uint64_t _M0L6_2atmpS1402;
    uint64_t _M0L6_2atmpS1403;
    uint64_t _M0L3hi3S517;
    uint64_t _M0L3lo4S518;
    uint64_t _M0L6_2atmpS1400;
    uint64_t _M0L6_2atmpS1401;
    uint64_t _M0L4mid4S519;
    uint64_t _M0L6_2atmpS1399;
    uint64_t _M0L3hi4S520;
    int32_t _M0L6_2atmpS1398;
    if (_M0L3lo3S515 < _M0L5_2aloS498) {
      _M0L6_2atmpS1405 = 1ull;
    } else {
      _M0L6_2atmpS1405 = 0ull;
    }
    _M0L4mid3S516 = _M0L6_2atmpS1404 + _M0L6_2atmpS1405;
    _M0L6_2atmpS1402 = _M0L2hiS504 + _M0L2hiS504;
    if (_M0L4mid3S516 < _M0L3midS503) {
      _M0L6_2atmpS1403 = 1ull;
    } else {
      _M0L6_2atmpS1403 = 0ull;
    }
    _M0L3hi3S517 = _M0L6_2atmpS1402 + _M0L6_2atmpS1403;
    _M0L3lo4S518 = _M0L3lo3S515 - _M0L7_2amul0S492;
    _M0L6_2atmpS1400 = _M0L4mid3S516 - _M0L7_2amul1S494;
    if (_M0L3lo3S515 < _M0L3lo4S518) {
      _M0L6_2atmpS1401 = 1ull;
    } else {
      _M0L6_2atmpS1401 = 0ull;
    }
    _M0L4mid4S519 = _M0L6_2atmpS1400 - _M0L6_2atmpS1401;
    if (_M0L4mid3S516 < _M0L4mid4S519) {
      _M0L6_2atmpS1399 = 1ull;
    } else {
      _M0L6_2atmpS1399 = 0ull;
    }
    _M0L3hi4S520 = _M0L3hi3S517 - _M0L6_2atmpS1399;
    _M0L6_2atmpS1398 = _M0L1jS509 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS510
    = _M0FPB13shiftright128(_M0L4mid4S519, _M0L3hi4S520, _M0L6_2atmpS1398);
  }
  _M0L6_2atmpS1408 = _M0L1jS509 - 64;
  _M0L6_2atmpS1407 = _M0L6_2atmpS1408 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS521
  = _M0FPB13shiftright128(_M0L3midS503, _M0L2hiS504, _M0L6_2atmpS1407);
  _M0L6_2atmpS1406 = _M0Lm2vmS510;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS521,
                                                .$1 = _M0L2vpS508,
                                                .$2 = _M0L6_2atmpS1406};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS490,
  int32_t _M0L1pS491
) {
  uint64_t _M0L6_2atmpS1392;
  uint64_t _M0L6_2atmpS1391;
  uint64_t _M0L6_2atmpS1390;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1392 = 1ull << (_M0L1pS491 & 63);
  _M0L6_2atmpS1391 = _M0L6_2atmpS1392 - 1ull;
  _M0L6_2atmpS1390 = _M0L5valueS490 & _M0L6_2atmpS1391;
  return _M0L6_2atmpS1390 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS488,
  int32_t _M0L1pS489
) {
  int32_t _M0L6_2atmpS1389;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1389 = _M0FPB10pow5Factor(_M0L5valueS488);
  return _M0L6_2atmpS1389 >= _M0L1pS489;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS483) {
  uint64_t _M0L6_2atmpS1380;
  uint64_t _M0L6_2atmpS1381;
  uint64_t _M0L6_2atmpS1382;
  uint64_t _M0L6_2atmpS1383;
  uint64_t _M0L6_2atmpS1388;
  int32_t _M0L5countS484;
  uint64_t _M0L1vS485;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1380 = _M0L5valueS483 % 5ull;
  if (_M0L6_2atmpS1380 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1381 = _M0L5valueS483 % 25ull;
  if (_M0L6_2atmpS1381 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1382 = _M0L5valueS483 % 125ull;
  if (_M0L6_2atmpS1382 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1383 = _M0L5valueS483 % 625ull;
  if (_M0L6_2atmpS1383 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1388 = _M0L5valueS483 / 625ull;
  _M0L5countS484 = 4;
  _M0L1vS485 = _M0L6_2atmpS1388;
  while (1) {
    if (_M0L1vS485 > 0ull) {
      uint64_t _M0L6_2atmpS1384 = _M0L1vS485 % 5ull;
      int32_t _M0L6_2atmpS1385;
      uint64_t _M0L6_2atmpS1386;
      if (_M0L6_2atmpS1384 != 0ull) {
        return _M0L5countS484;
      }
      _M0L6_2atmpS1385 = _M0L5countS484 + 1;
      _M0L6_2atmpS1386 = _M0L1vS485 / 5ull;
      _M0L5countS484 = _M0L6_2atmpS1385;
      _M0L1vS485 = _M0L6_2atmpS1386;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS487;
      moonbit_string_t _M0L6_2atmpS1387;
      int32_t _result_1922;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS487
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS487, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS487, _M0L5valueS483);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1387
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS487);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS487);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1922 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1387);
      moonbit_decref_cycle_free(_M0L6_2atmpS1387);
      return _result_1922;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS482,
  uint64_t _M0L2hiS480,
  int32_t _M0L4distS481
) {
  int32_t _M0L6_2atmpS1379;
  uint64_t _M0L6_2atmpS1377;
  uint64_t _M0L6_2atmpS1378;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1379 = 64 - _M0L4distS481;
  _M0L6_2atmpS1377 = _M0L2hiS480 << (_M0L6_2atmpS1379 & 63);
  _M0L6_2atmpS1378 = _M0L2loS482 >> (_M0L4distS481 & 63);
  return _M0L6_2atmpS1377 | _M0L6_2atmpS1378;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS470,
  uint64_t _M0L1bS473
) {
  uint64_t _M0L3aLoS469;
  uint64_t _M0L3aHiS471;
  uint64_t _M0L3bLoS472;
  uint64_t _M0L3bHiS474;
  uint64_t _M0L1xS475;
  uint64_t _M0L6_2atmpS1375;
  uint64_t _M0L6_2atmpS1376;
  uint64_t _M0L1yS476;
  uint64_t _M0L6_2atmpS1373;
  uint64_t _M0L6_2atmpS1374;
  uint64_t _M0L1zS477;
  uint64_t _M0L6_2atmpS1371;
  uint64_t _M0L6_2atmpS1372;
  uint64_t _M0L6_2atmpS1369;
  uint64_t _M0L6_2atmpS1370;
  uint64_t _M0L1wS478;
  uint64_t _M0L2loS479;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS469 = _M0L1aS470 & 4294967295ull;
  _M0L3aHiS471 = _M0L1aS470 >> 32;
  _M0L3bLoS472 = _M0L1bS473 & 4294967295ull;
  _M0L3bHiS474 = _M0L1bS473 >> 32;
  _M0L1xS475 = _M0L3aLoS469 * _M0L3bLoS472;
  _M0L6_2atmpS1375 = _M0L3aHiS471 * _M0L3bLoS472;
  _M0L6_2atmpS1376 = _M0L1xS475 >> 32;
  _M0L1yS476 = _M0L6_2atmpS1375 + _M0L6_2atmpS1376;
  _M0L6_2atmpS1373 = _M0L3aLoS469 * _M0L3bHiS474;
  _M0L6_2atmpS1374 = _M0L1yS476 & 4294967295ull;
  _M0L1zS477 = _M0L6_2atmpS1373 + _M0L6_2atmpS1374;
  _M0L6_2atmpS1371 = _M0L3aHiS471 * _M0L3bHiS474;
  _M0L6_2atmpS1372 = _M0L1yS476 >> 32;
  _M0L6_2atmpS1369 = _M0L6_2atmpS1371 + _M0L6_2atmpS1372;
  _M0L6_2atmpS1370 = _M0L1zS477 >> 32;
  _M0L1wS478 = _M0L6_2atmpS1369 + _M0L6_2atmpS1370;
  _M0L2loS479 = _M0L1aS470 * _M0L1bS473;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS479, .$1 = _M0L1wS478};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS467,
  int32_t _M0L4fromS464,
  int32_t _M0L2toS463
) {
  int32_t _M0L3lenS462;
  int32_t _M0L6_2atmpS1368;
  uint16_t* _M0L6bufferS465;
  int32_t _M0L1iS466;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS462 = _M0L2toS463 - _M0L4fromS464;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1368 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS465
  = (uint16_t*)moonbit_make_string(_M0L3lenS462, _M0L6_2atmpS1368);
  _M0L1iS466 = 0;
  while (1) {
    if (_M0L1iS466 < _M0L3lenS462) {
      int32_t _M0L6_2atmpS1366 = _M0L4fromS464 + _M0L1iS466;
      int32_t _M0L6_2atmpS1365;
      int32_t _M0L6_2atmpS1364;
      int32_t _M0L6_2atmpS1367;
      if (
        _M0L6_2atmpS1366 < 0
        || _M0L6_2atmpS1366 >= Moonbit_array_length(_M0L5bytesS467)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1365 = (int32_t)_M0L5bytesS467[_M0L6_2atmpS1366];
      _M0L6_2atmpS1364 = (uint16_t)_M0L6_2atmpS1365;
      if (
        _M0L1iS466 < 0 || _M0L1iS466 >= Moonbit_array_length(_M0L6bufferS465)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS465[_M0L1iS466] = _M0L6_2atmpS1364;
      _M0L6_2atmpS1367 = _M0L1iS466 + 1;
      _M0L1iS466 = _M0L6_2atmpS1367;
      continue;
    }
    break;
  }
  return _M0L6bufferS465;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS461) {
  int32_t _M0L6_2atmpS1363;
  uint32_t _M0L6_2atmpS1362;
  uint32_t _M0L6_2atmpS1361;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1363 = _M0L1eS461 * 78913;
  _M0L6_2atmpS1362 = *(uint32_t*)&_M0L6_2atmpS1363;
  _M0L6_2atmpS1361 = _M0L6_2atmpS1362 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1361;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS460) {
  int32_t _M0L6_2atmpS1360;
  uint32_t _M0L6_2atmpS1359;
  uint32_t _M0L6_2atmpS1358;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1360 = _M0L1eS460 * 732923;
  _M0L6_2atmpS1359 = *(uint32_t*)&_M0L6_2atmpS1360;
  _M0L6_2atmpS1358 = _M0L6_2atmpS1359 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1358;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS458,
  int32_t _M0L8exponentS459,
  int32_t _M0L8mantissaS456
) {
  moonbit_string_t _M0L1sS457;
  moonbit_string_t _result_1925;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS456) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS458) {
    _M0L1sS457 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS457 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS459) {
    moonbit_string_t _result_1924;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1924
    = moonbit_add_string(_M0L1sS457, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS457);
    return _result_1924;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1925
  = moonbit_add_string(_M0L1sS457, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS457);
  return _result_1925;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS455) {
  int32_t _M0L6_2atmpS1357;
  uint32_t _M0L6_2atmpS1356;
  uint32_t _M0L6_2atmpS1355;
  int32_t _M0L6_2atmpS1354;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1357 = _M0L1eS455 * 1217359;
  _M0L6_2atmpS1356 = *(uint32_t*)&_M0L6_2atmpS1357;
  _M0L6_2atmpS1355 = _M0L6_2atmpS1356 >> 19;
  _M0L6_2atmpS1354 = *(int32_t*)&_M0L6_2atmpS1355;
  return _M0L6_2atmpS1354 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS454) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS454 != _M0L4selfS454) {
    return 0;
  } else if (_M0L4selfS454 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS454 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS454;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS453) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS453 != _M0L4selfS453) {
    return 0ll;
  } else if (_M0L4selfS453 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS453 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS453;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS451
) {
  float* _M0L6_2atmpS1352;
  struct _M0TPB5ArrayGfE* _block_1926;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1352 = (float*)moonbit_make_float_array_raw(_M0L3lenS451);
  _block_1926
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1926)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 28, 0);
  _block_1926->$0 = _M0L6_2atmpS1352;
  _block_1926->$1 = _M0L3lenS451;
  return _block_1926;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS452
) {
  uint8_t* _M0L6_2atmpS1353;
  struct _M0TPB5ArrayGbE* _block_1927;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1353 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS452);
  _block_1927
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_1927)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 31, 0);
  _block_1927->$0 = _M0L6_2atmpS1353;
  _block_1927->$1 = _M0L3lenS452;
  return _block_1927;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS447,
  int32_t _M0L5indexS448
) {
  uint64_t* _M0L6_2atmpS1350;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1350 = _M0L4selfS447;
  if (
    _M0L5indexS448 < 0
    || _M0L5indexS448 >= Moonbit_array_length(_M0L6_2atmpS1350)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1350[_M0L5indexS448];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS449,
  int32_t _M0L5indexS450
) {
  uint32_t* _M0L6_2atmpS1351;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1351 = _M0L4selfS449;
  if (
    _M0L5indexS450 < 0
    || _M0L5indexS450 >= Moonbit_array_length(_M0L6_2atmpS1351)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1351[_M0L5indexS450];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS446
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS446, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS445) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS445, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS444) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS444) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS443) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS443;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS437,
  moonbit_string_t _M0L5valueS439
) {
  int32_t _M0L3lenS1336;
  moonbit_string_t* _M0L6_2atmpS1338;
  int32_t _M0L6_2atmpS1337;
  int32_t _M0L6lengthS438;
  moonbit_string_t* _M0L3bufS1341;
  moonbit_string_t _M0L6_2aoldS1849;
  int32_t _M0L6_2atmpS1342;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1336 = _M0L4selfS437->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1338 = _M0MPC15array5Array6bufferGsE(_M0L4selfS437);
  _M0L6_2atmpS1337 = Moonbit_array_length(_M0L6_2atmpS1338);
  moonbit_decref_cycle_free(_M0L6_2atmpS1338);
  if (_M0L3lenS1336 == _M0L6_2atmpS1337) {
    int32_t _M0L3lenS1340 = _M0L4selfS437->$1;
    int32_t _M0L6_2atmpS1339 = _M0L3lenS1340 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS437, _M0L6_2atmpS1339);
  }
  _M0L6lengthS438 = _M0L4selfS437->$1;
  _M0L3bufS1341 = _M0L4selfS437->$0;
  _M0L6_2aoldS1849 = (moonbit_string_t)_M0L3bufS1341[_M0L6lengthS438];
  moonbit_decref_cycle_free(_M0L6_2aoldS1849);
  _M0L3bufS1341[_M0L6lengthS438] = _M0L5valueS439;
  _M0L6_2atmpS1342 = _M0L6lengthS438 + 1;
  _M0L4selfS437->$1 = _M0L6_2atmpS1342;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS440,
  struct _M0TUsiE* _M0L5valueS442
) {
  int32_t _M0L3lenS1343;
  struct _M0TUsiE** _M0L6_2atmpS1345;
  int32_t _M0L6_2atmpS1344;
  int32_t _M0L6lengthS441;
  struct _M0TUsiE** _M0L3bufS1348;
  struct _M0TUsiE* _M0L6_2aoldS1850;
  int32_t _M0L6_2atmpS1349;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1343 = _M0L4selfS440->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1345 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS440);
  _M0L6_2atmpS1344 = Moonbit_array_length(_M0L6_2atmpS1345);
  moonbit_decref_cycle_free(_M0L6_2atmpS1345);
  if (_M0L3lenS1343 == _M0L6_2atmpS1344) {
    int32_t _M0L3lenS1347 = _M0L4selfS440->$1;
    int32_t _M0L6_2atmpS1346 = _M0L3lenS1347 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS440, _M0L6_2atmpS1346);
  }
  _M0L6lengthS441 = _M0L4selfS440->$1;
  _M0L3bufS1348 = _M0L4selfS440->$0;
  _M0L6_2aoldS1850 = (struct _M0TUsiE*)_M0L3bufS1348[_M0L6lengthS441];
  if (_M0L6_2aoldS1850) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1850);
  }
  _M0L3bufS1348[_M0L6lengthS441] = _M0L5valueS442;
  _M0L6_2atmpS1349 = _M0L6lengthS441 + 1;
  _M0L4selfS440->$1 = _M0L6_2atmpS1349;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS430,
  int32_t _M0L8requiredS432
) {
  int32_t _M0L8old__capS429;
  int32_t _M0L3lenS1334;
  int32_t _M0L8new__capS431;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS429 = _M0MPC15array5Array8capacityGsE(_M0L4selfS430);
  _M0L3lenS1334 = _M0L4selfS430->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS431
  = _M0FPB23array__growth__capacity(_M0L8old__capS429, _M0L3lenS1334, _M0L8requiredS432);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS430, _M0L8new__capS431);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS434,
  int32_t _M0L8requiredS436
) {
  int32_t _M0L8old__capS433;
  int32_t _M0L3lenS1335;
  int32_t _M0L8new__capS435;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS433 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS434);
  _M0L3lenS1335 = _M0L4selfS434->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS435
  = _M0FPB23array__growth__capacity(_M0L8old__capS433, _M0L3lenS1335, _M0L8requiredS436);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS434, _M0L8new__capS435);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS418,
  int32_t _M0L13new__capacityS421
) {
  moonbit_string_t* _M0L8old__bufS417;
  int32_t _M0L3lenS419;
  int32_t _M0L9copy__lenS420;
  moonbit_string_t* _M0L8new__bufS422;
  moonbit_string_t* _M0L6_2aoldS1851;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS417 = _M0L4selfS418->$0;
  _M0L3lenS419 = _M0L4selfS418->$1;
  if (_M0L3lenS419 < _M0L13new__capacityS421) {
    _M0L9copy__lenS420 = _M0L3lenS419;
  } else {
    _M0L9copy__lenS420 = _M0L13new__capacityS421;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS417);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS422
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS417, _M0L13new__capacityS421, _M0L9copy__lenS420, 0, 0);
  _M0L6_2aoldS1851 = _M0L4selfS418->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1851);
  _M0L4selfS418->$0 = _M0L8new__bufS422;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS424,
  int32_t _M0L13new__capacityS427
) {
  struct _M0TUsiE** _M0L8old__bufS423;
  int32_t _M0L3lenS425;
  int32_t _M0L9copy__lenS426;
  struct _M0TUsiE** _M0L8new__bufS428;
  struct _M0TUsiE** _M0L6_2aoldS1852;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS423 = _M0L4selfS424->$0;
  _M0L3lenS425 = _M0L4selfS424->$1;
  if (_M0L3lenS425 < _M0L13new__capacityS427) {
    _M0L9copy__lenS426 = _M0L3lenS425;
  } else {
    _M0L9copy__lenS426 = _M0L13new__capacityS427;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS423);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS428
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS423, _M0L13new__capacityS427, _M0L9copy__lenS426, 0, 0);
  _M0L6_2aoldS1852 = _M0L4selfS424->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1852);
  _M0L4selfS424->$0 = _M0L8new__bufS428;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS415
) {
  moonbit_string_t* _M0L6_2atmpS1332;
  int32_t _result_1928;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1332 = _M0MPC15array5Array6bufferGsE(_M0L4selfS415);
  _result_1928 = Moonbit_array_length(_M0L6_2atmpS1332);
  moonbit_decref_cycle_free(_M0L6_2atmpS1332);
  return _result_1928;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS416
) {
  struct _M0TUsiE** _M0L6_2atmpS1333;
  int32_t _result_1929;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1333 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS416);
  _result_1929 = Moonbit_array_length(_M0L6_2atmpS1333);
  moonbit_decref_cycle_free(_M0L6_2atmpS1333);
  return _result_1929;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS411,
  int32_t _M0L3lenS409,
  int32_t _M0L8requiredS408
) {
  int32_t _M0L5startS410;
  int32_t _M0L5spaceS412;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS408 < _M0L3lenS409) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_17.data);
  }
  if (_M0L7currentS411 == 0) {
    _M0L5startS410 = 8;
  } else {
    _M0L5startS410 = _M0L7currentS411;
  }
  _M0L5spaceS412 = _M0L5startS410;
  while (1) {
    if (_M0L5spaceS412 < _M0L8requiredS408) {
      int32_t _M0L4nextS413 = _M0L5spaceS412 * 2;
      if (_M0L4nextS413 <= _M0L5spaceS412) {
        return _M0L8requiredS408;
      }
      _M0L5spaceS412 = _M0L4nextS413;
      continue;
    } else {
      return _M0L5spaceS412;
    }
    break;
  }
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS404) {
  float* _M0L8_2afieldS1853;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1853 = _M0L4selfS404->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1853);
  return _M0L8_2afieldS1853;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS405) {
  uint8_t* _M0L8_2afieldS1854;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1854 = _M0L4selfS405->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1854);
  return _M0L8_2afieldS1854;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS406
) {
  moonbit_string_t* _M0L8_2afieldS1855;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1855 = _M0L4selfS406->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1855);
  return _M0L8_2afieldS1855;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS407
) {
  struct _M0TUsiE** _M0L8_2afieldS1856;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1856 = _M0L4selfS407->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1856);
  return _M0L8_2afieldS1856;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS403
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS403);
  return _M0L4selfS403;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS402,
  struct _M0TPC16string10StringView _M0L3strS400
) {
  int32_t _M0L3endS1330;
  int32_t _M0L5startS1331;
  int32_t _M0L8str__lenS399;
  int32_t _M0L3lenS1329;
  int32_t _M0L8requiredS401;
  uint16_t* _M0L4dataS1322;
  int32_t _M0L6_2atmpS1321;
  int32_t _if__result_1931;
  uint16_t* _M0L4dataS1323;
  int32_t _M0L3lenS1324;
  moonbit_string_t _M0L6_2atmpS1325;
  int32_t _M0L6_2atmpS1326;
  int32_t _M0L3lenS1328;
  int32_t _M0L6_2atmpS1327;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1330 = _M0L3strS400.$2;
  _M0L5startS1331 = _M0L3strS400.$1;
  _M0L8str__lenS399 = _M0L3endS1330 - _M0L5startS1331;
  if (_M0L8str__lenS399 == 0) {
    return 0;
  }
  _M0L3lenS1329 = _M0L4selfS402->$1;
  _M0L8requiredS401 = _M0L3lenS1329 + _M0L8str__lenS399;
  _M0L4dataS1322 = _M0L4selfS402->$0;
  _M0L6_2atmpS1321 = Moonbit_array_length(_M0L4dataS1322);
  if (_M0L8requiredS401 > _M0L6_2atmpS1321) {
    _if__result_1931 = 1;
  } else {
    int32_t _M0L3lenS1320 = _M0L4selfS402->$1;
    _if__result_1931 = _M0L8requiredS401 < _M0L3lenS1320;
  }
  if (_if__result_1931) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS402, _M0L8requiredS401);
  }
  _M0L4dataS1323 = _M0L4selfS402->$0;
  _M0L3lenS1324 = _M0L4selfS402->$1;
  moonbit_incref_cycle_free(_M0L4dataS1323);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1325 = _M0MPC16string10StringView4data(_M0L3strS400);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1326 = _M0MPC16string10StringView13start__offset(_M0L3strS400);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1323, _M0L3lenS1324, _M0L6_2atmpS1325, _M0L6_2atmpS1326, _M0L8str__lenS399);
  moonbit_decref_cycle_free(_M0L4dataS1323);
  moonbit_decref_cycle_free(_M0L6_2atmpS1325);
  _M0L3lenS1328 = _M0L4selfS402->$1;
  _M0L6_2atmpS1327 = _M0L3lenS1328 + _M0L8str__lenS399;
  _M0L4selfS402->$1 = _M0L6_2atmpS1327;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS396,
  int32_t _M0L5startS394,
  int32_t _M0L3endS395
) {
  int32_t _if__result_1932;
  int32_t _M0L3lenS397;
  int32_t _M0L6_2atmpS1319;
  moonbit_bytes_t _M0L5bytesS398;
  moonbit_bytes_t _M0L6_2atmpS1318;
  moonbit_string_t _result_1933;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS394 == 0) {
    int32_t _M0L6_2atmpS1317 = Moonbit_array_length(_M0L3strS396);
    _if__result_1932 = _M0L3endS395 == _M0L6_2atmpS1317;
  } else {
    _if__result_1932 = 0;
  }
  if (_if__result_1932) {
    moonbit_incref_cycle_free(_M0L3strS396);
    return _M0L3strS396;
  }
  _M0L3lenS397 = _M0L3endS395 - _M0L5startS394;
  _M0L6_2atmpS1319 = _M0L3lenS397 * 2;
  _M0L5bytesS398 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1319, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS398, 0, _M0L3strS396, _M0L5startS394, _M0L3lenS397);
  _M0L6_2atmpS1318 = _M0L5bytesS398;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_1933
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1318, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1318);
  return _result_1933;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS389,
  int32_t _M0L6offsetS393,
  int64_t _M0L6lengthS391
) {
  int32_t _M0L3lenS388;
  int32_t _M0L6lengthS390;
  int32_t _if__result_1934;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS388 = Moonbit_array_length(_M0L4selfS389);
  if (_M0L6lengthS391 == 4294967296ll) {
    _M0L6lengthS390 = _M0L3lenS388 - _M0L6offsetS393;
  } else {
    int64_t _M0L7_2aSomeS392 = _M0L6lengthS391;
    _M0L6lengthS390 = (int32_t)_M0L7_2aSomeS392;
  }
  if (_M0L6offsetS393 >= 0) {
    if (_M0L6lengthS390 >= 0) {
      int32_t _M0L6_2atmpS1316 = _M0L6offsetS393 + _M0L6lengthS390;
      _if__result_1934 = _M0L6_2atmpS1316 <= _M0L3lenS388;
    } else {
      _if__result_1934 = 0;
    }
  } else {
    _if__result_1934 = 0;
  }
  if (_if__result_1934) {
    moonbit_incref_cycle_free(_M0L4selfS389);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS389, _M0L6offsetS393, _M0L6lengthS390);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS380,
  int32_t _M0L13bytes__offsetS375,
  moonbit_string_t _M0L3strS382,
  int32_t _M0L11str__offsetS378,
  int32_t _M0L6lengthS376
) {
  int32_t _M0L6_2atmpS1315;
  int32_t _M0L6_2atmpS1314;
  int32_t _M0L2e1S374;
  int32_t _M0L6_2atmpS1313;
  int32_t _M0L2e2S377;
  int32_t _M0L4len1S379;
  int32_t _M0L4len2S381;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1315 = _M0L6lengthS376 * 2;
  _M0L6_2atmpS1314 = _M0L13bytes__offsetS375 + _M0L6_2atmpS1315;
  _M0L2e1S374 = _M0L6_2atmpS1314 - 1;
  _M0L6_2atmpS1313 = _M0L11str__offsetS378 + _M0L6lengthS376;
  _M0L2e2S377 = _M0L6_2atmpS1313 - 1;
  _M0L4len1S379 = Moonbit_array_length(_M0L4selfS380);
  _M0L4len2S381 = Moonbit_array_length(_M0L3strS382);
  if (
    _M0L6lengthS376 >= 0
    && _M0L13bytes__offsetS375 >= 0
    && _M0L2e1S374 < _M0L4len1S379
    && _M0L11str__offsetS378 >= 0
    && _M0L2e2S377 < _M0L4len2S381
  ) {
    int32_t _M0L16end__str__offsetS383 =
      _M0L11str__offsetS378 + _M0L6lengthS376;
    int32_t _M0L1iS384 = _M0L11str__offsetS378;
    int32_t _M0L1jS385 = _M0L13bytes__offsetS375;
    while (1) {
      if (_M0L1iS384 < _M0L16end__str__offsetS383) {
        int32_t _M0L6_2atmpS1310 = _M0L3strS382[_M0L1iS384];
        int32_t _M0L6_2atmpS1309 = (int32_t)_M0L6_2atmpS1310;
        uint32_t _M0L1cS386 = *(uint32_t*)&_M0L6_2atmpS1309;
        uint32_t _M0L6_2atmpS1305 = _M0L1cS386 & 255u;
        int32_t _M0L6_2atmpS1304;
        int32_t _M0L6_2atmpS1306;
        uint32_t _M0L6_2atmpS1308;
        int32_t _M0L6_2atmpS1307;
        int32_t _M0L6_2atmpS1311;
        int32_t _M0L6_2atmpS1312;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1304 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1305);
        if (
          _M0L1jS385 < 0 || _M0L1jS385 >= Moonbit_array_length(_M0L4selfS380)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS380[_M0L1jS385] = _M0L6_2atmpS1304;
        _M0L6_2atmpS1306 = _M0L1jS385 + 1;
        _M0L6_2atmpS1308 = _M0L1cS386 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1307 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1308);
        if (
          _M0L6_2atmpS1306 < 0
          || _M0L6_2atmpS1306 >= Moonbit_array_length(_M0L4selfS380)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS380[_M0L6_2atmpS1306] = _M0L6_2atmpS1307;
        _M0L6_2atmpS1311 = _M0L1iS384 + 1;
        _M0L6_2atmpS1312 = _M0L1jS385 + 2;
        _M0L1iS384 = _M0L6_2atmpS1311;
        _M0L1jS385 = _M0L6_2atmpS1312;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS373) {
  int32_t _M0L6_2atmpS1303;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1303 = *(int32_t*)&_M0L4selfS373;
  return _M0L6_2atmpS1303 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS365,
  int32_t _M0L5radixS364
) {
  uint16_t* _M0L6bufferS366;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS364 < 2 || _M0L5radixS364 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS365 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  switch (_M0L5radixS364) {
    case 10: {
      int32_t _M0L3lenS367;
      uint16_t* _M0L6bufferS368;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS367 = _M0FPB12dec__count64(_M0L4selfS365);
      _M0L6bufferS368 = (uint16_t*)moonbit_make_string(_M0L3lenS367, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS368, _M0L4selfS365, 0, _M0L3lenS367);
      _M0L6bufferS366 = _M0L6bufferS368;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS369;
      uint16_t* _M0L6bufferS370;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS369 = _M0FPB12hex__count64(_M0L4selfS365);
      _M0L6bufferS370 = (uint16_t*)moonbit_make_string(_M0L3lenS369, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS370, _M0L4selfS365, 0, _M0L3lenS369);
      _M0L6bufferS366 = _M0L6bufferS370;
      break;
    }
    default: {
      int32_t _M0L3lenS371;
      uint16_t* _M0L6bufferS372;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS371 = _M0FPB14radix__count64(_M0L4selfS365, _M0L5radixS364);
      _M0L6bufferS372 = (uint16_t*)moonbit_make_string(_M0L3lenS371, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS372, _M0L4selfS365, 0, _M0L3lenS371, _M0L5radixS364);
      _M0L6bufferS366 = _M0L6bufferS372;
      break;
    }
  }
  return _M0L6bufferS366;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS348,
  int32_t _M0L5radixS347
) {
  int32_t _M0L12is__negativeS349;
  uint64_t _M0L3numS350;
  uint16_t* _M0L6bufferS351;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS347 < 2 || _M0L5radixS347 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS348 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS349 = _M0L4selfS348 < 0ll;
  if (_M0L12is__negativeS349) {
    int64_t _M0L6_2atmpS1302 = -_M0L4selfS348;
    _M0L3numS350 = *(uint64_t*)&_M0L6_2atmpS1302;
  } else {
    _M0L3numS350 = *(uint64_t*)&_M0L4selfS348;
  }
  switch (_M0L5radixS347) {
    case 10: {
      int32_t _M0L10digit__lenS352;
      int32_t _M0L6_2atmpS1299;
      int32_t _M0L10total__lenS353;
      uint16_t* _M0L6bufferS354;
      int32_t _M0L12digit__startS355;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS352 = _M0FPB12dec__count64(_M0L3numS350);
      if (_M0L12is__negativeS349) {
        _M0L6_2atmpS1299 = 1;
      } else {
        _M0L6_2atmpS1299 = 0;
      }
      _M0L10total__lenS353 = _M0L10digit__lenS352 + _M0L6_2atmpS1299;
      _M0L6bufferS354
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS353, 0);
      if (_M0L12is__negativeS349) {
        _M0L12digit__startS355 = 1;
      } else {
        _M0L12digit__startS355 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS354, _M0L3numS350, _M0L12digit__startS355, _M0L10total__lenS353);
      _M0L6bufferS351 = _M0L6bufferS354;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS356;
      int32_t _M0L6_2atmpS1300;
      int32_t _M0L10total__lenS357;
      uint16_t* _M0L6bufferS358;
      int32_t _M0L12digit__startS359;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS356 = _M0FPB12hex__count64(_M0L3numS350);
      if (_M0L12is__negativeS349) {
        _M0L6_2atmpS1300 = 1;
      } else {
        _M0L6_2atmpS1300 = 0;
      }
      _M0L10total__lenS357 = _M0L10digit__lenS356 + _M0L6_2atmpS1300;
      _M0L6bufferS358
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS357, 0);
      if (_M0L12is__negativeS349) {
        _M0L12digit__startS359 = 1;
      } else {
        _M0L12digit__startS359 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS358, _M0L3numS350, _M0L12digit__startS359, _M0L10total__lenS357);
      _M0L6bufferS351 = _M0L6bufferS358;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS360;
      int32_t _M0L6_2atmpS1301;
      int32_t _M0L10total__lenS361;
      uint16_t* _M0L6bufferS362;
      int32_t _M0L12digit__startS363;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS360
      = _M0FPB14radix__count64(_M0L3numS350, _M0L5radixS347);
      if (_M0L12is__negativeS349) {
        _M0L6_2atmpS1301 = 1;
      } else {
        _M0L6_2atmpS1301 = 0;
      }
      _M0L10total__lenS361 = _M0L10digit__lenS360 + _M0L6_2atmpS1301;
      _M0L6bufferS362
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS361, 0);
      if (_M0L12is__negativeS349) {
        _M0L12digit__startS363 = 1;
      } else {
        _M0L12digit__startS363 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS362, _M0L3numS350, _M0L12digit__startS363, _M0L10total__lenS361, _M0L5radixS347);
      _M0L6bufferS351 = _M0L6bufferS362;
      break;
    }
  }
  if (_M0L12is__negativeS349) {
    _M0L6bufferS351[0] = 45;
  }
  return _M0L6bufferS351;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS333,
  uint64_t _M0L3numS345,
  int32_t _M0L12digit__startS334,
  int32_t _M0L10total__lenS346
) {
  int32_t _M0L6_2atmpS1298;
  uint64_t _M0L3numS323;
  int32_t _M0L6offsetS324;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1298 = _M0L10total__lenS346 - _M0L12digit__startS334;
  _M0L3numS323 = _M0L3numS345;
  _M0L6offsetS324 = _M0L6_2atmpS1298;
  while (1) {
    if (_M0L3numS323 >= 10000ull) {
      uint64_t _M0L1tS325 = _M0L3numS323 / 10000ull;
      uint64_t _M0L6_2atmpS1275 = _M0L3numS323 % 10000ull;
      int32_t _M0L1rS326 = (int32_t)_M0L6_2atmpS1275;
      int32_t _M0L2d1S327 = _M0L1rS326 / 100;
      int32_t _M0L2d2S328 = _M0L1rS326 % 100;
      int32_t _M0L6_2atmpS1274 = _M0L2d1S327 / 10;
      int32_t _M0L6_2atmpS1273 = 48 + _M0L6_2atmpS1274;
      int32_t _M0L6d1__hiS329 = (uint16_t)_M0L6_2atmpS1273;
      int32_t _M0L6_2atmpS1272 = _M0L2d1S327 % 10;
      int32_t _M0L6_2atmpS1271 = 48 + _M0L6_2atmpS1272;
      int32_t _M0L6d1__loS330 = (uint16_t)_M0L6_2atmpS1271;
      int32_t _M0L6_2atmpS1270 = _M0L2d2S328 / 10;
      int32_t _M0L6_2atmpS1269 = 48 + _M0L6_2atmpS1270;
      int32_t _M0L6d2__hiS331 = (uint16_t)_M0L6_2atmpS1269;
      int32_t _M0L6_2atmpS1268 = _M0L2d2S328 % 10;
      int32_t _M0L6_2atmpS1267 = 48 + _M0L6_2atmpS1268;
      int32_t _M0L6d2__loS332 = (uint16_t)_M0L6_2atmpS1267;
      int32_t _M0L6_2atmpS1259 = _M0L12digit__startS334 + _M0L6offsetS324;
      int32_t _M0L6_2atmpS1258 = _M0L6_2atmpS1259 - 4;
      int32_t _M0L6_2atmpS1261;
      int32_t _M0L6_2atmpS1260;
      int32_t _M0L6_2atmpS1263;
      int32_t _M0L6_2atmpS1262;
      int32_t _M0L6_2atmpS1265;
      int32_t _M0L6_2atmpS1264;
      int32_t _M0L6_2atmpS1266;
      _M0L6bufferS333[_M0L6_2atmpS1258] = _M0L6d1__hiS329;
      _M0L6_2atmpS1261 = _M0L12digit__startS334 + _M0L6offsetS324;
      _M0L6_2atmpS1260 = _M0L6_2atmpS1261 - 3;
      _M0L6bufferS333[_M0L6_2atmpS1260] = _M0L6d1__loS330;
      _M0L6_2atmpS1263 = _M0L12digit__startS334 + _M0L6offsetS324;
      _M0L6_2atmpS1262 = _M0L6_2atmpS1263 - 2;
      _M0L6bufferS333[_M0L6_2atmpS1262] = _M0L6d2__hiS331;
      _M0L6_2atmpS1265 = _M0L12digit__startS334 + _M0L6offsetS324;
      _M0L6_2atmpS1264 = _M0L6_2atmpS1265 - 1;
      _M0L6bufferS333[_M0L6_2atmpS1264] = _M0L6d2__loS332;
      _M0L6_2atmpS1266 = _M0L6offsetS324 - 4;
      _M0L3numS323 = _M0L1tS325;
      _M0L6offsetS324 = _M0L6_2atmpS1266;
      continue;
    } else {
      int32_t _M0L6_2atmpS1297 = (int32_t)_M0L3numS323;
      int32_t _M0L9remainingS336 = _M0L6_2atmpS1297;
      int32_t _M0L6offsetS337 = _M0L6offsetS324;
      while (1) {
        if (_M0L9remainingS336 >= 100) {
          int32_t _M0L1tS338 = _M0L9remainingS336 / 100;
          int32_t _M0L1dS339 = _M0L9remainingS336 % 100;
          int32_t _M0L6_2atmpS1284 = _M0L1dS339 / 10;
          int32_t _M0L6_2atmpS1283 = 48 + _M0L6_2atmpS1284;
          int32_t _M0L5d__hiS340 = (uint16_t)_M0L6_2atmpS1283;
          int32_t _M0L6_2atmpS1282 = _M0L1dS339 % 10;
          int32_t _M0L6_2atmpS1281 = 48 + _M0L6_2atmpS1282;
          int32_t _M0L5d__loS341 = (uint16_t)_M0L6_2atmpS1281;
          int32_t _M0L6_2atmpS1277 = _M0L12digit__startS334 + _M0L6offsetS337;
          int32_t _M0L6_2atmpS1276 = _M0L6_2atmpS1277 - 2;
          int32_t _M0L6_2atmpS1279;
          int32_t _M0L6_2atmpS1278;
          int32_t _M0L6_2atmpS1280;
          _M0L6bufferS333[_M0L6_2atmpS1276] = _M0L5d__hiS340;
          _M0L6_2atmpS1279 = _M0L12digit__startS334 + _M0L6offsetS337;
          _M0L6_2atmpS1278 = _M0L6_2atmpS1279 - 1;
          _M0L6bufferS333[_M0L6_2atmpS1278] = _M0L5d__loS341;
          _M0L6_2atmpS1280 = _M0L6offsetS337 - 2;
          _M0L9remainingS336 = _M0L1tS338;
          _M0L6offsetS337 = _M0L6_2atmpS1280;
          continue;
        } else if (_M0L9remainingS336 >= 10) {
          int32_t _M0L6_2atmpS1292 = _M0L9remainingS336 / 10;
          int32_t _M0L6_2atmpS1291 = 48 + _M0L6_2atmpS1292;
          int32_t _M0L5d__hiS343 = (uint16_t)_M0L6_2atmpS1291;
          int32_t _M0L6_2atmpS1290 = _M0L9remainingS336 % 10;
          int32_t _M0L6_2atmpS1289 = 48 + _M0L6_2atmpS1290;
          int32_t _M0L5d__loS344 = (uint16_t)_M0L6_2atmpS1289;
          int32_t _M0L6_2atmpS1286 = _M0L12digit__startS334 + _M0L6offsetS337;
          int32_t _M0L6_2atmpS1285 = _M0L6_2atmpS1286 - 2;
          int32_t _M0L6_2atmpS1288;
          int32_t _M0L6_2atmpS1287;
          _M0L6bufferS333[_M0L6_2atmpS1285] = _M0L5d__hiS343;
          _M0L6_2atmpS1288 = _M0L12digit__startS334 + _M0L6offsetS337;
          _M0L6_2atmpS1287 = _M0L6_2atmpS1288 - 1;
          _M0L6bufferS333[_M0L6_2atmpS1287] = _M0L5d__loS344;
        } else {
          int32_t _M0L6_2atmpS1296 = _M0L12digit__startS334 + _M0L6offsetS337;
          int32_t _M0L6_2atmpS1293 = _M0L6_2atmpS1296 - 1;
          int32_t _M0L6_2atmpS1295 = 48 + _M0L9remainingS336;
          int32_t _M0L6_2atmpS1294 = (uint16_t)_M0L6_2atmpS1295;
          _M0L6bufferS333[_M0L6_2atmpS1293] = _M0L6_2atmpS1294;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS313,
  uint64_t _M0L3numS317,
  int32_t _M0L12digit__startS314,
  int32_t _M0L10total__lenS316,
  int32_t _M0L5radixS307
) {
  uint64_t _M0L4baseS306;
  int32_t _M0L6_2atmpS1243;
  int32_t _M0L6_2atmpS1242;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS306 = _M0MPC13int3Int10to__uint64(_M0L5radixS307);
  _M0L6_2atmpS1243 = _M0L5radixS307 - 1;
  _M0L6_2atmpS1242 = _M0L5radixS307 & _M0L6_2atmpS1243;
  if (_M0L6_2atmpS1242 == 0) {
    int32_t _M0L5shiftS308;
    uint64_t _M0L4maskS309;
    int32_t _M0L6_2atmpS1250;
    int32_t _M0L6offsetS310;
    uint64_t _M0L1nS311;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS308 = moonbit_ctz32(_M0L5radixS307);
    _M0L4maskS309 = _M0L4baseS306 - 1ull;
    _M0L6_2atmpS1250 = _M0L10total__lenS316 - _M0L12digit__startS314;
    _M0L6offsetS310 = _M0L6_2atmpS1250;
    _M0L1nS311 = _M0L3numS317;
    while (1) {
      if (_M0L1nS311 > 0ull) {
        uint64_t _M0L6_2atmpS1249 = _M0L1nS311 & _M0L4maskS309;
        int32_t _M0L5digitS312 = (int32_t)_M0L6_2atmpS1249;
        int32_t _M0L6_2atmpS1246 = _M0L12digit__startS314 + _M0L6offsetS310;
        int32_t _M0L6_2atmpS1244 = _M0L6_2atmpS1246 - 1;
        int32_t _M0L6_2atmpS1245 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS312];
        int32_t _M0L6_2atmpS1247;
        uint64_t _M0L6_2atmpS1248;
        _M0L6bufferS313[_M0L6_2atmpS1244] = _M0L6_2atmpS1245;
        _M0L6_2atmpS1247 = _M0L6offsetS310 - 1;
        _M0L6_2atmpS1248 = _M0L1nS311 >> (_M0L5shiftS308 & 63);
        _M0L6offsetS310 = _M0L6_2atmpS1247;
        _M0L1nS311 = _M0L6_2atmpS1248;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1257 = _M0L10total__lenS316 - _M0L12digit__startS314;
    int32_t _M0L6offsetS318 = _M0L6_2atmpS1257;
    uint64_t _M0L1nS319 = _M0L3numS317;
    while (1) {
      if (_M0L1nS319 > 0ull) {
        uint64_t _M0L1qS320 = _M0L1nS319 / _M0L4baseS306;
        uint64_t _M0L6_2atmpS1256 = _M0L1qS320 * _M0L4baseS306;
        uint64_t _M0L6_2atmpS1255 = _M0L1nS319 - _M0L6_2atmpS1256;
        int32_t _M0L5digitS321 = (int32_t)_M0L6_2atmpS1255;
        int32_t _M0L6_2atmpS1253 = _M0L12digit__startS314 + _M0L6offsetS318;
        int32_t _M0L6_2atmpS1251 = _M0L6_2atmpS1253 - 1;
        int32_t _M0L6_2atmpS1252 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS321];
        int32_t _M0L6_2atmpS1254;
        _M0L6bufferS313[_M0L6_2atmpS1251] = _M0L6_2atmpS1252;
        _M0L6_2atmpS1254 = _M0L6offsetS318 - 1;
        _M0L6offsetS318 = _M0L6_2atmpS1254;
        _M0L1nS319 = _M0L1qS320;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS300,
  uint64_t _M0L3numS305,
  int32_t _M0L12digit__startS301,
  int32_t _M0L10total__lenS304
) {
  int32_t _M0L6_2atmpS1241;
  int32_t _M0L6offsetS295;
  uint64_t _M0L1nS296;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1241 = _M0L10total__lenS304 - _M0L12digit__startS301;
  _M0L6offsetS295 = _M0L6_2atmpS1241;
  _M0L1nS296 = _M0L3numS305;
  while (1) {
    if (_M0L6offsetS295 >= 2) {
      uint64_t _M0L6_2atmpS1238 = _M0L1nS296 & 255ull;
      int32_t _M0L9byte__valS297 = (int32_t)_M0L6_2atmpS1238;
      int32_t _M0L2hiS298 = _M0L9byte__valS297 / 16;
      int32_t _M0L2loS299 = _M0L9byte__valS297 % 16;
      int32_t _M0L6_2atmpS1232 = _M0L12digit__startS301 + _M0L6offsetS295;
      int32_t _M0L6_2atmpS1230 = _M0L6_2atmpS1232 - 2;
      int32_t _M0L6_2atmpS1231 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L2hiS298];
      int32_t _M0L6_2atmpS1235;
      int32_t _M0L6_2atmpS1233;
      int32_t _M0L6_2atmpS1234;
      int32_t _M0L6_2atmpS1236;
      uint64_t _M0L6_2atmpS1237;
      _M0L6bufferS300[_M0L6_2atmpS1230] = _M0L6_2atmpS1231;
      _M0L6_2atmpS1235 = _M0L12digit__startS301 + _M0L6offsetS295;
      _M0L6_2atmpS1233 = _M0L6_2atmpS1235 - 1;
      _M0L6_2atmpS1234
      = ((moonbit_string_t)moonbit_string_literal_19.data)[
        _M0L2loS299
      ];
      _M0L6bufferS300[_M0L6_2atmpS1233] = _M0L6_2atmpS1234;
      _M0L6_2atmpS1236 = _M0L6offsetS295 - 2;
      _M0L6_2atmpS1237 = _M0L1nS296 >> 8;
      _M0L6offsetS295 = _M0L6_2atmpS1236;
      _M0L1nS296 = _M0L6_2atmpS1237;
      continue;
    } else if (_M0L6offsetS295 == 1) {
      uint64_t _M0L6_2atmpS1240 = _M0L1nS296 & 15ull;
      int32_t _M0L6nibbleS303 = (int32_t)_M0L6_2atmpS1240;
      int32_t _M0L6_2atmpS1239 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L6nibbleS303];
      _M0L6bufferS300[_M0L12digit__startS301] = _M0L6_2atmpS1239;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS289,
  int32_t _M0L5radixS291
) {
  uint64_t _M0L4baseS290;
  uint64_t _M0L3numS292;
  int32_t _M0L5countS293;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS289 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS290 = _M0MPC13int3Int10to__uint64(_M0L5radixS291);
  _M0L3numS292 = _M0L5valueS289;
  _M0L5countS293 = 0;
  while (1) {
    if (_M0L3numS292 > 0ull) {
      uint64_t _M0L6_2atmpS1228 = _M0L3numS292 / _M0L4baseS290;
      int32_t _M0L6_2atmpS1229 = _M0L5countS293 + 1;
      _M0L3numS292 = _M0L6_2atmpS1228;
      _M0L5countS293 = _M0L6_2atmpS1229;
      continue;
    } else {
      return _M0L5countS293;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS287) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS287 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS288;
    int32_t _M0L6_2atmpS1227;
    int32_t _M0L6_2atmpS1226;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS288 = moonbit_clz64(_M0L5valueS287);
    _M0L6_2atmpS1227 = 63 - _M0L14leading__zerosS288;
    _M0L6_2atmpS1226 = _M0L6_2atmpS1227 / 4;
    return _M0L6_2atmpS1226 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS286) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS286 >= 10000000000ull) {
    if (_M0L5valueS286 >= 100000000000000ull) {
      if (_M0L5valueS286 >= 10000000000000000ull) {
        if (_M0L5valueS286 >= 1000000000000000000ull) {
          if (_M0L5valueS286 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS286 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS286 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS286 >= 1000000000000ull) {
      if (_M0L5valueS286 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS286 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS286 >= 100000ull) {
    if (_M0L5valueS286 >= 10000000ull) {
      if (_M0L5valueS286 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS286 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS286 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS286 >= 1000ull) {
    if (_M0L5valueS286 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS286 >= 100ull) {
    return 3;
  } else if (_M0L5valueS286 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS270,
  int32_t _M0L5radixS269
) {
  int32_t _M0L12is__negativeS271;
  uint32_t _M0L3numS272;
  uint16_t* _M0L6bufferS273;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS269 < 2 || _M0L5radixS269 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS270 == 0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS271 = _M0L4selfS270 < 0;
  if (_M0L12is__negativeS271) {
    int32_t _M0L6_2atmpS1225 = -_M0L4selfS270;
    _M0L3numS272 = *(uint32_t*)&_M0L6_2atmpS1225;
  } else {
    _M0L3numS272 = *(uint32_t*)&_M0L4selfS270;
  }
  switch (_M0L5radixS269) {
    case 10: {
      int32_t _M0L10digit__lenS274;
      int32_t _M0L6_2atmpS1222;
      int32_t _M0L10total__lenS275;
      uint16_t* _M0L6bufferS276;
      int32_t _M0L12digit__startS277;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS274 = _M0FPB12dec__count32(_M0L3numS272);
      if (_M0L12is__negativeS271) {
        _M0L6_2atmpS1222 = 1;
      } else {
        _M0L6_2atmpS1222 = 0;
      }
      _M0L10total__lenS275 = _M0L10digit__lenS274 + _M0L6_2atmpS1222;
      _M0L6bufferS276
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS275, 0);
      if (_M0L12is__negativeS271) {
        _M0L12digit__startS277 = 1;
      } else {
        _M0L12digit__startS277 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS276, _M0L3numS272, _M0L12digit__startS277, _M0L10total__lenS275);
      _M0L6bufferS273 = _M0L6bufferS276;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS278;
      int32_t _M0L6_2atmpS1223;
      int32_t _M0L10total__lenS279;
      uint16_t* _M0L6bufferS280;
      int32_t _M0L12digit__startS281;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS278 = _M0FPB12hex__count32(_M0L3numS272);
      if (_M0L12is__negativeS271) {
        _M0L6_2atmpS1223 = 1;
      } else {
        _M0L6_2atmpS1223 = 0;
      }
      _M0L10total__lenS279 = _M0L10digit__lenS278 + _M0L6_2atmpS1223;
      _M0L6bufferS280
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS279, 0);
      if (_M0L12is__negativeS271) {
        _M0L12digit__startS281 = 1;
      } else {
        _M0L12digit__startS281 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS280, _M0L3numS272, _M0L12digit__startS281, _M0L10total__lenS279);
      _M0L6bufferS273 = _M0L6bufferS280;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS282;
      int32_t _M0L6_2atmpS1224;
      int32_t _M0L10total__lenS283;
      uint16_t* _M0L6bufferS284;
      int32_t _M0L12digit__startS285;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS282
      = _M0FPB14radix__count32(_M0L3numS272, _M0L5radixS269);
      if (_M0L12is__negativeS271) {
        _M0L6_2atmpS1224 = 1;
      } else {
        _M0L6_2atmpS1224 = 0;
      }
      _M0L10total__lenS283 = _M0L10digit__lenS282 + _M0L6_2atmpS1224;
      _M0L6bufferS284
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS283, 0);
      if (_M0L12is__negativeS271) {
        _M0L12digit__startS285 = 1;
      } else {
        _M0L12digit__startS285 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS284, _M0L3numS272, _M0L12digit__startS285, _M0L10total__lenS283, _M0L5radixS269);
      _M0L6bufferS273 = _M0L6bufferS284;
      break;
    }
  }
  if (_M0L12is__negativeS271) {
    _M0L6bufferS273[0] = 45;
  }
  return _M0L6bufferS273;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS263,
  int32_t _M0L5radixS265
) {
  uint32_t _M0L4baseS264;
  uint32_t _M0L3numS266;
  int32_t _M0L5countS267;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS263 == 0u) {
    return 1;
  }
  _M0L4baseS264 = *(uint32_t*)&_M0L5radixS265;
  _M0L3numS266 = _M0L5valueS263;
  _M0L5countS267 = 0;
  while (1) {
    if (_M0L3numS266 > 0u) {
      uint32_t _M0L6_2atmpS1220 = _M0L3numS266 / _M0L4baseS264;
      int32_t _M0L6_2atmpS1221 = _M0L5countS267 + 1;
      _M0L3numS266 = _M0L6_2atmpS1220;
      _M0L5countS267 = _M0L6_2atmpS1221;
      continue;
    } else {
      return _M0L5countS267;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS261) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS261 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS262;
    int32_t _M0L6_2atmpS1219;
    int32_t _M0L6_2atmpS1218;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS262 = moonbit_clz32(_M0L5valueS261);
    _M0L6_2atmpS1219 = 31 - _M0L14leading__zerosS262;
    _M0L6_2atmpS1218 = _M0L6_2atmpS1219 / 4;
    return _M0L6_2atmpS1218 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS260) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS260 >= 100000u) {
    if (_M0L5valueS260 >= 10000000u) {
      if (_M0L5valueS260 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS260 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS260 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS260 >= 1000u) {
    if (_M0L5valueS260 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS260 >= 100u) {
    return 3;
  } else if (_M0L5valueS260 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS246,
  uint32_t _M0L3numS258,
  int32_t _M0L12digit__startS247,
  int32_t _M0L10total__lenS259
) {
  int32_t _M0L6_2atmpS1217;
  uint32_t _M0L3numS236;
  int32_t _M0L6offsetS237;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1217 = _M0L10total__lenS259 - _M0L12digit__startS247;
  _M0L3numS236 = _M0L3numS258;
  _M0L6offsetS237 = _M0L6_2atmpS1217;
  while (1) {
    if (_M0L3numS236 >= 10000u) {
      uint32_t _M0L1tS238 = _M0L3numS236 / 10000u;
      uint32_t _M0L6_2atmpS1194 = _M0L3numS236 % 10000u;
      int32_t _M0L1rS239 = *(int32_t*)&_M0L6_2atmpS1194;
      int32_t _M0L2d1S240 = _M0L1rS239 / 100;
      int32_t _M0L2d2S241 = _M0L1rS239 % 100;
      int32_t _M0L6_2atmpS1193 = _M0L2d1S240 / 10;
      int32_t _M0L6_2atmpS1192 = 48 + _M0L6_2atmpS1193;
      int32_t _M0L6d1__hiS242 = (uint16_t)_M0L6_2atmpS1192;
      int32_t _M0L6_2atmpS1191 = _M0L2d1S240 % 10;
      int32_t _M0L6_2atmpS1190 = 48 + _M0L6_2atmpS1191;
      int32_t _M0L6d1__loS243 = (uint16_t)_M0L6_2atmpS1190;
      int32_t _M0L6_2atmpS1189 = _M0L2d2S241 / 10;
      int32_t _M0L6_2atmpS1188 = 48 + _M0L6_2atmpS1189;
      int32_t _M0L6d2__hiS244 = (uint16_t)_M0L6_2atmpS1188;
      int32_t _M0L6_2atmpS1187 = _M0L2d2S241 % 10;
      int32_t _M0L6_2atmpS1186 = 48 + _M0L6_2atmpS1187;
      int32_t _M0L6d2__loS245 = (uint16_t)_M0L6_2atmpS1186;
      int32_t _M0L6_2atmpS1178 = _M0L12digit__startS247 + _M0L6offsetS237;
      int32_t _M0L6_2atmpS1177 = _M0L6_2atmpS1178 - 4;
      int32_t _M0L6_2atmpS1180;
      int32_t _M0L6_2atmpS1179;
      int32_t _M0L6_2atmpS1182;
      int32_t _M0L6_2atmpS1181;
      int32_t _M0L6_2atmpS1184;
      int32_t _M0L6_2atmpS1183;
      int32_t _M0L6_2atmpS1185;
      _M0L6bufferS246[_M0L6_2atmpS1177] = _M0L6d1__hiS242;
      _M0L6_2atmpS1180 = _M0L12digit__startS247 + _M0L6offsetS237;
      _M0L6_2atmpS1179 = _M0L6_2atmpS1180 - 3;
      _M0L6bufferS246[_M0L6_2atmpS1179] = _M0L6d1__loS243;
      _M0L6_2atmpS1182 = _M0L12digit__startS247 + _M0L6offsetS237;
      _M0L6_2atmpS1181 = _M0L6_2atmpS1182 - 2;
      _M0L6bufferS246[_M0L6_2atmpS1181] = _M0L6d2__hiS244;
      _M0L6_2atmpS1184 = _M0L12digit__startS247 + _M0L6offsetS237;
      _M0L6_2atmpS1183 = _M0L6_2atmpS1184 - 1;
      _M0L6bufferS246[_M0L6_2atmpS1183] = _M0L6d2__loS245;
      _M0L6_2atmpS1185 = _M0L6offsetS237 - 4;
      _M0L3numS236 = _M0L1tS238;
      _M0L6offsetS237 = _M0L6_2atmpS1185;
      continue;
    } else {
      int32_t _M0L6_2atmpS1216 = *(int32_t*)&_M0L3numS236;
      int32_t _M0L9remainingS249 = _M0L6_2atmpS1216;
      int32_t _M0L6offsetS250 = _M0L6offsetS237;
      while (1) {
        if (_M0L9remainingS249 >= 100) {
          int32_t _M0L1tS251 = _M0L9remainingS249 / 100;
          int32_t _M0L1dS252 = _M0L9remainingS249 % 100;
          int32_t _M0L6_2atmpS1203 = _M0L1dS252 / 10;
          int32_t _M0L6_2atmpS1202 = 48 + _M0L6_2atmpS1203;
          int32_t _M0L5d__hiS253 = (uint16_t)_M0L6_2atmpS1202;
          int32_t _M0L6_2atmpS1201 = _M0L1dS252 % 10;
          int32_t _M0L6_2atmpS1200 = 48 + _M0L6_2atmpS1201;
          int32_t _M0L5d__loS254 = (uint16_t)_M0L6_2atmpS1200;
          int32_t _M0L6_2atmpS1196 = _M0L12digit__startS247 + _M0L6offsetS250;
          int32_t _M0L6_2atmpS1195 = _M0L6_2atmpS1196 - 2;
          int32_t _M0L6_2atmpS1198;
          int32_t _M0L6_2atmpS1197;
          int32_t _M0L6_2atmpS1199;
          _M0L6bufferS246[_M0L6_2atmpS1195] = _M0L5d__hiS253;
          _M0L6_2atmpS1198 = _M0L12digit__startS247 + _M0L6offsetS250;
          _M0L6_2atmpS1197 = _M0L6_2atmpS1198 - 1;
          _M0L6bufferS246[_M0L6_2atmpS1197] = _M0L5d__loS254;
          _M0L6_2atmpS1199 = _M0L6offsetS250 - 2;
          _M0L9remainingS249 = _M0L1tS251;
          _M0L6offsetS250 = _M0L6_2atmpS1199;
          continue;
        } else if (_M0L9remainingS249 >= 10) {
          int32_t _M0L6_2atmpS1211 = _M0L9remainingS249 / 10;
          int32_t _M0L6_2atmpS1210 = 48 + _M0L6_2atmpS1211;
          int32_t _M0L5d__hiS256 = (uint16_t)_M0L6_2atmpS1210;
          int32_t _M0L6_2atmpS1209 = _M0L9remainingS249 % 10;
          int32_t _M0L6_2atmpS1208 = 48 + _M0L6_2atmpS1209;
          int32_t _M0L5d__loS257 = (uint16_t)_M0L6_2atmpS1208;
          int32_t _M0L6_2atmpS1205 = _M0L12digit__startS247 + _M0L6offsetS250;
          int32_t _M0L6_2atmpS1204 = _M0L6_2atmpS1205 - 2;
          int32_t _M0L6_2atmpS1207;
          int32_t _M0L6_2atmpS1206;
          _M0L6bufferS246[_M0L6_2atmpS1204] = _M0L5d__hiS256;
          _M0L6_2atmpS1207 = _M0L12digit__startS247 + _M0L6offsetS250;
          _M0L6_2atmpS1206 = _M0L6_2atmpS1207 - 1;
          _M0L6bufferS246[_M0L6_2atmpS1206] = _M0L5d__loS257;
        } else {
          int32_t _M0L6_2atmpS1215 = _M0L12digit__startS247 + _M0L6offsetS250;
          int32_t _M0L6_2atmpS1212 = _M0L6_2atmpS1215 - 1;
          int32_t _M0L6_2atmpS1214 = 48 + _M0L9remainingS249;
          int32_t _M0L6_2atmpS1213 = (uint16_t)_M0L6_2atmpS1214;
          _M0L6bufferS246[_M0L6_2atmpS1212] = _M0L6_2atmpS1213;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS226,
  uint32_t _M0L3numS230,
  int32_t _M0L12digit__startS227,
  int32_t _M0L10total__lenS229,
  int32_t _M0L5radixS220
) {
  uint32_t _M0L4baseS219;
  int32_t _M0L6_2atmpS1162;
  int32_t _M0L6_2atmpS1161;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS219 = *(uint32_t*)&_M0L5radixS220;
  _M0L6_2atmpS1162 = _M0L5radixS220 - 1;
  _M0L6_2atmpS1161 = _M0L5radixS220 & _M0L6_2atmpS1162;
  if (_M0L6_2atmpS1161 == 0) {
    int32_t _M0L5shiftS221;
    uint32_t _M0L4maskS222;
    int32_t _M0L6_2atmpS1169;
    int32_t _M0L6offsetS223;
    uint32_t _M0L1nS224;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS221 = moonbit_ctz32(_M0L5radixS220);
    _M0L4maskS222 = _M0L4baseS219 - 1u;
    _M0L6_2atmpS1169 = _M0L10total__lenS229 - _M0L12digit__startS227;
    _M0L6offsetS223 = _M0L6_2atmpS1169;
    _M0L1nS224 = _M0L3numS230;
    while (1) {
      if (_M0L1nS224 > 0u) {
        uint32_t _M0L6_2atmpS1168 = _M0L1nS224 & _M0L4maskS222;
        int32_t _M0L5digitS225 = *(int32_t*)&_M0L6_2atmpS1168;
        int32_t _M0L6_2atmpS1165 = _M0L12digit__startS227 + _M0L6offsetS223;
        int32_t _M0L6_2atmpS1163 = _M0L6_2atmpS1165 - 1;
        int32_t _M0L6_2atmpS1164 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS225];
        int32_t _M0L6_2atmpS1166;
        uint32_t _M0L6_2atmpS1167;
        _M0L6bufferS226[_M0L6_2atmpS1163] = _M0L6_2atmpS1164;
        _M0L6_2atmpS1166 = _M0L6offsetS223 - 1;
        _M0L6_2atmpS1167 = _M0L1nS224 >> (_M0L5shiftS221 & 31);
        _M0L6offsetS223 = _M0L6_2atmpS1166;
        _M0L1nS224 = _M0L6_2atmpS1167;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1176 = _M0L10total__lenS229 - _M0L12digit__startS227;
    int32_t _M0L6offsetS231 = _M0L6_2atmpS1176;
    uint32_t _M0L1nS232 = _M0L3numS230;
    while (1) {
      if (_M0L1nS232 > 0u) {
        uint32_t _M0L1qS233 = _M0L1nS232 / _M0L4baseS219;
        uint32_t _M0L6_2atmpS1175 = _M0L1qS233 * _M0L4baseS219;
        uint32_t _M0L6_2atmpS1174 = _M0L1nS232 - _M0L6_2atmpS1175;
        int32_t _M0L5digitS234 = *(int32_t*)&_M0L6_2atmpS1174;
        int32_t _M0L6_2atmpS1172 = _M0L12digit__startS227 + _M0L6offsetS231;
        int32_t _M0L6_2atmpS1170 = _M0L6_2atmpS1172 - 1;
        int32_t _M0L6_2atmpS1171 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS234];
        int32_t _M0L6_2atmpS1173;
        _M0L6bufferS226[_M0L6_2atmpS1170] = _M0L6_2atmpS1171;
        _M0L6_2atmpS1173 = _M0L6offsetS231 - 1;
        _M0L6offsetS231 = _M0L6_2atmpS1173;
        _M0L1nS232 = _M0L1qS233;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS213,
  uint32_t _M0L3numS218,
  int32_t _M0L12digit__startS214,
  int32_t _M0L10total__lenS217
) {
  int32_t _M0L6_2atmpS1160;
  int32_t _M0L6offsetS208;
  uint32_t _M0L1nS209;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1160 = _M0L10total__lenS217 - _M0L12digit__startS214;
  _M0L6offsetS208 = _M0L6_2atmpS1160;
  _M0L1nS209 = _M0L3numS218;
  while (1) {
    if (_M0L6offsetS208 >= 2) {
      uint32_t _M0L6_2atmpS1157 = _M0L1nS209 & 255u;
      int32_t _M0L9byte__valS210 = *(int32_t*)&_M0L6_2atmpS1157;
      int32_t _M0L2hiS211 = _M0L9byte__valS210 / 16;
      int32_t _M0L2loS212 = _M0L9byte__valS210 % 16;
      int32_t _M0L6_2atmpS1151 = _M0L12digit__startS214 + _M0L6offsetS208;
      int32_t _M0L6_2atmpS1149 = _M0L6_2atmpS1151 - 2;
      int32_t _M0L6_2atmpS1150 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L2hiS211];
      int32_t _M0L6_2atmpS1154;
      int32_t _M0L6_2atmpS1152;
      int32_t _M0L6_2atmpS1153;
      int32_t _M0L6_2atmpS1155;
      uint32_t _M0L6_2atmpS1156;
      _M0L6bufferS213[_M0L6_2atmpS1149] = _M0L6_2atmpS1150;
      _M0L6_2atmpS1154 = _M0L12digit__startS214 + _M0L6offsetS208;
      _M0L6_2atmpS1152 = _M0L6_2atmpS1154 - 1;
      _M0L6_2atmpS1153
      = ((moonbit_string_t)moonbit_string_literal_19.data)[
        _M0L2loS212
      ];
      _M0L6bufferS213[_M0L6_2atmpS1152] = _M0L6_2atmpS1153;
      _M0L6_2atmpS1155 = _M0L6offsetS208 - 2;
      _M0L6_2atmpS1156 = _M0L1nS209 >> 8;
      _M0L6offsetS208 = _M0L6_2atmpS1155;
      _M0L1nS209 = _M0L6_2atmpS1156;
      continue;
    } else if (_M0L6offsetS208 == 1) {
      uint32_t _M0L6_2atmpS1159 = _M0L1nS209 & 15u;
      int32_t _M0L6nibbleS216 = *(int32_t*)&_M0L6_2atmpS1159;
      int32_t _M0L6_2atmpS1158 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L6nibbleS216];
      _M0L6bufferS213[_M0L12digit__startS214] = _M0L6_2atmpS1158;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS207
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS206;
  struct _M0TPB6Logger _M0L6_2atmpS1148;
  moonbit_string_t _result_1948;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS206 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS206);
  _M0L6_2atmpS1148
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS206
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS207, _M0L6_2atmpS1148);
  if (_M0L6_2atmpS1148.$1) {
    moonbit_decref(_M0L6_2atmpS1148.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_1948 = _M0MPB13StringBuilder10to__string(_M0L6loggerS206);
  moonbit_decref_cycle_free(_M0L6loggerS206);
  return _result_1948;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS197,
  struct _M0TPB6Logger _M0L6loggerS196
) {
  moonbit_string_t _M0L6_2atmpS1143;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1143 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS197);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS196.$0->$method_0(_M0L6loggerS196.$1, _M0L6_2atmpS1143);
  moonbit_decref_cycle_free(_M0L6_2atmpS1143);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float _M0L4selfS199,
  struct _M0TPB6Logger _M0L6loggerS198
) {
  moonbit_string_t _M0L6_2atmpS1144;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1144 = _M0IPC15float5FloatPB4Show10to__string(_M0L4selfS199);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS198.$0->$method_0(_M0L6loggerS198.$1, _M0L6_2atmpS1144);
  moonbit_decref_cycle_free(_M0L6_2atmpS1144);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGbE(
  int32_t _M0L4selfS201,
  struct _M0TPB6Logger _M0L6loggerS200
) {
  moonbit_string_t _M0L6_2atmpS1145;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1145 = _M0IPC14bool4BoolPB4Show10to__string(_M0L4selfS201);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS200.$0->$method_0(_M0L6loggerS200.$1, _M0L6_2atmpS1145);
  moonbit_decref_cycle_free(_M0L6_2atmpS1145);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS203,
  struct _M0TPB6Logger _M0L6loggerS202
) {
  moonbit_string_t _M0L6_2atmpS1146;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1146 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS203);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS202.$0->$method_0(_M0L6loggerS202.$1, _M0L6_2atmpS1146);
  moonbit_decref_cycle_free(_M0L6_2atmpS1146);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS205,
  struct _M0TPB6Logger _M0L6loggerS204
) {
  moonbit_string_t _M0L6_2atmpS1147;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1147 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS205);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS204.$0->$method_0(_M0L6loggerS204.$1, _M0L6_2atmpS1147);
  moonbit_decref_cycle_free(_M0L6_2atmpS1147);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS195
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS195.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS194
) {
  moonbit_string_t _M0L8_2afieldS1857;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1857 = _M0L4selfS194.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1857);
  return _M0L8_2afieldS1857;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS190,
  moonbit_string_t _M0L5valueS191,
  int32_t _M0L5startS192,
  int32_t _M0L3lenS193
) {
  int32_t _M0L6_2atmpS1142;
  int64_t _M0L6_2atmpS1141;
  struct _M0TPC16string10StringView _M0L6_2atmpS1140;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1142 = _M0L5startS192 + _M0L3lenS193;
  _M0L6_2atmpS1141 = (int64_t)_M0L6_2atmpS1142;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1140
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS191, _M0L5startS192, _M0L6_2atmpS1141);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS190, _M0L6_2atmpS1140);
  moonbit_decref_cycle_free(_M0L6_2atmpS1140.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS183,
  int32_t _M0L5startS185,
  int64_t _M0L3endS187
) {
  int32_t _M0L3lenS182;
  int32_t _M0Lm2loS184;
  int32_t _M0Lm2hiS186;
  int32_t _M0L6_2atmpS1124;
  int32_t _if__result_1949;
  int32_t _M0L6_2atmpS1132;
  int32_t _if__result_1950;
  int32_t _M0L6_2atmpS1134;
  int32_t _M0L6_2atmpS1135;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS182 = Moonbit_array_length(_M0L4selfS183);
  if (_M0L5startS185 < 0) {
    _M0Lm2loS184 = 0;
  } else if (_M0L5startS185 > _M0L3lenS182) {
    _M0Lm2loS184 = _M0L3lenS182;
  } else {
    _M0Lm2loS184 = _M0L5startS185;
  }
  if (_M0L3endS187 == 4294967296ll) {
    _M0Lm2hiS186 = _M0L3lenS182;
  } else {
    int64_t _M0L7_2aSomeS188 = _M0L3endS187;
    int32_t _M0L4_2aeS189 = (int32_t)_M0L7_2aSomeS188;
    if (_M0L4_2aeS189 < 0) {
      _M0Lm2hiS186 = 0;
    } else if (_M0L4_2aeS189 > _M0L3lenS182) {
      _M0Lm2hiS186 = _M0L3lenS182;
    } else {
      _M0Lm2hiS186 = _M0L4_2aeS189;
    }
  }
  _M0L6_2atmpS1124 = _M0Lm2loS184;
  if (_M0L6_2atmpS1124 > 0) {
    int32_t _M0L6_2atmpS1123 = _M0Lm2loS184;
    if (_M0L6_2atmpS1123 < _M0L3lenS182) {
      int32_t _M0L6_2atmpS1122 = _M0Lm2loS184;
      int32_t _M0L6_2atmpS1121 = _M0L4selfS183[_M0L6_2atmpS1122];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1121)) {
        int32_t _M0L6_2atmpS1120 = _M0Lm2loS184;
        int32_t _M0L6_2atmpS1119 = _M0L6_2atmpS1120 - 1;
        int32_t _M0L6_2atmpS1118 = _M0L4selfS183[_M0L6_2atmpS1119];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1949
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1118);
      } else {
        _if__result_1949 = 0;
      }
    } else {
      _if__result_1949 = 0;
    }
  } else {
    _if__result_1949 = 0;
  }
  if (_if__result_1949) {
    int32_t _M0L6_2atmpS1125 = _M0Lm2loS184;
    _M0Lm2loS184 = _M0L6_2atmpS1125 + 1;
  }
  _M0L6_2atmpS1132 = _M0Lm2hiS186;
  if (_M0L6_2atmpS1132 > 0) {
    int32_t _M0L6_2atmpS1131 = _M0Lm2hiS186;
    if (_M0L6_2atmpS1131 < _M0L3lenS182) {
      int32_t _M0L6_2atmpS1130 = _M0Lm2hiS186;
      int32_t _M0L6_2atmpS1129 = _M0L4selfS183[_M0L6_2atmpS1130];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1129)) {
        int32_t _M0L6_2atmpS1128 = _M0Lm2hiS186;
        int32_t _M0L6_2atmpS1127 = _M0L6_2atmpS1128 - 1;
        int32_t _M0L6_2atmpS1126 = _M0L4selfS183[_M0L6_2atmpS1127];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1950
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1126);
      } else {
        _if__result_1950 = 0;
      }
    } else {
      _if__result_1950 = 0;
    }
  } else {
    _if__result_1950 = 0;
  }
  if (_if__result_1950) {
    int32_t _M0L6_2atmpS1133 = _M0Lm2hiS186;
    _M0Lm2hiS186 = _M0L6_2atmpS1133 - 1;
  }
  _M0L6_2atmpS1134 = _M0Lm2loS184;
  _M0L6_2atmpS1135 = _M0Lm2hiS186;
  if (_M0L6_2atmpS1134 >= _M0L6_2atmpS1135) {
    int32_t _M0L6_2atmpS1136 = _M0Lm2loS184;
    int32_t _M0L6_2atmpS1137 = _M0Lm2loS184;
    moonbit_incref_cycle_free(_M0L4selfS183);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS183,
                                                 .$1 = _M0L6_2atmpS1136,
                                                 .$2 = _M0L6_2atmpS1137};
  } else {
    int32_t _M0L6_2atmpS1138 = _M0Lm2loS184;
    int32_t _M0L6_2atmpS1139 = _M0Lm2hiS186;
    moonbit_incref_cycle_free(_M0L4selfS183);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS183,
                                                 .$1 = _M0L6_2atmpS1138,
                                                 .$2 = _M0L6_2atmpS1139};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS181,
  struct _M0TPB4Show _M0L4showS180
) {
  struct _M0TPB6Logger _M0L6_2atmpS1117;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS181);
  _M0L6_2atmpS1117
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS181
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS180.$0->$method_0(_M0L4showS180.$1, _M0L6_2atmpS1117);
  if (_M0L6_2atmpS1117.$1) {
    moonbit_decref(_M0L6_2atmpS1117.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS179,
  struct _M0TPB4Show _M0L4showS178
) {
  struct _M0TPB6Logger _M0L6_2atmpS1116;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS179);
  _M0L6_2atmpS1116
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS179
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS178.$0->$method_0(_M0L4showS178.$1, _M0L6_2atmpS1116);
  if (_M0L6_2atmpS1116.$1) {
    moonbit_decref(_M0L6_2atmpS1116.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS177) {
  int64_t _M0L6_2atmpS1115;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1115 = (int64_t)_M0L4selfS177;
  return *(uint64_t*)&_M0L6_2atmpS1115;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS175,
  int32_t _M0L5quoteS176
) {
  struct _M0TPB13StringBuilder* _M0L3bufS174;
  int32_t _M0L6_2atmpS1114;
  struct _M0TPC16string10StringView _M0L6_2atmpS1112;
  struct _M0TPB6Logger _M0L6_2atmpS1113;
  moonbit_string_t _result_1951;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS174 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1114 = Moonbit_array_length(_M0L4selfS175);
  moonbit_incref_cycle_free(_M0L4selfS175);
  _M0L6_2atmpS1112
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS175, .$1 = 0, .$2 = _M0L6_2atmpS1114
  };
  moonbit_incref_cycle_free(_M0L3bufS174);
  _M0L6_2atmpS1113
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS174
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1112, _M0L6_2atmpS1113, _M0L5quoteS176);
  moonbit_decref_cycle_free(_M0L6_2atmpS1112.$0);
  if (_M0L6_2atmpS1113.$1) {
    moonbit_decref(_M0L6_2atmpS1113.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1951 = _M0MPB13StringBuilder10to__string(_M0L3bufS174);
  moonbit_decref_cycle_free(_M0L3bufS174);
  return _result_1951;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS166,
  struct _M0TPB6Logger _M0L6loggerS164,
  int32_t _M0L5quoteS163
) {
  int32_t _M0L3endS1110;
  int32_t _M0L5startS1111;
  int32_t _M0L3lenS165;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS167;
  int32_t _M0L1iS168;
  int32_t _M0L3segS169;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS163) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS164.$0->$method_3(_M0L6loggerS164.$1, 34);
  }
  _M0L3endS1110 = _M0L4selfS166.$2;
  _M0L5startS1111 = _M0L4selfS166.$1;
  _M0L3lenS165 = _M0L3endS1110 - _M0L5startS1111;
  moonbit_incref_cycle_free(_M0L4selfS166.$0);
  if (_M0L6loggerS164.$1) {
    moonbit_incref(_M0L6loggerS164.$1);
  }
  _M0L6_2aenvS167
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS167)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 41, 0);
  _M0L6_2aenvS167->$0 = _M0L4selfS166;
  _M0L6_2aenvS167->$1 = _M0L6loggerS164;
  _M0L1iS168 = 0;
  _M0L3segS169 = 0;
  _2afor_170:;
  while (1) {
    moonbit_string_t _M0L3strS1107;
    int32_t _M0L5startS1109;
    int32_t _M0L6_2atmpS1108;
    int32_t _M0L4codeS171;
    int32_t _M0L1cS173;
    int32_t _M0L6_2atmpS1091;
    int32_t _M0L6_2atmpS1092;
    int32_t _M0L6_2atmpS1093;
    if (_M0L1iS168 >= _M0L3lenS165) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS167, _M0L3segS169, _M0L1iS168);
      moonbit_decref_cycle_free(_M0L6_2aenvS167);
      break;
    }
    _M0L3strS1107 = _M0L4selfS166.$0;
    _M0L5startS1109 = _M0L4selfS166.$1;
    _M0L6_2atmpS1108 = _M0L5startS1109 + _M0L1iS168;
    _M0L4codeS171 = _M0L3strS1107[_M0L6_2atmpS1108];
    switch (_M0L4codeS171) {
      case 34: {
        _M0L1cS173 = _M0L4codeS171;
        goto join_172;
        break;
      }
      
      case 92: {
        _M0L1cS173 = _M0L4codeS171;
        goto join_172;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1094;
        int32_t _M0L6_2atmpS1095;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS167, _M0L3segS169, _M0L1iS168);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS164.$0->$method_0(_M0L6loggerS164.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1094 = _M0L1iS168 + 1;
        _M0L6_2atmpS1095 = _M0L1iS168 + 1;
        _M0L1iS168 = _M0L6_2atmpS1094;
        _M0L3segS169 = _M0L6_2atmpS1095;
        goto _2afor_170;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1096;
        int32_t _M0L6_2atmpS1097;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS167, _M0L3segS169, _M0L1iS168);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS164.$0->$method_0(_M0L6loggerS164.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1096 = _M0L1iS168 + 1;
        _M0L6_2atmpS1097 = _M0L1iS168 + 1;
        _M0L1iS168 = _M0L6_2atmpS1096;
        _M0L3segS169 = _M0L6_2atmpS1097;
        goto _2afor_170;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1098;
        int32_t _M0L6_2atmpS1099;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS167, _M0L3segS169, _M0L1iS168);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS164.$0->$method_0(_M0L6loggerS164.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1098 = _M0L1iS168 + 1;
        _M0L6_2atmpS1099 = _M0L1iS168 + 1;
        _M0L1iS168 = _M0L6_2atmpS1098;
        _M0L3segS169 = _M0L6_2atmpS1099;
        goto _2afor_170;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1100;
        int32_t _M0L6_2atmpS1101;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS167, _M0L3segS169, _M0L1iS168);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS164.$0->$method_0(_M0L6loggerS164.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1100 = _M0L1iS168 + 1;
        _M0L6_2atmpS1101 = _M0L1iS168 + 1;
        _M0L1iS168 = _M0L6_2atmpS1100;
        _M0L3segS169 = _M0L6_2atmpS1101;
        goto _2afor_170;
        break;
      }
      default: {
        if (_M0L4codeS171 < 32) {
          int32_t _M0L6_2atmpS1103;
          moonbit_string_t _M0L6_2atmpS1102;
          int32_t _M0L6_2atmpS1104;
          int32_t _M0L6_2atmpS1105;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS167, _M0L3segS169, _M0L1iS168);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS164.$0->$method_0(_M0L6loggerS164.$1, (moonbit_string_t)moonbit_string_literal_24.data);
          _M0L6_2atmpS1103 = _M0L4codeS171 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1102 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1103);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS164.$0->$method_0(_M0L6loggerS164.$1, _M0L6_2atmpS1102);
          moonbit_decref_cycle_free(_M0L6_2atmpS1102);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS164.$0->$method_0(_M0L6loggerS164.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1104 = _M0L1iS168 + 1;
          _M0L6_2atmpS1105 = _M0L1iS168 + 1;
          _M0L1iS168 = _M0L6_2atmpS1104;
          _M0L3segS169 = _M0L6_2atmpS1105;
          goto _2afor_170;
        } else {
          int32_t _M0L6_2atmpS1106 = _M0L1iS168 + 1;
          int32_t _tmp_1954 = _M0L3segS169;
          _M0L1iS168 = _M0L6_2atmpS1106;
          _M0L3segS169 = _tmp_1954;
          goto _2afor_170;
        }
        break;
      }
    }
    goto joinlet_1953;
    join_172:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS167, _M0L3segS169, _M0L1iS168);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS164.$0->$method_3(_M0L6loggerS164.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1091 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS173);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS164.$0->$method_3(_M0L6loggerS164.$1, _M0L6_2atmpS1091);
    _M0L6_2atmpS1092 = _M0L1iS168 + 1;
    _M0L6_2atmpS1093 = _M0L1iS168 + 1;
    _M0L1iS168 = _M0L6_2atmpS1092;
    _M0L3segS169 = _M0L6_2atmpS1093;
    continue;
    joinlet_1953:;
    break;
  }
  if (_M0L5quoteS163) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS164.$0->$method_3(_M0L6loggerS164.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS159,
  int32_t _M0L3segS162,
  int32_t _M0L1iS161
) {
  struct _M0TPB6Logger _M0L6loggerS158;
  struct _M0TPC16string10StringView _M0L4selfS160;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS158 = _M0L6_2aenvS159->$1;
  _M0L4selfS160 = _M0L6_2aenvS159->$0;
  if (_M0L1iS161 > _M0L3segS162) {
    int64_t _M0L6_2atmpS1090 = (int64_t)_M0L1iS161;
    struct _M0TPC16string10StringView _M0L6_2atmpS1089;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1089
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS160, _M0L3segS162, _M0L6_2atmpS1090);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS158.$0->$method_2(_M0L6loggerS158.$1, _M0L6_2atmpS1089);
    moonbit_decref_cycle_free(_M0L6_2atmpS1089.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS149,
  int32_t _M0L5startS151,
  int64_t _M0L3endS153
) {
  int32_t _M0L3endS1087;
  int32_t _M0L5startS1088;
  int32_t _M0L3lenS148;
  int32_t _M0Lm2loS150;
  int32_t _M0Lm2hiS152;
  moonbit_string_t _M0L3strS156;
  int32_t _M0L4baseS157;
  int32_t _M0L6_2atmpS1065;
  int32_t _if__result_1955;
  int32_t _M0L6_2atmpS1075;
  int32_t _if__result_1956;
  int32_t _M0L6_2atmpS1077;
  int32_t _M0L6_2atmpS1078;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1087 = _M0L4selfS149.$2;
  _M0L5startS1088 = _M0L4selfS149.$1;
  _M0L3lenS148 = _M0L3endS1087 - _M0L5startS1088;
  if (_M0L5startS151 < 0) {
    _M0Lm2loS150 = 0;
  } else if (_M0L5startS151 > _M0L3lenS148) {
    _M0Lm2loS150 = _M0L3lenS148;
  } else {
    _M0Lm2loS150 = _M0L5startS151;
  }
  if (_M0L3endS153 == 4294967296ll) {
    _M0Lm2hiS152 = _M0L3lenS148;
  } else {
    int64_t _M0L7_2aSomeS154 = _M0L3endS153;
    int32_t _M0L4_2aeS155 = (int32_t)_M0L7_2aSomeS154;
    if (_M0L4_2aeS155 < 0) {
      _M0Lm2hiS152 = 0;
    } else if (_M0L4_2aeS155 > _M0L3lenS148) {
      _M0Lm2hiS152 = _M0L3lenS148;
    } else {
      _M0Lm2hiS152 = _M0L4_2aeS155;
    }
  }
  _M0L3strS156 = _M0L4selfS149.$0;
  _M0L4baseS157 = _M0L4selfS149.$1;
  _M0L6_2atmpS1065 = _M0Lm2loS150;
  if (_M0L6_2atmpS1065 > 0) {
    int32_t _M0L6_2atmpS1064 = _M0Lm2loS150;
    if (_M0L6_2atmpS1064 < _M0L3lenS148) {
      int32_t _M0L6_2atmpS1063 = _M0Lm2loS150;
      int32_t _M0L6_2atmpS1062 = _M0L4baseS157 + _M0L6_2atmpS1063;
      int32_t _M0L6_2atmpS1061 = _M0L3strS156[_M0L6_2atmpS1062];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1061)) {
        int32_t _M0L6_2atmpS1060 = _M0Lm2loS150;
        int32_t _M0L6_2atmpS1059 = _M0L4baseS157 + _M0L6_2atmpS1060;
        int32_t _M0L6_2atmpS1058 = _M0L6_2atmpS1059 - 1;
        int32_t _M0L6_2atmpS1057 = _M0L3strS156[_M0L6_2atmpS1058];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1955
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1057);
      } else {
        _if__result_1955 = 0;
      }
    } else {
      _if__result_1955 = 0;
    }
  } else {
    _if__result_1955 = 0;
  }
  if (_if__result_1955) {
    int32_t _M0L6_2atmpS1066 = _M0Lm2loS150;
    _M0Lm2loS150 = _M0L6_2atmpS1066 + 1;
  }
  _M0L6_2atmpS1075 = _M0Lm2hiS152;
  if (_M0L6_2atmpS1075 > 0) {
    int32_t _M0L6_2atmpS1074 = _M0Lm2hiS152;
    if (_M0L6_2atmpS1074 < _M0L3lenS148) {
      int32_t _M0L6_2atmpS1073 = _M0Lm2hiS152;
      int32_t _M0L6_2atmpS1072 = _M0L4baseS157 + _M0L6_2atmpS1073;
      int32_t _M0L6_2atmpS1071 = _M0L3strS156[_M0L6_2atmpS1072];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1071)) {
        int32_t _M0L6_2atmpS1070 = _M0Lm2hiS152;
        int32_t _M0L6_2atmpS1069 = _M0L4baseS157 + _M0L6_2atmpS1070;
        int32_t _M0L6_2atmpS1068 = _M0L6_2atmpS1069 - 1;
        int32_t _M0L6_2atmpS1067 = _M0L3strS156[_M0L6_2atmpS1068];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1956
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1067);
      } else {
        _if__result_1956 = 0;
      }
    } else {
      _if__result_1956 = 0;
    }
  } else {
    _if__result_1956 = 0;
  }
  if (_if__result_1956) {
    int32_t _M0L6_2atmpS1076 = _M0Lm2hiS152;
    _M0Lm2hiS152 = _M0L6_2atmpS1076 - 1;
  }
  _M0L6_2atmpS1077 = _M0Lm2loS150;
  _M0L6_2atmpS1078 = _M0Lm2hiS152;
  if (_M0L6_2atmpS1077 >= _M0L6_2atmpS1078) {
    int32_t _M0L6_2atmpS1082 = _M0Lm2loS150;
    int32_t _M0L6_2atmpS1079 = _M0L4baseS157 + _M0L6_2atmpS1082;
    int32_t _M0L6_2atmpS1081 = _M0Lm2loS150;
    int32_t _M0L6_2atmpS1080 = _M0L4baseS157 + _M0L6_2atmpS1081;
    moonbit_incref_cycle_free(_M0L3strS156);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS156,
                                                 .$1 = _M0L6_2atmpS1079,
                                                 .$2 = _M0L6_2atmpS1080};
  } else {
    int32_t _M0L6_2atmpS1086 = _M0Lm2loS150;
    int32_t _M0L6_2atmpS1083 = _M0L4baseS157 + _M0L6_2atmpS1086;
    int32_t _M0L6_2atmpS1085 = _M0Lm2hiS152;
    int32_t _M0L6_2atmpS1084 = _M0L4baseS157 + _M0L6_2atmpS1085;
    moonbit_incref_cycle_free(_M0L3strS156);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS156,
                                                 .$1 = _M0L6_2atmpS1083,
                                                 .$2 = _M0L6_2atmpS1084};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS147) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS146;
  int32_t _M0L6_2atmpS1054;
  int32_t _M0L6_2atmpS1053;
  int32_t _M0L6_2atmpS1056;
  int32_t _M0L6_2atmpS1055;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1052;
  moonbit_string_t _result_1957;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS146 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1054 = _M0IPC14byte4BytePB3Div3div(_M0L1bS147, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1053
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1054);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS146, _M0L6_2atmpS1053);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1056 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS147, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1055
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1056);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS146, _M0L6_2atmpS1055);
  _M0L6_2atmpS1052 = _M0L7_2aselfS146;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1957 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1052);
  moonbit_decref_cycle_free(_M0L6_2atmpS1052);
  return _result_1957;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS145) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS145 < 10) {
    int32_t _M0L6_2atmpS1049;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1049 = _M0IPC14byte4BytePB3Add3add(_M0L1iS145, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1049);
  } else {
    int32_t _M0L6_2atmpS1051;
    int32_t _M0L6_2atmpS1050;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1051 = _M0IPC14byte4BytePB3Add3add(_M0L1iS145, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1050 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1051, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1050);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS143,
  int32_t _M0L4thatS144
) {
  int32_t _M0L6_2atmpS1047;
  int32_t _M0L6_2atmpS1048;
  int32_t _M0L6_2atmpS1046;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1047 = (int32_t)_M0L4selfS143;
  _M0L6_2atmpS1048 = (int32_t)_M0L4thatS144;
  _M0L6_2atmpS1046 = _M0L6_2atmpS1047 - _M0L6_2atmpS1048;
  return _M0L6_2atmpS1046 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS141,
  int32_t _M0L4thatS142
) {
  int32_t _M0L6_2atmpS1044;
  int32_t _M0L6_2atmpS1045;
  int32_t _M0L6_2atmpS1043;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1044 = (int32_t)_M0L4selfS141;
  _M0L6_2atmpS1045 = (int32_t)_M0L4thatS142;
  _M0L6_2atmpS1043 = _M0L6_2atmpS1044 % _M0L6_2atmpS1045;
  return _M0L6_2atmpS1043 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS139,
  int32_t _M0L4thatS140
) {
  int32_t _M0L6_2atmpS1041;
  int32_t _M0L6_2atmpS1042;
  int32_t _M0L6_2atmpS1040;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1041 = (int32_t)_M0L4selfS139;
  _M0L6_2atmpS1042 = (int32_t)_M0L4thatS140;
  _M0L6_2atmpS1040 = _M0L6_2atmpS1041 / _M0L6_2atmpS1042;
  return _M0L6_2atmpS1040 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS137,
  int32_t _M0L4thatS138
) {
  int32_t _M0L6_2atmpS1038;
  int32_t _M0L6_2atmpS1039;
  int32_t _M0L6_2atmpS1037;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1038 = (int32_t)_M0L4selfS137;
  _M0L6_2atmpS1039 = (int32_t)_M0L4thatS138;
  _M0L6_2atmpS1037 = _M0L6_2atmpS1038 + _M0L6_2atmpS1039;
  return _M0L6_2atmpS1037 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS136) {
  int32_t _M0L6_2atmpS1036;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1036 = (int32_t)_M0L4selfS136;
  return _M0L6_2atmpS1036;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS135) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS135 >= 56320 && _M0L4selfS135 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS134) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS134 >= 55296 && _M0L4selfS134 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS133,
  moonbit_string_t _M0L3strS131
) {
  int32_t _M0L8str__lenS130;
  int32_t _M0L3lenS1035;
  int32_t _M0L8requiredS132;
  uint16_t* _M0L4dataS1030;
  int32_t _M0L6_2atmpS1029;
  int32_t _if__result_1958;
  uint16_t* _M0L4dataS1031;
  int32_t _M0L3lenS1032;
  int32_t _M0L3lenS1034;
  int32_t _M0L6_2atmpS1033;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS130 = Moonbit_array_length(_M0L3strS131);
  if (_M0L8str__lenS130 == 0) {
    return 0;
  }
  _M0L3lenS1035 = _M0L4selfS133->$1;
  _M0L8requiredS132 = _M0L3lenS1035 + _M0L8str__lenS130;
  _M0L4dataS1030 = _M0L4selfS133->$0;
  _M0L6_2atmpS1029 = Moonbit_array_length(_M0L4dataS1030);
  if (_M0L8requiredS132 > _M0L6_2atmpS1029) {
    _if__result_1958 = 1;
  } else {
    int32_t _M0L3lenS1028 = _M0L4selfS133->$1;
    _if__result_1958 = _M0L8requiredS132 < _M0L3lenS1028;
  }
  if (_if__result_1958) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS133, _M0L8requiredS132);
  }
  _M0L4dataS1031 = _M0L4selfS133->$0;
  _M0L3lenS1032 = _M0L4selfS133->$1;
  moonbit_incref_cycle_free(_M0L4dataS1031);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1031, _M0L3lenS1032, _M0L3strS131, 0, _M0L8str__lenS130);
  moonbit_decref_cycle_free(_M0L4dataS1031);
  _M0L3lenS1034 = _M0L4selfS133->$1;
  _M0L6_2atmpS1033 = _M0L3lenS1034 + _M0L8str__lenS130;
  _M0L4selfS133->$1 = _M0L6_2atmpS1033;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS126,
  int32_t _M0L11dst__offsetS129,
  moonbit_string_t _M0L3strS127,
  int32_t _M0L11str__offsetS122,
  int32_t _M0L3lenS123
) {
  int32_t _M0L16end__str__offsetS121;
  int32_t _M0L1iS124;
  int32_t _M0L1jS125;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS121 = _M0L11str__offsetS122 + _M0L3lenS123;
  _M0L1iS124 = _M0L11str__offsetS122;
  _M0L1jS125 = _M0L11dst__offsetS129;
  while (1) {
    if (_M0L1iS124 < _M0L16end__str__offsetS121) {
      int32_t _M0L6_2atmpS1025 = _M0L3strS127[_M0L1iS124];
      int32_t _M0L6_2atmpS1026;
      int32_t _M0L6_2atmpS1027;
      _M0L4selfS126[_M0L1jS125] = _M0L6_2atmpS1025;
      _M0L6_2atmpS1026 = _M0L1iS124 + 1;
      _M0L6_2atmpS1027 = _M0L1jS125 + 1;
      _M0L1iS124 = _M0L6_2atmpS1026;
      _M0L1jS125 = _M0L6_2atmpS1027;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS119,
  int32_t _M0L2chS118
) {
  uint32_t _M0L4codeS117;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS117 = _M0MPC14char4Char8to__uint(_M0L2chS118);
  if (_M0L4codeS117 <= 65535u) {
    int32_t _M0L3lenS996 = _M0L4selfS119->$1;
    uint16_t* _M0L4dataS998 = _M0L4selfS119->$0;
    int32_t _M0L6_2atmpS997 = Moonbit_array_length(_M0L4dataS998);
    uint16_t* _M0L4dataS1001;
    int32_t _M0L3lenS1002;
    int32_t _M0L6_2atmpS1003;
    int32_t _M0L3lenS1005;
    int32_t _M0L6_2atmpS1004;
    if (_M0L3lenS996 >= _M0L6_2atmpS997) {
      int32_t _M0L3lenS1000 = _M0L4selfS119->$1;
      int32_t _M0L6_2atmpS999 = _M0L3lenS1000 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS119, _M0L6_2atmpS999);
    }
    _M0L4dataS1001 = _M0L4selfS119->$0;
    _M0L3lenS1002 = _M0L4selfS119->$1;
    moonbit_incref_cycle_free(_M0L4dataS1001);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1003 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS117);
    if (
      _M0L3lenS1002 < 0
      || _M0L3lenS1002 >= Moonbit_array_length(_M0L4dataS1001)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1001[_M0L3lenS1002] = _M0L6_2atmpS1003;
    moonbit_decref_cycle_free(_M0L4dataS1001);
    _M0L3lenS1005 = _M0L4selfS119->$1;
    _M0L6_2atmpS1004 = _M0L3lenS1005 + 1;
    _M0L4selfS119->$1 = _M0L6_2atmpS1004;
  } else if (_M0L4codeS117 <= 1114111u) {
    uint16_t* _M0L4dataS1009 = _M0L4selfS119->$0;
    int32_t _M0L6_2atmpS1007 = Moonbit_array_length(_M0L4dataS1009);
    int32_t _M0L3lenS1008 = _M0L4selfS119->$1;
    int32_t _M0L6_2atmpS1006 = _M0L6_2atmpS1007 - _M0L3lenS1008;
    uint32_t _M0L4codeS120;
    uint16_t* _M0L4dataS1012;
    int32_t _M0L3lenS1013;
    uint32_t _M0L6_2atmpS1016;
    uint32_t _M0L6_2atmpS1015;
    int32_t _M0L6_2atmpS1014;
    uint16_t* _M0L4dataS1017;
    int32_t _M0L3lenS1022;
    int32_t _M0L6_2atmpS1018;
    uint32_t _M0L6_2atmpS1021;
    uint32_t _M0L6_2atmpS1020;
    int32_t _M0L6_2atmpS1019;
    int32_t _M0L3lenS1024;
    int32_t _M0L6_2atmpS1023;
    if (_M0L6_2atmpS1006 < 2) {
      int32_t _M0L3lenS1011 = _M0L4selfS119->$1;
      int32_t _M0L6_2atmpS1010 = _M0L3lenS1011 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS119, _M0L6_2atmpS1010);
    }
    _M0L4codeS120 = _M0L4codeS117 - 65536u;
    _M0L4dataS1012 = _M0L4selfS119->$0;
    _M0L3lenS1013 = _M0L4selfS119->$1;
    _M0L6_2atmpS1016 = _M0L4codeS120 >> 10;
    _M0L6_2atmpS1015 = 55296u + _M0L6_2atmpS1016;
    moonbit_incref_cycle_free(_M0L4dataS1012);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1014 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1015);
    if (
      _M0L3lenS1013 < 0
      || _M0L3lenS1013 >= Moonbit_array_length(_M0L4dataS1012)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1012[_M0L3lenS1013] = _M0L6_2atmpS1014;
    moonbit_decref_cycle_free(_M0L4dataS1012);
    _M0L4dataS1017 = _M0L4selfS119->$0;
    _M0L3lenS1022 = _M0L4selfS119->$1;
    _M0L6_2atmpS1018 = _M0L3lenS1022 + 1;
    _M0L6_2atmpS1021 = _M0L4codeS120 & 1023u;
    _M0L6_2atmpS1020 = 56320u + _M0L6_2atmpS1021;
    moonbit_incref_cycle_free(_M0L4dataS1017);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1019 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1020);
    if (
      _M0L6_2atmpS1018 < 0
      || _M0L6_2atmpS1018 >= Moonbit_array_length(_M0L4dataS1017)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1017[_M0L6_2atmpS1018] = _M0L6_2atmpS1019;
    moonbit_decref_cycle_free(_M0L4dataS1017);
    _M0L3lenS1024 = _M0L4selfS119->$1;
    _M0L6_2atmpS1023 = _M0L3lenS1024 + 2;
    _M0L4selfS119->$1 = _M0L6_2atmpS1023;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_25.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS114,
  int32_t _M0L8requiredS115
) {
  uint16_t* _M0L4dataS995;
  int32_t _M0L6_2atmpS993;
  int32_t _M0L3lenS994;
  int32_t _M0L13new__capacityS113;
  uint16_t* _M0L4dataS990;
  int32_t _M0L6_2atmpS991;
  int32_t _M0L3lenS992;
  uint16_t* _M0L9new__dataS116;
  uint16_t* _M0L6_2aoldS1858;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS995 = _M0L4selfS114->$0;
  _M0L6_2atmpS993 = Moonbit_array_length(_M0L4dataS995);
  _M0L3lenS994 = _M0L4selfS114->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS113
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS993, _M0L3lenS994, _M0L8requiredS115);
  _M0L4dataS990 = _M0L4selfS114->$0;
  moonbit_incref_cycle_free(_M0L4dataS990);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS991 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS992 = _M0L4selfS114->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS116
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS990, _M0L13new__capacityS113, _M0L6_2atmpS991, _M0L3lenS992, 0, 0);
  _M0L6_2aoldS1858 = _M0L4selfS114->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1858);
  _M0L4selfS114->$0 = _M0L9new__dataS116;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS112,
  int32_t _M0L3lenS108,
  int32_t _M0L8requiredS107
) {
  int32_t _M0L5spaceS109;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS107 < _M0L3lenS108) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  _M0L5spaceS109 = _M0L7currentS112;
  while (1) {
    if (_M0L5spaceS109 < _M0L8requiredS107) {
      int32_t _M0L4nextS110 = _M0L5spaceS109 * 2;
      if (_M0L4nextS110 <= _M0L5spaceS109) {
        return _M0L8requiredS107;
      }
      _M0L5spaceS109 = _M0L4nextS110;
      continue;
    } else {
      return _M0L5spaceS109;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS106) {
  int32_t _M0L6_2atmpS989;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS989 = *(int32_t*)&_M0L4selfS106;
  return (uint16_t)_M0L6_2atmpS989;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS105) {
  int32_t _M0L6_2atmpS988;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS988 = _M0L4selfS105;
  return *(uint32_t*)&_M0L6_2atmpS988;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS103
) {
  int32_t _M0L3lenS979;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS979 = _M0L4selfS103->$1;
  if (_M0L3lenS979 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS980 = _M0L4selfS103->$1;
    uint16_t* _M0L4dataS982 = _M0L4selfS103->$0;
    int32_t _M0L6_2atmpS981 = Moonbit_array_length(_M0L4dataS982);
    if (_M0L3lenS980 == _M0L6_2atmpS981) {
      uint16_t* _M0L4dataS983 = _M0L4selfS103->$0;
      moonbit_incref_cycle_free(_M0L4dataS983);
      return _M0L4dataS983;
    } else {
      uint16_t* _M0L4dataS984 = _M0L4selfS103->$0;
      int32_t _M0L3lenS985 = _M0L4selfS103->$1;
      int32_t _M0L6_2atmpS986;
      int32_t _M0L3lenS987;
      uint16_t* _M0L4dataS104;
      moonbit_incref_cycle_free(_M0L4dataS984);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS986 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS987 = _M0L4selfS103->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS104
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS984, _M0L3lenS985, _M0L6_2atmpS986, _M0L3lenS987, 0, 0);
      return _M0L4dataS104;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS100,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L4initS101,
  int32_t _M0L3lenS97,
  int32_t _M0L11src__offsetS98,
  int32_t _M0L11dst__offsetS99
) {
  int32_t _if__result_1961;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS96 >= 0) {
    if (_M0L3lenS97 >= 0) {
      if (_M0L11src__offsetS98 >= 0) {
        if (_M0L11dst__offsetS99 >= 0) {
          int32_t _M0L6_2atmpS975 = _M0L11src__offsetS98 + _M0L3lenS97;
          int32_t _M0L6_2atmpS976 = Moonbit_array_length(_M0L3srcS100);
          if (_M0L6_2atmpS975 <= _M0L6_2atmpS976) {
            int32_t _M0L6_2atmpS974 = _M0L11dst__offsetS99 + _M0L3lenS97;
            _if__result_1961 = _M0L6_2atmpS974 <= _M0L13allocate__lenS96;
          } else {
            _if__result_1961 = 0;
          }
        } else {
          _if__result_1961 = 0;
        }
      } else {
        _if__result_1961 = 0;
      }
    } else {
      _if__result_1961 = 0;
    }
  } else {
    _if__result_1961 = 0;
  }
  if (_if__result_1961) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS100, _M0L13allocate__lenS96, _M0L4initS101, _M0L11src__offsetS98, _M0L11dst__offsetS99, _M0L3lenS97);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS102;
    int32_t _M0L6_2atmpS978;
    moonbit_string_t _M0L6_2atmpS977;
    uint16_t* _result_1962;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS102
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L13allocate__lenS96);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11src__offsetS98);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11dst__offsetS99);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L3lenS97);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_31.data);
    _M0L6_2atmpS978 = Moonbit_array_length(_M0L3srcS100);
    moonbit_decref_cycle_free(_M0L3srcS100);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L6_2atmpS978);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS977
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS102);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS102);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_1962 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS977);
    moonbit_decref_cycle_free(_M0L6_2atmpS977);
    return _result_1962;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS93,
  int32_t _M0L13allocate__lenS90,
  int32_t _M0L4initS91,
  int32_t _M0L11src__offsetS94,
  int32_t _M0L11dst__offsetS92,
  int32_t _M0L9blit__lenS95
) {
  uint16_t* _M0L3dstS89;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS89
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS90, _M0L4initS91);
  moonbit_incref_cycle_free(_M0L3dstS89);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS89, _M0L11dst__offsetS92, _M0L3srcS93, _M0L11src__offsetS94, _M0L9blit__lenS95, sizeof(uint16_t));
  return _M0L3dstS89;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS87
) {
  int32_t _M0L7initialS86;
  uint16_t* _M0L4dataS88;
  struct _M0TPB13StringBuilder* _block_1963;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS87 < 1) {
    _M0L7initialS86 = 1;
  } else {
    int32_t _M0L6_2atmpS973 = _M0L10size__hintS87 + 1;
    _M0L7initialS86 = _M0L6_2atmpS973 / 2;
  }
  _M0L4dataS88 = (uint16_t*)moonbit_make_string(_M0L7initialS86, 0);
  _block_1963
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_1963)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 46, 0);
  _block_1963->$0 = _M0L4dataS88;
  _block_1963->$1 = 0;
  return _block_1963;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS85) {
  int32_t _M0L6_2atmpS972;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS972 = (int32_t)_M0L4selfS85;
  return _M0L6_2atmpS972;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS77,
  int32_t _M0L13allocate__lenS73,
  int32_t _M0L3lenS74,
  int32_t _M0L11src__offsetS75,
  int32_t _M0L11dst__offsetS76
) {
  int32_t _if__result_1964;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS73 >= 0) {
    if (_M0L3lenS74 >= 0) {
      if (_M0L11src__offsetS75 >= 0) {
        if (_M0L11dst__offsetS76 >= 0) {
          int32_t _M0L6_2atmpS963 = _M0L11src__offsetS75 + _M0L3lenS74;
          int32_t _M0L6_2atmpS964;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS964 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS77);
          if (_M0L6_2atmpS963 <= _M0L6_2atmpS964) {
            int32_t _M0L6_2atmpS962 = _M0L11dst__offsetS76 + _M0L3lenS74;
            _if__result_1964 = _M0L6_2atmpS962 <= _M0L13allocate__lenS73;
          } else {
            _if__result_1964 = 0;
          }
        } else {
          _if__result_1964 = 0;
        }
      } else {
        _if__result_1964 = 0;
      }
    } else {
      _if__result_1964 = 0;
    }
  } else {
    _if__result_1964 = 0;
  }
  if (_if__result_1964) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS73, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS77, _M0L11src__offsetS75, _M0L11dst__offsetS76, _M0L3lenS74);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS78;
    int32_t _M0L6_2atmpS966;
    moonbit_string_t _M0L6_2atmpS965;
    moonbit_string_t* _result_1965;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS78
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS78, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS78, _M0L13allocate__lenS73);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS78, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS78, _M0L11src__offsetS75);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS78, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS78, _M0L11dst__offsetS76);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS78, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS78, _M0L3lenS74);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS78, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS966 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS77);
    moonbit_decref_cycle_free(_M0L3srcS77);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS78, _M0L6_2atmpS966);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS965
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS78);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS78);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1965
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS965);
    moonbit_decref_cycle_free(_M0L6_2atmpS965);
    return _result_1965;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS83,
  int32_t _M0L13allocate__lenS79,
  int32_t _M0L3lenS80,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L11dst__offsetS82
) {
  int32_t _if__result_1966;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS79 >= 0) {
    if (_M0L3lenS80 >= 0) {
      if (_M0L11src__offsetS81 >= 0) {
        if (_M0L11dst__offsetS82 >= 0) {
          int32_t _M0L6_2atmpS968 = _M0L11src__offsetS81 + _M0L3lenS80;
          int32_t _M0L6_2atmpS969;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS969
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS83);
          if (_M0L6_2atmpS968 <= _M0L6_2atmpS969) {
            int32_t _M0L6_2atmpS967 = _M0L11dst__offsetS82 + _M0L3lenS80;
            _if__result_1966 = _M0L6_2atmpS967 <= _M0L13allocate__lenS79;
          } else {
            _if__result_1966 = 0;
          }
        } else {
          _if__result_1966 = 0;
        }
      } else {
        _if__result_1966 = 0;
      }
    } else {
      _if__result_1966 = 0;
    }
  } else {
    _if__result_1966 = 0;
  }
  if (_if__result_1966) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS79, 0, _M0L3srcS83, _M0L11src__offsetS81, _M0L11dst__offsetS82, _M0L3lenS80);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS84;
    int32_t _M0L6_2atmpS971;
    moonbit_string_t _M0L6_2atmpS970;
    struct _M0TUsiE** _result_1967;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS84
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS84, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS84, _M0L13allocate__lenS79);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS84, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS84, _M0L11src__offsetS81);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS84, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS84, _M0L11dst__offsetS82);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS84, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS84, _M0L3lenS80);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS84, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS971 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS83);
    moonbit_decref_cycle_free(_M0L3srcS83);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS84, _M0L6_2atmpS971);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS970
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS84);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS84);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1967
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS970);
    moonbit_decref_cycle_free(_M0L6_2atmpS970);
    return _result_1967;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS64,
  int32_t _M0L3objS63
) {
  struct _M0TPB6Logger _M0L6_2atmpS957;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS64);
  _M0L6_2atmpS957
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS64
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS63, _M0L6_2atmpS957);
  if (_M0L6_2atmpS957.$1) {
    moonbit_decref(_M0L6_2atmpS957.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGfE(
  struct _M0TPB13StringBuilder* _M0L4selfS66,
  float _M0L3objS65
) {
  struct _M0TPB6Logger _M0L6_2atmpS958;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS66);
  _M0L6_2atmpS958
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS66
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGfE(_M0L3objS65, _M0L6_2atmpS958);
  if (_M0L6_2atmpS958.$1) {
    moonbit_decref(_M0L6_2atmpS958.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGbE(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  int32_t _M0L3objS67
) {
  struct _M0TPB6Logger _M0L6_2atmpS959;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS68);
  _M0L6_2atmpS959
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS68
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGbE(_M0L3objS67, _M0L6_2atmpS959);
  if (_M0L6_2atmpS959.$1) {
    moonbit_decref(_M0L6_2atmpS959.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS70,
  moonbit_string_t _M0L3objS69
) {
  struct _M0TPB6Logger _M0L6_2atmpS960;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS70);
  _M0L6_2atmpS960
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS70
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS69, _M0L6_2atmpS960);
  if (_M0L6_2atmpS960.$1) {
    moonbit_decref(_M0L6_2atmpS960.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS72,
  uint64_t _M0L3objS71
) {
  struct _M0TPB6Logger _M0L6_2atmpS961;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS72);
  _M0L6_2atmpS961
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS72
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS71, _M0L6_2atmpS961);
  if (_M0L6_2atmpS961.$1) {
    moonbit_decref(_M0L6_2atmpS961.$1);
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
        int32_t _M0L6_2atmpS930 = _M0L11dst__offsetS16 + _M0L1iS18;
        int32_t _M0L6_2atmpS932 = _M0L11src__offsetS17 + _M0L1iS18;
        int32_t _M0L6_2atmpS931;
        int32_t _M0L6_2atmpS933;
        if (
          _M0L6_2atmpS932 < 0
          || _M0L6_2atmpS932 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS931 = (int32_t)_M0L3srcS15[_M0L6_2atmpS932];
        if (
          _M0L6_2atmpS930 < 0
          || _M0L6_2atmpS930 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS930] = _M0L6_2atmpS931;
        _M0L6_2atmpS933 = _M0L1iS18 + 1;
        _M0L1iS18 = _M0L6_2atmpS933;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS938 = _M0L3lenS19 - 1;
    int32_t _M0L1iS21 = _M0L6_2atmpS938;
    while (1) {
      if (_M0L1iS21 >= 0) {
        int32_t _M0L6_2atmpS934 = _M0L11dst__offsetS16 + _M0L1iS21;
        int32_t _M0L6_2atmpS936 = _M0L11src__offsetS17 + _M0L1iS21;
        int32_t _M0L6_2atmpS935;
        int32_t _M0L6_2atmpS937;
        if (
          _M0L6_2atmpS936 < 0
          || _M0L6_2atmpS936 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS935 = (int32_t)_M0L3srcS15[_M0L6_2atmpS936];
        if (
          _M0L6_2atmpS934 < 0
          || _M0L6_2atmpS934 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS934] = _M0L6_2atmpS935;
        _M0L6_2atmpS937 = _M0L1iS21 - 1;
        _M0L1iS21 = _M0L6_2atmpS937;
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
        int32_t _M0L6_2atmpS939 = _M0L11dst__offsetS25 + _M0L1iS27;
        int32_t _M0L6_2atmpS941 = _M0L11src__offsetS26 + _M0L1iS27;
        moonbit_string_t _M0L6_2atmpS940;
        moonbit_string_t _M0L6_2aoldS1859;
        int32_t _M0L6_2atmpS942;
        if (
          _M0L6_2atmpS941 < 0
          || _M0L6_2atmpS941 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS940 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS941];
        if (
          _M0L6_2atmpS939 < 0
          || _M0L6_2atmpS939 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1859 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS939];
        moonbit_incref_cycle_free(_M0L6_2atmpS940);
        moonbit_decref_cycle_free(_M0L6_2aoldS1859);
        _M0L3dstS23[_M0L6_2atmpS939] = _M0L6_2atmpS940;
        _M0L6_2atmpS942 = _M0L1iS27 + 1;
        _M0L1iS27 = _M0L6_2atmpS942;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS947 = _M0L3lenS28 - 1;
    int32_t _M0L1iS30 = _M0L6_2atmpS947;
    while (1) {
      if (_M0L1iS30 >= 0) {
        int32_t _M0L6_2atmpS943 = _M0L11dst__offsetS25 + _M0L1iS30;
        int32_t _M0L6_2atmpS945 = _M0L11src__offsetS26 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS944;
        moonbit_string_t _M0L6_2aoldS1860;
        int32_t _M0L6_2atmpS946;
        if (
          _M0L6_2atmpS945 < 0
          || _M0L6_2atmpS945 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS944 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS945];
        if (
          _M0L6_2atmpS943 < 0
          || _M0L6_2atmpS943 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1860 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS943];
        moonbit_incref_cycle_free(_M0L6_2atmpS944);
        moonbit_decref_cycle_free(_M0L6_2aoldS1860);
        _M0L3dstS23[_M0L6_2atmpS943] = _M0L6_2atmpS944;
        _M0L6_2atmpS946 = _M0L1iS30 - 1;
        _M0L1iS30 = _M0L6_2atmpS946;
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
        int32_t _M0L6_2atmpS948 = _M0L11dst__offsetS34 + _M0L1iS36;
        int32_t _M0L6_2atmpS950 = _M0L11src__offsetS35 + _M0L1iS36;
        struct _M0TUsiE* _M0L6_2atmpS949;
        struct _M0TUsiE* _M0L6_2aoldS1861;
        int32_t _M0L6_2atmpS951;
        if (
          _M0L6_2atmpS950 < 0
          || _M0L6_2atmpS950 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS949 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS950];
        if (
          _M0L6_2atmpS948 < 0
          || _M0L6_2atmpS948 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1861 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS948];
        if (_M0L6_2atmpS949) {
          moonbit_incref_cycle_free(_M0L6_2atmpS949);
        }
        if (_M0L6_2aoldS1861) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1861);
        }
        _M0L3dstS32[_M0L6_2atmpS948] = _M0L6_2atmpS949;
        _M0L6_2atmpS951 = _M0L1iS36 + 1;
        _M0L1iS36 = _M0L6_2atmpS951;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS956 = _M0L3lenS37 - 1;
    int32_t _M0L1iS39 = _M0L6_2atmpS956;
    while (1) {
      if (_M0L1iS39 >= 0) {
        int32_t _M0L6_2atmpS952 = _M0L11dst__offsetS34 + _M0L1iS39;
        int32_t _M0L6_2atmpS954 = _M0L11src__offsetS35 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS953;
        struct _M0TUsiE* _M0L6_2aoldS1862;
        int32_t _M0L6_2atmpS955;
        if (
          _M0L6_2atmpS954 < 0
          || _M0L6_2atmpS954 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS953 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS954];
        if (
          _M0L6_2atmpS952 < 0
          || _M0L6_2atmpS952 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1862 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS952];
        if (_M0L6_2atmpS953) {
          moonbit_incref_cycle_free(_M0L6_2atmpS953);
        }
        if (_M0L6_2aoldS1862) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1862);
        }
        _M0L3dstS32[_M0L6_2atmpS952] = _M0L6_2atmpS953;
        _M0L6_2atmpS955 = _M0L1iS39 - 1;
        _M0L1iS39 = _M0L6_2atmpS955;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS902) {
  switch (Moonbit_object_tag(_M0L4_2aeS902)) {
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_34.data;
      break;
    }
    
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS902);
      break;
    }
    
    case 1: {
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
  void* _M0L11_2aobj__ptrS925,
  struct _M0TPB4Show _M0L8_2aparamS924
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS923 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS925;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS923, _M0L8_2aparamS924);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS922,
  struct _M0TPB4Show _M0L8_2aparamS921
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS920 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS922;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS920, _M0L8_2aparamS921);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS919,
  int32_t _M0L8_2aparamS918
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS917 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS919;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS917, _M0L8_2aparamS918);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS916,
  struct _M0TPC16string10StringView _M0L8_2aparamS915
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS914 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS916;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS914, _M0L8_2aparamS915);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS913,
  moonbit_string_t _M0L8_2aparamS910,
  int32_t _M0L8_2aparamS911,
  int32_t _M0L8_2aparamS912
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS909 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS913;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS909, _M0L8_2aparamS910, _M0L8_2aparamS911, _M0L8_2aparamS912);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS908,
  moonbit_string_t _M0L8_2aparamS907
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS906 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS908;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS906, _M0L8_2aparamS907);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS929;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS895;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS896;
  int32_t _M0L7_2abindS897;
  struct _M0TUsiE** _M0L7_2abindS898;
  int32_t _M0L6_2acntS1867;
  int32_t _M0L2__S899;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS929
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS895
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS895)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 49, 0);
  _M0L12async__testsS895->$0 = _M0L6_2atmpS929;
  _M0L12async__testsS895->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS896
  = _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS897 = _M0L7_2abindS896->$1;
  _M0L7_2abindS898 = _M0L7_2abindS896->$0;
  _M0L6_2acntS1867
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS896));
  if (_M0L6_2acntS1867 > 1) {
    int32_t _M0L11_2anew__cntS1868 = _M0L6_2acntS1867 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS896), _M0L11_2anew__cntS1868);
    moonbit_incref_cycle_free(_M0L7_2abindS898);
  } else if (_M0L6_2acntS1867 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS896);
  }
  _M0L2__S899 = 0;
  while (1) {
    if (_M0L2__S899 < _M0L7_2abindS897) {
      struct _M0TUsiE* _M0L3argS900 =
        (struct _M0TUsiE*)_M0L7_2abindS898[_M0L2__S899];
      moonbit_string_t _M0L6_2atmpS926 = _M0L3argS900->$0;
      int32_t _M0L6_2atmpS927 = _M0L3argS900->$1;
      int32_t _M0L6_2atmpS928;
      moonbit_incref_cycle_free(_M0L6_2atmpS926);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples28if__extended__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS895, _M0L6_2atmpS926, _M0L6_2atmpS927);
      moonbit_decref_cycle_free(_M0L6_2atmpS926);
      _M0L6_2atmpS928 = _M0L2__S899 + 1;
      _M0L2__S899 = _M0L6_2atmpS928;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS898);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples28if__extended__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28if__extended__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS895);
  moonbit_decref_cycle_free(_M0L12async__testsS895);
  moonbit_flush_cycles();
  return 0;
}