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

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TUdiE;

struct _M0BTPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23if__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23if__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

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

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235;

struct _M0TUddE;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TPB17FloatingDecimal64 {
  uint64_t $0;
  int32_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23if__net__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23if__net__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1247(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1240(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1235(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1212(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1205(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples23if__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
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

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*,
  int32_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*
);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
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

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 102, 95, 110, 101, 116, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[111]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 110, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 102, 95, 110, 101, 116, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 
    114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 0
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
} const _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1247$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1247
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[93] =
  {
    sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235)
    / 4, 1,
    offsetof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235, $1)
    / 4
    * 2,
    sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240)
    / 4, 1,
    offsetof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

float _M0FP26RiantR8snn__mbt2ms = 0x1p+0f;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2653
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1268,
  moonbit_string_t _M0L8filenameS1237,
  int32_t _M0L5indexS1239
) {
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235* _closure_2688;
  struct _M0TWEu* _M0L13handle__startS1235;
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240* _closure_2689;
  struct _M0TWssbEu* _M0L14handle__resultS1240;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1247;
  void* _M0L11_2atry__errS1262;
  struct moonbit_result_0 _tmp_2691;
  int32_t _handle__error__result_2692;
  int32_t _M0L6_2atmpS2641;
  void* _M0L3errS1263;
  moonbit_string_t _M0L4nameS1265;
  struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1266;
  moonbit_string_t _M0L7_2anameS1267;
  int32_t _M0L6_2acntS2682;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1237);
  _closure_2688
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235*)moonbit_malloc(sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235));
  Moonbit_object_header(_closure_2688)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2688->code
  = &_M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1235;
  _closure_2688->$0 = _M0L5indexS1239;
  _closure_2688->$1 = _M0L8filenameS1237;
  _M0L13handle__startS1235 = (struct _M0TWEu*)_closure_2688;
  moonbit_incref_cycle_free(_M0L8filenameS1237);
  _closure_2689
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240*)moonbit_malloc(sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240));
  Moonbit_object_header(_closure_2689)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2689->code
  = &_M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1240;
  _closure_2689->$0 = _M0L5indexS1239;
  _closure_2689->$1 = _M0L8filenameS1237;
  _M0L14handle__resultS1240 = (struct _M0TWssbEu*)_closure_2689;
  _M0L17error__to__stringS1247
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1247$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2691
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1268, _M0L8filenameS1237, _M0L5indexS1239, _M0L13handle__startS1235, _M0L14handle__resultS1240, _M0L17error__to__stringS1247);
  if (_tmp_2691.tag) {
    int32_t const _M0L5_2aokS2650 = _tmp_2691.data.ok;
    _handle__error__result_2692 = _M0L5_2aokS2650;
  } else {
    void* const _M0L6_2aerrS2651 = _tmp_2691.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
    moonbit_decref_cycle_free(_M0L13handle__startS1235);
    _M0L11_2atry__errS1262 = _M0L6_2aerrS2651;
    goto join_1261;
  }
  if (_handle__error__result_2692) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
    moonbit_decref_cycle_free(_M0L13handle__startS1235);
    _M0L6_2atmpS2641 = 1;
  } else {
    struct moonbit_result_0 _tmp_2693;
    int32_t _handle__error__result_2694;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2693
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1268, _M0L8filenameS1237, _M0L5indexS1239, _M0L13handle__startS1235, _M0L14handle__resultS1240, _M0L17error__to__stringS1247);
    if (_tmp_2693.tag) {
      int32_t const _M0L5_2aokS2648 = _tmp_2693.data.ok;
      _handle__error__result_2694 = _M0L5_2aokS2648;
    } else {
      void* const _M0L6_2aerrS2649 = _tmp_2693.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
      moonbit_decref_cycle_free(_M0L13handle__startS1235);
      _M0L11_2atry__errS1262 = _M0L6_2aerrS2649;
      goto join_1261;
    }
    if (_handle__error__result_2694) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
      moonbit_decref_cycle_free(_M0L13handle__startS1235);
      _M0L6_2atmpS2641 = 1;
    } else {
      struct moonbit_result_0 _tmp_2695;
      int32_t _handle__error__result_2696;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2695
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1268, _M0L8filenameS1237, _M0L5indexS1239, _M0L13handle__startS1235, _M0L14handle__resultS1240, _M0L17error__to__stringS1247);
      if (_tmp_2695.tag) {
        int32_t const _M0L5_2aokS2646 = _tmp_2695.data.ok;
        _handle__error__result_2696 = _M0L5_2aokS2646;
      } else {
        void* const _M0L6_2aerrS2647 = _tmp_2695.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
        moonbit_decref_cycle_free(_M0L13handle__startS1235);
        _M0L11_2atry__errS1262 = _M0L6_2aerrS2647;
        goto join_1261;
      }
      if (_handle__error__result_2696) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
        moonbit_decref_cycle_free(_M0L13handle__startS1235);
        _M0L6_2atmpS2641 = 1;
      } else {
        struct moonbit_result_0 _tmp_2697;
        int32_t _handle__error__result_2698;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2697
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1268, _M0L8filenameS1237, _M0L5indexS1239, _M0L13handle__startS1235, _M0L14handle__resultS1240, _M0L17error__to__stringS1247);
        if (_tmp_2697.tag) {
          int32_t const _M0L5_2aokS2644 = _tmp_2697.data.ok;
          _handle__error__result_2698 = _M0L5_2aokS2644;
        } else {
          void* const _M0L6_2aerrS2645 = _tmp_2697.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
          moonbit_decref_cycle_free(_M0L13handle__startS1235);
          _M0L11_2atry__errS1262 = _M0L6_2aerrS2645;
          goto join_1261;
        }
        if (_handle__error__result_2698) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
          moonbit_decref_cycle_free(_M0L13handle__startS1235);
          _M0L6_2atmpS2641 = 1;
        } else {
          struct moonbit_result_0 _tmp_2699;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2699
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1268, _M0L8filenameS1237, _M0L5indexS1239, _M0L13handle__startS1235, _M0L14handle__resultS1240, _M0L17error__to__stringS1247);
          moonbit_decref_cycle_free(_M0L13handle__startS1235);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1247);
          if (_tmp_2699.tag) {
            int32_t const _M0L5_2aokS2642 = _tmp_2699.data.ok;
            _M0L6_2atmpS2641 = _M0L5_2aokS2642;
          } else {
            void* const _M0L6_2aerrS2643 = _tmp_2699.data.err;
            _M0L11_2atry__errS1262 = _M0L6_2aerrS2643;
            goto join_1261;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2641) {
    void* _M0L126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2652 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2652)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2652)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1262
    = _M0L126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2652;
    goto join_1261;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1240);
  }
  goto joinlet_2690;
  join_1261:;
  _M0L3errS1263 = _M0L11_2atry__errS1262;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1266
  = (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1263;
  _M0L7_2anameS1267 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1266->$0;
  _M0L6_2acntS2682
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1266));
  if (_M0L6_2acntS2682 > 1) {
    int32_t _M0L11_2anew__cntS2683 = _M0L6_2acntS2682 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1266), _M0L11_2anew__cntS2683);
    moonbit_incref_cycle_free(_M0L7_2anameS1267);
  } else if (_M0L6_2acntS2682 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1266);
  }
  _M0L4nameS1265 = _M0L7_2anameS1267;
  goto join_1264;
  goto joinlet_2700;
  join_1264:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1240(_M0L14handle__resultS1240, _M0L4nameS1265, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1240);
  moonbit_decref_cycle_free(_M0L4nameS1265);
  joinlet_2700:;
  joinlet_2690:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1247(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2640,
  void* _M0L3errS1248
) {
  void* _M0L1eS1250;
  moonbit_string_t _M0L1eS1252;
  moonbit_string_t _result_2703;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1248)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1253 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1248;
      moonbit_string_t _M0L4_2aeS1254 = _M0L10_2aFailureS1253->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1254);
      _M0L1eS1252 = _M0L4_2aeS1254;
      goto join_1251;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1255 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1248;
      moonbit_string_t _M0L4_2aeS1256 = _M0L15_2aInspectErrorS1255->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1256);
      _M0L1eS1252 = _M0L4_2aeS1256;
      goto join_1251;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1257 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1248;
      moonbit_string_t _M0L4_2aeS1258 = _M0L16_2aSnapshotErrorS1257->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1258);
      _M0L1eS1252 = _M0L4_2aeS1258;
      goto join_1251;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1259 =
        (struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1248;
      moonbit_string_t _M0L4_2aeS1260 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1259->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1260);
      _M0L1eS1252 = _M0L4_2aeS1260;
      goto join_1251;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1248);
      _M0L1eS1250 = _M0L3errS1248;
      goto join_1249;
      break;
    }
  }
  join_1251:;
  return _M0L1eS1252;
  join_1249:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2703 = _M0FP15Error10to__string(_M0L1eS1250);
  moonbit_decref_cycle_free(_M0L1eS1250);
  return _result_2703;
}

int32_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1240(
  struct _M0TWssbEu* _M0L6_2aenvS2637,
  moonbit_string_t _M0L10__testnameS1241,
  moonbit_string_t _M0L7messageS1242,
  int32_t _M0L7skippedS1243
) {
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240* _M0L14_2acasted__envS2638;
  moonbit_string_t _M0L8filenameS1237;
  int32_t _M0L5indexS1239;
  moonbit_string_t _M0L10file__nameS1244;
  moonbit_string_t _M0L7messageS1245;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1246;
  moonbit_string_t _M0L6_2atmpS2639;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2638
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1240*)_M0L6_2aenvS2637;
  _M0L8filenameS1237 = _M0L14_2acasted__envS2638->$1;
  _M0L5indexS1239 = _M0L14_2acasted__envS2638->$0;
  if (!_M0L7skippedS1243 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1244
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1237, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1245
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1242, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1246
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1246, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1246, _M0L10file__nameS1244);
  moonbit_decref_cycle_free(_M0L10file__nameS1244);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1246, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1246, _M0L5indexS1239);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1246, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1246, _M0L7messageS1245);
  moonbit_decref_cycle_free(_M0L7messageS1245);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1246, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2639
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1246);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1246);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2639);
  moonbit_decref_cycle_free(_M0L6_2atmpS2639);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1235(
  struct _M0TWEu* _M0L6_2aenvS2634
) {
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235* _M0L14_2acasted__envS2635;
  moonbit_string_t _M0L8filenameS1237;
  int32_t _M0L5indexS1239;
  moonbit_string_t _M0L10file__nameS1236;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1238;
  moonbit_string_t _M0L6_2atmpS2636;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2635
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fif__net__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1235*)_M0L6_2aenvS2634;
  _M0L8filenameS1237 = _M0L14_2acasted__envS2635->$1;
  _M0L5indexS1239 = _M0L14_2acasted__envS2635->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1236
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1237, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1238
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1238, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1238, _M0L10file__nameS1236);
  moonbit_decref_cycle_free(_M0L10file__nameS1236);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1238, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1238, _M0L5indexS1239);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1238, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2636
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1238);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1238);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2636);
  moonbit_decref_cycle_free(_M0L6_2atmpS2636);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1205;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1212;
  struct _M0TUsiE** _M0L6_2atmpS2633;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1219;
  moonbit_string_t* _M0L9cli__argsS1220;
  moonbit_string_t _M0L6_2atmpS2632;
  moonbit_string_t _M0L6_2atmpS2631;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1221;
  int32_t _M0L7_2abindS1222;
  moonbit_string_t* _M0L7_2abindS1223;
  int32_t _M0L6_2acntS2684;
  int32_t _M0L2__S1224;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1205 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1212 = 0;
  _M0L6_2atmpS2633 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1219
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1219->$0 = _M0L6_2atmpS2633;
  _M0L16file__and__indexS1219->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1220
  = _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1220)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2632 = (moonbit_string_t)_M0L9cli__argsS1220[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2632);
  moonbit_decref_cycle_free(_M0L9cli__argsS1220);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2631
  = _M0MP46RiantR8snn__mbt8examples23if__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2632);
  moonbit_decref_cycle_free(_M0L6_2atmpS2632);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1221
  = _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1212(_M0L51moonbit__test__driver__internal__split__mbt__stringS1212, _M0L6_2atmpS2631, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2631);
  _M0L7_2abindS1222 = _M0L10test__argsS1221->$1;
  _M0L7_2abindS1223 = _M0L10test__argsS1221->$0;
  _M0L6_2acntS2684
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1221));
  if (_M0L6_2acntS2684 > 1) {
    int32_t _M0L11_2anew__cntS2685 = _M0L6_2acntS2684 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1221), _M0L11_2anew__cntS2685);
    moonbit_incref_cycle_free(_M0L7_2abindS1223);
  } else if (_M0L6_2acntS2684 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1221);
  }
  _M0L2__S1224 = 0;
  while (1) {
    if (_M0L2__S1224 < _M0L7_2abindS1222) {
      moonbit_string_t _M0L3argS1225 =
        (moonbit_string_t)_M0L7_2abindS1223[_M0L2__S1224];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1226;
      moonbit_string_t _M0L4fileS1227;
      moonbit_string_t _M0L5rangeS1228;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1229;
      moonbit_string_t _M0L6_2atmpS2629;
      int32_t _M0L5startS1230;
      moonbit_string_t _M0L6_2atmpS2628;
      int32_t _M0L3endS1231;
      int32_t _M0L1iS1232;
      int32_t _M0L6_2atmpS2630;
      moonbit_incref_cycle_free(_M0L3argS1225);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1226
      = _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1212(_M0L51moonbit__test__driver__internal__split__mbt__stringS1212, _M0L3argS1225, 58);
      moonbit_decref_cycle_free(_M0L3argS1225);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1227
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1226, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1228
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1226, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1226);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1229
      = _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1212(_M0L51moonbit__test__driver__internal__split__mbt__stringS1212, _M0L5rangeS1228, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1228);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2629
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1229, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1230
      = _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1205(_M0L45moonbit__test__driver__internal__parse__int__S1205, _M0L6_2atmpS2629);
      moonbit_decref_cycle_free(_M0L6_2atmpS2629);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2628
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1229, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1229);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1231
      = _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1205(_M0L45moonbit__test__driver__internal__parse__int__S1205, _M0L6_2atmpS2628);
      moonbit_decref_cycle_free(_M0L6_2atmpS2628);
      _M0L1iS1232 = _M0L5startS1230;
      while (1) {
        if (_M0L1iS1232 < _M0L3endS1231) {
          struct _M0TUsiE* _M0L8_2atupleS2626;
          int32_t _M0L6_2atmpS2627;
          moonbit_incref_cycle_free(_M0L4fileS1227);
          _M0L8_2atupleS2626
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2626)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2626->$0 = _M0L4fileS1227;
          _M0L8_2atupleS2626->$1 = _M0L1iS1232;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1219, _M0L8_2atupleS2626);
          _M0L6_2atmpS2627 = _M0L1iS1232 + 1;
          _M0L1iS1232 = _M0L6_2atmpS2627;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1227);
        }
        break;
      }
      _M0L6_2atmpS2630 = _M0L2__S1224 + 1;
      _M0L2__S1224 = _M0L6_2atmpS2630;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1223);
    }
    break;
  }
  return _M0L16file__and__indexS1219;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1212(
  int32_t _M0L6_2aenvS2607,
  moonbit_string_t _M0L1sS1213,
  int32_t _M0L3sepS1214
) {
  moonbit_string_t* _M0L6_2atmpS2625;
  struct _M0TPB5ArrayGsE* _M0L3resS1215;
  struct _M0TPB8MutLocalGiE* _M0L1iS1216;
  struct _M0TPB8MutLocalGiE* _M0L5startS1217;
  int32_t _M0L3valS2620;
  int32_t _M0L6_2atmpS2621;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2625 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1215
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1215)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1215->$0 = _M0L6_2atmpS2625;
  _M0L3resS1215->$1 = 0;
  _M0L1iS1216
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1216)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1216->$0 = 0;
  _M0L5startS1217
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1217)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1217->$0 = 0;
  while (1) {
    int32_t _M0L3valS2608 = _M0L1iS1216->$0;
    int32_t _M0L6_2atmpS2609 = Moonbit_array_length(_M0L1sS1213);
    if (_M0L3valS2608 < _M0L6_2atmpS2609) {
      int32_t _M0L3valS2612 = _M0L1iS1216->$0;
      int32_t _M0L6_2atmpS2611;
      int32_t _M0L6_2atmpS2610;
      int32_t _M0L3valS2619;
      int32_t _M0L6_2atmpS2618;
      if (
        _M0L3valS2612 < 0
        || _M0L3valS2612 >= Moonbit_array_length(_M0L1sS1213)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2611 = _M0L1sS1213[_M0L3valS2612];
      _M0L6_2atmpS2610 = _M0L6_2atmpS2611;
      if (_M0L6_2atmpS2610 == _M0L3sepS1214) {
        int32_t _M0L3valS2614 = _M0L5startS1217->$0;
        int32_t _M0L3valS2615 = _M0L1iS1216->$0;
        moonbit_string_t _M0L6_2atmpS2613;
        int32_t _M0L3valS2617;
        int32_t _M0L6_2atmpS2616;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2613
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1213, _M0L3valS2614, _M0L3valS2615);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1215, _M0L6_2atmpS2613);
        _M0L3valS2617 = _M0L1iS1216->$0;
        _M0L6_2atmpS2616 = _M0L3valS2617 + 1;
        _M0L5startS1217->$0 = _M0L6_2atmpS2616;
      }
      _M0L3valS2619 = _M0L1iS1216->$0;
      _M0L6_2atmpS2618 = _M0L3valS2619 + 1;
      _M0L1iS1216->$0 = _M0L6_2atmpS2618;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1216);
    }
    break;
  }
  _M0L3valS2620 = _M0L5startS1217->$0;
  _M0L6_2atmpS2621 = Moonbit_array_length(_M0L1sS1213);
  if (_M0L3valS2620 < _M0L6_2atmpS2621) {
    int32_t _M0L3valS2623 = _M0L5startS1217->$0;
    int32_t _M0L6_2atmpS2624;
    moonbit_string_t _M0L6_2atmpS2622;
    moonbit_decref_cycle_free(_M0L5startS1217);
    _M0L6_2atmpS2624 = Moonbit_array_length(_M0L1sS1213);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2622
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1213, _M0L3valS2623, _M0L6_2atmpS2624);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1215, _M0L6_2atmpS2622);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1217);
  }
  return _M0L3resS1215;
}

int32_t _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1205(
  int32_t _M0L6_2aenvS2600,
  moonbit_string_t _M0L1sS1206
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1207;
  int32_t _M0L3lenS1208;
  int32_t _M0L7_2abindS1209;
  int32_t _M0L1iS1210;
  int32_t _result_2708;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1207
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1207)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1207->$0 = 0;
  _M0L3lenS1208 = Moonbit_array_length(_M0L1sS1206);
  _M0L7_2abindS1209 = 0;
  _M0L1iS1210 = _M0L7_2abindS1209;
  while (1) {
    if (_M0L1iS1210 < _M0L3lenS1208) {
      int32_t _M0L3valS2605 = _M0L3resS1207->$0;
      int32_t _M0L6_2atmpS2602 = _M0L3valS2605 * 10;
      int32_t _M0L6_2atmpS2604;
      int32_t _M0L6_2atmpS2603;
      int32_t _M0L6_2atmpS2601;
      int32_t _M0L6_2atmpS2606;
      if (
        _M0L1iS1210 < 0 || _M0L1iS1210 >= Moonbit_array_length(_M0L1sS1206)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2604 = _M0L1sS1206[_M0L1iS1210];
      _M0L6_2atmpS2603 = _M0L6_2atmpS2604 - 48;
      _M0L6_2atmpS2601 = _M0L6_2atmpS2602 + _M0L6_2atmpS2603;
      _M0L3resS1207->$0 = _M0L6_2atmpS2601;
      _M0L6_2atmpS2606 = _M0L1iS1210 + 1;
      _M0L1iS1210 = _M0L6_2atmpS2606;
      continue;
    }
    break;
  }
  _result_2708 = _M0L3resS1207->$0;
  moonbit_decref_cycle_free(_M0L3resS1207);
  return _result_2708;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples23if__net__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1204
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1204);
  return _M0L4selfS1204;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1174,
  moonbit_string_t _M0L12_2adiscard__S1175,
  int32_t _M0L12_2adiscard__S1176,
  struct _M0TWEu* _M0L12_2adiscard__S1177,
  struct _M0TWssbEu* _M0L12_2adiscard__S1178,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1179
) {
  struct moonbit_result_0 _result_2709;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2709.tag = 1;
  _result_2709.data.ok = 0;
  return _result_2709;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1180,
  moonbit_string_t _M0L12_2adiscard__S1181,
  int32_t _M0L12_2adiscard__S1182,
  struct _M0TWEu* _M0L12_2adiscard__S1183,
  struct _M0TWssbEu* _M0L12_2adiscard__S1184,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1185
) {
  struct moonbit_result_0 _result_2710;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2710.tag = 1;
  _result_2710.data.ok = 0;
  return _result_2710;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1186,
  moonbit_string_t _M0L12_2adiscard__S1187,
  int32_t _M0L12_2adiscard__S1188,
  struct _M0TWEu* _M0L12_2adiscard__S1189,
  struct _M0TWssbEu* _M0L12_2adiscard__S1190,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1191
) {
  struct moonbit_result_0 _result_2711;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2711.tag = 1;
  _result_2711.data.ok = 0;
  return _result_2711;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1192,
  moonbit_string_t _M0L12_2adiscard__S1193,
  int32_t _M0L12_2adiscard__S1194,
  struct _M0TWEu* _M0L12_2adiscard__S1195,
  struct _M0TWssbEu* _M0L12_2adiscard__S1196,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1197
) {
  struct moonbit_result_0 _result_2712;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2712.tag = 1;
  _result_2712.data.ok = 0;
  return _result_2712;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23if__net__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1198,
  moonbit_string_t _M0L12_2adiscard__S1199,
  int32_t _M0L12_2adiscard__S1200,
  struct _M0TWEu* _M0L12_2adiscard__S1201,
  struct _M0TWssbEu* _M0L12_2adiscard__S1202,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1203
) {
  struct moonbit_result_0 _result_2713;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _result_2713.tag = 1;
  _result_2713.data.ok = 0;
  return _result_2713;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1173
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1151,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1152,
  moonbit_string_t _M0L3symS1157,
  float _M0L2muS1153,
  float _M0L5sigmaS1154,
  float _M0L1pS1155,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1156
) {
  int32_t _M0L1nS2598;
  int32_t _M0L1nS2599;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1150;
  float* _M0L6_2atmpS2597;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2588;
  float* _M0L6_2atmpS2596;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2589;
  float* _M0L6_2atmpS2595;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2590;
  int32_t* _M0L6_2atmpS2594;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2591;
  float* _M0L6_2atmpS2593;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2592;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2714;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2598 = _M0L3preS1151->$2;
  _M0L1nS2599 = _M0L4postS1152->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1150
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2598, _M0L1nS2599, _M0L2muS1153, _M0L5sigmaS1154, _M0L1pS1155, _M0L3rngS1156);
  _M0L6_2atmpS2597 = moonbit_empty_float_array;
  _M0L6_2atmpS2588
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2588)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2588->$0 = _M0L6_2atmpS2597;
  _M0L6_2atmpS2588->$1 = 0;
  _M0L6_2atmpS2596 = moonbit_empty_float_array;
  _M0L6_2atmpS2589
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2589)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2589->$0 = _M0L6_2atmpS2596;
  _M0L6_2atmpS2589->$1 = 0;
  _M0L6_2atmpS2595 = moonbit_empty_float_array;
  _M0L6_2atmpS2590
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2590)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2590->$0 = _M0L6_2atmpS2595;
  _M0L6_2atmpS2590->$1 = 0;
  _M0L6_2atmpS2594 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2591
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2591)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2591->$0 = _M0L6_2atmpS2594;
  _M0L6_2atmpS2591->$1 = 0;
  _M0L6_2atmpS2593 = moonbit_empty_float_array;
  _M0L6_2atmpS2592
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2592)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2592->$0 = _M0L6_2atmpS2593;
  _M0L6_2atmpS2592->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1151);
  moonbit_incref_cycle_free(_M0L4postS1152);
  moonbit_incref_cycle_free(_M0L3symS1157);
  _block_2714
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2714)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2714->$0 = _M0L3preS1151;
  _block_2714->$1 = _M0L4postS1152;
  _block_2714->$2 = _M0L3symS1157;
  _block_2714->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2714->$4 = _M0L6matrixS1150;
  _block_2714->$5 = _M0L6_2atmpS2588;
  _block_2714->$6 = _M0L6_2atmpS2589;
  _block_2714->$7 = _M0L6_2atmpS2590;
  _block_2714->$8 = _M0L6_2atmpS2591;
  _block_2714->$9 = _M0L6_2atmpS2592;
  return _block_2714;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter8with__el(
  float _M0L2elS1149
) {
  float _M0L1cS1147;
  float _M0L2glS1148;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2715;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1147 = -0x1p+0f;
  _M0L2glS1148 = -0x1p+0f;
  _block_2715
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2715)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2715->$0 = _M0L1cS1147;
  _block_2715->$1 = _M0L2glS1148;
  _block_2715->$2 = 0x1.ep+3f;
  _block_2715->$3 = -0x1.9p+5f;
  _block_2715->$4 = -0x1.ep+5f;
  _block_2715->$5 = _M0L2elS1149;
  _block_2715->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2715->$7 = 0x1p+1f;
  _block_2715->$8 = 0x0p+0f;
  _block_2715->$9 = 0x0p+0f;
  _block_2715->$10 = 0x0p+0f;
  return _block_2715;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1121,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1123,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1126
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1120;
  float _M0L2vtS2586;
  float _M0L2vrS2587;
  float _M0L6spreadS1122;
  int32_t _M0L7_2abindS1124;
  int32_t _M0L1kS1125;
  struct _M0TPB5ArrayGfE* _M0L1wS1128;
  struct _M0TPB5ArrayGbE* _M0L4fireS1129;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1130;
  struct _M0TPB5ArrayGfE* _M0L1iS1131;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1132;
  struct _M0TPB5ArrayGfE* _M0L2geS1133;
  struct _M0TPB5ArrayGfE* _M0L2giS1134;
  struct _M0TPB5ArrayGfE* _M0L2heS1135;
  struct _M0TPB5ArrayGfE* _M0L2hiS1136;
  struct _M0TPB5ArrayGfE* _M0L3gluS1137;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1138;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1139;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1140;
  float _M0L4e__eS1141;
  float _M0L4e__iS1142;
  float _M0L3treS1143;
  float _M0L3tdeS1144;
  float _M0L3triS1145;
  float _M0L3tdiS1146;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2585;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2717;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1120 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  _M0L2vtS2586 = _M0L5paramS1123->$3;
  _M0L2vrS2587 = _M0L5paramS1123->$4;
  _M0L6spreadS1122 = _M0L2vtS2586 - _M0L2vrS2587;
  _M0L7_2abindS1124 = 0;
  _M0L1kS1125 = _M0L7_2abindS1124;
  while (1) {
    if (_M0L1kS1125 < _M0L1nS1121) {
      float _M0L2vrS2581 = _M0L5paramS1123->$4;
      float _M0L6_2atmpS2583;
      float _M0L6_2atmpS2582;
      float _M0L6_2atmpS2580;
      int32_t _M0L6_2atmpS2584;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2583 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1126);
      _M0L6_2atmpS2582 = _M0L6_2atmpS2583 * _M0L6spreadS1122;
      _M0L6_2atmpS2580 = _M0L2vrS2581 + _M0L6_2atmpS2582;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1120, _M0L1kS1125, _M0L6_2atmpS2580);
      _M0L6_2atmpS2584 = _M0L1kS1125 + 1;
      _M0L1kS1125 = _M0L6_2atmpS2584;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1128 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1129 = _M0MPC15array5Array4makeGbE(_M0L1nS1121, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1130 = _M0MPC15array5Array4makeGiE(_M0L1nS1121, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1131 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1132 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1133 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1134 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1135 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1136 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1137 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1138 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1139 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1140 = _M0MPC15array5Array4makeGfE(_M0L1nS1121, 0x1p+0f);
  _M0L4e__eS1141 = 0x0p+0f;
  _M0L4e__iS1142 = -0x1.2cp+6f;
  _M0L3treS1143 = 0x1p+0f;
  _M0L3tdeS1144 = 0x1.8p+2f;
  _M0L3triS1145 = 0x1p-1f;
  _M0L3tdiS1146 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2585 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1123);
  _block_2717
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2717)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2717->$0 = _M0L5paramS1123;
  _block_2717->$1 = _M0L6_2atmpS2585;
  _block_2717->$2 = _M0L1nS1121;
  _block_2717->$3 = _M0L1vS1120;
  _block_2717->$4 = _M0L1wS1128;
  _block_2717->$5 = _M0L4fireS1129;
  _block_2717->$6 = _M0L4tabsS1130;
  _block_2717->$7 = _M0L1iS1131;
  _block_2717->$8 = _M0L9syn__currS1132;
  _block_2717->$9 = _M0L2geS1133;
  _block_2717->$10 = _M0L2giS1134;
  _block_2717->$11 = _M0L2heS1135;
  _block_2717->$12 = _M0L2hiS1136;
  _block_2717->$13 = _M0L3gluS1137;
  _block_2717->$14 = _M0L4gabaS1138;
  _block_2717->$15 = _M0L7gsyn__eS1139;
  _block_2717->$16 = _M0L7gsyn__iS1140;
  _block_2717->$17 = _M0L4e__eS1141;
  _block_2717->$18 = _M0L4e__iS1142;
  _block_2717->$19 = _M0L3treS1143;
  _block_2717->$20 = _M0L3tdeS1144;
  _block_2717->$21 = _M0L3triS1145;
  _block_2717->$22 = _M0L3tdiS1146;
  return _block_2717;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2718;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2718
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2718)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2718->$0 = 0x1p+1f;
  return _block_2718;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt8sim__for(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS1116,
  float _M0L8durationS1115
) {
  float _M0L2dtS1112;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS1113;
  float _M0L6_2atmpS2579;
  int32_t _M0L5stepsS1114;
  int32_t _M0L7_2abindS1117;
  int32_t _M0L2__S1118;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L2dtS1112 = 0x1p-3f;
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L4timeS1113 = _M0MP26RiantR8snn__mbt4Time3new();
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt7set__dt(_M0L4timeS1113, _M0L2dtS1112);
  _M0L6_2atmpS2579 = _M0L8durationS1115 / _M0L2dtS1112;
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L5stepsS1114 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2579);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12record__zero(_M0L5modelS1116);
  _M0L7_2abindS1117 = 0;
  _M0L2__S1118 = _M0L7_2abindS1117;
  while (1) {
    if (_M0L2__S1118 < _M0L5stepsS1114) {
      int32_t _M0L6_2atmpS2578;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11step__model(_M0L5modelS1116, _M0L4timeS1113);
      _M0L6_2atmpS2578 = _M0L2__S1118 + 1;
      _M0L2__S1118 = _M0L6_2atmpS2578;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4timeS1113);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11step__model(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS1087,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS1085
) {
  float _M0L6t__nowS1084;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1086;
  int32_t _M0L7_2abindS1088;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1089;
  int32_t _M0L2__S1090;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1093;
  int32_t _M0L7_2abindS1094;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1095;
  int32_t _M0L2__S1096;
  float _M0L2dtS1099;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L7_2abindS1100;
  int32_t _M0L7_2abindS1101;
  struct _M0TP26RiantR8snn__mbt2IF** _M0L7_2abindS1102;
  int32_t _M0L2__S1103;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1106;
  int32_t _M0L7_2abindS1107;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1108;
  int32_t _M0L2__S1109;
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6t__nowS1084 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS1085);
  _M0L7_2abindS1086 = _M0L5modelS1087->$1;
  _M0L7_2abindS1088 = _M0L7_2abindS1086->$1;
  _M0L7_2abindS1089 = _M0L7_2abindS1086->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1089);
  _M0L2__S1090 = 0;
  while (1) {
    if (_M0L2__S1090 < _M0L7_2abindS1088) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1091 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1089[
          _M0L2__S1090
        ];
      int32_t _M0L6_2atmpS2572;
      moonbit_incref_cycle_free(_M0L1cS1091);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1091, _M0L6t__nowS1084);
      moonbit_decref_cycle_free(_M0L1cS1091);
      _M0L6_2atmpS2572 = _M0L2__S1090 + 1;
      _M0L2__S1090 = _M0L6_2atmpS2572;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1089);
    }
    break;
  }
  _M0L7_2abindS1093 = _M0L5modelS1087->$1;
  _M0L7_2abindS1094 = _M0L7_2abindS1093->$1;
  _M0L7_2abindS1095 = _M0L7_2abindS1093->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1095);
  _M0L2__S1096 = 0;
  while (1) {
    if (_M0L2__S1096 < _M0L7_2abindS1094) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1097 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1095[
          _M0L2__S1096
        ];
      int32_t _M0L6_2atmpS2573;
      moonbit_incref_cycle_free(_M0L1cS1097);
      #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1097, _M0L6t__nowS1084);
      moonbit_decref_cycle_free(_M0L1cS1097);
      _M0L6_2atmpS2573 = _M0L2__S1096 + 1;
      _M0L2__S1096 = _M0L6_2atmpS2573;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1095);
    }
    break;
  }
  _M0L2dtS1099 = _M0L4timeS1085->$2;
  _M0L7_2abindS1100 = _M0L5modelS1087->$0;
  _M0L7_2abindS1101 = _M0L7_2abindS1100->$1;
  _M0L7_2abindS1102 = _M0L7_2abindS1100->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1102);
  _M0L2__S1103 = 0;
  while (1) {
    if (_M0L2__S1103 < _M0L7_2abindS1101) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1104 =
        (struct _M0TP26RiantR8snn__mbt2IF*)_M0L7_2abindS1102[_M0L2__S1103];
      int32_t _M0L6_2atmpS2574;
      moonbit_incref_cycle_free(_M0L1pS1104);
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L1pS1104, _M0L2dtS1099);
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1pS1104);
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L1pS1104, _M0L2dtS1099);
      moonbit_decref_cycle_free(_M0L1pS1104);
      _M0L6_2atmpS2574 = _M0L2__S1103 + 1;
      _M0L2__S1103 = _M0L6_2atmpS2574;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1102);
    }
    break;
  }
  _M0L7_2abindS1106 = _M0L5modelS1087->$2;
  _M0L7_2abindS1107 = _M0L7_2abindS1106->$1;
  _M0L7_2abindS1108 = _M0L7_2abindS1106->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1108);
  _M0L2__S1109 = 0;
  while (1) {
    if (_M0L2__S1109 < _M0L7_2abindS1107) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1110 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1108[
          _M0L2__S1109
        ];
      struct _M0TPB5ArrayGfE* _M0L1tS2576 = _M0L4timeS1085->$0;
      float _M0L6_2atmpS2575;
      int32_t _M0L6_2atmpS2577;
      moonbit_incref_cycle_free(_M0L1mS1110);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0L6_2atmpS2575 = _M0MPC15array5Array2atGfE(_M0L1tS2576, 0);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS1110, _M0L6_2atmpS2575);
      moonbit_decref_cycle_free(_M0L1mS1110);
      _M0L6_2atmpS2577 = _M0L2__S1109 + 1;
      _M0L2__S1109 = _M0L6_2atmpS2577;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1108);
    }
    break;
  }
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS1085, _M0L2dtS1099);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1060,
  float _M0L6t__nowS1071
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS2571;
  int32_t _M0L6_2atmpS2570;
  int32_t _M0L10use__delayS1059;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2569;
  int32_t _M0L6_2atmpS2568;
  int32_t _M0L8use__rhoS1061;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS2571 = _M0L1cS1060->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2570 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS2571);
  _M0L10use__delayS1059 = _M0L6_2atmpS2570 > 0;
  _M0L3rhoS2569 = _M0L1cS1060->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2568 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2569);
  _M0L8use__rhoS1061 = _M0L6_2atmpS2568 > 0;
  if (_M0L10use__delayS1059) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2531 = _M0L1cS1060->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS2530 = _M0L3preS2531->$5;
    int32_t _M0L6n__preS1062;
    struct _M0TPB8MutLocalGiE* _M0L1jS1063;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1062 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2530);
    _M0L1jS1063
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1063)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1063->$0 = 0;
    while (1) {
      int32_t _M0L3valS2499 = _M0L1jS1063->$0;
      if (_M0L3valS2499 < _M0L6n__preS1062) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2502 = _M0L1cS1060->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS2500 = _M0L3preS2502->$5;
        int32_t _M0L3valS2501 = _M0L1jS1063->$0;
        int32_t _M0L3valS2529;
        int32_t _M0L6_2atmpS2528;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS2500, _M0L3valS2501)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2527 =
            _M0L1cS1060->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2525 = _M0L6matrixS2527->$2;
          int32_t _M0L3valS2526 = _M0L1jS1063->$0;
          int32_t _M0L5startS1064;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2524;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2521;
          int32_t _M0L3valS2523;
          int32_t _M0L6_2atmpS2522;
          int32_t _M0L3endS1065;
          struct _M0TPB8MutLocalGiE* _M0L1sS1066;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1064
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2525, _M0L3valS2526);
          _M0L6matrixS2524 = _M0L1cS1060->$4;
          _M0L6rowptrS2521 = _M0L6matrixS2524->$2;
          _M0L3valS2523 = _M0L1jS1063->$0;
          _M0L6_2atmpS2522 = _M0L3valS2523 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1065
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2521, _M0L6_2atmpS2522);
          _M0L1sS1066
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1066)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1066->$0 = _M0L5startS1064;
          while (1) {
            int32_t _M0L3valS2503 = _M0L1sS1066->$0;
            if (_M0L3valS2503 < _M0L3endS1065) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2520 =
                _M0L1cS1060->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS2518 = _M0L6matrixS2520->$3;
              int32_t _M0L3valS2519 = _M0L1sS1066->$0;
              int32_t _M0L9post__idxS1067;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2517;
              struct _M0TPB5ArrayGfE* _M0L4valsS2515;
              int32_t _M0L3valS2516;
              float _M0L1wS1068;
              struct _M0TPB5ArrayGfE* _M0L6delaysS2513;
              int32_t _M0L3valS2514;
              float _M0L1dS1069;
              float _M0L9w__scaledS1070;
              int32_t _M0L3valS2509;
              int32_t _M0L6_2atmpS2508;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1067
              = _M0MPC15array5Array2atGiE(_M0L6colptrS2518, _M0L3valS2519);
              _M0L6matrixS2517 = _M0L1cS1060->$4;
              _M0L4valsS2515 = _M0L6matrixS2517->$4;
              _M0L3valS2516 = _M0L1sS1066->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1068
              = _M0MPC15array5Array2atGfE(_M0L4valsS2515, _M0L3valS2516);
              _M0L6delaysS2513 = _M0L1cS1060->$5;
              _M0L3valS2514 = _M0L1sS1066->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1069
              = _M0MPC15array5Array2atGfE(_M0L6delaysS2513, _M0L3valS2514);
              if (_M0L8use__rhoS1061) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS2511 = _M0L1cS1060->$6;
                int32_t _M0L3valS2512 = _M0L1sS1066->$0;
                float _M0L6_2atmpS2510;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2510
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2511, _M0L3valS2512);
                _M0L9w__scaledS1070 = _M0L1wS1068 * _M0L6_2atmpS2510;
              } else {
                _M0L9w__scaledS1070 = _M0L1wS1068;
              }
              if (_M0L1dS1069 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1060, _M0L9post__idxS1067, _M0L9w__scaledS1070);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS2504 =
                  _M0L1cS1060->$7;
                float _M0L6_2atmpS2505 = _M0L6t__nowS1071 + _M0L1dS1069;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS2506;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2507;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS2504, _M0L6_2atmpS2505);
                _M0L14pending__postsS2506 = _M0L1cS1060->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS2506, _M0L9post__idxS1067);
                _M0L16pending__weightsS2507 = _M0L1cS1060->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS2507, _M0L9w__scaledS1070);
              }
              _M0L3valS2509 = _M0L1sS1066->$0;
              _M0L6_2atmpS2508 = _M0L3valS2509 + 1;
              _M0L1sS1066->$0 = _M0L6_2atmpS2508;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1066);
            }
            break;
          }
        }
        _M0L3valS2529 = _M0L1jS1063->$0;
        _M0L6_2atmpS2528 = _M0L3valS2529 + 1;
        _M0L1jS1063->$0 = _M0L6_2atmpS2528;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1063);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2565 = _M0L1cS1060->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1074;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS2565 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS2565)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS2565, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2565) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2566 = _M0L1cS1060->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2654 = _M0L4postS2566->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS2654);
      _M0L6targetS1074 = _M0L8_2afieldS2654;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2567 = _M0L1cS1060->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2655 = _M0L4postS2567->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS2655);
      _M0L6targetS1074 = _M0L8_2afieldS2655;
    }
    if (_M0L8use__rhoS1061) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2561 = _M0L1cS1060->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2560 = _M0L3preS2561->$5;
      int32_t _M0L6n__preS1075;
      struct _M0TPB8MutLocalGiE* _M0L1jS1076;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1075 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2560);
      _M0L1jS1076
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1076)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1076->$0 = 0;
      while (1) {
        int32_t _M0L3valS2532 = _M0L1jS1076->$0;
        if (_M0L3valS2532 < _M0L6n__preS1075) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2535 = _M0L1cS1060->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS2533 = _M0L3preS2535->$5;
          int32_t _M0L3valS2534 = _M0L1jS1076->$0;
          int32_t _M0L3valS2559;
          int32_t _M0L6_2atmpS2558;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS2533, _M0L3valS2534)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2557 =
              _M0L1cS1060->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2555 = _M0L6matrixS2557->$2;
            int32_t _M0L3valS2556 = _M0L1jS1076->$0;
            int32_t _M0L5startS1077;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2554;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2551;
            int32_t _M0L3valS2553;
            int32_t _M0L6_2atmpS2552;
            int32_t _M0L3endS1078;
            struct _M0TPB8MutLocalGiE* _M0L1sS1079;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1077
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2555, _M0L3valS2556);
            _M0L6matrixS2554 = _M0L1cS1060->$4;
            _M0L6rowptrS2551 = _M0L6matrixS2554->$2;
            _M0L3valS2553 = _M0L1jS1076->$0;
            _M0L6_2atmpS2552 = _M0L3valS2553 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1078
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2551, _M0L6_2atmpS2552);
            _M0L1sS1079
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1079)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1079->$0 = _M0L5startS1077;
            while (1) {
              int32_t _M0L3valS2536 = _M0L1sS1079->$0;
              if (_M0L3valS2536 < _M0L3endS1078) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2550 =
                  _M0L1cS1060->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS2548 =
                  _M0L6matrixS2550->$3;
                int32_t _M0L3valS2549 = _M0L1sS1079->$0;
                int32_t _M0L9post__idxS1080;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2547;
                struct _M0TPB5ArrayGfE* _M0L4valsS2545;
                int32_t _M0L3valS2546;
                float _M0L6_2atmpS2541;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2543;
                int32_t _M0L3valS2544;
                float _M0L6_2atmpS2542;
                float _M0L9w__scaledS1081;
                float _M0L6_2atmpS2538;
                float _M0L6_2atmpS2537;
                int32_t _M0L3valS2540;
                int32_t _M0L6_2atmpS2539;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1080
                = _M0MPC15array5Array2atGiE(_M0L6colptrS2548, _M0L3valS2549);
                _M0L6matrixS2547 = _M0L1cS1060->$4;
                _M0L4valsS2545 = _M0L6matrixS2547->$4;
                _M0L3valS2546 = _M0L1sS1079->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2541
                = _M0MPC15array5Array2atGfE(_M0L4valsS2545, _M0L3valS2546);
                _M0L3rhoS2543 = _M0L1cS1060->$6;
                _M0L3valS2544 = _M0L1sS1079->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2542
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2543, _M0L3valS2544);
                _M0L9w__scaledS1081 = _M0L6_2atmpS2541 * _M0L6_2atmpS2542;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2538
                = _M0MPC15array5Array2atGfE(_M0L6targetS1074, _M0L9post__idxS1080);
                _M0L6_2atmpS2537 = _M0L6_2atmpS2538 + _M0L9w__scaledS1081;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1074, _M0L9post__idxS1080, _M0L6_2atmpS2537);
                _M0L3valS2540 = _M0L1sS1079->$0;
                _M0L6_2atmpS2539 = _M0L3valS2540 + 1;
                _M0L1sS1079->$0 = _M0L6_2atmpS2539;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1079);
              }
              break;
            }
          }
          _M0L3valS2559 = _M0L1jS1076->$0;
          _M0L6_2atmpS2558 = _M0L3valS2559 + 1;
          _M0L1jS1076->$0 = _M0L6_2atmpS2558;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1076);
          moonbit_decref_cycle_free(_M0L6targetS1074);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2562 =
        _M0L1cS1060->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2564 = _M0L1cS1060->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2563 = _M0L3preS2564->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2562, _M0L4fireS2563, _M0L6targetS1074);
      moonbit_decref_cycle_free(_M0L6targetS1074);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1052,
  float _M0L6t__nowS1055
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS2498;
  int32_t _M0L1nS1051;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1053;
  struct _M0TPB8MutLocalGiE* _M0L1kS1054;
  int32_t _M0L3valS2497;
  int32_t _M0L6_2atmpS2496;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1057;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS2498 = _M0L1cS1052->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1051 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS2498);
  if (_M0L1nS1051 == 0) {
    return 0;
  }
  _M0L4keptS1053
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1053)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1053->$0 = 0;
  _M0L1kS1054
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1054)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1054->$0 = 0;
  while (1) {
    int32_t _M0L3valS2459 = _M0L1kS1054->$0;
    if (_M0L3valS2459 < _M0L1nS1051) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS2461 = _M0L1cS1052->$7;
      int32_t _M0L3valS2462 = _M0L1kS1054->$0;
      float _M0L6_2atmpS2460;
      int32_t _M0L3valS2489;
      int32_t _M0L6_2atmpS2488;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2460
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS2461, _M0L3valS2462);
      if (_M0L6_2atmpS2460 <= _M0L6t__nowS1055) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS2467 = _M0L1cS1052->$8;
        int32_t _M0L3valS2468 = _M0L1kS1054->$0;
        int32_t _M0L6_2atmpS2463;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2465;
        int32_t _M0L3valS2466;
        float _M0L6_2atmpS2464;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS2463
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS2467, _M0L3valS2468);
        _M0L16pending__weightsS2465 = _M0L1cS1052->$9;
        _M0L3valS2466 = _M0L1kS1054->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS2464
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS2465, _M0L3valS2466);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1052, _M0L6_2atmpS2463, _M0L6_2atmpS2464);
      } else {
        int32_t _M0L3valS2469 = _M0L4keptS1053->$0;
        int32_t _M0L3valS2470 = _M0L1kS1054->$0;
        int32_t _M0L3valS2487;
        int32_t _M0L6_2atmpS2486;
        if (_M0L3valS2469 != _M0L3valS2470) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS2471 = _M0L1cS1052->$7;
          int32_t _M0L3valS2472 = _M0L4keptS1053->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS2474 = _M0L1cS1052->$7;
          int32_t _M0L3valS2475 = _M0L1kS1054->$0;
          float _M0L6_2atmpS2473;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS2476;
          int32_t _M0L3valS2477;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS2479;
          int32_t _M0L3valS2480;
          int32_t _M0L6_2atmpS2478;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2481;
          int32_t _M0L3valS2482;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2484;
          int32_t _M0L3valS2485;
          float _M0L6_2atmpS2483;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2473
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS2474, _M0L3valS2475);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS2471, _M0L3valS2472, _M0L6_2atmpS2473);
          _M0L14pending__postsS2476 = _M0L1cS1052->$8;
          _M0L3valS2477 = _M0L4keptS1053->$0;
          _M0L14pending__postsS2479 = _M0L1cS1052->$8;
          _M0L3valS2480 = _M0L1kS1054->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2478
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS2479, _M0L3valS2480);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS2476, _M0L3valS2477, _M0L6_2atmpS2478);
          _M0L16pending__weightsS2481 = _M0L1cS1052->$9;
          _M0L3valS2482 = _M0L4keptS1053->$0;
          _M0L16pending__weightsS2484 = _M0L1cS1052->$9;
          _M0L3valS2485 = _M0L1kS1054->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2483
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS2484, _M0L3valS2485);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS2481, _M0L3valS2482, _M0L6_2atmpS2483);
        }
        _M0L3valS2487 = _M0L4keptS1053->$0;
        _M0L6_2atmpS2486 = _M0L3valS2487 + 1;
        _M0L4keptS1053->$0 = _M0L6_2atmpS2486;
      }
      _M0L3valS2489 = _M0L1kS1054->$0;
      _M0L6_2atmpS2488 = _M0L3valS2489 + 1;
      _M0L1kS1054->$0 = _M0L6_2atmpS2488;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1054);
    }
    break;
  }
  _M0L3valS2497 = _M0L4keptS1053->$0;
  moonbit_decref_cycle_free(_M0L4keptS1053);
  _M0L6_2atmpS2496 = _M0L1nS1051 - _M0L3valS2497;
  _M0L4dropS1057
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1057)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1057->$0 = _M0L6_2atmpS2496;
  while (1) {
    int32_t _M0L3valS2490 = _M0L4dropS1057->$0;
    if (_M0L3valS2490 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS2491 = _M0L1cS1052->$7;
      void* _M0L6_2atmpS2657;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS2492;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2493;
      void* _M0L6_2atmpS2656;
      int32_t _M0L3valS2495;
      int32_t _M0L6_2atmpS2494;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2657
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS2491);
      moonbit_decref_cycle_free(_M0L6_2atmpS2657);
      _M0L14pending__postsS2492 = _M0L1cS1052->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS2492);
      _M0L16pending__weightsS2493 = _M0L1cS1052->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2656
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS2493);
      moonbit_decref_cycle_free(_M0L6_2atmpS2656);
      _M0L3valS2495 = _M0L4dropS1057->$0;
      _M0L6_2atmpS2494 = _M0L3valS2495 - 1;
      _M0L4dropS1057->$0 = _M0L6_2atmpS2494;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS1057);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1048,
  int32_t _M0L9post__idxS1049,
  float _M0L1wS1050
) {
  moonbit_string_t _M0L3symS2446;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS2446 = _M0L1cS1048->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS2446 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2446)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2446, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2446) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2452 = _M0L1cS1048->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS2447 = _M0L4postS2452->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2451 = _M0L1cS1048->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS2450 = _M0L4postS2451->$13;
    float _M0L6_2atmpS2449;
    float _M0L6_2atmpS2448;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS2449
    = _M0MPC15array5Array2atGfE(_M0L3gluS2450, _M0L9post__idxS1049);
    _M0L6_2atmpS2448 = _M0L6_2atmpS2449 + _M0L1wS1050;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS2447, _M0L9post__idxS1049, _M0L6_2atmpS2448);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2458 = _M0L1cS1048->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS2453 = _M0L4postS2458->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2457 = _M0L1cS1048->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS2456 = _M0L4postS2457->$14;
    float _M0L6_2atmpS2455;
    float _M0L6_2atmpS2454;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS2455
    = _M0MPC15array5Array2atGfE(_M0L4gabaS2456, _M0L9post__idxS1049);
    _M0L6_2atmpS2454 = _M0L6_2atmpS2455 + _M0L1wS1050;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS2453, _M0L9post__idxS1049, _M0L6_2atmpS2454);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS1042
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1041;
  int32_t _M0L7_2abindS1043;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1044;
  int32_t _M0L2__S1045;
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS1041 = _M0L5modelS1042->$2;
  _M0L7_2abindS1043 = _M0L7_2abindS1041->$1;
  _M0L7_2abindS1044 = _M0L7_2abindS1041->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1044);
  _M0L2__S1045 = 0;
  while (1) {
    if (_M0L2__S1045 < _M0L7_2abindS1043) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1046 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1044[
          _M0L2__S1045
        ];
      int32_t _M0L6_2atmpS2445;
      moonbit_incref_cycle_free(_M0L1mS1046);
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS1046, 0x0p+0f);
      moonbit_decref_cycle_free(_M0L1mS1046);
      _M0L6_2atmpS2445 = _M0L2__S1045 + 1;
      _M0L2__S1045 = _M0L6_2atmpS2445;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1044);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1038,
  float _M0L1tS1040
) {
  int32_t _M0L11step__countS2431;
  int32_t _M0L6_2atmpS2430;
  int32_t _M0L11step__countS2433;
  int32_t _M0L9rec__stepS2434;
  int32_t _M0L6_2atmpS2432;
  moonbit_string_t _M0L3symS2437;
  float _M0L1vS1039;
  struct _M0TPB5ArrayGfE* _M0L4dataS2435;
  struct _M0TPB5ArrayGfE* _M0L5timesS2436;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS2431 = _M0L1mS1038->$6;
  _M0L6_2atmpS2430 = _M0L11step__countS2431 + 1;
  _M0L1mS1038->$6 = _M0L6_2atmpS2430;
  _M0L11step__countS2433 = _M0L1mS1038->$6;
  _M0L9rec__stepS2434 = _M0L1mS1038->$5;
  _M0L6_2atmpS2432 = _M0L11step__countS2433 % _M0L9rec__stepS2434;
  if (_M0L6_2atmpS2432 != 0) {
    return 0;
  }
  _M0L3symS2437 = _M0L1mS1038->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS2437 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS2437)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS2437, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS2437) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS2440 = _M0L1mS1038->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS2438 = _M0L3popS2440->$3;
    int32_t _M0L6neuronS2439 = _M0L1mS1038->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1039 = _M0MPC15array5Array2atGfE(_M0L1vS2438, _M0L6neuronS2439);
  } else {
    moonbit_string_t _M0L3symS2441 = _M0L1mS1038->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS2441 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS2441)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS2441, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS2441) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS2444 = _M0L1mS1038->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2442 = _M0L3popS2444->$5;
      int32_t _M0L6neuronS2443 = _M0L1mS1038->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2442, _M0L6neuronS2443)) {
        _M0L1vS1039 = 0x1p+0f;
      } else {
        _M0L1vS1039 = 0x0p+0f;
      }
    } else {
      _M0L1vS1039 = 0x0p+0f;
    }
  }
  _M0L4dataS2435 = _M0L1mS1038->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS2435, _M0L1vS1039);
  _M0L5timesS2436 = _M0L1mS1038->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS2436, _M0L1tS1040);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1036,
  int32_t _M0L6neuronS1037
) {
  float* _M0L6_2atmpS2429;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2426;
  float* _M0L6_2atmpS2428;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2427;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_2731;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS2429 = moonbit_empty_float_array;
  _M0L6_2atmpS2426
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2426)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2426->$0 = _M0L6_2atmpS2429;
  _M0L6_2atmpS2426->$1 = 0;
  _M0L6_2atmpS2428 = moonbit_empty_float_array;
  _M0L6_2atmpS2427
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2427)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2427->$0 = _M0L6_2atmpS2428;
  _M0L6_2atmpS2427->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1036);
  _block_2731
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_2731)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2731->$0 = _M0L3popS1036;
  _block_2731->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_2731->$2 = _M0L6_2atmpS2426;
  _block_2731->$3 = _M0L6_2atmpS2427;
  _block_2731->$4 = _M0L6neuronS1037;
  _block_2731->$5 = 1;
  _block_2731->$6 = 0;
  return _block_2731;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1032
) {
  int32_t _M0L1nS1031;
  int32_t _M0L7_2abindS1033;
  int32_t _M0L1iS1034;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1031 = _M0L1pS1032->$2;
  _M0L7_2abindS1033 = 0;
  _M0L1iS1034 = _M0L7_2abindS1033;
  while (1) {
    if (_M0L1iS1034 < _M0L1nS1031) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2403 = _M0L1pS1032->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2424 = _M0L1pS1032->$9;
      float _M0L6_2atmpS2419;
      struct _M0TPB5ArrayGfE* _M0L1vS2423;
      float _M0L6_2atmpS2421;
      float _M0L4e__eS2422;
      float _M0L6_2atmpS2420;
      float _M0L6_2atmpS2416;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2418;
      float _M0L6_2atmpS2417;
      float _M0L6_2atmpS2405;
      struct _M0TPB5ArrayGfE* _M0L2giS2415;
      float _M0L6_2atmpS2410;
      struct _M0TPB5ArrayGfE* _M0L1vS2414;
      float _M0L6_2atmpS2412;
      float _M0L4e__iS2413;
      float _M0L6_2atmpS2411;
      float _M0L6_2atmpS2407;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2409;
      float _M0L6_2atmpS2408;
      float _M0L6_2atmpS2406;
      float _M0L6_2atmpS2404;
      int32_t _M0L6_2atmpS2425;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2419 = _M0MPC15array5Array2atGfE(_M0L2geS2424, _M0L1iS1034);
      _M0L1vS2423 = _M0L1pS1032->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2421 = _M0MPC15array5Array2atGfE(_M0L1vS2423, _M0L1iS1034);
      _M0L4e__eS2422 = _M0L1pS1032->$17;
      _M0L6_2atmpS2420 = _M0L6_2atmpS2421 - _M0L4e__eS2422;
      _M0L6_2atmpS2416 = _M0L6_2atmpS2419 * _M0L6_2atmpS2420;
      _M0L7gsyn__eS2418 = _M0L1pS1032->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2417
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2418, _M0L1iS1034);
      _M0L6_2atmpS2405 = _M0L6_2atmpS2416 * _M0L6_2atmpS2417;
      _M0L2giS2415 = _M0L1pS1032->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2410 = _M0MPC15array5Array2atGfE(_M0L2giS2415, _M0L1iS1034);
      _M0L1vS2414 = _M0L1pS1032->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2412 = _M0MPC15array5Array2atGfE(_M0L1vS2414, _M0L1iS1034);
      _M0L4e__iS2413 = _M0L1pS1032->$18;
      _M0L6_2atmpS2411 = _M0L6_2atmpS2412 - _M0L4e__iS2413;
      _M0L6_2atmpS2407 = _M0L6_2atmpS2410 * _M0L6_2atmpS2411;
      _M0L7gsyn__iS2409 = _M0L1pS1032->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2408
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2409, _M0L1iS1034);
      _M0L6_2atmpS2406 = _M0L6_2atmpS2407 * _M0L6_2atmpS2408;
      _M0L6_2atmpS2404 = _M0L6_2atmpS2405 + _M0L6_2atmpS2406;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2403, _M0L1iS1034, _M0L6_2atmpS2404);
      _M0L6_2atmpS2425 = _M0L1iS1034 + 1;
      _M0L1iS1034 = _M0L6_2atmpS2425;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1023,
  float _M0L2dtS1026
) {
  int32_t _M0L1nS1022;
  int32_t _M0L7_2abindS1024;
  int32_t _M0L1iS1025;
  int32_t _M0L7_2abindS1028;
  int32_t _M0L1iS1029;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1022 = _M0L1pS1023->$2;
  _M0L7_2abindS1024 = 0;
  _M0L1iS1025 = _M0L7_2abindS1024;
  while (1) {
    if (_M0L1iS1025 < _M0L1nS1022) {
      struct _M0TPB5ArrayGfE* _M0L2heS2341 = _M0L1pS1023->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2346 = _M0L1pS1023->$11;
      float _M0L6_2atmpS2343;
      struct _M0TPB5ArrayGfE* _M0L3gluS2345;
      float _M0L6_2atmpS2344;
      float _M0L6_2atmpS2342;
      struct _M0TPB5ArrayGfE* _M0L2hiS2347;
      struct _M0TPB5ArrayGfE* _M0L2hiS2352;
      float _M0L6_2atmpS2349;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2351;
      float _M0L6_2atmpS2350;
      float _M0L6_2atmpS2348;
      struct _M0TPB5ArrayGfE* _M0L2geS2353;
      struct _M0TPB5ArrayGfE* _M0L2geS2365;
      float _M0L6_2atmpS2355;
      struct _M0TPB5ArrayGfE* _M0L2geS2364;
      float _M0L6_2atmpS2363;
      float _M0L6_2atmpS2361;
      float _M0L3tdeS2362;
      float _M0L6_2atmpS2358;
      struct _M0TPB5ArrayGfE* _M0L2heS2360;
      float _M0L6_2atmpS2359;
      float _M0L6_2atmpS2357;
      float _M0L6_2atmpS2356;
      float _M0L6_2atmpS2354;
      struct _M0TPB5ArrayGfE* _M0L2heS2366;
      struct _M0TPB5ArrayGfE* _M0L2heS2375;
      float _M0L6_2atmpS2368;
      struct _M0TPB5ArrayGfE* _M0L2heS2374;
      float _M0L6_2atmpS2373;
      float _M0L6_2atmpS2371;
      float _M0L3treS2372;
      float _M0L6_2atmpS2370;
      float _M0L6_2atmpS2369;
      float _M0L6_2atmpS2367;
      struct _M0TPB5ArrayGfE* _M0L2giS2376;
      struct _M0TPB5ArrayGfE* _M0L2giS2388;
      float _M0L6_2atmpS2378;
      struct _M0TPB5ArrayGfE* _M0L2giS2387;
      float _M0L6_2atmpS2386;
      float _M0L6_2atmpS2384;
      float _M0L3tdiS2385;
      float _M0L6_2atmpS2381;
      struct _M0TPB5ArrayGfE* _M0L2hiS2383;
      float _M0L6_2atmpS2382;
      float _M0L6_2atmpS2380;
      float _M0L6_2atmpS2379;
      float _M0L6_2atmpS2377;
      struct _M0TPB5ArrayGfE* _M0L2hiS2389;
      struct _M0TPB5ArrayGfE* _M0L2hiS2398;
      float _M0L6_2atmpS2391;
      struct _M0TPB5ArrayGfE* _M0L2hiS2397;
      float _M0L6_2atmpS2396;
      float _M0L6_2atmpS2394;
      float _M0L3triS2395;
      float _M0L6_2atmpS2393;
      float _M0L6_2atmpS2392;
      float _M0L6_2atmpS2390;
      int32_t _M0L6_2atmpS2399;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2343 = _M0MPC15array5Array2atGfE(_M0L2heS2346, _M0L1iS1025);
      _M0L3gluS2345 = _M0L1pS1023->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2344
      = _M0MPC15array5Array2atGfE(_M0L3gluS2345, _M0L1iS1025);
      _M0L6_2atmpS2342 = _M0L6_2atmpS2343 + _M0L6_2atmpS2344;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2341, _M0L1iS1025, _M0L6_2atmpS2342);
      _M0L2hiS2347 = _M0L1pS1023->$12;
      _M0L2hiS2352 = _M0L1pS1023->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2349 = _M0MPC15array5Array2atGfE(_M0L2hiS2352, _M0L1iS1025);
      _M0L4gabaS2351 = _M0L1pS1023->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2350
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2351, _M0L1iS1025);
      _M0L6_2atmpS2348 = _M0L6_2atmpS2349 + _M0L6_2atmpS2350;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2347, _M0L1iS1025, _M0L6_2atmpS2348);
      _M0L2geS2353 = _M0L1pS1023->$9;
      _M0L2geS2365 = _M0L1pS1023->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2355 = _M0MPC15array5Array2atGfE(_M0L2geS2365, _M0L1iS1025);
      _M0L2geS2364 = _M0L1pS1023->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2363 = _M0MPC15array5Array2atGfE(_M0L2geS2364, _M0L1iS1025);
      _M0L6_2atmpS2361 = -_M0L6_2atmpS2363;
      _M0L3tdeS2362 = _M0L1pS1023->$20;
      _M0L6_2atmpS2358 = _M0L6_2atmpS2361 / _M0L3tdeS2362;
      _M0L2heS2360 = _M0L1pS1023->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2359 = _M0MPC15array5Array2atGfE(_M0L2heS2360, _M0L1iS1025);
      _M0L6_2atmpS2357 = _M0L6_2atmpS2358 + _M0L6_2atmpS2359;
      _M0L6_2atmpS2356 = _M0L2dtS1026 * _M0L6_2atmpS2357;
      _M0L6_2atmpS2354 = _M0L6_2atmpS2355 + _M0L6_2atmpS2356;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2353, _M0L1iS1025, _M0L6_2atmpS2354);
      _M0L2heS2366 = _M0L1pS1023->$11;
      _M0L2heS2375 = _M0L1pS1023->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2368 = _M0MPC15array5Array2atGfE(_M0L2heS2375, _M0L1iS1025);
      _M0L2heS2374 = _M0L1pS1023->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2373 = _M0MPC15array5Array2atGfE(_M0L2heS2374, _M0L1iS1025);
      _M0L6_2atmpS2371 = -_M0L6_2atmpS2373;
      _M0L3treS2372 = _M0L1pS1023->$19;
      _M0L6_2atmpS2370 = _M0L6_2atmpS2371 / _M0L3treS2372;
      _M0L6_2atmpS2369 = _M0L2dtS1026 * _M0L6_2atmpS2370;
      _M0L6_2atmpS2367 = _M0L6_2atmpS2368 + _M0L6_2atmpS2369;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2366, _M0L1iS1025, _M0L6_2atmpS2367);
      _M0L2giS2376 = _M0L1pS1023->$10;
      _M0L2giS2388 = _M0L1pS1023->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2378 = _M0MPC15array5Array2atGfE(_M0L2giS2388, _M0L1iS1025);
      _M0L2giS2387 = _M0L1pS1023->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2386 = _M0MPC15array5Array2atGfE(_M0L2giS2387, _M0L1iS1025);
      _M0L6_2atmpS2384 = -_M0L6_2atmpS2386;
      _M0L3tdiS2385 = _M0L1pS1023->$22;
      _M0L6_2atmpS2381 = _M0L6_2atmpS2384 / _M0L3tdiS2385;
      _M0L2hiS2383 = _M0L1pS1023->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2382 = _M0MPC15array5Array2atGfE(_M0L2hiS2383, _M0L1iS1025);
      _M0L6_2atmpS2380 = _M0L6_2atmpS2381 + _M0L6_2atmpS2382;
      _M0L6_2atmpS2379 = _M0L2dtS1026 * _M0L6_2atmpS2380;
      _M0L6_2atmpS2377 = _M0L6_2atmpS2378 + _M0L6_2atmpS2379;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2376, _M0L1iS1025, _M0L6_2atmpS2377);
      _M0L2hiS2389 = _M0L1pS1023->$12;
      _M0L2hiS2398 = _M0L1pS1023->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2391 = _M0MPC15array5Array2atGfE(_M0L2hiS2398, _M0L1iS1025);
      _M0L2hiS2397 = _M0L1pS1023->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2396 = _M0MPC15array5Array2atGfE(_M0L2hiS2397, _M0L1iS1025);
      _M0L6_2atmpS2394 = -_M0L6_2atmpS2396;
      _M0L3triS2395 = _M0L1pS1023->$21;
      _M0L6_2atmpS2393 = _M0L6_2atmpS2394 / _M0L3triS2395;
      _M0L6_2atmpS2392 = _M0L2dtS1026 * _M0L6_2atmpS2393;
      _M0L6_2atmpS2390 = _M0L6_2atmpS2391 + _M0L6_2atmpS2392;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2389, _M0L1iS1025, _M0L6_2atmpS2390);
      _M0L6_2atmpS2399 = _M0L1iS1025 + 1;
      _M0L1iS1025 = _M0L6_2atmpS2399;
      continue;
    }
    break;
  }
  _M0L7_2abindS1028 = 0;
  _M0L1iS1029 = _M0L7_2abindS1028;
  while (1) {
    if (_M0L1iS1029 < _M0L1nS1022) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2400 = _M0L1pS1023->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2401;
      int32_t _M0L6_2atmpS2402;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2400, _M0L1iS1029, 0x0p+0f);
      _M0L4gabaS2401 = _M0L1pS1023->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2401, _M0L1iS1029, 0x0p+0f);
      _M0L6_2atmpS2402 = _M0L1iS1029 + 1;
      _M0L1iS1029 = _M0L6_2atmpS2402;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1008,
  float _M0L2dtS1017
) {
  int32_t _M0L1nS1007;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1009;
  float _M0L2tmS1010;
  float _M0L2elS1011;
  float _M0L1rS1012;
  float _M0L2vtS1013;
  float _M0L2vrS1014;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2340;
  float _M0L11tabs__constS1015;
  float _M0L6_2atmpS2339;
  int32_t _M0L11tabs__stepsS1016;
  int32_t _M0L7_2abindS1018;
  int32_t _M0L1iS1019;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1007 = _M0L1pS1008->$2;
  _M0L3p__S1009 = _M0L1pS1008->$0;
  _M0L2tmS1010 = _M0L3p__S1009->$2;
  _M0L2elS1011 = _M0L3p__S1009->$5;
  _M0L1rS1012 = _M0L3p__S1009->$6;
  _M0L2vtS1013 = _M0L3p__S1009->$3;
  _M0L2vrS1014 = _M0L3p__S1009->$4;
  _M0L5spikeS2340 = _M0L1pS1008->$1;
  _M0L11tabs__constS1015 = _M0L5spikeS2340->$0;
  _M0L6_2atmpS2339 = _M0L11tabs__constS1015 / _M0L2dtS1017;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1016 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2339);
  _M0L7_2abindS1018 = 0;
  _M0L1iS1019 = _M0L7_2abindS1018;
  while (1) {
    if (_M0L1iS1019 < _M0L1nS1007) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2299 = _M0L1pS1008->$6;
      int32_t _M0L6_2atmpS2298;
      struct _M0TPB5ArrayGfE* _M0L1vS2305;
      struct _M0TPB5ArrayGfE* _M0L1vS2326;
      float _M0L6_2atmpS2307;
      float _M0L6_2atmpS2309;
      struct _M0TPB5ArrayGfE* _M0L1vS2325;
      float _M0L6_2atmpS2324;
      float _M0L6_2atmpS2323;
      float _M0L6_2atmpS2315;
      struct _M0TPB5ArrayGfE* _M0L1wS2322;
      float _M0L6_2atmpS2321;
      float _M0L6_2atmpS2318;
      struct _M0TPB5ArrayGfE* _M0L1iS2320;
      float _M0L6_2atmpS2319;
      float _M0L6_2atmpS2317;
      float _M0L6_2atmpS2316;
      float _M0L6_2atmpS2311;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2314;
      float _M0L6_2atmpS2313;
      float _M0L6_2atmpS2312;
      float _M0L6_2atmpS2310;
      float _M0L6_2atmpS2308;
      float _M0L6_2atmpS2306;
      struct _M0TPB5ArrayGbE* _M0L4fireS2327;
      struct _M0TPB5ArrayGfE* _M0L1vS2330;
      float _M0L6_2atmpS2329;
      int32_t _M0L6_2atmpS2328;
      struct _M0TPB5ArrayGfE* _M0L1vS2331;
      struct _M0TPB5ArrayGbE* _M0L4fireS2333;
      float _M0L6_2atmpS2332;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2335;
      struct _M0TPB5ArrayGbE* _M0L4fireS2337;
      int32_t _M0L6_2atmpS2336;
      int32_t _M0L6_2atmpS2297;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2298
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2299, _M0L1iS1019);
      if (_M0L6_2atmpS2298 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2300 = _M0L1pS1008->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2301;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2304;
        int32_t _M0L6_2atmpS2303;
        int32_t _M0L6_2atmpS2302;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2300, _M0L1iS1019, 0);
        _M0L4tabsS2301 = _M0L1pS1008->$6;
        _M0L4tabsS2304 = _M0L1pS1008->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2303
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2304, _M0L1iS1019);
        _M0L6_2atmpS2302 = _M0L6_2atmpS2303 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2301, _M0L1iS1019, _M0L6_2atmpS2302);
        goto join_1020;
      }
      _M0L1vS2305 = _M0L1pS1008->$3;
      _M0L1vS2326 = _M0L1pS1008->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2307 = _M0MPC15array5Array2atGfE(_M0L1vS2326, _M0L1iS1019);
      _M0L6_2atmpS2309 = _M0L2dtS1017 / _M0L2tmS1010;
      _M0L1vS2325 = _M0L1pS1008->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2324 = _M0MPC15array5Array2atGfE(_M0L1vS2325, _M0L1iS1019);
      _M0L6_2atmpS2323 = _M0L6_2atmpS2324 - _M0L2elS1011;
      _M0L6_2atmpS2315 = -_M0L6_2atmpS2323;
      _M0L1wS2322 = _M0L1pS1008->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2321 = _M0MPC15array5Array2atGfE(_M0L1wS2322, _M0L1iS1019);
      _M0L6_2atmpS2318 = -_M0L6_2atmpS2321;
      _M0L1iS2320 = _M0L1pS1008->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2319 = _M0MPC15array5Array2atGfE(_M0L1iS2320, _M0L1iS1019);
      _M0L6_2atmpS2317 = _M0L6_2atmpS2318 + _M0L6_2atmpS2319;
      _M0L6_2atmpS2316 = _M0L1rS1012 * _M0L6_2atmpS2317;
      _M0L6_2atmpS2311 = _M0L6_2atmpS2315 + _M0L6_2atmpS2316;
      _M0L9syn__currS2314 = _M0L1pS1008->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2313
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2314, _M0L1iS1019);
      _M0L6_2atmpS2312 = _M0L1rS1012 * _M0L6_2atmpS2313;
      _M0L6_2atmpS2310 = _M0L6_2atmpS2311 - _M0L6_2atmpS2312;
      _M0L6_2atmpS2308 = _M0L6_2atmpS2309 * _M0L6_2atmpS2310;
      _M0L6_2atmpS2306 = _M0L6_2atmpS2307 + _M0L6_2atmpS2308;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2305, _M0L1iS1019, _M0L6_2atmpS2306);
      _M0L4fireS2327 = _M0L1pS1008->$5;
      _M0L1vS2330 = _M0L1pS1008->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2329 = _M0MPC15array5Array2atGfE(_M0L1vS2330, _M0L1iS1019);
      _M0L6_2atmpS2328 = _M0L6_2atmpS2329 > _M0L2vtS1013;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2327, _M0L1iS1019, _M0L6_2atmpS2328);
      _M0L1vS2331 = _M0L1pS1008->$3;
      _M0L4fireS2333 = _M0L1pS1008->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2333, _M0L1iS1019)) {
        _M0L6_2atmpS2332 = _M0L2vrS1014;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2334 = _M0L1pS1008->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2332
        = _M0MPC15array5Array2atGfE(_M0L1vS2334, _M0L1iS1019);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2331, _M0L1iS1019, _M0L6_2atmpS2332);
      _M0L4tabsS2335 = _M0L1pS1008->$6;
      _M0L4fireS2337 = _M0L1pS1008->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2337, _M0L1iS1019)) {
        _M0L6_2atmpS2336 = _M0L11tabs__stepsS1016;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2338 = _M0L1pS1008->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2336
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2338, _M0L1iS1019);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2335, _M0L1iS1019, _M0L6_2atmpS2336);
      goto join_1020;
      goto joinlet_2736;
      join_1020:;
      _M0L6_2atmpS2297 = _M0L1iS1019 + 1;
      _M0L1iS1019 = _M0L6_2atmpS2297;
      continue;
      joinlet_2736:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS995,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS998,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1004
) {
  int32_t _M0L4rowsS994;
  int32_t _M0L7_2abindS996;
  int32_t _M0L1iS997;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS994 = _M0L1mS995->$0;
  _M0L7_2abindS996 = 0;
  _M0L1iS997 = _M0L7_2abindS996;
  while (1) {
    if (_M0L1iS997 < _M0L4rowsS994) {
      int32_t _M0L6_2atmpS2296;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS998, _M0L1iS997)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2295 = _M0L1mS995->$2;
        int32_t _M0L5startS999;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2293;
        int32_t _M0L6_2atmpS2294;
        int32_t _M0L3endS1000;
        int32_t _M0L1kS1001;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS999
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2295, _M0L1iS997);
        _M0L6rowptrS2293 = _M0L1mS995->$2;
        _M0L6_2atmpS2294 = _M0L1iS997 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1000
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2293, _M0L6_2atmpS2294);
        _M0L1kS1001 = _M0L5startS999;
        while (1) {
          if (_M0L1kS1001 < _M0L3endS1000) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS2291 = _M0L1mS995->$3;
            int32_t _M0L9post__idxS1002;
            struct _M0TPB5ArrayGfE* _M0L4valsS2290;
            float _M0L1wS1003;
            float _M0L6_2atmpS2289;
            float _M0L6_2atmpS2288;
            int32_t _M0L6_2atmpS2292;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1002
            = _M0MPC15array5Array2atGiE(_M0L6colptrS2291, _M0L1kS1001);
            _M0L4valsS2290 = _M0L1mS995->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1003
            = _M0MPC15array5Array2atGfE(_M0L4valsS2290, _M0L1kS1001);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS2289
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1004, _M0L9post__idxS1002);
            _M0L6_2atmpS2288 = _M0L6_2atmpS2289 + _M0L1wS1003;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1004, _M0L9post__idxS1002, _M0L6_2atmpS2288);
            _M0L6_2atmpS2292 = _M0L1kS1001 + 1;
            _M0L1kS1001 = _M0L6_2atmpS2292;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2296 = _M0L1iS997 + 1;
      _M0L1iS997 = _M0L6_2atmpS2296;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS988,
  int32_t _M0L4colsS989,
  float _M0L2muS990,
  float _M0L5sigmaS991,
  float _M0L1pS992,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS993
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS988, _M0L4colsS989, _M0L2muS990, _M0L5sigmaS991, _M0L1pS992, 0, _M0L3rngS993);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS902,
  int32_t _M0L4colsS906,
  float _M0L2muS912,
  float _M0L5sigmaS913,
  float _M0L1pS925,
  int32_t _M0L4ruleS919,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS915
) {
  float* _M0L6_2atmpS2287;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2286;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS901;
  int32_t _M0L7_2abindS903;
  int32_t _M0L1iS904;
  int32_t _M0L6_2atmpS2285;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS978;
  int32_t* _M0L6_2atmpS2284;
  struct _M0TPB5ArrayGiE* _M0L6colptrS979;
  float* _M0L6_2atmpS2283;
  struct _M0TPB5ArrayGfE* _M0L4valsS980;
  int32_t _M0L7_2abindS981;
  int32_t _M0L1iS982;
  int32_t _M0L6_2atmpS2282;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2758;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2287 = moonbit_empty_float_array;
  _M0L6_2atmpS2286
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2286)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2286->$0 = _M0L6_2atmpS2287;
  _M0L6_2atmpS2286->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS901
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS902, _M0L6_2atmpS2286);
  _M0L7_2abindS903 = 0;
  _M0L1iS904 = _M0L7_2abindS903;
  while (1) {
    if (_M0L1iS904 < _M0L4rowsS902) {
      struct _M0TPB5ArrayGfE* _M0L3rowS905;
      int32_t _M0L7_2abindS907;
      int32_t _M0L1jS908;
      int32_t _M0L6_2atmpS2238;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS905 = _M0MPC15array5Array4makeGfE(_M0L4colsS906, 0x0p+0f);
      _M0L7_2abindS907 = 0;
      _M0L1jS908 = _M0L7_2abindS907;
      while (1) {
        if (_M0L1jS908 < _M0L4colsS906) {
          double _M0L2z1S910;
          struct _M0TUddE* _M0L7_2abindS914;
          double _M0L5_2az1S916;
          float _M0L6_2atmpS2236;
          float _M0L6_2atmpS2235;
          float _M0L1wS911;
          int32_t _M0L6_2atmpS2237;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS914
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS915);
          _M0L5_2az1S916 = _M0L7_2abindS914->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS914);
          _M0L2z1S910 = _M0L5_2az1S916;
          goto join_909;
          goto joinlet_2741;
          join_909:;
          _M0L6_2atmpS2236 = (float)_M0L2z1S910;
          _M0L6_2atmpS2235 = _M0L5sigmaS913 * _M0L6_2atmpS2236;
          _M0L1wS911 = _M0L2muS912 + _M0L6_2atmpS2235;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS905, _M0L1jS908, _M0L1wS911);
          joinlet_2741:;
          _M0L6_2atmpS2237 = _M0L1jS908 + 1;
          _M0L1jS908 = _M0L6_2atmpS2237;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS901, _M0L1iS904, _M0L3rowS905);
      _M0L6_2atmpS2238 = _M0L1iS904 + 1;
      _M0L1iS904 = _M0L6_2atmpS2238;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS919) {
    case 0: {
      int32_t _M0L7_2abindS920 = 0;
      int32_t _M0L1iS921 = _M0L7_2abindS920;
      while (1) {
        if (_M0L1iS921 < _M0L4rowsS902) {
          int32_t _M0L7_2abindS922 = 0;
          int32_t _M0L1jS923 = _M0L7_2abindS922;
          int32_t _M0L6_2atmpS2241;
          while (1) {
            if (_M0L1jS923 < _M0L4colsS906) {
              float _M0L1uS924;
              int32_t _M0L6_2atmpS2240;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS924 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS915);
              if (_M0L1uS924 >= _M0L1pS925) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2239;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2239
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS901, _M0L1iS921);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2239, _M0L1jS923, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2239);
              }
              _M0L6_2atmpS2240 = _M0L1jS923 + 1;
              _M0L1jS923 = _M0L6_2atmpS2240;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2241 = _M0L1iS921 + 1;
          _M0L1iS921 = _M0L6_2atmpS2241;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2259 = (float)_M0L4rowsS902;
      float _M0L6_2atmpS2258 = _M0L6_2atmpS2259 * _M0L1pS925;
      int32_t _M0L7n__keepS928;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS928 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2258);
      if (_M0L7n__keepS928 > 0 && _M0L7n__keepS928 <= _M0L4rowsS902) {
        int32_t _M0L7_2abindS929 = 0;
        int32_t _M0L1jS930 = _M0L7_2abindS929;
        while (1) {
          if (_M0L1jS930 < _M0L4colsS906) {
            int32_t* _M0L6_2atmpS2253 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS931 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS932;
            int32_t _M0L1kS933;
            int32_t _M0L7n__dropS935;
            int32_t _M0L7_2abindS936;
            int32_t _M0L1kS937;
            int32_t _M0L7_2abindS943;
            int32_t _M0L1kS944;
            int32_t _M0L6_2atmpS2254;
            Moonbit_object_header(_M0L8pre__idxS931)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L8pre__idxS931->$0 = _M0L6_2atmpS2253;
            _M0L8pre__idxS931->$1 = 0;
            _M0L7_2abindS932 = 0;
            _M0L1kS933 = _M0L7_2abindS932;
            while (1) {
              if (_M0L1kS933 < _M0L4rowsS902) {
                int32_t _M0L6_2atmpS2242;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS931, _M0L1kS933);
                _M0L6_2atmpS2242 = _M0L1kS933 + 1;
                _M0L1kS933 = _M0L6_2atmpS2242;
                continue;
              }
              break;
            }
            _M0L7n__dropS935 = _M0L4rowsS902 - _M0L7n__keepS928;
            _M0L7_2abindS936 = 0;
            _M0L1kS937 = _M0L7_2abindS936;
            while (1) {
              if (_M0L1kS937 < _M0L7n__dropS935) {
                float _M0L1uS938;
                float _M0L6_2atmpS2246;
                float _M0L6_2atmpS2248;
                float _M0L6_2atmpS2247;
                float _M0L6_2atmpS2245;
                int32_t _M0L6_2atmpS2244;
                int32_t _M0L6r__idxS939;
                int32_t _M0L10r__clampedS940;
                int32_t _M0L3tmpS941;
                int32_t _M0L6_2atmpS2243;
                int32_t _M0L6_2atmpS2249;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS938 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS915);
                _M0L6_2atmpS2246 = (float)_M0L4rowsS902;
                _M0L6_2atmpS2248 = (float)_M0L1kS937;
                _M0L6_2atmpS2247 = _M0L6_2atmpS2248 * _M0L1uS938;
                _M0L6_2atmpS2245 = _M0L6_2atmpS2246 - _M0L6_2atmpS2247;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2244
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2245);
                _M0L6r__idxS939 = _M0L1kS937 + _M0L6_2atmpS2244;
                if (_M0L6r__idxS939 >= _M0L4rowsS902) {
                  _M0L10r__clampedS940 = _M0L4rowsS902 - 1;
                } else {
                  _M0L10r__clampedS940 = _M0L6r__idxS939;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS941
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS931, _M0L1kS937);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2243
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS931, _M0L10r__clampedS940);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS931, _M0L1kS937, _M0L6_2atmpS2243);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS931, _M0L10r__clampedS940, _M0L3tmpS941);
                _M0L6_2atmpS2249 = _M0L1kS937 + 1;
                _M0L1kS937 = _M0L6_2atmpS2249;
                continue;
              }
              break;
            }
            _M0L7_2abindS943 = 0;
            _M0L1kS944 = _M0L7_2abindS943;
            while (1) {
              if (_M0L1kS944 < _M0L7n__dropS935) {
                int32_t _M0L6_2atmpS2251;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2250;
                int32_t _M0L6_2atmpS2252;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2251
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS931, _M0L1kS944);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2250
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS901, _M0L6_2atmpS2251);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2250, _M0L1jS930, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2250);
                _M0L6_2atmpS2252 = _M0L1kS944 + 1;
                _M0L1kS944 = _M0L6_2atmpS2252;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS931);
              }
              break;
            }
            _M0L6_2atmpS2254 = _M0L1jS930 + 1;
            _M0L1jS930 = _M0L6_2atmpS2254;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS928 == 0) {
        int32_t _M0L7_2abindS947 = 0;
        int32_t _M0L1iS948 = _M0L7_2abindS947;
        while (1) {
          if (_M0L1iS948 < _M0L4rowsS902) {
            int32_t _M0L7_2abindS949 = 0;
            int32_t _M0L1jS950 = _M0L7_2abindS949;
            int32_t _M0L6_2atmpS2257;
            while (1) {
              if (_M0L1jS950 < _M0L4colsS906) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2255;
                int32_t _M0L6_2atmpS2256;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2255
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS901, _M0L1iS948);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2255, _M0L1jS950, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2255);
                _M0L6_2atmpS2256 = _M0L1jS950 + 1;
                _M0L1jS950 = _M0L6_2atmpS2256;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2257 = _M0L1iS948 + 1;
            _M0L1iS948 = _M0L6_2atmpS2257;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2277 = (float)_M0L4colsS906;
      float _M0L6_2atmpS2276 = _M0L6_2atmpS2277 * _M0L1pS925;
      int32_t _M0L7n__keepS953;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS953 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2276);
      if (_M0L7n__keepS953 > 0 && _M0L7n__keepS953 <= _M0L4colsS906) {
        int32_t _M0L7_2abindS954 = 0;
        int32_t _M0L1iS955 = _M0L7_2abindS954;
        while (1) {
          if (_M0L1iS955 < _M0L4rowsS902) {
            int32_t* _M0L6_2atmpS2271 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS956 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS957;
            int32_t _M0L1kS958;
            int32_t _M0L7n__dropS960;
            int32_t _M0L7_2abindS961;
            int32_t _M0L1kS962;
            int32_t _M0L7_2abindS968;
            int32_t _M0L1kS969;
            int32_t _M0L6_2atmpS2272;
            Moonbit_object_header(_M0L9post__idxS956)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L9post__idxS956->$0 = _M0L6_2atmpS2271;
            _M0L9post__idxS956->$1 = 0;
            _M0L7_2abindS957 = 0;
            _M0L1kS958 = _M0L7_2abindS957;
            while (1) {
              if (_M0L1kS958 < _M0L4colsS906) {
                int32_t _M0L6_2atmpS2260;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS956, _M0L1kS958);
                _M0L6_2atmpS2260 = _M0L1kS958 + 1;
                _M0L1kS958 = _M0L6_2atmpS2260;
                continue;
              }
              break;
            }
            _M0L7n__dropS960 = _M0L4colsS906 - _M0L7n__keepS953;
            _M0L7_2abindS961 = 0;
            _M0L1kS962 = _M0L7_2abindS961;
            while (1) {
              if (_M0L1kS962 < _M0L7n__dropS960) {
                float _M0L1uS963;
                float _M0L6_2atmpS2264;
                float _M0L6_2atmpS2266;
                float _M0L6_2atmpS2265;
                float _M0L6_2atmpS2263;
                int32_t _M0L6_2atmpS2262;
                int32_t _M0L6r__idxS964;
                int32_t _M0L10r__clampedS965;
                int32_t _M0L3tmpS966;
                int32_t _M0L6_2atmpS2261;
                int32_t _M0L6_2atmpS2267;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS963 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS915);
                _M0L6_2atmpS2264 = (float)_M0L4colsS906;
                _M0L6_2atmpS2266 = (float)_M0L1kS962;
                _M0L6_2atmpS2265 = _M0L6_2atmpS2266 * _M0L1uS963;
                _M0L6_2atmpS2263 = _M0L6_2atmpS2264 - _M0L6_2atmpS2265;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2262
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2263);
                _M0L6r__idxS964 = _M0L1kS962 + _M0L6_2atmpS2262;
                if (_M0L6r__idxS964 >= _M0L4colsS906) {
                  _M0L10r__clampedS965 = _M0L4colsS906 - 1;
                } else {
                  _M0L10r__clampedS965 = _M0L6r__idxS964;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS966
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS956, _M0L1kS962);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2261
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS956, _M0L10r__clampedS965);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS956, _M0L1kS962, _M0L6_2atmpS2261);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS956, _M0L10r__clampedS965, _M0L3tmpS966);
                _M0L6_2atmpS2267 = _M0L1kS962 + 1;
                _M0L1kS962 = _M0L6_2atmpS2267;
                continue;
              }
              break;
            }
            _M0L7_2abindS968 = 0;
            _M0L1kS969 = _M0L7_2abindS968;
            while (1) {
              if (_M0L1kS969 < _M0L7n__dropS960) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2268;
                int32_t _M0L6_2atmpS2269;
                int32_t _M0L6_2atmpS2270;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2268
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS901, _M0L1iS955);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2269
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS956, _M0L1kS969);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2268, _M0L6_2atmpS2269, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2268);
                _M0L6_2atmpS2270 = _M0L1kS969 + 1;
                _M0L1kS969 = _M0L6_2atmpS2270;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS956);
              }
              break;
            }
            _M0L6_2atmpS2272 = _M0L1iS955 + 1;
            _M0L1iS955 = _M0L6_2atmpS2272;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS953 == 0) {
        int32_t _M0L7_2abindS972 = 0;
        int32_t _M0L1iS973 = _M0L7_2abindS972;
        while (1) {
          if (_M0L1iS973 < _M0L4rowsS902) {
            int32_t _M0L7_2abindS974 = 0;
            int32_t _M0L1jS975 = _M0L7_2abindS974;
            int32_t _M0L6_2atmpS2275;
            while (1) {
              if (_M0L1jS975 < _M0L4colsS906) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2273;
                int32_t _M0L6_2atmpS2274;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2273
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS901, _M0L1iS973);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2273, _M0L1jS975, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2273);
                _M0L6_2atmpS2274 = _M0L1jS975 + 1;
                _M0L1jS975 = _M0L6_2atmpS2274;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2275 = _M0L1iS973 + 1;
            _M0L1iS973 = _M0L6_2atmpS2275;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2285 = _M0L4rowsS902 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS978 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2285, 0);
  _M0L6_2atmpS2284 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS979
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS979)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6colptrS979->$0 = _M0L6_2atmpS2284;
  _M0L6colptrS979->$1 = 0;
  _M0L6_2atmpS2283 = moonbit_empty_float_array;
  _M0L4valsS980
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS980)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS980->$0 = _M0L6_2atmpS2283;
  _M0L4valsS980->$1 = 0;
  _M0L7_2abindS981 = 0;
  _M0L1iS982 = _M0L7_2abindS981;
  while (1) {
    if (_M0L1iS982 < _M0L4rowsS902) {
      int32_t _M0L6_2atmpS2278;
      int32_t _M0L7_2abindS983;
      int32_t _M0L1jS984;
      int32_t _M0L6_2atmpS2281;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2278 = _M0MPC15array5Array6lengthGfE(_M0L4valsS980);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS978, _M0L1iS982, _M0L6_2atmpS2278);
      _M0L7_2abindS983 = 0;
      _M0L1jS984 = _M0L7_2abindS983;
      while (1) {
        if (_M0L1jS984 < _M0L4colsS906) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2279;
          float _M0L1vS985;
          int32_t _M0L6_2atmpS2280;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2279
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS901, _M0L1iS982);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS985
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2279, _M0L1jS984);
          moonbit_decref_cycle_free(_M0L6_2atmpS2279);
          if (_M0L1vS985 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS979, _M0L1jS984);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS980, _M0L1vS985);
          }
          _M0L6_2atmpS2280 = _M0L1jS984 + 1;
          _M0L1jS984 = _M0L6_2atmpS2280;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2281 = _M0L1iS982 + 1;
      _M0L1iS982 = _M0L6_2atmpS2281;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS901);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2282 = _M0MPC15array5Array6lengthGfE(_M0L4valsS980);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS978, _M0L4rowsS902, _M0L6_2atmpS2282);
  _block_2758
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2758)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_2758->$0 = _M0L4rowsS902;
  _block_2758->$1 = _M0L4colsS906;
  _block_2758->$2 = _M0L6rowptrS978;
  _block_2758->$3 = _M0L6colptrS979;
  _block_2758->$4 = _M0L4valsS980;
  return _block_2758;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS900
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS2234;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS2234 = _M0L1mS900->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS2234);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS898
) {
  struct _M0TUmmmmE* _M0L1sS897;
  uint64_t _M0L6_2atmpS2233;
  struct _M0TUmmmmE* _M0L1tS899;
  uint64_t _M0L6_2atmpS2229;
  uint64_t _M0L6_2atmpS2230;
  uint64_t _M0L6_2atmpS2231;
  uint64_t _M0L6_2atmpS2232;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2759;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS897 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS898);
  _M0L6_2atmpS2233 = _M0L1sS897->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS899 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2233);
  _M0L6_2atmpS2229 = _M0L1sS897->$0;
  _M0L6_2atmpS2230 = _M0L1sS897->$1;
  _M0L6_2atmpS2231 = _M0L1sS897->$2;
  moonbit_decref_cycle_free(_M0L1sS897);
  _M0L6_2atmpS2232 = _M0L1tS899->$0;
  moonbit_decref_cycle_free(_M0L1tS899);
  _block_2759
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2759)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2759->$0 = _M0L6_2atmpS2229;
  _block_2759->$1 = _M0L6_2atmpS2230;
  _block_2759->$2 = _M0L6_2atmpS2231;
  _block_2759->$3 = _M0L6_2atmpS2232;
  return _block_2759;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS889) {
  uint64_t _M0L2s1S888;
  uint64_t _M0L2z1S890;
  uint64_t _M0L2s2S891;
  uint64_t _M0L2z2S892;
  uint64_t _M0L2s3S893;
  uint64_t _M0L2z3S894;
  uint64_t _M0L2s4S895;
  uint64_t _M0L2z4S896;
  struct _M0TUmmmmE* _block_2760;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S888 = _M0L4seedS889 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S890 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S888);
  _M0L2s2S891 = _M0L2s1S888 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S892 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S891);
  _M0L2s3S893 = _M0L2s2S891 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S894 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S893);
  _M0L2s4S895 = _M0L2s3S893 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S896 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S895);
  _block_2760 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2760)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2760->$0 = _M0L2z1S890;
  _block_2760->$1 = _M0L2z2S892;
  _block_2760->$2 = _M0L2z3S894;
  _block_2760->$3 = _M0L2z4S896;
  return _block_2760;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS886) {
  uint64_t _M0L6_2atmpS2228;
  uint64_t _M0L6_2atmpS2227;
  uint64_t _M0L1zS885;
  uint64_t _M0L6_2atmpS2226;
  uint64_t _M0L6_2atmpS2225;
  uint64_t _M0L1zS887;
  uint64_t _M0L6_2atmpS2224;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2228 = _M0L1zS886 >> 30;
  _M0L6_2atmpS2227 = _M0L1zS886 ^ _M0L6_2atmpS2228;
  _M0L1zS885 = _M0L6_2atmpS2227 * 13787848793156543929ull;
  _M0L6_2atmpS2226 = _M0L1zS885 >> 27;
  _M0L6_2atmpS2225 = _M0L1zS885 ^ _M0L6_2atmpS2226;
  _M0L1zS887 = _M0L6_2atmpS2225 * 10723151780598845931ull;
  _M0L6_2atmpS2224 = _M0L1zS887 >> 31;
  return _M0L1zS887 ^ _M0L6_2atmpS2224;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS880
) {
  double _M0L2u1S879;
  double _M0L8u1__safeS881;
  double _M0L2u2S882;
  double _M0L6_2atmpS2223;
  double _M0L6_2atmpS2222;
  double _M0L1rS883;
  double _M0L5thetaS884;
  double _M0L6_2atmpS2221;
  double _M0L6_2atmpS2218;
  double _M0L6_2atmpS2220;
  double _M0L6_2atmpS2219;
  struct _M0TUddE* _block_2761;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S879 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS880);
  if (_M0L2u1S879 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS881 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS881 = _M0L2u1S879;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S882 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS880);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2223 = _M0FPC14math2ln(_M0L8u1__safeS881);
  _M0L6_2atmpS2222 = -0x1p+1 * _M0L6_2atmpS2223;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS883 = sqrt(_M0L6_2atmpS2222);
  _M0L5thetaS884 = 0x1.921fb54442d18p+2 * _M0L2u2S882;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2221 = _M0FPC14math3cos(_M0L5thetaS884);
  _M0L6_2atmpS2218 = _M0L1rS883 * _M0L6_2atmpS2221;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2220 = _M0FPC14math3sin(_M0L5thetaS884);
  _M0L6_2atmpS2219 = _M0L1rS883 * _M0L6_2atmpS2220;
  _block_2761 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2761)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2761->$0 = _M0L6_2atmpS2218;
  _block_2761->$1 = _M0L6_2atmpS2219;
  return _block_2761;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS877
) {
  uint64_t _M0L1uS876;
  uint64_t _M0L4bitsS878;
  double _M0L6_2atmpS2217;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS876 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS877);
  _M0L4bitsS878 = _M0L1uS876 >> 11;
  _M0L6_2atmpS2217 = (double)_M0L4bitsS878;
  return _M0L6_2atmpS2217 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS874,
  float _M0L2dtS875
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2209;
  struct _M0TPB5ArrayGfE* _M0L1tS2212;
  float _M0L6_2atmpS2211;
  float _M0L6_2atmpS2210;
  struct _M0TPB5ArrayGiE* _M0L2ttS2213;
  struct _M0TPB5ArrayGiE* _M0L2ttS2216;
  int32_t _M0L6_2atmpS2215;
  int32_t _M0L6_2atmpS2214;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2209 = _M0L1tS874->$0;
  _M0L1tS2212 = _M0L1tS874->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2211 = _M0MPC15array5Array2atGfE(_M0L1tS2212, 0);
  _M0L6_2atmpS2210 = _M0L6_2atmpS2211 + _M0L2dtS875;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS2209, 0, _M0L6_2atmpS2210);
  _M0L2ttS2213 = _M0L1tS874->$1;
  _M0L2ttS2216 = _M0L1tS874->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2215 = _M0MPC15array5Array2atGiE(_M0L2ttS2216, 0);
  _M0L6_2atmpS2214 = _M0L6_2atmpS2215 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS2213, 0, _M0L6_2atmpS2214);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS872,
  float _M0L1vS873
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS872->$2 = _M0L1vS873;
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS871
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2208;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2208 = _M0L1tS871->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS2208, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS2207;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2204;
  int32_t* _M0L6_2atmpS2206;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2205;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2762;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2207 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2207[0] = 0x0p+0f;
  _M0L6_2atmpS2204
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2204)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2204->$0 = _M0L6_2atmpS2207;
  _M0L6_2atmpS2204->$1 = 1;
  _M0L6_2atmpS2206 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS2206[0] = 0;
  _M0L6_2atmpS2205
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2205)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2205->$0 = _M0L6_2atmpS2206;
  _M0L6_2atmpS2205->$1 = 1;
  _block_2762
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2762)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 65, 0);
  _block_2762->$0 = _M0L6_2atmpS2204;
  _block_2762->$1 = _M0L6_2atmpS2205;
  _block_2762->$2 = 0x1p-3f;
  return _block_2762;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS869
) {
  uint32_t _M0L1uS868;
  uint32_t _M0L4bitsS870;
  double _M0L6_2atmpS2203;
  double _M0L6_2atmpS2202;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS868 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS869);
  _M0L4bitsS870 = _M0L1uS868 >> 8;
  _M0L6_2atmpS2203 = (double)_M0L4bitsS870;
  _M0L6_2atmpS2202 = _M0L6_2atmpS2203 * 0x1p-24;
  return (float)_M0L6_2atmpS2202;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS867
) {
  uint64_t _M0L1uS866;
  uint64_t _M0L6_2atmpS2201;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS866 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS867);
  _M0L6_2atmpS2201 = _M0L1uS866 >> 32;
  return (uint32_t)_M0L6_2atmpS2201;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS859
) {
  uint64_t _M0L2s0S858;
  uint64_t _M0L2s1S860;
  uint64_t _M0L2s2S861;
  uint64_t _M0L2s3S862;
  uint64_t _M0L3tmpS863;
  uint64_t _M0L6_2atmpS2200;
  uint64_t _M0L3resS864;
  uint64_t _M0L1tS865;
  uint64_t _M0L6_2atmpS2190;
  uint64_t _M0L6_2atmpS2191;
  uint64_t _M0L2s2S2193;
  uint64_t _M0L6_2atmpS2192;
  uint64_t _M0L2s3S2195;
  uint64_t _M0L6_2atmpS2194;
  uint64_t _M0L2s2S2197;
  uint64_t _M0L6_2atmpS2196;
  uint64_t _M0L2s3S2199;
  uint64_t _M0L6_2atmpS2198;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S858 = _M0L1rS859->$0;
  _M0L2s1S860 = _M0L1rS859->$1;
  _M0L2s2S861 = _M0L1rS859->$2;
  _M0L2s3S862 = _M0L1rS859->$3;
  _M0L3tmpS863 = _M0L2s0S858 + _M0L2s3S862;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2200 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS863, 23);
  _M0L3resS864 = _M0L6_2atmpS2200 + _M0L2s0S858;
  _M0L1tS865 = _M0L2s1S860 << 17;
  _M0L6_2atmpS2190 = _M0L2s2S861 ^ _M0L2s0S858;
  _M0L1rS859->$2 = _M0L6_2atmpS2190;
  _M0L6_2atmpS2191 = _M0L2s3S862 ^ _M0L2s1S860;
  _M0L1rS859->$3 = _M0L6_2atmpS2191;
  _M0L2s2S2193 = _M0L1rS859->$2;
  _M0L6_2atmpS2192 = _M0L2s1S860 ^ _M0L2s2S2193;
  _M0L1rS859->$1 = _M0L6_2atmpS2192;
  _M0L2s3S2195 = _M0L1rS859->$3;
  _M0L6_2atmpS2194 = _M0L2s0S858 ^ _M0L2s3S2195;
  _M0L1rS859->$0 = _M0L6_2atmpS2194;
  _M0L2s2S2197 = _M0L1rS859->$2;
  _M0L6_2atmpS2196 = _M0L2s2S2197 ^ _M0L1tS865;
  _M0L1rS859->$2 = _M0L6_2atmpS2196;
  _M0L2s3S2199 = _M0L1rS859->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2198 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2199, 45);
  _M0L1rS859->$3 = _M0L6_2atmpS2198;
  return _M0L3resS864;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS856, int32_t _M0L1kS857) {
  uint64_t _M0L6_2atmpS2187;
  int32_t _M0L6_2atmpS2189;
  uint64_t _M0L6_2atmpS2188;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2187 = _M0L1xS856 << (_M0L1kS857 & 63);
  _M0L6_2atmpS2189 = 64 - _M0L1kS857;
  _M0L6_2atmpS2188 = _M0L1xS856 >> (_M0L6_2atmpS2189 & 63);
  return _M0L6_2atmpS2187 | _M0L6_2atmpS2188;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS849
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS2186;
  int32_t _M0L1nS848;
  struct _M0TPB8MutLocalGiE* _M0L5countS850;
  struct _M0TPB8MutLocalGfE* _M0L4prevS851;
  int32_t _M0L7_2abindS852;
  int32_t _M0L1iS853;
  int32_t _result_2764;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS2186 = _M0L1mS849->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS848 = _M0MPC15array5Array6lengthGfE(_M0L4dataS2186);
  if (_M0L1nS848 == 0) {
    return 0;
  }
  _M0L5countS850
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS850)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS850->$0 = 0;
  _M0L4prevS851
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS851)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS851->$0 = 0x0p+0f;
  _M0L7_2abindS852 = 0;
  _M0L1iS853 = _M0L7_2abindS852;
  while (1) {
    if (_M0L1iS853 < _M0L1nS848) {
      struct _M0TPB5ArrayGfE* _M0L4dataS2184 = _M0L1mS849->$2;
      float _M0L3curS854;
      float _M0L3valS2181;
      int32_t _M0L6_2atmpS2185;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS854 = _M0MPC15array5Array2atGfE(_M0L4dataS2184, _M0L1iS853);
      _M0L3valS2181 = _M0L4prevS851->$0;
      if (_M0L3valS2181 < 0x1p-1f && _M0L3curS854 >= 0x1p-1f) {
        int32_t _M0L3valS2183 = _M0L5countS850->$0;
        int32_t _M0L6_2atmpS2182 = _M0L3valS2183 + 1;
        _M0L5countS850->$0 = _M0L6_2atmpS2182;
      }
      _M0L4prevS851->$0 = _M0L3curS854;
      _M0L6_2atmpS2185 = _M0L1iS853 + 1;
      _M0L1iS853 = _M0L6_2atmpS2185;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS851);
    }
    break;
  }
  _result_2764 = _M0L5countS850->$0;
  moonbit_decref_cycle_free(_M0L5countS850);
  return _result_2764;
}

double _M0FPC14math2ln(double _M0L1xS834) {
  struct _M0TUdiE* _M0L7_2abindS835;
  double _M0L5_2af1S836;
  int32_t _M0L5_2akiS837;
  double _M0L1fS839;
  double _M0L1kS840;
  double _M0L6_2atmpS2174;
  double _M0L1sS841;
  double _M0L2s2S842;
  double _M0L2s4S843;
  double _M0L6_2atmpS2173;
  double _M0L6_2atmpS2172;
  double _M0L6_2atmpS2171;
  double _M0L6_2atmpS2170;
  double _M0L6_2atmpS2169;
  double _M0L6_2atmpS2168;
  double _M0L2t1S844;
  double _M0L6_2atmpS2167;
  double _M0L6_2atmpS2166;
  double _M0L6_2atmpS2165;
  double _M0L6_2atmpS2164;
  double _M0L2t2S845;
  double _M0L1rS846;
  double _M0L6_2atmpS2163;
  double _M0L4hfsqS847;
  double _M0L6_2atmpS2156;
  double _M0L6_2atmpS2162;
  double _M0L6_2atmpS2160;
  double _M0L6_2atmpS2161;
  double _M0L6_2atmpS2159;
  double _M0L6_2atmpS2158;
  double _M0L6_2atmpS2157;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS834 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS834)
      || _M0MPC16double6Double7is__inf(_M0L1xS834)
    ) {
      return _M0L1xS834;
    } else if (_M0L1xS834 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS835 = _M0FPC14math5frexp(_M0L1xS834);
  _M0L5_2af1S836 = _M0L7_2abindS835->$0;
  _M0L5_2akiS837 = _M0L7_2abindS835->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS835);
  if (_M0L5_2af1S836 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2178 = _M0L5_2af1S836 * 0x1p+1;
    double _M0L6_2atmpS2175 = _M0L6_2atmpS2178 - 0x1p+0;
    int32_t _M0L6_2atmpS2177 = _M0L5_2akiS837 - 1;
    double _M0L6_2atmpS2176 = (double)_M0L6_2atmpS2177;
    _M0L1fS839 = _M0L6_2atmpS2175;
    _M0L1kS840 = _M0L6_2atmpS2176;
    goto join_838;
  } else {
    double _M0L6_2atmpS2179 = _M0L5_2af1S836 - 0x1p+0;
    double _M0L6_2atmpS2180 = (double)_M0L5_2akiS837;
    _M0L1fS839 = _M0L6_2atmpS2179;
    _M0L1kS840 = _M0L6_2atmpS2180;
    goto join_838;
  }
  join_838:;
  _M0L6_2atmpS2174 = 0x1p+1 + _M0L1fS839;
  _M0L1sS841 = _M0L1fS839 / _M0L6_2atmpS2174;
  _M0L2s2S842 = _M0L1sS841 * _M0L1sS841;
  _M0L2s4S843 = _M0L2s2S842 * _M0L2s2S842;
  _M0L6_2atmpS2173 = _M0L2s4S843 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2172 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2173;
  _M0L6_2atmpS2171 = _M0L2s4S843 * _M0L6_2atmpS2172;
  _M0L6_2atmpS2170 = 0x1.2492494229359p-2 + _M0L6_2atmpS2171;
  _M0L6_2atmpS2169 = _M0L2s4S843 * _M0L6_2atmpS2170;
  _M0L6_2atmpS2168 = 0x1.5555555555593p-1 + _M0L6_2atmpS2169;
  _M0L2t1S844 = _M0L2s2S842 * _M0L6_2atmpS2168;
  _M0L6_2atmpS2167 = _M0L2s4S843 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2166 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2167;
  _M0L6_2atmpS2165 = _M0L2s4S843 * _M0L6_2atmpS2166;
  _M0L6_2atmpS2164 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2165;
  _M0L2t2S845 = _M0L2s4S843 * _M0L6_2atmpS2164;
  _M0L1rS846 = _M0L2t1S844 + _M0L2t2S845;
  _M0L6_2atmpS2163 = 0x1p-1 * _M0L1fS839;
  _M0L4hfsqS847 = _M0L6_2atmpS2163 * _M0L1fS839;
  _M0L6_2atmpS2156 = _M0L1kS840 * 0x1.62e42feep-1;
  _M0L6_2atmpS2162 = _M0L4hfsqS847 + _M0L1rS846;
  _M0L6_2atmpS2160 = _M0L1sS841 * _M0L6_2atmpS2162;
  _M0L6_2atmpS2161 = _M0L1kS840 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2159 = _M0L6_2atmpS2160 + _M0L6_2atmpS2161;
  _M0L6_2atmpS2158 = _M0L4hfsqS847 - _M0L6_2atmpS2159;
  _M0L6_2atmpS2157 = _M0L6_2atmpS2158 - _M0L1fS839;
  return _M0L6_2atmpS2156 - _M0L6_2atmpS2157;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS827) {
  struct _M0TUdiE* _M0L7_2abindS828;
  double _M0L10_2anorm__fS829;
  int32_t _M0L6_2aexpS830;
  uint64_t _M0L1uS831;
  uint64_t _M0L6_2atmpS2155;
  uint64_t _M0L6_2atmpS2154;
  int32_t _M0L6_2atmpS2153;
  int32_t _M0L6_2atmpS2152;
  int32_t _M0L3expS832;
  uint64_t _M0L6_2atmpS2151;
  uint64_t _M0L6_2atmpS2150;
  uint64_t _M0L6_2atmpS2149;
  double _M0L4fracS833;
  struct _M0TUdiE* _block_2767;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS827 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS827)
    || _M0MPC16double6Double7is__nan(_M0L1fS827)
  ) {
    struct _M0TUdiE* _block_2766 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2766)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2766->$0 = _M0L1fS827;
    _block_2766->$1 = 0;
    return _block_2766;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS828 = _M0FPC14math9normalize(_M0L1fS827);
  _M0L10_2anorm__fS829 = _M0L7_2abindS828->$0;
  _M0L6_2aexpS830 = _M0L7_2abindS828->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS828);
  _M0L1uS831 = *(int64_t*)&_M0L10_2anorm__fS829;
  _M0L6_2atmpS2155 = _M0L1uS831 >> 52;
  _M0L6_2atmpS2154 = _M0L6_2atmpS2155 & 2047ull;
  _M0L6_2atmpS2153 = (int32_t)_M0L6_2atmpS2154;
  _M0L6_2atmpS2152 = _M0L6_2aexpS830 + _M0L6_2atmpS2153;
  _M0L3expS832 = _M0L6_2atmpS2152 - 1022;
  _M0L6_2atmpS2151 = ~9218868437227405312ull;
  _M0L6_2atmpS2150 = _M0L1uS831 & _M0L6_2atmpS2151;
  _M0L6_2atmpS2149 = _M0L6_2atmpS2150 | 4602678819172646912ull;
  _M0L4fracS833 = *(double*)&_M0L6_2atmpS2149;
  _block_2767 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2767)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2767->$0 = _M0L4fracS833;
  _block_2767->$1 = _M0L3expS832;
  return _block_2767;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS826) {
  double _M0L6_2atmpS2146;
  struct _M0TUdiE* _block_2769;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2146 = fabs(_M0L1fS826);
  if (_M0L6_2atmpS2146 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2148 = (double)4503599627370496ll;
    double _M0L6_2atmpS2147 = _M0L1fS826 * _M0L6_2atmpS2148;
    struct _M0TUdiE* _block_2768 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2768)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2768->$0 = _M0L6_2atmpS2147;
    _block_2768->$1 = -52;
    return _block_2768;
  }
  _block_2769 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2769)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2769->$0 = _M0L1fS826;
  _block_2769->$1 = 0;
  return _block_2769;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS825) {
  double _M0L6_2atmpS2145;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2145 = (double)_M0L4selfS825;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2145);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS824) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS824 != _M0L4selfS824) {
    return 0;
  } else if (_M0L4selfS824 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS824 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS824;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS805,
  float _M0L4elemS807
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS804;
  int32_t _M0L1iS806;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS804 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS805);
  _M0L1iS806 = 0;
  while (1) {
    if (_M0L1iS806 < _M0L3lenS805) {
      float* _M0L3bufS2137 = _M0L3arrS804->$0;
      int32_t _M0L6_2atmpS2138;
      _M0L3bufS2137[_M0L1iS806] = _M0L4elemS807;
      _M0L6_2atmpS2138 = _M0L1iS806 + 1;
      _M0L1iS806 = _M0L6_2atmpS2138;
      continue;
    }
    break;
  }
  return _M0L3arrS804;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS810,
  int32_t _M0L4elemS812
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS809;
  int32_t _M0L1iS811;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS809 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS810);
  _M0L1iS811 = 0;
  while (1) {
    if (_M0L1iS811 < _M0L3lenS810) {
      uint8_t* _M0L3bufS2139 = _M0L3arrS809->$0;
      int32_t _M0L6_2atmpS2140;
      _M0L3bufS2139[_M0L1iS811] = _M0L4elemS812;
      _M0L6_2atmpS2140 = _M0L1iS811 + 1;
      _M0L1iS811 = _M0L6_2atmpS2140;
      continue;
    }
    break;
  }
  return _M0L3arrS809;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS815,
  int32_t _M0L4elemS817
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS814;
  int32_t _M0L1iS816;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS814 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS815);
  _M0L1iS816 = 0;
  while (1) {
    if (_M0L1iS816 < _M0L3lenS815) {
      int32_t* _M0L3bufS2141 = _M0L3arrS814->$0;
      int32_t _M0L6_2atmpS2142;
      _M0L3bufS2141[_M0L1iS816] = _M0L4elemS817;
      _M0L6_2atmpS2142 = _M0L1iS816 + 1;
      _M0L1iS816 = _M0L6_2atmpS2142;
      continue;
    }
    break;
  }
  return _M0L3arrS814;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS820,
  struct _M0TPB5ArrayGfE* _M0L4elemS822
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS819;
  int32_t _M0L1iS821;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS819
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS820);
  _M0L1iS821 = 0;
  while (1) {
    if (_M0L1iS821 < _M0L3lenS820) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2143 = _M0L3arrS819->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2658 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2143[_M0L1iS821];
      int32_t _M0L6_2atmpS2144;
      moonbit_incref_cycle_free(_M0L4elemS822);
      if (_M0L6_2aoldS2658) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2658);
      }
      _M0L3bufS2143[_M0L1iS821] = _M0L4elemS822;
      _M0L6_2atmpS2144 = _M0L1iS821 + 1;
      _M0L1iS821 = _M0L6_2atmpS2144;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS822);
    }
    break;
  }
  return _M0L3arrS819;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS789,
  int32_t _M0L5indexS790,
  float _M0L5valueS791
) {
  int32_t _M0L3lenS788;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS788 = _M0L4selfS789->$1;
  if (_M0L5indexS790 >= 0 && _M0L5indexS790 < _M0L3lenS788) {
    float* _M0L6_2atmpS2133;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2133 = _M0MPC15array5Array6bufferGfE(_M0L4selfS789);
    _M0L6_2atmpS2133[_M0L5indexS790] = _M0L5valueS791;
    moonbit_decref_cycle_free(_M0L6_2atmpS2133);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS793,
  int32_t _M0L5indexS794,
  struct _M0TPB5ArrayGfE* _M0L5valueS795
) {
  int32_t _M0L3lenS792;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS792 = _M0L4selfS793->$1;
  if (_M0L5indexS794 >= 0 && _M0L5indexS794 < _M0L3lenS792) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2134;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2659;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2134
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS793);
    _M0L6_2aoldS2659
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2134[_M0L5indexS794];
    if (_M0L6_2aoldS2659) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2659);
    }
    _M0L6_2atmpS2134[_M0L5indexS794] = _M0L5valueS795;
    moonbit_decref_cycle_free(_M0L6_2atmpS2134);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS795);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS797,
  int32_t _M0L5indexS798,
  int32_t _M0L5valueS799
) {
  int32_t _M0L3lenS796;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS796 = _M0L4selfS797->$1;
  if (_M0L5indexS798 >= 0 && _M0L5indexS798 < _M0L3lenS796) {
    int32_t* _M0L6_2atmpS2135;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2135 = _M0MPC15array5Array6bufferGiE(_M0L4selfS797);
    _M0L6_2atmpS2135[_M0L5indexS798] = _M0L5valueS799;
    moonbit_decref_cycle_free(_M0L6_2atmpS2135);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS801,
  int32_t _M0L5indexS802,
  int32_t _M0L5valueS803
) {
  int32_t _M0L3lenS800;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS800 = _M0L4selfS801->$1;
  if (_M0L5indexS802 >= 0 && _M0L5indexS802 < _M0L3lenS800) {
    uint8_t* _M0L6_2atmpS2136;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2136 = _M0MPC15array5Array6bufferGbE(_M0L4selfS801);
    _M0L6_2atmpS2136[_M0L5indexS802] = _M0L5valueS803;
    moonbit_decref_cycle_free(_M0L6_2atmpS2136);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS781) {
  int32_t _M0L3lenS780;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS780 = _M0L4selfS781->$1;
  if (_M0L3lenS780 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS782 = _M0L3lenS780 - 1;
    float* _M0L3bufS2131 = _M0L4selfS781->$0;
    float _M0L1vS783 = (float)_M0L3bufS2131[_M0L5indexS782];
    void* _block_2774;
    _M0L4selfS781->$1 = _M0L5indexS782;
    _block_2774
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_2774)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_2774)->$0 = _M0L1vS783;
    return _block_2774;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS785) {
  int32_t _M0L3lenS784;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS784 = _M0L4selfS785->$1;
  if (_M0L3lenS784 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS786 = _M0L3lenS784 - 1;
    int32_t* _M0L3bufS2132 = _M0L4selfS785->$0;
    int32_t _M0L1vS787 = (int32_t)_M0L3bufS2132[_M0L5indexS786];
    _M0L4selfS785->$1 = _M0L5indexS786;
    return (int64_t)_M0L1vS787;
  }
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS763,
  int32_t _M0L5indexS764
) {
  int32_t _M0L3lenS762;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS762 = _M0L4selfS763->$1;
  if (_M0L5indexS764 >= 0 && _M0L5indexS764 < _M0L3lenS762) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS2125;
    struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS2660;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2125
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(_M0L4selfS763);
    _M0L6_2atmpS2660
    = (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L6_2atmpS2125[
        _M0L5indexS764
      ];
    if (_M0L6_2atmpS2660) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2660);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2125);
    return _M0L6_2atmpS2660;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS766,
  int32_t _M0L5indexS767
) {
  int32_t _M0L3lenS765;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS765 = _M0L4selfS766->$1;
  if (_M0L5indexS767 >= 0 && _M0L5indexS767 < _M0L3lenS765) {
    float* _M0L6_2atmpS2126;
    float _result_2775;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2126 = _M0MPC15array5Array6bufferGfE(_M0L4selfS766);
    _result_2775 = (float)_M0L6_2atmpS2126[_M0L5indexS767];
    moonbit_decref_cycle_free(_M0L6_2atmpS2126);
    return _result_2775;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS769,
  int32_t _M0L5indexS770
) {
  int32_t _M0L3lenS768;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS768 = _M0L4selfS769->$1;
  if (_M0L5indexS770 >= 0 && _M0L5indexS770 < _M0L3lenS768) {
    moonbit_string_t* _M0L6_2atmpS2127;
    moonbit_string_t _M0L6_2atmpS2661;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2127 = _M0MPC15array5Array6bufferGsE(_M0L4selfS769);
    _M0L6_2atmpS2661 = (moonbit_string_t)_M0L6_2atmpS2127[_M0L5indexS770];
    moonbit_incref_cycle_free(_M0L6_2atmpS2661);
    moonbit_decref_cycle_free(_M0L6_2atmpS2127);
    return _M0L6_2atmpS2661;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS772,
  int32_t _M0L5indexS773
) {
  int32_t _M0L3lenS771;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS771 = _M0L4selfS772->$1;
  if (_M0L5indexS773 >= 0 && _M0L5indexS773 < _M0L3lenS771) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2128;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2662;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2128
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS772);
    _M0L6_2atmpS2662
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2128[_M0L5indexS773];
    if (_M0L6_2atmpS2662) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2662);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2128);
    return _M0L6_2atmpS2662;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS775,
  int32_t _M0L5indexS776
) {
  int32_t _M0L3lenS774;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS774 = _M0L4selfS775->$1;
  if (_M0L5indexS776 >= 0 && _M0L5indexS776 < _M0L3lenS774) {
    int32_t* _M0L6_2atmpS2129;
    int32_t _result_2776;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2129 = _M0MPC15array5Array6bufferGiE(_M0L4selfS775);
    _result_2776 = (int32_t)_M0L6_2atmpS2129[_M0L5indexS776];
    moonbit_decref_cycle_free(_M0L6_2atmpS2129);
    return _result_2776;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS778,
  int32_t _M0L5indexS779
) {
  int32_t _M0L3lenS777;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS777 = _M0L4selfS778->$1;
  if (_M0L5indexS779 >= 0 && _M0L5indexS779 < _M0L3lenS777) {
    uint8_t* _M0L6_2atmpS2130;
    int32_t _result_2777;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2130 = _M0MPC15array5Array6bufferGbE(_M0L4selfS778);
    _result_2777 = (int32_t)_M0L6_2atmpS2130[_M0L5indexS779];
    moonbit_decref_cycle_free(_M0L6_2atmpS2130);
    return _result_2777;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS761) {
  moonbit_string_t _M0L6_2atmpS2124;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2124 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS761);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2124);
  moonbit_decref_cycle_free(_M0L6_2atmpS2124);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS760) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS760);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS759) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS759 > _M0FPB18double__max__value
         || _M0L4selfS759 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS758) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS758 != _M0L4selfS758;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS743) {
  uint64_t _M0L4bitsS746;
  uint64_t _M0L6_2atmpS2123;
  uint64_t _M0L6_2atmpS2122;
  int32_t _M0L8ieeeSignS747;
  uint64_t _M0L12ieeeMantissaS748;
  uint64_t _M0L6_2atmpS2121;
  uint64_t _M0L6_2atmpS2120;
  int32_t _M0L12ieeeExponentS749;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS750;
  struct _M0TPB17FloatingDecimal64* _M0L1vS751;
  moonbit_string_t _result_2779;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS743 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L3valS743 >= -0x1p+53 && _M0L3valS743 <= 0x1p+53) {
    if (_M0L3valS743 >= -0x1p+31 && _M0L3valS743 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS744;
      double _M0L6_2atmpS2109;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double7to__int(_M0L3valS743);
      _M0L6_2atmpS2109 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2109 == _M0L3valS743) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS744, 10);
      }
    } else {
      int64_t _M0L1iS745;
      double _M0L6_2atmpS2110;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS745 = _M0MPC16double6Double9to__int64(_M0L3valS743);
      _M0L6_2atmpS2110 = (double)_M0L1iS745;
      if (_M0L6_2atmpS2110 == _M0L3valS743) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS745, 10);
      }
    }
  }
  _M0L4bitsS746 = *(int64_t*)&_M0L3valS743;
  _M0L6_2atmpS2123 = _M0L4bitsS746 >> 63;
  _M0L6_2atmpS2122 = _M0L6_2atmpS2123 & 1ull;
  _M0L8ieeeSignS747 = _M0L6_2atmpS2122 != 0ull;
  _M0L12ieeeMantissaS748 = _M0L4bitsS746 & 4503599627370495ull;
  _M0L6_2atmpS2121 = _M0L4bitsS746 >> 52;
  _M0L6_2atmpS2120 = _M0L6_2atmpS2121 & 2047ull;
  _M0L12ieeeExponentS749 = (int32_t)_M0L6_2atmpS2120;
  if (
    _M0L12ieeeExponentS749 == 2047
    || _M0L12ieeeExponentS749 == 0 && _M0L12ieeeMantissaS748 == 0ull
  ) {
    int32_t _M0L6_2atmpS2111 = _M0L12ieeeExponentS749 != 0;
    int32_t _M0L6_2atmpS2112 = _M0L12ieeeMantissaS748 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS747, _M0L6_2atmpS2111, _M0L6_2atmpS2112);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS750
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS748, _M0L12ieeeExponentS749);
  if (_M0L7_2abindS750 == 0) {
    uint32_t _M0L6_2atmpS2113;
    if (_M0L7_2abindS750) {
      moonbit_decref_cycle_free(_M0L7_2abindS750);
    }
    _M0L6_2atmpS2113 = *(uint32_t*)&_M0L12ieeeExponentS749;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS751 = _M0FPB3d2d(_M0L12ieeeMantissaS748, _M0L6_2atmpS2113);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS752 = _M0L7_2abindS750;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS753 = _M0L7_2aSomeS752;
    struct _M0TPB17FloatingDecimal64* _M0L1xS754 = _M0L4_2afS753;
    while (1) {
      uint64_t _M0L8mantissaS2119 = _M0L1xS754->$0;
      uint64_t _M0L1qS755 = _M0L8mantissaS2119 / 10ull;
      uint64_t _M0L8mantissaS2117 = _M0L1xS754->$0;
      uint64_t _M0L6_2atmpS2118 = 10ull * _M0L1qS755;
      uint64_t _M0L1rS756 = _M0L8mantissaS2117 - _M0L6_2atmpS2118;
      int32_t _M0L8exponentS2116;
      int32_t _M0L6_2atmpS2115;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2114;
      if (_M0L1rS756 != 0ull) {
        _M0L1vS751 = _M0L1xS754;
        break;
      }
      _M0L8exponentS2116 = _M0L1xS754->$1;
      moonbit_decref_cycle_free(_M0L1xS754);
      _M0L6_2atmpS2115 = _M0L8exponentS2116 + 1;
      _M0L6_2atmpS2114
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2114)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2114->$0 = _M0L1qS755;
      _M0L6_2atmpS2114->$1 = _M0L6_2atmpS2115;
      _M0L1xS754 = _M0L6_2atmpS2114;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2779 = _M0FPB9to__chars(_M0L1vS751, _M0L8ieeeSignS747);
  moonbit_decref_cycle_free(_M0L1vS751);
  return _result_2779;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS738,
  int32_t _M0L12ieeeExponentS740
) {
  uint64_t _M0L2m2S737;
  int32_t _M0L6_2atmpS2108;
  int32_t _M0L2e2S739;
  int32_t _M0L6_2atmpS2107;
  uint64_t _M0L6_2atmpS2106;
  uint64_t _M0L4maskS741;
  uint64_t _M0L8fractionS742;
  int32_t _M0L6_2atmpS2105;
  uint64_t _M0L6_2atmpS2104;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2103;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S737 = 4503599627370496ull | _M0L12ieeeMantissaS738;
  _M0L6_2atmpS2108 = _M0L12ieeeExponentS740 - 1023;
  _M0L2e2S739 = _M0L6_2atmpS2108 - 52;
  if (_M0L2e2S739 > 0) {
    return 0;
  }
  if (_M0L2e2S739 < -52) {
    return 0;
  }
  _M0L6_2atmpS2107 = -_M0L2e2S739;
  _M0L6_2atmpS2106 = 1ull << (_M0L6_2atmpS2107 & 63);
  _M0L4maskS741 = _M0L6_2atmpS2106 - 1ull;
  _M0L8fractionS742 = _M0L2m2S737 & _M0L4maskS741;
  if (_M0L8fractionS742 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2105 = -_M0L2e2S739;
  _M0L6_2atmpS2104 = _M0L2m2S737 >> (_M0L6_2atmpS2105 & 63);
  _M0L6_2atmpS2103
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2103)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2103->$0 = _M0L6_2atmpS2104;
  _M0L6_2atmpS2103->$1 = 0;
  return _M0L6_2atmpS2103;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS705,
  int32_t _M0L4signS703
) {
  moonbit_bytes_t _M0L6resultS701;
  int32_t _M0Lm5indexS702;
  uint64_t _M0L6outputS704;
  int32_t _M0L7olengthS706;
  int32_t _M0L8exponentS2102;
  int32_t _M0L6_2atmpS2101;
  int32_t _M0Lm3expS707;
  int32_t _M0L6_2atmpS2100;
  int32_t _M0L6_2atmpS2098;
  int32_t _M0L18scientificNotationS708;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS701 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS702 = 0;
  if (_M0L4signS703) {
    int32_t _M0L6_2atmpS1972 = _M0Lm5indexS702;
    int32_t _M0L6_2atmpS1973;
    if (
      _M0L6_2atmpS1972 < 0
      || _M0L6_2atmpS1972 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1972] = 45;
    _M0L6_2atmpS1973 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS1973 + 1;
  }
  _M0L6outputS704 = _M0L1vS705->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS706 = _M0FPB17decimal__length17(_M0L6outputS704);
  _M0L8exponentS2102 = _M0L1vS705->$1;
  _M0L6_2atmpS2101 = _M0L8exponentS2102 + _M0L7olengthS706;
  _M0Lm3expS707 = _M0L6_2atmpS2101 - 1;
  _M0L6_2atmpS2100 = _M0Lm3expS707;
  if (_M0L6_2atmpS2100 >= -6) {
    int32_t _M0L6_2atmpS2099 = _M0Lm3expS707;
    _M0L6_2atmpS2098 = _M0L6_2atmpS2099 < 21;
  } else {
    _M0L6_2atmpS2098 = 0;
  }
  _M0L18scientificNotationS708 = !_M0L6_2atmpS2098;
  if (_M0L18scientificNotationS708) {
    int32_t _M0L7_2abindS709 = _M0L7olengthS706 - 1;
    uint64_t _M0L6outputS710;
    int32_t _M0L1iS711 = 0;
    uint64_t _M0L6outputS712 = _M0L6outputS704;
    int32_t _M0L6_2atmpS1974;
    int32_t _M0L6_2atmpS1978;
    int32_t _M0L6_2atmpS1977;
    int32_t _M0L6_2atmpS1976;
    int32_t _M0L6_2atmpS1975;
    int32_t _M0L6_2atmpS1982;
    int32_t _M0L6_2atmpS1983;
    int32_t _M0L6_2atmpS1984;
    int32_t _M0L6_2atmpS1985;
    int32_t _M0L6_2atmpS1986;
    int32_t _M0L6_2atmpS1992;
    int32_t _M0L6_2atmpS2025;
    moonbit_string_t _result_2781;
    while (1) {
      if (_M0L1iS711 < _M0L7_2abindS709) {
        uint64_t _M0L1cS713 = _M0L6outputS712 % 10ull;
        int32_t _M0L6_2atmpS2031 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2030 = _M0L6_2atmpS2031 + _M0L7olengthS706;
        int32_t _M0L6_2atmpS2026 = _M0L6_2atmpS2030 - _M0L1iS711;
        int32_t _M0L6_2atmpS2029 = (int32_t)_M0L1cS713;
        int32_t _M0L6_2atmpS2028 = 48 + _M0L6_2atmpS2029;
        int32_t _M0L6_2atmpS2027 = _M0L6_2atmpS2028 & 0xff;
        int32_t _M0L6_2atmpS2032;
        uint64_t _M0L6_2atmpS2033;
        if (
          _M0L6_2atmpS2026 < 0
          || _M0L6_2atmpS2026 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2026] = _M0L6_2atmpS2027;
        _M0L6_2atmpS2032 = _M0L1iS711 + 1;
        _M0L6_2atmpS2033 = _M0L6outputS712 / 10ull;
        _M0L1iS711 = _M0L6_2atmpS2032;
        _M0L6outputS712 = _M0L6_2atmpS2033;
        continue;
      } else {
        _M0L6outputS710 = _M0L6outputS712;
      }
      break;
    }
    _M0L6_2atmpS1974 = _M0Lm5indexS702;
    _M0L6_2atmpS1978 = (int32_t)_M0L6outputS710;
    _M0L6_2atmpS1977 = _M0L6_2atmpS1978 % 10;
    _M0L6_2atmpS1976 = 48 + _M0L6_2atmpS1977;
    _M0L6_2atmpS1975 = _M0L6_2atmpS1976 & 0xff;
    if (
      _M0L6_2atmpS1974 < 0
      || _M0L6_2atmpS1974 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1974] = _M0L6_2atmpS1975;
    if (_M0L7olengthS706 > 1) {
      int32_t _M0L6_2atmpS1980 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1979 = _M0L6_2atmpS1980 + 1;
      if (
        _M0L6_2atmpS1979 < 0
        || _M0L6_2atmpS1979 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1979] = 46;
    } else {
      int32_t _M0L6_2atmpS1981 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1981 - 1;
    }
    _M0L6_2atmpS1982 = _M0Lm5indexS702;
    _M0L6_2atmpS1983 = _M0L7olengthS706 + 1;
    _M0Lm5indexS702 = _M0L6_2atmpS1982 + _M0L6_2atmpS1983;
    _M0L6_2atmpS1984 = _M0Lm5indexS702;
    if (
      _M0L6_2atmpS1984 < 0
      || _M0L6_2atmpS1984 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1984] = 101;
    _M0L6_2atmpS1985 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS1985 + 1;
    _M0L6_2atmpS1986 = _M0Lm3expS707;
    if (_M0L6_2atmpS1986 < 0) {
      int32_t _M0L6_2atmpS1987 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1988;
      int32_t _M0L6_2atmpS1989;
      if (
        _M0L6_2atmpS1987 < 0
        || _M0L6_2atmpS1987 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1987] = 45;
      _M0L6_2atmpS1988 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1988 + 1;
      _M0L6_2atmpS1989 = _M0Lm3expS707;
      _M0Lm3expS707 = -_M0L6_2atmpS1989;
    } else {
      int32_t _M0L6_2atmpS1990 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1991;
      if (
        _M0L6_2atmpS1990 < 0
        || _M0L6_2atmpS1990 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1990] = 43;
      _M0L6_2atmpS1991 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1991 + 1;
    }
    _M0L6_2atmpS1992 = _M0Lm3expS707;
    if (_M0L6_2atmpS1992 >= 100) {
      int32_t _M0L6_2atmpS2008 = _M0Lm3expS707;
      int32_t _M0L1aS715 = _M0L6_2atmpS2008 / 100;
      int32_t _M0L6_2atmpS2007 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS2006 = _M0L6_2atmpS2007 / 10;
      int32_t _M0L1bS716 = _M0L6_2atmpS2006 % 10;
      int32_t _M0L6_2atmpS2005 = _M0Lm3expS707;
      int32_t _M0L1cS717 = _M0L6_2atmpS2005 % 10;
      int32_t _M0L6_2atmpS1993 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1995 = 48 + _M0L1aS715;
      int32_t _M0L6_2atmpS1994 = _M0L6_2atmpS1995 & 0xff;
      int32_t _M0L6_2atmpS1999;
      int32_t _M0L6_2atmpS1996;
      int32_t _M0L6_2atmpS1998;
      int32_t _M0L6_2atmpS1997;
      int32_t _M0L6_2atmpS2003;
      int32_t _M0L6_2atmpS2000;
      int32_t _M0L6_2atmpS2002;
      int32_t _M0L6_2atmpS2001;
      int32_t _M0L6_2atmpS2004;
      if (
        _M0L6_2atmpS1993 < 0
        || _M0L6_2atmpS1993 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1993] = _M0L6_2atmpS1994;
      _M0L6_2atmpS1999 = _M0Lm5indexS702;
      _M0L6_2atmpS1996 = _M0L6_2atmpS1999 + 1;
      _M0L6_2atmpS1998 = 48 + _M0L1bS716;
      _M0L6_2atmpS1997 = _M0L6_2atmpS1998 & 0xff;
      if (
        _M0L6_2atmpS1996 < 0
        || _M0L6_2atmpS1996 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1996] = _M0L6_2atmpS1997;
      _M0L6_2atmpS2003 = _M0Lm5indexS702;
      _M0L6_2atmpS2000 = _M0L6_2atmpS2003 + 2;
      _M0L6_2atmpS2002 = 48 + _M0L1cS717;
      _M0L6_2atmpS2001 = _M0L6_2atmpS2002 & 0xff;
      if (
        _M0L6_2atmpS2000 < 0
        || _M0L6_2atmpS2000 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2000] = _M0L6_2atmpS2001;
      _M0L6_2atmpS2004 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2004 + 3;
    } else {
      int32_t _M0L6_2atmpS2009 = _M0Lm3expS707;
      if (_M0L6_2atmpS2009 >= 10) {
        int32_t _M0L6_2atmpS2019 = _M0Lm3expS707;
        int32_t _M0L1aS718 = _M0L6_2atmpS2019 / 10;
        int32_t _M0L6_2atmpS2018 = _M0Lm3expS707;
        int32_t _M0L1bS719 = _M0L6_2atmpS2018 % 10;
        int32_t _M0L6_2atmpS2010 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2012 = 48 + _M0L1aS718;
        int32_t _M0L6_2atmpS2011 = _M0L6_2atmpS2012 & 0xff;
        int32_t _M0L6_2atmpS2016;
        int32_t _M0L6_2atmpS2013;
        int32_t _M0L6_2atmpS2015;
        int32_t _M0L6_2atmpS2014;
        int32_t _M0L6_2atmpS2017;
        if (
          _M0L6_2atmpS2010 < 0
          || _M0L6_2atmpS2010 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2010] = _M0L6_2atmpS2011;
        _M0L6_2atmpS2016 = _M0Lm5indexS702;
        _M0L6_2atmpS2013 = _M0L6_2atmpS2016 + 1;
        _M0L6_2atmpS2015 = 48 + _M0L1bS719;
        _M0L6_2atmpS2014 = _M0L6_2atmpS2015 & 0xff;
        if (
          _M0L6_2atmpS2013 < 0
          || _M0L6_2atmpS2013 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2013] = _M0L6_2atmpS2014;
        _M0L6_2atmpS2017 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2017 + 2;
      } else {
        int32_t _M0L6_2atmpS2020 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2023 = _M0Lm3expS707;
        int32_t _M0L6_2atmpS2022 = 48 + _M0L6_2atmpS2023;
        int32_t _M0L6_2atmpS2021 = _M0L6_2atmpS2022 & 0xff;
        int32_t _M0L6_2atmpS2024;
        if (
          _M0L6_2atmpS2020 < 0
          || _M0L6_2atmpS2020 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS2020] = _M0L6_2atmpS2021;
        _M0L6_2atmpS2024 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2024 + 1;
      }
    }
    _M0L6_2atmpS2025 = _M0Lm5indexS702;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2781
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS2025);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_2781;
  } else {
    int32_t _M0L6_2atmpS2034 = _M0Lm3expS707;
    int32_t _M0L6_2atmpS2097;
    moonbit_string_t _result_2787;
    if (_M0L6_2atmpS2034 < 0) {
      int32_t _M0L6_2atmpS2035 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS2037;
      int32_t _M0L6_2atmpS2036;
      int32_t _M0L6_2atmpS2038;
      int32_t _M0L1iS720;
      int32_t _M0L6_2atmpS2053;
      int32_t _M0L6_2atmpS2055;
      int32_t _M0L6_2atmpS2054;
      int32_t _M0L7currentS722;
      int32_t _M0L1iS723;
      uint64_t _M0L6outputS724;
      if (
        _M0L6_2atmpS2035 < 0
        || _M0L6_2atmpS2035 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2035] = 48;
      _M0L6_2atmpS2037 = _M0Lm5indexS702;
      _M0L6_2atmpS2036 = _M0L6_2atmpS2037 + 1;
      if (
        _M0L6_2atmpS2036 < 0
        || _M0L6_2atmpS2036 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS2036] = 46;
      _M0L6_2atmpS2038 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS2038 + 2;
      _M0L1iS720 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2039 = _M0Lm3expS707;
        if (_M0L1iS720 > _M0L6_2atmpS2039) {
          int32_t _M0L6_2atmpS2042 = _M0Lm5indexS702;
          int32_t _M0L6_2atmpS2041 = _M0L6_2atmpS2042 - _M0L1iS720;
          int32_t _M0L6_2atmpS2040 = _M0L6_2atmpS2041 - 1;
          int32_t _M0L6_2atmpS2043;
          if (
            _M0L6_2atmpS2040 < 0
            || _M0L6_2atmpS2040 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS2040] = 48;
          _M0L6_2atmpS2043 = _M0L1iS720 - 1;
          _M0L1iS720 = _M0L6_2atmpS2043;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2053 = _M0Lm5indexS702;
      _M0L6_2atmpS2055 = _M0Lm3expS707;
      _M0L6_2atmpS2054 = -1 - _M0L6_2atmpS2055;
      _M0L7currentS722 = _M0L6_2atmpS2053 + _M0L6_2atmpS2054;
      _M0L1iS723 = 0;
      _M0L6outputS724 = _M0L6outputS704;
      while (1) {
        if (_M0L1iS723 < _M0L7olengthS706) {
          int32_t _M0L6_2atmpS2050 = _M0L7currentS722 + _M0L7olengthS706;
          int32_t _M0L6_2atmpS2049 = _M0L6_2atmpS2050 - _M0L1iS723;
          int32_t _M0L6_2atmpS2044 = _M0L6_2atmpS2049 - 1;
          uint64_t _M0L6_2atmpS2048 = _M0L6outputS724 % 10ull;
          int32_t _M0L6_2atmpS2047 = (int32_t)_M0L6_2atmpS2048;
          int32_t _M0L6_2atmpS2046 = 48 + _M0L6_2atmpS2047;
          int32_t _M0L6_2atmpS2045 = _M0L6_2atmpS2046 & 0xff;
          int32_t _M0L6_2atmpS2051;
          uint64_t _M0L6_2atmpS2052;
          if (
            _M0L6_2atmpS2044 < 0
            || _M0L6_2atmpS2044 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS2044] = _M0L6_2atmpS2045;
          _M0L6_2atmpS2051 = _M0L1iS723 + 1;
          _M0L6_2atmpS2052 = _M0L6outputS724 / 10ull;
          _M0L1iS723 = _M0L6_2atmpS2051;
          _M0L6outputS724 = _M0L6_2atmpS2052;
          continue;
        }
        break;
      }
      _M0Lm5indexS702 = _M0L7currentS722 + _M0L7olengthS706;
    } else {
      int32_t _M0L6_2atmpS2057 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS2056 = _M0L6_2atmpS2057 + 1;
      if (_M0L6_2atmpS2056 >= _M0L7olengthS706) {
        int32_t _M0L1iS726 = 0;
        uint64_t _M0L6outputS727 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2068;
        int32_t _M0L6_2atmpS2073;
        int32_t _M0L7_2abindS729;
        int32_t _M0L1iS730;
        int32_t _M0L6_2atmpS2074;
        int32_t _M0L6_2atmpS2077;
        int32_t _M0L6_2atmpS2076;
        int32_t _M0L6_2atmpS2075;
        while (1) {
          if (_M0L1iS726 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2065 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2064 = _M0L6_2atmpS2065 + _M0L7olengthS706;
            int32_t _M0L6_2atmpS2063 = _M0L6_2atmpS2064 - _M0L1iS726;
            int32_t _M0L6_2atmpS2058 = _M0L6_2atmpS2063 - 1;
            uint64_t _M0L6_2atmpS2062 = _M0L6outputS727 % 10ull;
            int32_t _M0L6_2atmpS2061 = (int32_t)_M0L6_2atmpS2062;
            int32_t _M0L6_2atmpS2060 = 48 + _M0L6_2atmpS2061;
            int32_t _M0L6_2atmpS2059 = _M0L6_2atmpS2060 & 0xff;
            int32_t _M0L6_2atmpS2066;
            uint64_t _M0L6_2atmpS2067;
            if (
              _M0L6_2atmpS2058 < 0
              || _M0L6_2atmpS2058 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2058] = _M0L6_2atmpS2059;
            _M0L6_2atmpS2066 = _M0L1iS726 + 1;
            _M0L6_2atmpS2067 = _M0L6outputS727 / 10ull;
            _M0L1iS726 = _M0L6_2atmpS2066;
            _M0L6outputS727 = _M0L6_2atmpS2067;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2068 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2068 + _M0L7olengthS706;
        _M0L6_2atmpS2073 = _M0Lm3expS707;
        _M0L7_2abindS729 = _M0L6_2atmpS2073 + 1;
        _M0L1iS730 = _M0L7olengthS706;
        while (1) {
          if (_M0L1iS730 < _M0L7_2abindS729) {
            int32_t _M0L6_2atmpS2071 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2070 = _M0L6_2atmpS2071 + _M0L1iS730;
            int32_t _M0L6_2atmpS2069 = _M0L6_2atmpS2070 - _M0L7olengthS706;
            int32_t _M0L6_2atmpS2072;
            if (
              _M0L6_2atmpS2069 < 0
              || _M0L6_2atmpS2069 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2069] = 48;
            _M0L6_2atmpS2072 = _M0L1iS730 + 1;
            _M0L1iS730 = _M0L6_2atmpS2072;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2074 = _M0Lm5indexS702;
        _M0L6_2atmpS2077 = _M0Lm3expS707;
        _M0L6_2atmpS2076 = _M0L6_2atmpS2077 + 1;
        _M0L6_2atmpS2075 = _M0L6_2atmpS2076 - _M0L7olengthS706;
        _M0Lm5indexS702 = _M0L6_2atmpS2074 + _M0L6_2atmpS2075;
      } else {
        int32_t _M0L6_2atmpS2094 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2093 = _M0L6_2atmpS2094 + 1;
        int32_t _M0L1iS732 = 0;
        int32_t _M0L7currentS733 = _M0L6_2atmpS2093;
        uint64_t _M0L6outputS734 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2095;
        int32_t _M0L6_2atmpS2096;
        while (1) {
          if (_M0L1iS732 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2089 = _M0L7olengthS706 - _M0L1iS732;
            int32_t _M0L6_2atmpS2087 = _M0L6_2atmpS2089 - 1;
            int32_t _M0L6_2atmpS2088 = _M0Lm3expS707;
            int32_t _M0L7currentS735;
            int32_t _M0L6_2atmpS2084;
            int32_t _M0L6_2atmpS2083;
            int32_t _M0L6_2atmpS2078;
            uint64_t _M0L6_2atmpS2082;
            int32_t _M0L6_2atmpS2081;
            int32_t _M0L6_2atmpS2080;
            int32_t _M0L6_2atmpS2079;
            int32_t _M0L6_2atmpS2085;
            uint64_t _M0L6_2atmpS2086;
            if (_M0L6_2atmpS2087 == _M0L6_2atmpS2088) {
              int32_t _M0L6_2atmpS2092 = _M0L7currentS733 + _M0L7olengthS706;
              int32_t _M0L6_2atmpS2091 = _M0L6_2atmpS2092 - _M0L1iS732;
              int32_t _M0L6_2atmpS2090 = _M0L6_2atmpS2091 - 1;
              if (
                _M0L6_2atmpS2090 < 0
                || _M0L6_2atmpS2090 >= Moonbit_array_length(_M0L6resultS701)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS701[_M0L6_2atmpS2090] = 46;
              _M0L7currentS735 = _M0L7currentS733 - 1;
            } else {
              _M0L7currentS735 = _M0L7currentS733;
            }
            _M0L6_2atmpS2084 = _M0L7currentS735 + _M0L7olengthS706;
            _M0L6_2atmpS2083 = _M0L6_2atmpS2084 - _M0L1iS732;
            _M0L6_2atmpS2078 = _M0L6_2atmpS2083 - 1;
            _M0L6_2atmpS2082 = _M0L6outputS734 % 10ull;
            _M0L6_2atmpS2081 = (int32_t)_M0L6_2atmpS2082;
            _M0L6_2atmpS2080 = 48 + _M0L6_2atmpS2081;
            _M0L6_2atmpS2079 = _M0L6_2atmpS2080 & 0xff;
            if (
              _M0L6_2atmpS2078 < 0
              || _M0L6_2atmpS2078 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2078] = _M0L6_2atmpS2079;
            _M0L6_2atmpS2085 = _M0L1iS732 + 1;
            _M0L6_2atmpS2086 = _M0L6outputS734 / 10ull;
            _M0L1iS732 = _M0L6_2atmpS2085;
            _M0L7currentS733 = _M0L7currentS735;
            _M0L6outputS734 = _M0L6_2atmpS2086;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2095 = _M0Lm5indexS702;
        _M0L6_2atmpS2096 = _M0L7olengthS706 + 1;
        _M0Lm5indexS702 = _M0L6_2atmpS2095 + _M0L6_2atmpS2096;
      }
    }
    _M0L6_2atmpS2097 = _M0Lm5indexS702;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2787
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS2097);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_2787;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS647,
  uint32_t _M0L12ieeeExponentS646
) {
  int32_t _M0Lm2e2S644;
  uint64_t _M0Lm2m2S645;
  uint64_t _M0L6_2atmpS1971;
  uint64_t _M0L6_2atmpS1970;
  int32_t _M0L4evenS648;
  uint64_t _M0L6_2atmpS1969;
  uint64_t _M0L2mvS649;
  int32_t _M0L7mmShiftS650;
  uint64_t _M0Lm2vrS651;
  uint64_t _M0Lm2vpS652;
  uint64_t _M0Lm2vmS653;
  int32_t _M0Lm3e10S654;
  int32_t _M0Lm17vmIsTrailingZerosS655;
  int32_t _M0Lm17vrIsTrailingZerosS656;
  int32_t _M0L6_2atmpS1871;
  int32_t _M0Lm7removedS675;
  int32_t _M0Lm16lastRemovedDigitS676;
  uint64_t _M0Lm6outputS677;
  int32_t _M0L6_2atmpS1967;
  int32_t _M0L6_2atmpS1968;
  int32_t _M0L3expS700;
  uint64_t _M0L6_2atmpS1966;
  struct _M0TPB17FloatingDecimal64* _block_2793;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S644 = 0;
  _M0Lm2m2S645 = 0ull;
  if (_M0L12ieeeExponentS646 == 0u) {
    _M0Lm2e2S644 = -1076;
    _M0Lm2m2S645 = _M0L12ieeeMantissaS647;
  } else {
    int32_t _M0L6_2atmpS1870 = *(int32_t*)&_M0L12ieeeExponentS646;
    int32_t _M0L6_2atmpS1869 = _M0L6_2atmpS1870 - 1023;
    int32_t _M0L6_2atmpS1868 = _M0L6_2atmpS1869 - 52;
    _M0Lm2e2S644 = _M0L6_2atmpS1868 - 2;
    _M0Lm2m2S645 = 4503599627370496ull | _M0L12ieeeMantissaS647;
  }
  _M0L6_2atmpS1971 = _M0Lm2m2S645;
  _M0L6_2atmpS1970 = _M0L6_2atmpS1971 & 1ull;
  _M0L4evenS648 = _M0L6_2atmpS1970 == 0ull;
  _M0L6_2atmpS1969 = _M0Lm2m2S645;
  _M0L2mvS649 = 4ull * _M0L6_2atmpS1969;
  _M0L7mmShiftS650
  = _M0L12ieeeMantissaS647 != 0ull || _M0L12ieeeExponentS646 <= 1u;
  _M0Lm2vrS651 = 0ull;
  _M0Lm2vpS652 = 0ull;
  _M0Lm2vmS653 = 0ull;
  _M0Lm3e10S654 = 0;
  _M0Lm17vmIsTrailingZerosS655 = 0;
  _M0Lm17vrIsTrailingZerosS656 = 0;
  _M0L6_2atmpS1871 = _M0Lm2e2S644;
  if (_M0L6_2atmpS1871 >= 0) {
    int32_t _M0L6_2atmpS1893 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1889;
    int32_t _M0L6_2atmpS1892;
    int32_t _M0L6_2atmpS1891;
    int32_t _M0L6_2atmpS1890;
    int32_t _M0L1qS657;
    int32_t _M0L6_2atmpS1888;
    int32_t _M0L6_2atmpS1887;
    int32_t _M0L1kS658;
    int32_t _M0L6_2atmpS1886;
    int32_t _M0L6_2atmpS1885;
    int32_t _M0L6_2atmpS1884;
    int32_t _M0L1iS659;
    struct _M0TPB8Pow5Pair _M0L4pow5S660;
    uint64_t _M0L6_2atmpS1883;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS661;
    uint64_t _M0L8_2avrOutS662;
    uint64_t _M0L8_2avpOutS663;
    uint64_t _M0L8_2avmOutS664;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1889 = _M0FPB9log10Pow2(_M0L6_2atmpS1893);
    _M0L6_2atmpS1892 = _M0Lm2e2S644;
    _M0L6_2atmpS1891 = _M0L6_2atmpS1892 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1890 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1891);
    _M0L1qS657 = _M0L6_2atmpS1889 - _M0L6_2atmpS1890;
    _M0Lm3e10S654 = _M0L1qS657;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1888 = _M0FPB8pow5bits(_M0L1qS657);
    _M0L6_2atmpS1887 = 125 + _M0L6_2atmpS1888;
    _M0L1kS658 = _M0L6_2atmpS1887 - 1;
    _M0L6_2atmpS1886 = _M0Lm2e2S644;
    _M0L6_2atmpS1885 = -_M0L6_2atmpS1886;
    _M0L6_2atmpS1884 = _M0L6_2atmpS1885 + _M0L1qS657;
    _M0L1iS659 = _M0L6_2atmpS1884 + _M0L1kS658;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S660 = _M0FPB22double__computeInvPow5(_M0L1qS657);
    _M0L6_2atmpS1883 = _M0Lm2m2S645;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS661
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1883, _M0L4pow5S660, _M0L1iS659, _M0L7mmShiftS650);
    _M0L8_2avrOutS662 = _M0L7_2abindS661.$0;
    _M0L8_2avpOutS663 = _M0L7_2abindS661.$1;
    _M0L8_2avmOutS664 = _M0L7_2abindS661.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS662;
    _M0Lm2vpS652 = _M0L8_2avpOutS663;
    _M0Lm2vmS653 = _M0L8_2avmOutS664;
    if (_M0L1qS657 <= 21) {
      int32_t _M0L6_2atmpS1879 = (int32_t)_M0L2mvS649;
      uint64_t _M0L6_2atmpS1882 = _M0L2mvS649 / 5ull;
      int32_t _M0L6_2atmpS1881 = (int32_t)_M0L6_2atmpS1882;
      int32_t _M0L6_2atmpS1880 = 5 * _M0L6_2atmpS1881;
      int32_t _M0L6mvMod5S665 = _M0L6_2atmpS1879 - _M0L6_2atmpS1880;
      if (_M0L6mvMod5S665 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS656
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS649, _M0L1qS657);
      } else if (_M0L4evenS648) {
        uint64_t _M0L6_2atmpS1873 = _M0L2mvS649 - 1ull;
        uint64_t _M0L6_2atmpS1874;
        uint64_t _M0L6_2atmpS1872;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1874 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS650);
        _M0L6_2atmpS1872 = _M0L6_2atmpS1873 - _M0L6_2atmpS1874;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1872, _M0L1qS657);
      } else {
        uint64_t _M0L6_2atmpS1875 = _M0Lm2vpS652;
        uint64_t _M0L6_2atmpS1878 = _M0L2mvS649 + 2ull;
        int32_t _M0L6_2atmpS1877;
        uint64_t _M0L6_2atmpS1876;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1877
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1878, _M0L1qS657);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1876 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1877);
        _M0Lm2vpS652 = _M0L6_2atmpS1875 - _M0L6_2atmpS1876;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1907 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1906 = -_M0L6_2atmpS1907;
    int32_t _M0L6_2atmpS1901;
    int32_t _M0L6_2atmpS1905;
    int32_t _M0L6_2atmpS1904;
    int32_t _M0L6_2atmpS1903;
    int32_t _M0L6_2atmpS1902;
    int32_t _M0L1qS666;
    int32_t _M0L6_2atmpS1894;
    int32_t _M0L6_2atmpS1900;
    int32_t _M0L6_2atmpS1899;
    int32_t _M0L1iS667;
    int32_t _M0L6_2atmpS1898;
    int32_t _M0L1kS668;
    int32_t _M0L1jS669;
    struct _M0TPB8Pow5Pair _M0L4pow5S670;
    uint64_t _M0L6_2atmpS1897;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS671;
    uint64_t _M0L8_2avrOutS672;
    uint64_t _M0L8_2avpOutS673;
    uint64_t _M0L8_2avmOutS674;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1901 = _M0FPB9log10Pow5(_M0L6_2atmpS1906);
    _M0L6_2atmpS1905 = _M0Lm2e2S644;
    _M0L6_2atmpS1904 = -_M0L6_2atmpS1905;
    _M0L6_2atmpS1903 = _M0L6_2atmpS1904 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1902 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1903);
    _M0L1qS666 = _M0L6_2atmpS1901 - _M0L6_2atmpS1902;
    _M0L6_2atmpS1894 = _M0Lm2e2S644;
    _M0Lm3e10S654 = _M0L1qS666 + _M0L6_2atmpS1894;
    _M0L6_2atmpS1900 = _M0Lm2e2S644;
    _M0L6_2atmpS1899 = -_M0L6_2atmpS1900;
    _M0L1iS667 = _M0L6_2atmpS1899 - _M0L1qS666;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1898 = _M0FPB8pow5bits(_M0L1iS667);
    _M0L1kS668 = _M0L6_2atmpS1898 - 125;
    _M0L1jS669 = _M0L1qS666 - _M0L1kS668;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S670 = _M0FPB19double__computePow5(_M0L1iS667);
    _M0L6_2atmpS1897 = _M0Lm2m2S645;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS671
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1897, _M0L4pow5S670, _M0L1jS669, _M0L7mmShiftS650);
    _M0L8_2avrOutS672 = _M0L7_2abindS671.$0;
    _M0L8_2avpOutS673 = _M0L7_2abindS671.$1;
    _M0L8_2avmOutS674 = _M0L7_2abindS671.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS672;
    _M0Lm2vpS652 = _M0L8_2avpOutS673;
    _M0Lm2vmS653 = _M0L8_2avmOutS674;
    if (_M0L1qS666 <= 1) {
      _M0Lm17vrIsTrailingZerosS656 = 1;
      if (_M0L4evenS648) {
        int32_t _M0L6_2atmpS1895;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1895 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS650);
        _M0Lm17vmIsTrailingZerosS655 = _M0L6_2atmpS1895 == 1;
      } else {
        uint64_t _M0L6_2atmpS1896 = _M0Lm2vpS652;
        _M0Lm2vpS652 = _M0L6_2atmpS1896 - 1ull;
      }
    } else if (_M0L1qS666 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS656
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS649, _M0L1qS666);
    }
  }
  _M0Lm7removedS675 = 0;
  _M0Lm16lastRemovedDigitS676 = 0;
  _M0Lm6outputS677 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS655 || _M0Lm17vrIsTrailingZerosS656) {
    int32_t _if__result_2790;
    uint64_t _M0L6_2atmpS1937;
    uint64_t _M0L6_2atmpS1943;
    uint64_t _M0L6_2atmpS1944;
    int32_t _if__result_2791;
    int32_t _M0L6_2atmpS1940;
    int64_t _M0L6_2atmpS1939;
    uint64_t _M0L6_2atmpS1938;
    while (1) {
      uint64_t _M0L6_2atmpS1920 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S678 = _M0L6_2atmpS1920 / 10ull;
      uint64_t _M0L6_2atmpS1919 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S679 = _M0L6_2atmpS1919 / 10ull;
      uint64_t _M0L6_2atmpS1918;
      int32_t _M0L6_2atmpS1915;
      int32_t _M0L6_2atmpS1917;
      int32_t _M0L6_2atmpS1916;
      int32_t _M0L7vmMod10S681;
      uint64_t _M0L6_2atmpS1914;
      uint64_t _M0L7vrDiv10S682;
      uint64_t _M0L6_2atmpS1913;
      int32_t _M0L6_2atmpS1910;
      int32_t _M0L6_2atmpS1912;
      int32_t _M0L6_2atmpS1911;
      int32_t _M0L7vrMod10S683;
      int32_t _M0L6_2atmpS1909;
      if (_M0L7vpDiv10S678 <= _M0L7vmDiv10S679) {
        break;
      }
      _M0L6_2atmpS1918 = _M0Lm2vmS653;
      _M0L6_2atmpS1915 = (int32_t)_M0L6_2atmpS1918;
      _M0L6_2atmpS1917 = (int32_t)_M0L7vmDiv10S679;
      _M0L6_2atmpS1916 = 10 * _M0L6_2atmpS1917;
      _M0L7vmMod10S681 = _M0L6_2atmpS1915 - _M0L6_2atmpS1916;
      _M0L6_2atmpS1914 = _M0Lm2vrS651;
      _M0L7vrDiv10S682 = _M0L6_2atmpS1914 / 10ull;
      _M0L6_2atmpS1913 = _M0Lm2vrS651;
      _M0L6_2atmpS1910 = (int32_t)_M0L6_2atmpS1913;
      _M0L6_2atmpS1912 = (int32_t)_M0L7vrDiv10S682;
      _M0L6_2atmpS1911 = 10 * _M0L6_2atmpS1912;
      _M0L7vrMod10S683 = _M0L6_2atmpS1910 - _M0L6_2atmpS1911;
      _M0Lm17vmIsTrailingZerosS655
      = _M0Lm17vmIsTrailingZerosS655 && _M0L7vmMod10S681 == 0;
      if (_M0Lm17vrIsTrailingZerosS656) {
        int32_t _M0L6_2atmpS1908 = _M0Lm16lastRemovedDigitS676;
        _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS1908 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS656 = 0;
      }
      _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S683;
      _M0Lm2vrS651 = _M0L7vrDiv10S682;
      _M0Lm2vpS652 = _M0L7vpDiv10S678;
      _M0Lm2vmS653 = _M0L7vmDiv10S679;
      _M0L6_2atmpS1909 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1909 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS655) {
      while (1) {
        uint64_t _M0L6_2atmpS1933 = _M0Lm2vmS653;
        uint64_t _M0L7vmDiv10S684 = _M0L6_2atmpS1933 / 10ull;
        uint64_t _M0L6_2atmpS1932 = _M0Lm2vmS653;
        int32_t _M0L6_2atmpS1929 = (int32_t)_M0L6_2atmpS1932;
        int32_t _M0L6_2atmpS1931 = (int32_t)_M0L7vmDiv10S684;
        int32_t _M0L6_2atmpS1930 = 10 * _M0L6_2atmpS1931;
        int32_t _M0L7vmMod10S685 = _M0L6_2atmpS1929 - _M0L6_2atmpS1930;
        uint64_t _M0L6_2atmpS1928;
        uint64_t _M0L7vpDiv10S687;
        uint64_t _M0L6_2atmpS1927;
        uint64_t _M0L7vrDiv10S688;
        uint64_t _M0L6_2atmpS1926;
        int32_t _M0L6_2atmpS1923;
        int32_t _M0L6_2atmpS1925;
        int32_t _M0L6_2atmpS1924;
        int32_t _M0L7vrMod10S689;
        int32_t _M0L6_2atmpS1922;
        if (_M0L7vmMod10S685 != 0) {
          break;
        }
        _M0L6_2atmpS1928 = _M0Lm2vpS652;
        _M0L7vpDiv10S687 = _M0L6_2atmpS1928 / 10ull;
        _M0L6_2atmpS1927 = _M0Lm2vrS651;
        _M0L7vrDiv10S688 = _M0L6_2atmpS1927 / 10ull;
        _M0L6_2atmpS1926 = _M0Lm2vrS651;
        _M0L6_2atmpS1923 = (int32_t)_M0L6_2atmpS1926;
        _M0L6_2atmpS1925 = (int32_t)_M0L7vrDiv10S688;
        _M0L6_2atmpS1924 = 10 * _M0L6_2atmpS1925;
        _M0L7vrMod10S689 = _M0L6_2atmpS1923 - _M0L6_2atmpS1924;
        if (_M0Lm17vrIsTrailingZerosS656) {
          int32_t _M0L6_2atmpS1921 = _M0Lm16lastRemovedDigitS676;
          _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS1921 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS656 = 0;
        }
        _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S689;
        _M0Lm2vrS651 = _M0L7vrDiv10S688;
        _M0Lm2vpS652 = _M0L7vpDiv10S687;
        _M0Lm2vmS653 = _M0L7vmDiv10S684;
        _M0L6_2atmpS1922 = _M0Lm7removedS675;
        _M0Lm7removedS675 = _M0L6_2atmpS1922 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS656) {
      int32_t _M0L6_2atmpS1936 = _M0Lm16lastRemovedDigitS676;
      if (_M0L6_2atmpS1936 == 5) {
        uint64_t _M0L6_2atmpS1935 = _M0Lm2vrS651;
        uint64_t _M0L6_2atmpS1934 = _M0L6_2atmpS1935 % 2ull;
        _if__result_2790 = _M0L6_2atmpS1934 == 0ull;
      } else {
        _if__result_2790 = 0;
      }
    } else {
      _if__result_2790 = 0;
    }
    if (_if__result_2790) {
      _M0Lm16lastRemovedDigitS676 = 4;
    }
    _M0L6_2atmpS1937 = _M0Lm2vrS651;
    _M0L6_2atmpS1943 = _M0Lm2vrS651;
    _M0L6_2atmpS1944 = _M0Lm2vmS653;
    if (_M0L6_2atmpS1943 == _M0L6_2atmpS1944) {
      if (!_M0L4evenS648) {
        _if__result_2791 = 1;
      } else {
        int32_t _M0L6_2atmpS1942 = _M0Lm17vmIsTrailingZerosS655;
        _if__result_2791 = !_M0L6_2atmpS1942;
      }
    } else {
      _if__result_2791 = 0;
    }
    if (_if__result_2791) {
      _M0L6_2atmpS1940 = 1;
    } else {
      int32_t _M0L6_2atmpS1941 = _M0Lm16lastRemovedDigitS676;
      _M0L6_2atmpS1940 = _M0L6_2atmpS1941 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1939 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1940);
    _M0L6_2atmpS1938 = *(uint64_t*)&_M0L6_2atmpS1939;
    _M0Lm6outputS677 = _M0L6_2atmpS1937 + _M0L6_2atmpS1938;
  } else {
    int32_t _M0Lm7roundUpS690 = 0;
    uint64_t _M0L6_2atmpS1965 = _M0Lm2vpS652;
    uint64_t _M0L8vpDiv100S691 = _M0L6_2atmpS1965 / 100ull;
    uint64_t _M0L6_2atmpS1964 = _M0Lm2vmS653;
    uint64_t _M0L8vmDiv100S692 = _M0L6_2atmpS1964 / 100ull;
    uint64_t _M0L6_2atmpS1959;
    uint64_t _M0L6_2atmpS1962;
    uint64_t _M0L6_2atmpS1963;
    int32_t _M0L6_2atmpS1961;
    uint64_t _M0L6_2atmpS1960;
    if (_M0L8vpDiv100S691 > _M0L8vmDiv100S692) {
      uint64_t _M0L6_2atmpS1950 = _M0Lm2vrS651;
      uint64_t _M0L8vrDiv100S693 = _M0L6_2atmpS1950 / 100ull;
      uint64_t _M0L6_2atmpS1949 = _M0Lm2vrS651;
      int32_t _M0L6_2atmpS1946 = (int32_t)_M0L6_2atmpS1949;
      int32_t _M0L6_2atmpS1948 = (int32_t)_M0L8vrDiv100S693;
      int32_t _M0L6_2atmpS1947 = 100 * _M0L6_2atmpS1948;
      int32_t _M0L8vrMod100S694 = _M0L6_2atmpS1946 - _M0L6_2atmpS1947;
      int32_t _M0L6_2atmpS1945;
      _M0Lm7roundUpS690 = _M0L8vrMod100S694 >= 50;
      _M0Lm2vrS651 = _M0L8vrDiv100S693;
      _M0Lm2vpS652 = _M0L8vpDiv100S691;
      _M0Lm2vmS653 = _M0L8vmDiv100S692;
      _M0L6_2atmpS1945 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1945 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1958 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S695 = _M0L6_2atmpS1958 / 10ull;
      uint64_t _M0L6_2atmpS1957 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S696 = _M0L6_2atmpS1957 / 10ull;
      uint64_t _M0L6_2atmpS1956;
      uint64_t _M0L7vrDiv10S698;
      uint64_t _M0L6_2atmpS1955;
      int32_t _M0L6_2atmpS1952;
      int32_t _M0L6_2atmpS1954;
      int32_t _M0L6_2atmpS1953;
      int32_t _M0L7vrMod10S699;
      int32_t _M0L6_2atmpS1951;
      if (_M0L7vpDiv10S695 <= _M0L7vmDiv10S696) {
        break;
      }
      _M0L6_2atmpS1956 = _M0Lm2vrS651;
      _M0L7vrDiv10S698 = _M0L6_2atmpS1956 / 10ull;
      _M0L6_2atmpS1955 = _M0Lm2vrS651;
      _M0L6_2atmpS1952 = (int32_t)_M0L6_2atmpS1955;
      _M0L6_2atmpS1954 = (int32_t)_M0L7vrDiv10S698;
      _M0L6_2atmpS1953 = 10 * _M0L6_2atmpS1954;
      _M0L7vrMod10S699 = _M0L6_2atmpS1952 - _M0L6_2atmpS1953;
      _M0Lm7roundUpS690 = _M0L7vrMod10S699 >= 5;
      _M0Lm2vrS651 = _M0L7vrDiv10S698;
      _M0Lm2vpS652 = _M0L7vpDiv10S695;
      _M0Lm2vmS653 = _M0L7vmDiv10S696;
      _M0L6_2atmpS1951 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1951 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1959 = _M0Lm2vrS651;
    _M0L6_2atmpS1962 = _M0Lm2vrS651;
    _M0L6_2atmpS1963 = _M0Lm2vmS653;
    _M0L6_2atmpS1961
    = _M0L6_2atmpS1962 == _M0L6_2atmpS1963 || _M0Lm7roundUpS690;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1960 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1961);
    _M0Lm6outputS677 = _M0L6_2atmpS1959 + _M0L6_2atmpS1960;
  }
  _M0L6_2atmpS1967 = _M0Lm3e10S654;
  _M0L6_2atmpS1968 = _M0Lm7removedS675;
  _M0L3expS700 = _M0L6_2atmpS1967 + _M0L6_2atmpS1968;
  _M0L6_2atmpS1966 = _M0Lm6outputS677;
  _block_2793
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2793)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2793->$0 = _M0L6_2atmpS1966;
  _block_2793->$1 = _M0L3expS700;
  return _block_2793;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS643) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS643) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS642) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS642) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS641) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS641) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS640) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS640 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS640 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS640 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS640 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS640 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS640 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS640 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS640 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS640 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS640 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS640 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS640 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS640 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS640 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS640 >= 100ull) {
    return 3;
  }
  if (_M0L1vS640 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS623) {
  int32_t _M0L6_2atmpS1867;
  int32_t _M0L6_2atmpS1866;
  int32_t _M0L4baseS622;
  int32_t _M0L5base2S624;
  int32_t _M0L6offsetS625;
  int32_t _M0L6_2atmpS1865;
  uint64_t _M0L4mul0S626;
  int32_t _M0L6_2atmpS1864;
  int32_t _M0L6_2atmpS1863;
  uint64_t _M0L4mul1S627;
  uint64_t _M0L1mS628;
  struct _M0TPB7Umul128 _M0L7_2abindS629;
  uint64_t _M0L7_2alow1S630;
  uint64_t _M0L8_2ahigh1S631;
  struct _M0TPB7Umul128 _M0L7_2abindS632;
  uint64_t _M0L7_2alow0S633;
  uint64_t _M0L8_2ahigh0S634;
  uint64_t _M0L3sumS635;
  uint64_t _M0Lm5high1S636;
  int32_t _M0L6_2atmpS1861;
  int32_t _M0L6_2atmpS1862;
  int32_t _M0L5deltaS637;
  uint64_t _M0L6_2atmpS1860;
  uint64_t _M0L6_2atmpS1852;
  int32_t _M0L6_2atmpS1859;
  uint32_t _M0L6_2atmpS1856;
  int32_t _M0L6_2atmpS1858;
  int32_t _M0L6_2atmpS1857;
  uint32_t _M0L6_2atmpS1855;
  uint32_t _M0L6_2atmpS1854;
  uint64_t _M0L6_2atmpS1853;
  uint64_t _M0L1aS638;
  uint64_t _M0L6_2atmpS1851;
  uint64_t _M0L1bS639;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1867 = _M0L1iS623 + 26;
  _M0L6_2atmpS1866 = _M0L6_2atmpS1867 - 1;
  _M0L4baseS622 = _M0L6_2atmpS1866 / 26;
  _M0L5base2S624 = _M0L4baseS622 * 26;
  _M0L6offsetS625 = _M0L5base2S624 - _M0L1iS623;
  _M0L6_2atmpS1865 = _M0L4baseS622 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1865);
  _M0L6_2atmpS1864 = _M0L4baseS622 * 2;
  _M0L6_2atmpS1863 = _M0L6_2atmpS1864 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1863);
  if (_M0L6offsetS625 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S626, .$1 = _M0L4mul1S627};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS628
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS625);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS629 = _M0FPB7umul128(_M0L1mS628, _M0L4mul1S627);
  _M0L7_2alow1S630 = _M0L7_2abindS629.$0;
  _M0L8_2ahigh1S631 = _M0L7_2abindS629.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS632 = _M0FPB7umul128(_M0L1mS628, _M0L4mul0S626);
  _M0L7_2alow0S633 = _M0L7_2abindS632.$0;
  _M0L8_2ahigh0S634 = _M0L7_2abindS632.$1;
  _M0L3sumS635 = _M0L8_2ahigh0S634 + _M0L7_2alow1S630;
  _M0Lm5high1S636 = _M0L8_2ahigh1S631;
  if (_M0L3sumS635 < _M0L8_2ahigh0S634) {
    uint64_t _M0L6_2atmpS1850 = _M0Lm5high1S636;
    _M0Lm5high1S636 = _M0L6_2atmpS1850 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1861 = _M0FPB8pow5bits(_M0L5base2S624);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1862 = _M0FPB8pow5bits(_M0L1iS623);
  _M0L5deltaS637 = _M0L6_2atmpS1861 - _M0L6_2atmpS1862;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1860
  = _M0FPB13shiftright128(_M0L7_2alow0S633, _M0L3sumS635, _M0L5deltaS637);
  _M0L6_2atmpS1852 = _M0L6_2atmpS1860 + 1ull;
  _M0L6_2atmpS1859 = _M0L1iS623 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1856
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1859);
  _M0L6_2atmpS1858 = _M0L1iS623 % 16;
  _M0L6_2atmpS1857 = _M0L6_2atmpS1858 << 1;
  _M0L6_2atmpS1855 = _M0L6_2atmpS1856 >> (_M0L6_2atmpS1857 & 31);
  _M0L6_2atmpS1854 = _M0L6_2atmpS1855 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1853 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1854);
  _M0L1aS638 = _M0L6_2atmpS1852 + _M0L6_2atmpS1853;
  _M0L6_2atmpS1851 = _M0Lm5high1S636;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS639
  = _M0FPB13shiftright128(_M0L3sumS635, _M0L6_2atmpS1851, _M0L5deltaS637);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS638, .$1 = _M0L1bS639};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS605) {
  int32_t _M0L4baseS604;
  int32_t _M0L5base2S606;
  int32_t _M0L6offsetS607;
  int32_t _M0L6_2atmpS1849;
  uint64_t _M0L4mul0S608;
  int32_t _M0L6_2atmpS1848;
  int32_t _M0L6_2atmpS1847;
  uint64_t _M0L4mul1S609;
  uint64_t _M0L1mS610;
  struct _M0TPB7Umul128 _M0L7_2abindS611;
  uint64_t _M0L7_2alow1S612;
  uint64_t _M0L8_2ahigh1S613;
  struct _M0TPB7Umul128 _M0L7_2abindS614;
  uint64_t _M0L7_2alow0S615;
  uint64_t _M0L8_2ahigh0S616;
  uint64_t _M0L3sumS617;
  uint64_t _M0Lm5high1S618;
  int32_t _M0L6_2atmpS1845;
  int32_t _M0L6_2atmpS1846;
  int32_t _M0L5deltaS619;
  uint64_t _M0L6_2atmpS1837;
  int32_t _M0L6_2atmpS1844;
  uint32_t _M0L6_2atmpS1841;
  int32_t _M0L6_2atmpS1843;
  int32_t _M0L6_2atmpS1842;
  uint32_t _M0L6_2atmpS1840;
  uint32_t _M0L6_2atmpS1839;
  uint64_t _M0L6_2atmpS1838;
  uint64_t _M0L1aS620;
  uint64_t _M0L6_2atmpS1836;
  uint64_t _M0L1bS621;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS604 = _M0L1iS605 / 26;
  _M0L5base2S606 = _M0L4baseS604 * 26;
  _M0L6offsetS607 = _M0L1iS605 - _M0L5base2S606;
  _M0L6_2atmpS1849 = _M0L4baseS604 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1849);
  _M0L6_2atmpS1848 = _M0L4baseS604 * 2;
  _M0L6_2atmpS1847 = _M0L6_2atmpS1848 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1847);
  if (_M0L6offsetS607 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S608, .$1 = _M0L4mul1S609};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS610
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS607);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS611 = _M0FPB7umul128(_M0L1mS610, _M0L4mul1S609);
  _M0L7_2alow1S612 = _M0L7_2abindS611.$0;
  _M0L8_2ahigh1S613 = _M0L7_2abindS611.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS614 = _M0FPB7umul128(_M0L1mS610, _M0L4mul0S608);
  _M0L7_2alow0S615 = _M0L7_2abindS614.$0;
  _M0L8_2ahigh0S616 = _M0L7_2abindS614.$1;
  _M0L3sumS617 = _M0L8_2ahigh0S616 + _M0L7_2alow1S612;
  _M0Lm5high1S618 = _M0L8_2ahigh1S613;
  if (_M0L3sumS617 < _M0L8_2ahigh0S616) {
    uint64_t _M0L6_2atmpS1835 = _M0Lm5high1S618;
    _M0Lm5high1S618 = _M0L6_2atmpS1835 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1845 = _M0FPB8pow5bits(_M0L1iS605);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1846 = _M0FPB8pow5bits(_M0L5base2S606);
  _M0L5deltaS619 = _M0L6_2atmpS1845 - _M0L6_2atmpS1846;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1837
  = _M0FPB13shiftright128(_M0L7_2alow0S615, _M0L3sumS617, _M0L5deltaS619);
  _M0L6_2atmpS1844 = _M0L1iS605 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1841
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1844);
  _M0L6_2atmpS1843 = _M0L1iS605 % 16;
  _M0L6_2atmpS1842 = _M0L6_2atmpS1843 << 1;
  _M0L6_2atmpS1840 = _M0L6_2atmpS1841 >> (_M0L6_2atmpS1842 & 31);
  _M0L6_2atmpS1839 = _M0L6_2atmpS1840 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1838 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1839);
  _M0L1aS620 = _M0L6_2atmpS1837 + _M0L6_2atmpS1838;
  _M0L6_2atmpS1836 = _M0Lm5high1S618;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS621
  = _M0FPB13shiftright128(_M0L3sumS617, _M0L6_2atmpS1836, _M0L5deltaS619);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS620, .$1 = _M0L1bS621};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS578,
  struct _M0TPB8Pow5Pair _M0L3mulS575,
  int32_t _M0L1jS591,
  int32_t _M0L7mmShiftS593
) {
  uint64_t _M0L7_2amul0S574;
  uint64_t _M0L7_2amul1S576;
  uint64_t _M0L1mS577;
  struct _M0TPB7Umul128 _M0L7_2abindS579;
  uint64_t _M0L5_2aloS580;
  uint64_t _M0L6_2atmpS581;
  struct _M0TPB7Umul128 _M0L7_2abindS582;
  uint64_t _M0L6_2alo2S583;
  uint64_t _M0L6_2ahi2S584;
  uint64_t _M0L3midS585;
  uint64_t _M0L6_2atmpS1834;
  uint64_t _M0L2hiS586;
  uint64_t _M0L3lo2S587;
  uint64_t _M0L6_2atmpS1832;
  uint64_t _M0L6_2atmpS1833;
  uint64_t _M0L4mid2S588;
  uint64_t _M0L6_2atmpS1831;
  uint64_t _M0L3hi2S589;
  int32_t _M0L6_2atmpS1830;
  int32_t _M0L6_2atmpS1829;
  uint64_t _M0L2vpS590;
  uint64_t _M0Lm2vmS592;
  int32_t _M0L6_2atmpS1828;
  int32_t _M0L6_2atmpS1827;
  uint64_t _M0L2vrS603;
  uint64_t _M0L6_2atmpS1826;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S574 = _M0L3mulS575.$0;
  _M0L7_2amul1S576 = _M0L3mulS575.$1;
  _M0L1mS577 = _M0L1mS578 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS579 = _M0FPB7umul128(_M0L1mS577, _M0L7_2amul0S574);
  _M0L5_2aloS580 = _M0L7_2abindS579.$0;
  _M0L6_2atmpS581 = _M0L7_2abindS579.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS582 = _M0FPB7umul128(_M0L1mS577, _M0L7_2amul1S576);
  _M0L6_2alo2S583 = _M0L7_2abindS582.$0;
  _M0L6_2ahi2S584 = _M0L7_2abindS582.$1;
  _M0L3midS585 = _M0L6_2atmpS581 + _M0L6_2alo2S583;
  if (_M0L3midS585 < _M0L6_2atmpS581) {
    _M0L6_2atmpS1834 = 1ull;
  } else {
    _M0L6_2atmpS1834 = 0ull;
  }
  _M0L2hiS586 = _M0L6_2ahi2S584 + _M0L6_2atmpS1834;
  _M0L3lo2S587 = _M0L5_2aloS580 + _M0L7_2amul0S574;
  _M0L6_2atmpS1832 = _M0L3midS585 + _M0L7_2amul1S576;
  if (_M0L3lo2S587 < _M0L5_2aloS580) {
    _M0L6_2atmpS1833 = 1ull;
  } else {
    _M0L6_2atmpS1833 = 0ull;
  }
  _M0L4mid2S588 = _M0L6_2atmpS1832 + _M0L6_2atmpS1833;
  if (_M0L4mid2S588 < _M0L3midS585) {
    _M0L6_2atmpS1831 = 1ull;
  } else {
    _M0L6_2atmpS1831 = 0ull;
  }
  _M0L3hi2S589 = _M0L2hiS586 + _M0L6_2atmpS1831;
  _M0L6_2atmpS1830 = _M0L1jS591 - 64;
  _M0L6_2atmpS1829 = _M0L6_2atmpS1830 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS590
  = _M0FPB13shiftright128(_M0L4mid2S588, _M0L3hi2S589, _M0L6_2atmpS1829);
  _M0Lm2vmS592 = 0ull;
  if (_M0L7mmShiftS593) {
    uint64_t _M0L3lo3S594 = _M0L5_2aloS580 - _M0L7_2amul0S574;
    uint64_t _M0L6_2atmpS1816 = _M0L3midS585 - _M0L7_2amul1S576;
    uint64_t _M0L6_2atmpS1817;
    uint64_t _M0L4mid3S595;
    uint64_t _M0L6_2atmpS1815;
    uint64_t _M0L3hi3S596;
    int32_t _M0L6_2atmpS1814;
    int32_t _M0L6_2atmpS1813;
    if (_M0L5_2aloS580 < _M0L3lo3S594) {
      _M0L6_2atmpS1817 = 1ull;
    } else {
      _M0L6_2atmpS1817 = 0ull;
    }
    _M0L4mid3S595 = _M0L6_2atmpS1816 - _M0L6_2atmpS1817;
    if (_M0L3midS585 < _M0L4mid3S595) {
      _M0L6_2atmpS1815 = 1ull;
    } else {
      _M0L6_2atmpS1815 = 0ull;
    }
    _M0L3hi3S596 = _M0L2hiS586 - _M0L6_2atmpS1815;
    _M0L6_2atmpS1814 = _M0L1jS591 - 64;
    _M0L6_2atmpS1813 = _M0L6_2atmpS1814 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid3S595, _M0L3hi3S596, _M0L6_2atmpS1813);
  } else {
    uint64_t _M0L3lo3S597 = _M0L5_2aloS580 + _M0L5_2aloS580;
    uint64_t _M0L6_2atmpS1824 = _M0L3midS585 + _M0L3midS585;
    uint64_t _M0L6_2atmpS1825;
    uint64_t _M0L4mid3S598;
    uint64_t _M0L6_2atmpS1822;
    uint64_t _M0L6_2atmpS1823;
    uint64_t _M0L3hi3S599;
    uint64_t _M0L3lo4S600;
    uint64_t _M0L6_2atmpS1820;
    uint64_t _M0L6_2atmpS1821;
    uint64_t _M0L4mid4S601;
    uint64_t _M0L6_2atmpS1819;
    uint64_t _M0L3hi4S602;
    int32_t _M0L6_2atmpS1818;
    if (_M0L3lo3S597 < _M0L5_2aloS580) {
      _M0L6_2atmpS1825 = 1ull;
    } else {
      _M0L6_2atmpS1825 = 0ull;
    }
    _M0L4mid3S598 = _M0L6_2atmpS1824 + _M0L6_2atmpS1825;
    _M0L6_2atmpS1822 = _M0L2hiS586 + _M0L2hiS586;
    if (_M0L4mid3S598 < _M0L3midS585) {
      _M0L6_2atmpS1823 = 1ull;
    } else {
      _M0L6_2atmpS1823 = 0ull;
    }
    _M0L3hi3S599 = _M0L6_2atmpS1822 + _M0L6_2atmpS1823;
    _M0L3lo4S600 = _M0L3lo3S597 - _M0L7_2amul0S574;
    _M0L6_2atmpS1820 = _M0L4mid3S598 - _M0L7_2amul1S576;
    if (_M0L3lo3S597 < _M0L3lo4S600) {
      _M0L6_2atmpS1821 = 1ull;
    } else {
      _M0L6_2atmpS1821 = 0ull;
    }
    _M0L4mid4S601 = _M0L6_2atmpS1820 - _M0L6_2atmpS1821;
    if (_M0L4mid3S598 < _M0L4mid4S601) {
      _M0L6_2atmpS1819 = 1ull;
    } else {
      _M0L6_2atmpS1819 = 0ull;
    }
    _M0L3hi4S602 = _M0L3hi3S599 - _M0L6_2atmpS1819;
    _M0L6_2atmpS1818 = _M0L1jS591 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid4S601, _M0L3hi4S602, _M0L6_2atmpS1818);
  }
  _M0L6_2atmpS1828 = _M0L1jS591 - 64;
  _M0L6_2atmpS1827 = _M0L6_2atmpS1828 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS603
  = _M0FPB13shiftright128(_M0L3midS585, _M0L2hiS586, _M0L6_2atmpS1827);
  _M0L6_2atmpS1826 = _M0Lm2vmS592;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS603,
                                                .$1 = _M0L2vpS590,
                                                .$2 = _M0L6_2atmpS1826};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS572,
  int32_t _M0L1pS573
) {
  uint64_t _M0L6_2atmpS1812;
  uint64_t _M0L6_2atmpS1811;
  uint64_t _M0L6_2atmpS1810;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1812 = 1ull << (_M0L1pS573 & 63);
  _M0L6_2atmpS1811 = _M0L6_2atmpS1812 - 1ull;
  _M0L6_2atmpS1810 = _M0L5valueS572 & _M0L6_2atmpS1811;
  return _M0L6_2atmpS1810 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS570,
  int32_t _M0L1pS571
) {
  int32_t _M0L6_2atmpS1809;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1809 = _M0FPB10pow5Factor(_M0L5valueS570);
  return _M0L6_2atmpS1809 >= _M0L1pS571;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS565) {
  uint64_t _M0L6_2atmpS1800;
  uint64_t _M0L6_2atmpS1801;
  uint64_t _M0L6_2atmpS1802;
  uint64_t _M0L6_2atmpS1803;
  uint64_t _M0L6_2atmpS1808;
  int32_t _M0L5countS566;
  uint64_t _M0L1vS567;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1800 = _M0L5valueS565 % 5ull;
  if (_M0L6_2atmpS1800 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1801 = _M0L5valueS565 % 25ull;
  if (_M0L6_2atmpS1801 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1802 = _M0L5valueS565 % 125ull;
  if (_M0L6_2atmpS1802 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1803 = _M0L5valueS565 % 625ull;
  if (_M0L6_2atmpS1803 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1808 = _M0L5valueS565 / 625ull;
  _M0L5countS566 = 4;
  _M0L1vS567 = _M0L6_2atmpS1808;
  while (1) {
    if (_M0L1vS567 > 0ull) {
      uint64_t _M0L6_2atmpS1804 = _M0L1vS567 % 5ull;
      int32_t _M0L6_2atmpS1805;
      uint64_t _M0L6_2atmpS1806;
      if (_M0L6_2atmpS1804 != 0ull) {
        return _M0L5countS566;
      }
      _M0L6_2atmpS1805 = _M0L5countS566 + 1;
      _M0L6_2atmpS1806 = _M0L1vS567 / 5ull;
      _M0L5countS566 = _M0L6_2atmpS1805;
      _M0L1vS567 = _M0L6_2atmpS1806;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS569;
      moonbit_string_t _M0L6_2atmpS1807;
      int32_t _result_2795;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS569
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS569, (moonbit_string_t)moonbit_string_literal_13.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS569, _M0L5valueS565);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1807
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS569);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS569);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2795 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1807);
      moonbit_decref_cycle_free(_M0L6_2atmpS1807);
      return _result_2795;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS564,
  uint64_t _M0L2hiS562,
  int32_t _M0L4distS563
) {
  int32_t _M0L6_2atmpS1799;
  uint64_t _M0L6_2atmpS1797;
  uint64_t _M0L6_2atmpS1798;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1799 = 64 - _M0L4distS563;
  _M0L6_2atmpS1797 = _M0L2hiS562 << (_M0L6_2atmpS1799 & 63);
  _M0L6_2atmpS1798 = _M0L2loS564 >> (_M0L4distS563 & 63);
  return _M0L6_2atmpS1797 | _M0L6_2atmpS1798;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS552,
  uint64_t _M0L1bS555
) {
  uint64_t _M0L3aLoS551;
  uint64_t _M0L3aHiS553;
  uint64_t _M0L3bLoS554;
  uint64_t _M0L3bHiS556;
  uint64_t _M0L1xS557;
  uint64_t _M0L6_2atmpS1795;
  uint64_t _M0L6_2atmpS1796;
  uint64_t _M0L1yS558;
  uint64_t _M0L6_2atmpS1793;
  uint64_t _M0L6_2atmpS1794;
  uint64_t _M0L1zS559;
  uint64_t _M0L6_2atmpS1791;
  uint64_t _M0L6_2atmpS1792;
  uint64_t _M0L6_2atmpS1789;
  uint64_t _M0L6_2atmpS1790;
  uint64_t _M0L1wS560;
  uint64_t _M0L2loS561;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS551 = _M0L1aS552 & 4294967295ull;
  _M0L3aHiS553 = _M0L1aS552 >> 32;
  _M0L3bLoS554 = _M0L1bS555 & 4294967295ull;
  _M0L3bHiS556 = _M0L1bS555 >> 32;
  _M0L1xS557 = _M0L3aLoS551 * _M0L3bLoS554;
  _M0L6_2atmpS1795 = _M0L3aHiS553 * _M0L3bLoS554;
  _M0L6_2atmpS1796 = _M0L1xS557 >> 32;
  _M0L1yS558 = _M0L6_2atmpS1795 + _M0L6_2atmpS1796;
  _M0L6_2atmpS1793 = _M0L3aLoS551 * _M0L3bHiS556;
  _M0L6_2atmpS1794 = _M0L1yS558 & 4294967295ull;
  _M0L1zS559 = _M0L6_2atmpS1793 + _M0L6_2atmpS1794;
  _M0L6_2atmpS1791 = _M0L3aHiS553 * _M0L3bHiS556;
  _M0L6_2atmpS1792 = _M0L1yS558 >> 32;
  _M0L6_2atmpS1789 = _M0L6_2atmpS1791 + _M0L6_2atmpS1792;
  _M0L6_2atmpS1790 = _M0L1zS559 >> 32;
  _M0L1wS560 = _M0L6_2atmpS1789 + _M0L6_2atmpS1790;
  _M0L2loS561 = _M0L1aS552 * _M0L1bS555;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS561, .$1 = _M0L1wS560};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS549,
  int32_t _M0L4fromS546,
  int32_t _M0L2toS545
) {
  int32_t _M0L3lenS544;
  int32_t _M0L6_2atmpS1788;
  uint16_t* _M0L6bufferS547;
  int32_t _M0L1iS548;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS544 = _M0L2toS545 - _M0L4fromS546;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1788 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS547
  = (uint16_t*)moonbit_make_string(_M0L3lenS544, _M0L6_2atmpS1788);
  _M0L1iS548 = 0;
  while (1) {
    if (_M0L1iS548 < _M0L3lenS544) {
      int32_t _M0L6_2atmpS1786 = _M0L4fromS546 + _M0L1iS548;
      int32_t _M0L6_2atmpS1785;
      int32_t _M0L6_2atmpS1784;
      int32_t _M0L6_2atmpS1787;
      if (
        _M0L6_2atmpS1786 < 0
        || _M0L6_2atmpS1786 >= Moonbit_array_length(_M0L5bytesS549)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1785 = (int32_t)_M0L5bytesS549[_M0L6_2atmpS1786];
      _M0L6_2atmpS1784 = (uint16_t)_M0L6_2atmpS1785;
      if (
        _M0L1iS548 < 0 || _M0L1iS548 >= Moonbit_array_length(_M0L6bufferS547)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS547[_M0L1iS548] = _M0L6_2atmpS1784;
      _M0L6_2atmpS1787 = _M0L1iS548 + 1;
      _M0L1iS548 = _M0L6_2atmpS1787;
      continue;
    }
    break;
  }
  return _M0L6bufferS547;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS543) {
  int32_t _M0L6_2atmpS1783;
  uint32_t _M0L6_2atmpS1782;
  uint32_t _M0L6_2atmpS1781;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1783 = _M0L1eS543 * 78913;
  _M0L6_2atmpS1782 = *(uint32_t*)&_M0L6_2atmpS1783;
  _M0L6_2atmpS1781 = _M0L6_2atmpS1782 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1781;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS1780;
  uint32_t _M0L6_2atmpS1779;
  uint32_t _M0L6_2atmpS1778;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1780 = _M0L1eS542 * 732923;
  _M0L6_2atmpS1779 = *(uint32_t*)&_M0L6_2atmpS1780;
  _M0L6_2atmpS1778 = _M0L6_2atmpS1779 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1778;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS540,
  int32_t _M0L8exponentS541,
  int32_t _M0L8mantissaS538
) {
  moonbit_string_t _M0L1sS539;
  moonbit_string_t _result_2798;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS538) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L4signS540) {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_15.data;
  } else {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS541) {
    moonbit_string_t _result_2797;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2797
    = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_16.data);
    moonbit_decref_cycle_free(_M0L1sS539);
    return _result_2797;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2798
  = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref_cycle_free(_M0L1sS539);
  return _result_2798;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS537) {
  int32_t _M0L6_2atmpS1777;
  uint32_t _M0L6_2atmpS1776;
  uint32_t _M0L6_2atmpS1775;
  int32_t _M0L6_2atmpS1774;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1777 = _M0L1eS537 * 1217359;
  _M0L6_2atmpS1776 = *(uint32_t*)&_M0L6_2atmpS1777;
  _M0L6_2atmpS1775 = _M0L6_2atmpS1776 >> 19;
  _M0L6_2atmpS1774 = *(int32_t*)&_M0L6_2atmpS1775;
  return _M0L6_2atmpS1774 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS536) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS536 != _M0L4selfS536) {
    return 0;
  } else if (_M0L4selfS536 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS536 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS536;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS535) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS535 != _M0L4selfS535) {
    return 0ll;
  } else if (_M0L4selfS535 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS535 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS535;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS531
) {
  float* _M0L6_2atmpS1770;
  struct _M0TPB5ArrayGfE* _block_2799;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1770 = (float*)moonbit_make_float_array_raw(_M0L3lenS531);
  _block_2799
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2799)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2799->$0 = _M0L6_2atmpS1770;
  _block_2799->$1 = _M0L3lenS531;
  return _block_2799;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS532
) {
  uint8_t* _M0L6_2atmpS1771;
  struct _M0TPB5ArrayGbE* _block_2800;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1771 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS532);
  _block_2800
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2800)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _block_2800->$0 = _M0L6_2atmpS1771;
  _block_2800->$1 = _M0L3lenS532;
  return _block_2800;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS533
) {
  int32_t* _M0L6_2atmpS1772;
  struct _M0TPB5ArrayGiE* _block_2801;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1772 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS533);
  _block_2801
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2801)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2801->$0 = _M0L6_2atmpS1772;
  _block_2801->$1 = _M0L3lenS533;
  return _block_2801;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS534
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1773;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2802;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1773
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS534, 0);
  _block_2802
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2802)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 72, 0);
  _block_2802->$0 = _M0L6_2atmpS1773;
  _block_2802->$1 = _M0L3lenS534;
  return _block_2802;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint64_t* _M0L6_2atmpS1768;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1768 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS1768)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1768[_M0L5indexS528];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS529,
  int32_t _M0L5indexS530
) {
  uint32_t* _M0L6_2atmpS1769;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1769 = _M0L4selfS529;
  if (
    _M0L5indexS530 < 0
    || _M0L5indexS530 >= Moonbit_array_length(_M0L6_2atmpS1769)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1769[_M0L5indexS530];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS526
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS526, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS525) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS525, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS524) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS524;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS512,
  moonbit_string_t _M0L5valueS514
) {
  int32_t _M0L3lenS1740;
  moonbit_string_t* _M0L6_2atmpS1742;
  int32_t _M0L6_2atmpS1741;
  int32_t _M0L6lengthS513;
  moonbit_string_t* _M0L3bufS1745;
  moonbit_string_t _M0L6_2aoldS2663;
  int32_t _M0L6_2atmpS1746;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1740 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1742 = _M0MPC15array5Array6bufferGsE(_M0L4selfS512);
  _M0L6_2atmpS1741 = Moonbit_array_length(_M0L6_2atmpS1742);
  moonbit_decref_cycle_free(_M0L6_2atmpS1742);
  if (_M0L3lenS1740 == _M0L6_2atmpS1741) {
    int32_t _M0L3lenS1744 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS1743 = _M0L3lenS1744 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS512, _M0L6_2atmpS1743);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS1745 = _M0L4selfS512->$0;
  _M0L6_2aoldS2663 = (moonbit_string_t)_M0L3bufS1745[_M0L6lengthS513];
  moonbit_decref_cycle_free(_M0L6_2aoldS2663);
  _M0L3bufS1745[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS1746 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS1746;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS515,
  struct _M0TUsiE* _M0L5valueS517
) {
  int32_t _M0L3lenS1747;
  struct _M0TUsiE** _M0L6_2atmpS1749;
  int32_t _M0L6_2atmpS1748;
  int32_t _M0L6lengthS516;
  struct _M0TUsiE** _M0L3bufS1752;
  struct _M0TUsiE* _M0L6_2aoldS2664;
  int32_t _M0L6_2atmpS1753;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1747 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1749 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS515);
  _M0L6_2atmpS1748 = Moonbit_array_length(_M0L6_2atmpS1749);
  moonbit_decref_cycle_free(_M0L6_2atmpS1749);
  if (_M0L3lenS1747 == _M0L6_2atmpS1748) {
    int32_t _M0L3lenS1751 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS1750 = _M0L3lenS1751 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS515, _M0L6_2atmpS1750);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS1752 = _M0L4selfS515->$0;
  _M0L6_2aoldS2664 = (struct _M0TUsiE*)_M0L3bufS1752[_M0L6lengthS516];
  if (_M0L6_2aoldS2664) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2664);
  }
  _M0L3bufS1752[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS1753 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS1753;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS518,
  int32_t _M0L5valueS520
) {
  int32_t _M0L3lenS1754;
  int32_t* _M0L6_2atmpS1756;
  int32_t _M0L6_2atmpS1755;
  int32_t _M0L6lengthS519;
  int32_t* _M0L3bufS1759;
  int32_t _M0L6_2atmpS1760;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1754 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1756 = _M0MPC15array5Array6bufferGiE(_M0L4selfS518);
  _M0L6_2atmpS1755 = Moonbit_array_length(_M0L6_2atmpS1756);
  moonbit_decref_cycle_free(_M0L6_2atmpS1756);
  if (_M0L3lenS1754 == _M0L6_2atmpS1755) {
    int32_t _M0L3lenS1758 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS1757 = _M0L3lenS1758 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS518, _M0L6_2atmpS1757);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS1759 = _M0L4selfS518->$0;
  _M0L3bufS1759[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS1760 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS1760;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS521,
  float _M0L5valueS523
) {
  int32_t _M0L3lenS1761;
  float* _M0L6_2atmpS1763;
  int32_t _M0L6_2atmpS1762;
  int32_t _M0L6lengthS522;
  float* _M0L3bufS1766;
  int32_t _M0L6_2atmpS1767;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1761 = _M0L4selfS521->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1763 = _M0MPC15array5Array6bufferGfE(_M0L4selfS521);
  _M0L6_2atmpS1762 = Moonbit_array_length(_M0L6_2atmpS1763);
  moonbit_decref_cycle_free(_M0L6_2atmpS1763);
  if (_M0L3lenS1761 == _M0L6_2atmpS1762) {
    int32_t _M0L3lenS1765 = _M0L4selfS521->$1;
    int32_t _M0L6_2atmpS1764 = _M0L3lenS1765 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS521, _M0L6_2atmpS1764);
  }
  _M0L6lengthS522 = _M0L4selfS521->$1;
  _M0L3bufS1766 = _M0L4selfS521->$0;
  _M0L3bufS1766[_M0L6lengthS522] = _M0L5valueS523;
  _M0L6_2atmpS1767 = _M0L6lengthS522 + 1;
  _M0L4selfS521->$1 = _M0L6_2atmpS1767;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS497,
  int32_t _M0L8requiredS499
) {
  int32_t _M0L8old__capS496;
  int32_t _M0L3lenS1736;
  int32_t _M0L8new__capS498;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS496 = _M0MPC15array5Array8capacityGsE(_M0L4selfS497);
  _M0L3lenS1736 = _M0L4selfS497->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS498
  = _M0FPB23array__growth__capacity(_M0L8old__capS496, _M0L3lenS1736, _M0L8requiredS499);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS497, _M0L8new__capS498);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS501,
  int32_t _M0L8requiredS503
) {
  int32_t _M0L8old__capS500;
  int32_t _M0L3lenS1737;
  int32_t _M0L8new__capS502;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS500 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS501);
  _M0L3lenS1737 = _M0L4selfS501->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS502
  = _M0FPB23array__growth__capacity(_M0L8old__capS500, _M0L3lenS1737, _M0L8requiredS503);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS501, _M0L8new__capS502);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS505,
  int32_t _M0L8requiredS507
) {
  int32_t _M0L8old__capS504;
  int32_t _M0L3lenS1738;
  int32_t _M0L8new__capS506;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS504 = _M0MPC15array5Array8capacityGiE(_M0L4selfS505);
  _M0L3lenS1738 = _M0L4selfS505->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS506
  = _M0FPB23array__growth__capacity(_M0L8old__capS504, _M0L3lenS1738, _M0L8requiredS507);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS505, _M0L8new__capS506);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS509,
  int32_t _M0L8requiredS511
) {
  int32_t _M0L8old__capS508;
  int32_t _M0L3lenS1739;
  int32_t _M0L8new__capS510;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS508 = _M0MPC15array5Array8capacityGfE(_M0L4selfS509);
  _M0L3lenS1739 = _M0L4selfS509->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS510
  = _M0FPB23array__growth__capacity(_M0L8old__capS508, _M0L3lenS1739, _M0L8requiredS511);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS509, _M0L8new__capS510);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS473,
  int32_t _M0L13new__capacityS476
) {
  moonbit_string_t* _M0L8old__bufS472;
  int32_t _M0L3lenS474;
  int32_t _M0L9copy__lenS475;
  moonbit_string_t* _M0L8new__bufS477;
  moonbit_string_t* _M0L6_2aoldS2665;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS472 = _M0L4selfS473->$0;
  _M0L3lenS474 = _M0L4selfS473->$1;
  if (_M0L3lenS474 < _M0L13new__capacityS476) {
    _M0L9copy__lenS475 = _M0L3lenS474;
  } else {
    _M0L9copy__lenS475 = _M0L13new__capacityS476;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS472);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS477
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS472, _M0L13new__capacityS476, _M0L9copy__lenS475, 0, 0);
  _M0L6_2aoldS2665 = _M0L4selfS473->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2665);
  _M0L4selfS473->$0 = _M0L8new__bufS477;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS479,
  int32_t _M0L13new__capacityS482
) {
  struct _M0TUsiE** _M0L8old__bufS478;
  int32_t _M0L3lenS480;
  int32_t _M0L9copy__lenS481;
  struct _M0TUsiE** _M0L8new__bufS483;
  struct _M0TUsiE** _M0L6_2aoldS2666;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS478 = _M0L4selfS479->$0;
  _M0L3lenS480 = _M0L4selfS479->$1;
  if (_M0L3lenS480 < _M0L13new__capacityS482) {
    _M0L9copy__lenS481 = _M0L3lenS480;
  } else {
    _M0L9copy__lenS481 = _M0L13new__capacityS482;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS478);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS483
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS478, _M0L13new__capacityS482, _M0L9copy__lenS481, 0, 0);
  _M0L6_2aoldS2666 = _M0L4selfS479->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2666);
  _M0L4selfS479->$0 = _M0L8new__bufS483;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS485,
  int32_t _M0L13new__capacityS488
) {
  int32_t* _M0L8old__bufS484;
  int32_t _M0L3lenS486;
  int32_t _M0L9copy__lenS487;
  int32_t* _M0L8new__bufS489;
  int32_t* _M0L6_2aoldS2667;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS484 = _M0L4selfS485->$0;
  _M0L3lenS486 = _M0L4selfS485->$1;
  if (_M0L3lenS486 < _M0L13new__capacityS488) {
    _M0L9copy__lenS487 = _M0L3lenS486;
  } else {
    _M0L9copy__lenS487 = _M0L13new__capacityS488;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS484);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS489
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS484, _M0L13new__capacityS488, _M0L9copy__lenS487, 0, 0);
  _M0L6_2aoldS2667 = _M0L4selfS485->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2667);
  _M0L4selfS485->$0 = _M0L8new__bufS489;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS491,
  int32_t _M0L13new__capacityS494
) {
  float* _M0L8old__bufS490;
  int32_t _M0L3lenS492;
  int32_t _M0L9copy__lenS493;
  float* _M0L8new__bufS495;
  float* _M0L6_2aoldS2668;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS490 = _M0L4selfS491->$0;
  _M0L3lenS492 = _M0L4selfS491->$1;
  if (_M0L3lenS492 < _M0L13new__capacityS494) {
    _M0L9copy__lenS493 = _M0L3lenS492;
  } else {
    _M0L9copy__lenS493 = _M0L13new__capacityS494;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS490);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS495
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS490, _M0L13new__capacityS494, _M0L9copy__lenS493, 0, 0);
  _M0L6_2aoldS2668 = _M0L4selfS491->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2668);
  _M0L4selfS491->$0 = _M0L8new__bufS495;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS468
) {
  moonbit_string_t* _M0L6_2atmpS1732;
  int32_t _result_2803;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1732 = _M0MPC15array5Array6bufferGsE(_M0L4selfS468);
  _result_2803 = Moonbit_array_length(_M0L6_2atmpS1732);
  moonbit_decref_cycle_free(_M0L6_2atmpS1732);
  return _result_2803;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS469
) {
  struct _M0TUsiE** _M0L6_2atmpS1733;
  int32_t _result_2804;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1733 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS469);
  _result_2804 = Moonbit_array_length(_M0L6_2atmpS1733);
  moonbit_decref_cycle_free(_M0L6_2atmpS1733);
  return _result_2804;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS470
) {
  int32_t* _M0L6_2atmpS1734;
  int32_t _result_2805;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1734 = _M0MPC15array5Array6bufferGiE(_M0L4selfS470);
  _result_2805 = Moonbit_array_length(_M0L6_2atmpS1734);
  moonbit_decref_cycle_free(_M0L6_2atmpS1734);
  return _result_2805;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS471
) {
  float* _M0L6_2atmpS1735;
  int32_t _result_2806;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1735 = _M0MPC15array5Array6bufferGfE(_M0L4selfS471);
  _result_2806 = Moonbit_array_length(_M0L6_2atmpS1735);
  moonbit_decref_cycle_free(_M0L6_2atmpS1735);
  return _result_2806;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS464,
  int32_t _M0L3lenS462,
  int32_t _M0L8requiredS461
) {
  int32_t _M0L5startS463;
  int32_t _M0L5spaceS465;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS461 < _M0L3lenS462) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L7currentS464 == 0) {
    _M0L5startS463 = 8;
  } else {
    _M0L5startS463 = _M0L7currentS464;
  }
  _M0L5spaceS465 = _M0L5startS463;
  while (1) {
    if (_M0L5spaceS465 < _M0L8requiredS461) {
      int32_t _M0L4nextS466 = _M0L5spaceS465 * 2;
      if (_M0L4nextS466 <= _M0L5spaceS465) {
        return _M0L8requiredS461;
      }
      _M0L5spaceS465 = _M0L4nextS466;
      continue;
    } else {
      return _M0L5spaceS465;
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

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS452
) {
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L8_2afieldS2669;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2669 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2669);
  return _M0L8_2afieldS2669;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS453) {
  float* _M0L8_2afieldS2670;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2670 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2670);
  return _M0L8_2afieldS2670;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS454
) {
  moonbit_string_t* _M0L8_2afieldS2671;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2671 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2671);
  return _M0L8_2afieldS2671;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS455
) {
  struct _M0TUsiE** _M0L8_2afieldS2672;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2672 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2672);
  return _M0L8_2afieldS2672;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS456
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2673;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2673 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2673);
  return _M0L8_2afieldS2673;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS457) {
  int32_t* _M0L8_2afieldS2674;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2674 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2674);
  return _M0L8_2afieldS2674;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS458) {
  uint8_t* _M0L8_2afieldS2675;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2675 = _M0L4selfS458->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2675);
  return _M0L8_2afieldS2675;
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
  int32_t _M0L3endS1730;
  int32_t _M0L5startS1731;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1729;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1722;
  int32_t _M0L6_2atmpS1721;
  int32_t _if__result_2808;
  uint16_t* _M0L4dataS1723;
  int32_t _M0L3lenS1724;
  moonbit_string_t _M0L6_2atmpS1725;
  int32_t _M0L6_2atmpS1726;
  int32_t _M0L3lenS1728;
  int32_t _M0L6_2atmpS1727;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1730 = _M0L3strS448.$2;
  _M0L5startS1731 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1730 - _M0L5startS1731;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1729 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1729 + _M0L8str__lenS447;
  _M0L4dataS1722 = _M0L4selfS450->$0;
  _M0L6_2atmpS1721 = Moonbit_array_length(_M0L4dataS1722);
  if (_M0L8requiredS449 > _M0L6_2atmpS1721) {
    _if__result_2808 = 1;
  } else {
    int32_t _M0L3lenS1720 = _M0L4selfS450->$1;
    _if__result_2808 = _M0L8requiredS449 < _M0L3lenS1720;
  }
  if (_if__result_2808) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1723 = _M0L4selfS450->$0;
  _M0L3lenS1724 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1723);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1725 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1726 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1723, _M0L3lenS1724, _M0L6_2atmpS1725, _M0L6_2atmpS1726, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1723);
  moonbit_decref_cycle_free(_M0L6_2atmpS1725);
  _M0L3lenS1728 = _M0L4selfS450->$1;
  _M0L6_2atmpS1727 = _M0L3lenS1728 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1727;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2809;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1719;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1718;
  moonbit_string_t _result_2810;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1717 = Moonbit_array_length(_M0L3strS444);
    _if__result_2809 = _M0L3endS443 == _M0L6_2atmpS1717;
  } else {
    _if__result_2809 = 0;
  }
  if (_if__result_2809) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1719 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1719, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1718 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2810
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1718, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1718);
  return _result_2810;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2811;
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
      int32_t _M0L6_2atmpS1716 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2811 = _M0L6_2atmpS1716 <= _M0L3lenS436;
    } else {
      _if__result_2811 = 0;
    }
  } else {
    _if__result_2811 = 0;
  }
  if (_if__result_2811) {
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
  int32_t _M0L6_2atmpS1715;
  int32_t _M0L6_2atmpS1714;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1713;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1715 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1714 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1715;
  _M0L2e1S422 = _M0L6_2atmpS1714 - 1;
  _M0L6_2atmpS1713 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1713 - 1;
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
        int32_t _M0L6_2atmpS1710 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1709 = (int32_t)_M0L6_2atmpS1710;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1709;
        uint32_t _M0L6_2atmpS1705 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1704;
        int32_t _M0L6_2atmpS1706;
        uint32_t _M0L6_2atmpS1708;
        int32_t _M0L6_2atmpS1707;
        int32_t _M0L6_2atmpS1711;
        int32_t _M0L6_2atmpS1712;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1704 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1705);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1704;
        _M0L6_2atmpS1706 = _M0L1jS433 + 1;
        _M0L6_2atmpS1708 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1707 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1708);
        if (
          _M0L6_2atmpS1706 < 0
          || _M0L6_2atmpS1706 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1706] = _M0L6_2atmpS1707;
        _M0L6_2atmpS1711 = _M0L1iS432 + 1;
        _M0L6_2atmpS1712 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1711;
        _M0L1jS433 = _M0L6_2atmpS1712;
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
  int32_t _M0L6_2atmpS1703;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1703 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1703 & 0xff;
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
    int64_t _M0L6_2atmpS1702 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1702;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1699;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1699 = 1;
      } else {
        _M0L6_2atmpS1699 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1699;
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
      int32_t _M0L6_2atmpS1700;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1700 = 1;
      } else {
        _M0L6_2atmpS1700 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1700;
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
      int32_t _M0L6_2atmpS1701;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1701 = 1;
      } else {
        _M0L6_2atmpS1701 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1701;
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
  int32_t _M0L6_2atmpS1698;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1698 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1698;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1675 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1675;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1674 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1673 = 48 + _M0L6_2atmpS1674;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1673;
      int32_t _M0L6_2atmpS1672 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1671 = 48 + _M0L6_2atmpS1672;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1671;
      int32_t _M0L6_2atmpS1670 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1669 = 48 + _M0L6_2atmpS1670;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1669;
      int32_t _M0L6_2atmpS1668 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1667 = 48 + _M0L6_2atmpS1668;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1667;
      int32_t _M0L6_2atmpS1659 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1658 = _M0L6_2atmpS1659 - 4;
      int32_t _M0L6_2atmpS1661;
      int32_t _M0L6_2atmpS1660;
      int32_t _M0L6_2atmpS1663;
      int32_t _M0L6_2atmpS1662;
      int32_t _M0L6_2atmpS1665;
      int32_t _M0L6_2atmpS1664;
      int32_t _M0L6_2atmpS1666;
      _M0L6bufferS381[_M0L6_2atmpS1658] = _M0L6d1__hiS377;
      _M0L6_2atmpS1661 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1660 = _M0L6_2atmpS1661 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1660] = _M0L6d1__loS378;
      _M0L6_2atmpS1663 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1662 = _M0L6_2atmpS1663 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1662] = _M0L6d2__hiS379;
      _M0L6_2atmpS1665 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1664 = _M0L6_2atmpS1665 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1664] = _M0L6d2__loS380;
      _M0L6_2atmpS1666 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1666;
      continue;
    } else {
      int32_t _M0L6_2atmpS1697 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1697;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1684 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1683 = 48 + _M0L6_2atmpS1684;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1683;
          int32_t _M0L6_2atmpS1682 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1681 = 48 + _M0L6_2atmpS1682;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1681;
          int32_t _M0L6_2atmpS1677 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1676 = _M0L6_2atmpS1677 - 2;
          int32_t _M0L6_2atmpS1679;
          int32_t _M0L6_2atmpS1678;
          int32_t _M0L6_2atmpS1680;
          _M0L6bufferS381[_M0L6_2atmpS1676] = _M0L5d__hiS388;
          _M0L6_2atmpS1679 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1678 = _M0L6_2atmpS1679 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1678] = _M0L5d__loS389;
          _M0L6_2atmpS1680 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1680;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1692 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1691 = 48 + _M0L6_2atmpS1692;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1691;
          int32_t _M0L6_2atmpS1690 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1689 = 48 + _M0L6_2atmpS1690;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1689;
          int32_t _M0L6_2atmpS1686 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1685 = _M0L6_2atmpS1686 - 2;
          int32_t _M0L6_2atmpS1688;
          int32_t _M0L6_2atmpS1687;
          _M0L6bufferS381[_M0L6_2atmpS1685] = _M0L5d__hiS391;
          _M0L6_2atmpS1688 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1687 = _M0L6_2atmpS1688 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1687] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1696 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1693 = _M0L6_2atmpS1696 - 1;
          int32_t _M0L6_2atmpS1695 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1694 = (uint16_t)_M0L6_2atmpS1695;
          _M0L6bufferS381[_M0L6_2atmpS1693] = _M0L6_2atmpS1694;
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
  int32_t _M0L6_2atmpS1643;
  int32_t _M0L6_2atmpS1642;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1643 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1642 = _M0L5radixS355 & _M0L6_2atmpS1643;
  if (_M0L6_2atmpS1642 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1650;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1650 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1650;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1649 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1649;
        int32_t _M0L6_2atmpS1646 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1644 = _M0L6_2atmpS1646 - 1;
        int32_t _M0L6_2atmpS1645 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1647;
        uint64_t _M0L6_2atmpS1648;
        _M0L6bufferS361[_M0L6_2atmpS1644] = _M0L6_2atmpS1645;
        _M0L6_2atmpS1647 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1648 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1647;
        _M0L1nS359 = _M0L6_2atmpS1648;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1657 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1657;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1656 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1655 = _M0L1nS367 - _M0L6_2atmpS1656;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1655;
        int32_t _M0L6_2atmpS1653 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1651 = _M0L6_2atmpS1653 - 1;
        int32_t _M0L6_2atmpS1652 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1654;
        _M0L6bufferS361[_M0L6_2atmpS1651] = _M0L6_2atmpS1652;
        _M0L6_2atmpS1654 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1654;
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
  int32_t _M0L6_2atmpS1641;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1641 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1641;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1638 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1638;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1632 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1630 = _M0L6_2atmpS1632 - 2;
      int32_t _M0L6_2atmpS1631 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1635;
      int32_t _M0L6_2atmpS1633;
      int32_t _M0L6_2atmpS1634;
      int32_t _M0L6_2atmpS1636;
      uint64_t _M0L6_2atmpS1637;
      _M0L6bufferS348[_M0L6_2atmpS1630] = _M0L6_2atmpS1631;
      _M0L6_2atmpS1635 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1633 = _M0L6_2atmpS1635 - 1;
      _M0L6_2atmpS1634
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1633] = _M0L6_2atmpS1634;
      _M0L6_2atmpS1636 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1637 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1636;
      _M0L1nS344 = _M0L6_2atmpS1637;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1640 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1640;
      int32_t _M0L6_2atmpS1639 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1639;
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
      uint64_t _M0L6_2atmpS1628 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1629 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1628;
      _M0L5countS341 = _M0L6_2atmpS1629;
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
    int32_t _M0L6_2atmpS1627;
    int32_t _M0L6_2atmpS1626;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1627 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1626 = _M0L6_2atmpS1627 / 4;
    return _M0L6_2atmpS1626 + 1;
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
    int32_t _M0L6_2atmpS1625 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1625;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1622;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1622 = 1;
      } else {
        _M0L6_2atmpS1622 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1622;
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
      int32_t _M0L6_2atmpS1623;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1623 = 1;
      } else {
        _M0L6_2atmpS1623 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1623;
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
      int32_t _M0L6_2atmpS1624;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1624 = 1;
      } else {
        _M0L6_2atmpS1624 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1624;
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
      uint32_t _M0L6_2atmpS1620 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1621 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1620;
      _M0L5countS315 = _M0L6_2atmpS1621;
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
    int32_t _M0L6_2atmpS1619;
    int32_t _M0L6_2atmpS1618;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1619 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1618 = _M0L6_2atmpS1619 / 4;
    return _M0L6_2atmpS1618 + 1;
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
  int32_t _M0L6_2atmpS1617;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1617 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1617;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1594 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1594;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1593 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1592 = 48 + _M0L6_2atmpS1593;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1592;
      int32_t _M0L6_2atmpS1591 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1590 = 48 + _M0L6_2atmpS1591;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1590;
      int32_t _M0L6_2atmpS1589 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1588 = 48 + _M0L6_2atmpS1589;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1588;
      int32_t _M0L6_2atmpS1587 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1586 = 48 + _M0L6_2atmpS1587;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1586;
      int32_t _M0L6_2atmpS1578 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1577 = _M0L6_2atmpS1578 - 4;
      int32_t _M0L6_2atmpS1580;
      int32_t _M0L6_2atmpS1579;
      int32_t _M0L6_2atmpS1582;
      int32_t _M0L6_2atmpS1581;
      int32_t _M0L6_2atmpS1584;
      int32_t _M0L6_2atmpS1583;
      int32_t _M0L6_2atmpS1585;
      _M0L6bufferS294[_M0L6_2atmpS1577] = _M0L6d1__hiS290;
      _M0L6_2atmpS1580 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1579 = _M0L6_2atmpS1580 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1579] = _M0L6d1__loS291;
      _M0L6_2atmpS1582 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1581 = _M0L6_2atmpS1582 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1581] = _M0L6d2__hiS292;
      _M0L6_2atmpS1584 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1583 = _M0L6_2atmpS1584 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1583] = _M0L6d2__loS293;
      _M0L6_2atmpS1585 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1585;
      continue;
    } else {
      int32_t _M0L6_2atmpS1616 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1616;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1603 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1602 = 48 + _M0L6_2atmpS1603;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1602;
          int32_t _M0L6_2atmpS1601 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1600 = 48 + _M0L6_2atmpS1601;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1600;
          int32_t _M0L6_2atmpS1596 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1595 = _M0L6_2atmpS1596 - 2;
          int32_t _M0L6_2atmpS1598;
          int32_t _M0L6_2atmpS1597;
          int32_t _M0L6_2atmpS1599;
          _M0L6bufferS294[_M0L6_2atmpS1595] = _M0L5d__hiS301;
          _M0L6_2atmpS1598 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1597 = _M0L6_2atmpS1598 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1597] = _M0L5d__loS302;
          _M0L6_2atmpS1599 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1599;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1611 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1610 = 48 + _M0L6_2atmpS1611;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1610;
          int32_t _M0L6_2atmpS1609 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1608 = 48 + _M0L6_2atmpS1609;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1608;
          int32_t _M0L6_2atmpS1605 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1604 = _M0L6_2atmpS1605 - 2;
          int32_t _M0L6_2atmpS1607;
          int32_t _M0L6_2atmpS1606;
          _M0L6bufferS294[_M0L6_2atmpS1604] = _M0L5d__hiS304;
          _M0L6_2atmpS1607 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1606 = _M0L6_2atmpS1607 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1606] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1615 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1612 = _M0L6_2atmpS1615 - 1;
          int32_t _M0L6_2atmpS1614 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1613 = (uint16_t)_M0L6_2atmpS1614;
          _M0L6bufferS294[_M0L6_2atmpS1612] = _M0L6_2atmpS1613;
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
  int32_t _M0L6_2atmpS1562;
  int32_t _M0L6_2atmpS1561;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1562 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1561 = _M0L5radixS268 & _M0L6_2atmpS1562;
  if (_M0L6_2atmpS1561 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1569;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1569 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1569;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1568 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1568;
        int32_t _M0L6_2atmpS1565 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1563 = _M0L6_2atmpS1565 - 1;
        int32_t _M0L6_2atmpS1564 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1566;
        uint32_t _M0L6_2atmpS1567;
        _M0L6bufferS274[_M0L6_2atmpS1563] = _M0L6_2atmpS1564;
        _M0L6_2atmpS1566 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1567 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1566;
        _M0L1nS272 = _M0L6_2atmpS1567;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1576 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1576;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1575 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1574 = _M0L1nS280 - _M0L6_2atmpS1575;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1574;
        int32_t _M0L6_2atmpS1572 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1570 = _M0L6_2atmpS1572 - 1;
        int32_t _M0L6_2atmpS1571 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1573;
        _M0L6bufferS274[_M0L6_2atmpS1570] = _M0L6_2atmpS1571;
        _M0L6_2atmpS1573 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1573;
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
  int32_t _M0L6_2atmpS1560;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1560 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1560;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1557 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1557;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1551 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1549 = _M0L6_2atmpS1551 - 2;
      int32_t _M0L6_2atmpS1550 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1554;
      int32_t _M0L6_2atmpS1552;
      int32_t _M0L6_2atmpS1553;
      int32_t _M0L6_2atmpS1555;
      uint32_t _M0L6_2atmpS1556;
      _M0L6bufferS261[_M0L6_2atmpS1549] = _M0L6_2atmpS1550;
      _M0L6_2atmpS1554 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1552 = _M0L6_2atmpS1554 - 1;
      _M0L6_2atmpS1553
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1552] = _M0L6_2atmpS1553;
      _M0L6_2atmpS1555 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1556 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1555;
      _M0L1nS257 = _M0L6_2atmpS1556;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1559 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1559;
      int32_t _M0L6_2atmpS1558 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1558;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1548;
  moonbit_string_t _result_2825;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1548
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1548);
  if (_M0L6_2atmpS1548.$1) {
    moonbit_decref(_M0L6_2atmpS1548.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2825 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2825;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1545;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1545 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1545);
  moonbit_decref_cycle_free(_M0L6_2atmpS1545);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1546;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1546 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1546);
  moonbit_decref_cycle_free(_M0L6_2atmpS1546);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1547;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1547 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1547);
  moonbit_decref_cycle_free(_M0L6_2atmpS1547);
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
  moonbit_string_t _M0L8_2afieldS2676;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2676 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2676);
  return _M0L8_2afieldS2676;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1544;
  int64_t _M0L6_2atmpS1543;
  struct _M0TPC16string10StringView _M0L6_2atmpS1542;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1544 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1543 = (int64_t)_M0L6_2atmpS1544;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1542
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1543);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1542);
  moonbit_decref_cycle_free(_M0L6_2atmpS1542.$0);
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
  int32_t _M0L6_2atmpS1526;
  int32_t _if__result_2826;
  int32_t _M0L6_2atmpS1534;
  int32_t _if__result_2827;
  int32_t _M0L6_2atmpS1536;
  int32_t _M0L6_2atmpS1537;
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
  _M0L6_2atmpS1526 = _M0Lm2loS236;
  if (_M0L6_2atmpS1526 > 0) {
    int32_t _M0L6_2atmpS1525 = _M0Lm2loS236;
    if (_M0L6_2atmpS1525 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1524 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1523 = _M0L4selfS235[_M0L6_2atmpS1524];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1523)) {
        int32_t _M0L6_2atmpS1522 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1521 = _M0L6_2atmpS1522 - 1;
        int32_t _M0L6_2atmpS1520 = _M0L4selfS235[_M0L6_2atmpS1521];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2826
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1520);
      } else {
        _if__result_2826 = 0;
      }
    } else {
      _if__result_2826 = 0;
    }
  } else {
    _if__result_2826 = 0;
  }
  if (_if__result_2826) {
    int32_t _M0L6_2atmpS1527 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1527 + 1;
  }
  _M0L6_2atmpS1534 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1534 > 0) {
    int32_t _M0L6_2atmpS1533 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1533 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1532 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1531 = _M0L4selfS235[_M0L6_2atmpS1532];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1531)) {
        int32_t _M0L6_2atmpS1530 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1529 = _M0L6_2atmpS1530 - 1;
        int32_t _M0L6_2atmpS1528 = _M0L4selfS235[_M0L6_2atmpS1529];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2827
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1528);
      } else {
        _if__result_2827 = 0;
      }
    } else {
      _if__result_2827 = 0;
    }
  } else {
    _if__result_2827 = 0;
  }
  if (_if__result_2827) {
    int32_t _M0L6_2atmpS1535 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1535 - 1;
  }
  _M0L6_2atmpS1536 = _M0Lm2loS236;
  _M0L6_2atmpS1537 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1536 >= _M0L6_2atmpS1537) {
    int32_t _M0L6_2atmpS1538 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1539 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1538,
                                                 .$2 = _M0L6_2atmpS1539};
  } else {
    int32_t _M0L6_2atmpS1540 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1541 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1540,
                                                 .$2 = _M0L6_2atmpS1541};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1519;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1519
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1519);
  if (_M0L6_2atmpS1519.$1) {
    moonbit_decref(_M0L6_2atmpS1519.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1518;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1518
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1518);
  if (_M0L6_2atmpS1518.$1) {
    moonbit_decref(_M0L6_2atmpS1518.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1517;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1517 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1517;
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
  int32_t _M0L6_2atmpS1516;
  struct _M0TPC16string10StringView _M0L6_2atmpS1514;
  struct _M0TPB6Logger _M0L6_2atmpS1515;
  moonbit_string_t _result_2828;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1516 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1514
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1516
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1515
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1514, _M0L6_2atmpS1515, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1514.$0);
  if (_M0L6_2atmpS1515.$1) {
    moonbit_decref(_M0L6_2atmpS1515.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2828 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2828;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1512;
  int32_t _M0L5startS1513;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1512 = _M0L4selfS218.$2;
  _M0L5startS1513 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1512 - _M0L5startS1513;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 82, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1509;
    int32_t _M0L5startS1511;
    int32_t _M0L6_2atmpS1510;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1493;
    int32_t _M0L6_2atmpS1494;
    int32_t _M0L6_2atmpS1495;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1509 = _M0L4selfS218.$0;
    _M0L5startS1511 = _M0L4selfS218.$1;
    _M0L6_2atmpS1510 = _M0L5startS1511 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1509[_M0L6_2atmpS1510];
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
        int32_t _M0L6_2atmpS1496;
        int32_t _M0L6_2atmpS1497;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1496 = _M0L1iS220 + 1;
        _M0L6_2atmpS1497 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1496;
        _M0L3segS221 = _M0L6_2atmpS1497;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1498;
        int32_t _M0L6_2atmpS1499;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1498 = _M0L1iS220 + 1;
        _M0L6_2atmpS1499 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1498;
        _M0L3segS221 = _M0L6_2atmpS1499;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1500;
        int32_t _M0L6_2atmpS1501;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1500 = _M0L1iS220 + 1;
        _M0L6_2atmpS1501 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1500;
        _M0L3segS221 = _M0L6_2atmpS1501;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1502;
        int32_t _M0L6_2atmpS1503;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1502 = _M0L1iS220 + 1;
        _M0L6_2atmpS1503 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1502;
        _M0L3segS221 = _M0L6_2atmpS1503;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1505;
          moonbit_string_t _M0L6_2atmpS1504;
          int32_t _M0L6_2atmpS1506;
          int32_t _M0L6_2atmpS1507;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
          _M0L6_2atmpS1505 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1504 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1505);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1504);
          moonbit_decref_cycle_free(_M0L6_2atmpS1504);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1506 = _M0L1iS220 + 1;
          _M0L6_2atmpS1507 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1506;
          _M0L3segS221 = _M0L6_2atmpS1507;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1508 = _M0L1iS220 + 1;
          int32_t _tmp_2831 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1508;
          _M0L3segS221 = _tmp_2831;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2830;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1493 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1493);
    _M0L6_2atmpS1494 = _M0L1iS220 + 1;
    _M0L6_2atmpS1495 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1494;
    _M0L3segS221 = _M0L6_2atmpS1495;
    continue;
    joinlet_2830:;
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
    int64_t _M0L6_2atmpS1492 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1491;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1491
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1492);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1491);
    moonbit_decref_cycle_free(_M0L6_2atmpS1491.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1489;
  int32_t _M0L5startS1490;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1467;
  int32_t _if__result_2832;
  int32_t _M0L6_2atmpS1477;
  int32_t _if__result_2833;
  int32_t _M0L6_2atmpS1479;
  int32_t _M0L6_2atmpS1480;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1489 = _M0L4selfS201.$2;
  _M0L5startS1490 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1489 - _M0L5startS1490;
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
  _M0L6_2atmpS1467 = _M0Lm2loS202;
  if (_M0L6_2atmpS1467 > 0) {
    int32_t _M0L6_2atmpS1466 = _M0Lm2loS202;
    if (_M0L6_2atmpS1466 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1465 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1464 = _M0L4baseS209 + _M0L6_2atmpS1465;
      int32_t _M0L6_2atmpS1463 = _M0L3strS208[_M0L6_2atmpS1464];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1463)) {
        int32_t _M0L6_2atmpS1462 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1461 = _M0L4baseS209 + _M0L6_2atmpS1462;
        int32_t _M0L6_2atmpS1460 = _M0L6_2atmpS1461 - 1;
        int32_t _M0L6_2atmpS1459 = _M0L3strS208[_M0L6_2atmpS1460];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2832
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1459);
      } else {
        _if__result_2832 = 0;
      }
    } else {
      _if__result_2832 = 0;
    }
  } else {
    _if__result_2832 = 0;
  }
  if (_if__result_2832) {
    int32_t _M0L6_2atmpS1468 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1468 + 1;
  }
  _M0L6_2atmpS1477 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1477 > 0) {
    int32_t _M0L6_2atmpS1476 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1476 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1475 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1474 = _M0L4baseS209 + _M0L6_2atmpS1475;
      int32_t _M0L6_2atmpS1473 = _M0L3strS208[_M0L6_2atmpS1474];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1473)) {
        int32_t _M0L6_2atmpS1472 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1471 = _M0L4baseS209 + _M0L6_2atmpS1472;
        int32_t _M0L6_2atmpS1470 = _M0L6_2atmpS1471 - 1;
        int32_t _M0L6_2atmpS1469 = _M0L3strS208[_M0L6_2atmpS1470];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2833
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1469);
      } else {
        _if__result_2833 = 0;
      }
    } else {
      _if__result_2833 = 0;
    }
  } else {
    _if__result_2833 = 0;
  }
  if (_if__result_2833) {
    int32_t _M0L6_2atmpS1478 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1478 - 1;
  }
  _M0L6_2atmpS1479 = _M0Lm2loS202;
  _M0L6_2atmpS1480 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1479 >= _M0L6_2atmpS1480) {
    int32_t _M0L6_2atmpS1484 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1481 = _M0L4baseS209 + _M0L6_2atmpS1484;
    int32_t _M0L6_2atmpS1483 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1482 = _M0L4baseS209 + _M0L6_2atmpS1483;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1481,
                                                 .$2 = _M0L6_2atmpS1482};
  } else {
    int32_t _M0L6_2atmpS1488 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1485 = _M0L4baseS209 + _M0L6_2atmpS1488;
    int32_t _M0L6_2atmpS1487 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1486 = _M0L4baseS209 + _M0L6_2atmpS1487;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1485,
                                                 .$2 = _M0L6_2atmpS1486};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1456;
  int32_t _M0L6_2atmpS1455;
  int32_t _M0L6_2atmpS1458;
  int32_t _M0L6_2atmpS1457;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1454;
  moonbit_string_t _result_2834;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1456 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1455
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1456);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1455);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1458 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1457
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1458);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1457);
  _M0L6_2atmpS1454 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2834 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1454);
  moonbit_decref_cycle_free(_M0L6_2atmpS1454);
  return _result_2834;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1451;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1451 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1451);
  } else {
    int32_t _M0L6_2atmpS1453;
    int32_t _M0L6_2atmpS1452;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1453 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1452 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1453, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1452);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1449;
  int32_t _M0L6_2atmpS1450;
  int32_t _M0L6_2atmpS1448;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1449 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1450 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1448 = _M0L6_2atmpS1449 - _M0L6_2atmpS1450;
  return _M0L6_2atmpS1448 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1446;
  int32_t _M0L6_2atmpS1447;
  int32_t _M0L6_2atmpS1445;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1446 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1447 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1445 = _M0L6_2atmpS1446 % _M0L6_2atmpS1447;
  return _M0L6_2atmpS1445 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1443;
  int32_t _M0L6_2atmpS1444;
  int32_t _M0L6_2atmpS1442;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1443 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1444 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1442 = _M0L6_2atmpS1443 / _M0L6_2atmpS1444;
  return _M0L6_2atmpS1442 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1440;
  int32_t _M0L6_2atmpS1441;
  int32_t _M0L6_2atmpS1439;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1440 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1441 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1439 = _M0L6_2atmpS1440 + _M0L6_2atmpS1441;
  return _M0L6_2atmpS1439 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1438;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1438 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1438;
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
  int32_t _M0L3lenS1437;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1432;
  int32_t _M0L6_2atmpS1431;
  int32_t _if__result_2835;
  uint16_t* _M0L4dataS1433;
  int32_t _M0L3lenS1434;
  int32_t _M0L3lenS1436;
  int32_t _M0L6_2atmpS1435;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1437 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1437 + _M0L8str__lenS182;
  _M0L4dataS1432 = _M0L4selfS185->$0;
  _M0L6_2atmpS1431 = Moonbit_array_length(_M0L4dataS1432);
  if (_M0L8requiredS184 > _M0L6_2atmpS1431) {
    _if__result_2835 = 1;
  } else {
    int32_t _M0L3lenS1430 = _M0L4selfS185->$1;
    _if__result_2835 = _M0L8requiredS184 < _M0L3lenS1430;
  }
  if (_if__result_2835) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1433 = _M0L4selfS185->$0;
  _M0L3lenS1434 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1433);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1433, _M0L3lenS1434, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1433);
  _M0L3lenS1436 = _M0L4selfS185->$1;
  _M0L6_2atmpS1435 = _M0L3lenS1436 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1435;
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
      int32_t _M0L6_2atmpS1427 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1428;
      int32_t _M0L6_2atmpS1429;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1427;
      _M0L6_2atmpS1428 = _M0L1iS176 + 1;
      _M0L6_2atmpS1429 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1428;
      _M0L1jS177 = _M0L6_2atmpS1429;
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
    int32_t _M0L3lenS1398 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1400 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1399 = Moonbit_array_length(_M0L4dataS1400);
    uint16_t* _M0L4dataS1403;
    int32_t _M0L3lenS1404;
    int32_t _M0L6_2atmpS1405;
    int32_t _M0L3lenS1407;
    int32_t _M0L6_2atmpS1406;
    if (_M0L3lenS1398 >= _M0L6_2atmpS1399) {
      int32_t _M0L3lenS1402 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1401 = _M0L3lenS1402 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1401);
    }
    _M0L4dataS1403 = _M0L4selfS171->$0;
    _M0L3lenS1404 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1403);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1405 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1404 < 0
      || _M0L3lenS1404 >= Moonbit_array_length(_M0L4dataS1403)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1403[_M0L3lenS1404] = _M0L6_2atmpS1405;
    moonbit_decref_cycle_free(_M0L4dataS1403);
    _M0L3lenS1407 = _M0L4selfS171->$1;
    _M0L6_2atmpS1406 = _M0L3lenS1407 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1406;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1411 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1409 = Moonbit_array_length(_M0L4dataS1411);
    int32_t _M0L3lenS1410 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1408 = _M0L6_2atmpS1409 - _M0L3lenS1410;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1414;
    int32_t _M0L3lenS1415;
    uint32_t _M0L6_2atmpS1418;
    uint32_t _M0L6_2atmpS1417;
    int32_t _M0L6_2atmpS1416;
    uint16_t* _M0L4dataS1419;
    int32_t _M0L3lenS1424;
    int32_t _M0L6_2atmpS1420;
    uint32_t _M0L6_2atmpS1423;
    uint32_t _M0L6_2atmpS1422;
    int32_t _M0L6_2atmpS1421;
    int32_t _M0L3lenS1426;
    int32_t _M0L6_2atmpS1425;
    if (_M0L6_2atmpS1408 < 2) {
      int32_t _M0L3lenS1413 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1412 = _M0L3lenS1413 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1412);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1414 = _M0L4selfS171->$0;
    _M0L3lenS1415 = _M0L4selfS171->$1;
    _M0L6_2atmpS1418 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1417 = 55296u + _M0L6_2atmpS1418;
    moonbit_incref_cycle_free(_M0L4dataS1414);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1416 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1417);
    if (
      _M0L3lenS1415 < 0
      || _M0L3lenS1415 >= Moonbit_array_length(_M0L4dataS1414)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1414[_M0L3lenS1415] = _M0L6_2atmpS1416;
    moonbit_decref_cycle_free(_M0L4dataS1414);
    _M0L4dataS1419 = _M0L4selfS171->$0;
    _M0L3lenS1424 = _M0L4selfS171->$1;
    _M0L6_2atmpS1420 = _M0L3lenS1424 + 1;
    _M0L6_2atmpS1423 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1422 = 56320u + _M0L6_2atmpS1423;
    moonbit_incref_cycle_free(_M0L4dataS1419);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1421 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1422);
    if (
      _M0L6_2atmpS1420 < 0
      || _M0L6_2atmpS1420 >= Moonbit_array_length(_M0L4dataS1419)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1419[_M0L6_2atmpS1420] = _M0L6_2atmpS1421;
    moonbit_decref_cycle_free(_M0L4dataS1419);
    _M0L3lenS1426 = _M0L4selfS171->$1;
    _M0L6_2atmpS1425 = _M0L3lenS1426 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1425;
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
  uint16_t* _M0L4dataS1397;
  int32_t _M0L6_2atmpS1395;
  int32_t _M0L3lenS1396;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1392;
  int32_t _M0L6_2atmpS1393;
  int32_t _M0L3lenS1394;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2677;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1397 = _M0L4selfS166->$0;
  _M0L6_2atmpS1395 = Moonbit_array_length(_M0L4dataS1397);
  _M0L3lenS1396 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1395, _M0L3lenS1396, _M0L8requiredS167);
  _M0L4dataS1392 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1392);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1393 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1394 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1392, _M0L13new__capacityS165, _M0L6_2atmpS1393, _M0L3lenS1394, 0, 0);
  _M0L6_2aoldS2677 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2677);
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
  int32_t _M0L6_2atmpS1391;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1391 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1391;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1390;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1390 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1390;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1381;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1381 = _M0L4selfS155->$1;
  if (_M0L3lenS1381 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1382 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1384 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1383 = Moonbit_array_length(_M0L4dataS1384);
    if (_M0L3lenS1382 == _M0L6_2atmpS1383) {
      uint16_t* _M0L4dataS1385 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1385);
      return _M0L4dataS1385;
    } else {
      uint16_t* _M0L4dataS1386 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1387 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1388;
      int32_t _M0L3lenS1389;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1386);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1388 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1389 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1386, _M0L3lenS1387, _M0L6_2atmpS1388, _M0L3lenS1389, 0, 0);
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
  int32_t _if__result_2838;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1377 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1378 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1377 <= _M0L6_2atmpS1378) {
            int32_t _M0L6_2atmpS1376 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2838 = _M0L6_2atmpS1376 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2838 = 0;
          }
        } else {
          _if__result_2838 = 0;
        }
      } else {
        _if__result_2838 = 0;
      }
    } else {
      _if__result_2838 = 0;
    }
  } else {
    _if__result_2838 = 0;
  }
  if (_if__result_2838) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1380;
    moonbit_string_t _M0L6_2atmpS1379;
    uint16_t* _result_2839;
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
    _M0L6_2atmpS1380 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1380);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1379
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2839 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1379);
    moonbit_decref_cycle_free(_M0L6_2atmpS1379);
    return _result_2839;
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
  struct _M0TPB13StringBuilder* _block_2840;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1375 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1375 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2840
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2840)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 87, 0);
  _block_2840->$0 = _M0L4dataS140;
  _block_2840->$1 = 0;
  return _block_2840;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1374;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1374 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1374;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2841;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1355 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1356;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1356
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS1355 <= _M0L6_2atmpS1356) {
            int32_t _M0L6_2atmpS1354 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2841 = _M0L6_2atmpS1354 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2841 = 0;
          }
        } else {
          _if__result_2841 = 0;
        }
      } else {
        _if__result_2841 = 0;
      }
    } else {
      _if__result_2841 = 0;
    }
  } else {
    _if__result_2841 = 0;
  }
  if (_if__result_2841) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1358;
    moonbit_string_t _M0L6_2atmpS1357;
    moonbit_string_t* _result_2842;
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
    _M0L6_2atmpS1358 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1358);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1357
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2842
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1357);
    moonbit_decref_cycle_free(_M0L6_2atmpS1357);
    return _result_2842;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2843;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1360 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1361;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1361
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS1360 <= _M0L6_2atmpS1361) {
            int32_t _M0L6_2atmpS1359 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2843 = _M0L6_2atmpS1359 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2843 = 0;
          }
        } else {
          _if__result_2843 = 0;
        }
      } else {
        _if__result_2843 = 0;
      }
    } else {
      _if__result_2843 = 0;
    }
  } else {
    _if__result_2843 = 0;
  }
  if (_if__result_2843) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1363;
    moonbit_string_t _M0L6_2atmpS1362;
    struct _M0TUsiE** _result_2844;
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
    _M0L6_2atmpS1363 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1363);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1362
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2844
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1362);
    moonbit_decref_cycle_free(_M0L6_2atmpS1362);
    return _result_2844;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2845;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1365 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1366;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1366
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
          if (_M0L6_2atmpS1365 <= _M0L6_2atmpS1366) {
            int32_t _M0L6_2atmpS1364 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2845 = _M0L6_2atmpS1364 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2845 = 0;
          }
        } else {
          _if__result_2845 = 0;
        }
      } else {
        _if__result_2845 = 0;
      }
    } else {
      _if__result_2845 = 0;
    }
  } else {
    _if__result_2845 = 0;
  }
  if (_if__result_2845) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1368;
    moonbit_string_t _M0L6_2atmpS1367;
    int32_t* _result_2846;
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
    _M0L6_2atmpS1368 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1368);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1367
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2846
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1367);
    moonbit_decref_cycle_free(_M0L6_2atmpS1367);
    return _result_2846;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2847;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1370 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1371;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1371
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS1370 <= _M0L6_2atmpS1371) {
            int32_t _M0L6_2atmpS1369 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2847 = _M0L6_2atmpS1369 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2847 = 0;
          }
        } else {
          _if__result_2847 = 0;
        }
      } else {
        _if__result_2847 = 0;
      }
    } else {
      _if__result_2847 = 0;
    }
  } else {
    _if__result_2847 = 0;
  }
  if (_if__result_2847) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1373;
    moonbit_string_t _M0L6_2atmpS1372;
    float* _result_2848;
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
    _M0L6_2atmpS1373 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1373);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1372
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2848
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1372);
    moonbit_decref_cycle_free(_M0L6_2atmpS1372);
    return _result_2848;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1351;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1351
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1351);
  if (_M0L6_2atmpS1351.$1) {
    moonbit_decref(_M0L6_2atmpS1351.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1352;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1352
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1352);
  if (_M0L6_2atmpS1352.$1) {
    moonbit_decref(_M0L6_2atmpS1352.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1353;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1353
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1353);
  if (_M0L6_2atmpS1353.$1) {
    moonbit_decref(_M0L6_2atmpS1353.$1);
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
        int32_t _M0L6_2atmpS1306 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1308 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1307;
        int32_t _M0L6_2atmpS1309;
        if (
          _M0L6_2atmpS1308 < 0
          || _M0L6_2atmpS1308 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1307 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1308];
        if (
          _M0L6_2atmpS1306 < 0
          || _M0L6_2atmpS1306 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1306] = _M0L6_2atmpS1307;
        _M0L6_2atmpS1309 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1309;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1314 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1314;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1310 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1312 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1311;
        int32_t _M0L6_2atmpS1313;
        if (
          _M0L6_2atmpS1312 < 0
          || _M0L6_2atmpS1312 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1311 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1312];
        if (
          _M0L6_2atmpS1310 < 0
          || _M0L6_2atmpS1310 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1310] = _M0L6_2atmpS1311;
        _M0L6_2atmpS1313 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1313;
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
        int32_t _M0L6_2atmpS1315 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1317 = _M0L11src__offsetS30 + _M0L1iS31;
        moonbit_string_t _M0L6_2atmpS1316;
        moonbit_string_t _M0L6_2aoldS2678;
        int32_t _M0L6_2atmpS1318;
        if (
          _M0L6_2atmpS1317 < 0
          || _M0L6_2atmpS1317 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1316 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1317];
        if (
          _M0L6_2atmpS1315 < 0
          || _M0L6_2atmpS1315 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2678 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1315];
        moonbit_incref_cycle_free(_M0L6_2atmpS1316);
        moonbit_decref_cycle_free(_M0L6_2aoldS2678);
        _M0L3dstS27[_M0L6_2atmpS1315] = _M0L6_2atmpS1316;
        _M0L6_2atmpS1318 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1318;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1323 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1323;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1319 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1321 = _M0L11src__offsetS30 + _M0L1iS34;
        moonbit_string_t _M0L6_2atmpS1320;
        moonbit_string_t _M0L6_2aoldS2679;
        int32_t _M0L6_2atmpS1322;
        if (
          _M0L6_2atmpS1321 < 0
          || _M0L6_2atmpS1321 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1320 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1321];
        if (
          _M0L6_2atmpS1319 < 0
          || _M0L6_2atmpS1319 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2679 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1319];
        moonbit_incref_cycle_free(_M0L6_2atmpS1320);
        moonbit_decref_cycle_free(_M0L6_2aoldS2679);
        _M0L3dstS27[_M0L6_2atmpS1319] = _M0L6_2atmpS1320;
        _M0L6_2atmpS1322 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1322;
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
        int32_t _M0L6_2atmpS1324 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1326 = _M0L11src__offsetS39 + _M0L1iS40;
        struct _M0TUsiE* _M0L6_2atmpS1325;
        struct _M0TUsiE* _M0L6_2aoldS2680;
        int32_t _M0L6_2atmpS1327;
        if (
          _M0L6_2atmpS1326 < 0
          || _M0L6_2atmpS1326 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1325 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1326];
        if (
          _M0L6_2atmpS1324 < 0
          || _M0L6_2atmpS1324 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2680 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1324];
        if (_M0L6_2atmpS1325) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1325);
        }
        if (_M0L6_2aoldS2680) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2680);
        }
        _M0L3dstS36[_M0L6_2atmpS1324] = _M0L6_2atmpS1325;
        _M0L6_2atmpS1327 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1327;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1332 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1332;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1328 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1330 = _M0L11src__offsetS39 + _M0L1iS43;
        struct _M0TUsiE* _M0L6_2atmpS1329;
        struct _M0TUsiE* _M0L6_2aoldS2681;
        int32_t _M0L6_2atmpS1331;
        if (
          _M0L6_2atmpS1330 < 0
          || _M0L6_2atmpS1330 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1329 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1330];
        if (
          _M0L6_2atmpS1328 < 0
          || _M0L6_2atmpS1328 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2681 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1328];
        if (_M0L6_2atmpS1329) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1329);
        }
        if (_M0L6_2aoldS2681) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2681);
        }
        _M0L3dstS36[_M0L6_2atmpS1328] = _M0L6_2atmpS1329;
        _M0L6_2atmpS1331 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1331;
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
        int32_t _M0L6_2atmpS1333 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1335 = _M0L11src__offsetS48 + _M0L1iS49;
        int32_t _M0L6_2atmpS1334;
        int32_t _M0L6_2atmpS1336;
        if (
          _M0L6_2atmpS1335 < 0
          || _M0L6_2atmpS1335 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1334 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1335];
        if (
          _M0L6_2atmpS1333 < 0
          || _M0L6_2atmpS1333 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1333] = _M0L6_2atmpS1334;
        _M0L6_2atmpS1336 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1336;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1341 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1341;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1337 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1339 = _M0L11src__offsetS48 + _M0L1iS52;
        int32_t _M0L6_2atmpS1338;
        int32_t _M0L6_2atmpS1340;
        if (
          _M0L6_2atmpS1339 < 0
          || _M0L6_2atmpS1339 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1338 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1339];
        if (
          _M0L6_2atmpS1337 < 0
          || _M0L6_2atmpS1337 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1337] = _M0L6_2atmpS1338;
        _M0L6_2atmpS1340 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1340;
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
        int32_t _M0L6_2atmpS1342 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1344 = _M0L11src__offsetS57 + _M0L1iS58;
        float _M0L6_2atmpS1343;
        int32_t _M0L6_2atmpS1345;
        if (
          _M0L6_2atmpS1344 < 0
          || _M0L6_2atmpS1344 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1343 = (float)_M0L3srcS55[_M0L6_2atmpS1344];
        if (
          _M0L6_2atmpS1342 < 0
          || _M0L6_2atmpS1342 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1342] = _M0L6_2atmpS1343;
        _M0L6_2atmpS1345 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1345;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1350 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1350;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1346 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1348 = _M0L11src__offsetS57 + _M0L1iS61;
        float _M0L6_2atmpS1347;
        int32_t _M0L6_2atmpS1349;
        if (
          _M0L6_2atmpS1348 < 0
          || _M0L6_2atmpS1348 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1347 = (float)_M0L3srcS55[_M0L6_2atmpS1348];
        if (
          _M0L6_2atmpS1346 < 0
          || _M0L6_2atmpS1346 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1346] = _M0L6_2atmpS1347;
        _M0L6_2atmpS1349 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1349;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1276) {
  switch (Moonbit_object_tag(_M0L4_2aeS1276)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1276);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 3: {
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
  void* _M0L11_2aobj__ptrS1301,
  struct _M0TPB4Show _M0L8_2aparamS1300
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1299 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1301;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1299, _M0L8_2aparamS1300);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1298,
  struct _M0TPB4Show _M0L8_2aparamS1297
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1296 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1298;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1296, _M0L8_2aparamS1297);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1295,
  int32_t _M0L8_2aparamS1294
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1293 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1295;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1293, _M0L8_2aparamS1294);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1292,
  struct _M0TPC16string10StringView _M0L8_2aparamS1291
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1290 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1292;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1290, _M0L8_2aparamS1291);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1289,
  moonbit_string_t _M0L8_2aparamS1286,
  int32_t _M0L8_2aparamS1287,
  int32_t _M0L8_2aparamS1288
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1285 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1289;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1285, _M0L8_2aparamS1286, _M0L8_2aparamS1287, _M0L8_2aparamS1288);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1284,
  moonbit_string_t _M0L8_2aparamS1283
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1282 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1284;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1282, _M0L8_2aparamS1283);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2859 = 9218868437227405311ll;
  int64_t _tmp_2860;
  int64_t _tmp_2861;
  int64_t _tmp_2862;
  int64_t _tmp_2863;
  _M0FPB18double__max__value = *(double*)&_tmp_2859;
  _tmp_2860 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2860;
  _tmp_2861 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2861;
  _tmp_2862 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2862;
  _tmp_2863 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2863;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1305;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1269;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1270;
  int32_t _M0L7_2abindS1271;
  struct _M0TUsiE** _M0L7_2abindS1272;
  int32_t _M0L6_2acntS2686;
  int32_t _M0L2__S1273;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1305
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1269
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1269)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 90, 0);
  _M0L12async__testsS1269->$0 = _M0L6_2atmpS1305;
  _M0L12async__testsS1269->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1270
  = _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1271 = _M0L7_2abindS1270->$1;
  _M0L7_2abindS1272 = _M0L7_2abindS1270->$0;
  _M0L6_2acntS2686
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1270));
  if (_M0L6_2acntS2686 > 1) {
    int32_t _M0L11_2anew__cntS2687 = _M0L6_2acntS2686 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1270), _M0L11_2anew__cntS2687);
    moonbit_incref_cycle_free(_M0L7_2abindS1272);
  } else if (_M0L6_2acntS2686 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1270);
  }
  _M0L2__S1273 = 0;
  while (1) {
    if (_M0L2__S1273 < _M0L7_2abindS1271) {
      struct _M0TUsiE* _M0L3argS1274 =
        (struct _M0TUsiE*)_M0L7_2abindS1272[_M0L2__S1273];
      moonbit_string_t _M0L6_2atmpS1302 = _M0L3argS1274->$0;
      int32_t _M0L6_2atmpS1303 = _M0L3argS1274->$1;
      int32_t _M0L6_2atmpS1304;
      moonbit_incref_cycle_free(_M0L6_2atmpS1302);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples23if__net__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1269, _M0L6_2atmpS1302, _M0L6_2atmpS1303);
      moonbit_decref_cycle_free(_M0L6_2atmpS1302);
      _M0L6_2atmpS1304 = _M0L2__S1273 + 1;
      _M0L2__S1273 = _M0L6_2atmpS1304;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1272);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples23if__net__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23if__net__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1269);
  moonbit_decref_cycle_free(_M0L12async__testsS1269);
  moonbit_flush_cycles();
  return 0;
}